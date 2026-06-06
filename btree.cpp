#include "btree.hpp"
#include <cstring>

namespace btree {

std::optional<Item> BTree::searchR(const Node& h, Key v, int ht) const {
    // Защита от пустых узлов (на всякий случай, чтобы избежать int underflow ниже)
    if (h.hdr_.nritems_ == 0) {
        return std::nullopt;
    }

    if (ht == 0) {
        // ==========================================
        // ЛИСТОВОЙ УЗЕЛ: Поиск точного совпадения
        // ==========================================
        int left = 0;
        int right = h.hdr_.nritems_ - 1;

        while (left <= right) {
            int mid = left + (right - left) / 2;
            Key mid_key = h.item(mid).key_;

            if (v == mid_key) {
                return h.item(mid);
            } else if (v < mid_key) {
                right = mid - 1;
            } else {
                left = mid + 1;
            }
        }
    } else {
        // ==========================================
        // ВНУТРЕННИЙ УЗЕЛ: Поиск нужного потомка
        // ==========================================
        int left = 0;
        int right = h.hdr_.nritems_ - 1;
        
        // По умолчанию смотрим в самый левый child, 
        // если искомый ключ меньше всех ключей в текущем узле
        int match_idx = 0; 

        while (left <= right) {
            int mid = left + (right - left) / 2;
            Key mid_key = h.ptr(mid).key_;

            // Если искомый ключ меньше ключа указателя, значит идем влево
            if (v < mid_key) {
                right = mid - 1;
            } else {
                // Если ключ больше или равен, это потенциальный кандидат.
                // Сохраняем его индекс и продолжаем искать правее (вдруг есть еще ближе).
                match_idx = mid;
                left = mid + 1;
            }
        }

        Node next(fd, h.ptr(match_idx).addr_); 
        return searchR(next, v, ht - 1);
    }
    
    return std::nullopt;
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

    u64 right_node_addr = sb.allocate_block(fd);

    //lseek(fd, addr, SEEK_SET);
    //write(fd, &h, BLOCK_SIZE);

    pwrite(fd, &h, BLOCK_SIZE, addr);

    //lseek(fd, right_node_addr, SEEK_SET);
    //write(fd, &right_node, BLOCK_SIZE);

    pwrite(fd, &right_node, BLOCK_SIZE, right_node_addr);

    Key up_key = right_node.is_item() ? right_node.item(0).key_ : right_node.ptr(0).key_;
    return KeyPtr{up_key, right_node_addr, 1};
}

std::optional<KeyPtr> BTree::insertR(Node& h, const Item& x, int ht, u64 current_blk_addr) {
    u32 n = h.hdr_.nritems_;
    u32 i = 0;

    // 1. Бинарный поиск позиции вставки
    int left = 0;
    int right = n - 1;
    u32 insert_pos = n; // По умолчанию вставляем в конец

    while (left <= right) {
        int mid = left + (right - left) / 2;
        Key mid_key = (ht == 0) ? h.item(mid).key_ : h.ptr(mid).key_;

        if (x.key_ < mid_key) {
            insert_pos = mid;
            right = mid - 1;
        } else {
            left = mid + 1;
        }
    }

    if (ht == 0) {
        // ЛИСТОВОЙ УЗЕЛ: Сдвигаем элементы и вставляем
        for (u32 j = n; j > insert_pos; j--) {
            h.item(j) = h.item(j - 1);
        }
        h.item(insert_pos) = x;
        h.hdr_.nritems_++;

    } else {
        // ВНУТРЕННИЙ УЗЕЛ
        // Если insert_pos == 0, мы должны идти в самый левый child.
        // Иначе мы идем в child, который находится перед insert_pos (т.е. insert_pos - 1).
        u32 target_idx = (insert_pos > 0) ? insert_pos - 1 : 0;
        
        u64 child_addr = h.ptr(target_idx).addr_;
        Node next(fd, child_addr);

        auto split_child = insertR(next, x, ht - 1, child_addr);

        //lseek(fd, child_addr, SEEK_SET);
        //write(fd, &next, BLOCK_SIZE);
        pwrite(fd, &next, BLOCK_SIZE, child_addr);

        if (split_child.has_value()) {
            // Если потомок разделился, вставляем новый KeyPtr в текущий узел
            for (u32 j = n; j > insert_pos; j--) {
                h.ptr(j) = h.ptr(j - 1);
            }
            h.ptr(insert_pos) = split_child.value();
            h.hdr_.nritems_++;
        }
    }

    // Сохранение узла или вызов split
    u32 max_capacity = (ht == 0) ? ITEMS_SIZE : PTRS_SIZE;
    if (h.hdr_.nritems_ < max_capacity) {
        //lseek(fd, current_blk_addr, SEEK_SET);
        //write(fd, &h, BLOCK_SIZE);
        pwrite(fd, &h, BLOCK_SIZE, current_blk_addr);
        return std::nullopt;
    } else {
        return split(h, current_blk_addr);
    }
}

std::optional<Item> BTree::search(Key key) const {
    return searchR(root, key, root.hdr_.level_);
}

void BTree::insert(Item item) {
    u64 root_addr = sb.root_tree_root_;

    auto split_child = insertR(root, item, root.hdr_.level_, root_addr);
    
    if (split_child.has_value()) {
        u64 left_child_addr = sb.allocate_block(fd);
        Node left_node = root;
        
        //lseek(fd, left_child_addr, SEEK_SET);
        //write(fd, &left_node, BLOCK_SIZE);
        pwrite(fd, &left_node, BLOCK_SIZE, left_child_addr);

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
        
        //lseek(fd, root_addr, SEEK_SET);
        //write(fd, &root, BLOCK_SIZE);
        pwrite(fd, &root, BLOCK_SIZE, root_addr);


        //lseek(fd, 0, SEEK_SET);
        //write(fd, &sb, sizeof(fs::SuperBlock));
        pwrite(fd, &sb, sizeof(fs::SuperBlock), 0);
    }


}


} // namespace btree