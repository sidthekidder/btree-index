#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <cmath>
using namespace std;

// block size is constant at 1024 KB
int block_size = 1024;

// global variables that are populated by reading the first metadata block and re-used across the program
int degree;
int key_len;
long root_address;
string index_filename;
string data_filename;

/* class representing a B+ tree node 
 *
 * member variables:
 * address (long) - stores the offset in the index file where this node is written (-1 when not written)
 * is_leaf (boolean) - flag representing whether this is a leaf node or internal node
 * keys (vector<string>) - list of strings representing keys stored by this internal/leaf node
 * children (vector<long>) - list of offsets representing pointers to children nodes stored by this internal node
 * pointers (vector<long>) - list of offsets representing pointers to file offsets stored by this leaf node
 * next (long) - offset representing pointer to next sibling node for this leaf (-1 for internal or uninitialized nodes)
 * prev (long) - offset representing pointer to prev sibling node for this leaf (-1 for internal or uninitialized nodes)

 */
class Node
{
    public:
    long address;
    bool is_leaf;
    vector<string> keys;
    vector<long> children;
    vector<long> pointers;
    long next; // address of next block
    long prev; // address of prev block

    /* read the Node from a specific address */
    Node(long addr)
    {
        address = addr;
        read_from_disk();
    }

    /* constructor for node with a single value */
    Node(bool leaf, string key, long val, long ptr)
    {
        is_leaf = leaf;
        next = -1;
        prev = -1;
        address = -1;

        keys.push_back(key);

        if (leaf)
            pointers.push_back(val);
        else
            children.push_back(ptr);
    }

    /* constructor for nodes with multiple values */
    Node(bool leaf, vector<string> keys_, vector<long> vals, vector<long> ptrs)
    {
        is_leaf = leaf;
        next = -1;
        prev = -1;
        address = -1;

        keys = keys_;

        if (leaf)
            pointers = vals;
        else
            children = ptrs;
    }

    /* write a Node object to memory at the specified 'address'. Written either at
     * 1.'address' if exists already then overwrite that block for block_size
     * 2. append to end of file
     */
    void write_to_disk()
    {
        if (address == -1)
        {
            // get end of file offset
            ifstream infile;
            infile.open(index_filename, ios::in | ios::binary);
            infile.seekg(0, ios::end);
            address = infile.tellg();
            infile.close();
        }

        long offset = 0;
        char buffer[1024];

        // write is_leaf bool
        memcpy(buffer + offset, &is_leaf, sizeof(is_leaf));
        offset += sizeof(is_leaf);

        // write next pointer
        memcpy(buffer + offset, &next, sizeof(next));
        offset += sizeof(next);

        // write prev pointer
        memcpy(buffer + offset, &prev, sizeof(prev));
        offset += sizeof(prev);

        // write number of keys in this node
        long keys_size = keys.size();
        memcpy(buffer + offset, &keys_size, sizeof(keys_size));
        offset += sizeof(keys_size);

        // write keys
        for (string key : keys) 
        {
            memcpy(buffer + offset, key.c_str(), strlen(key.c_str()) + 1);
            offset += strlen(key.c_str()) + 1;
        }

        // Add the child pointers to memory
        if (is_leaf == false) // internal node - write long children 
        {
            for (long child : children) 
            {
                memcpy(buffer + offset, &child, sizeof(child));
                offset += sizeof(child);
            }
        } 
        else  // leaf node - write long pointers
        {
            for (long ptr : pointers) 
            {
                memcpy(buffer + offset, &ptr, sizeof(ptr));
                offset += sizeof(ptr);
            }
        }

        ofstream outfile;
        outfile.open(index_filename, ios::out | ios::binary | ios::in);
        outfile.seekp(address, ios::beg);
        outfile.write(buffer, block_size);
        outfile.close();

        flush_node();
    }

    /* delete the current Node from memory */
    void flush_node()
    {
        is_leaf = false;
        next = -1;
        prev = -1;
        keys.clear();
        children.clear();
        pointers.clear();
    }

    /* read Node object from index file at 'address' */
    void read_from_disk()
    {
        if (address <= 0) // block hasn't been written to disk yet, can't read it
            return;

        char *buf = new char[block_size];
        long offset = 0;

        // Open the binary file and read into memory
        ifstream infile;
        infile.open(index_filename, ios::in | ios::binary);
        infile.seekg(address);
        infile.read(buf, block_size);
        infile.close();

        // read is_leaf bool 
        memcpy(&is_leaf, buf + offset, sizeof(bool));
        offset += sizeof(is_leaf);

        // read next addr pointer
        memcpy(&next, buf + offset, sizeof(long));
        offset += sizeof(long);

        // read prev addr pointer
        memcpy(&prev, buf + offset, sizeof(long));
        offset += sizeof(long);

        // read the number of keys stored
        long keys_size;
        memcpy(&keys_size, buf + offset, sizeof(keys_size));
        offset += sizeof(keys_size);

        // read the keys
        keys.clear();
        for (int i = 0 ; i < keys_size ; i++) 
        {
            string key(buf + offset, key_len); // keylen
            offset += key_len + 1; // keylen + 1 to account for '\0' character
            keys.push_back(key);
        }

        if (is_leaf == false) // internal node - read children 
        {
            children.clear();
            for (int i = 0 ; i < keys_size + 1 ; i++) 
            {
                long child;
                memcpy(&child, buf + offset, sizeof(child));
                offset += sizeof(child);
                children.push_back(child);
            }
        } 
        else // leaf node - read pointers
        {
            pointers.clear();
            for (long i = 0 ; i < keys_size ; i++) 
            {
                long pointer;
                memcpy(&pointer, buf + offset, sizeof(pointer));
                offset += sizeof(pointer);
                pointers.push_back(pointer);
            }
        }
    }

    /* bring the ith child of an internal node into memory 
     * 
     * input parameters:
     * idx (int) - position of the child to bring into memory
     * 
     * output (Node *) - reference to the ith child now brought into memory
     */
    Node* get_child(int idx)
    {
        long address = children[idx];
        Node *n = new Node(address);
        return n;
    }
};

/* signature for update_metadata function */
void update_metadata();

/* create a new node with 2*degree+1 keys and 2*degree+2 pointers and remove 'degree' keys from leaf
 *
 * input parameters:
 * index (Node *) - the internal node to be split
 * parent_key (Node *) - store the middle key (middle of the split) in this variable
 *
 * output (Node *) - the node that was created that contains half the keys (degree) of the internal node
 */
Node* split_index_node(Node* index, string &parent_key) 
{
    // splitting internal node - has (2*degree + 1) keys and (2*degree + 2) pointers
    parent_key = index->keys[degree];
    index->keys.erase(index->keys.begin() + degree);
    
    // keep first degree keys and degree+1 pointers
    // move degree keys and degree+1 pointers to new node
    vector<string> new_keys;
    vector<long> new_children;

    new_children.push_back(index->children[degree + 1]);
    index->children.erase(index->children.begin() + degree + 1);
    
    while (index->keys.size() > degree) 
    {
        new_keys.push_back(index->keys[degree]);
        index->keys.erase(index->keys.begin() + degree);
        new_children.push_back(index->children[degree + 1]);
        index->children.erase(index->children.begin() + degree + 1);
    }

    vector<long> v1;
    Node* new_node = new Node(false, new_keys, v1, new_children);

    return new_node;
}

/* create a new node with 'degree' keys and remove 'degree' keys from leaf
 *
 * input parameters:
 * leaf (Node *) - the leaf to be split
 *
 * output (Node *) - the node that was created that contains half the keys (degree) of the leaf
 */
Node* split_leaf_node(Node* leaf) 
{
    vector<string> new_keys;
    vector<long> new_pointers;
    
    // move 'degree' entries to the new node
    for(int i = degree ; i <= 2*degree ; i++) 
    {
        new_keys.push_back(leaf->keys[i]);
        new_pointers.push_back(leaf->pointers[i]);
    }
    
    // keep first 'degree' entries in the original leaf node
    for(int i = degree ; i <= 2*degree ; i++) 
    {
        leaf->keys.pop_back();
        leaf->pointers.pop_back();
    }
    
    vector<long> v1;
    Node* new_node = new Node(true, new_keys, new_pointers, v1);
    
    return new_node;
}

/* inserts a key-offset pair in the bplus tree
 *
 * input parameters:
 * root (Node *) - the current node being inserted in or probed
 * key (string) - the key to be inserted
 * offset (long integer) - the offset in the data file where the key can be found
 *
 * output (Node *) - a pointer to a new node if root was split or NULL in the general case
 */
Node* insert_record_in_btree(Node* root, string key, long offset)
{
    root->read_from_disk(); // bring root into the memory buffer
    if(!root->is_leaf) // root is internal node
    {
        // find the position of the first key which is greater than key to insert
        Node* index = root;
        int posn_key = 0;
        while (posn_key < index->keys.size()) 
        {
            if (key.compare(index->keys[posn_key]) < 0) 
                break;
            posn_key++;
        }

        // insert this entry recursively in the ith child pointer of this internal node
        Node* newchild = insert_record_in_btree(index->get_child(posn_key), key, offset);
        
        if(newchild == NULL) // no splitting occurred in this node's child
        {
            return NULL;
        } 
        else // splitting occurred, now add a new pointer to this internal node
        {
            // find the first corresponding pointer whose key is greater than newchild_key
            int key_idx = 0;
            string newchild_key = newchild->keys[0];
            while (key_idx < index->keys.size()) 
            {
                if(newchild_key.compare(index->keys[key_idx]) < 0) 
                    break;
                key_idx++;
            }

            // check if we have to 
            if (key_idx >= index->keys.size()) 
            {
                index->keys.push_back(newchild_key);
                index->children.push_back(newchild->address);
            }
            else 
            {
                index->keys.insert(index->keys.begin() + key_idx, newchild_key);
                index->children.insert(index->children.begin() + key_idx + 1, newchild->address);
            }

            // insert the new pointer in this node as it has space remaining
            if (index->keys.size() <= 2 * degree)
            {
                index->write_to_disk(); // write out to file and delete from buffer
                return NULL;
            }
            else // split this node because it's full
            {
                // original node is index and new node is new_child
                string parent_key = "";
                newchild = split_index_node(index, parent_key);

                // root was just split
                if (index->address == root_address) 
                {
                    // create a new node new_root containing index and newchild nodes as pointers
                    // and make the root bptree's pointer point to new_root
                    index->write_to_disk();
                    newchild->write_to_disk();

                    // add the index and newchild as children of the new_root
                    vector<long> new_children;
                    new_children.push_back(index->address);
                    new_children.push_back(newchild->address);

                    vector<long> v1; // empty variable that will be ignored in the constructor
                    vector<string> newkeys;
                    newkeys.push_back(parent_key);

                    // create the new_root
                    Node* new_root = new Node(false, newkeys, v1, new_children);
                    new_root->write_to_disk();

                    // update the root_address and update the first metadata block using update_metadata()
                    root_address = new_root->address;
                    update_metadata();

                    return NULL;
                }
                return newchild;
            }
        }
    }
    else // root is leaf node
    {
        Node* leaf = root;
        
        // find the position of the first key which is greater than key to insert
        if (key.compare(leaf->keys[0]) < 0) // insert at the beginning
        {
            leaf->keys.insert(leaf->keys.begin(), key);
            leaf->pointers.insert(leaf->pointers.begin(), offset);
        } 
        else if (key.compare(leaf->keys[leaf->keys.size() - 1]) > 0) // insert at the end
        {
            leaf->keys.push_back(key);
            leaf->pointers.push_back(offset);
        } 
        else // insert somewhere in between
        {
            for(int key_idx = 0 ; key_idx < leaf->keys.size() ; key_idx++)
            {
                if (leaf->keys[key_idx].compare(key) > 0) {
                    leaf->keys.insert(leaf->keys.begin() + key_idx, key);
                    leaf->pointers.insert(leaf->pointers.begin() + key_idx, offset);
                    break;
                }
            }
        }

        // since this leaf has space, insert this entry recursively in the ith position
        if(leaf->keys.size() <= 2 * degree) 
        {
            leaf->write_to_disk();
            return NULL;
        }
        else // splitting occurred, now add a new pointer to this leaf node
        {
            Node* newchild = split_leaf_node(leaf);

            if (leaf->address == root_address) // if this leaf was the root, make a new root
            {
                vector<string> newkeys;
                newkeys.push_back(newchild->keys[0]);

                // set prev/next siblings - point prev's next and next's prev to newchild
                long tmp = leaf->next;
                newchild->prev = leaf->address;
                newchild->next = tmp;
                if (tmp != -1) 
                {
                    Node *n = new Node(tmp);
                    n->prev = newchild->address; // check against NULL next pointer
                    n->write_to_disk();
                }

                newchild->write_to_disk();
                leaf->next = newchild->address;
                leaf->write_to_disk();

                // add leaf and newchild pointers to our new_root
                vector<long> new_children;
                new_children.push_back(leaf->address);
                new_children.push_back(newchild->address);
                vector<long> v1;

                // create the new_root
                Node* new_root = new Node(false, newkeys, v1, new_children);
                new_root->is_leaf = false;
                new_root->write_to_disk();

                // update the root_address and update the first metadata block using update_metadata()
                root_address = new_root->address;
                update_metadata();

                return NULL;
            }
            else
            {
                newchild->write_to_disk();
                newchild->read_from_disk(); // write and read to get a valid address for newchild

                // set prev/next siblings - point prev's next and next's prev to newchild
                long tmp = leaf->next;
                leaf->next = newchild->address;
                newchild->prev = leaf->address;
                newchild->next = tmp;
                if (tmp != -1) 
                {
                    Node *n = new Node(tmp);
                    n->prev = newchild->address; // check against NULL next pointer
                    n->write_to_disk();
                }

                newchild->write_to_disk();
                leaf->write_to_disk();
            }
            return newchild;
        }
    }
}

/* find a record in the current index_filename
 *
 * input parameters:
 * root (Node *) - pointer to current node being searched
 * key (string) - key to search for
 *
 * output (long int) - the offset of the key or -1
 */
long find_record(Node* root, string key)
{
    if (root == NULL)
        return NULL;

    root->read_from_disk();
    if (root->is_leaf) // reached a leaf node
    {
        // iterate through all values of this node
        for(int key_idx = 0 ; key_idx < root->keys.size() ; key_idx++)
        {
            if (key.compare(root->keys[key_idx]) == 0) // if any key matches exactly, return it
            {
                long p = root->pointers[key_idx];
                root->flush_node();
                return p;
            }
        }
        return -1; // return -1 if no key matches
    }
    else // route the search query in internal nodes after comparing key
    {
        // if search key is smaller than ith key of internal node
        for(int key_idx = 0 ; key_idx < root->keys.size() ; key_idx++)
        {
            // go down the child pointer whose corresponding key is less than the target
            if (key.compare(root->keys[key_idx]) < 0)
            {
                Node* c = root->get_child(key_idx);
                root->flush_node();
                return find_record(c, key);
            }
        }
        Node* c = root->get_child(root->children.size() - 1);
        root->flush_node();
        return find_record(c, key);  // follow rightmost pointer
    }
}

/* helper function for printing a record at a specified offset in the data_filename */
void print_record_at_offset(long key_offset)
{
    // open file at offset address, read till end of line
    char buf[1001] = "";
    ifstream infile(data_filename);
    infile.seekg(key_offset, infile.beg);
    infile.read(buf, 1000);
    infile.close();
    string str(buf);
    cout << str.substr(0, str.find("\n")) << endl;
}

/* helper function for list_records() */
void list_records_count(Node* root, string target_key, int count)
{
    root->read_from_disk(); // bring the current node into the buffer
    if (root->is_leaf) // if its a leaf
    {
        for(int i = 0 ; i < root->keys.size() ; i++)
        {
            // if any key matches or our target is between any 2 consecutive keys or there's only 1 key
            if (target_key.compare(root->keys[i]) == 0 || 
                (i > 0 && target_key.compare(root->keys[i-1]) < 0 && target_key.compare(root->keys[i]) > 0) || 
                (root->keys.size() == 1))
            {
                // start printing from here till 'count' next nodes
                while (root && count > 0)
                {
                    for(int a = i ; a < root->keys.size() && count > 0 ; a++)
                    {
                        cout << "[" << root->pointers[a] << "]: ";
                        count--;
                        print_record_at_offset(root->pointers[a]);
                    }
                    cout << endl;

                    if (root->next == -1) break; // follow the next pointer to a sibling leaf node
                    long next_root = root->next;
                    root->flush_node();
                    root = new Node(next_root);
                }
                return;
            }
        }
    }
    else
    {
        // if search key is smaller than ith key of internal node
        for(int key_idx = 0 ; key_idx < root->keys.size() ; key_idx++)
        {
            if (target_key.compare(root->keys[key_idx]) < 0) // go down the child pointer whose corresponding key is smaller
            {
                Node* c = root->get_child(key_idx);
                root->flush_node();
                return list_records_count(c, target_key, count);
            }
        }

        // didn't match any - going for last child pointer
        Node* c = root->get_child(root->children.size() - 1);
        root->flush_node();
        return list_records_count(c, target_key, count); // follow rightmost pointer
    }
}

/* fills the global variables after reading data from the first metadata block
 *
 * output (void) -reads the metadata block at address 0 only
 */
void initialize_bplus_tree()
{
    // read first 1024kb block to get the data filename, keylength and degree
    long offset = 0;
    char buffer[block_size];

    ifstream infile;
    infile.open(index_filename, ios::in | ios::binary);
    infile.read(buffer, block_size);
    infile.close();

    // read data_filename
    string get_data_filename(buffer, 257);
    int end_idx = get_data_filename.find("0000"); // assumming no filename has 4 0s
    data_filename = get_data_filename.substr(0, end_idx);
    offset += 257;

    // read key_len
    memcpy(&key_len, buffer + offset, sizeof(key_len));
    offset += sizeof(key_len);

    // read degree
    memcpy(&degree, buffer + offset, sizeof(degree));
    offset += sizeof(degree);

    // read root location
    memcpy(&root_address, buffer + offset, sizeof(root_address));
    offset += sizeof(root_address);
}

/* create or update an index file and the first metadata block at position 0
 *
 * input parameters:
 * data_file (string) - the file containing all the records to insert
 * index_file (string) - the index file that will store bplus tree key + offsets
 * keylen (int) - the key length of the file
 * new_root_address (long int) - the root address to be written - used only if the global root_address is not initialized
 * update_flag (bool) - specifies whether we are creating the index for the first time or just updating it
 *
 * output (void) - creates or updates the index
 */
void create_index(string data_file, string index_file, int keylen, long new_root_address, bool update_flag=false)
{
    /* create index file with one 1024kb block
     * structure:
     * data filename (256 bytes)
     * key length (4 bytes - int)
     * degree (4 bytes - int)
     * root_address (8 bytes - long)
     */
    long offset = 0;
    char buffer[block_size];

    // write filename in first 256 bytes
    string filename = data_file.append(string((256 - data_file.length()), '0'));
    memcpy(buffer + offset, filename.c_str(), strlen(filename.c_str()) + 1);
    offset += strlen(filename.c_str()) + 1;

    // write keylen
    memcpy(buffer + offset, &keylen, sizeof(keylen));
    offset += sizeof(keylen);

    // calculate degree of a node (a node can store degree <= n <= 2*degree key-value pairs)
    // assume 50 bytes for metadata (on the safe side)
    // the exact number of bytes used in a block apart from records = 25 bytes (3 longs and a bool)
    int degree = (block_size - 50)/ ((keylen+8)*2); // each record is key_length bytes + 8 bytes for a long

    // write degree
    memcpy(buffer + offset, &degree, sizeof(degree));
    offset += sizeof(degree);

    // write root address with default as 1024 otherwise use the global variable value
    if (new_root_address == -1) 
    {
        new_root_address = 1024;
        root_address = 1024;
    }
    memcpy(buffer + offset, &new_root_address, sizeof(new_root_address));
    offset += sizeof(new_root_address);

    // copy buffer to file
    ofstream outfile;
    // we open the output file as ios::in and ios::out when we're updating it
    // but open only as ios::out when we're creating it for the first time
    if (update_flag) 
        outfile.open(index_file, ios::in | ios::out | ios::binary);
    else
        outfile.open(index_file, ios::out | ios::binary);
    outfile.write(buffer, block_size);
    outfile.close();

    if (update_flag) // if this was just an update then no need to insert everything again
        return;

    // initialize the root of the bplus tree
    index_filename = index_file;
    initialize_bplus_tree();

    // iterate through the data file and keep inserting records into index
    ifstream infile(data_filename);
    string line;
    offset = 0;

    int count = 0;
    Node* root;
    bool first_time = true;

    // for each record
    while (getline(infile, line)) 
    {
        // insert pair(string, offset) in records array and update offset
        string key = line.substr(0, key_len);
        if (first_time) // for the first insert, create a root otherwise read the root_address
        {
            root = new Node(true, key, offset, NULL);
            first_time = false;
        }
        else
        {
            root = new Node(root_address);
        }

        insert_record_in_btree(root, key, offset);
        offset = infile.tellg();;
        count++;
    }
    cout << "Successfully inserted " << count << " records in index file b+ tree." << endl;
}

/* find an exact target key in the specified index file and print its offset or a message if not found
 *
 * input parameters:
 * index_file (string) - the index file we will search through
 * target_key (string) - the key to search for
 *
 * output (void) -finds the record
 */
void find_index(string index_file, string target_key)
{
    index_filename = index_file;
    initialize_bplus_tree();
    // TODO - check if empty index file then return -1

    Node* root = new Node(root_address);

    // if key supplied is longer than key_len, truncate it or pad it with blanks
    if (target_key.length() > key_len) 
        target_key = target_key.substr(0, key_len);
    else if (target_key.length() < key_len)
    {
        while (target_key.length() < key_len)
            target_key = target_key + " ";
    }

    long key_offset = find_record(root, target_key);
    if (key_offset == -1)
        cout << "Cannot find specified record in index.\n";
    else
        print_record_at_offset(key_offset);
}

/* inserts a new string into the specified index file
 * first insert this record in the data file before inserting its pointer in the index file
 *
 * input parameters:
 * index_file (string) - the index file we will search through
 * initial_key (string) - the key to insert
 *
 * output: void (inserts the record)
 */
void insert_record(string index_file, string initial_key)
{
    index_filename = index_file;
    initialize_bplus_tree();

    if (key_len > initial_key.length())
    {
        cout << "Input Error: key supplied is too short\n";
        return;
    }
    string key = initial_key.substr(0, key_len);
    Node* root = new Node(root_address);

    // if key doesn't exist in index file, first insert record in data file then insert that key+its offset in bptree
    if (find_record(root, key) != -1)
    {
        cout << "Key already exists in the index.\n";
        return;
    }
    else
    {
        // append record at the end of the data file and then insert normally into index
        ofstream outfile;
        outfile.open(data_filename, ios::out | ios::binary | ios::app);
        long key_offset = outfile.tellp();
        initial_key = "\n" + initial_key;

        cout << "Inserting \"" << initial_key << "\" at line number: " << key_offset << endl;

        outfile.write(initial_key.c_str(), initial_key.length());
        outfile.close();
        insert_record_in_btree(root, key, key_offset + 1); // add 1 to account for newline
    }
}

/* list a variable number of records starting from a specified key into the specified index file
 *
 * input parameters:
 * index_file (string) - the index file we will search through
 * target_key (string) - the target key (or next largest) to start the listing from
 * count (int) - the number of records to show following the target_key
 *
 * output: void (prints the records)
 */
void list_records(string index_file, string target_key, int count)
{
    index_filename = index_file;
    initialize_bplus_tree();
    Node* root = new Node(root_address);

    list_records_count(root, target_key, count);
}

/* updates the root address whenever it may have changed (during splitting) */
void update_metadata()
{
    create_index(data_filename, index_filename, key_len, root_address, true);
}

int main(int argc, char **argv)
{
    if (argc < 4 || argc > 6)
    {
        cout << "Incorrect number of arguments\n";
        return 0;
    }

    string choice(argv[1]);

    if (choice.compare("-create") == 0) // ./a.out -create data.txt data1.indx 15
    {
        string data_filename(argv[2]);
        if (data_filename.length() > 256)
        {
            cout << "Data file name too long, please keep it less than 256 characters\n";
            return 0;
        }
        string index_file(argv[3]);
        int keylen = stoi(argv[4]);
        create_index(data_filename, index_file, keylen, -1, false);
    }
    else if (choice.compare("-find") == 0) // ./a.out -find data1.indx 11111111111111A 
    {
        string index_file(argv[2]);
        string target_key(argv[3]);
        find_index(index_file, target_key);
    }
    else if (choice.compare("-insert") == 0) // ./a.out -insert MyIndex.indx "64541668700164B Some new Record"
    {
        string index_file(argv[2]);
        string target_key(argv[3]);
        insert_record(index_file, target_key);
    }
    else if (choice.compare("-list") == 0) // ./a.out -list <index filename> <starting key> <count>
    {
        string index_file(argv[2]);
        string target_key(argv[3]);
        int count = stoi(argv[4]);
        list_records(index_file, target_key, count);
    }
    return 0;
}
