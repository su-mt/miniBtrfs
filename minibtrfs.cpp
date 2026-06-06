#include "minibtrfs.hpp"
#include "fs.hpp"
#include <iostream>
#include <string>

namespace minibtrfs {


void MiniBtrfs::mkdir(u64 parent_id, const char* name) {
    u64 new_id = next_inode_id++;

    // ====================================================================
    // ШАГ 1: Обновляем родительскую директорию (Добавляем в нее файл)
    // ====================================================================
    
    // 1.1 Ищем DirIndex родителя
    auto parent_index_item = tree.search(btree::Key{parent_id, btree::Type::DIR_INDEX, 0});
    if (!parent_index_item) {
        throw std::runtime_error("Parent directory not found!");
    }

    // 1.2 Читаем девайс, обновляем счетчик и пишем обратно (in-place update)
    fs::DirIndex p_index;
    lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    read(fd, &p_index, sizeof(fs::DirIndex));
    
    u64 current_offset = p_index.cnt; 
    p_index.cnt++;                    
    
    lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    write(fd, &p_index, sizeof(fs::DirIndex)); 

    // 1.3 Вставляем DirItem (ссылка в родителе на новую папку)
    fs::DirItem dir_item;
    std::memset(&dir_item, 0, sizeof(fs::DirItem)); // Очищаем память от мусора
    dir_item.location = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    std::strncpy((char*)dir_item.name, name, fs::MAX_NAME_SIZE);

    u64 dir_item_addr = fs::allocate_block();
    lseek(fd, dir_item_addr, SEEK_SET);
    write(fd, &dir_item, sizeof(fs::DirItem));

    btree::Item tree_dir_item;

    // Ключ должен быть: {parent_id, DIR_ITEM, 0} -> {parent_id, DIR_ITEM, 1}
    tree_dir_item.key_ = btree::Key{parent_id, btree::Type::DIR_ITEM, current_offset}; 
    tree_dir_item.data_offset_ = dir_item_addr; // ВОТ ЗДЕСЬ должен быть адрес данных
    tree_dir_item.data_size_ = sizeof(fs::DirItem);
    tree.insert(tree_dir_item);

    std::cerr << "[DEBUG] Inserted DirItem for key: (" 
          << tree_dir_item.key_.id_ << ", " 
          << (int)tree_dir_item.key_.type_ << ", " 
          << tree_dir_item.key_.offset_ << ")" << std::endl;

    // ====================================================================
    // ШАГ 2: Создаем саму новую директорию (Внутренности new_id)
    // ====================================================================

    // 2.1 Создаем InodeItem (Основной паспорт новой папки)
    fs::InodeItem new_inode(fs::InodeType::Dir);


    u64 inode_addr = fs::allocate_block();
    lseek(fd, inode_addr, SEEK_SET);
    write(fd, &new_inode, sizeof(fs::InodeItem));
    
    btree::Item inode_tree_item;
    inode_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    inode_tree_item.data_offset_ = inode_addr;
    inode_tree_item.data_size_ = sizeof(fs::InodeItem);
    tree.insert(inode_tree_item);

    // 2.2 Создаем InodeRef (Обратная жесткая ссылка: новая папка -> родитель)
    fs::InodeRef new_inode_ref;
    std::memset(&new_inode_ref, 0, sizeof(fs::InodeRef));
    new_inode_ref.parent = btree::Key{parent_id, btree::Type::INODE_ITEM, 0}; 
    std::strncpy((char*)new_inode_ref.name, name, fs::MAX_NAME_SIZE);

    u64 ref_addr = fs::allocate_block();
    lseek(fd, ref_addr, SEEK_SET);
    write(fd, &new_inode_ref, sizeof(fs::InodeRef));

    btree::Item ref_tree_item;
    // В ключе INODE_REF смещение (offset) указывает на ID родителя
    ref_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_REF, parent_id}; 
    ref_tree_item.data_offset_ = ref_addr;
    ref_tree_item.data_size_ = sizeof(fs::InodeRef);
    tree.insert(ref_tree_item);

    // 2.3 Создаем DirIndex (Дескриптор содержимого самой новой папки)
    fs::DirIndex new_dir_index;
    std::memset(&new_dir_index, 0, sizeof(fs::DirIndex));
    new_dir_index.cnt = 0; 
    new_dir_index.start = btree::Key{new_id, btree::Type::DIR_ITEM, 0}; 

    u64 index_addr = fs::allocate_block();
    lseek(fd, index_addr, SEEK_SET);
    write(fd, &new_dir_index, sizeof(fs::DirIndex));

    btree::Item index_tree_item;
    index_tree_item.key_ = btree::Key{new_id, btree::Type::DIR_INDEX, 0};
    index_tree_item.data_offset_ = index_addr;
    index_tree_item.data_size_ = sizeof(fs::DirIndex);
    tree.insert(index_tree_item);

    // Синхронизируем изменения с диском
    fsync(fd);
}

void MiniBtrfs::ls(u64 dir_id) {
    // 1. Ищем DirIndex, чтобы узнать сколько элементов лежит в этой папке
    auto index_item = tree.search(btree::Key{dir_id, btree::Type::DIR_INDEX, 0});
    if (!index_item) {
        throw std::runtime_error("ls: Directory not found!");
        return;
    }

    fs::DirIndex p_index;
    lseek(fd, index_item->data_offset_, SEEK_SET);
    read(fd, &p_index, sizeof(fs::DirIndex));

    if (p_index.cnt == 0) {
        std::cout << "Directory " << dir_id << " is empty.\n";
        return;
    }

    std::cout << "SIZE=" << p_index.cnt << std::endl;
    std::cout << "ID\tTYPE\tSIZE\tNAME\n";
    std::cout << "------------------------------------\n";

    // 2. Читаем каждый DirItem по его индексу
    for (u64 i = 0; i < p_index.cnt; i++) {
        auto dir_tree_item = tree.search(btree::Key{dir_id, btree::Type::DIR_ITEM, i});
        if (!dir_tree_item) {
            std::string err = "ls: Warning! Missing DirItem at index "; err += i;
            //throw std::runtime_error(err);
            continue;
        }

        fs::DirItem dir_item;
        lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
        read(fd, &dir_item, sizeof(fs::DirItem));

        // 3. Достаем сам InodeItem файла по ссылке location, чтобы узнать его параметры
        auto inode_tree_item = tree.search(dir_item.location);
        
        std::string type_str = "???";
        u64 size = 0;
        
        if (inode_tree_item) {
            fs::InodeItem inode;
            lseek(fd, inode_tree_item->data_offset_, SEEK_SET);
            read(fd, &inode, sizeof(fs::InodeItem));
            
            type_str = (inode.isDir()) ? "DIR " : "FILE";
            size = inode.size;
        }

        // 4. Выводим красивую строку
        std::cout << dir_item.location.id_ << "\t" 
                  << type_str << "\t" 
                  << size << "\t" 
                  << dir_item.name << "\n";
    }
}



bool MiniBtrfs::inspectFS() {

        const std::string expected_magic = "MINIBTFS";

        auto sb = tree.getSuperBlock();
        auto rootNode = tree.getRootNode();

        std::string actual_magic(
            reinterpret_cast<const char*>(sb.magic_), 8);

        if (actual_magic != expected_magic)
            return false;

        // 2. BASIC SUPERBLOCK VALIDATION
        if (sb.nodesize_ == 0)
            return false;

        if (sb.root_tree_root_ == 0)
            return false;

        // optional: sanity limit
        if (sb.nodesize_ > (1 << 20))
            return false;


        return true;
}



} // minibtrfs