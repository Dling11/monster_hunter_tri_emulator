#pragma once
#ifdef _WIN32
#include "win_posix_dir.h"
#include <cwchar>

namespace mh3_windows_nand {
// CREATE_ALWAYS (including std::ofstream truncation) removes named NTFS streams.
// Keep Wii metadata outside the NAND tree, keyed by the stable NTFS file ID.
// This survives content overwrites and renames without affecting NAND quotas.
inline std::filesystem::path MetadataPath(int fd) {
    const auto raw=_get_osfhandle(fd);
    if(raw==-1) { errno=EBADF; return {}; }
    HANDLE file=reinterpret_cast<HANDLE>(raw);
    bool closeFile=false;
    if(GetFileType(file)==FILE_TYPE_CHAR) {
        std::string directory;
        { std::lock_guard lock(mh3_dirs_lock());
          auto found=mh3_dirs().find(fd);
          if(found!=mh3_dirs().end()) directory=found->second;
        }
        if(directory.empty()) { errno=EBADF; return {}; }
        file=CreateFileW(std::filesystem::path(directory).c_str(),FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(file==INVALID_HANDLE_VALUE) { errno=EIO; return {}; }
        closeFile=true;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid=GetFileInformationByHandle(file,&info);
    if(closeFile) CloseHandle(file);
    if(!valid || !(info.nFileIndexHigh || info.nFileIndexLow)) { errno=EIO; return {}; }
    wchar_t identity[40]{};
    std::swprintf(identity,40,L"%08x-%08x-%08x.meta",unsigned(info.dwVolumeSerialNumber),
        unsigned(info.nFileIndexHigh),unsigned(info.nFileIndexLow));
    const char* configured=std::getenv("MH3_USER_DIR");
    const auto user=configured && *configured ? std::filesystem::path(configured)
        : mh3_self_exe().parent_path()/"MH3_Rowell_Dev";
    return user/".nand-metadata"/identity;
}
inline int Set(int fd,const void* data,DWORD size) {
    auto path=MetadataPath(fd); if(path.empty()) return -1;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(),error);
    if(error) { errno=EIO; return -1; }
    auto temporary=path.wstring()+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId());
    HANDLE output=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ,
        nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(output==INVALID_HANDLE_VALUE) { errno=EIO; return -1; }
    DWORD written=0;
    const bool complete=WriteFile(output,data,size,&written,nullptr) && written==size && FlushFileBuffers(output);
    CloseHandle(output);
    if(!complete || !MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); errno=EIO; return -1;
    }
    return 0;
}
inline int Get(int fd,void* data,DWORD size,bool& missing) {
    missing=false;
    auto path=MetadataPath(fd); if(path.empty()) return -1;
    HANDLE input=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(input==INVALID_HANDLE_VALUE) {
        const auto code=GetLastError();
        if(code==ERROR_FILE_NOT_FOUND || code==ERROR_PATH_NOT_FOUND) { missing=true; return 0; }
        errno=EIO; return -1;
    }
    LARGE_INTEGER length{}; DWORD read=0;
    const bool complete=GetFileSizeEx(input,&length) && length.QuadPart==size && ReadFile(input,data,size,&read,nullptr) && read==size;
    CloseHandle(input);
    if(!complete) { errno=EIO; return -1; }
    return 0;
}
}
#endif
