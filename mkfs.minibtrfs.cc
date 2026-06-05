#include "btree.hpp"
#include "fs.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>



int fd;

bool format () {

    return true;

}




int main (int argc, char** argv) {
    if (argc != 3){        
        std::cout << "Usage: -f <file> -s <size>" << std::endl;
        return 0;
    }

    if (atoi(argv[2]) < MIN_FS_SIZE){
        std::cout << "Size of fs must be " << MIN_FS_SIZE << std::endl;
    }

    fd = open(argv[1], O_CREAT | O_RDWR, 0644);


    fs::SuperBlock sb;
    btree::Node root(sb.fsid_, 0);





    return 0;
}