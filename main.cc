#include <ios>
#include <iostream>
#include <ostream>
#include <string>
#include "minibtrfs.hpp"
int main(int argc, char** argv)
{

    std::string dirname;
    std::string cmd;
    try {
        MiniBtrfs FS (argv[1]);
        for (;;) {
            std::cout << "BTFS>> ";
            std::cin >> cmd;

            if (cmd == "ls"){
                try {
                    FS.ls(256);
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
                }
                
            } else if (cmd == "mkdir") {
                std::cin >> std::ws; 
                std::getline(std::cin, dirname);
                try {
                    FS.mkdir(256, dirname.c_str());
                } catch (const std::exception& e) {
                    std::cerr << e.what() << "\n";
                }
            }else if (cmd == "exit"){
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