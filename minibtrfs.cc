#include "btree.hpp"
#include "fs.hpp"


namespace MiniBtrfs {

class MiniBtrfs {

    btree::BTree tree; 
    int fd;
    u64 next_inode_id = 257;


public:

    MiniBtrfs (const char* filename) {
        
    }

};


} // MiniBtrfs