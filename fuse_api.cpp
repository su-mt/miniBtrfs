#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <string>
#include <algorithm>
#include <vector>

#include "minibtrfs.hpp"

// Хелпер для быстрого доступа к объекту файловой системы
static inline MiniBtrfs* get_fs() {
    return static_cast<MiniBtrfs*>(fuse_get_context()->private_data);
}

// Хелпер для разбиения пути вида "/world/hello.txt" -> parent="/world", name="hello.txt"
static void split_path(const std::string& full_path, std::string& parent, std::string& filename) {
    size_t pos = full_path.find_last_of('/');
    if (pos == std::string::npos || full_path == "/") {
        parent = "/";
        filename = full_path;
    } else if (pos == 0) {
        parent = "/";
        filename = full_path.substr(1);
    } else {
        parent = full_path.substr(0, pos);
        filename = full_path.substr(pos + 1);
    }
}

// ---------------------------------------------------------
// Чтение метаданных (stat)
// ---------------------------------------------------------
static int mb_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    MiniBtrfs* fs = get_fs();

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0777; // Заглушка: всем всё разрешено
        stbuf->st_nlink = 2;
        return 0;
    }

    try {
        // resolve_path стартует с корня (256)
        u64 inode_id = fs->resolve_path(256, path);
        
        // Для этого нужно добавить публичный метод fs->get_inode(id) 
        // который делает tree.search и read InodeItem
        fs::InodeItem inode = fs->get_inode(inode_id); 
        
        if (inode.isDir()) { // Директория
            stbuf->st_mode = S_IFDIR | 0777;
            stbuf->st_nlink = 2;
        } else {               // Обычный файл
            stbuf->st_mode = S_IFREG | 0777;
            stbuf->st_nlink = 1;
            stbuf->st_size = inode.size;
        }
        return 0;
    } catch (...) {
        return -ENOENT; // Вернет ошибку, если пути нет
    }
}

// ---------------------------------------------------------
// Чтение содержимого директории (ls)
// ---------------------------------------------------------
static int mb_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    MiniBtrfs* fs = get_fs();

    try {
        u64 dir_id = fs->resolve_path(256, path);

        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);

        // Для этого понадобится метод, возвращающий список имен в папке.
        // Или можно добавить метод fs->get_dir_entries(dir_id) 
        // который делает тот же цикл, что и в вашем старом REPL ls.
        std::vector<std::string> entries = fs->get_dir_entries(dir_id);
        
        for (const auto& name : entries) {
            filler(buf, name.c_str(), NULL, 0);
        }
        
        return 0;
    } catch (...) {
        return -ENOENT;
    }
}

// ---------------------------------------------------------
// Создание директории (mkdir)
// ---------------------------------------------------------
static int mb_mkdir(const char *path, mode_t mode) {
    MiniBtrfs* fs = get_fs();
    std::string parent_path, new_dir_name;
    split_path(path, parent_path, new_dir_name);
    
    try {
        u64 parent_id = fs->resolve_path(256, parent_path.c_str());
        fs->mkdir(parent_id, new_dir_name.c_str());
        return 0;
    } catch (...) {
        return -ENOENT;
    }
}

// ---------------------------------------------------------
// Создание пустого файла (touch)
// ---------------------------------------------------------
static int mb_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    MiniBtrfs* fs = get_fs();
    std::string parent_path, filename;
    split_path(path, parent_path, filename);
    
    try {
        u64 parent_id = fs->resolve_path(256, parent_path.c_str());
        fs->create_file(parent_id, filename.c_str());
        return 0;
    } catch (...) {
        return -ENOENT;
    }
}

// ---------------------------------------------------------
// Чтение данных из файла (cat)
// ---------------------------------------------------------
static int mb_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    MiniBtrfs* fs = get_fs();

    try {
        u64 inode_id = fs->resolve_path(256, path);
        
        // Получаем вектор байт (от offset до конца файла)
        std::vector<u8> data = fs->read_file(inode_id, offset);
        
        // FUSE ожидает не больше size байт. Вычисляем, сколько реально отдадим.
        size_t bytes_to_copy = std::min((size_t)size, data.size());
        
        if (bytes_to_copy > 0) {
            memcpy(buf, data.data(), bytes_to_copy);
        }
        
        return bytes_to_copy; // FUSE сдвинет указатель сам
    } catch (...) {
        return -ENOENT;
    }
}

// ---------------------------------------------------------
// Запись данных в файл (echo, >)
// ---------------------------------------------------------
static int mb_write(const char *path, const char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    MiniBtrfs* fs = get_fs();

    try {
        u64 inode_id = fs->resolve_path(256, path);
        fs->write_file(inode_id, (const u8*)buf, size, offset);
        
        return size; // Сообщаем VFS, что успешно записали всё запрошенное
    } catch (...) {
        return -ENOENT;
    }
}

// ---------------------------------------------------------
// Удаление (Опционально)
// ---------------------------------------------------------
static int mb_unlink(const char *path) {
    return -ENOSYS; // Заглушка: "Функция не реализована" (пока что)
}

// Регистрация коллбеков
static struct fuse_operations mb_oper = {};

// Инициализируем структуру (решение проблемы C-style designated initializers в C++)
void init_fuse_operations() {
    mb_oper.getattr    = mb_getattr;
    mb_oper.mkdir      = mb_mkdir;
    mb_oper.unlink     = mb_unlink;
    mb_oper.read       = mb_read;
    mb_oper.write      = mb_write;
    mb_oper.readdir    = mb_readdir;
    mb_oper.create     = mb_create;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <image_file> <mountpoint> [FUSE options]\n";
        return 1;
    }

    const char* image_file = argv[1];

    int fuse_argc = argc - 1;
    char** fuse_argv = new char*[fuse_argc];
    fuse_argv[0] = argv[0]; 
    for (int i = 1; i < fuse_argc; i++) {
        fuse_argv[i] = argv[i + 1];
    }

    init_fuse_operations();

    try {
        MiniBtrfs fs(image_file);
        std::cout << "Mounting MiniBtrfs to " << fuse_argv[1] << "..." << std::endl;

        // FUSE уходит в фоновый цикл (или работает в foreground, если передать -f)
        int status = fuse_main(fuse_argc, fuse_argv, &mb_oper, &fs);
        
        delete[] fuse_argv;
        return status;

    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        delete[] fuse_argv;
        return 1;
    }
}