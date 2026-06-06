#include "btree.hpp"
#include "fs.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>



int fd;


int main (int argc, char** argv) {
    if (argc != 3){        
        std::cout << "Usage: " << argv[0] << " <file> <size>" << std::endl;
        return 0;
    }


    if (atoi(argv[2]) < MIN_FS_SIZE){
        std::cout << "Size of fs must be greater than " << MIN_FS_SIZE << std::endl;
        return 0;
    }


    fd = open(argv[1], O_CREAT | O_RDWR, 0644);
    ftruncate(fd, atoi(argv[2]));

    fs::SuperBlock sb;
    btree::Node root(sb.fsid_, 0);

    lseek(fd, sb.bytenr_, SEEK_SET);
    write(fd, &sb, sizeof(fs::SuperBlock));

    lseek(fd, sb.root_tree_root_, SEEK_SET);
    write(fd, &root, BLOCK_SIZE);


    fs::InodeItem root_inode(fs::InodeType::Dir);
    u64 root_inode_addr = sb.allocate_block(fd);

    lseek(fd, root_inode_addr, SEEK_SET);
    write(fd, &root_inode, sizeof(fs::InodeItem));
    
    btree::Item inode_tree_item;
    inode_tree_item.key_ = btree::Key{256, btree::Type::INODE_ITEM, 0};
    inode_tree_item.data_offset_ = root_inode_addr;
    inode_tree_item.data_size_ = sizeof(fs::InodeItem);

    BTree tree (sb, root, fd);

    tree.insert(inode_tree_item);


    fs::InodeRef root_inode_ref;
    root_inode_ref.parent = btree::Key{256, btree::Type::INODE_ITEM, 0};
    std::memcpy(root_inode_ref.name, "/", 2);

    u64 ref_addr = sb.allocate_block(fd);
    lseek(fd, ref_addr, SEEK_SET);
    write(fd, &root_inode_ref, sizeof(fs::InodeRef));

    btree::Item ref_tree_item;
    ref_tree_item.key_ = btree::Key{256, btree::Type::INODE_REF, 256};
    ref_tree_item.data_offset_ = ref_addr;
    ref_tree_item.data_size_ = sizeof(fs::InodeRef);

    tree.insert(ref_tree_item);


    // 3. Создаем дескриптор содержимого корневой директории (пока пустого)
    fs::DirIndex root_dir_index;
    root_dir_index.cnt = 0; // В папке '/' пока нет файлов
    // Устанавливаем стартовый ключ для поиска файлов внутри '/'
    root_dir_index.start = btree::Key{256, btree::Type::DIR_ITEM, 0}; 

    // Выделяем физический блок для DirIndex
    u64 index_addr = sb.allocate_block(fd);
    
    // Записываем DirIndex на диск
    lseek(fd, index_addr, SEEK_SET);
    write(fd, &root_dir_index, sizeof(fs::DirIndex));

    // 4. Вставляем DIR_INDEX в B-дерево
    btree::Item index_tree_item;
    index_tree_item.key_ = btree::Key{256, btree::Type::DIR_INDEX, 0};
    index_tree_item.data_offset_ = index_addr;
    index_tree_item.data_size_ = sizeof(fs::DirIndex);

    tree.insert(index_tree_item);




    fsync(fd);
    close(fd);


    return 0;
}