#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

// Separate from character saves, touch layout and physical/keyboard mappings.
namespace RowellFeatures {
inline std::atomic<bool> cutsceneSkip{false};
inline std::atomic<bool> androidKeyboard{true};
inline std::atomic<bool> physicsLog{false}; // Session only; never enabled at startup.

inline std::filesystem::path UserDir() {
    if (const char* dir = std::getenv("MH3_USER_DIR"); dir && *dir) return dir;
#ifdef __ANDROID__
    return "/storage/emulated/0/MH3_Low_Spec";
#else
    return "MH3_Low_Spec";
#endif
}

inline void Load() {
    cutsceneSkip.store(false);
    androidKeyboard.store(true);
    physicsLog.store(false);
    std::ifstream input(UserDir() / "features.cfg");
    std::string key;
    int value = 0;
    while (input >> key >> value) {
        if (value != 0 && value != 1) continue;
        if (key == "CUTSCENE_SKIP") cutsceneSkip.store(value != 0);
        if (key == "ANDROID_KEYBOARD") androidKeyboard.store(value != 0);
    }
}

inline bool Save() {
    std::ofstream output(UserDir() / "features.cfg", std::ios::trunc);
    output << "CUTSCENE_SKIP " << int(cutsceneSkip.load()) << '\n'
           << "ANDROID_KEYBOARD " << int(androidKeyboard.load()) << '\n';
    output.flush();
    return bool(output);
}

inline bool IsOfflineMode(uint8_t mode, uint8_t play) {
    return (mode == 0 && play == 0) || (mode == 2 && play == 5) ||
           (mode == 1 && (play == 3 || play == 5 || play == 6));
}

// A button held before a scene starts never skips it. A fresh press may wait
// for an outstanding movie read, then finishes only when retirement is safe.
class SkipPress {
    uint64_t scene_ = 0;
    bool initialized_ = false;
    bool held_ = false;
    bool pending_ = false;
public:
    void Reset() { initialized_ = false; pending_ = false; held_ = false; }
    bool Poll(uint64_t scene, bool pressed, bool allowed, bool ready = true) {
        if (!initialized_ || scene != scene_) {
            scene_ = scene;
            initialized_ = true;
            held_ = pressed;
            pending_ = false;
            return false;
        }
        const bool edge = pressed && !held_;
        held_ = pressed;
        if (!allowed) { pending_ = false; return false; }
        pending_ = pending_ || edge;
        if (!pending_ || !ready) return false;
        pending_ = false;
        return true;
    }
};
} // namespace RowellFeatures
