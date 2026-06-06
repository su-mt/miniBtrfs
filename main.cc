#include <iostream>
#include <stack>
#include <string>
#include <utility>
#include <vector>
#include <optional>
#include "minibtrfs.hpp"

using namespace minibtrfs;

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <image_file>\n";
        return 1;
    }

    int dir_id = 256, prevVisDirId = 256, prevDirId = 256;
    std::stack<int> dirStack; 
    dirStack.push(dir_id);
    
    std::string dirname;
    std::string cmd;
    
    try {
        // Конструктор может выбрасывать исключения при критических ошибках 
        // (например, не найден файл образа), поэтому внешний try-catch оставляем.
        MiniBtrfs FS(argv[1]);
        
        for (;;) {
            std::cout << "BTFS>> ";
            std::cin >> cmd;

            if (cmd == "ls"){
                if (!FS.ls(dir_id)) {
                    std::cerr << "ls: failed to list directory\n";
                }
                
            } else if (cmd == "mkdir") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);
                if (!FS.mkdir(dir_id, dirname.c_str())) {
                    std::cerr << "mkdir: failed to create directory\n";
                }
                
            } else if (cmd == "cd"){
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);

                if (dirname == "."){
                    continue;
                } else if (dirname == "-"){
                    std::swap(dir_id, prevVisDirId);
                } else if (dirname == "..") {
                    // FIXME:: this impl use stack, but parent id stored in INODE_REF, we should use it;
                    prevVisDirId = dir_id;
                    if (!dirStack.empty()){
                        dir_id = dirStack.top(); 
                        dirStack.pop();
                    }
                } else if (dirname == "/") {
                    prevVisDirId = dir_id;
                    dir_id = 256;
                    while (!dirStack.empty()) {
                        dirStack.pop();
                    }
                } else {
                    auto next_dir_opt = FS.cd(dir_id, dirname.c_str());
                    if (next_dir_opt) {
                        prevVisDirId = dir_id;
                        dirStack.push(dir_id);
                        dir_id = *next_dir_opt;
                    } else {
                        std::cerr << "cd: No such directory or access failed\n";
                    }
                }
                
            } else if (cmd == "touch") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);
                if (!FS.create_file(dir_id, dirname.c_str())) {
                    std::cerr << "touch: failed to create file\n";
                }
                
            } else if (cmd == "vim") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);

                auto inode_opt = FS.resolve_path(dir_id, dirname.c_str());
                if (inode_opt) {
                    std::cout << "VIM>> ";
                    char c;
                    std::string input;
                    std::string exit_seq = ":wq";
                    
                    while (std::cin.get(c)) {
                        input.push_back(c);
                        if (input.size() >= 3 && input.substr(input.size() - 3) == exit_seq) {
                            input.erase(input.size() - 3);
                            break;
                        }
                    }
                    
                    if (!FS.write_file(*inode_opt, static_cast<const void*>(input.c_str()), input.size(), 0)) {
                        std::cerr << "vim: failed to write file\n";
                    }
                } else {
                    std::cerr << "vim: path not found or invalid\n";
                }

            } else if (cmd == "cat") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);
                
                auto inode_opt = FS.resolve_path(dir_id, dirname.c_str());
                if (inode_opt) {
                    auto data_opt = FS.read_file(*inode_opt, 0);
                    if (data_opt) {
                        std::cout << std::string(data_opt->begin(), data_opt->end()) << "\n";
                    } else {
                        std::cerr << "cat: failed to read file\n";
                    }
                } else {
                    std::cerr << "cat: path not found or invalid\n";
                }
                
            } else if (cmd == "exit"){
                std::cout << "Connection with " << argv[1] << " closed!\n";
                return 0;
            } else {
                std::cout << "Command not found!\n";
            }
        }
    } catch (const std::exception& e) {
        // Оставляем только для фатальных ошибок (например, падение при инициализации образа)
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}