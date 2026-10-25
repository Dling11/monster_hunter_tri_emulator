#include "game_speed.h"
#include "game_step.h"
#include "memory.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <windows.h>

namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr, "Game speed check failed: %s\n", message); std::exit(1); }
}
bool close(double a, double b) { return std::abs(a - b) < 1e-7; }
}

// Run the production GameStep clock and memory writes in private zero-filled
// RAM, replacing only unused effect snapshot hooks. No image/save is touched.
namespace EffectStep {
std::any Save() { return {}; }
void Restore(const std::any&) {}
}

int main() {
    using namespace MH3GameSpeed;
    check(!IsOnlinePlay(2,5) && !IsOnlinePlay(1,0) && IsOnlinePlay(2,4) && IsOnlinePlay(1,4),
          "FPS lock distinguishes Village/solo quests from City/online quests");
    void* memory = VirtualAlloc(GuestFlat::Host(0x80000000), Memory::kMem1Size,
                               MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    check(memory == GuestFlat::Host(0x80000000), "allocate private guest RAM");
    check(!Available() && Percent() == 100 && !Select(150), "boot defaults to 1x");
    UpdateMode(true, 2, 5, true);
    check(Available() && Select(125), "Village allows speed-up");
    check(!Select(200) && Percent() == 125, "unsupported speed is rejected");

    GameStep::SetVariable(true);
    for (int percent : {100, 125, 150}) for (int fps : {30, 60, 120}) {
        UpdateMode(true, 1, 0, true);
        check(Select(percent), "solo quest allows selected rate");
        const double before = GameStep::Elapsed();
        int shared = 0;
        for (int frame = 0; frame < fps; ++frame) {
            GameStep::Begin(ScaleStep(60.0 / fps, 6.0) / 2.0);
            shared += GameStep::Share(2);
        }
        check(close(GameStep::Elapsed() - before, 30.0 * percent / 100.0),
              "one wall second advances the selected amount at 30/60/120 FPS");
        check(std::abs(shared - 60.0 * percent / 100.0) <= 1,
              "integer game timers advance with simulation time");
        check(Memory::ReadFloat32(0x8065B4D0) == 30.0f && Memory::ReadFloat32(0x80793628) > 0,
              "animation rate scales while the game's tick rate remains 30");
    }
    check(close(ScaleStep(8, 6), 6), "accelerated slow frame is bounded");
    Select(100);
    check(close(ScaleStep(8, 6), 8), "1x retains the original step unchanged");

    for (const auto mode : {std::pair{2,4}, {1,4}, {3,2}, {1,2}, {0,0}, {255,255}}) {
        UpdateMode(true, 2, 5, true); Select(150);
        UpdateMode(true, uint8_t(mode.first), uint8_t(mode.second), true);
        check(!Available() && Percent() == 100 && !Select(125) && close(ScaleStep(1,6),1),
              "City, online quest, Arena, title and unknown states force 1x");
    }
    UpdateMode(true, 2, 5, true);
    check(Percent() == 100, "returning to Village does not automatically resume speed-up");
    Select(150); UpdateMode(false, 2, 5, true);
    check(!Available() && Percent() == 100, "unavailable guest memory forces 1x");
    UpdateMode(true, 2, 5, false);
    check(!Available(), "fixed-step diagnostic mode cannot enable speed-up");
    UpdateMode(true, 2, 5, true); Select(150); Reset();
    check(Percent() == 100, "background/reset restores 1x");
    check(VirtualFree(memory, 0, MEM_RELEASE) != 0, "release private guest RAM");
    std::puts("Game speed: clock ratios, timers, step bounds and offline-only transitions passed.");
}
