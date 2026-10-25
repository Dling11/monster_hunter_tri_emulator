#include "touch_controls.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include "memory.h"
#include "save_manager.h"
#include "mh3_frame_rate_control.h"

// Input checks run the real touch engine without a game or renderer. Camera
// output and the optional window are the only hooks used by these test cases.
void QueueTouchCameraDelta(float,float) { std::abort(); }
extern "C" SDL_Window* aurora_get_window() { return nullptr; }
// These production hooks are linked for the menu branches in the real engine.
// The input-only cases must never invoke them; abort makes accidental use fail.
void aurora_enable_vsync(bool) { std::abort(); }
void SetWidescreenSetting(bool) { std::abort(); }
extern "C" void VISetFrameBufferScale(float) { std::abort(); }
extern "C" void aurora_set_fullscreen(bool) { std::abort(); }
extern "C" bool aurora_get_fullscreen() { std::abort(); }
extern "C" void aurora_set_native_target_aspect(int) { std::abort(); }
extern "C" int Gamepad_GetLayout() { std::abort(); }
extern "C" bool Gamepad_GetSwapTriggers() { std::abort(); }
extern "C" void Gamepad_SetLayout(int) { std::abort(); }
extern "C" void Gamepad_SetSwapTriggers(bool) { std::abort(); }
bool Memory::Contains(uint32_t,size_t) { std::abort(); }
namespace MH3FrameRateControl {
void SetTargetFPS(int) noexcept { std::abort(); }
uint64_t LogicalFrameCount() noexcept { std::abort(); }
}
namespace mh3_save_manager {
unsigned backupCalls = 0;
unsigned exportCalls = 0, importCalls = 0, stageCalls = 0;
fs::path stagedImport;
unsigned BackupCallsForTesting() { return backupCalls; }
unsigned ExportCallsForTesting() { return exportCalls; }
unsigned ImportCallsForTesting() { return importCalls; }
unsigned StageCallsForTesting() { return stageCalls; }
fs::path StagedImportForTesting() { return stagedImport; }
bool RestartForTesting() { return true; }
std::string CopyCreatedTime(const fs::path& folder) {
    return folder.filename().string().rfind("Backup-",0)==0?"25 Oct 2026, 12:34:56 PM (local time)":"Creation time not recorded";
}
std::vector<SaveCopy> ListSaveCopies(const fs::path& userDir,bool backups) {
    if(backups) {
        if(!fs::is_directory(userDir/"saves"))return {};
        return {{userDir/"saves/Backup-20261025-180000-001","25 Oct 2026, 06:00:00 PM (local time)","Backup"},
                {userDir/"saves/Backup-20261025-170000-001","25 Oct 2026, 05:00:00 PM (local time)","Backup"},
                {userDir/"saves/Backup-20261025-160000-001","25 Oct 2026, 04:00:00 PM (local time)","Backup"}};
    }
    if(fs::is_directory(userDir/"imports/FixtureImport"))return {{userDir/"imports/FixtureImport","Creation time not recorded","imports"}};
    return {};
}
Result ExportSave(const fs::path&,const fs::path& root) {
    ++exportCalls;
    return {true, root / "ExportFixture", {}};
}
Result CreateBackup(const fs::path&,const fs::path& root) {
    ++backupCalls;
    return {true, root / "Backup-20261025-123456-001", {}};
}
Result StageImport(const fs::path& source,const fs::path& pending) {
    ++stageCalls;stagedImport=source;
    return {true, pending, {}};
}
Result QueueRestore(const fs::path& source,const fs::path& user) { return StageImport(source,user/"PendingRestore"); }
Result ApplyPendingImport(const fs::path& save,const fs::path&,const fs::path&) {
    ++importCalls;
    return {true, save, {}};
}
}
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    std::filesystem::create_directories(argv[1]);
    _putenv_s("MH3_USER_DIR",argv[1]);
    return TouchControls_RunInputChecks()?0:1;
}
