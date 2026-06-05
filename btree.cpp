#include "btree.hpp"
#include <cstring>

namespace btree {

std::optional<Item> BTree::searchR(const Node& h, Key v, int ht) {
    if (ht == 0) {
        for(int i = 0; i < h.hdr_.nritems_; i++) {
            if (v == h.item(i).key_) {
                return h.item(i);
            }
        }
    } else {
        for(int i = 0; i < h.hdr_.nritems_; i++) {
            if (i + 1 == h.hdr_.nritems_ || v < h.ptr(i + 1).key_) {
                Node next(fd, h.ptr(i).addr_); 
                return searchR(next, v, ht - 1);
            }
        }
    }
    return std::nullopt;
}

u64 BTree::allocate_block() {
    static u64 mock_addr = 10 * 1024 * 1024; 
    mock_addr += BLOCK_SIZE;
    return mock_addr;
}

KeyPtr BTree::split(Node& h, u64 addr) {
    u32 max_capacity = (h.is_item()) ? ITEMS_SIZE : PTRS_SIZE;
    
    u32 left_capacity = max_capacity / 2;
    u32 right_capacity = max_capacity - left_capacity;

    Node right_node;


    right_node.hdr_.fsid_ = h.hdr_.fsid_;

    right_node.hdr_.level_ = h.hdr_.level_;

    if (h.is_item()) {
        for (u32 i = 0; i < right_capacity; i++) {
            right_node.item(i) = h.item(left_capacity + i);
        }
    } else {
        for (u32 i = 0; i < right_capacity; i++) {
            right_node.ptr(i) = h.ptr(left_capacity + i);
        }
    }

    h.hdr_.nritems_ = left_capacity;
    right_node.hdr_.nritems_ = right_capacity;

    u64 right_node_addr = allocate_block();

    lseek(fd, addr, SEEK_SET);
    write(fd, &h, BLOCK_SIZE);

    lseek(fd, right_node_addr, SEEK_SET);
    write(fd, &right_node, BLOCK_SIZE);

    Key up_key = right_node.is_item() ? right_node.item(0).key_ : right_node.ptr(0).key_;
    return KeyPtr{up_key, right_node_addr, 1};
}

std::optional<KeyPtr> BTree::insertR(Node& h, const Item& x, int ht, u64 current_blk_addr) {
    u32 i = 0;

    if (ht == 0) {
        for (; i < h.hdr_.nritems_; i++) {
            if (x.key_ < h.item(i).key_) break;
        }
        
        for (u32 j = h.hdr_.nritems_; j > i; j--) {
            h.item(j) = h.item(j - 1);
        }
        
        h.item(i) = x;
        h.hdr_.nritems_++;

    } else {
        for (; i < h.hdr_.nritems_; i++) {
            if ((i + 1 == h.hdr_.nritems_) || x.key_ < h.ptr(i + 1).key_) {
                
                u64 child_addr = h.ptr(i).addr_;
                Node next(fd, child_addr);

                auto split_child = insertR(next, x, ht - 1, child_addr);

                lseek(fd, child_addr, SEEK_SET);
                write(fd, &next, BLOCK_SIZE);

                if (split_child.has_value()) {
                    u32 insert_pos = i + 1;
                    
                    for (u32 j = h.hdr_.nritems_; j > insert_pos; j--) {
                        h.ptr(j) = h.ptr(j - 1);
                    }
                    h.ptr(insert_pos) = split_child.value();
                    h.hdr_.nritems_++;
                }
                break;
            }
        }
    }

    u32 max_capacity = (ht == 0) ? ITEMS_SIZE : PTRS_SIZE;
    
    if (h.hdr_.nritems_ < max_capacity) {
        lseek(fd, current_blk_addr, SEEK_SET);
        write(fd, &h, BLOCK_SIZE);
        return std::nullopt;
    } else {
        return split(h, current_blk_addr);
    }
}


std::optional<Item> BTree::search(Key key) {
    return searchR(root, key, root.hdr_.level_);
}

void BTree::insert(Item item) {
    u64 root_addr = sb.root_tree_root_;

    auto split_child = insertR(root, item, root.hdr_.level_, root_addr);
    
    if (split_child.has_value()) {
        u64 left_child_addr = allocate_block();
        Node left_node = root;
        
        lseek(fd, left_child_addr, SEEK_SET);
        write(fd, &left_node, BLOCK_SIZE);
        
        //std::memcpy(saved_fsid, root.hdr_.fsid_, 16);
        fs::uuid_t saved_fsid = root.hdr_.fsid_;
        u8 new_level = root.hdr_.level_ + 1; 
        
        std::memset(&root, 0, sizeof(Node));
        
        //std::memcpy(root.hdr_.fsid_, saved_fsid, 16);
        root.hdr_.fsid_ = saved_fsid;
        root.hdr_.level_ = new_level;
        root.hdr_.nritems_ = 2; 

        Key left_key = left_node.is_item() ? left_node.item(0).key_ : left_node.ptr(0).key_;
        root.ptr(0) = KeyPtr{left_key, left_child_addr, 1};
        root.ptr(1) = split_child.value();
        
        lseek(fd, root_addr, SEEK_SET);
        write(fd, &root, BLOCK_SIZE);
    }
}


} // namespace btree