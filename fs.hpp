#ifndef FS_HPP_
#define FS_HPP_


#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <fcntl.h>
#include <unistd.h>

using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8  = uint8_t;


constexpr u32 BLOCK_SIZE = 4096;
constexpr u32 MIN_FS_SIZE = BLOCK_SIZE * 10;

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

struct [[gnu::packed]] uuid_t {
    u8 uuid[16];


#if 0
    uuid_t () { memset(uuid, 0, 16); }

    uuid_t(const uuid_t& other) {
        std::memcpy(uuid , other.uuid, 16);
    }

    uuid_t& operator=(const uuid_t& other) {
        std::memcpy(uuid, other.uuid, 16);
        return *this;
    }

    inline u8* raw() { return uuid; }
    inline const u8* raw() const { return uuid; }
#endif

    auto& operator[] (size_t ix) {
        return uuid[ix];
    }
    const auto& operator[](size_t ix)const {
        return uuid[ix];
    }

};



struct [[gnu::packed]] SuperBlock {
    u8 csum_[32];       
    uuid_t fsid_;       
    u64 bytenr_;        
    u64 flags_;
    u8 magic_[8];       
    u64 generation_;    
    
    u64 root_tree_root_; 
    u32 nodesize_ = BLOCK_SIZE;

    mutable u64 next_free_block_;
    mutable u64 next_inode_id_;

    SuperBlock() {
        std::memset(this, 0, sizeof(SuperBlock)); 
        std::memmove(magic_, "MINIBTFS", 8);
        
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint8_t> dist(0, 255);

        for(int i = 0; i < 16; i++) {
            fsid_[i] = dist(gen);
        }

        bytenr_ = 0;            
        generation_ = 1;        
        nodesize_ = BLOCK_SIZE; 
        


        root_tree_root_ = BLOCK_SIZE; 
    }

    inline u64 allocate_block(int fd) const {
        u64 addr = next_free_block_;
        next_free_block_ += BLOCK_SIZE;
        
    
        lseek(fd, 0, SEEK_SET); 
        write(fd, this, sizeof(fs::SuperBlock));
        return addr;
    }

    inline u64 allocate_inode(int fd) const {
        u64 id = next_inode_id_++;
        lseek(fd, 0, SEEK_SET); 
        write(fd, this, sizeof(fs::SuperBlock));
        return id;
    }
};



constexpr u8 MAX_NAME_SIZE = 64;

enum class InodeType : u8 {
    File,
    Dir
};

struct [[gnu::packed]] InodeItem {
    u64 size; //bytes
    InodeType mode;

    InodeItem () = default;
    InodeItem (InodeType type) : mode(type) {
        // TODO: 
        size = 0;
    }

    inline bool isDir () {return mode == InodeType::Dir;}
    inline bool isFile () {return mode == InodeType::File;}

};

struct [[gnu::packed]] InodeRef {
    btree::Key parent;
    u8 name [MAX_NAME_SIZE];
};

struct [[gnu::packed]] Extentdata {
    u64 block_addr; // Где искать данные на диске
    u64 size;   // Сколько байт читать
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

    DirIndex () {
        std::memset(this, 0, sizeof(DirIndex));
    }
};


struct [[gnu::unused]] [[gnu::packed]] ExtentItems {
    // we store real phys offset in extent data directly
};




    
} // namespace fs


#endif // FS_HPP_