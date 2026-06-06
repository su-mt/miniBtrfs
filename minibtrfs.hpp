
#ifndef MINIBTRFS_HPP_
#define MINIBTRFS_HPP_






#include "btree.hpp"
#include "fs.hpp"
#include <fcntl.h>


namespace minibtrfs {

class MiniBtrfs {

    int fd;
    btree::BTree tree; 
    u64 next_inode_id = 257;



public:

    MiniBtrfs (const char* filename) : fd(open(filename, O_RDWR)), tree(fd) {
        if (fd < 0) {
            throw std::runtime_error("Failed to open device");
        }
        if (!inspectFS()) {
            throw std::runtime_error("Failed to read fs. Ensure that device was formatted via make fs");
        }
    }

    void mkdir (u64 parent_id, const char* name);
    void ls(u64 dir_id);
    void create_file(u64 parent_id, const char* name);
    [[nodiscard]] u64 cd(u64 current_dir_id, const char* name) const;

    bool inspectFS();



};

} // minibtrfs
using minibtrfs::MiniBtrfs ;




#endif // MINIBTRFS_HPP_