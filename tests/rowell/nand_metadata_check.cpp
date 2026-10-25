#include "windows_nand_metadata.h"
#include <array>
#include <iostream>
#include <fstream>

int main(int argc,char** argv) {
    if(argc!=2) return 2;
    namespace fs=std::filesystem;
    fs::path root=fs::absolute(argv[1])/std::to_string(GetCurrentProcessId());
    fs::create_directories(root);
    _putenv_s("MH3_USER_DIR",root.string().c_str());
    std::array<unsigned char,10> expected{0,0,0x10,0,0x30,0x38,3,1,0,0x20},actual{};
    auto check=[&](bool ok,const char* name) { if(!ok) std::cerr<<name<<" failed\n"; return ok; };
    auto path=root/"original";
    int fd=mh3_open(path.string().c_str(),O_CREAT|O_RDWR,0600);
    bool missing=false;
    if(!check(fd>=0,"open file"))return 1;
    if(!check(mh3_windows_nand::Get(fd,actual.data(),10,missing)==0 && missing,"default metadata"))return 1;
    if(!check(mh3_windows_nand::Set(fd,expected.data(),10)==0,"write metadata"))return 1;
    _close(fd);
    { std::ofstream body(path); body << "saved"; }
    fd=mh3_open(path.string().c_str(),O_RDONLY);
    if(!check(mh3_windows_nand::Get(fd,actual.data(),10,missing)==0 && !missing && actual==expected,"reopen persistence"))return 1;
    _close(fd);
    auto renamed=root/"renamed";fs::rename(path,renamed);
    fd=mh3_open(renamed.string().c_str(),O_RDONLY);
    if(!check(mh3_windows_nand::Get(fd,actual.data(),10,missing)==0 && actual==expected,"rename persistence"))return 1;
    const int rootFd=mh3_open(root.string().c_str(),O_RDONLY|O_DIRECTORY);
    if(!check(mh3_same_file(fd,rootFd,"renamed") && !mh3_same_file(fd,rootFd,".nand-metadata"),"open file identity"))return 1;
    _close(rootFd);
    _close(fd);
    auto dir=root/"directory";fs::create_directory(dir);
    fd=mh3_open(dir.string().c_str(),O_RDONLY|O_DIRECTORY);
    if(!check(mh3_windows_nand::Set(fd,expected.data(),10)==0,"directory metadata"))return 1;
    _close(fd);fd=mh3_open(dir.string().c_str(),O_RDONLY|O_DIRECTORY);
    if(!check(mh3_windows_nand::Get(fd,actual.data(),10,missing)==0 && actual==expected,"directory reopen"))return 1;
    _close(fd);
    fd=mh3_open((root/"after-directory").string().c_str(),O_CREAT|O_RDWR,0600);
    if(!check(mh3_windows_nand::Get(fd,actual.data(),10,missing)==0 && missing,"reused descriptor metadata isolation"))return 1;
    _close(fd);
    if(!check(fs::directory_iterator(root)!=fs::directory_iterator{},"fixture retained"))return 1;
    std::cout<<"Windows NAND metadata: persistence and rename checks passed.\n";
    return 0;
}
