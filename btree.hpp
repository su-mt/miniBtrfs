
#ifndef BTREE_HPP_
#define BTREE_HPP_


#include <cstdio>
#include <cstring>
#include <optional>
#include <unistd.h>

#include "fs.hpp"

namespace btree {


struct [[gnu::packed]] Header {
    u8 checksum_[32]; 
    fs::uuid_t fsid_;     
    u8 level_;
    u32 nritems_;
    // u8 padding_[17] = {0};


    Header () {
        std::memset(this, 0, sizeof(Header));
    }

    Header(fs::uuid_t fsid, u8 level) : fsid_(fsid), level_(level), nritems_(0) { }
};

struct [[gnu::packed]] Item {
    Key key_;
    u64 data_offset_; 
    u32 data_size_;   
};

struct [[gnu::packed]] KeyPtr {
    Key key_;
    u64 addr_;
    u64 gen_;
};

constexpr u16 DATA_SIZE = BLOCK_SIZE - sizeof(Header);
constexpr u16 ITEMS_SIZE = DATA_SIZE / sizeof(Item);
constexpr u16 PTRS_SIZE = DATA_SIZE / sizeof(KeyPtr);

struct [[gnu::packed]] Node {
    Header hdr_;
    u8 data_[DATA_SIZE];

    Node() {
        std::memset(this, 0, BLOCK_SIZE);
    }

    Node(int fd) {
        read(fd, this, BLOCK_SIZE);
    }

    Node(int fd, u64 addr) {
        lseek(fd, addr, SEEK_SET);
        read(fd, this, BLOCK_SIZE);
    }

    Node (fs::uuid_t uuid, int level) : hdr_(uuid, level)  {
        std::memset(data_, 0, DATA_SIZE);
    }

    inline bool is_item() const {
        return hdr_.level_ == 0;
    }
    
    inline bool is_ptr() const {
        return !is_item();
    }    
    
    inline Item& item(u32 i) {
        return *reinterpret_cast<Item*>(&data_[i * sizeof(Item)]);
    }
    
    inline const Item& item(u32 i) const {
        return *reinterpret_cast<const Item*>(&data_[i * sizeof(Item)]);
    }

    inline KeyPtr& ptr(u32 i) {
        return *reinterpret_cast<KeyPtr*>(&data_[i * sizeof(KeyPtr)]);
    }
    
    inline const KeyPtr& ptr(u32 i) const {
        return *reinterpret_cast<const KeyPtr*>(&data_[i * sizeof(KeyPtr)]);
    }
};
static_assert(sizeof(Node) == 4096, "Sizeof node must be 4k");



class BTree {
    fs::SuperBlock sb;
    Node root;
    int fd;

    std::optional<Item> searchR(const Node& h, Key v, int ht) const ;
    std::optional<KeyPtr> insertR(Node& h, const Item& x, int ht, u64 current_blk_addr) ;

    KeyPtr split(Node& h, u64 addr) ;



public:

    std::optional<Item> search(Key key) const ;
    void insert(Item item);

    BTree () = delete;
    BTree (fs::SuperBlock sb, btree::Node root, int fd) : sb(sb), root(root), fd(fd) {}
    BTree (int fd) : fd(fd) { load();}

    inline void load() {
        read(fd, &sb, sizeof(sb));
        lseek(fd, sb.root_tree_root_, SEEK_SET);
        read(fd, &root, sizeof(root)); 
    }
    inline void load(int fd) {
        this->fd = fd;
        read(fd, &sb, sizeof(sb));
        lseek(fd, sb.root_tree_root_, SEEK_SET);
        read(fd, &root, sizeof(root));
    }

    inline const auto& getSuperBlock () {return sb; }
    inline const auto& getRootNode () { return root;}

};

} // namespace btree

using btree::BTree;

#endif // BTREE_HPP_