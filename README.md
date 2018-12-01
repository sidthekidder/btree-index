# btree-index
This program accepts input as a file of text records and builds a B+ tree written 
to disk that provides fast search and list capabilities. The B+ tree supports 
insertion (not deletion) and holds only 4-5 nodes in memory at any given time.
It regularly flushes objects from memory to ensure fast and efficient access for 
millions of records. 

The Node class contains all the logic for writing and reading to file. The
initialize_bplus_tree() function populates the global variables after reading the
index file for metadata. The first 1024 bytes of the index file store metadata. 
Subsequent blocks contain 1 node each. Each node may be either an internal or
a leaf node. Internal nodes have the children array populated to other nodes.
Leaf nodes have the pointer array populated pointing to file blocks. The number of
nodes per block (degree) is determined dynamically and is equal to the block size
(1024) divided by the key_length + pointer size.

Supports the following commands - 
- Create an index
- Find a record by key
- Insert a new text record
- List n sequential records
