#include "minibtrfs.hpp"
#include "fs.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <optional>
#include <cstring>

#ifndef MB_LOG_LEVEL
#define MB_LOG_LEVEL 0
#endif

#define LOG_BTRFS(msg) do { if (MB_LOG_LEVEL >= 1) std::cerr << "[BTRFS] " << msg << std::endl; } while(0)

namespace minibtrfs {

bool MiniBtrfs::mkdir(u64 parent_id, const char* name) {
    u64 new_id = tree.getSuperBlock().allocate_inode(fd);

    // ====================================================================
    // ШАГ 1: Обновляем родительскую директорию (Добавляем в нее файл)
    // ====================================================================
    
    // 1.1 Ищем DirIndex родителя
    auto parent_index_item = tree.search(btree::Key{parent_id, btree::Type::DIR_INDEX, 0});
    if (!parent_index_item) {
        LOG_BTRFS("mkdir: Parent directory not found!");
        return false;
    }

    // 1.2 Читаем девайс, обновляем счетчик и пишем обратно (in-place update)
    fs::DirIndex p_index;
    //lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    //read(fd, &p_index, sizeof(fs::DirIndex));
    pread(fd, &p_index, sizeof(fs::DirIndex), parent_index_item->data_offset_);
    
    u64 current_offset = p_index.cnt; 
    p_index.cnt++;                    
    
    //lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    //write(fd, &p_index, sizeof(fs::DirIndex));
    pwrite(fd, &p_index, sizeof(fs::DirIndex), parent_index_item->data_offset_);


    // 1.3 Вставляем DirItem (ссылка в родителе на новую папку)
    fs::DirItem dir_item;
    std::memset(&dir_item, 0, sizeof(fs::DirItem));
    dir_item.location = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    std::strncpy((char*)dir_item.name, name, fs::MAX_NAME_SIZE);
    
    u64 dir_item_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, dir_item_addr, SEEK_SET);
    //write(fd, &dir_item, sizeof(fs::DirItem));
    pwrite(fd, &dir_item, sizeof(fs::DirItem), dir_item_addr);


    btree::Item tree_dir_item;
    tree_dir_item.key_ = btree::Key{parent_id, btree::Type::DIR_ITEM, current_offset}; 
    tree_dir_item.data_offset_ = dir_item_addr;
    tree_dir_item.data_size_ = sizeof(fs::DirItem);
    tree.insert(tree_dir_item);

    LOG_BTRFS("Inserted DirItem for key: (" << tree_dir_item.key_.id_ << ", " 
              << (int)tree_dir_item.key_.type_ << ", " << tree_dir_item.key_.offset_ << ")");

    // ====================================================================
    // ШАГ 2: Создаем саму новую директорию (Внутренности new_id)
    // ====================================================================

    // 2.1 Создаем InodeItem
    fs::InodeItem new_inode(fs::InodeType::Dir);
    u64 inode_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, inode_addr, SEEK_SET);
    //write(fd, &new_inode, sizeof(fs::InodeItem));
    pwrite(fd, &new_inode, sizeof(fs::InodeItem), inode_addr);

    btree::Item inode_tree_item;
    inode_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    inode_tree_item.data_offset_ = inode_addr;
    inode_tree_item.data_size_ = sizeof(fs::InodeItem);
    tree.insert(inode_tree_item);

    // 2.2 Создаем InodeRef
    fs::InodeRef new_inode_ref;
    std::memset(&new_inode_ref, 0, sizeof(fs::InodeRef));
    new_inode_ref.parent = btree::Key{parent_id, btree::Type::INODE_ITEM, 0}; 
    std::strncpy((char*)new_inode_ref.name, name, fs::MAX_NAME_SIZE);

    u64 ref_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, ref_addr, SEEK_SET);
    //write(fd, &new_inode_ref, sizeof(fs::InodeRef));
    pwrite(fd, &new_inode_ref, sizeof(fs::InodeRef), ref_addr);

    btree::Item ref_tree_item;
    ref_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_REF, parent_id}; 
    ref_tree_item.data_offset_ = ref_addr;
    ref_tree_item.data_size_ = sizeof(fs::InodeRef);
    tree.insert(ref_tree_item);

    // 2.3 Создаем DirIndex
    fs::DirIndex new_dir_index;
    std::memset(&new_dir_index, 0, sizeof(fs::DirIndex));
    new_dir_index.cnt = 0; 
    new_dir_index.start = btree::Key{new_id, btree::Type::DIR_ITEM, 0}; 

    u64 index_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, index_addr, SEEK_SET);
    //write(fd, &new_dir_index, sizeof(fs::DirIndex));
    pwrite(fd, &new_dir_index, sizeof(fs::DirIndex), index_addr);

    btree::Item index_tree_item;
    index_tree_item.key_ = btree::Key{new_id, btree::Type::DIR_INDEX, 0};
    index_tree_item.data_offset_ = index_addr;
    index_tree_item.data_size_ = sizeof(fs::DirIndex);
    tree.insert(index_tree_item);

    fsync(fd);
    return true;
}

bool MiniBtrfs::ls(u64 dir_id) {
    auto index_item = tree.search(btree::Key{dir_id, btree::Type::DIR_INDEX, 0});
    if (!index_item) {
        LOG_BTRFS("ls: Directory not found!");
        return false;
    }

    fs::DirIndex p_index;
    //lseek(fd, index_item->data_offset_, SEEK_SET);
    //read(fd, &p_index, sizeof(fs::DirIndex));
    pread(fd, &p_index, sizeof(fs::DirIndex), index_item->data_offset_);

    if (p_index.cnt == 0) {
        std::cout << "Directory " << dir_id << " is empty.\n";
        return true;
    }

    std::cout << "SIZE=" << p_index.cnt << std::endl;
    std::cout << "ID\tTYPE\tSIZE\tNAME\n";
    std::cout << "------------------------------------\n";

    for (u64 i = 0; i < p_index.cnt; i++) {
        auto dir_tree_item = tree.search(btree::Key{dir_id, btree::Type::DIR_ITEM, i});
        if (!dir_tree_item) {
            LOG_BTRFS("ls: Warning! Missing DirItem at index " << i);
            continue;
        }

        fs::DirItem dir_item;
        //lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
        //read(fd, &dir_item, sizeof(fs::DirItem));
        pread(fd, &dir_item, sizeof(fs::DirItem), dir_tree_item->data_offset_);

        auto inode_tree_item = tree.search(dir_item.location);
        
        std::string type_str = "???";
        u64 size = 0;
        
        if (inode_tree_item) {
            fs::InodeItem inode;
            //lseek(fd, inode_tree_item->data_offset_, SEEK_SET);
            //read(fd, &inode, sizeof(fs::InodeItem));
            pread(fd, &inode, sizeof(fs::InodeItem), inode_tree_item->data_offset_);
            
            read(fd, &inode, sizeof(fs::InodeItem));
            
            type_str = (inode.isDir()) ? "DIR " : "FILE";
            size = inode.size;
        }

        std::cout << dir_item.location.id_ << "\t" << type_str << "\t" << size << "\t" << dir_item.name << "\n";
    }
    return true;
}

[[nodiscard]] std::optional<u64> MiniBtrfs::cd(u64 current_dir_id, const char* name) const {
    auto index_item = tree.search(btree::Key{current_dir_id, btree::Type::DIR_INDEX, 0});
    if (!index_item) {
        LOG_BTRFS("cd: Current directory metadata is corrupted or missing!");
        return std::nullopt;
    }

    fs::DirIndex p_index;
    //lseek(fd, index_item->data_offset_, SEEK_SET);
    //read(fd, &p_index, sizeof(fs::DirIndex));
    pread(fd, &p_index, sizeof(fs::DirIndex), index_item->data_offset_);

    for (u64 i = 0; i < p_index.cnt; i++) {
        auto dir_tree_item = tree.search(btree::Key{current_dir_id, btree::Type::DIR_ITEM, i});
        if (!dir_tree_item) continue;

        fs::DirItem dir_item;
        //lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
        //read(fd, &dir_item, sizeof(fs::DirItem));
        pread(fd, &dir_item, sizeof(fs::DirItem), dir_tree_item->data_offset_);

        if (std::strcmp((const char*)dir_item.name, name) == 0) {
            auto inode_tree_item = tree.search(dir_item.location);
            if (!inode_tree_item) {
                LOG_BTRFS("cd: Broken link (Inode missing)");
                return std::nullopt;
            }
            
            fs::InodeItem inode;
            //lseek(fd, inode_tree_item->data_offset_, SEEK_SET);
            //read(fd, &inode, sizeof(fs::InodeItem));
            pread(fd, &inode, sizeof(fs::InodeItem), inode_tree_item->data_offset_);

            if (!inode.isDir()) {
                LOG_BTRFS("cd: Not a directory");
                return std::nullopt;
            }
            return static_cast<u64>(dir_item.location.id_);
        }
    }
    
    LOG_BTRFS("cd: No such file or directory");
    return std::nullopt;
}

bool MiniBtrfs::create_file(u64 parent_id, const char* name) {
    u64 new_id = tree.getSuperBlock().allocate_inode(fd);
    
    auto parent_index_item = tree.search(btree::Key{parent_id, btree::Type::DIR_INDEX, 0});
    if (!parent_index_item) {
        LOG_BTRFS("create_file: Parent directory not found!");
        return false;
    }

    fs::DirIndex p_index;
    //lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    //read(fd, &p_index, sizeof(fs::DirIndex));
    pread(fd, &p_index, sizeof(fs::DirIndex), parent_index_item->data_offset_);

    u64 current_offset = p_index.cnt; 
    p_index.cnt++;                    
    
    //lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    //write(fd, &p_index, sizeof(fs::DirIndex));
    pwrite(fd, &p_index, sizeof(fs::DirIndex), parent_index_item->data_offset_);    

    fs::DirItem dir_item;
    std::memset(&dir_item, 0, sizeof(fs::DirItem));
    dir_item.location = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    std::strncpy((char*)dir_item.name, name, fs::MAX_NAME_SIZE);

    u64 dir_item_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, dir_item_addr, SEEK_SET);
    //write(fd, &dir_item, sizeof(fs::DirItem));
    pwrite(fd, &dir_item, sizeof(fs::DirItem), dir_item_addr);

    btree::Item tree_dir_item;
    tree_dir_item.key_ = btree::Key{parent_id, btree::Type::DIR_ITEM, current_offset};
    tree_dir_item.data_offset_ = dir_item_addr;
    tree_dir_item.data_size_ = sizeof(fs::DirItem);
    tree.insert(tree_dir_item);

    // 2.1 Создаем InodeItem
    fs::InodeItem new_inode;
    std::memset(&new_inode, 0, sizeof(fs::InodeItem));
    new_inode.size = 0; 
    new_inode.mode = fs::InodeType::File; 

    u64 inode_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, inode_addr, SEEK_SET);
    //write(fd, &new_inode, sizeof(fs::InodeItem));
    pwrite(fd, &new_inode, sizeof(fs::InodeItem), inode_addr);
    
    btree::Item inode_tree_item;
    inode_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    inode_tree_item.data_offset_ = inode_addr;
    inode_tree_item.data_size_ = sizeof(fs::InodeItem);
    tree.insert(inode_tree_item);

    // 2.2 Создаем InodeRef
    fs::InodeRef new_inode_ref;
    std::memset(&new_inode_ref, 0, sizeof(fs::InodeRef));
    new_inode_ref.parent = btree::Key{parent_id, btree::Type::INODE_ITEM, 0}; 
    std::strncpy((char*)new_inode_ref.name, name, fs::MAX_NAME_SIZE);

    u64 ref_addr = tree.getSuperBlock().allocate_block(fd);
    //lseek(fd, ref_addr, SEEK_SET);
    //write(fd, &new_inode_ref, sizeof(fs::InodeRef));
    pwrite(fd, &new_inode_ref, sizeof(fs::InodeRef), ref_addr);

    btree::Item ref_tree_item;
    ref_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_REF, parent_id}; 
    ref_tree_item.data_offset_ = ref_addr;
    ref_tree_item.data_size_ = sizeof(fs::InodeRef);
    tree.insert(ref_tree_item);

    fsync(fd);
    return true;
}

bool MiniBtrfs::inspectFS() {
    const std::string expected_magic = "MINIBTFS";
    auto sb = tree.getSuperBlock();

    std::string actual_magic(reinterpret_cast<const char*>(sb.magic_), 8);
    if (actual_magic != expected_magic) return false;
    if (sb.nodesize_ == 0) return false;
    if (sb.root_tree_root_ == 0) return false;
    if (sb.nodesize_ > (1 << 20)) return false;

    return true;
}

bool MiniBtrfs::write_file(u64 inode_id, const void* data, size_t size, off_t offset) {
    auto inode_item = tree.search(btree::Key{inode_id, btree::Type::INODE_ITEM, 0});
    if (!inode_item) {
        LOG_BTRFS("write_file: Inode not found!");
        return false;
    }

    fs::InodeItem inode;
    //lseek(fd, inode_item->data_offset_, SEEK_SET);
    //read(fd, &inode, sizeof(fs::InodeItem));
    pread(fd, &inode, sizeof(fs::InodeItem), inode_item->data_offset_);

    if (offset == 0 && size <= btree::MAX_INLINE_SIZE) {
        u64 data_addr = tree.getSuperBlock().allocate_block(fd);
        //lseek(fd, data_addr, SEEK_SET);
        //write(fd, data, size);
        pwrite(fd, data, size, data_addr);

        btree::Item inline_item;
        inline_item.key_ = btree::Key{inode_id, btree::Type::EXTENT_INLINE, (u64)offset};
        inline_item.data_offset_ = data_addr; 
        inline_item.data_size_ = size;
        tree.insert(inline_item);

        LOG_BTRFS("Wrote INLINE extent for Inode " << inode_id << " (size: " << size << ")");
    } else {
        u64 raw_data_block = tree.getSuperBlock().allocate_block(fd);
        //lseek(fd, raw_data_block, SEEK_SET);
        //write(fd, data, size);
        pwrite(fd, data, size, raw_data_block);

        fs::Extentdata ext_item;
        ext_item.block_addr = raw_data_block;
        ext_item.size = size;

        u64 ext_desc_addr = tree.getSuperBlock().allocate_block(fd);
        //lseek(fd, ext_desc_addr, SEEK_SET);
        //write(fd, &ext_item, sizeof(fs::Extentdata));
        pwrite(fd, &ext_item, sizeof(fs::Extentdata), ext_desc_addr);

        btree::Item tree_extent_item;
        tree_extent_item.key_ = btree::Key{inode_id, btree::Type::EXTENT_DATA, (u64)offset};
        tree_extent_item.data_offset_ = ext_desc_addr; 
        tree_extent_item.data_size_ = sizeof(fs::Extentdata);
        tree.insert(tree_extent_item);

        LOG_BTRFS("Wrote REGULAR extent for Inode " << inode_id << " at offset " << offset);
    }

    if (offset + size > inode.size) {
        inode.size = offset + size;
        //lseek(fd, inode_item->data_offset_, SEEK_SET);
        //write(fd, &inode, sizeof(fs::InodeItem));
        pwrite(fd, &inode, sizeof(fs::InodeItem), inode_item->data_offset_);
    }

    fsync(fd);
    return true;
}

[[nodiscard]] std::optional<u64> MiniBtrfs::resolve_path(u64 current_dir_id, const char* path) const {
    std::string path_str(path);
    if (path_str.empty()) return current_dir_id;

    u64 current_id = current_dir_id;
    if (path_str[0] == '/') {
        current_id = 256; 
    }

    std::stringstream ss(path_str);
    std::string token;

    while (std::getline(ss, token, '/')) {
        if (token.empty() || token == ".") continue; 

        auto inode_item = tree.search(btree::Key{current_id, btree::Type::INODE_ITEM, 0});
        if (!inode_item) {
            LOG_BTRFS("resolve: Corrupted inode link");
            return std::nullopt;
        }

        fs::InodeItem inode;
        //lseek(fd, inode_item->data_offset_, SEEK_SET);
        //read(fd, &inode, sizeof(fs::InodeItem));
        pread(fd, &inode, sizeof(fs::InodeItem), inode_item->data_offset_);

        #if 0
        if (!inode.isDir()) { 
            LOG_BTRFS("resolve: Not a directory");
            return std::nullopt;
        }

        #endif
        auto index_item = tree.search(btree::Key{current_id, btree::Type::DIR_INDEX, 0});
        if (!index_item) {
            LOG_BTRFS("resolve: Directory metadata missing");
            return std::nullopt;
        }

        fs::DirIndex p_index;
        //lseek(fd, index_item->data_offset_, SEEK_SET);
        //read(fd, &p_index, sizeof(fs::DirIndex));
        pread(fd, &p_index, sizeof(fs::DirIndex), index_item->data_offset_);

        bool found = false;

        for (u64 i = 0; i < p_index.cnt; i++) {
            auto dir_tree_item = tree.search(btree::Key{current_id, btree::Type::DIR_ITEM, i});
            if (!dir_tree_item) continue;

            fs::DirItem dir_item;
            //lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
            //read(fd, &dir_item, sizeof(fs::DirItem));
            pread(fd, &dir_item, sizeof(fs::DirItem), dir_tree_item->data_offset_);
            //read(fd, &dir_item, sizeof(fs::DirItem));

            if (token == (const char*)dir_item.name) {
                current_id = dir_item.location.id_;
                found = true;
                break;
            }
        }

        if (!found) {
            LOG_BTRFS("resolve: No such file or directory: " << token);
            return std::nullopt;
        }
    }

    return current_id;
}

// Не забудь обновить сигнатуру в minibtrfs.hpp:
// std::optional<std::vector<u8>> read_file(u64 inode_id, off_t offset, size_t size_requested) const;

std::optional<std::vector<u8>> MiniBtrfs::read_file(u64 inode_id, off_t offset, size_t size_requested) const {
    auto inode_item = tree.search(btree::Key{inode_id, btree::Type::INODE_ITEM, 0});
    if (!inode_item) {
        LOG_BTRFS("read_file: Inode not found!");
        return std::nullopt;
    }

    fs::InodeItem inode;
    //lseek(fd, inode_item->data_offset_, SEEK_SET);
    //read(fd, &inode, sizeof(fs::InodeItem));
    pread(fd, &inode, sizeof(fs::InodeItem), inode_item->data_offset_);

    if (offset >= inode.size) {
        return std::vector<u8>(); // Настоящий честный EOF
    }

    // Ограничиваем запрос концом файла, чтобы не читать лишний мусор
    size_t total_to_read = std::min(size_requested, (size_t)(inode.size - offset));
    
    // Выделяем буфер сразу нужного размера и забиваем нулями
    // (нули останутся на местах "дыр" / sparse extents)
    std::vector<u8> result(total_to_read, 0); 

    // ПУТЬ А: INLINE (Файл целиком лежит в метаданных)
    auto inline_ext = tree.search(btree::Key{inode_id, btree::Type::EXTENT_INLINE, 0});
    if (inline_ext) {
        //lseek(fd, inline_ext->data_offset_ + offset, SEEK_SET);
        //read(fd, result.data(), total_to_read);
        pread(fd, result.data(), total_to_read, inline_ext->data_offset_ + offset);
        LOG_BTRFS("Read INLINE extent (size: " << total_to_read << ")");
        return result;
    }

    // ПУТЬ Б: REGULAR (Чтение блоков в цикле)
    off_t current_offset = offset;
    size_t bytes_left = total_to_read;
    u8* out_ptr = result.data(); // Указатель для записи в результирующий вектор

    while (bytes_left > 0) {
        u64 extent_start = (current_offset / BLOCK_SIZE) * BLOCK_SIZE;
        u64 offset_in_block = current_offset % BLOCK_SIZE;

        auto reg_ext = tree.search(btree::Key{inode_id, btree::Type::EXTENT_DATA, extent_start});

        // Сколько максимум можно прочитать из текущего 4K блока
        size_t chunk_size = std::min(bytes_left, (size_t)(BLOCK_SIZE - offset_in_block));

        if (reg_ext) {
            fs::Extentdata ext_item;
            //lseek(fd, reg_ext->data_offset_, SEEK_SET);
            //read(fd, &ext_item, sizeof(fs::Extentdata));
            pread(fd, &ext_item, sizeof(fs::Extentdata), reg_ext->data_offset_);

            // Если размер записанного экстента больше смещения, читаем реальные данные
            if (ext_item.size > offset_in_block) {
                size_t valid_bytes = std::min(chunk_size, (size_t)(ext_item.size - offset_in_block));
                //lseek(fd, ext_item.block_addr + offset_in_block, SEEK_SET);
                //read(fd, out_ptr, valid_bytes);
                pread(fd, out_ptr, valid_bytes, ext_item.block_addr + offset_in_block);
            }
        } else {
            // Экстент не найден. Поскольку мы заранее забили result нулями, 
            // мы просто логируем это как sparse hole и ничего не читаем с диска.
            LOG_BTRFS("read_file: Sparse hole implicitly filled with zeros at offset " << extent_start);
        }

        current_offset += chunk_size;
        out_ptr += chunk_size;
        bytes_left -= chunk_size;
    }

    LOG_BTRFS("Read REGULAR multi-extent file (total size: " << total_to_read << ")");
    return result;
}

std::optional<std::vector<std::string>> MiniBtrfs::get_dir_entries(u64 dir_id) const {
    std::vector<std::string> entries;
    auto index_item = tree.search(btree::Key{dir_id, btree::Type::DIR_INDEX, 0});
    if (!index_item) return std::nullopt; 

    fs::DirIndex p_index;
    //lseek(fd, index_item->data_offset_, SEEK_SET);
    //read(fd, &p_index, sizeof(fs::DirIndex));
    pread(fd, &p_index, sizeof(fs::DirIndex), index_item->data_offset_);

    for (u64 i = 0; i < p_index.cnt; i++) {
        auto dir_tree_item = tree.search(btree::Key{dir_id, btree::Type::DIR_ITEM, i});
        if (!dir_tree_item) continue;
        
        fs::DirItem dir_item;
        //lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
        //read(fd, &dir_item, sizeof(fs::DirItem));
        pread(fd, &dir_item, sizeof(fs::DirItem), dir_tree_item->data_offset_);
        entries.push_back((char*)dir_item.name);
    }
    return entries;
}

std::optional<fs::InodeItem> MiniBtrfs::get_inode(u64 inode_id) const {
    auto item = tree.search(btree::Key{inode_id, btree::Type::INODE_ITEM, 0});
    if (!item) {
        LOG_BTRFS("get_inode: Inode not found");
        return std::nullopt;
    }
    
    fs::InodeItem inode;
    //lseek(fd, item->data_offset_, SEEK_SET);
    //read(fd, &inode, sizeof(fs::InodeItem));
    pread(fd, &inode, sizeof(fs::InodeItem), item->data_offset_);
    return inode;
}

} // minibtrfs