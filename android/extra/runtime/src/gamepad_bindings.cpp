#include "gamepad_bindings.h"
#include "../native/mh3_classic_map.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace RowellPad {
namespace {
constexpr Action actions[ActionCount] = {
    {"A","A",MH3_CL_A}, {"B","B",MH3_CL_B}, {"X","X",MH3_CL_X}, {"Y","Y",MH3_CL_Y},
    {"L","L",MH3_CL_L}, {"R","R",MH3_CL_R}, {"ZL","ZL",MH3_CL_ZL}, {"ZR","ZR",MH3_CL_ZR},
    {"PLUS","+ (Plus)",MH3_CL_PLUS}, {"MINUS","- (Minus)",MH3_CL_MINUS}, {"HOME","Wii Home",MH3_CL_HOME},
    {"UP","D-pad up",MH3_CL_UP}, {"DOWN","D-pad down",MH3_CL_DOWN},
    {"LEFT","D-pad left",MH3_CL_LEFT}, {"RIGHT","D-pad right",MH3_CL_RIGHT},
    {"COMBO_1","Combo 1",0}, {"COMBO_2","Combo 2",0}, {"COMBO_3","Combo 3",0},
    {"COMBO_4","Combo 4",0}, {"COMBO_5","Combo 5",0}
};
bool ValidSource(int source) { return source >= Unassigned && source < SourceCount; }
bool Valid(const Store::Bindings& bindings) {
    uint64_t used = 0;
    for (int source : bindings) {
        if (!ValidSource(source)) return false;
        if (source == Unassigned) continue;
        const auto bit = uint64_t(1) << source;
        if (used & bit) return false;
        used |= bit;
    }
    return true;
}
std::mutex deviceMutex;
SDL_Gamepad* device = nullptr;
SDL_JoystickID suppressedDevice = 0;
uint64_t suppressed = 0, nextProbe = 0;
Snapshot PollUnlocked(bool includeName = true) {
    Snapshot result;
    if (!SDL_WasInit(SDL_INIT_GAMEPAD)) return result;
    if (device && !SDL_GamepadConnected(device)) {
        SDL_CloseGamepad(device); device = nullptr; suppressed = 0; nextProbe = 0;
    }
    if (!device && SDL_GetTicks() >= nextProbe) {
        nextProbe = SDL_GetTicks() + 500;
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        for (int i = 0; i < count && !device; ++i) device = SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    if (!device) return result;
    result.connected = true;
    result.id = SDL_GetGamepadID(device);
    if (includeName) {
        const char* name = SDL_GetGamepadName(device);
        result.name = name ? name : "Controller";
    }
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i)
        if (SDL_GetGamepadButton(device, SDL_GamepadButton(i))) result.held |= uint64_t(1) << i;
    if (SDL_GetGamepadAxis(device, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16383) result.held |= uint64_t(1) << LeftTrigger;
    if (SDL_GetGamepadAxis(device, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16383) result.held |= uint64_t(1) << RightTrigger;
    for (int i = 0; i < 4; ++i)
        result.sticks[i] = std::clamp(SDL_GetGamepadAxis(device, SDL_GamepadAxis(i)) / 32767.0f, -1.0f, 1.0f);
    return result;
}
}
const Action& Definition(int action) { return actions[std::clamp(action, 0, ActionCount - 1)]; }
std::string SourceName(int source) {
    switch (source) {
    case Unassigned: return "Unassigned";
    case SDL_GAMEPAD_BUTTON_SOUTH: return "Bottom (Cross / A)";
    case SDL_GAMEPAD_BUTTON_EAST: return "Right (Circle / B)";
    case SDL_GAMEPAD_BUTTON_WEST: return "Left (Square / X)";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Top (Triangle / Y)";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return "L1 / LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return "R1 / RB";
    case LeftTrigger: return "L2 / LT";
    case RightTrigger: return "R2 / RT";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return "L3 (left stick click)";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return "R3 (right stick click)";
    case SDL_GAMEPAD_BUTTON_BACK: return "Share / Back";
    case SDL_GAMEPAD_BUTTON_START: return "Options / Start";
    case SDL_GAMEPAD_BUTTON_GUIDE: return "PS / Guide";
    default:
        if (ValidSource(source)) {
            const char* name = SDL_GetGamepadStringForButton(SDL_GamepadButton(source));
            if (name) return name;
        }
        return "Unknown input";
    }
}
Store::Bindings Store::Defaults(int layout, bool swap) {
    return {layout == 1 ? SDL_GAMEPAD_BUTTON_SOUTH : SDL_GAMEPAD_BUTTON_EAST,
            layout == 1 ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH,
            layout == 1 ? SDL_GAMEPAD_BUTTON_WEST : SDL_GAMEPAD_BUTTON_NORTH,
            layout == 1 ? SDL_GAMEPAD_BUTTON_NORTH : SDL_GAMEPAD_BUTTON_WEST,
            swap ? LeftTrigger : SDL_GAMEPAD_BUTTON_LEFT_SHOULDER,
            swap ? RightTrigger : SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
            swap ? SDL_GAMEPAD_BUTTON_LEFT_SHOULDER : LeftTrigger,
            swap ? SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER : RightTrigger,
            SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE,
            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN,
            SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            Unassigned, Unassigned, Unassigned, Unassigned, Unassigned};
}
Store::Store(std::filesystem::path file) : file_(std::move(file)) {
    std::ifstream input(file_);
    std::string header;
    if (!std::getline(input, header) || header != "MH_TRI_ROWELL_PAD 1") return;
    int enabled = -1;
    if (!(input >> enabled) || enabled != 1) return;
    Bindings proposed{};
    for (int i = 0; i < ActionCount; ++i) {
        std::string id;
        if (!(input >> id >> proposed[i]) || id != actions[i].id) return;
    }
    std::string extra;
    if (!(input >> extra) && Valid(proposed)) custom_ = proposed;
}
Store::Bindings Store::Get(int layout, bool swap) const {
    std::lock_guard lock(mutex_);
    return custom_.value_or(Defaults(layout, swap));
}
bool Store::HasCustom() const { std::lock_guard lock(mutex_); return custom_.has_value(); }
int Store::Owner(int source, int layout, bool swap, int except) const {
    if (source == Unassigned) return -1;
    const auto bindings = Get(layout, swap);
    for (int i = 0; i < ActionCount; ++i) if (i != except && bindings[i] == source) return i;
    return -1;
}
std::string Store::Commit(std::optional<Bindings> proposed) {
    if (proposed && !Valid(*proposed)) return "Invalid or duplicate controller binding.";
    auto temporary = file_; temporary += ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    output << "MH_TRI_ROWELL_PAD 1\n" << (proposed ? 1 : 0) << '\n';
    if (proposed) for (int i = 0; i < ActionCount; ++i) output << actions[i].id << ' ' << (*proposed)[i] << '\n';
    output.close();
    if (!output) return "Could not save controller bindings. Previous bindings remain active.";
#ifdef _WIN32
    const bool replaced = MoveFileExW(temporary.c_str(), file_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
    const bool replaced = std::rename(temporary.c_str(), file_.c_str()) == 0;
#endif
    if (!replaced) return "Could not replace controller bindings. Previous bindings remain active.";
    custom_ = std::move(proposed);
    return {};
}
std::string Store::Assign(int action, int source, int layout, bool swap, bool exchange) {
    std::lock_guard lock(mutex_);
    if (action < 0 || action >= ActionCount || !ValidSource(source)) return "Invalid controller input.";
    auto proposed = custom_.value_or(Defaults(layout, swap));
    if (source != Unassigned) for (int i = 0; i < ActionCount; ++i) {
        if (i != action && proposed[i] == source) {
            if (!exchange) return std::string("Input already used by ") + actions[i].label + ". Swap or cancel.";
            proposed[i] = proposed[action];
            break;
        }
    }
    proposed[action] = source;
    return Commit(proposed);
}
std::string Store::Reset() { std::lock_guard lock(mutex_); return Commit(std::nullopt); }
Sample Store::Read(uint64_t held, int layout, bool swap) const {
    Sample result;
    const auto bindings = Get(layout, swap);
    for (int i = 0; i < ActionCount; ++i) if (bindings[i] >= 0 && (held & (uint64_t(1) << bindings[i]))) {
        result.buttons |= actions[i].mask;
        if (i >= 15) result.combos |= uint8_t(1u << (i - 15));
    }
    return result;
}
Store& Current() {
    static Store store([] {
        const char* user = std::getenv("MH3_USER_DIR");
#ifdef __ANDROID__
        const char* fallback = "/storage/emulated/0/MH3_Rowell_Dev";
#else
        const char* fallback = "MH3_Rowell_Dev";
#endif
        return (user && *user ? std::filesystem::path(user) : std::filesystem::path(fallback)) / "controller_bindings.cfg";
    }());
    return store;
}
Snapshot Poll() { std::lock_guard lock(deviceMutex); return PollUnlocked(); }
void SuppressHeld() {
    std::lock_guard lock(deviceMutex);
    const auto state = PollUnlocked(false); suppressedDevice = state.id; suppressed = state.held;
}
Snapshot ReadForGame() {
    std::lock_guard lock(deviceMutex);
    auto state = PollUnlocked(false);
    if (state.id != suppressedDevice) suppressed = 0;
    suppressed &= state.held;
    state.held &= ~suppressed;
    return state;
}
void Capture::Begin(const Snapshot& state) {
    device_ = state.id; previous_ = blocked_ = state.held; disconnected_ = !state.connected;
}
int Capture::Update(const Snapshot& state) {
    if (!state.connected || state.id != device_) { disconnected_ = true; return Unassigned; }
    blocked_ &= state.held;
    const auto pressed = state.held & ~previous_ & ~blocked_;
    previous_ = state.held;
    for (int i = 0; i < SourceCount; ++i) if (pressed & (uint64_t(1) << i)) return i;
    return Unassigned;
}
}
