#include <iostream>
#include <ostream>
#include <stack>
#include <string>
#include <utility>
#include "minibtrfs.hpp"
int main(int argc, char** argv)
{
    int dir_id = 256, prevVisDirId = 256, prevDirId = 256;
    std::stack<int> dirStack; dirStack.push(dir_id);
    std::string dirname;
    std::string cmd;
    try {
        MiniBtrfs FS (argv[1]);
        for (;;) {
            std::cout << "BTFS>> ";
            std::cin >> cmd;

            if (cmd == "ls"){
                try {
                    FS.ls(dir_id);
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
                }
                
            } else if (cmd == "mkdir") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);
                try {
                    FS.mkdir(dir_id, dirname.c_str());
                } catch (const std::invalid_argument& e) {
                    std::cout << e.what() << "\n";
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
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
                        dir_id = dirStack.top(); dirStack.pop();
                    }
                } else if (dirname == "/") {
                    prevVisDirId = dir_id;
                    dir_id = 256;
                    while (!dirStack.empty()) {
                        dirStack.pop();
                    }
                } else {
                    try {
                        prevVisDirId = dir_id;
                        dirStack.push(dir_id);
                        dir_id = FS.cd(dir_id, dirname.c_str());
                    } catch (const std::invalid_argument& e) {
                        std::cout << e.what() << "\n";
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << "\n";
                    }
                }

                
            } else if (cmd == "touch") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);
                try {
                    FS.create_file(dir_id, dirname.c_str());
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
                }
            } else if (cmd == "vim") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);

                bool ok = true;
                u64 inode_id;
                try {
                    inode_id = FS.resolve_path(dir_id, dirname.c_str());
                } catch (const std::invalid_argument& e) {
                    std::cout << e.what() << "\n";
                    ok = false;
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
                    ok = false;
                }
                if (ok) {
                    std::cout << "VIM>> ";
                    char c;
                    std::string input;
                    std::string exit; exit += ":wq";
                    while (std::cin.get(c)) {
                        input.push_back(c);
                        if (input.size() >= 3 &&
                            input.substr(input.size() - 3) == exit) {
                            input.erase(input.size() - 3);
                            break;
                        }
                    }
                    try {
                        FS.write_file(inode_id, static_cast<const void*>(input.c_str()), input.size() ,0);
                    } catch (const std::invalid_argument& e) {
                        std::cout << e.what() << "\n";
                        
                    } catch (const std::exception& e) {
                        std::cerr << e.what() << "\n";
                    }
                }

            } else if (cmd == "exit"){
                std::cout << "Connection with " << argv[1] << " closed!" << std::endl;
                return 0;
            } else {
                std::cout << "Command not found!" << std::endl;
            }
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    
    return 0;
}