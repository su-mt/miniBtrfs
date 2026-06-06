#include "btree.hpp"
#include "fs.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <file>\n";
        return 0;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) {
        std::cerr << "Failed to open device.\n";
        return 1;
    }

    // 1. Читаем девайс и заполняем структуру SuperBlock
    fs::SuperBlock sb;
    read(fd, &sb, sizeof(fs::SuperBlock));

    std::cout << "=== SUPERBLOCK ===\n";
    std::cout << "Magic: " << std::string((char*)sb.magic_, 8) << "\n";
    std::cout << "Node size: " << sb.nodesize_ << "\n";
    std::cout << "Root tree address: " << sb.root_tree_root_ << "\n\n";

    // 2. Идем по адресу корня и читаем узел дерева
    btree::Node root;
    lseek(fd, sb.root_tree_root_, SEEK_SET);
    read(fd, &root, BLOCK_SIZE);

    std::cout << "=== ROOT NODE ===\n";
    std::cout << "Level: " << (int)root.hdr_.level_ << "\n";
    std::cout << "Items count: " << root.hdr_.nritems_ << "\n\n";

    // 3. Выводим все элементы (паспорта), которые есть в корне
    for (u32 i = 0; i < root.hdr_.nritems_; i++) {
        const auto& item = root.item(i);
        std::cout << "Item [" << i << "]:\n";
        std::cout << "  Key(id=" << item.key_.id_ 
                  << ", type=" << (int)item.key_.type_ 
                  << ", offset=" << item.key_.offset_ << ")\n";
        std::cout << "  Data Offset: " << item.data_offset_ << "\n";
        std::cout << "  Data Size: " << item.data_size_ << "\n";
        std::cout << "-------------------\n";
    }

    close(fd);
    return 0;
}