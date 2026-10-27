#include "controller_host.h"
#include "gamepad_bindings.h"
#include "touch_controls.h"
#include "pc_keybindings.h"
extern "C" int Gamepad_GetLayout();
extern "C" bool Gamepad_GetSwapTriggers();
#include "mh3_mouse_look_sensitivity.h"
#include "fatal.h"
#include "dev_options.h"
#include "../native/mh3_classic_map.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <cstdio>
#include <string>
#include <vector>
#if defined(MH3_HAS_AURORA) && !defined(__vita__)
#include <SDL3/SDL.h>
#endif
namespace {
std::optional<ClassicControllerSample> testSample;
uint64_t inputFrame = 0;
bool playbackHeld = false;
std::atomic<int32_t> pendingMouseLookX{0};
std::atomic<int32_t> pendingMouseLookY{0};

void QueueMouseDelta(std::atomic<int32_t>& target, int32_t delta) {
    if (delta == 0)
        return;

    int32_t current = target.load(std::memory_order_relaxed);

    for (;;) {
        const int32_t next = std::clamp<int32_t>(current + delta, -512, 512);

        if (target.compare_exchange_weak(current, next, std::memory_order_relaxed, std::memory_order_relaxed))
            return;
    }
}

// MH3_INPUT=[file=PATH][,record=PATH][,playback=PATH][,camera=N]: scripted controller
// input. file: a one-sample control file, read every frame, replaces the host
// controller. record and playback: frame-indexed input logs for repeatable
// runs; each line is "frame buttons(hex) lx ly rx ry", the sample in effect
// from that paced frame on. record writes one line per change of the host
// input; playback replaces the host input with a log. camera=N[:D]
// (development builds): the field camera looks at monster N, from D units
// away (field_camera.cpp).
const DevOptions& InputOptions() {
    static const DevOptions options("MH3_INPUT", {"file", "record", "playback", "camera"});
    return options;
}

bool SameInput(const ClassicControllerSample& a, const ClassicControllerSample& b) {
    return a.buttons == b.buttons && a.leftX == b.leftX && a.leftY == b.leftY && a.rightX == b.rightX && a.rightY == b.rightY;
}

void RecordInput(const ClassicControllerSample& sample) {
    static const char* path = InputOptions().Value("record");

    if (!path)
        return;

    static std::ofstream log(path);
    static std::optional<ClassicControllerSample> last;

    if (last && SameInput(*last, sample))
        return;

    last = sample;
    log << inputFrame << ' ' << std::hex << sample.buttons << std::dec << ' ' << sample.leftX << ' ' << sample.leftY << ' ' << sample.rightX << ' ' << sample.rightY << std::endl;
}

std::optional<ClassicControllerSample> PlaybackInput() {
    static const char* path = InputOptions().Value("playback");

    if (!path)
        return std::nullopt;

    struct Entry {
        uint64_t frame;
        ClassicControllerSample sample;
    };

    static const std::vector<Entry> entries = [] {
        std::vector<Entry> result;
        std::ifstream input(path);
        uint64_t frame;
        unsigned buttons;
        int lx, ly, rx, ry;

        while (input >> frame >> std::hex >> buttons >> std::dec >> lx >> ly >> rx >> ry) {
            if (!result.empty() && frame < result.back().frame)
                MH3_THROW(std::runtime_error("MH3_INPUT playback frames must not decrease"));

            result.push_back({frame, {true, uint16_t(buttons), int16_t(lx), int16_t(ly), int16_t(rx), int16_t(ry)}});
        }

        if (!input.eof() || result.empty())
            MH3_THROW(std::runtime_error("Invalid MH3_INPUT playback log"));

        return result;
    }();
    static size_t next = 0;

    // While held (the village shortcut still navigating), the log does not
    // reach its next entry; it resumes from there, later by the time held.
    static uint64_t delay = 0;
    uint64_t frame = inputFrame - delay;

    if (playbackHeld && next < entries.size() && entries[next].frame != 0 && entries[next].frame <= frame) {
        delay += frame - (entries[next].frame - 1);
        frame = entries[next].frame - 1;
    }

    static uint64_t reportedDelay = 0;

    if (!playbackHeld && delay != reportedDelay) {
        std::fprintf(stderr, "MH3 input playback: held %llu frames for the village shortcut\n", (unsigned long long)(delay - reportedDelay));
        reportedDelay = delay;
    }
    while (next < entries.size() && entries[next].frame <= frame)
        ++next;

    if (next == entries.size()) {
        static bool reported = false;

        if (!reported)
            std::fprintf(stderr, "MH3 input playback: finished at frame %llu\n", (unsigned long long)frame);

        reported = true;
    }

    if (next == 0)
        return ClassicControllerSample{true};

    return entries[next - 1].sample;
}
} // namespace

int32_t TakeMouseLookXDelta() {
    return pendingMouseLookX.exchange(0, std::memory_order_relaxed);
}

int32_t TakeMouseLookYDelta() {
    return pendingMouseLookY.exchange(0, std::memory_order_relaxed);
}

#ifdef MH3_DEVELOPMENT
int DebugCameraMonster(float* distance) {
    static float eyeDistance = 0.0f;
    static const int monster = [] {
        const char* value = InputOptions().Value("camera");

        if (!value)
            return -1;

        if (const char* colon = std::strchr(value, ':'))
            eyeDistance = float(std::atof(colon + 1));

        return std::atoi(value);
    }();

    if (distance)
        *distance = eyeDistance;

    return monster;
}
#endif

void QueueTouchCameraDelta(float dx, float dy) {
    const double sensitivity = mh3_mouse_look_sensitivity::GetSensitivity();
    QueueMouseDelta(pendingMouseLookX, int32_t(std::clamp(-double(dx)*sensitivity*1.5,-512.0,512.0)));
    QueueMouseDelta(pendingMouseLookY, int32_t(std::clamp(double(dy)*sensitivity*1.5,-512.0,512.0)));
}

void SetControllerTestSample(std::optional<ClassicControllerSample> sample) {
    testSample = sample;
}

void SetInputFrame(uint64_t frame) {
    inputFrame = frame;
}

void HoldInputPlayback(bool hold) {
    playbackHeld = hold;
}

ClassicControllerSample ReadHostController() {
    if (testSample)
        return *testSample;

    if (auto played = PlaybackInput()) {
        RecordInput(*played);
        return *played;
    }

    const ClassicControllerSample sample = ReadLiveController();
    RecordInput(sample);
    return sample;
}

ClassicControllerSample ReadLiveController() {
    ClassicControllerSample sample;

    if (const char* path = InputOptions().Value("file")) {
        // Explicit diagnostic controller; the normal source is SDL. This lets
        // unattended runs exercise the same guest input path while macOS is locked.
        std::ifstream input(path);
        unsigned buttons;
        int lx, ly, rx, ry;

        if (!(input >> std::hex >> buttons >> std::dec >> lx >> ly >> rx >> ry) || buttons > 0xFFFF || lx < -512 || lx > 511 || ly < -512 || ly > 511 || rx < -512 || rx > 511 || ry < -512 || ry > 511)
            MH3_THROW(std::runtime_error("Invalid MH3_INPUT file: expected hex buttons and four signed axes (-512..511)"));

        sample = {true, uint16_t(buttons), int16_t(lx), int16_t(ly), int16_t(rx), int16_t(ry)};
        return sample;
    }
#if defined(__vita__)
    (void)sample;
    return ReadVitaController();
#elif defined(MH3_HAS_AURORA)
    // The keyboard and the first SDL gamepad, as the Classic Controller on
    // channel 0 (docs/optimizations/system/controller-input.md, docs/vita-controls.md).
    sample.connected = true;

    if (!SDL_WasInit(SDL_INIT_VIDEO))
        return sample;

    if (!SDL_WasInit(SDL_INIT_GAMEPAD) && !SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        MH3_THROW(std::runtime_error(std::string("SDL controller initialization: ") + SDL_GetError()));

    if (TouchControls_WantsPointer()) {
        if (auto* window = SDL_GetKeyboardFocus()) {
            if (SDL_GetWindowRelativeMouseMode(window)) SDL_SetWindowRelativeMouseMode(window, false);
            if (!SDL_CursorVisible()) SDL_ShowCursor();
        }
    }
    if (TouchControls_IsMenuOpen()) return sample;
    SDL_Window* focusedWindow = SDL_GetKeyboardFocus();
    const bool touchPointer = TouchControls_WantsPointer();
    float mouseDeltaX = 0.0f, mouseDeltaY = 0.0f;
    SDL_MouseButtonFlags mouseButtons = 0;
    static SDL_Window* relativeModeWindow = nullptr;
    static bool relativeModeUnavailable = false;
    static bool haveFallbackMousePosition = false;
    static float fallbackMouseX = 0.0f, fallbackMouseY = 0.0f;

    if (focusedWindow) {
        if (relativeModeWindow != focusedWindow) {
            relativeModeWindow = focusedWindow;
            relativeModeUnavailable = false;
            haveFallbackMousePosition = false;
        }

        if (!touchPointer && !relativeModeUnavailable && !SDL_GetWindowRelativeMouseMode(focusedWindow) &&
            !SDL_SetWindowRelativeMouseMode(focusedWindow, true)) {
            relativeModeUnavailable = true;
            std::fprintf(stderr, "MH3: SDL relative mouse mode failed; using cursor deltas: %s\n", SDL_GetError());
        }

        if (!relativeModeUnavailable && SDL_GetWindowRelativeMouseMode(focusedWindow)) {
            mouseButtons = SDL_GetRelativeMouseState(&mouseDeltaX, &mouseDeltaY);
            haveFallbackMousePosition = false;
        } else {
            // Fallback for hosts where SDL cannot enable relative mode. It works inside the window;
            // relative mode remains preferred because it has no edge limit.
            float x = 0.0f, y = 0.0f;
            mouseButtons = SDL_GetMouseState(&x, &y);
            if (haveFallbackMousePosition) {
                mouseDeltaX = x - fallbackMouseX;
                mouseDeltaY = y - fallbackMouseY;
            }
            fallbackMouseX = x;
            fallbackMouseY = y;
            haveFallbackMousePosition = true;
        }
    } else {
        // Drain stale movement while the game window is not focused.
        (void)SDL_GetRelativeMouseState(nullptr, nullptr);
        haveFallbackMousePosition = false;
        relativeModeWindow = nullptr;
        relativeModeUnavailable = false;
    }

    int keyCount=0;
    const bool* keys = SDL_GetKeyboardState(&keyCount);

    if (focusedWindow) {
        const auto keyboard=RowellKeys::Current().Read(keys,keyCount);
        sample.buttons |= keyboard.buttons;
        for(unsigned i=0;i<5;++i)if(keyboard.combos&(1u<<i))sample.buttons|=TouchControls_GetComboMask(i);
        if (!touchPointer && mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_RIGHT))
            sample.buttons |= MH3_CL_A;
        if (!touchPointer && mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_LEFT))
            sample.buttons |= MH3_CL_X;
        if (!touchPointer && mouseButtons & SDL_BUTTON_MASK(SDL_BUTTON_MIDDLE))
            sample.buttons |= MH3_CL_R;

        sample.leftX=keyboard.leftX;sample.leftY=keyboard.leftY;
        sample.rightX=keyboard.rightX;sample.rightY=keyboard.rightY;
        // The mouse controls yaw/pitch through relative deltas, not a held virtual stick.
    }

    if (touchPointer) { mouseDeltaX=mouseDeltaY=0; mouseButtons=0; }
// MH3_MOUSE_LOOK_TUNE: sensibilidad configurable desde Opciones > Controles.
    static double mh3MouseLookRemainderX = 0.0;
    static double mh3MouseLookRemainderY = 0.0;
const double mh3MouseLookSensitivity = mh3_mouse_look_sensitivity::GetSensitivity();
    if (focusedWindow) {
        // SDL suma X hacia la derecha e Y hacia abajo; invertimos ambos para la camara.
        mh3MouseLookRemainderX -= std::clamp(double(mouseDeltaX), -512.0, 512.0) * mh3MouseLookSensitivity;
        mh3MouseLookRemainderY += std::clamp(double(mouseDeltaY), -512.0, 512.0) * mh3MouseLookSensitivity;
        const int32_t adjustedMouseX = int32_t(std::trunc(mh3MouseLookRemainderX));
        const int32_t adjustedMouseY = int32_t(std::trunc(mh3MouseLookRemainderY));
        mh3MouseLookRemainderX -= double(adjustedMouseX);
        mh3MouseLookRemainderY -= double(adjustedMouseY);
        QueueMouseDelta(pendingMouseLookX, std::clamp<int32_t>(adjustedMouseX, -512, 512));
        QueueMouseDelta(pendingMouseLookY, std::clamp<int32_t>(adjustedMouseY, -512, 512));
    } else {
        pendingMouseLookX.store(0, std::memory_order_relaxed);
        pendingMouseLookY.store(0, std::memory_order_relaxed);
        mh3MouseLookRemainderX = 0.0;
        mh3MouseLookRemainderY = 0.0;
    }

    const auto physical = RowellPad::ReadForGame();
    if (physical.connected) {
        const auto mapped = RowellPad::Current().Read(physical.held, Gamepad_GetLayout(), Gamepad_GetSwapTriggers());
        sample.buttons |= mapped.buttons;
        for (unsigned i = 0; i < 5; ++i)
            if (mapped.combos & (1u << i)) sample.buttons |= TouchControls_GetComboMask(i);
        // Keep the existing analog stick curve and touch/keyboard precedence.
        if (!sample.leftX && !sample.leftY)
            mh3_classic_stick(physical.sticks[0], -physical.sticks[1], &mh3_gamepad_stick_curve, &sample.leftX, &sample.leftY);
        if (!sample.rightX && !sample.rightY)
            mh3_classic_stick(physical.sticks[2], -physical.sticks[3], &mh3_gamepad_stick_curve, &sample.rightX, &sample.rightY);
    }

    TouchControls_UpdateSample(&sample.buttons,&sample.leftX,&sample.leftY,
        &sample.rightX,&sample.rightY,&sample.triggerL,&sample.triggerR);

    // Digital L/R inputs (keyboard, mouse or shoulder) report a full press.
    if (sample.buttons & MH3_CL_L)
        sample.triggerL = 255;

    if (sample.buttons & MH3_CL_R)
        sample.triggerR = 255;
#endif
    return sample;
}
