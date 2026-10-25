#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

// Shared by the game thread and touch UI. Only the frame pacer publishes the
// game's mode; the renderer never grants permission to accelerate simulation.
namespace MH3GameSpeed {
inline bool IsOnlinePlay(uint8_t gameMode, uint8_t playMode) {
    return (gameMode == 1 || gameMode == 2) && playMode == 4;
}
inline std::atomic<bool> singlePlayer{false};
inline std::atomic<int> selectedPercent{100};

inline bool Available() { return singlePlayer.load(); }
inline int Percent() { return selectedPercent.load(); }
inline void Reset() { selectedPercent.store(100); }

inline bool Select(int percent) {
    if (percent != 100 && percent != 125 && percent != 150) return false;
    if (percent != 100 && !Available()) return false;
    selectedPercent.store(percent);
    return true;
}

inline void Cycle() {
    const int current = Percent();
    Select(current == 100 ? 125 : current == 125 ? 150 : 100);
}

inline void UpdateMode(bool memoryReady, uint8_t gameMode, uint8_t playMode,
                       bool variableStep) {
    // RMHP08 system_w+0x24/+0x25, GetOnlineMode / PlayMode_ck:
    // game mode 1 = field, 2 = village/city; play mode 0 = solo quest,
    // 5 = village/free roam, 4 = online, 2 = Arena. Permit only known solo
    // combinations. Title, connection screens, Arena and unknown states stay
    // at normal speed, including a solo hunter inside the online City.
    const bool allowed = memoryReady && variableStep &&
        (gameMode == 1 || gameMode == 2) && (playMode == 0 || playMode == 5);
    singlePlayer.store(allowed);
    if (!allowed) Reset();
}

inline double ScaleStep(double retraces, double maxRetraces) {
    const int percent = Percent();
    if (!Available() || percent == 100) return retraces;
    // Retain the existing 1x path exactly. Faster steps have the same 100 ms
    // upper bound as the pacer, so a slow frame cannot cause a larger jump.
    return std::min(retraces * double(percent) / 100.0, maxRetraces);
}
} // namespace MH3GameSpeed
