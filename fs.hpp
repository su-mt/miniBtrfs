#ifndef FS_HPP_
#define FS_HPP_


#include <cstdint>

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8  = uint8_t;

constexpr u32 BLOCK_SIZE = 4096;

namespace btree {
enum class Type : u8 {
    INODE_ITEM,
    INODE_REF,
    EXTENT_DATA,
    EXTENT_INLINE,
    DIR_ITEM, 
    DIR_INDEX,
    EXTENT_ITEMS 
};

struct [[gnu::packed]] Key {
    u64 id_;
    Type type_;
    u64 offset_;

    bool operator==(const Key& k) const {
        return id_ == k.id_ and
               type_ == k.type_ and
               offset_ == k.offset_;
    }

    bool operator<(const Key& k) const {
        if (id_ != k.id_) {
            return id_ < k.id_;
        } else if (type_ != k.type_) {
            return type_ < k.type_;
        } else {
            return offset_ < k.offset_;
        }
    }
};
} // namespace btree


namespace fs {

constexpr u8 MAX_NAME_SIZE = 64;

struct [[gnu::packed]] InodeItem {
    u64 size; //bytes
    u8 mode;


    inline bool isDir () {return mode == 1;}
    inline bool isFile () {return mode == 0;}

};

struct [[gnu::packed]] InodeRef {
    btree::Key parent;
    u8 name [MAX_NAME_SIZE];
};

struct [[gnu::packed]] Extentdata {
    u64 disk_bytenr; // Где искать данные на диске
    u64 num_bytes;   // Сколько байт читать
};

struct [[gnu::packed]] ExtentInline {
    u64 num_bytes;   // Сколько байт читать
    u8 data[];
};

struct [[gnu::packed]] DirItem {
    btree::Key location;
    u8 name[MAX_NAME_SIZE];
};


struct [[gnu::packed]] DirIndex {
    btree::Key start;
    u64 cnt;
};


struct [[gnu::unused]] [[gnu::packed]] ExtentItems {
    // we store real phys offset in extent data directly
};




    
} // namespace fs


#endif // FS_HPP_