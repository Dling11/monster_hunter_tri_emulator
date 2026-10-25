#include "rowell_touch.h"
#include "touch_controls.h"
#include "save_manager.h"
#include "../../aurora-main/lib/window.hpp"
#include <SDL3/SDL.h>
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <algorithm>

namespace {
SDL_Window* touchWindow = nullptr;
std::atomic<int> padLayout{0};
std::atomic<bool> swapTriggers{false};
std::once_flag padConfigLoaded;
std::filesystem::path UserDir() {
    const char* dir = std::getenv("MH3_USER_DIR");
    return dir && *dir ? std::filesystem::path(dir) : std::filesystem::path("MH3_Rowell_Dev");
}
void LoadPadConfig() {
    std::call_once(padConfigLoaded, [] {
        int layout=0, swap=0;
        std::ifstream in(UserDir()/"gamepad.cfg");
        if(in >> layout >> swap) { padLayout.store(layout==1?1:0); swapTriggers.store(swap!=0); }
    });
}
void SavePadConfig() {
    std::ofstream out(UserDir()/"gamepad.cfg");
    if(out) out << padLayout.load() << ' ' << swapTriggers.load() << '\n';
}
}

void RowellTouch_InitializeUserDir() {
    // The development build defaults to its own directory, never the installed
    // game's Saves folder. Explicit test environments can select a temp directory.
    const char* existing = std::getenv("MH3_USER_DIR");
    if(!existing || !*existing) {
        wchar_t path[32768]{};
        const DWORD count=GetModuleFileNameW(nullptr,path,32768);
        if(count && count<32768) {
            auto dir=std::filesystem::path(path).parent_path()/"MH3_Rowell_Dev";
            _putenv_s("MH3_USER_DIR",dir.string().c_str());
        } else _putenv_s("MH3_USER_DIR","MH3_Rowell_Dev");
    }
    std::filesystem::create_directories(UserDir());
}

void RowellTouch_Attach(SDL_Window* window) {
    touchWindow=window;
    SDL_SetWindowTitle(window,"MH Tri - Rowell | F2 Settings | F1 Original settings");
    TouchControls_Init();
    SDL_ShowCursor();
    if (std::getenv("MH3_SHOW_TOUCH_SETTINGS")) TouchControls_SetMenuOpen(true);
}

bool RowellTouch_WaitForRestartParent(int& argc,char** argv) {
    for(int i=1;i+1<argc;++i)if(std::string_view(argv[i])=="--rowell-wait-parent") {
        char* end=nullptr;const auto pid=std::strtoul(argv[i+1],&end,10);
        if(!pid || !end || *end || pid==GetCurrentProcessId())return false;
        HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,DWORD(pid));
        if(parent) {const auto state=WaitForSingleObject(parent,30000);CloseHandle(parent);if(state!=WAIT_OBJECT_0)return false;}
        else if(GetLastError()!=ERROR_INVALID_PARAMETER)return false;
        std::copy(argv+i+2,argv+argc,argv+i);argc-=2;argv[argc]=nullptr;break;
    }
    return true;
}
bool RowellTouch_RestartGame() {
    wchar_t executable[32768]{};const auto length=GetModuleFileNameW(nullptr,executable,32768);
    if(!length || length>=32768)return false;
    std::wstring command=L"\""+std::wstring(executable)+L"\" --rowell-wait-parent "+std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW start{};start.cb=sizeof(start);PROCESS_INFORMATION child{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&child))return false;
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
    // The new process waits here before touching NAND, so old handles are gone.
    ExitProcess(0);
}

extern "C" SDL_Window* aurora_get_window() { return touchWindow; }
extern "C" void aurora_set_fullscreen(bool enabled) { if(touchWindow) aurora::window::set_fullscreen(enabled); }
extern "C" bool aurora_get_fullscreen() { return touchWindow && aurora::window::get_fullscreen(); }
extern "C" int Gamepad_GetLayout() { LoadPadConfig(); return padLayout.load(); }
extern "C" bool Gamepad_GetSwapTriggers() { LoadPadConfig(); return swapTriggers.load(); }
extern "C" void Gamepad_SetLayout(int layout) { LoadPadConfig(); padLayout.store(layout==1?1:0); SavePadConfig(); }
extern "C" void Gamepad_SetSwapTriggers(bool swap) { LoadPadConfig(); swapTriggers.store(swap); SavePadConfig(); }
