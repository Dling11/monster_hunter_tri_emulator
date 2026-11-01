#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

// Physical gamepads only. Touch and keyboard bindings never enter this store.
namespace RowellPad {
inline constexpr int ActionCount = 20;
inline constexpr int Unassigned = -1;
inline constexpr int LeftTrigger = SDL_GAMEPAD_BUTTON_COUNT;
inline constexpr int RightTrigger = LeftTrigger + 1;
inline constexpr int SourceCount = RightTrigger + 1;
static_assert(SourceCount < 64);
struct Action { const char* id; const char* label; uint16_t mask; };
const Action& Definition(int action);
std::string SourceName(int source);
struct Sample { uint16_t buttons = 0; uint8_t combos = 0; };
class Store {
public:
    using Bindings = std::array<int, ActionCount>;
    explicit Store(std::filesystem::path file);
    static Bindings Defaults(int layout, bool swap);
    Bindings Get(int layout, bool swap) const;
    bool HasCustom() const;
    int Owner(int source, int layout, bool swap, int except = -1) const;
    std::string Assign(int action, int source, int layout, bool swap, bool exchange = false);
    std::string Reset();
    Sample Read(uint64_t held, int layout, bool swap) const;
private:
    std::string Commit(std::optional<Bindings> proposed);
    std::filesystem::path file_;
    std::optional<Bindings> custom_;
    mutable std::mutex mutex_;
};
Store& Current();
struct Snapshot {
    bool connected = false;
    SDL_JoystickID id = 0;
    std::string name;
    uint64_t held = 0;
    std::array<float, 4> sticks{};
};
Snapshot Poll();
// Prevent a button used in settings from also activating the game on Resume.
void SuppressHeld();
Snapshot ReadForGame();
class Capture {
public:
    void Begin(const Snapshot& state);
    // Ignores buttons already held at Begin, including a stuck broken button.
    int Update(const Snapshot& state);
    bool Disconnected() const { return disconnected_; }
private:
    SDL_JoystickID device_ = 0;
    uint64_t previous_ = 0, blocked_ = 0;
    bool disconnected_ = false;
};
}
