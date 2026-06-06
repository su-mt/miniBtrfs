#define FUSE_USE_VERSION 29
#include <fuse.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <iostream>
#include <string>
#include <vector>
#include <optional>

#include "minibtrfs.hpp"

// Настройка уровня логирования
#ifndef MB_LOG_LEVEL
#define MB_LOG_LEVEL 0
#endif

#define LOG_BTRFS(msg) do { if (MB_LOG_LEVEL >= 1) std::cerr << "[BTRFS] " << msg << std::endl; } while(0)
#define LOG_FUSE(msg)  do { if (MB_LOG_LEVEL >= 2) std::cerr << "[FUSE]  " << msg << std::endl; } while(0)

// Хелпер для быстрого доступа к объекту файловой системы
static inline minibtrfs::MiniBtrfs* get_fs() {
    return static_cast<minibtrfs::MiniBtrfs*>(fuse_get_context()->private_data);
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
    LOG_FUSE("getattr called for " << path);
    memset(stbuf, 0, sizeof(struct stat));
    minibtrfs::MiniBtrfs* fs = get_fs();

    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0777; 
        stbuf->st_nlink = 2;
        return 0;
    }

    auto inode_id_opt = fs->resolve_path(256, path);
    if (!inode_id_opt) {
        LOG_FUSE("getattr: path not found");
        return -ENOENT;
    }

    auto inode_opt = fs->get_inode(*inode_id_opt); 
    if (!inode_opt) {
        LOG_FUSE("getattr: failed to read inode");
        return -ENOENT;
    }

    if (inode_opt->isDir()) { 
        stbuf->st_mode = S_IFDIR | 0777;
        stbuf->st_nlink = 2;
    } else {               
        stbuf->st_mode = S_IFREG | 0777;
        stbuf->st_nlink = 1;
        stbuf->st_size = inode_opt->size;
    }
    
    return 0;
}

// ---------------------------------------------------------
// Чтение содержимого директории (ls)
// ---------------------------------------------------------
static int mb_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    LOG_FUSE("readdir called for " << path);
    minibtrfs::MiniBtrfs* fs = get_fs();

    auto dir_id_opt = fs->resolve_path(256, path);
    if (!dir_id_opt) {
        LOG_FUSE("readdir: directory not found");
        return -ENOENT;
    }

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    auto entries_opt = fs->get_dir_entries(*dir_id_opt);
    if (!entries_opt) {
        LOG_FUSE("readdir: failed to fetch entries");
        return -ENOENT;
    }
    
    for (const auto& name : *entries_opt) {
        filler(buf, name.c_str(), NULL, 0);
    }
    
    return 0;
}

// ---------------------------------------------------------
// Создание директории (mkdir)
// ---------------------------------------------------------
static int mb_mkdir(const char *path, mode_t mode) {
    LOG_FUSE("mkdir called for " << path);
    minibtrfs::MiniBtrfs* fs = get_fs();
    std::string parent_path, new_dir_name;
    split_path(path, parent_path, new_dir_name);
    
    auto parent_id_opt = fs->resolve_path(256, parent_path.c_str());
    if (!parent_id_opt) {
        LOG_FUSE("mkdir: parent path not found");
        return -ENOENT;
    }

    if (!fs->mkdir(*parent_id_opt, new_dir_name.c_str())) {
        LOG_FUSE("mkdir: underlying FS error");
        return -EIO;
    }
    
    return 0;
}

// ---------------------------------------------------------
// Создание пустого файла (touch)
// ---------------------------------------------------------
static int mb_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    LOG_FUSE("create called for " << path);
    minibtrfs::MiniBtrfs* fs = get_fs();
    std::string parent_path, filename;
    split_path(path, parent_path, filename);
    
    auto parent_id_opt = fs->resolve_path(256, parent_path.c_str());
    if (!parent_id_opt) {
        LOG_FUSE("create: parent path not found");
        return -ENOENT;
    }

    if (!fs->create_file(*parent_id_opt, filename.c_str())) {
        LOG_FUSE("create: underlying FS error");
        return -EIO;
    }

    return 0;
}

// ---------------------------------------------------------
// Чтение данных из файла (cat)
// ---------------------------------------------------------
static int mb_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    LOG_FUSE("read called for " << path << " (offset=" << offset << ", size=" << size << ")");
    minibtrfs::MiniBtrfs* fs = get_fs();

    auto inode_id_opt = fs->resolve_path(256, path);
    if (!inode_id_opt) {
        LOG_FUSE("read: path not found");
        return -ENOENT;
    }
    
    auto data_opt = fs->read_file(inode_id_opt.value(), offset, size);
    
    // 1. Проверяем именно ошибку (если метод вернул std::nullopt)
    if (!data_opt) {
        LOG_FUSE("read: critical error reading data from tree");
        return -EIO;
    }
    
    // 2. Если вектор пустой — это нормальный честный EOF. Возвращаем 0.
    if (data_opt->empty()) {
        LOG_FUSE("read: reach EOF at offset " << offset);
        return 0; 
    }
    
    // 3. Копируем данные, если они есть
    memcpy(buf, data_opt->data(), data_opt->size());
    return (int)data_opt->size(); 
}


// ---------------------------------------------------------
// Запись данных в файл (echo, >)
// ---------------------------------------------------------
static int mb_write(const char *path, const char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi) {
    LOG_FUSE("write called for " << path << " (offset=" << offset << ", size=" << size << ")");
    minibtrfs::MiniBtrfs* fs = get_fs();

    auto inode_id_opt = fs->resolve_path(256, path);
    if (!inode_id_opt) {
        LOG_FUSE("write: path not found");
        return -ENOENT;
    }
    
    if (!fs->write_file(*inode_id_opt, (const u8*)buf, size, offset)) {
        LOG_FUSE("write: failed to write data");
        return -EIO;
    }
    
    return size; 
}

// ---------------------------------------------------------
// Удаление (Опционально)
// ---------------------------------------------------------
static int mb_unlink(const char *path) {
    LOG_FUSE("unlink called for " << path);
    return -ENOSYS; 
}

// Регистрация коллбеков
static struct fuse_operations mb_oper = {};

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

    minibtrfs::MiniBtrfs fs(image_file);
    std::cout << "Mounting MiniBtrfs to " << fuse_argv[1] << "..." << std::endl;
    LOG_FUSE("Fuse API Started. MB_LOG_LEVEL = " << MB_LOG_LEVEL);

    int status = fuse_main(fuse_argc, fuse_argv, &mb_oper, &fs);
    
    delete[] fuse_argv;
    return status;
}