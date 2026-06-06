#include "minibtrfs.hpp"
#include "fs.hpp"
#include <iostream>
#include <string>
#include <sstream>
#include <string>
#include <stdexcept>

namespace minibtrfs {



void MiniBtrfs::mkdir(u64 parent_id, const char* name) {
    u64 new_id = tree.getSuperBlock().allocate_inode(fd);

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
    
    u64 dir_item_addr = tree.getSuperBlock().allocate_block(fd);
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


    u64 inode_addr = tree.getSuperBlock().allocate_block(fd);

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

    u64 ref_addr = tree.getSuperBlock().allocate_block(fd);
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

    u64 index_addr = tree.getSuperBlock().allocate_block(fd);
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


[[nodiscard]] u64 MiniBtrfs::cd(u64 current_dir_id, const char* name) const {
    // 1. Получаем дескриптор текущей директории, чтобы узнать количество элементов
    auto index_item = tree.search(btree::Key{current_dir_id, btree::Type::DIR_INDEX, 0});
    if (!index_item) {
        throw std::runtime_error("cd: Current directory metadata is corrupted or missing!");
    }

    fs::DirIndex p_index;
    lseek(fd, index_item->data_offset_, SEEK_SET);
    read(fd, &p_index, sizeof(fs::DirIndex));

    // 2. Итерируемся по всем элементам папки в поиске нужного имени
    for (u64 i = 0; i < p_index.cnt; i++) {
        auto dir_tree_item = tree.search(btree::Key{current_dir_id, btree::Type::DIR_ITEM, i});
        if (!dir_tree_item) {
            continue; // Пропускаем "битые" записи, если они есть
        }

        fs::DirItem dir_item;
        lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
        read(fd, &dir_item, sizeof(fs::DirItem));

        // 3. Сравниваем имена
        if (std::strcmp((const char*)dir_item.name, name) == 0) {
            
            // Нашли! Теперь нужно убедиться, что это действительно директория, а не обычный файл
            auto inode_tree_item = tree.search(dir_item.location);
            if (inode_tree_item) {
                fs::InodeItem inode;
                lseek(fd, inode_tree_item->data_offset_, SEEK_SET);
                read(fd, &inode, sizeof(fs::InodeItem));
                

                if (!inode.isDir()){
                    throw std::invalid_argument("cd: Not a directory");
                }
            } else {
                throw std::runtime_error("cd: Broken link (Inode missing)");
            }
            
            // Успех. Возвращаем ID найденной папки
            return dir_item.location.id_;
        }
    }

    // Если цикл завершился, а совпадений нет
    throw std::invalid_argument("cd: No such file or directory");
}

void MiniBtrfs::create_file(u64 parent_id, const char* name) {
    u64 new_id =tree.getSuperBlock().allocate_inode(fd);

    // ====================================================================
    // ШАГ 1: Обновляем родительскую директорию (Точно так же, как в mkdir)
    // ====================================================================
    
    auto parent_index_item = tree.search(btree::Key{parent_id, btree::Type::DIR_INDEX, 0});
    if (!parent_index_item) {
        throw std::runtime_error("create_file: Parent directory not found!");
    }

    fs::DirIndex p_index;
    lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    read(fd, &p_index, sizeof(fs::DirIndex));
    
    u64 current_offset = p_index.cnt; 
    p_index.cnt++;                    
    
    lseek(fd, parent_index_item->data_offset_, SEEK_SET);
    write(fd, &p_index, sizeof(fs::DirIndex)); 

    fs::DirItem dir_item;
    std::memset(&dir_item, 0, sizeof(fs::DirItem));
    dir_item.location = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    std::strncpy((char*)dir_item.name, name, fs::MAX_NAME_SIZE);

    u64 dir_item_addr = tree.getSuperBlock().allocate_block(fd);
    lseek(fd, dir_item_addr, SEEK_SET);
    write(fd, &dir_item, sizeof(fs::DirItem));

    btree::Item tree_dir_item;
    tree_dir_item.key_ = btree::Key{parent_id, btree::Type::DIR_ITEM, current_offset};
    tree_dir_item.data_offset_ = dir_item_addr;
    tree_dir_item.data_size_ = sizeof(fs::DirItem);
    tree.insert(tree_dir_item);

    // ====================================================================
    // ШАГ 2: Создаем сам файл (Только InodeItem и InodeRef)
    // ====================================================================

    // 2.1 Создаем InodeItem (Паспорт файла)
    fs::InodeItem new_inode;
    std::memset(&new_inode, 0, sizeof(fs::InodeItem));
    new_inode.size = 0; // Изначально файл пуст
    new_inode.mode = fs::InodeType::File; // 0 - обычный файл (FILE)

    u64 inode_addr = tree.getSuperBlock().allocate_block(fd);
    lseek(fd, inode_addr, SEEK_SET);
    write(fd, &new_inode, sizeof(fs::InodeItem));
    
    btree::Item inode_tree_item;
    inode_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_ITEM, 0};
    inode_tree_item.data_offset_ = inode_addr;
    inode_tree_item.data_size_ = sizeof(fs::InodeItem);
    tree.insert(inode_tree_item);

    // 2.2 Создаем InodeRef (Обратная ссылка на родительскую папку)
    fs::InodeRef new_inode_ref;
    std::memset(&new_inode_ref, 0, sizeof(fs::InodeRef));
    new_inode_ref.parent = btree::Key{parent_id, btree::Type::INODE_ITEM, 0}; 
    std::strncpy((char*)new_inode_ref.name, name, fs::MAX_NAME_SIZE);

    u64 ref_addr = tree.getSuperBlock().allocate_block(fd);
    lseek(fd, ref_addr, SEEK_SET);
    write(fd, &new_inode_ref, sizeof(fs::InodeRef));

    btree::Item ref_tree_item;
    ref_tree_item.key_ = btree::Key{new_id, btree::Type::INODE_REF, parent_id}; 
    ref_tree_item.data_offset_ = ref_addr;
    ref_tree_item.data_size_ = sizeof(fs::InodeRef);
    tree.insert(ref_tree_item);

    // Синхронизируем изменения
    fsync(fd);
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

void MiniBtrfs::write_file(u64 inode_id, const void* data, size_t size, off_t offset) {
    // 1. Ищем Inode файла, чтобы потом обновить его размер
    auto inode_item = tree.search(btree::Key{inode_id, btree::Type::INODE_ITEM, 0});
    if (!inode_item) {
        throw std::runtime_error("write_file: Inode not found!");
    }

    fs::InodeItem inode;
    lseek(fd, inode_item->data_offset_, SEEK_SET);
    read(fd, &inode, sizeof(fs::InodeItem));

    // Устанавливаем порог для INLINE (например, половина стандартного блока или 512 байт)
    

    // 2. Логика распределения: INLINE или REGULAR
    if (offset == 0 && size <= btree::MAX_INLINE_SIZE) {
        // ==========================================
        // ПУТЬ А: INLINE EXTENT (Маленький файл)
        // ==========================================
        
        // Выделяем место под payload дерева и пишем туда СЫРЫЕ данные
        u64 data_addr = tree.getSuperBlock().allocate_block(fd);
        lseek(fd, data_addr, SEEK_SET);
        write(fd, data, size);

        btree::Item inline_item;
        inline_item.key_ = btree::Key{inode_id, btree::Type::EXTENT_INLINE, (u64)offset};
        inline_item.data_offset_ = data_addr; // Указывает прямо на байты файла
        inline_item.data_size_ = size;
        tree.insert(inline_item);

        std::cout << "[DEBUG] Wrote INLINE extent for Inode " << inode_id << " (size: " << size << ")\n";

    } else {
        // ==========================================
        // ПУТЬ Б: REGULAR EXTENT (Большой файл)
        // ==========================================

        // Шаг Б.1: Выделяем блок(и) под сами данные на диске
        u64 raw_data_block = tree.getSuperBlock().allocate_block(fd);
        // В реальной системе тут может быть цикл аллокации для файлов > размера блока,
        // но для MiniBtrfs пишем напрямую в выделенный адрес.
        lseek(fd, raw_data_block, SEEK_SET);
        write(fd, data, size);

        // Шаг Б.2: Создаем дескриптор экстента (метаданные)

        fs::Extentdata ext_item;
        ext_item.block_addr = raw_data_block;
        ext_item.size = size;

        // Шаг Б.3: Выделяем место под payload для B-дерева и пишем дескриптор
        u64 ext_desc_addr = tree.getSuperBlock().allocate_block(fd);
        lseek(fd, ext_desc_addr, SEEK_SET);
        write(fd, &ext_item, sizeof(fs::Extentdata));

        // Шаг Б.4: Вставляем ключ в B-дерево
        btree::Item tree_extent_item;
        tree_extent_item.key_ = btree::Key{inode_id, btree::Type::EXTENT_DATA, (u64)offset};
        tree_extent_item.data_offset_ = ext_desc_addr; // Указывает на структуру ExtentItem
        tree_extent_item.data_size_ = sizeof(fs::Extentdata);
        tree.insert(tree_extent_item);

        std::cout << "[DEBUG] Wrote REGULAR extent for Inode " << inode_id 
                  << " at offset " << offset << "\n";
    }

    // 3. Обновляем размер файла в Inode, если мы дописали в конец
    if (offset + size > inode.size) {
        inode.size = offset + size;
        lseek(fd, inode_item->data_offset_, SEEK_SET);
        write(fd, &inode, sizeof(fs::InodeItem));
    }

    // Сбрасываем буферы на диск
    fsync(fd);
}



[[nodiscard]] u64 MiniBtrfs::resolve_path(u64 current_dir_id, const char* path) const {
    std::string path_str(path);
    if (path_str.empty()) return current_dir_id;

    u64 current_id = current_dir_id;

    // Если путь абсолютный (начинается с '/'), принудительно стартуем с корня
    if (path_str[0] == '/') {
        current_id = 256; // Базовый ROOT_ID
    }

    std::stringstream ss(path_str);
    std::string token;

    // Разбиваем путь по слешу '/'
    while (std::getline(ss, token, '/')) {
        // Пропускаем пустые токены (например, из "//" или начального "/")
        if (token.empty() || token == ".") {
            continue; 
        }

        // 1. Защита от попытки "войти" в обычный файл (эмуляция ENOTDIR)
        auto inode_item = tree.search(btree::Key{current_id, btree::Type::INODE_ITEM, 0});
        if (!inode_item) throw std::runtime_error("resolve: Corrupted inode link");

        fs::InodeItem inode;
        lseek(fd, inode_item->data_offset_, SEEK_SET);
        read(fd, &inode, sizeof(fs::InodeItem));

        if (inode.isDir()) { // 1 — это директория
            throw std::invalid_argument("resolve: Not a directory");
        }

        // 2. Читаем дескриптор текущей директории
        auto index_item = tree.search(btree::Key{current_id, btree::Type::DIR_INDEX, 0});
        if (!index_item) throw std::runtime_error("resolve: Directory metadata missing");

        fs::DirIndex p_index;
        lseek(fd, index_item->data_offset_, SEEK_SET);
        read(fd, &p_index, sizeof(fs::DirIndex));

        bool found = false;

        // 3. Сканируем элементы в поиске текущего токена
        for (u64 i = 0; i < p_index.cnt; i++) {
            auto dir_tree_item = tree.search(btree::Key{current_id, btree::Type::DIR_ITEM, i});
            if (!dir_tree_item) continue;

            fs::DirItem dir_item;
            lseek(fd, dir_tree_item->data_offset_, SEEK_SET);
            read(fd, &dir_item, sizeof(fs::DirItem));

            // Если имя совпало, "проваливаемся" на уровень ниже
            if (token == (const char*)dir_item.name) {
                current_id = dir_item.location.id_;
                found = true;
                break;
            }
        }

        // Если цикл завершился, а токен не найден — путь ошибочный
        if (!found) {
            throw std::invalid_argument("resolve: No such file or directory: " + token);
        }
    }

    // Возвращаем ID последнего найденного элемента (это может быть папка или файл)
    return current_id;
}

} // minibtrfs