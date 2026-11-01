#include "lite_profile.h"
#include "touch_controls.h"
#include "touch_contacts.h"
#include "game_speed.h"
#include "rowell_features.h"
#ifdef __ANDROID__
#include "android_keyboard.h"
#endif
#include "gamepad_bindings.h"
#include <optional>
#include <array>
#include <mutex>
#include <limits>
#include "rowell_ui_atlas.h"
#include "language.h"
#include "save_manager.h"
#include "mh3_frame_rate_control.h"
#include "widescreen.h"
#include "memory.h"
#ifdef MH3_PC_TOUCH_UI
#include "pc_keybindings.h"
#include "rowell_touch.h"
#ifdef MH3_TOUCH_INPUT_TESTS
void TouchDisplayFixtureBegin();
bool TouchDisplayFixtureMatches(int aspect,bool fullscreen);
bool TouchDisplayFixtureScaleMatches(float scale);
void TouchDisplayFixtureEnd();
#endif
#endif
#ifndef MH3_PC_TOUCH_UI
#include "net/local_server.h"
#include "net/lan_beacon.h"
#endif

#include <SDL3/SDL.h>
#include <GLES3/gl3.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#if defined(__ANDROID__)
#include <android/log.h>
#include <jni.h>
#define LOG_TAG "MH3_TOUCH"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#define LOGI(...) std::printf(__VA_ARGS__)
#define LOGE(...) std::fprintf(stderr, __VA_ARGS__)
#endif

// External engine hooks
extern void QueueTouchCameraDelta(float dx, float dy);
extern "C" void VISetFrameBufferScale(float scale);
extern void aurora_enable_vsync(bool enabled);
extern "C" void aurora_set_native_target_aspect(int aspect);
extern "C" void aurora_set_fullscreen(bool fullscreen);
extern "C" bool aurora_get_fullscreen(void);
extern "C" struct SDL_Window* aurora_get_window(void);

extern "C" {
int Gamepad_GetLayout();
void Gamepad_SetLayout(int layout);
bool Gamepad_GetSwapTriggers();
void Gamepad_SetSwapTriggers(bool swap);
}

#ifndef MH3_PC_TOUCH_UI
namespace aurora::gfx::texture_replacement {
size_t get_replacement_count() noexcept;
void set_enabled(bool enabled) noexcept;
bool is_enabled() noexcept;
void reload() noexcept;
}
#endif

#ifdef MH3_TOUCH_INPUT_TESTS
namespace mh3_save_manager {
unsigned BackupCallsForTesting();
unsigned ExportCallsForTesting();
unsigned ImportCallsForTesting();
unsigned StageCallsForTesting();
std::filesystem::path StagedImportForTesting();
bool RestartForTesting();
}
#endif

namespace {

#if defined(__ANDROID__)
bool RequestAndroidGameRestart() {
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) return false;
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID restart = env->GetMethodID(activityClass, "restartGame", "()V");
    if (restart) env->CallVoidMethod(activity, restart);
    const bool failed = env->ExceptionCheck();
    if (failed) { env->ExceptionDescribe(); env->ExceptionClear(); }
    env->DeleteLocalRef(activityClass);
    env->DeleteLocalRef(activity);
    return restart && !failed;
}
#endif

bool RequestGameRestart() {
#ifdef MH3_TOUCH_INPUT_TESTS
    return mh3_save_manager::RestartForTesting();
#elif defined(__ANDROID__)
    return RequestAndroidGameRestart();
#elif defined(MH3_PC_TOUCH_UI)
    return RowellTouch_RestartGame();
#else
    return false;
#endif
}

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Color {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

// Faded blue settings palette; gameplay button shapes keep their neutral finish.
constexpr Color COL_WHITE           { 0.929412f, 0.952941f, 0.980392f, 1.00f };
constexpr Color COL_MUTED           { 0.662745f, 0.733333f, 0.815686f, 1.00f };
constexpr Color COL_CYAN            { 0.513725f, 0.674510f, 0.847059f, 1.00f };
constexpr Color COL_BG_DARK         { 0.043137f, 0.078431f, 0.133333f, 0.98f };
constexpr Color COL_PANEL_CARD      { 0.090196f, 0.152941f, 0.231373f, 0.98f };
constexpr Color COL_PANEL_BORDER    { 0.188235f, 0.282353f, 0.388235f, 1.00f };
constexpr Color COL_ACTIVE_CHIP     { 0.141176f, 0.250980f, 0.372549f, 1.00f };
constexpr Color COL_ACTIVE_TEXT     { 0.929412f, 0.952941f, 0.980392f, 1.00f };
constexpr Color COL_INACTIVE_CHIP   { 0.101961f, 0.172549f, 0.254902f, 1.00f };
constexpr Color COL_CHIP_BORDER     { 0.188235f, 0.282353f, 0.388235f, 1.00f };
constexpr Color COL_CAUTION         { 0.847059f, 0.745098f, 0.537255f, 1.00f };
constexpr Color COL_DANGER          { 0.839216f, 0.627451f, 0.670588f, 1.00f };
Color Neutral(Color color) {
    const float gray=.299f*color.r+.587f*color.g+.114f*color.b;
    return {gray,gray,gray,color.a};
}

struct ConfigurableButton {
    std::string id;
    std::string label;
    uint16_t mask;
    Vec2 defaultNormPos;
    float defaultNormRadius;
    Vec2 normPos;
    float scale = 1.0f;
    Vec2 pos;
    float radius = 0.0f;
    bool isPressed = false;
    mh3_touch::Contacts contacts;
    Color baseColor;
    Color borderColor;
};

struct UIVertex {
    float x, y;
    float u, v;          // Local offset in pixels [-halfW, halfW] for SDF, or UV for texture
    float r, g, b, a;
    float br, bg, bb, ba;
    float type;          // 0: AA disc, 1: AA ring, 2: AA rounded rect (pixel SDF), 3: Font text, 4: Plain
    float thickness;     // Ring/border thickness
    float feather;       // corner radius (for type 2) or feather
    float extra;         // half width (for type 2)
};

enum class SaveAction { None, Export, Import, Backup };

class TouchSettingsEngine {
    struct EditSnapshot {
        std::vector<ConfigurableButton> buttons;
        Vec2 stick;
        float stickScale;
        std::array<uint16_t,5> combos;
        std::array<bool,5> enabled;
    };
public:
    TouchSettingsEngine() {
        m_enabled = true;
#if defined(__ANDROID__)
        m_touchVisible = true;
#else
        m_touchVisible = true;
#endif
    }

    void Init() {
        if (m_initialized) return;
        m_initialized = true;

        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");

        LoadSettings();
        LoadLayoutConfig();
        RowellFeatures::Load();
    }

    void ResetInput() {
        for (auto& b : m_buttons) { b.contacts.Clear(); b.isPressed = false; }
        m_latchedMask.store(0);
        m_buttonGestures.Clear();
        m_speedGestures.Clear();
        m_leftFingerId.reset(); m_rightFingerId.reset(); m_editFingerId.reset();
        m_leftX = m_leftY = m_rightX = m_rightY = 0;
        m_rightStickActive = false; m_editDragging = false; m_editorPanelDragging=false;
        m_leftStickPos = m_leftStickCenter;
        m_cameraDeltaX.store(0); m_cameraDeltaY.store(0);
    }
    void CancelControllerCapture() {
        if(m_padCaptureAction>=0) {
            m_padCaptureAction=-1;m_padStatus="Binding canceled while the app was in the background.";
        }
    }
    void SetEnabled(bool enabled) { if (!enabled) { ResetInput(); MH3GameSpeed::Reset(); } m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    bool IsMenuOpen() const { return m_menuOpen || m_editMode; }
    bool IsModalOpen() const {
        return IsSaveConfirmationOpen() || m_saveBrowserOpen || m_restoreRestarting || m_padEditorOpen
#ifdef MH3_PC_TOUCH_UI
            || m_keyboardEditorOpen
#endif
            ;
    }
    uint16_t GetComboMask(unsigned slot) const { return slot<m_comboMasks.size()?m_comboMasks[slot]:0; }
    bool WantsPointer() const { return m_menuOpen || m_editMode || m_touchVisible; }
    void ToggleMenu() { SetMenuOpen(!m_menuOpen); }
    void SetMenuOpen(bool open) {
        m_padEditorOpen=false; m_padCaptureAction=-1; m_padPendingPreset=-1;
        if(!open)RowellPad::SuppressHeld();
        m_pendingSaveAction = SaveAction::None;
        m_pendingImportFolder.clear();
        m_saveBrowserOpen=false;
#ifdef MH3_PC_TOUCH_UI
        m_keyboardEditorOpen=false;m_captureAction=-1;
#endif
        if (m_editMode) { CancelLayoutEdit(); }
        ResetInput();
        m_menuOpen = open;
        if (m_menuOpen) {
            if (m_menuTab == 2) RefreshLastBackup();
            SDL_ShowCursor();
            if (SDL_Window* win = aurora_get_window()) {
                SDL_SetWindowRelativeMouseMode(win, false);
            }
        } else {
            if (SDL_Window* win = aurora_get_window()) {
#if !defined(__ANDROID__)
                if(WantsPointer()){SDL_SetWindowRelativeMouseMode(win,false);SDL_ShowCursor();}
                else SDL_HideCursor();
#endif
            }
        }
    }

    bool IsSaveConfirmationOpen() const { return m_pendingSaveAction != SaveAction::None; }
    void CancelSaveConfirmation() {
        const bool importing=m_pendingSaveAction==SaveAction::Import;
        switch (m_pendingSaveAction) {
        case SaveAction::Export: m_saveStatusMsg = "Export canceled."; break;
        case SaveAction::Import: m_saveStatusMsg = "Import canceled. Your save was not changed."; break;
        case SaveAction::Backup: m_saveStatusMsg = "Backup canceled."; break;
        default: break;
        }
        m_pendingSaveAction = SaveAction::None;
        m_pendingImportFolder.clear();
        if(importing)m_saveBrowserOpen=true;
    }
    void CancelModal() {
        if(m_restoreRestarting)return;
        if(m_padEditorOpen) {
            if(m_padPendingPreset>=0)m_padPendingPreset=-1;
            else if(m_padCaptureAction>=0){m_padCaptureAction=-1;m_padStatus="Binding canceled.";}
            else m_padEditorOpen=false;
            return;
        }
#ifdef MH3_PC_TOUCH_UI
        if(m_keyboardEditorOpen) {
            if(m_captureAction>=0){m_captureAction=-1;m_keyStatus="Binding canceled.";}
            else m_keyboardEditorOpen=false;
            return;
        }
#endif
        if(IsSaveConfirmationOpen())CancelSaveConfirmation();
        else m_saveBrowserOpen=false;
    }
#ifdef MH3_PC_TOUCH_UI
    bool HandleKeyboardEditorKey(SDL_Scancode key) {
        if(!m_keyboardEditorOpen)return false;
        if(key==SDL_SCANCODE_ESCAPE){CancelModal();return true;}
        if(m_captureAction>=0) {
            const auto error=RowellKeys::Current().Assign(m_captureAction,m_captureSlot,key);
            m_keyStatus=error.empty()?"Binding saved automatically.":error;
            if(error.empty())m_captureAction=-1;
        }
        return true;
    }
#endif

    void ApplyFullscreen(bool enabled) {
        m_fullscreen = enabled;
        aurora_set_fullscreen(enabled);
#ifdef MH3_PC_TOUCH_UI
        // Window mode and picture shape are independent. Render-target changes
        // are queued by ApplyAspectRatio rather than applied by the event thread.
        ApplyAspectRatio(m_aspectIndex);
#else
        if (enabled) {
            // Fix de resolución a pantalla completa: relación 16:9 estricta en Aurora y en la memoria del juego
            aurora_set_native_target_aspect(2); // AURORA_NATIVE_TARGET_ASPECT_16_9
            m_aspectIndex = 0; // 16:9 Widescreen
            SetWidescreenSetting(true);
#if defined(__cpp_exceptions)
            try {
#endif
                if (Memory::Contains(0x80797F68, 4) && Memory::Contains(0x8065B4C8, 4)) {
                    uint32_t aspectVal = Memory::Read32(0x80797F68);
                    if (aspectVal != 0) {
                        Memory::Write32(0x8065B4C8, aspectVal);
                        Memory::Write8(0x8065B4D5, 1);
                    }
                }
#if defined(__cpp_exceptions)
            } catch (...) {}
#endif
        } else {
            ApplyAspectRatio(m_aspectIndex);
        }
#endif
        SaveSettings();
    }

    void ToggleFullscreen() {
        ApplyFullscreen(!m_fullscreen);
    }

    bool RunInputChecks() {
        TouchSettingsEngine e;
        e.SetEnabled(true); e.UpdateLayout(1280,720);
        auto find=[&](const char* name)->ConfigurableButton& {
            for(auto& b:e.m_buttons) if(b.id==name) return b;
            std::abort();
        };
        auto press=[&](uint64_t id,const char* name) {
            auto& b=find(name); e.HandleTouchDown(id,b.pos.x/e.m_width,b.pos.y/e.m_height);
        };
        uint8_t lastTriggerR=0;
        auto sample=[&] {
            uint16_t buttons=0; int16_t lx=0,ly=0,rx=0,ry=0; uint8_t tl=0,tr=0;
            e.UpdateSample(&buttons,&lx,&ly,&rx,&ry,&tl,&tr); lastTriggerR=tr; return buttons;
        };
        auto check=[](bool ok,const char* what) { if(!ok) std::fprintf(stderr,"Touch check failed: %s\n",what); return ok; };
#if defined(MH3_TOUCH_INPUT_TESTS) && defined(MH3_PC_TOUCH_UI)
        {
            TouchSettingsEngine display;
            TouchDisplayFixtureBegin();
            display.m_aspectIndex=1;
            display.ApplyFullscreen(true);
            if(!check(display.m_aspectIndex==1 && TouchDisplayFixtureMatches(0,true),"fullscreen preserves classic aspect through PC graphics queue"))return false;
            display.m_aspectIndex=2;
            display.ApplyFullscreen(true);
            if(!check(TouchDisplayFixtureMatches(1,true),"fullscreen preserves stretch aspect"))return false;
            display.ToggleFullscreen();
            if(!check(TouchDisplayFixtureMatches(1,false),"windowed mode preserves stretch aspect"))return false;
            display.ApplyAspectRatio(0);
            if(!check(TouchDisplayFixtureMatches(2,false),"widescreen reaches the PC graphics queue"))return false;
            display.ApplyResolutionScale(1);
            if(!check(TouchDisplayFixtureScaleMatches(1.6071429f),"resolution reaches the PC graphics queue"))return false;
            TouchDisplayFixtureEnd();
            std::printf("PC display UI: fullscreen/aspect independence and queued aspect/resolution hooks passed.\n");
        }
#endif
        constexpr uint64_t one=0x100000001ULL,two=0x200000001ULL,three=0x8000000000000001ULL;
        press(one,"R");press(two,"X");press(three,"A");
        if(!check(sample()==(MH3_CL_R|MH3_CL_X|MH3_CL_A),"three independent 64-bit contacts"))return false;
        e.HandleTouchUp(two);
        if(!check(sample()==(MH3_CL_R|MH3_CL_A),"release middle contact only"))return false;
        e.HandleTouchUp(one);
        if(!check(sample()==MH3_CL_A,"third contact remains held"))return false;
        e.HandleTouchUp(three);
        if(!check(sample()==0,"release all"))return false;
        press(0,"M5");
        if(!check(sample()==(MH3_CL_R|MH3_CL_X|MH3_CL_A) && lastTriggerR==255,"R+X+A combo and R trigger"))return false;
        e.HandleTouchUp(0);if(!check(sample()==0,"combo release"))return false;
        press(one,"A");press(two,"A");sample();e.HandleTouchUp(one);
        if(!check(sample()==MH3_CL_A,"two contacts on one button"))return false;
        e.HandleTouchUp(two);if(!check(sample()==0,"last owner releases button"))return false;
        press(one,"M5");e.HandleTouchUp(one);
        if(!check(sample()==(MH3_CL_R|MH3_CL_X|MH3_CL_A) && sample()==0,"short tap lasts one sample"))return false;
        press(one,"R"); e.SetMenuOpen(true); e.SetMenuOpen(false);
        if(!check(sample()==0,"opening settings clears held and latched input"))return false;
        press(one,"A");sample(); e.HandleTouchMove(one,0.5f,0.5f);
        if(!check(sample()==0,"sliding outside releases"))return false;
        auto& b=find("A"); e.HandleTouchMove(one,b.pos.x/e.m_width,b.pos.y/e.m_height);
        if(!check(sample()==MH3_CL_A,"sliding back restores button"))return false;
        e.ResetInput();if(!check(sample()==0,"focus loss resets input"))return false;
        press(one,"M5"); e.CancelTouch(one);
        if(!check(sample()==0,"canceled tap does not replay"))return false;
        press(one,"R");press(two,"M5");e.CancelTouch(two);
        if(!check(sample()==MH3_CL_R,"cancel leaves other contacts held"))return false;
        e.ResetInput();
        e.m_comboMasks[4]=MH3_CL_R|MH3_CL_B|MH3_CL_Y; e.ApplyComboMasks();e.SaveLayoutConfig();
        e.m_comboMasks[4]=0;e.LoadLayoutConfig();
        e.RecalculateButtonPositions();
        if(!check(find("M5").mask==(MH3_CL_R|MH3_CL_B|MH3_CL_Y),"custom combo survives reload"))return false;
        e.ResetInput();
        uint16_t physical=MH3_CL_B|MH3_CL_ZL; int16_t lx=0,ly=0,rx=0,ry=0; uint8_t tl=23,tr=45;
        e.UpdateSample(&physical,&lx,&ly,&rx,&ry,&tl,&tr);
        if(!check(physical==(MH3_CL_B|MH3_CL_ZL) && tl==23 && tr==45,"physical gamepad state stays intact"))return false;
        MH3GameSpeed::UpdateMode(true, 1, 3, true);
        MH3GameSpeed::Reset();
        press(one,"SPEED");
        if(!check(MH3GameSpeed::Percent()==125 && sample()==0,"speed button changes time without a Wii input"))return false;
        auto& a=find("A"); e.HandleTouchMove(one,a.pos.x/e.m_width,a.pos.y/e.m_height);
        if(!check(sample()==0,"dragging speed button cannot press an attack"))return false;
        e.HandleTouchUp(one);press(two,"SPEED");e.HandleTouchUp(two);
        if(!check(MH3GameSpeed::Percent()==150,"second speed tap selects 1.5x"))return false;
        MH3GameSpeed::UpdateMode(true, 1, 6, true);
        if(!check(MH3GameSpeed::Available() && MH3GameSpeed::Percent()==150,"offline counter quest retains speed"))return false;
        press(one,"SPEED");e.HandleTouchUp(one);
        if(!check(MH3GameSpeed::Percent()==100,"third speed tap restores 1x"))return false;
        MH3GameSpeed::UpdateMode(true, 1, 4, true);
        press(one,"SPEED");e.HandleTouchUp(one);
        if(!check(MH3GameSpeed::Percent()==100 && sample()==0,"speed tap is blocked in online quests"))return false;
#ifdef MH3_TOUCH_INPUT_TESTS
        e.SetMenuOpen(true); e.m_menuTab=2;
        const float panelX=e.m_width*0.05f, panelW=e.m_width-panelX*2.0f;
        const float contentW=panelW-40.0f, contentX=panelX+20.0f;
        const float contentY=e.m_height*0.04f+86.0f+44.0f+16.0f;
        const float bw=(contentW-56.0f)/3.0f;
        const float actionY=contentY+95.0f;
        const float dialogW=560.0f, dialogX=(e.m_width-dialogW)*0.5f, dialogY=(e.m_height-220.0f)*0.5f;
        const float choiceW=(dialogW-64.0f)*0.5f;
        const float yesX=dialogX+40.0f+choiceW*1.5f, yesY=dialogY+172.0f;
        auto counts=[] { return std::array<unsigned,4>{mh3_save_manager::ExportCallsForTesting(),
            mh3_save_manager::ImportCallsForTesting(),mh3_save_manager::BackupCallsForTesting(),
            mh3_save_manager::StageCallsForTesting()}; };
        const float importX=contentX+18.0f+(bw+10.0f)+bw*0.5f;
        const auto initial=counts();
        e.HandleMenuTouchDown(importX,actionY);
        if(!check(e.m_saveBrowserOpen && !e.IsSaveConfirmationOpen() && counts()==initial,"empty browser performs no save operation"))return false;
        e.CancelModal();
        const auto importFolder=std::filesystem::path(std::getenv("MH3_USER_DIR"))/"imports/FixtureImport";
        std::filesystem::create_directories(importFolder/"nocopy");
        std::ofstream(importFolder/"nocopy/data00") << "private test input";
        const SaveAction actions[]={SaveAction::Export,SaveAction::None,SaveAction::Backup};
        for(int action : {0,2}) {
            const float actionX=contentX+18.0f+action*(bw+10.0f)+bw*0.5f;
            const auto before=counts();
            for(int i=0;i<100;++i) e.HandleMenuTouchDown(actionX,actionY);
            e.HandleMenuTouchDown(contentX+18.0f+((action+1)%3)*(bw+10.0f)+bw*0.5f,actionY);
            if(!check(e.IsSaveConfirmationOpen() && e.m_pendingSaveAction==actions[action] && counts()==before,
                      "repeated/background taps cannot execute or change a pending save action"))return false;
            e.HandleMenuTouchDown(dialogX+24.0f+choiceW*0.5f,dialogY+172.0f);
            if(!check(!e.IsSaveConfirmationOpen() && counts()==before,"No cancels without save operations"))return false;
            e.HandleMenuTouchDown(actionX,actionY);e.CancelSaveConfirmation();
            if(!check(!e.IsSaveConfirmationOpen() && counts()==before,"keyboard cancel performs no save operations"))return false;
            e.HandleMenuTouchDown(actionX,actionY);e.SetMenuOpen(false);e.SetMenuOpen(true);
            if(!check(!e.IsSaveConfirmationOpen() && counts()==before,"closing the menu cancels the pending action"))return false;
            e.HandleMenuTouchDown(actionX,actionY);
            e.HandleMenuTouchDown(yesX,yesY);
            auto expected=before;++expected[action];if(action==1)++expected[3];
            if(!check(!e.IsSaveConfirmationOpen() && counts()==expected,"Yes executes only the confirmed save action once"))return false;
            for(int i=0;i<100;++i) e.HandleMenuTouchDown(yesX,yesY);
            if(!check(counts()==expected,"repeated Yes taps cannot repeat the save operation"))return false;
        }
        if(!check(e.m_lastBackupTime=="25 Oct 2026, 12:34:56 PM (local time)" &&
                  BackupDisplayTime("unrelated-folder").empty(),"backup time is readable and invalid names are ignored"))return false;
#ifdef MH3_PC_TOUCH_UI
        e.m_menuTab=3;e.HandleMenuTouchDown(contentX+contentW-123,contentY+28);
        if(!check(e.m_keyboardEditorOpen && e.IsModalOpen(),"PC keyboard editor opens from Controls"))return false;
        const float kw=920,kx=(e.m_width-kw)*.5f,ky=(e.m_height-560)*.5f;
        const float keyW=(kw-48-kw*.30f-90-32)/3;
        const float firstKeyX=kx+24+kw*.30f+keyW*.5f,firstKeyY=ky+105;
        e.HandleMenuTouchDown(firstKeyX,firstKeyY);e.HandleKeyboardEditorKey(SDL_SCANCODE_X);
        if(!check(e.m_captureAction==0 && RowellKeys::Current().Get(0)[0]==SDL_SCANCODE_F,"conflicting key stays in capture without changing controls"))return false;
        e.HandleKeyboardEditorKey(SDL_SCANCODE_ESCAPE);
        if(!check(e.m_captureAction==-1 && e.m_keyboardEditorOpen,"Escape cancels capture and keeps the editor"))return false;
        e.HandleMenuTouchDown(firstKeyX,firstKeyY);e.HandleKeyboardEditorKey(SDL_SCANCODE_G);
        if(!check(e.m_captureAction==-1 && RowellKeys::Current().Get(0)[0]==SDL_SCANCODE_G,"editor saves a chosen key"))return false;
        e.HandleMenuTouchDown(kx+kw-69,firstKeyY);
        if(!check(RowellKeys::Current().Get(0)[0]==SDL_SCANCODE_F,"mouse reset restores an action"))return false;
        e.HandleMenuTouchDown(firstKeyX,firstKeyY);e.HandleKeyboardEditorKey(SDL_SCANCODE_F2);
        if(!check(e.m_captureAction==0 && e.IsModalOpen(),"settings key cannot replace a gameplay binding"))return false;
        e.HandleKeyboardEditorKey(SDL_SCANCODE_ESCAPE);e.HandleMenuTouchDown(kx+kw-124,ky+519);
        e.HandleMenuTouchDown(kx+104,ky+519);
        if(!check(!e.m_keyboardEditorOpen,"mouse close leaves keyboard editor"))return false;
        e.m_menuTab=2;
#endif
        // A missing physical controller must not enter keyboard capture or alter touch settings.
        e.m_menuTab=3;
        const bool touchVisible=e.m_touchVisible;
        e.HandleMenuTouchDown(contentX+contentW-123,contentY+176+12+25);
        if(!check(e.m_padEditorOpen && e.IsModalOpen(),"controller editor opens independently"))return false;
        const float pw=920,px=(e.m_width-pw)*.5f,py=(e.m_height-560)*.5f;
        e.HandleMenuTouchDown(px+pw*.65f,py+126);
        if(!check(e.m_padCaptureAction==-1 && e.m_touchVisible==touchVisible,
                  "no controller leaves touch controls unchanged and does not capture a keyboard key"))return false;
#ifdef MH3_PC_TOUCH_UI
        if(!check(!e.m_keyboardEditorOpen,"controller editor never opens keyboard editor"))return false;
#endif
        e.m_padPendingPreset=3;e.CancelModal();
        if(!check(e.m_padPendingPreset==-1 && e.m_padEditorOpen,"canceling reset retains controller editor"))return false;
        e.CancelModal();
        if(!check(!e.m_padEditorOpen,"controller editor closes safely"))return false;
        e.m_menuTab=2;
        e.BeginLayoutEdit();
        const auto editOriginal=e.CaptureLayout();
        const auto panelBefore=e.EditorPanelPosition();
        e.HandleEditModeTouchDown(901,panelBefore.x+20,panelBefore.y+20);
        e.HandleTouchMove(901,0,0);e.HandleTouchUp(901);
        if(!check(e.EditorPanelPosition().x==12 && e.EditorPanelPosition().y==12 &&
                  e.m_editorUndo.empty(),"toolbar moves within bounds independently of controls"))return false;
        auto panel=e.EditorPanelPosition();const auto panelSize=e.EditorPanelSize();
        e.HandleEditModeTouchDown(902,panel.x+panelSize.x-25,panel.y+20);
        if(!check(e.m_editorCollapsed,"toolbar collapses"))return false;
        e.HandleEditModeTouchDown(902,panel.x+panelSize.x-25,panel.y+20);
        const auto aBefore=find("A").normPos;
        e.HandleEditModeTouchDown(903,find("A").pos.x,find("A").pos.y);
        e.HandleTouchMove(903,(find("A").pos.x-50)/e.m_width,(find("A").pos.y-30)/e.m_height);
        e.HandleTouchUp(903);
        if(!check(find("A").normPos.x!=aBefore.x && !e.m_editorUndo.empty(),"control moves and creates undo history"))return false;
        const float scale=find("A").scale;
        e.HandleEditModeTouchDown(904,panel.x+110,panel.y+98);
        if(!check(find("A").scale>scale,"plus resizes the selected button"))return false;
        e.HandleEditModeTouchDown(904,panel.x+panelSize.x*.5f,panel.y+148);
        if(!check(std::abs(find("A").scale-scale)<.001f,"undo restores button size"))return false;
        e.m_comboMasks[1]=0;e.ComboEnabled(1)=false; // Explicit unused slot, not a hidden saved combo.
        e.OpenEditorCombo(false);
        if(!check(e.m_editorComboSlot==1,"add combo chooses an unused slot without overwriting hidden combos"))return false;
        const int newSlot=e.m_editorComboSlot;
        e.m_editorComboMask=MH3_CL_R|MH3_CL_X|MH3_CL_A;
        const float editorDialogW=600,editorDialogX=(e.m_width-editorDialogW)*.5f,editorDialogY=(e.m_height-330)*.5f;
        e.HandleEditorDialogTouch(editorDialogX+editorDialogW-120,editorDialogY+287);
        if(!check(e.m_editorComboSlot==-1 && e.ComboEnabled(newSlot) &&
                  e.m_comboMasks[newSlot]==(MH3_CL_R|MH3_CL_X|MH3_CL_A),"combo creation applies its selected buttons"))return false;
        e.CancelLayoutEdit();
        if(!check(!e.m_editMode && find("A").normPos.x==aBefore.x && e.m_comboMasks==editOriginal.combos &&
                  e.ComboEnabled(newSlot)==editOriginal.enabled[newSlot],"Cancel restores positions and combo settings"))return false;
        e.BeginLayoutEdit();e.m_selectedButtonIndex=int(&find("A")-e.m_buttons.data());e.AdjustSelectedButtonScale(.1f);
        const float savedScale=find("A").scale;
        panel=e.EditorPanelPosition();e.HandleEditModeTouchDown(905,panel.x+panelSize.x-80,panel.y+242);
        e.LoadLayoutConfig();e.RecalculateButtonPositions();
        if(!check(!e.m_editMode && std::abs(find("A").scale-savedScale)<.001f,"Save and play persists layout across reload"))return false;
        // Library actions and the layout editor share one transactional combo store.
        const auto libraryBefore=e.CaptureLayout();
        const int menuBeforeLibrary=e.m_menuTab;
        const auto padBefore=RowellPad::Current().Get(0,false);
#ifdef MH3_PC_TOUCH_UI
        std::array<std::array<SDL_Scancode,RowellKeys::Slots>,RowellKeys::ActionCount> keysBefore{};
        for(int i=0;i<RowellKeys::ActionCount;++i)keysBefore[i]=RowellKeys::Current().Get(i);
#endif
        e.BeginLayoutEdit();
        e.m_selectedButtonIndex=int(&find("A")-e.m_buttons.data());e.RequestComboDelete();
        if(!check(e.m_editorDeleteSlot==-1,"original Wii controls cannot be deleted"))return false;
        const int deleteSlot=0;
        e.m_selectedButtonIndex=int(&find("M1")-e.m_buttons.data());
        const auto originalMask=e.m_comboMasks[deleteSlot];const bool originalVisible=e.ComboEnabled(deleteSlot);
        e.RequestComboDelete();
        if(!check(e.m_editorDeleteSlot==deleteSlot && e.m_comboMasks[deleteSlot]==originalMask,
                  "Delete opens confirmation without changing a combo"))return false;
        e.HandleEditorDialogTouch(editorDialogX+100,editorDialogY+287);
        if(!check(e.m_editorDeleteSlot==-1 && e.m_comboMasks[deleteSlot]==originalMask,
                  "No keeps the selected custom combo"))return false;
        auto confirmDelete=[&] {
            e.RequestComboDelete(deleteSlot);e.HandleEditorDialogTouch(editorDialogX+editorDialogW-120,editorDialogY+287);
        };
        confirmDelete();
        if(!check(e.m_comboMasks[deleteSlot]==0 && !e.ComboEnabled(deleteSlot) && !e.IsButtonActive(find("M1")) &&
                  find("A").mask==MH3_CL_A,"Delete removes only the custom combo"))return false;
        panel=e.EditorPanelPosition();
        e.HandleEditModeTouchDown(906,panel.x+panelSize.x*.5f,panel.y+148);
        if(!check(e.m_comboMasks[deleteSlot]==originalMask && e.ComboEnabled(deleteSlot)==originalVisible,
                  "Undo restores a deleted combo and its visibility"))return false;
        confirmDelete();e.CancelLayoutEdit();
        if(!check(e.m_comboMasks==libraryBefore.combos,"Cancel restores combo definitions before editing"))return false;
        // Save a deletion and reload it: no hidden default may recreate it.
        e.BeginLayoutEdit();confirmDelete();panel=e.EditorPanelPosition();
        e.HandleEditModeTouchDown(907,panel.x+panelSize.x-80,panel.y+242);
        e.LoadLayoutConfig();e.RecalculateButtonPositions();
        if(!check(!e.m_editMode && !e.m_comboMasks[deleteSlot] && !e.ComboEnabled(deleteSlot),
                  "saved deletion survives reload"))return false;
        e.BeginLayoutEdit();e.OpenEditorCombo(false);
        if(!check(e.m_editorComboSlot==deleteSlot,"Add reuses a deleted slot"))return false;
        e.m_editorComboMask=MH3_CL_R|MH3_CL_X|MH3_CL_A;
        e.HandleEditorDialogTouch(editorDialogX+editorDialogW-120,editorDialogY+287);
        panel=e.EditorPanelPosition();e.HandleEditModeTouchDown(908,panel.x+panelSize.x-80,panel.y+242);
        e.LoadLayoutConfig();e.RecalculateButtonPositions();
        if(!check(e.m_comboMasks[deleteSlot]==(MH3_CL_R|MH3_CL_X|MH3_CL_A) && e.ComboEnabled(deleteSlot),
                  "new combo persists in the reused slot"))return false;
        const auto reloaded=e.CaptureLayout();
        e.SetMenuOpen(true);e.m_menuTab=5;
        const float libraryX=e.m_width*.05f+20,libraryY=e.m_height*.04f+146,libraryW=e.m_width*.9f-40;
        e.HandleComboTap(libraryX+libraryW-172,libraryY+82+30,libraryX,libraryY,libraryW);
        if(!check(e.m_editMode && e.m_editorComboSlot==0 && !e.m_menuOpen,
                  "library Edit opens the shared combo editor"))return false;
        e.CancelLayoutEdit();
        if(!check(e.m_menuOpen && e.m_menuTab==5 && e.m_comboMasks==reloaded.combos,
                  "Cancel from the library returns without applying edits"))return false;
        // Visibility is distinct from existence: hidden saved combos are not free slots.
        e.OpenLibraryCombo(0);e.HandleEditorDialogTouch(editorDialogX+100,editorDialogY+230);
        e.HandleEditorDialogTouch(editorDialogX+editorDialogW-120,editorDialogY+287);
        if(!check(!e.ComboEnabled(0) && e.m_comboMasks[0]!=0,"hide preserves the combo definition"))return false;
        e.CancelLayoutEdit();
        if(!check(e.ComboEnabled(0),"Cancel restores visibility changes"))return false;
        e.BeginLayoutEdit();
        for(int i=0;i<5;++i){e.m_comboMasks[i]=MH3_CL_A|MH3_CL_X;e.ComboEnabled(i)=false;}
        const auto fullLibrary=e.m_comboMasks;e.OpenEditorCombo(false);
        if(!check(e.m_editorComboSlot==-1 && e.m_comboMasks==fullLibrary,
                  "Add never overwrites hidden saved combinations when the library is full"))return false;
        e.CancelLayoutEdit();
        if(!check(RowellPad::Current().Get(0,false)==padBefore,"combo editing preserves physical controller mappings"))return false;
#ifdef MH3_PC_TOUCH_UI
        for(int i=0;i<RowellKeys::ActionCount;++i)
            if(!check(RowellKeys::Current().Get(i)==keysBefore[i],"combo editing preserves keyboard mappings"))return false;
        // Confirm the warning inspects real saved assignments, not device polling.
        for(auto key:{SDL_SCANCODE_F8,SDL_SCANCODE_F9,SDL_SCANCODE_F10,SDL_SCANCODE_F11,SDL_SCANCODE_F12}) {
            bool used=false;for(const auto& action:keysBefore)for(auto existing:action)used|=existing==key;
            if(used)continue;
            const int action=RowellKeys::ActionCount-5;
            const auto previous=RowellKeys::Current().Get(action)[0];
            if(!check(RowellKeys::Current().Assign(action,0,key).empty() && e.ComboHasShortcut(0),
                      "delete warning detects an assigned keyboard shortcut"))return false;
            if(!check(RowellKeys::Current().Assign(action,0,previous).empty(),"warning fixture restores the original key assignment"))return false;
            break;
        }
#endif
        e.RestoreLayout(libraryBefore);e.SaveLayoutConfig();e.m_menuTab=menuBeforeLibrary;
        std::printf("Combo library: protected deletion, confirmation, Undo, Cancel, persistence, slot reuse and independent bindings passed.\n");
        // Check actual generated glyph geometry, including the text shadow.
        for(float radius : {16.0f,28.0f,50.0f,90.0f}) {
            e.m_verts.clear();e.DrawButtonLabel(100,100,"R+X+A",radius*.8f,COL_WHITE,radius*1.48f);
            for(const auto& vertex:e.m_verts)if(vertex.type==3)
                if(!check(vertex.x>100-radius && vertex.x<100+radius,"combo text fits inside its button"))return false;
        }
        e.m_verts.clear();e.RenderGameplayOverlay();
        for(const auto& vertex:e.m_verts)if(vertex.type==0 || vertex.type==1)
            if(!check(std::abs(vertex.r-vertex.g)<.001f && std::abs(vertex.r-vertex.b)<.001f,
                      "gameplay button shapes retain their neutral finish"))return false;
        // Actual glyph quads must stay inside narrow buttons at every supported UI scale.
        for(const char* label:{"Shoulders/triggers: ORIGINAL","R+X+A","Reset to defaults"}) {
            e.m_verts.clear();e.DrawRoundedRect(20,20,172,40,8,COL_ACTIVE_CHIP,COL_CHIP_BORDER);
            e.DrawText(106,30,label,18,COL_WHITE,true);
            for(const auto& vertex:e.m_verts)if(vertex.type==3)
                if(!check(vertex.x>=29.5f && vertex.x<=182.5f && vertex.y>=25.5f && vertex.y<=54.5f,
                          "button text is fitted and vertically centered"))return false;
        }
        e.m_verts.clear();
        e.DrawClippedText(10,10,"Village / solo quests. Actual FPS varies by device.",15,200,COL_MUTED);
        for(const auto& vertex:e.m_verts)if(vertex.type==3)
            if(!check(vertex.x>=9 && vertex.x<=210,"helper text stays in its own column"))return false;
        if(!check(FONT_RAW_SIZE==524288,"two font weights and icons keep the original texture budget"))return false;
        e.m_verts.clear();e.SetMenuOpen(true);
        const auto beforeRestore=counts();
        std::filesystem::create_directories(importFolder.parent_path().parent_path()/"saves");
        e.HandleMenuTouchDown(importX,actionY);
        const float browserW=900,browserX=(e.m_width-browserW)*.5f,browserY=(e.m_height-520)*.5f;
        e.HandleMenuTouchDown(browserX+browserW-124,browserY+477);
        if(!check(e.m_saveBrowserOpen && e.m_selectedSave==-1 && !e.IsSaveConfirmationOpen() && counts()==beforeRestore,
                  "browser never selects or restores a default copy"))return false;
        e.HandleMenuTouchDown(browserX+100,browserY+132+2*64+28);
        e.HandleMenuTouchDown(browserX+browserW-124,browserY+477);
        if(!check(e.IsSaveConfirmationOpen() && e.m_pendingImportFolder.filename()=="Backup-20261025-160000-001" && counts()==beforeRestore,
                  "choosing an older backup only opens review"))return false;
        e.HandleMenuTouchDown(dialogX+24+choiceW*.5f,yesY);
        if(!check(e.m_saveBrowserOpen && !e.IsSaveConfirmationOpen() && counts()==beforeRestore,"No returns to the chooser without writing saved files"))return false;
        e.HandleMenuTouchDown(browserX+browserW*.75f,browserY+97);
        if(!check(!e.m_browseBackups && e.m_selectedSave==-1,"changing source tab clears selection"))return false;
        e.HandleMenuTouchDown(browserX+100,browserY+160);
        e.HandleMenuTouchDown(browserX+browserW-124,browserY+477);
        e.HandleMenuTouchDown(yesX,yesY);
        if(!check(mh3_save_manager::StagedImportForTesting()==importFolder && counts()[3]==beforeRestore[3]+1 &&
                  counts()[1]==beforeRestore[1] && e.m_restoreRestarting,"Yes stages the selected import and requests restart without rewriting live saves"))return false;
        for(int i=0;i<100;++i)e.HandleMenuTouchDown(yesX,yesY);
        if(!check(counts()[3]==beforeRestore[3]+1,"repeated taps while restarting cannot queue additional restores"))return false;
        std::printf("Save UI: export/backup confirmation, explicit older-copy selection, no automatic choice, cancel, staged restart and repeat-tap checks passed.\n");
#endif
#ifdef MH3_TOUCH_INPUT_TESTS
        // Optional geometry capture for visual QA; absent from normal app builds.
        if(const char* directory=std::getenv("MH3_TOUCH_MENU_PREVIEW")) {
            std::filesystem::create_directories(directory);
            std::vector<uint8_t> pixels(FONT_RAW_SIZE);DecompressFontAtlas(pixels.data());
            auto captureEditor=[&](const char* name) {
                e.m_verts.clear();e.RenderEditMode();
                const uint32_t header[]={uint32_t(e.m_width),720,uint32_t(e.m_verts.size())};
                std::ofstream out(std::filesystem::path(directory)/(std::string(name)+".bin"),std::ios::binary);
                out.write(reinterpret_cast<const char*>(header),sizeof(header));
                out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
                out.write(reinterpret_cast<const char*>(e.m_verts.data()),e.m_verts.size()*sizeof(UIVertex));
            };
            e.SetMenuOpen(true);
            for(int width:{960,1280,1600})for(int tab=0;tab<7;++tab) {
                e.UpdateLayout(width,720);e.m_menuTab=tab;e.m_verts.clear();e.RenderSettingsMenu();
                for(const auto& v:e.m_verts)if(v.type==3)
                    if(!check(v.x>=-1 && v.x<=width+1 && v.y>=-1 && v.y<=721,
                              "menu glyphs remain within compact and wide landscape screens"))return false;
                const uint32_t header[]={uint32_t(width),720,uint32_t(e.m_verts.size())};
                std::ofstream out(std::filesystem::path(directory)/("menu-"+std::to_string(width)+"-"+std::to_string(tab)+".bin"),std::ios::binary);
                out.write(reinterpret_cast<const char*>(header),sizeof(header));
                out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
                out.write(reinterpret_cast<const char*>(e.m_verts.data()),e.m_verts.size()*sizeof(UIVertex));
                if(!check(bool(out),"menu geometry preview was written"))return false;
            }
            e.UpdateLayout(1280,720);e.OpenLibraryCombo(0);
            captureEditor("edit-combo");e.RequestComboDelete(0);captureEditor("delete-combo");
            e.CancelLayoutEdit();
        }
#endif
        std::printf("Touch controls: 21 input, persistence and speed-button checks passed.\n");return true;
    }

    void UpdateLayout(int width, int height) {
        if (width <= 0 || height <= 0) return;
        if (height > 0 && int(float(width)*720.0f/height)==m_width && m_height==720) return;

        m_width = std::max(1, int(float(width) * 720.0f / float(height)));
        m_height = 720;
        m_unit = 720.0f;

        RecalculateButtonPositions();
    }

    void RecalculateButtonPositions() {
        if (m_buttons.empty()) {
            InitDefaultButtons();
        }

        for (auto& btn : m_buttons) {
            btn.pos.x = btn.normPos.x * float(m_width);
            btn.pos.y = btn.normPos.y * float(m_height);
            btn.radius = btn.defaultNormRadius * m_unit * btn.scale;
        }

        // Left stick
        m_leftStickCenter = { m_normLeftStickPos.x * float(m_width), m_normLeftStickPos.y * float(m_height) };
        m_leftStickRadius = m_unit * 0.15f * m_leftStickScale;
        m_leftStickKnobRadius = m_unit * 0.075f * m_leftStickScale;
        m_leftStickPos = m_leftStickCenter;

        // Right stick
        m_rightStickRadius = m_unit * 0.15f;
        m_rightStickKnobRadius = m_unit * 0.075f;

        // Gear / Settings button
        m_gearPos = { float(m_width) * 0.5f + m_unit * 0.22f, m_unit * 0.075f };
        m_gearRadius = m_unit * 0.040f;
    }

    void InitDefaultButtons() {
        m_buttons.clear();

        // 1. Action Diamond (Lower-right corner)
        float diamondNormX = 0.90f;
        float diamondNormY = 0.77f;
        float spreadX = 0.055f;
        float spreadY = 0.10f;
        float btnR = 0.058f;

        // Macro Buttons FIRST (checked before individual buttons for collision priority)
        AddBtn("M1", "A+X", (MH3_CL_A | MH3_CL_X), { diamondNormX - spreadX * 1.8f, diamondNormY - spreadY * 0.8f }, btnR * 0.95f,
               { 0.85f, 0.30f, 0.60f, 0.60f }, { 1.0f, 0.45f, 0.80f, 0.95f });
        AddBtn("M2", "B+Y", (MH3_CL_B | MH3_CL_Y), { diamondNormX - spreadX * 1.8f, diamondNormY + spreadY * 0.8f }, btnR * 0.95f,
               { 0.30f, 0.75f, 0.75f, 0.60f }, { 0.45f, 0.95f, 0.95f, 0.95f });
        AddBtn("M3", "L+R", (MH3_CL_L | MH3_CL_R), { 0.50f, 0.15f }, btnR * 0.85f,
               { 0.55f, 0.35f, 0.85f, 0.60f }, { 0.75f, 0.55f, 1.00f, 0.95f });
        AddBtn("M4", "A+B", (MH3_CL_A | MH3_CL_B), { diamondNormX + spreadX * 0.5f, diamondNormY + spreadY * 1.15f }, btnR * 0.95f,
               { 0.85f, 0.48f, 0.18f, 0.60f }, { 1.0f, 0.70f, 0.30f, 0.95f });

        AddBtn("M5", "R+X+A", MH3_CL_R | MH3_CL_X | MH3_CL_A,
               { 0.74f, 0.56f }, btnR * 1.18f,
               {0.18f, 0.45f, 0.65f, 0.60f}, {0.35f, 0.90f, 1.0f, 0.95f});

        // A, B, X, Y (after macros so macros win collision priority)
        AddBtn("A", "A", MH3_CL_A, { diamondNormX + spreadX, diamondNormY }, btnR,
               { 0.85f, 0.25f, 0.25f, 0.55f }, { 1.0f, 0.40f, 0.40f, 0.90f });
        AddBtn("B", "B", MH3_CL_B, { diamondNormX, diamondNormY + spreadY }, btnR,
               { 0.25f, 0.65f, 0.30f, 0.55f }, { 0.35f, 0.95f, 0.45f, 0.90f });
        AddBtn("X", "X", MH3_CL_X, { diamondNormX, diamondNormY - spreadY }, btnR,
               { 0.25f, 0.45f, 0.85f, 0.55f }, { 0.40f, 0.70f, 1.00f, 0.90f });
        AddBtn("Y", "Y", MH3_CL_Y, { diamondNormX - spreadX, diamondNormY }, btnR,
               { 0.75f, 0.65f, 0.20f, 0.55f }, { 0.95f, 0.85f, 0.30f, 0.90f });

        // Shoulders (Top corners)
        float topY = 0.10f;
        float shoulderR = 0.052f;
        AddBtn("L",  "L",  MH3_CL_L,  { 0.055f, topY }, shoulderR * 1.25f,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("ZL", "ZL", MH3_CL_ZL, { 0.125f, topY }, shoulderR * 1.10f,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("ZR", "ZR", MH3_CL_ZR, { 0.875f, topY }, shoulderR * 1.10f,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("R",  "R",  MH3_CL_R,  { 0.945f, topY }, shoulderR * 1.25f,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });

        // D-Pad (Clean arrows)
        float dpadX = 0.11f;
        float dpadY = 0.44f;
        float dpadSpreadX = 0.038f;
        float dpadSpreadY = 0.075f;
        float dpadR = 0.048f;
        AddBtn("UP",    "▲", MH3_CL_UP,    { dpadX, dpadY - dpadSpreadY }, dpadR,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("DOWN",  "▼", MH3_CL_DOWN,  { dpadX, dpadY + dpadSpreadY }, dpadR,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("LEFT",  "◀", MH3_CL_LEFT,  { dpadX - dpadSpreadX, dpadY }, dpadR,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("RIGHT", "▶", MH3_CL_RIGHT, { dpadX + dpadSpreadX, dpadY }, dpadR,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });

        // Menu Buttons (Top Center)
        float menuR = 0.040f;
        float menuY = 0.075f;
        AddBtn("MINUS", "-", MH3_CL_MINUS, { 0.46f, menuY }, menuR,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("PLUS",  "+", MH3_CL_PLUS,  { 0.54f, menuY }, menuR,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        AddBtn("SPEED", "1x", 0, { 0.36f, menuY }, menuR * 1.20f,
               { 0.15f, 0.22f, 0.28f, 0.55f }, { 0.65f, 0.85f, 1.00f, 0.85f });
        ApplyComboMasks();
    }

    void AddBtn(const std::string& id, const char* label, uint16_t mask, Vec2 normPos, float normRadius,
                Color base, Color border) {
        ConfigurableButton b;
        b.id = id;
        b.label = label;
        b.mask = mask;
        b.defaultNormPos = normPos;
        b.defaultNormRadius = normRadius;
        b.normPos = normPos;
        b.scale = 1.0f;
        b.baseColor = base;
        b.borderColor = border;
        m_buttons.push_back(b);
    }

    ConfigurableButton* HitButton(float px, float py) {
        ConfigurableButton* best = nullptr;
        float distance = 1.18f * 1.18f;
        for (auto& b : m_buttons) {
            if (!IsButtonActive(b) || b.radius <= 0) continue;
            float dx = (px - b.pos.x) / b.radius, dy = (py - b.pos.y) / b.radius;
            float score = dx * dx + dy * dy;
            if (score <= distance) { distance = score; best = &b; }
        }
        return best;
    }

    void HandleTouchDown(SDL_FingerID id, float normX, float normY) {
        if (!m_enabled) return;
        float px = normX * float(m_width);
        float py = normY * float(m_height);

        if (m_editMode) {
            HandleEditModeTouchDown(id, px, py);
            return;
        }

        if (m_menuOpen) {
            HandleMenuTouchDown(px, py);
            return;
        }

        // Gameplay: Check Gear button tap
        float gdx = px - m_gearPos.x;
        float gdy = py - m_gearPos.y;
        if (gdx * gdx + gdy * gdy <= (m_gearRadius * 1.8f) * (m_gearRadius * 1.8f)) {
            SetMenuOpen(true);
            m_menuTab = 3; // Gráficos
            return;
        }

        if (!m_touchVisible) {
            // Touch controls are hidden; ignore touches to prevent accidental inputs
            return;
        }

        if (auto* btn = HitButton(px, py)) {
            m_buttonGestures.Press(id);
            if (btn->id == "SPEED") {
                m_speedGestures.Press(id);
                btn->contacts.Press(id);
                btn->isPressed = btn->contacts.Held();
                MH3GameSpeed::Cycle();
                return;
            }
            if (btn->contacts.Press(id)) m_latchedMask.fetch_or(btn->mask);
            btn->isPressed = btn->contacts.Held();
            return;
        }

        // Left Stick
        if (px < float(m_width) * 0.45f) {
            if (!m_leftFingerId.has_value()) {
                float dx = px - m_leftStickCenter.x;
                float dy = py - m_leftStickCenter.y;
                float distSq = dx * dx + dy * dy;
                if (distSq <= (m_leftStickRadius * 2.2f) * (m_leftStickRadius * 2.2f) || py > float(m_height) * 0.5f) {
                    m_leftFingerId = id;
                    UpdateLeftStickPosition(px, py);
                    return;
                }
            }
        }

        // Right Stick (Free floating camera look)
        if (px >= float(m_width) * 0.45f) {
            if (!m_rightFingerId.has_value()) {
                m_rightFingerId = id;
                m_rightFloatingOrigin = { px, py };
                m_rightStickPos = { px, py };
                m_rightLastPos = { px, py };
                m_rightStickActive = true;
                return;
            }
        }
    }

    void HandleTouchMove(SDL_FingerID id, float normX, float normY) {
        if (!m_enabled) return;
        float px = normX * float(m_width);
        float py = normY * float(m_height);

        if (m_editMode) {
            if (m_editDragging && m_editFingerId == id) MoveEditorControl(px,py);
            return;
        }

        if (m_menuOpen) return;
        if (m_speedGestures.Contains(id)) return;
        bool buttonContact = false;
        for (auto& b : m_buttons) if (b.contacts.Contains(id)) {
            b.contacts.Release(id); b.isPressed = b.contacts.Held(); buttonContact = true;
        }
        if (buttonContact || m_buttonGestures.Contains(id)) {
            if (auto* b = HitButton(px, py); b && b->id != "SPEED") {
                b->contacts.Press(id); b->isPressed = true;
                m_latchedMask.fetch_or(b->mask);
            }
            return;
        }

        if (id == m_leftFingerId) {
            UpdateLeftStickPosition(px, py);
            return;
        }

        if (id == m_rightFingerId && m_rightStickActive) {
            float dx = px - m_rightLastPos.x;
            float dy = py - m_rightLastPos.y;
            m_rightLastPos = { px, py };

            m_cameraDeltaX += dx;
            m_cameraDeltaY += dy;

            float relX = px - m_rightFloatingOrigin.x;
            float relY = py - m_rightFloatingOrigin.y;
            float dist = std::sqrt(relX * relX + relY * relY);
            if (dist > m_rightStickRadius) {
                m_rightStickPos.x = m_rightFloatingOrigin.x + (relX / dist) * m_rightStickRadius;
                m_rightStickPos.y = m_rightFloatingOrigin.y + (relY / dist) * m_rightStickRadius;
            } else {
                m_rightStickPos = { px, py };
            }

            float nx = std::clamp(relX / m_rightStickRadius, -1.0f, 1.0f);
            float ny = std::clamp(relY / m_rightStickRadius, -1.0f, 1.0f);
            mh3_classic_stick(nx, -ny, &mh3_gamepad_stick_curve, &m_rightX, &m_rightY);
            return;
        }
    }

    void HandleTouchUp(SDL_FingerID id) {
        m_speedGestures.Release(id);
        if (!m_enabled) return;
        m_buttonGestures.Release(id);

        if (m_editMode) {
            if (m_editFingerId == id) {
                m_editDragging = false; m_editorPanelDragging=false;
                m_editFingerId.reset();
            }
            return;
        }

        if (m_menuOpen) return;

        for (auto& btn : m_buttons) {
            btn.contacts.Release(id);
            btn.isPressed = btn.contacts.Held();
        }

        if (id == m_leftFingerId) {
            m_leftFingerId.reset();
            m_leftStickPos = m_leftStickCenter;
            m_leftX = 0;
            m_leftY = 0;
        }

        if (id == m_rightFingerId) {
            m_rightFingerId.reset();
            m_rightStickActive = false;
            m_rightX = 0;
            m_rightY = 0;
        }
    }

    void CancelTouch(SDL_FingerID id) {
        HandleTouchUp(id);
        m_latchedMask.store(0); // A canceled gesture must not replay as a short tap.
    }

    void HandleMouseDown(float px, float py) {
        if (!m_enabled) return;
        if (m_editMode) {
            HandleEditModeTouchDown(1, px, py);
            return;
        }
        if (m_menuOpen) {
            HandleMenuTouchDown(px, py);
            return;
        }
        float gdx = px - m_gearPos.x;
        float gdy = py - m_gearPos.y;
        if (gdx * gdx + gdy * gdy <= (m_gearRadius * 2.0f) * (m_gearRadius * 2.0f)) {
            ToggleMenu();
            return;
        }
        if (m_touchVisible && m_width > 0 && m_height > 0) {
            HandleTouchDown(UINT64_MAX, px / m_width, py / m_height);
        }
    }

    void HandleMouseUp(float px, float py) {
        (void)px; (void)py;
        HandleTouchUp(UINT64_MAX);
        if (m_editMode) {
            m_editDragging = false; m_editorPanelDragging=false;
            m_editFingerId.reset();
        }
    }

    void HandleMouseMove(float px, float py) {
        if (!m_editMode && m_width > 0 && m_height > 0)
            HandleTouchMove(UINT64_MAX, px / m_width, py / m_height);
        if (m_editMode && m_editDragging) MoveEditorControl(px,py);
    }

    void UpdateLeftStickPosition(float px, float py) {
        float dx = px - m_leftStickCenter.x;
        float dy = py - m_leftStickCenter.y;
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist > m_leftStickRadius) {
            dx = (dx / dist) * m_leftStickRadius;
            dy = (dy / dist) * m_leftStickRadius;
        }

        m_leftStickPos = { m_leftStickCenter.x + dx, m_leftStickCenter.y + dy };

        float nx = dx / m_leftStickRadius;
        float ny = dy / m_leftStickRadius;
        mh3_classic_stick(nx, -ny, &mh3_gamepad_stick_curve, &m_leftX, &m_leftY);
    }

    bool IsButtonActive(const ConfigurableButton& btn) const {
        if(btn.id.size()==2 && btn.id[0]=='M' && btn.id[1]>='1' && btn.id[1]<='5' &&
           !m_comboMasks[btn.id[1]-'1'])return false;
        if (btn.id == "M1") return m_macro1Enabled;
        if (btn.id == "M2") return m_macro2Enabled;
        if (btn.id == "M3") return m_macro3Enabled;
        if (btn.id == "M4") return m_macro4Enabled;
        if (btn.id == "M5") return m_macro5Enabled;
        return true;
    }

    void UpdateSample(uint16_t* buttons, int16_t* leftX, int16_t* leftY,
                      int16_t* rightX, int16_t* rightY,
                      uint8_t* triggerL, uint8_t* triggerR) {
#ifdef __ANDROID__
        if(AndroidKeyboard_IsImeShowing()) {
            ResetInput();
            if(buttons)*buttons=0;
            if(leftX)*leftX=0; if(leftY)*leftY=0; if(rightX)*rightX=0; if(rightY)*rightY=0;
            if(triggerL)*triggerL=0; if(triggerR)*triggerR=0;
            return;
        }
#endif
        if (!m_enabled || m_menuOpen || m_editMode) return;

        uint32_t latched = m_latchedMask.exchange(0, std::memory_order_relaxed);
        uint16_t touchMask = uint16_t(latched);

        for (const auto& btn : m_buttons) {
            if (btn.isPressed && IsButtonActive(btn)) {
                touchMask |= btn.mask;
            }
        }


        *buttons |= touchMask;
        if (m_leftFingerId.has_value()) {
            *leftX = m_leftX;
            *leftY = m_leftY;
        }

        if (m_rightStickActive) {
            if (m_rightX != 0 || m_rightY != 0) {
                *rightX = m_rightX;
                *rightY = m_rightY;
            }
        }

        if (touchMask & MH3_CL_L)  *triggerL = 255;
        if (touchMask & MH3_CL_ZL) *triggerL = 255;
        if (touchMask & MH3_CL_R)  *triggerR = 255;
        if (touchMask & MH3_CL_ZR) *triggerR = 255;

        float camX = m_cameraDeltaX.exchange(0.0f);
        float camY = m_cameraDeltaY.exchange(0.0f);
        if (std::abs(camX) > 0.01f || std::abs(camY) > 0.01f) {
            QueueTouchCameraDelta(camX, camY);
        }
    }

    // Lite keeps the HUD inexpensive and avoids repeatedly probing protected
    // sysfs temperature files on Android Go devices.
    static float GetDeviceTemperature() { return 0.0f; }

    bool IsOnlinePlay() const {
        if (!Memory::Contains(0x8065AA84, 2)) return false;
        return MH3GameSpeed::IsOnlinePlay(Memory::Read8(0x8065AA84), Memory::Read8(0x8065AA85));
    }

    // -------------------------------------------------------------
    // OpenGL ES Rendering
    // -------------------------------------------------------------
    void RenderGL(int windowWidth, int windowHeight) {
        if (!m_enabled) return;
#ifdef __ANDROID__
        AndroidKeyboard_UpdatePerFrame();
#endif
        UpdateLayout(windowWidth, windowHeight);
#if defined(MH3_PC_TOUCH_UI)
        // Reassert the visible pointer after menu changes and Alt Tab, even if
        // the game input path has returned early because a menu is open.
        if(auto* window=aurora_get_window(); window && (WantsPointer() || SDL_GetKeyboardFocus()!=window)) {
            if(SDL_GetWindowRelativeMouseMode(window))SDL_SetWindowRelativeMouseMode(window,false);
            if(!SDL_CursorVisible())SDL_ShowCursor();
        }
#endif

        static bool s_firstFrameApplied = false;
        if (!s_firstFrameApplied) {
            s_firstFrameApplied = true;
            ApplyResolutionScale(m_resolutionIndex);
            if (m_fullscreen) {
                ApplyFullscreen(true);
            } else {
                ApplyAspectRatio(m_aspectIndex);
            }
        }

        static bool s_aspectMemoryPatched = false;
        if (!s_aspectMemoryPatched) {
#if defined(__cpp_exceptions)
            try {
#endif
                if (Memory::Contains(0x80797F68, 4) && Memory::Contains(0x8065B4C8, 4)) {
                    uint32_t wideVal = Memory::Read32(0x80797F68);
                    if (wideVal != 0) {
#ifdef MH3_PC_TOUCH_UI
                        if (m_aspectIndex != 1) {
#else
                        if (m_fullscreen || m_aspectIndex == 0) {
#endif
                            Memory::Write32(0x8065B4C8, wideVal);
                            Memory::Write8(0x8065B4D5, 1);
                        } else if (m_aspectIndex == 1) {
                            uint32_t stdVal = Memory::Read32(0x80797F70);
                            if (stdVal != 0) Memory::Write32(0x8065B4C8, stdVal);
                            Memory::Write8(0x8065B4D5, 0);
                        }
                        s_aspectMemoryPatched = true;
                    }
                }
#if defined(__cpp_exceptions)
            } catch (...) {}
#endif
        }

        InitGL();
        if (!m_shaderProgram) return;

        // Compute Live FPS
        const auto now = std::chrono::steady_clock::now();
        static uint64_t lastCount = MH3FrameRateControl::LogicalFrameCount();
        static auto lastStamp = now;
        const double elapsed = std::chrono::duration<double>(now - lastStamp).count();
        if (elapsed >= 0.5) {
            const uint64_t count = MH3FrameRateControl::LogicalFrameCount();
            m_liveFps = double(count - lastCount) / elapsed;
            lastCount = count;
            lastStamp = now;
        }

        // Game mode 2 includes BOTH Moga Village and City. Play mode 4 is
        // online; village play mode 5 must retain its chosen offline limit.
        const int desiredFps = IsOnlinePlay() ? 30 : m_fpsLimit;
        if (MH3FrameRateControl::TargetFPS() != desiredFps)
            MH3FrameRateControl::SetTargetFPS(desiredFps);

        // Save OpenGL State
        GLint lastViewport[4]; glGetIntegerv(GL_VIEWPORT, lastViewport);
        GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
        GLint lastScissor[4]; glGetIntegerv(GL_SCISSOR_BOX, lastScissor);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
        GLint lastProgram = 0; glGetIntegerv(GL_CURRENT_PROGRAM, &lastProgram);
        GLint lastVAO = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &lastVAO);
        GLint lastVBO = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &lastVBO);
        GLint lastActiveTex = 0; glGetIntegerv(GL_ACTIVE_TEXTURE, &lastActiveTex);
        glActiveTexture(GL_TEXTURE0);
        GLint lastTex = 0; glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTex);
        GLint lastBlendSrc, lastBlendDst, lastBlendSrcA, lastBlendDstA;
        GLint lastEq, lastEqAlpha, lastSampler;
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &lastEq); glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &lastEqAlpha);
        glGetIntegerv(GL_SAMPLER_BINDING, &lastSampler);
        GLboolean stencilEnabled=glIsEnabled(GL_STENCIL_TEST);
        glGetIntegerv(GL_BLEND_SRC_RGB, &lastBlendSrc); glGetIntegerv(GL_BLEND_DST_RGB, &lastBlendDst);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &lastBlendSrcA); glGetIntegerv(GL_BLEND_DST_ALPHA, &lastBlendDstA);

        glViewport(0, 0, windowWidth, windowHeight);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBindSampler(0,0);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        glUseProgram(m_shaderProgram);
        glUniform2f(m_uScreenSize, float(m_width), float(m_height));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_fontTex);
        glUniform1i(m_uFontTex, 0);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

        m_verts.clear();
        if (m_verts.capacity() < 24000) m_verts.reserve(24000);

        if (m_editMode) {
            RenderEditMode();
        } else if (m_menuOpen) {
            RenderSettingsMenu();
            if(m_saveBrowserOpen)RenderSaveBrowser();
            if (IsSaveConfirmationOpen()) RenderSaveConfirmation();
            if(m_padEditorOpen)RenderPadEditor();
#ifdef MH3_PC_TOUCH_UI
            if(m_keyboardEditorOpen)RenderKeyboardEditor();
#endif
        } else {
            if (m_touchVisible) {
                RenderGameplayOverlay();
            } else {
                // Controls hidden: render subtle floating gear icon to allow reopening settings
                DrawAADisc(m_gearPos.x, m_gearPos.y, m_gearRadius,
                           { 0.08f, 0.40f, 0.48f, 0.35f }, { 0.25f, 0.90f, 0.95f, 0.55f }, 0.10f);
                DrawButtonLabel(m_gearPos.x, m_gearPos.y, "⚙", m_gearRadius * 1.05f, { 1.0f, 1.0f, 1.0f, 0.65f });

                RenderFpsHud();
            }
        }

        if (!m_verts.empty()) {
            glBufferData(GL_ARRAY_BUFFER, m_verts.size() * sizeof(UIVertex), m_verts.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, GLsizei(m_verts.size()));
        }

        // Restore OpenGL State
        glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
        if (scissorEnabled) { glEnable(GL_SCISSOR_TEST); glScissor(lastScissor[0], lastScissor[1], lastScissor[2], lastScissor[3]); }
        if (!blendEnabled) glDisable(GL_BLEND);
        if (depthEnabled) glEnable(GL_DEPTH_TEST);
        if (cullEnabled) glEnable(GL_CULL_FACE);
        glUseProgram(lastProgram);
        glBindTexture(GL_TEXTURE_2D, lastTex);
        glActiveTexture(lastActiveTex);
        glBlendFuncSeparate(lastBlendSrc, lastBlendDst, lastBlendSrcA, lastBlendDstA);
        glBlendEquationSeparate(lastEq,lastEqAlpha);
        glBindSampler(0,lastSampler);
        if(stencilEnabled) glEnable(GL_STENCIL_TEST);
        glBindVertexArray(lastVAO);
        glBindBuffer(GL_ARRAY_BUFFER, lastVBO);
    }

private:
    void InitGL() {
        if (m_shaderProgram && glIsProgram(m_shaderProgram)) return;
        m_shaderProgram=m_vao=m_vbo=m_fontTex=0;
        struct RestoreBindings {
            GLint vao=0,vbo=0,active=0,texture=0;
            RestoreBindings() {
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao); glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&vbo);
                glGetIntegerv(GL_ACTIVE_TEXTURE,&active); glActiveTexture(GL_TEXTURE0);
                glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);
            }
            ~RestoreBindings() {
                glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
                glBindTexture(GL_TEXTURE_2D,texture); glActiveTexture(active);
            }
        } restoreBindings;

        const char* vertShaderSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "layout(location = 0) in vec2 aPos;\n"
            "layout(location = 1) in vec2 aUV;\n"
            "layout(location = 2) in vec4 aColor;\n"
            "layout(location = 3) in vec4 aBorderColor;\n"
            "layout(location = 4) in vec4 aParams;\n"
            "uniform vec2 uScreenSize;\n"
            "out vec2 vUV;\n"
            "out vec4 vColor;\n"
            "out vec4 vBorderColor;\n"
            "out vec4 vParams;\n"
            "void main() {\n"
            "    vUV = aUV;\n"
            "    vColor = aColor;\n"
            "    vBorderColor = aBorderColor;\n"
            "    vParams = aParams;\n"
            "    vec2 clip = vec2(2.0 * aPos.x / uScreenSize.x - 1.0, 1.0 - 2.0 * aPos.y / uScreenSize.y);\n"
            "    gl_Position = vec4(clip, 0.0, 1.0);\n"
            "}\n";

        const char* fragShaderSrc =
            "#version 300 es\n"
            "precision highp float;\n"
            "in vec2 vUV;\n"
            "in vec4 vColor;\n"
            "in vec4 vBorderColor;\n"
            "in vec4 vParams;\n"
            "uniform sampler2D uFontTex;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    float type = vParams.x;\n"
            "    if (type < 0.5) {\n" // 0: Anti-aliased Disc (Button/Stick)
            "        float d = length(vUV);\n"
            "        float f = vParams.z;\n"
            "        float alpha = smoothstep(1.0, 1.0 - f, d);\n"
            "        if (alpha <= 0.0) discard;\n"
            "        vec4 bodyCol = vColor;\n"
            "        float ringThick = vParams.y;\n"
            "        float isBorder = smoothstep(1.0 - ringThick - f, 1.0 - ringThick, d);\n"
            "        vec4 rimCol = vBorderColor;\n"
            "        vec4 col = mix(bodyCol, rimCol, isBorder);\n"
            "        FragColor = vec4(col.rgb, col.a * alpha);\n"
            "    } else if (type < 1.5) {\n" // 1: Anti-aliased Ring
            "        float d = length(vUV);\n"
            "        float f = vParams.z;\n"
            "        float outer = smoothstep(1.0, 1.0 - f, d);\n"
            "        float inner = smoothstep(1.0 - vParams.y - f, 1.0 - vParams.y, d);\n"
            "        float alpha = outer * inner;\n"
            "        if (alpha <= 0.0) discard;\n"
            "        FragColor = vec4(vColor.rgb, vColor.a * alpha);\n"
            "    } else if (type < 2.5) {\n" // 2: Rounded Rect (Exact Pixel SDF)
            "        vec2 halfSize = vec2(vParams.w, vParams.y);\n"
            "        float rad = vParams.z;\n"
            "        vec2 q = abs(vUV) - (halfSize - vec2(rad));\n"
            "        float d = length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - rad;\n"
            "        float alpha = smoothstep(1.0, -1.0, d);\n"
            "        if (alpha <= 0.0) discard;\n"
            "        float borderT = 1.5;\n"
            "        float isBorder = smoothstep(-borderT - 1.0, -borderT + 0.5, d);\n"
            "        vec3 col = mix(vColor.rgb, vBorderColor.rgb, isBorder);\n"
            "        FragColor = vec4(col, vColor.a * alpha);\n"
            "    } else if (type < 3.5) {\n" // 3: Font text
            "        float texA = texture(uFontTex, vUV).r;\n"
            "        if (texA < 0.02) discard;\n"
            "        FragColor = vec4(vColor.rgb, vColor.a * texA);\n"
            "    } else {\n" // 4: Plain
            "        FragColor = vColor;\n"
            "    }\n"
            "}\n";

        GLuint vs = CompileShader(GL_VERTEX_SHADER, vertShaderSrc);
        GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragShaderSrc);
        if (!vs || !fs) return;

        m_shaderProgram = glCreateProgram();
        glAttachShader(m_shaderProgram, vs);
        glAttachShader(m_shaderProgram, fs);
        glLinkProgram(m_shaderProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        m_uScreenSize = glGetUniformLocation(m_shaderProgram, "uScreenSize");
        m_uFontTex = glGetUniformLocation(m_shaderProgram, "uFontTex");

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);

        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, x));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, u));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, r));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, br));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)offsetof(UIVertex, type));

        glBindVertexArray(0);

        // Upload Font Atlas
        std::vector<uint8_t> fontPixels(FONT_RAW_SIZE);
        DecompressFontAtlas(fontPixels.data());

        glGenTextures(1, &m_fontTex);
        glBindTexture(GL_TEXTURE_2D, m_fontTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, FONT_ATLAS_WIDTH, FONT_ATLAS_HEIGHT, 0, GL_RED, GL_UNSIGNED_BYTE, fontPixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    GLuint CompileShader(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            LOGE("Touch Shader Compile Error: %s", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }

    // Geometry Helpers
    void DrawAADisc(float cx, float cy, float radius, Color col, Color border, float borderThick = 0.08f) {
        col=Neutral(col);border=Neutral(border);
        // Keep the outline a thin, constant pixel width as controls resize.
        borderThick=std::min(.12f,1.25f/std::max(radius,1.0f));
        float x0 = cx - radius, y0 = cy - radius;
        float x1 = cx + radius, y1 = cy + radius;
        float feather = 1.5f / std::max(radius, 1.0f);

        m_verts.push_back({ x0, y0, -1.0f, -1.0f, col.r, col.g, col.b, col.a, border.r, border.g, border.b, border.a, 0.0f, borderThick, feather, 0.0f });
        m_verts.push_back({ x1, y0,  1.0f, -1.0f, col.r, col.g, col.b, col.a, border.r, border.g, border.b, border.a, 0.0f, borderThick, feather, 0.0f });
        m_verts.push_back({ x0, y1, -1.0f,  1.0f, col.r, col.g, col.b, col.a, border.r, border.g, border.b, border.a, 0.0f, borderThick, feather, 0.0f });

        m_verts.push_back({ x1, y0,  1.0f, -1.0f, col.r, col.g, col.b, col.a, border.r, border.g, border.b, border.a, 0.0f, borderThick, feather, 0.0f });
        m_verts.push_back({ x1, y1,  1.0f,  1.0f, col.r, col.g, col.b, col.a, border.r, border.g, border.b, border.a, 0.0f, borderThick, feather, 0.0f });
        m_verts.push_back({ x0, y1, -1.0f,  1.0f, col.r, col.g, col.b, col.a, border.r, border.g, border.b, border.a, 0.0f, borderThick, feather, 0.0f });
    }

    void DrawAARing(float cx, float cy, float radius, float thickness, Color col) {
        col=Neutral(col);
        float x0 = cx - radius, y0 = cy - radius;
        float x1 = cx + radius, y1 = cy + radius;
        float feather = 1.5f / std::max(radius, 1.0f);
        float normThick = thickness / radius;

        m_verts.push_back({ x0, y0, -1.0f, -1.0f, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 1.0f, normThick, feather, 0.0f });
        m_verts.push_back({ x1, y0,  1.0f, -1.0f, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 1.0f, normThick, feather, 0.0f });
        m_verts.push_back({ x0, y1, -1.0f,  1.0f, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 1.0f, normThick, feather, 0.0f });

        m_verts.push_back({ x1, y0,  1.0f, -1.0f, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 1.0f, normThick, feather, 0.0f });
        m_verts.push_back({ x1, y1,  1.0f,  1.0f, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 1.0f, normThick, feather, 0.0f });
        m_verts.push_back({ x0, y1, -1.0f,  1.0f, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 1.0f, normThick, feather, 0.0f });
    }

    void DrawRoundedRect(float x, float y, float w, float h, float radius, Color fill, Color border) {
        m_labelBox={x,y,w,h};
        float halfW = w * 0.5f;
        float halfH = h * 0.5f;
        radius = std::min(radius, std::min(halfW, halfH));

        // u, v are local offsets in pixels [-halfW, halfW], [-halfH, halfH]
        // vParams: x = 2.0f, y = halfH, z = radius, w = halfW
        m_verts.push_back({ x,     y,     -halfW, -halfH, fill.r, fill.g, fill.b, fill.a, border.r, border.g, border.b, border.a, 2.0f, halfH, radius, halfW });
        m_verts.push_back({ x + w, y,      halfW, -halfH, fill.r, fill.g, fill.b, fill.a, border.r, border.g, border.b, border.a, 2.0f, halfH, radius, halfW });
        m_verts.push_back({ x,     y + h, -halfW,  halfH, fill.r, fill.g, fill.b, fill.a, border.r, border.g, border.b, border.a, 2.0f, halfH, radius, halfW });

        m_verts.push_back({ x + w, y,      halfW, -halfH, fill.r, fill.g, fill.b, fill.a, border.r, border.g, border.b, border.a, 2.0f, halfH, radius, halfW });
        m_verts.push_back({ x + w, y + h,  halfW,  halfH, fill.r, fill.g, fill.b, fill.a, border.r, border.g, border.b, border.a, 2.0f, halfH, radius, halfW });
        m_verts.push_back({ x,     y + h, -halfW,  halfH, fill.r, fill.g, fill.b, fill.a, border.r, border.g, border.b, border.a, 2.0f, halfH, radius, halfW });
    }

    void DrawRectSolid(float x, float y, float w, float h, Color col) {
        m_verts.push_back({ x,     y,     0, 0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 4.0f, 0, 0, 0 });
        m_verts.push_back({ x + w, y,     0, 0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 4.0f, 0, 0, 0 });
        m_verts.push_back({ x,     y + h, 0, 0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 4.0f, 0, 0, 0 });

        m_verts.push_back({ x + w, y,     0, 0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 4.0f, 0, 0, 0 });
        m_verts.push_back({ x + w, y + h, 0, 0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 4.0f, 0, 0, 0 });
        m_verts.push_back({ x,     y + h, 0, 0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 4.0f, 0, 0, 0 });
    }

    void DrawQuadTextured(float x, float y, float w, float h, float u0, float v0, float u1, float v1, Color col) {
        m_verts.push_back({ x,     y,     u0, v0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 3.0f, 0, 0, 0 });
        m_verts.push_back({ x + w, y,     u1, v0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 3.0f, 0, 0, 0 });
        m_verts.push_back({ x,     y + h, u0, v1, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 3.0f, 0, 0, 0 });

        m_verts.push_back({ x + w, y,     u1, v0, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 3.0f, 0, 0, 0 });
        m_verts.push_back({ x + w, y + h, u1, v1, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 3.0f, 0, 0, 0 });
        m_verts.push_back({ x,     y + h, u0, v1, col.r, col.g, col.b, col.a, 0, 0, 0, 0, 3.0f, 0, 0, 0 });
    }

    uint32_t DecodeUtf8(const char*& p) {
        uint8_t c = (uint8_t)*p++;
        if (c < 0x80) return c;
        if ((c & 0xE0) == 0xC0) {
            uint8_t c2 = (uint8_t)*p++;
            return ((c & 0x1F) << 6) | (c2 & 0x3F);
        }
        if ((c & 0xF0) == 0xE0) {
            uint8_t c2 = (uint8_t)*p++;
            uint8_t c3 = (uint8_t)*p++;
            return ((c & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
        }
        return c;
    }

    float MeasureText(const char* text, float size, bool strong=false) {
        if (!text) return 0;
        float width=0;const char* p=text;
        while(*p) {
            const auto* glyph=FindFontGlyph(DecodeUtf8(p),strong || size>=18);
            if(glyph)width+=glyph->advance;
        }
        return width*size/BASE_FONT_SIZE;
    }

    void DrawMenuIcon(int icon,float x,float y,float size,Color color) {
        const auto* glyph=FindFontGlyph(0xe100u+unsigned(icon==6?1:icon));
        if(glyph)DrawQuadTextured(x,y,size,size,glyph->u0,glyph->v0,glyph->u1,glyph->v1,color);
    }

    void DrawText(float x,float y,const char* text,float size,Color col,bool center=false) {
        if(!text || !*text)return;
        const bool strong=size>=18;
        float scale=size/BASE_FONT_SIZE;
        if(center) {
            float advance=0,minX=1e9f,maxX=-1e9f,minY=1e9f,maxY=-1e9f;
            const char* p=text;
            while(*p) {
                const auto* g=FindFontGlyph(DecodeUtf8(p),strong);
                if(!g)continue;
                if(g->w>0 && g->h>0) {
                    minX=std::min(minX,advance+g->offX);maxX=std::max(maxX,advance+g->offX+g->w);
                    minY=std::min(minY,g->offY);maxY=std::max(maxY,g->offY+g->h);
                }
                advance+=g->advance;
            }
            const bool insideChip=m_labelBox[3]>0 && m_labelBox[3]<=64 &&
                std::abs(x-(m_labelBox[0]+m_labelBox[2]*.5f))<1 &&
                y>=m_labelBox[1] && y<=m_labelBox[1]+m_labelBox[3];
            if(insideChip && maxX>minX && maxY>minY) {
                scale=std::min(scale,std::max(1.0f,m_labelBox[2]-20)/(maxX-minX));
                scale=std::min(scale,std::max(1.0f,m_labelBox[3]-12)/(maxY-minY));
                x-=(minX+maxX)*scale*.5f;
                y=m_labelBox[1]+m_labelBox[3]*.5f-(minY+maxY)*scale*.5f;
            } else x-=advance*scale*.5f;
        }
        float right=float(m_width)-12;
        if(m_menuOpen)right=float(m_width)*.95f-20;
        if(!center && m_labelBox[2]>=200 && m_labelBox[3]>64 &&
           x>=m_labelBox[0] && y>=m_labelBox[1] && y<m_labelBox[1]+m_labelBox[3])
            right=std::min(right,m_labelBox[0]+m_labelBox[2]-18);
        const char* p=text;float current=x;
        // One glyph pass on opaque menu panels. No duplicated shadow geometry.
        while(*p) {
            const auto* g=FindFontGlyph(DecodeUtf8(p),strong);
            if(!g)continue;
            const float gx=current+g->offX*scale,gy=y+g->offY*scale;
            const float gw=g->w*scale,gh=g->h*scale;
            if(gx+gw>right+.5f)break;
            if(g->w>0 && g->h>0)DrawQuadTextured(gx,gy,gw,gh,g->u0,g->v0,g->u1,g->v1,col);
            current+=g->advance*scale;
        }
    }

    void DrawButtonLabel(float cx, float cy, const char* label, float size, Color col, float maxWidth=0) {
        if (!label) return;
        if(maxWidth>0) {
            const float measured=MeasureText(label,size,true);
            if(measured>maxWidth)size*=maxWidth/measured;
        }
        float scale = size / BASE_FONT_SIZE;
        float totalW = MeasureText(label, size);

        float minOffY = 9999.0f;
        float maxBottomY = -9999.0f;
        const char* pCheck = label;
        while (*pCheck) {
            uint32_t cp = DecodeUtf8(pCheck);
            const auto* g = FindFontGlyph(cp,true);
            if (g && g->w > 0 && g->h > 0) {
                minOffY = std::min(minOffY, g->offY);
                maxBottomY = std::max(maxBottomY, g->offY + g->h);
            }
        }
        if (minOffY > 9000.0f) {
            minOffY = 0.0f;
            maxBottomY = BASE_FONT_SIZE;
        }

        float visualCenterY = (minOffY + maxBottomY) * 0.5f * scale;
        float startX = cx - totalW * 0.5f;
        float startY = cy - visualCenterY;

        // Shadow pass for crisp contrast
        float shadowOffset = .65f;
        Color shadowCol = { 0.0f, 0.0f, 0.0f, col.a * 0.30f };
        const char* p = label;
        float curX = startX;
        while (*p) {
            uint32_t cp = DecodeUtf8(p);
            const auto* g = FindFontGlyph(cp,true);
            if (g && g->w > 0 && g->h > 0) {
                float gx = curX + g->offX * scale + shadowOffset;
                float gy = startY + g->offY * scale + shadowOffset;
                float gw = g->w * scale;
                float gh = g->h * scale;
                DrawQuadTextured(gx, gy, gw, gh, g->u0, g->v0, g->u1, g->v1, shadowCol);
            }
            curX += (g ? g->advance : (BASE_FONT_SIZE * 0.5f)) * scale;
        }

        // Foreground glyph pass
        p = label;
        curX = startX;
        while (*p) {
            uint32_t cp = DecodeUtf8(p);
            const auto* g = FindFontGlyph(cp,true);
            if (g && g->w > 0 && g->h > 0) {
                float gx = curX + g->offX * scale;
                float gy = startY + g->offY * scale;
                float gw = g->w * scale;
                float gh = g->h * scale;
                DrawQuadTextured(gx, gy, gw, gh, g->u0, g->v0, g->u1, g->v1, col);
            }
            curX += (g ? g->advance : (BASE_FONT_SIZE * 0.5f)) * scale;
        }
    }

    // -------------------------------------------------------------
    // Screen Rendering Modes
    // -------------------------------------------------------------
    void RenderFpsHud() {
        if (!m_showFpsHud) return;

        float temp = GetDeviceTemperature();
        char hudStr[48];
        if (temp > 0.0f) {
            std::snprintf(hudStr, sizeof(hudStr), "%u FPS | %.1f°C", unsigned(m_liveFps + 0.5), temp);
        } else {
            std::snprintf(hudStr, sizeof(hudStr), "%u FPS", unsigned(m_liveFps + 0.5));
        }

        float pillW = (temp > 0.0f) ? (m_unit * 0.22f) : (m_unit * 0.16f);
        float pillH = m_unit * 0.052f;
        float pillX = m_unit * 0.025f;
        float pillY = float(m_height) - pillH - m_unit * 0.025f;

        DrawRoundedRect(pillX, pillY, pillW, pillH, pillH * 0.5f,
                        { 0.035f, 0.125f, 0.176f, 0.88f }, COL_CYAN);
        DrawText(pillX + pillW * 0.5f, pillY + pillH * 0.20f, hudStr, pillH * 0.52f, COL_CYAN, true);
    }

    void RenderGameplayOverlay() {
#ifdef __ANDROID__
        if (AndroidKeyboard_IsImeShowing()) return;
#endif
        float op = std::clamp(m_buttonOpacity, 0.10f, 1.0f);

        // One thin outline and a plain thumb disc, like the MH4 touch overlay.
        DrawAARing(m_leftStickCenter.x,m_leftStickCenter.y,m_leftStickRadius,1.25f,{.9f,.9f,.9f,.55f*op});
        const bool leftHeld=m_leftFingerId.has_value();
        DrawAADisc(m_leftStickPos.x,m_leftStickPos.y,m_leftStickKnobRadius,
            leftHeld?Color{.27f,.27f,.27f,.55f*op}:Color{.12f,.12f,.12f,.30f*op},
            {.95f,.95f,.95f,(leftHeld?.9f:.6f)*op});

        // 2. Free Right Stick (Camera Look)
        if (m_rightStickActive) {
            DrawAARing(m_rightFloatingOrigin.x, m_rightFloatingOrigin.y, m_rightStickRadius, 2.5f, { 0.25f, 0.65f, 0.85f, 0.35f * op });
            DrawAADisc(m_rightStickPos.x, m_rightStickPos.y, m_rightStickKnobRadius * 0.75f,
                       { 0.15f, 0.45f, 0.60f, 0.45f * op }, { 0.40f, 0.80f, 1.00f, 0.70f * op }, 0.10f);
        }

        // The component keys light up with the combo that is holding them.
        uint16_t heldMask = 0;
        for (const auto& b : m_buttons) if (b.isPressed && IsButtonActive(b)) heldMask |= b.mask;
        // 3. Gameplay Buttons (ABXY, Shoulders, D-pad, Menu, Macros)
        for (const auto& btn : m_buttons) {
            if (!IsButtonActive(btn)) continue;
            bool singleButton = btn.mask && (btn.mask & (btn.mask-1))==0;
            bool pressed = btn.isPressed || (singleButton && (heldMask & btn.mask) == btn.mask);
            Color base = pressed ? Color{ .32f,.32f,.32f,.88f } : Color{ .08f,.08f,.08f,.48f };
            Color border = pressed ? Color{ 1,1,1,1 } : Color{ .85f,.85f,.85f,.72f };
            base.a *= op;
            border.a *= op;

            DrawAADisc(btn.pos.x, btn.pos.y, btn.radius * (pressed ? 0.94f : 1.0f), base, border, 0.09f);

            // A subtle fill change and shrink show presses without a bulky outer glow.
            const char* label = btn.label.c_str();
            if (btn.id == "SPEED") {
                label = MH3GameSpeed::Percent() == 125 ? "1.25x" : MH3GameSpeed::Percent() == 150 ? "1.5x" : "1x";
            }
            float textSize = std::min(btn.radius*0.80f, btn.radius*1.64f / std::max(1.0f,float(std::strlen(label))*0.58f));
            Color textCol = COL_WHITE;
            if (btn.id == "SPEED" && !MH3GameSpeed::Available()) textCol = COL_MUTED;
            else if (btn.id == "SPEED" && MH3GameSpeed::Percent() != 100) textCol = COL_CYAN;
            textCol.a *= op;
            DrawButtonLabel(btn.pos.x, btn.pos.y, label, textSize, textCol,btn.radius*1.48f);
        }

        // 4. Gear / Menu Button
        DrawAADisc(m_gearPos.x, m_gearPos.y, m_gearRadius,
                   { 0.08f, 0.40f, 0.48f, 0.75f * op }, { 0.25f, 0.90f, 0.95f, 0.95f * op }, 0.10f);
        Color gearText = COL_WHITE;
        gearText.a *= op;
        DrawButtonLabel(m_gearPos.x, m_gearPos.y, "⚙", m_gearRadius * 1.05f, gearText);

        // 5. FPS HUD Pill (if enabled)
        RenderFpsHud();
    }

    void RenderSettingsMenu() {
        // Semi-transparent Scrim
        DrawRectSolid(0, 0, float(m_width), float(m_height), { 0.0f, 0.0f, 0.0f, 0.72f });

        // Container Window (Laguna de Moga dark ocean aesthetic)
        float padX = float(m_width) * 0.05f;
        float padY = float(m_height) * 0.04f;
        float panW = float(m_width) - padX * 2.0f;
        float panH = float(m_height) - padY * 2.0f;

        DrawRoundedRect(padX, padY, panW, panH, 16.0f, COL_BG_DARK, COL_PANEL_BORDER);

        // Header Title & Subtitle (Larger readable fonts)
        const char* titles[] = { "Graphics", "System", "Saves", "Controls", "Online", "Combos", "Extras" };
        const char* subtitles[] = {
            "Display, resolution and presentation",
            "Game language and restart",
            "Saved progress, backups and transfers",
            "Touch layout and physical controllers",
            "Local and community multiplayer",
            "Your saved combinations and touch buttons",
            "Optional game tools and local diagnostics"
        };
        DrawMenuIcon(m_menuTab,padX+24,padY+18,25,COL_CYAN);
        DrawText(padX + 60.0f, padY + 16.0f, m_menuTab==5?"Custom combos":titles[m_menuTab], 28.0f, COL_WHITE);
        DrawClippedText(padX + 24.0f,padY + 48.0f,subtitles[m_menuTab],15,panW-340,COL_MUTED);

        // Header Right: FPS Badge & Close Button
        char fpsStr[32];
        std::snprintf(fpsStr, sizeof(fpsStr), "FPS  %u", unsigned(m_liveFps + 0.5));
        float fpsW = 100.0f, fpsH = 38.0f;
        float fpsX = padX + panW - 258.0f;
        float fpsY = padY + 18.0f;
        DrawRoundedRect(fpsX, fpsY, fpsW, fpsH, fpsH * 0.5f, COL_INACTIVE_CHIP, COL_CYAN);
        DrawText(fpsX + fpsW * 0.5f, fpsY + 10.0f, fpsStr, 15.0f, COL_CYAN, true);

        // Close Button [✕]
        float closeX = padX + panW - 144.0f;
        float closeY = padY + 18.0f;
        float closeS = 38.0f;
        DrawRoundedRect(closeX,closeY,126,closeS,8,COL_CYAN,COL_CYAN);
        DrawText(closeX+63,closeY+11,"Resume",18,COL_BG_DARK,true);

        // Horizontal divider line
        DrawRectSolid(padX + 20.0f, padY + 76.0f, panW - 40.0f, 1.0f, COL_PANEL_BORDER);

        // Tab Navigation Bar
        float tabY = padY + 86.0f;
        float tabW = (panW - 40.0f - 60.0f) / 7.0f;
        float tabH = 44.0f;
        for (int i = 0; i < 7; ++i) {
            float tx = padX + 20.0f + float(i) * (tabW + 10.0f);
            bool active = (m_menuTab == i);
            DrawRoundedRect(tx, tabY, tabW, tabH, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                            active ? COL_CYAN : COL_CHIP_BORDER);
            const float textSize=std::min(18.0f,18.0f*(tabW-44)/std::max(1.0f,MeasureText(titles[i],18)));
            const float textW=MeasureText(titles[i],textSize),groupW=textW+24;
            const float groupX=tx+(tabW-groupW)*.5f;
            DrawMenuIcon(i,groupX,tabY+13,18,active?COL_CYAN:COL_MUTED);
            DrawText(groupX+24,tabY+15,titles[i],textSize,COL_WHITE);
        }

        // Tab Content
        float contentY = tabY + tabH + 16.0f;
        float contentW = panW - 40.0f;

        if (m_menuTab == 0) RenderTabGraphics(padX + 20.0f, contentY, contentW);
        else if (m_menuTab == 1) RenderTabSystem(padX + 20.0f, contentY, contentW);
        else if (m_menuTab == 2) RenderTabSaves(padX + 20.0f, contentY, contentW);
        else if (m_menuTab == 3) RenderTabControls(padX + 20.0f, contentY, contentW);
        else if (m_menuTab == 4) RenderTabOnline(padX + 20.0f, contentY, contentW);
        else if (m_menuTab == 5) RenderTabCombos(padX + 20.0f, contentY, contentW);
        else if (m_menuTab == 6) RenderTabExtras(padX + 20.0f, contentY, contentW);
    }

    void RenderTabGraphics(float x, float y, float w) {
        // Card 1: Internal Resolution
        float c1H = 108.0f;
        DrawRoundedRect(x, y, w, c1H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, y + 14.0f, "Internal resolution", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, y + 36.0f, "Lower resolution reduces GPU load. Balanced is the Lite default.", 15.0f, COL_MUTED);

        constexpr auto resLabels = LiteProfile::ResolutionLabels;
        float chipW = (w - 36.0f - 25.0f) / 6.0f;
        float chipH = 42.0f;
        float chipY = y + 54.0f;
        for (int i = 0; i < 6; ++i) {
            float cx = x + 18.0f + float(i) * (chipW + 5.0f);
            bool active = (m_resolutionIndex == i);
            DrawRoundedRect(cx, chipY, chipW, chipH, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                            active ? COL_CYAN : COL_CHIP_BORDER);
            DrawText(cx + chipW * 0.5f, chipY + 12.0f, resLabels[i], 14.0f,
                     active ? COL_ACTIVE_TEXT : COL_WHITE, true);
        }

        // Card 2: Pantalla Completa (Fix de aspecto) & Aspect de aspecto
        float c2Y = y + c1H + 12.0f;
        float c2H = 114.0f;
        DrawRoundedRect(x, c2Y, w, c2H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);

        float colW = (w - 36.0f - 16.0f) * 0.5f;

        // Left side: Fullscreen & 16:9 Fix
        DrawText(x + 18.0f, c2Y + 14.0f, "Fullscreen", 20.0f, COL_WHITE);
        DrawClippedText(x+18,c2Y+36,"Keep the picture proportional in fullscreen.",15,(w-52)*.5f-8,COL_MUTED);

        float fsBtnY = c2Y + 54.0f;
        float fsBtnW = colW;
        bool fsActive = m_fullscreen;
        DrawRoundedRect(x + 18.0f, fsBtnY, fsBtnW, chipH, 8.0f,
                        fsActive ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        fsActive ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(x + 18.0f + fsBtnW * 0.5f, fsBtnY + 12.0f,
                 fsActive ? "Fullscreen: ON" : "Enable fullscreen", 14.5f,
                 fsActive ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Right side: Aspect de aspecto
        float rightColX = x + 18.0f + colW + 16.0f;
        DrawText(rightColX, c2Y + 14.0f, "Aspect ratio", 20.0f, COL_WHITE);
        DrawClippedText(rightColX,c2Y+36,"Screen shape and presentation",15,(w-52)*.5f-8,COL_MUTED);

        const char* aspectLabels[] = { "16:9 Widescreen", "4:3 Classic", "Stretch screen" };
        float aChipW = (colW - 10.0f) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            float ax = rightColX + float(i) * (aChipW + 5.0f);
            bool active = (m_aspectIndex == i);
            DrawRoundedRect(ax, fsBtnY, aChipW, chipH, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                            active ? COL_CYAN : COL_CHIP_BORDER);
            DrawText(ax + aChipW * 0.5f, fsBtnY + 12.0f, aspectLabels[i], 13.5f,
                     active ? COL_ACTIVE_TEXT : COL_WHITE, true);
        }

        // Card 3: Límite de fotogramas (FPS: 30, 60, 90, 120) & V-Sync / HUD
        float c3Y = c2Y + c2H + 12.0f;
        float c3H = 114.0f;
        DrawRoundedRect(x, c3Y, w, c3H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);

        float fpsColW = (w - 36.0f - 16.0f) * 0.52f;
        bool inCity = IsOnlinePlay();
        if (inCity) {
            DrawText(x + 18.0f, c3Y + 14.0f, "Frame limit: 30 FPS [ONLINE]", 18.0f, COL_CYAN);
            DrawClippedText(x+18,c3Y+36,"Online play keeps the 30 FPS sync cap.",15,fpsColW-8,COL_CAUTION);
        } else {
            DrawText(x + 18.0f, c3Y + 14.0f, "Frame rate limit (FPS)", 20.0f, COL_WHITE);
            DrawClippedText(x+18,c3Y+36,"Village / solo quests. Actual FPS varies by device.",15,fpsColW-8,COL_MUTED);
        }

        const char* fpsLabels[] = { "30 FPS", "60 FPS", "90 FPS", "120 FPS" };
        const int fpsValues[] = { 30, 60, 90, 120 };
        float fChipW = (fpsColW - 15.0f) / 4.0f;
        float fChipY = c3Y + 54.0f;
        for (int i = 0; i < 4; ++i) {
            float fx = x + 18.0f + float(i) * (fChipW + 5.0f);
            bool active = inCity ? (fpsValues[i] == 30) : (m_fpsLimit == fpsValues[i]);
            DrawRoundedRect(fx, fChipY, fChipW, chipH, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                            active ? COL_CYAN : COL_CHIP_BORDER);
            DrawText(fx + fChipW * 0.5f, fChipY + 12.0f, fpsLabels[i], 14.5f,
                     active ? COL_ACTIVE_TEXT : COL_WHITE, true);
        }

        // Right side: Sincronización y Contador
        float syncColX = x + 18.0f + fpsColW + 16.0f;
        float syncColW = (w - 36.0f - 16.0f) * 0.48f;
        DrawText(syncColX, c3Y + 14.0f, "Sync and statistics", 20.0f, COL_WHITE);
        DrawClippedText(syncColX,c3Y+36,"V-Sync and the FPS counter.",15,syncColW-8,COL_MUTED);

        float sChipW = (syncColW - 10.0f) * 0.5f;
        // VSync chip
        DrawRoundedRect(syncColX, fChipY, sChipW, chipH, 8.0f,
                        m_vsync ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        m_vsync ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(syncColX + sChipW * 0.5f, fChipY + 12.0f, m_vsync ? "V-Sync: ON" : "V-Sync: OFF", 13.0f,
                 m_vsync ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // HUD FPS chip
        float hudX = syncColX + sChipW + 10.0f;
        DrawRoundedRect(hudX, fChipY, sChipW, chipH, 8.0f,
                        m_showFpsHud ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        m_showFpsHud ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(hudX + sChipW * 0.5f, fChipY + 12.0f, m_showFpsHud ? "FPS counter: ON" : "FPS counter: OFF", 13.0f,
                 m_showFpsHud ? COL_ACTIVE_TEXT : COL_WHITE, true);

#ifdef MH3_PC_TOUCH_UI
        const float rendererY=c3Y+c3H+12;
        DrawRoundedRect(x,rendererY,w,108,12,COL_PANEL_CARD,COL_PANEL_BORDER);
        DrawText(x+18,rendererY+14,"PC graphics renderer",20,COL_WHITE);
        DrawClippedText(x+18,rendererY+36,m_rendererIndex!=m_startupRendererIndex
            ?"Saved. Save inside Tri, then use System > Restart now."
            :"Renderer changes apply after restarting the game.",15,w-36,COL_MUTED);
        const float rendererW=(w-46)*.5f;
        const char* rendererLabels[]={"Vulkan (ANGLE)","OpenGL ES"};
        for(int i=0;i<2;++i){
            const float rx=x+18+i*(rendererW+10);
            const bool active=m_rendererIndex==i;
            DrawRoundedRect(rx,rendererY+54,rendererW,42,8,active?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,active?COL_CYAN:COL_CHIP_BORDER);
            DrawText(rx+rendererW*.5f,rendererY+66,rendererLabels[i],15,COL_WHITE,true);
        }
#else
        // Card 4: Reemplazo de Texturas HD (Dolphin Texture Pack)
        float c4Y = c3Y + c3H + 12.0f;
        float c4H = 108.0f;
        DrawRoundedRect(x, c4Y, w, c4H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);

        bool texEnabled = aurora::gfx::texture_replacement::is_enabled();
        size_t texCount = aurora::gfx::texture_replacement::get_replacement_count();
        char texSub[140];
        std::snprintf(texSub, sizeof(texSub), "Textures: %zu | Folder: MH3_Low_Spec/textures/", texCount);

        DrawText(x + 18.0f, c4Y + 14.0f, "Custom HD textures (Dolphin packs)", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, c4Y + 36.0f, texSub, 13.0f, (texCount > 0) ? COL_CYAN : COL_MUTED);

        float tBtnW = (w - 36.0f - 15.0f) * 0.50f;
        float tBtnY = c4Y + 54.0f;

        // Toggle button
        DrawRoundedRect(x + 18.0f, tBtnY, tBtnW, chipH, 8.0f,
                        texEnabled ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        texEnabled ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(x + 18.0f + tBtnW * 0.5f, tBtnY + 12.0f,
                 texEnabled ? "HD textures: ON" : "HD textures: OFF", 14.5f,
                 texEnabled ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Reload button
        float reloadX = x + 18.0f + tBtnW + 15.0f;
        DrawRoundedRect(reloadX, tBtnY, tBtnW, chipH, 8.0f,
                        COL_ACTIVE_CHIP, COL_ACTIVE_CHIP);
        DrawText(reloadX + tBtnW * 0.5f, tBtnY + 12.0f, "Reload textures", 14.5f, COL_WHITE, true);
#endif
    }

    void RenderTabExtras(float x, float y, float w) {
        const char* labels[] = {"Offline cutscene skipping", "Android keyboard", "Physics diagnostics"};
        const char* details[] = {"Press A or + to skip. Online play keeps its normal cutscenes.",
            "Use your phone keyboard for character names and chat.",
            "Local troubleshooting only. Up to 2 MB. Turns off when you close the app."};
        const bool values[] = {RowellFeatures::cutsceneSkip.load(), RowellFeatures::androidKeyboard.load(), RowellFeatures::physicsLog.load()};
        int row = 0;
        for (int i=0;i<3;++i) {
#ifndef __ANDROID__
            if(i==1)continue;
#endif
            const float top=y+row++*128.0f;
            DrawRoundedRect(x,top,w,114,12,COL_PANEL_CARD,COL_PANEL_BORDER);
            DrawClippedText(x+18,top+16,labels[i],20,w-170,COL_WHITE);
            DrawClippedText(x+18,top+57,details[i],14,w-36,COL_MUTED);
            DrawRoundedRect(x+w-124,top+12,106,36,8,values[i]?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(x+w-71,top+21,values[i]?"ON":"OFF",15,COL_WHITE,true);
        }
        DrawClippedText(x+18,y+row*128+6,"Changes apply immediately. Saved progress is separate from these options.",14,w-36,COL_MUTED);
        if(!m_featureStatus.empty())DrawClippedText(x+18,y+row*128+34,m_featureStatus.c_str(),14,w-36,COL_MUTED);
    }

    void RenderTabSystem(float x, float y, float w) {
        // Card 1: Game Language
        float c1H = 130.0f;
        DrawRoundedRect(x, y, w, c1H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, y + 14.0f, "Game language", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, y + 36.0f, "Changes the game text only. Restart to apply. App settings remain in English.", 15.0f, COL_MUTED);

        const char* langLabels[] = { "Español", "English", "Français", "Deutsch", "Italiano" };
        float chipW = (w - 36.0f - 20.0f) / 5.0f;
        float chipH = 44.0f;
        float chipY = y + 60.0f;
        for (int i = 0; i < 5; ++i) {
            float lx = x + 18.0f + float(i) * (chipW + 5.0f);
            bool active = (m_langChoice == i);
            DrawRoundedRect(lx, chipY, chipW, chipH, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                            active ? COL_CYAN : COL_CHIP_BORDER);
            DrawText(lx + chipW * 0.5f, chipY + 13.0f, langLabels[i], 15.5f,
                     active ? COL_ACTIVE_TEXT : COL_WHITE, true);
        }

        // Active Language status text
        float statusY = y + c1H + 12.0f;
        std::string statStr = std::string("Selected game language: ") + langLabels[std::clamp(m_langChoice, 0, 4)];
        DrawText(x + 18.0f, statusY, statStr.c_str(), 15.0f, COL_MUTED);

        // Card 2: Restart Action Card
        float c2Y = statusY + 28.0f;
        float c2H = 88.0f;
        DrawRoundedRect(x, c2Y, w, c2H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, c2Y + 16.0f, "Restart game", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, c2Y + 42.0f, "Apply pending game language and startup settings.", 15.0f, COL_MUTED);

        float rBtnW = 200.0f;
        float rBtnX = x + w - rBtnW - 18.0f;
        float rBtnY = c2Y + 22.0f;
        DrawRoundedRect(rBtnX, rBtnY, rBtnW, 44.0f, 8.0f, COL_ACTIVE_CHIP, COL_CYAN);
        DrawText(rBtnX + rBtnW * 0.5f, rBtnY + 13.0f, "Restart now", 16.0f, COL_WHITE, true);

        const float speedY = y + 270.0f;
        DrawRoundedRect(x, speedY, w, 142.0f, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, speedY + 14.0f, "Game speed (single-player)", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, speedY + 38.0f,
                 MH3GameSpeed::Available() ? "Change speed here or tap the 1x button while playing."
                                          : "Available in Moga Village and solo quests. City and Arena stay at 1x.", 15.0f, COL_MUTED);
        const char* speedLabels[] = {"1x", "1.25x", "1.5x"};
        const int speedValues[] = {100, 125, 150};
        const float speedW = (w - 56.0f) / 3.0f;
        for (int i = 0; i < 3; ++i) {
            const float sx = x + 18.0f + i * (speedW + 10.0f);
            const bool active = MH3GameSpeed::Percent() == speedValues[i];
            const bool enabled = i == 0 || MH3GameSpeed::Available();
            DrawRoundedRect(sx, speedY + 62.0f, speedW, 40.0f, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP, COL_CHIP_BORDER);
            DrawText(sx + speedW * 0.5f, speedY + 73.0f, speedLabels[i], 16.0f,
                     active ? COL_ACTIVE_TEXT : enabled ? COL_WHITE : COL_MUTED, true);
        }
        DrawText(x + 18.0f, speedY + 115.0f,
                 "Experimental: audio and movies keep their normal speed. Resets to 1x outside solo play.", 15.0f, COL_MUTED);
#ifdef MH3_PC_TOUCH_UI
        const float foldersY=y+420,folderW=(w-46)*.5f;
        DrawRoundedRect(x+18,foldersY,folderW,44,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+18+folderW*.5f,foldersY+12,"Open data folder",15,COL_WHITE,true);
        DrawRoundedRect(x+28+folderW,foldersY,folderW,44,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+28+folderW*1.5f,foldersY+12,"Open app folder",15,COL_WHITE,true);
        DrawClippedText(x+18,foldersY+55,"Data folder contains your saved progress, backups and controls.",14,w-36,COL_MUTED);
#endif
    }

    void RenderTabSaves(float x, float y, float w) {
        float c1H = 266.0f;
        DrawRoundedRect(x, y, w, c1H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, y + 14.0f, "Saved progress", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, y + 38.0f, "Save inside Tri first. Backup and Export copy that saved progress.", 15.0f, COL_MUTED);

        // Action Buttons
        float btnW = (w - 36.0f - 20.0f) / 3.0f;
        float btnH = 50.0f;
        float btnY = y + 70.0f;

        // 1. Export
        float b1X = x + 18.0f;
        DrawRoundedRect(b1X, btnY, btnW, btnH, 8.0f, COL_ACTIVE_CHIP, COL_CYAN);
        DrawText(b1X + btnW * 0.5f, btnY + 15.0f, "Export save", 15.0f, COL_WHITE, true);

        // 2. Import
        float b2X = b1X + btnW + 10.0f;
        DrawRoundedRect(b2X, btnY, btnW, btnH, 8.0f, COL_ACTIVE_CHIP, COL_ACTIVE_CHIP);
        DrawText(b2X + btnW * 0.5f, btnY + 15.0f, "Restore / import", 15.0f, COL_WHITE, true);

        // 3. Quick Backup
        float b3X = b2X + btnW + 10.0f;
        DrawRoundedRect(b3X, btnY, btnW, btnH, 8.0f, COL_ACTIVE_CHIP, COL_ACTIVE_CHIP);
        DrawText(b3X + btnW * 0.5f, btnY + 15.0f, "Create backup", 15.0f, COL_WHITE, true);
        DrawText(b1X + btnW * 0.5f, y + 126.0f, "Copy for another device.", 15.0f, COL_MUTED, true);
        DrawText(b2X + btnW * 0.5f, y + 126.0f, "Choose a saved copy to restore.", 15.0f, COL_MUTED, true);
        DrawText(b3X + btnW * 0.5f, y + 126.0f, "Keep the newest 3 recovery copies.", 15.0f, COL_MUTED, true);

        // Status Banner
        float bannerY = y + 154.0f;
        float bannerH = 68.0f;
        DrawRoundedRect(x + 18.0f, bannerY, w - 36.0f, bannerH, 8.0f, COL_INACTIVE_CHIP, COL_PANEL_BORDER);
        DrawText(x + 30.0f, bannerY + 12.0f, "Status:", 14.0f, COL_CYAN);
        DrawText(x + 30.0f, bannerY + 36.0f, m_saveStatusMsg.c_str(), 14.0f, COL_WHITE);
        const std::string last = "Backup created: " + m_lastBackupTime;
        DrawText(x + 18.0f, y + 236.0f, last.c_str(), 15.0f, COL_MUTED);
    }

    void RenderSaveConfirmation() {
        DrawRectSolid(0, 0, float(m_width), float(m_height), {0, 0, 0, 0.70f});
        const float w = std::min(560.0f, float(m_width) - 48.0f);
        const float x = (m_width - w) * 0.5f, y = (m_height - 220.0f) * 0.5f;
        DrawRoundedRect(x, y, w, 220.0f, 16.0f, COL_BG_DARK, COL_CYAN);
        const bool importing = m_pendingSaveAction == SaveAction::Import;
        const bool exporting = m_pendingSaveAction == SaveAction::Export;
        DrawText(x + 24.0f, y + 24.0f,
                 importing ? "Restore this save and restart?" : exporting ? "Export this save?" : "Create a backup?",
                 25.0f, COL_WHITE);
        DrawText(x + 24.0f, y + 68.0f,
                 importing ? "Replaces your saved progress with your selected copy."
                           : exporting ? "Copies your last in-game save to the exports folder."
                                       : "Copies the progress you last saved inside Tri.", 15.0f, COL_MUTED);
        DrawText(x + 24.0f, y + 94.0f,
                 importing ? "Backs up saved progress first. Unsaved play is lost."
                           : "Your current save stays in place.", 15.0f, COL_MUTED);
        if (importing) {
            DrawClippedText(x+24,y+120,"From: "+m_pendingImportLabel,13,w-48,COL_CYAN);
        } else if(!exporting) DrawText(x+24,y+120,"Keeps up to 3 backups, including automatic safety copies.",13,COL_CYAN);
        const float bw = (w - 64.0f) * 0.5f;
        DrawRoundedRect(x + 24.0f, y + 148.0f, bw, 48.0f, 8.0f, COL_INACTIVE_CHIP, COL_CHIP_BORDER);
        DrawText(x + 24.0f + bw * 0.5f, y + 162.0f, "No", 18.0f, COL_WHITE, true);
        DrawRoundedRect(x + 40.0f + bw, y + 148.0f, bw, 48.0f, 8.0f, COL_ACTIVE_CHIP, COL_CYAN);
        DrawText(x + 40.0f + bw * 1.5f, y + 162.0f, importing?"Yes, restore":"Yes", 18.0f, COL_ACTIVE_TEXT, true);
    }

    void DrawClippedText(float x,float y,std::string text,float size,float width,Color color) {
        if(width<MeasureText("...",size)+4)return;
        if(MeasureText(text.c_str(),size)>width-4) {
            const float limit=width-4-MeasureText("...",size),scale=size/BASE_FONT_SIZE;
            const char* begin=text.c_str();const char* cursor=begin;
            size_t kept=0;float used=0;
            while(*cursor) {
                const auto* glyph=FindFontGlyph(DecodeUtf8(cursor),size>=18);
                const float advance=glyph?glyph->advance*scale:0;
                if(used+advance>limit)break;
                used+=advance;kept=size_t(cursor-begin);
            }
            text.resize(kept);text+="...";
        }
        DrawText(x,y,text.c_str(),size,color);
    }
    void OpenSaveBrowser(bool backups=true) {
        const char* user=std::getenv("MH3_USER_DIR");
        m_saveCopies=mh3_save_manager::ListSaveCopies(user&&*user?user:"/storage/emulated/0/MH3_Low_Spec",backups);
        m_browseBackups=backups;m_savePage=0;m_selectedSave=-1;m_saveBrowserOpen=true;
        ResetInput();
    }
    void RenderSaveBrowser() {
        const float w=std::min(900.0f,float(m_width)-64),x=(m_width-w)*.5f,y=(m_height-520)*.5f;
        DrawRectSolid(0,0,float(m_width),float(m_height),{0,0,0,.72f});
        DrawRoundedRect(x,y,w,520,16,COL_BG_DARK,COL_CYAN);
        DrawText(x+24,y+20,"Choose saved progress",24,COL_WHITE);
        DrawText(x+24,y+53,"Select a copy, then review it before restoring. Times show when copies were created.", 15.0f, COL_MUTED);
        const float tabW=(w-60)*.5f;
        for(int i=0;i<2;++i) {
            const bool active=m_browseBackups==(i==0);
            DrawRoundedRect(x+24+i*(tabW+12),y+80,tabW,34,8,active?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(x+24+i*(tabW+12)+tabW*.5f,y+89,i==0?"My backups (up to 3)":"Other saves (imports / exports)",14,active?COL_ACTIVE_TEXT:COL_WHITE,true);
        }
        if(m_saveCopies.empty()) {
            DrawText(x+24,y+160,m_browseBackups?"No backups yet. Save inside Tri, then choose Create backup.":"No compatible save folders found.",16,COL_WHITE);
            if(!m_browseBackups)DrawText(x+24,y+190,"Put a complete exported save folder in MH3_Low_Spec/imports.", 15.0f, COL_MUTED);
        }
        for(int row=0;row<4;++row) {
            const int index=m_savePage*4+row;if(index>=int(m_saveCopies.size()))break;
            const auto& copy=m_saveCopies[index];const bool selected=index==m_selectedSave;
            const float ry=y+132+row*64;
            DrawRoundedRect(x+24,ry,w-48,56,8,selected?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            const std::string title=copy.created=="Creation time not recorded"?copy.folder.filename().string():copy.created;
            DrawClippedText(x+38,ry+9,title,17,w-76,selected?COL_ACTIVE_TEXT:COL_WHITE);
            const std::string sub=m_browseBackups?(index==0?"Backup - newest":index==1?"Backup - previous copy":
                index==2 && m_saveCopies.size()==3?"Backup - oldest retained":"Backup - older copy"):
                copy.kind+" / "+copy.folder.filename().string();
            DrawClippedText(x+38,ry+34,sub,12.5f,w-76,selected?COL_ACTIVE_TEXT:COL_MUTED);
        }
        RenderPager(x,y+402,w,m_savePage,int((m_saveCopies.size()+3)/4));
        DrawRoundedRect(x+24,y+456,160,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+104,y+468,"Cancel",16,COL_WHITE,true);
        const bool ready=m_selectedSave>=0;
        DrawRoundedRect(x+w-224,y+456,200,42,8,ready?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-124,y+468,"Review selected copy",15,ready?COL_ACTIVE_TEXT:COL_MUTED,true);
    }
    void RenderPager(float x,float y,float w,int page,int pages) {
        DrawRoundedRect(x+24,y,100,32,7,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+74,y+8,"Previous",13,page>0?COL_WHITE:COL_MUTED,true);
        DrawRoundedRect(x+w-124,y,100,32,7,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-74,y+8,"Next",13,page+1<pages?COL_WHITE:COL_MUTED,true);
        char text[64];std::snprintf(text,sizeof(text),"Page %d / %d",page+1,std::max(1,pages));
        DrawText(x+w*.5f,y+8,text, 15.0f, COL_MUTED,true);
    }
    void HandleSaveBrowserTouch(float px,float py) {
        const float w=std::min(900.0f,float(m_width)-64),x=(m_width-w)*.5f,y=(m_height-520)*.5f;
        if(px<x+24 || px>x+w-24)return;
        if(py>=y+80 && py<=y+114) {
            const float tabW=(w-60)*.5f;
            if(px<=x+24+tabW)OpenSaveBrowser(true);
            else if(px>=x+36+tabW)OpenSaveBrowser(false);
            return;
        }
        for(int row=0;row<4;++row)if(py>=y+132+row*64 && py<=y+188+row*64) {
            const int index=m_savePage*4+row;if(index<int(m_saveCopies.size()))m_selectedSave=index;return;
        }
        if(py>=y+402 && py<=y+434) {
            if(px<=x+124 && m_savePage>0)--m_savePage;
            else if(px>=x+w-124 && (m_savePage+1)*4<int(m_saveCopies.size()))++m_savePage;
            return;
        }
        if(py>=y+456 && py<=y+498) {
            if(px<=x+184)m_saveBrowserOpen=false;
            else if(px>=x+w-224 && m_selectedSave>=0 && m_selectedSave<int(m_saveCopies.size())) {
                const auto& copy=m_saveCopies[m_selectedSave];m_pendingImportFolder=copy.folder;
                m_pendingImportLabel=copy.kind=="Backup"?copy.created:copy.folder.filename().string();
                m_pendingSaveAction=SaveAction::Import;m_saveBrowserOpen=false;
            }
        }
    }
    void ApplyPadPreset(int preset) {
        const auto error=RowellPad::Current().Reset();
        if(!error.empty()){m_padStatus=error;return;}
        if(preset==0 || preset==1)Gamepad_SetLayout(preset);
        else if(preset==2)Gamepad_SetSwapTriggers(!Gamepad_GetSwapTriggers());
        m_padPendingPreset=-1;
        m_padStatus="Controller defaults restored. Touch and keyboard settings unchanged.";
    }
    void RequestPadPreset(int preset) {
        if(RowellPad::Current().HasCustom()) {
            m_padEditorOpen=true;m_padCaptureAction=-1;m_padPendingPreset=preset;
        } else ApplyPadPreset(preset);
    }
    void SavePadBinding(int source,bool exchange=false) {
        const auto error=RowellPad::Current().Assign(m_padCaptureAction,source,
            Gamepad_GetLayout(),Gamepad_GetSwapTriggers(),exchange);
        m_padStatus=error.empty()?"Controller binding saved. No restart needed.":error;
        if(error.empty()){m_padCaptureAction=-1;RowellPad::SuppressHeld();}
    }
    void RenderPadEditor() {
        const float w=std::min(920.0f,float(m_width)-64),x=(m_width-w)*.5f,y=(m_height-560)*.5f;
        const auto state=RowellPad::Poll();
        if(m_padCaptureAction>=0) {
            // Stop capture even if a conflict prompt was already showing.
            if(!state.connected || state.id!=m_padCaptureDevice) {
                m_padCaptureAction=-1;m_padStatus="Controller disconnected. Reconnect and choose the action again.";
            } else if(m_padConflictSource<0) {
                const int source=m_padCapture.Update(state);
                if(source>=0) {
                    m_padConflictOwner=RowellPad::Current().Owner(source,Gamepad_GetLayout(),Gamepad_GetSwapTriggers(),m_padCaptureAction);
                    if(m_padConflictOwner>=0)m_padConflictSource=source;
                    else SavePadBinding(source);
                }
            }
        }
        DrawRectSolid(0,0,float(m_width),float(m_height),{0,0,0,.72f});
        DrawRoundedRect(x,y,w,560,16,COL_BG_DARK,COL_CYAN);
        DrawText(x+24,y+18,"Controller bindings",24,COL_WHITE);
        DrawClippedText(x+24,y+53,state.connected?"Connected: "+state.name:"No controller connected. Pair a Bluetooth or USB controller first.",14,w-48,COL_CYAN);
        DrawText(x+24,y+76,"Choose an action, then press a button. Touch and keyboard bindings stay separate.", 15.0f, COL_MUTED);
        const auto bindings=RowellPad::Current().Get(Gamepad_GetLayout(),Gamepad_GetSwapTriggers());
        const float labelW=w*.28f,bw=w-48-labelW;
        DrawText(x+24,y+90,"Wii action",12,COL_MUTED);
        DrawText(x+24+labelW,y+90,"Controller input",12,COL_MUTED);
        for(int row=0;row<6;++row) {
            const int action=m_padPage*6+row;if(action>=RowellPad::ActionCount)break;
            const float ry=y+106+row*48;
            DrawText(x+24,ry+12,RowellPad::Definition(action).label,16,COL_WHITE);
            DrawRoundedRect(x+24+labelW,ry,bw,40,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawClippedText(x+36+labelW,ry+12,RowellPad::SourceName(bindings[action]),15,bw-24,
                bindings[action]<0?COL_MUTED:COL_WHITE);
        }
        DrawClippedText(x+24,y+416,m_padStatus,13,w-48,COL_CYAN);
        RenderPager(x,y+450,w,m_padPage,(RowellPad::ActionCount+5)/6);
        DrawRoundedRect(x+24,y+500,160,38,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+104,y+511,"Close",15,COL_WHITE,true);
        DrawRoundedRect(x+w-224,y+500,200,38,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-124,y+511,"Reset defaults",15,COL_WHITE,true);
        if(m_padCaptureAction>=0 || m_padPendingPreset>=0) {
            const float cw=std::min(660.0f,w-32),cx=(m_width-cw)*.5f,cy=(m_height-260)*.5f,buttonW=(cw-64)*.5f;
            DrawRectSolid(0,0,float(m_width),float(m_height),{0,0,0,.70f});
            DrawRoundedRect(cx,cy,cw,260,16,COL_BG_DARK,COL_CYAN);
            if(m_padPendingPreset>=0) {
                DrawText(cx+24,cy+24,"Reset controller bindings?",23,COL_WHITE);
                DrawText(cx+24,cy+72,"This replaces your custom controller map with the selected defaults.", 15.0f, COL_MUTED);
                DrawText(cx+24,cy+98,"Your touch layout, keyboard bindings and game saves stay unchanged.", 15.0f, COL_MUTED);
            } else {
                DrawText(cx+24,cy+24,m_padConflictSource>=0?"Input already assigned":"Press a controller button",23,COL_WHITE);
                DrawText(cx+24,cy+64,(std::string("Wii action: ")+RowellPad::Definition(m_padCaptureAction).label).c_str(),17,COL_CYAN);
                if(m_padConflictSource>=0) {
                    DrawClippedText(cx+24,cy+100,RowellPad::SourceName(m_padConflictSource)+" is used by "+RowellPad::Definition(m_padConflictOwner).label+".",14,cw-48,COL_WHITE);
                    const auto old=RowellPad::SourceName(bindings[m_padCaptureAction]);
                    DrawClippedText(cx+24,cy+126,"Swap gives "+std::string(RowellPad::Definition(m_padConflictOwner).label)+" your old input: "+old,13,cw-48,COL_MUTED);
                } else {
                    DrawText(cx+24,cy+100,"Buttons, triggers and L3/R3 work. Keyboard keys are ignored.", 15.0f, COL_MUTED);
                    DrawText(cx+24,cy+126,"Already-held buttons are ignored until released. Clear disables this action.", 15.0f, COL_MUTED);
                }
            }
            DrawRoundedRect(cx+24,cy+190,buttonW,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(cx+24+buttonW*.5f,cy+202,"Cancel",15,COL_WHITE,true);
            DrawRoundedRect(cx+40+buttonW,cy+190,buttonW,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(cx+40+buttonW*1.5f,cy+202,m_padPendingPreset>=0?"Yes, reset":m_padConflictSource>=0?"Swap bindings":"Clear binding",15,COL_WHITE,true);
        }
    }
    void HandlePadEditorTouch(float px,float py) {
        const float w=std::min(920.0f,float(m_width)-64),x=(m_width-w)*.5f,y=(m_height-560)*.5f;
        if(m_padCaptureAction>=0 || m_padPendingPreset>=0) {
            const float cw=std::min(660.0f,w-32),cx=(m_width-cw)*.5f,cy=(m_height-260)*.5f,bw=(cw-64)*.5f;
            if(py>=cy+190 && py<=cy+232) {
                if(px>=cx+24 && px<=cx+24+bw)CancelModal();
                else if(px>=cx+40+bw && px<=cx+cw-24) {
                    if(m_padPendingPreset>=0)ApplyPadPreset(m_padPendingPreset);
                    else if(const auto state=RowellPad::Poll(); !state.connected || state.id!=m_padCaptureDevice) {
                        m_padCaptureAction=-1;m_padStatus="Controller disconnected. Binding was not changed.";
                    } else SavePadBinding(m_padConflictSource,m_padConflictSource>=0);
                }
            }
            return;
        }
        if(px<x+24 || px>x+w-24)return;
        for(int row=0;row<6;++row) {
            const int action=m_padPage*6+row;if(action>=RowellPad::ActionCount)break;
            const float ry=y+106+row*48;
            if(py>=ry && py<=ry+40 && px>=x+24+w*.28f) {
                const auto state=RowellPad::Poll();
                if(!state.connected){m_padStatus="Connect a Bluetooth or USB controller first. Touch controls are still available.";return;}
                m_padCaptureAction=action;m_padCaptureDevice=state.id;m_padConflictSource=-1;m_padConflictOwner=-1;
                m_padCapture.Begin(state);ResetInput();return;
            }
        }
        if(py>=y+450 && py<=y+482) {
            if(px<=x+124 && m_padPage>0)--m_padPage;
            else if(px>=x+w-124 && (m_padPage+1)*6<RowellPad::ActionCount)++m_padPage;
        }
        if(py>=y+500 && py<=y+538) {
            if(px<=x+184)m_padEditorOpen=false;
            else if(px>=x+w-224){m_padPendingPreset=3;}
        }
    }
#ifdef MH3_PC_TOUCH_UI
    void RenderKeyboardEditor() {
        const float w=std::min(920.0f,float(m_width)-64),x=(m_width-w)*.5f,y=(m_height-560)*.5f;
        DrawRectSolid(0,0,float(m_width),float(m_height),{0,0,0,.72f});
        DrawRoundedRect(x,y,w,560,16,COL_BG_DARK,COL_CYAN);
        DrawText(x+24,y+18,"PC keyboard bindings",24,COL_WHITE);
        DrawText(x+24,y+53,"Click a key to change it. Three keys per action. Changes save automatically.", 15.0f, COL_MUTED);
        const float labelW=w*.30f,keyW=(w-48-labelW-90-32)/3;
        for(int row=0;row<6;++row) {
            const int action=m_keyPage*6+row;if(action>=RowellKeys::ActionCount)break;
            const float ry=y+84+row*54;
            DrawText(x+24,ry+14,RowellKeys::Definition(action).label,16,COL_WHITE);
            const auto keys=RowellKeys::Current().Get(action);
            for(int slot=0;slot<3;++slot) {
                const float kx=x+24+labelW+slot*(keyW+8);
                DrawRoundedRect(kx,ry,keyW,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
                const char* name=keys[slot]==SDL_SCANCODE_UNKNOWN?"Unassigned":SDL_GetScancodeName(keys[slot]);
                DrawClippedText(kx+10,ry+13,name,14,keyW-20,keys[slot]==SDL_SCANCODE_UNKNOWN?COL_MUTED:COL_WHITE);
            }
            DrawRoundedRect(x+w-114,ry,90,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(x+w-69,ry+13,"Reset",14,COL_WHITE,true);
        }
        DrawClippedText(x+24,y+416,m_keyStatus,13,w-48,COL_CYAN);
        RenderPager(x,y+450,w,m_keyPage,(RowellKeys::ActionCount+5)/6);
        DrawRoundedRect(x+24,y+500,160,38,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+104,y+511,"Close",15,COL_WHITE,true);
        DrawRoundedRect(x+w-224,y+500,200,38,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-124,y+511,"Reset all defaults",15,COL_WHITE,true);
        if(m_captureAction>=0) {
            const float cw=std::min(600.0f,w-32),cx=(m_width-cw)*.5f,cy=(m_height-240)*.5f;
            DrawRectSolid(0,0,float(m_width),float(m_height),{0,0,0,.70f});
            DrawRoundedRect(cx,cy,cw,240,16,COL_BG_DARK,COL_CYAN);
            DrawText(cx+24,cy+24,"Press a key",25,COL_WHITE);
            DrawText(cx+24,cy+65,RowellKeys::Definition(m_captureAction).label,18,COL_CYAN);
            DrawText(cx+24,cy+95,"Escape cancels. F2/F3 stay reserved for settings and speed.", 15.0f, COL_MUTED);
            DrawClippedText(cx+24,cy+125,m_keyStatus,13,cw-48,COL_CYAN);
            const float bw=(cw-64)*.5f;
            DrawRoundedRect(cx+24,cy+174,bw,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(cx+24+bw*.5f,cy+186,"Cancel",15,COL_WHITE,true);
            DrawRoundedRect(cx+40+bw,cy+174,bw,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(cx+40+bw*1.5f,cy+186,"Clear binding",15,COL_WHITE,true);
        }
    }
    void HandleKeyboardEditorTouch(float px,float py) {
        const float w=std::min(920.0f,float(m_width)-64),x=(m_width-w)*.5f,y=(m_height-560)*.5f;
        if(m_captureAction>=0) {
            const float cw=std::min(600.0f,w-32),cx=(m_width-cw)*.5f,cy=(m_height-240)*.5f,bw=(cw-64)*.5f;
            if(py>=cy+174 && py<=cy+216) {
                if(px>=cx+24 && px<=cx+24+bw)CancelModal();
                else if(px>=cx+40+bw && px<=cx+cw-24) {
                    const auto error=RowellKeys::Current().Assign(m_captureAction,m_captureSlot,SDL_SCANCODE_UNKNOWN);
                    m_keyStatus=error.empty()?"Binding cleared and saved.":error;
                    if(error.empty())m_captureAction=-1;
                }
            }
            return;
        }
        if(px<x+24 || px>x+w-24)return;
        const float labelW=w*.30f,keyW=(w-48-labelW-90-32)/3;
        for(int row=0;row<6;++row) {
            const int action=m_keyPage*6+row;if(action>=RowellKeys::ActionCount)break;
            const float ry=y+84+row*54;
            if(py<ry || py>ry+42)continue;
            if(px>=x+w-114) {
                const auto error=RowellKeys::Current().Reset(action);m_keyStatus=error.empty()?"Action reset to defaults.":error;return;
            }
            for(int slot=0;slot<3;++slot) {
                const float kx=x+24+labelW+slot*(keyW+8);
                if(px>=kx && px<=kx+keyW) {m_captureAction=action;m_captureSlot=slot;m_keyStatus="Waiting for a key...";ResetInput();return;}
            }
        }
        if(py>=y+450 && py<=y+482) {
            if(px<=x+124 && m_keyPage>0)--m_keyPage;
            else if(px>=x+w-124 && (m_keyPage+1)*6<RowellKeys::ActionCount)++m_keyPage;
        }
        if(py>=y+500 && py<=y+538) {
            if(px<=x+184)m_keyboardEditorOpen=false;
            else if(px>=x+w-224){const auto error=RowellKeys::Current().Reset();m_keyStatus=error.empty()?"All keyboard defaults restored.":error;}
        }
    }
#endif

    void ApplyComboMasks() {
        for(auto& button:m_buttons) {
            if(button.id.size()!=2 || button.id[0]!='M' || button.id[1]<'1' || button.id[1]>'5')continue;
            const int slot=button.id[1]-'1';button.mask=m_comboMasks[slot];
            button.label=button.mask?ComboLabel(slot,true):"Empty";
        }
    }
    bool& ComboEnabled(int i) {
        switch(i) { case 0:return m_macro1Enabled; case 1:return m_macro2Enabled;
          case 2:return m_macro3Enabled; case 3:return m_macro4Enabled; default:return m_macro5Enabled; }
    }

    std::string ComboLabel(int slot,bool compact=false) const {
        if(slot<0 || slot>=5 || !m_comboMasks[slot])return "Not configured";
        const auto mask=m_comboMasks[slot];
        if(mask==(MH3_CL_R|MH3_CL_X|MH3_CL_A))return compact?"R+X+A":"R + X + A";
        const char* names[]={"A","B","X","Y","L","R","ZL","ZR","Plus","Minus","Home","Up","Down","Left","Right"};
        const uint16_t bits[]={MH3_CL_A,MH3_CL_B,MH3_CL_X,MH3_CL_Y,MH3_CL_L,MH3_CL_R,MH3_CL_ZL,MH3_CL_ZR,
            MH3_CL_PLUS,MH3_CL_MINUS,MH3_CL_HOME,MH3_CL_UP,MH3_CL_DOWN,MH3_CL_LEFT,MH3_CL_RIGHT};
        std::string label;
        for(int i=0;i<15;++i)if(mask&bits[i]){if(!label.empty())label+=compact?"+":" + ";label+=names[i];}
        return label.empty()?"Custom combo":label;
    }
    int ComboCount() const {
        return int(std::count_if(m_comboMasks.begin(),m_comboMasks.end(),[](uint16_t mask){return mask!=0;}));
    }
    bool ComboHasShortcut(int slot) const {
        if(slot<0 || slot>=5)return false;
        // Combo defaults are unassigned for every gamepad layout. Do not poll a device.
        if(RowellPad::Current().Get(0,false)[RowellPad::ActionCount-5+slot]!=RowellPad::Unassigned)return true;
#ifdef MH3_PC_TOUCH_UI
        for(auto key:RowellKeys::Current().Get(RowellKeys::ActionCount-5+slot))
            if(key!=SDL_SCANCODE_UNKNOWN)return true;
#endif
        return false;
    }
    void OpenLibraryCombo(int slot,bool remove=false) {
        BeginLayoutEdit(true);
        if(slot<0){OpenEditorCombo(false);return;}
        for(int i=0;i<int(m_buttons.size());++i)if(m_buttons[i].id=="M"+std::to_string(slot+1)){
            m_selectedButtonIndex=i;break;
        }
        if(remove)RequestComboDelete(slot);else OpenEditorCombo(true);
    }
    void RenderTabCombos(float x,float y,float w) {
        const int count=ComboCount();
        DrawText(x,y+6,"Saved combinations",20,COL_WHITE);
        const auto hint=std::to_string(count)+" of 5 slots. "+(count<5?"Add a combo, then place its button.":"Delete an unused combo to free a slot.");
        DrawClippedText(x,y+39,hint,15,w-200,COL_MUTED);
        DrawRoundedRect(x+w-180,y+5,180,42,8,count<5?COL_CYAN:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-90,y+17,"Add combo",18,count<5?COL_BG_DARK:COL_MUTED,true);
        if(!count) {
            DrawRoundedRect(x,y+82,w,144,12,COL_PANEL_CARD,COL_PANEL_BORDER);
            DrawText(x+20,y+106,"No custom combos yet",21,COL_WHITE);
            DrawClippedText(x+20,y+146,"Choose Wii buttons to hold together, then drag the new button into place.",15,w-40,COL_MUTED);
        }
        int row=0;
        for(int slot=0;slot<5;++slot)if(m_comboMasks[slot]) {
            const float ry=y+82+row++*72;
            DrawRoundedRect(x,ry,w,62,10,COL_PANEL_CARD,COL_PANEL_BORDER);
            DrawClippedText(x+16,ry+10,ComboLabel(slot),20,w-256,COL_WHITE);
            const auto detail="Combo "+std::to_string(slot+1)+(ComboEnabled(slot)?" | Shown on screen":" | Hidden on screen");
            DrawText(x+16,ry+37,detail.c_str(),13,COL_MUTED);
            DrawRoundedRect(x+w-220,ry+13,96,36,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(x+w-172,ry+23,"Edit",15,COL_WHITE,true);
            DrawRoundedRect(x+w-112,ry+13,96,36,8,COL_INACTIVE_CHIP,COL_DANGER);
            DrawText(x+w-64,ry+23,"Delete",15,COL_DANGER,true);
        }
        DrawClippedText(x,y+462,"Edit uses the layout editor. Save & play keeps changes; Cancel discards them.",14,w,COL_MUTED);
    }

    bool HandleComboTap(float px,float py,float x,float y,float w) {
        if(px>=x+w-180 && px<=x+w && py>=y+5 && py<=y+47) {
            if(ComboCount()<5)OpenLibraryCombo(-1);
            return true;
        }
        int row=0;
        for(int slot=0;slot<5;++slot)if(m_comboMasks[slot]) {
            const float ry=y+82+row++*72;
            if(py<ry+13 || py>ry+49)continue;
            if(px>=x+w-220 && px<=x+w-124){OpenLibraryCombo(slot);return true;}
            if(px>=x+w-112 && px<=x+w-16){OpenLibraryCombo(slot,true);return true;}
        }
        return false;
    }

    void RenderTabControls(float x, float y, float w) {
        // Card 1: Visibilidad y Opacidad de controles táctiles en pantalla
        float c1H = 176.0f;
        DrawRoundedRect(x, y, w, c1H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, y + 14.0f, "On-screen touch controls", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, y + 34.0f, "Show touch controls, or hide them when using a gamepad.", 15.0f, COL_MUTED);
#ifdef MH3_PC_TOUCH_UI
        DrawRoundedRect(x+w-228,y+12,210,32,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-123,y+21,"Keyboard bindings",14,COL_WHITE,true);
#endif

        float tvChipW = (w - 36.0f - 15.0f) / 2.0f;
        float tvChipH = 38.0f;
        float tvChipY = y + 54.0f;

        // Visible chip
        float tv1X = x + 18.0f;
        DrawRoundedRect(tv1X, tvChipY, tvChipW, tvChipH, 8.0f,
                        m_touchVisible ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        m_touchVisible ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(tv1X + tvChipW * 0.5f, tvChipY + 10.0f, "Touch buttons: SHOW", 14.5f,
                 m_touchVisible ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Hidden chip
        float tv2X = tv1X + tvChipW + 15.0f;
        DrawRoundedRect(tv2X, tvChipY, tvChipW, tvChipH, 8.0f,
                        !m_touchVisible ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        !m_touchVisible ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(tv2X + tvChipW * 0.5f, tvChipY + 10.0f, "Touch buttons: HIDE", 14.5f,
                 !m_touchVisible ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Row 2: Opacity selector
        DrawText(x + 18.0f, y + 102.0f, "Button opacity:", 15.0f, COL_WHITE);
        const char* opLabels[] = { "25%", "50%", "75%", "100%" };
        const float opValues[] = { 0.25f, 0.50f, 0.75f, 1.00f };
        float opChipW = (w - 36.0f - 30.0f) / 4.0f;
        float opChipH = 36.0f;
        float opChipY = y + 124.0f;
        for (int i = 0; i < 4; ++i) {
            float ox = x + 18.0f + float(i) * (opChipW + 10.0f);
            bool active = (std::abs(m_buttonOpacity - opValues[i]) < 0.10f);
            DrawRoundedRect(ox, opChipY, opChipW, opChipH, 8.0f,
                            active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                            active ? COL_CYAN : COL_CHIP_BORDER);
            DrawText(ox + opChipW * 0.5f, opChipY + 9.0f, opLabels[i], 13.5f,
                     active ? COL_ACTIVE_TEXT : COL_WHITE, true);
        }

        // Card 2: Mando Gamepad (Distribución / Keymapper)
        float c2Y = y + c1H + 12.0f;
        float c2H = 116.0f;
        DrawRoundedRect(x, c2Y, w, c2H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, c2Y + 14.0f, "Physical controller", 20.0f, COL_WHITE);
        DrawRoundedRect(x+w-228,c2Y+10,210,30,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-123,c2Y+18,"Remap buttons",14,COL_WHITE,true);
        DrawText(x + 18.0f, c2Y + 39.0f, "Remap controller buttons and triggers. Touch and keyboard settings stay separate.", 15.0f, COL_MUTED);

        const int gpLayout = Gamepad_GetLayout();
        const bool swapTrig = Gamepad_GetSwapTriggers();

        float lChipW = (w * 0.60f - 36.0f - 10.0f) / 2.0f;
        float gpChipH = 42.0f;
        float gpChipY = c2Y + 58.0f;

        // Nintendo layout
        float l1X = x + 18.0f;
        DrawRoundedRect(l1X, gpChipY, lChipW, gpChipH, 8.0f,
                        (gpLayout == 0) ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        (gpLayout == 0) ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(l1X + lChipW * 0.5f, gpChipY + 12.0f, "Nintendo: A right, B bottom", 13.5f,
                 (gpLayout == 0) ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Xbox/Generic layout
        float l2X = l1X + lChipW + 10.0f;
        DrawRoundedRect(l2X, gpChipY, lChipW, gpChipH, 8.0f,
                        (gpLayout == 1) ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        (gpLayout == 1) ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(l2X + lChipW * 0.5f, gpChipY + 12.0f, "Xbox: A bottom, B right", 13.5f,
                 (gpLayout == 1) ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Swap triggers
        float trigX = x + w * 0.62f;
        float trigW = w * 0.38f - 18.0f;
        DrawRoundedRect(trigX, gpChipY, trigW, gpChipH, 8.0f,
                        swapTrig ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        swapTrig ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(trigX + trigW * 0.5f, gpChipY + 12.0f,
                 swapTrig ? "Shoulders/triggers: SWAP" : "Shoulders/triggers: ORIGINAL", 14.5f,
                 swapTrig ? COL_ACTIVE_TEXT : COL_WHITE, true);

        const float c3Y=c2Y+c2H+12;
        DrawRoundedRect(x,c3Y,w,118,12,COL_PANEL_CARD,COL_PANEL_BORDER);
        DrawText(x+18,c3Y+14,"Touch layout",20,COL_WHITE);
        DrawClippedText(x+18,c3Y+40,"Move and resize Wii controls. Your custom combinations live in the Combos library.",15,w-36,COL_MUTED);
        const float bw=(w-51)*.5f,by=c3Y+64;
        DrawRoundedRect(x+18,by,bw,38,8,COL_ACTIVE_CHIP,COL_CYAN);
        DrawText(x+18+bw*.5f,by+11,"Edit touch layout",16,COL_WHITE,true);
        DrawRoundedRect(x+33+bw,by,bw,38,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+33+bw*1.5f,by+11,"Custom combos",16,COL_WHITE,true);

    }

    EditSnapshot CaptureLayout() {
        return {m_buttons,m_normLeftStickPos,m_leftStickScale,m_comboMasks,
            {m_macro1Enabled,m_macro2Enabled,m_macro3Enabled,m_macro4Enabled,m_macro5Enabled}};
    }
    void RestoreLayout(const EditSnapshot& saved) {
        m_buttons=saved.buttons;m_normLeftStickPos=saved.stick;m_leftStickScale=saved.stickScale;
        m_comboMasks=saved.combos;
        for(int i=0;i<5;++i)ComboEnabled(i)=saved.enabled[i];
        ApplyComboMasks();RecalculateButtonPositions();ResetInput();m_selectedButtonIndex=-1;
    }
    void CheckpointLayout() {
        if(m_editorUndo.size()>=20)m_editorUndo.erase(m_editorUndo.begin());
        m_editorUndo.push_back(CaptureLayout());
    }
    void BeginLayoutEdit(bool fromLibrary=false) {
        SetMenuOpen(false);ResetInput();m_editMode=true;m_selectedButtonIndex=-1;
        m_editorOriginal=CaptureLayout();m_editorUndo.clear();m_editorComboSlot=-1;m_editorResetConfirm=false;
        m_editorDeleteSlot=-1;m_editorFromLibrary=fromLibrary;
        m_editorStatus="Drag a control. Drag this panel by its title.";
    }
    void CancelLayoutEdit() {
        if(m_editorOriginal)RestoreLayout(*m_editorOriginal);
        else {LoadLayoutConfig();RecalculateButtonPositions();}
        m_editMode=false;m_editorComboSlot=-1;m_editorDeleteSlot=-1;m_editorResetConfirm=false;m_editorUndo.clear();m_editorOriginal.reset();
        const bool returnToLibrary=m_editorFromLibrary;m_editorFromLibrary=false;
        if(returnToLibrary){SetMenuOpen(true);m_menuTab=5;}
    }
    Vec2 EditorPanelSize() const { return {std::min(360.0f,float(m_width)-24),m_editorCollapsed?42.0f:276.0f}; }
    Vec2 EditorPanelPosition() const {
        const auto size=EditorPanelSize();
        return {12+m_editorPanelNorm.x*std::max(0.0f,m_width-size.x-24),
                12+m_editorPanelNorm.y*std::max(0.0f,m_height-size.y-24)};
    }
    void MoveEditorControl(float px,float py) {
        if(m_editorPanelDragging) {
            const auto size=EditorPanelSize();
            const float maxX=std::max(1.0f,m_width-size.x-24),maxY=std::max(1.0f,m_height-size.y-24);
            m_editorPanelNorm={std::clamp((px-m_editorDragOffset.x-12)/maxX,0.0f,1.0f),
                               std::clamp((py-m_editorDragOffset.y-12)/maxY,0.0f,1.0f)};
        } else if(m_selectedButtonIndex>=0 && m_selectedButtonIndex<int(m_buttons.size())) {
            auto& button=m_buttons[m_selectedButtonIndex];
            button.pos={std::clamp(px,button.radius,float(m_width)-button.radius),
                        std::clamp(py,button.radius,float(m_height)-button.radius)};
            button.normPos={button.pos.x/m_width,button.pos.y/m_height};
        } else if(m_selectedButtonIndex==-2) {
            m_leftStickCenter={std::clamp(px,m_leftStickRadius,float(m_width)-m_leftStickRadius),
                               std::clamp(py,m_leftStickRadius,float(m_height)-m_leftStickRadius)};
            m_normLeftStickPos={m_leftStickCenter.x/m_width,m_leftStickCenter.y/m_height};
            m_leftStickPos=m_leftStickCenter;
        }
    }
    int SelectedCombo() const {
        if(m_selectedButtonIndex<0 || m_selectedButtonIndex>=int(m_buttons.size()))return -1;
        const auto& id=m_buttons[m_selectedButtonIndex].id;
        return id.size()==2 && id[0]=='M' && id[1]>='1' && id[1]<='5'?id[1]-'1':-1;
    }

    void OpenEditorCombo(bool edit) {
        int slot=edit?SelectedCombo():-1;
        if(!edit)for(int i=0;i<5;++i)if(!m_comboMasks[i]){slot=i;break;}
        if(slot<0 || (edit && !m_comboMasks[slot])) {
            m_editorStatus=edit?"Select a custom combo to edit it.":"All 5 slots are in use. Delete an unused combo first.";return;
        }
        m_editorComboSlot=slot;m_editorComboNew=!edit;m_editorComboMask=edit?m_comboMasks[slot]:0;
        m_editorComboVisible=edit?ComboEnabled(slot):true;
        m_editDragging=false;m_editFingerId.reset();
    }
    void RequestComboDelete(int slot=-1) {
        if(slot<0)slot=SelectedCombo();
        if(slot<0 || slot>=5 || !m_comboMasks[slot]) {
            m_editorStatus="Select a custom combo. Original Wii controls cannot be deleted.";return;
        }
        m_editorDeleteSlot=slot;m_editDragging=false;m_editFingerId.reset();
    }
    void RenderEditMode() {
        DrawRectSolid(0, 0, float(m_width), float(m_height), { 0.0f, 0.0f, 0.0f, 0.40f });

        // Buttons with editor bounding rings
        for (size_t i = 0; i < m_buttons.size(); ++i) {
            const auto& btn = m_buttons[i];
            if (!IsButtonActive(btn)) continue;
            bool isSel = (int(i) == m_selectedButtonIndex);
            DrawAADisc(btn.pos.x, btn.pos.y, btn.radius,
                       isSel ? Color{ .30f,.30f,.30f,.80f } : Color{ .08f,.08f,.08f,.60f },
                       isSel ? COL_WHITE : Color{ .80f,.80f,.80f,.75f }, isSel ? 0.14f : 0.09f);
            if(isSel)DrawAARing(btn.pos.x,btn.pos.y,btn.radius*1.10f,1.25f,COL_WHITE);
            DrawButtonLabel(btn.pos.x, btn.pos.y, btn.label.c_str(), btn.radius * 0.80f, COL_WHITE,btn.radius*1.48f);
        }

        // Left Stick
        bool stickSel = (m_selectedButtonIndex == -2);
        DrawAARing(m_leftStickCenter.x, m_leftStickCenter.y, m_leftStickRadius, stickSel ? 1.75f : 1.25f, stickSel ? COL_CYAN : Color{ 0.30f, 0.70f, 0.88f, 0.55f });
        DrawAADisc(m_leftStickPos.x, m_leftStickPos.y, m_leftStickKnobRadius,
                   {.12f,.12f,.12f,.35f},stickSel?COL_WHITE:Color{.9f,.9f,.9f,.65f});

        const auto size=EditorPanelSize(),panel=EditorPanelPosition();
        const float x=panel.x,y=panel.y,w=size.x;
        DrawRoundedRect(x,y,w,size.y,12,COL_BG_DARK,COL_CYAN);
        DrawText(x+14,y+13,"Layout editor  : :  drag",16,COL_WHITE);
        DrawRoundedRect(x+w-42,y+6,34,30,6,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+w-25,y+12,m_editorCollapsed?"+":"-",20,COL_WHITE,true);
        if(!m_editorCollapsed) {
            std::string selected="Select a control";
            if(m_selectedButtonIndex==-2)selected="Selected: movement stick";
            else if(m_selectedButtonIndex>=0 && m_selectedButtonIndex<int(m_buttons.size())) {
                const auto& button=m_buttons[m_selectedButtonIndex];
                selected="Selected: "+button.label+"  ("+std::to_string(int(std::lround(button.scale*100)))+"%)";
            }
            DrawClippedText(x+14,y+51,selected,14,w-28,COL_WHITE);
            auto chip=[&](float bx,float by,float bw,const char* label,bool active=false) {
                DrawRoundedRect(bx,by,bw,40,7,active?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
                DrawText(bx+bw*.5f,by+12,label,14,active?COL_ACTIVE_TEXT:COL_WHITE,true);
            };
            chip(x+14,y+78,44,"-");chip(x+66,y+78,44,"+");
            const float actionW=(w-140)*.5f;
            const bool custom=SelectedCombo()>=0 && m_comboMasks[SelectedCombo()]!=0;
            DrawRoundedRect(x+118,y+78,actionW,40,7,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
            DrawText(x+118+actionW*.5f,y+90,"Edit combo",14,custom?COL_WHITE:COL_MUTED,true);
            DrawRoundedRect(x+126+actionW,y+78,actionW,40,7,COL_INACTIVE_CHIP,custom?COL_DANGER:COL_CHIP_BORDER);
            DrawText(x+126+actionW*1.5f,y+90,"Delete",14,custom?COL_DANGER:COL_MUTED,true);
            const float cw=(w-44)/3;
            chip(x+14,y+128,cw,"Add combo");chip(x+22+cw,y+128,cw,"Undo");chip(x+30+cw*2,y+128,cw,"Reset");
            DrawClippedText(x+14,y+181,m_editorStatus,12,w-28,COL_CYAN);
            DrawText(x+14,y+199,"Save & play keeps edits. Cancel discards them.", 15.0f, COL_MUTED);
            chip(x+14,y+222,(w-36)*.5f,"Cancel");chip(x+22+(w-36)*.5f,y+222,(w-36)*.5f,"Save & play",true);
        }
        if(m_editorComboSlot>=0 || m_editorResetConfirm || m_editorDeleteSlot>=0)RenderEditorDialog();
    }


    void RenderEditorDialog() {
        const float w=std::min(600.0f,float(m_width)-48),x=(m_width-w)*.5f,y=(m_height-330)*.5f;
        DrawRectSolid(0,0,float(m_width),float(m_height),{0,0,0,.66f});
        DrawRoundedRect(x,y,w,330,14,COL_BG_DARK,m_editorDeleteSlot>=0?COL_DANGER:COL_CYAN);
        if(m_editorDeleteSlot>=0) {
            DrawText(x+24,y+24,"Delete custom combo?",23,COL_WHITE);
            DrawClippedText(x+24,y+76,ComboLabel(m_editorDeleteSlot),21,w-48,COL_CYAN);
            DrawClippedText(x+24,y+118,"Original Wii buttons and their mappings stay unchanged.",15,w-48,COL_MUTED);
            if(ComboHasShortcut(m_editorDeleteSlot))
                DrawClippedText(x+24,y+157,"Its assigned controller or keyboard shortcut will become inactive.",14,w-48,COL_CAUTION);
            DrawClippedText(x+24,y+204,"Undo restores it before Save & play. Cancel discards all edits.",14,w-48,COL_MUTED);
        } else if(m_editorResetConfirm) {
            DrawText(x+24,y+24,"Reset control positions?",23,COL_WHITE);
            DrawClippedText(x+24,y+78,"Restore default positions and sizes. Your combo choices stay.",15,w-48,COL_MUTED);
            DrawText(x+24,y+112,"Undo can recover this change before you save.",15,COL_MUTED);
        } else {
            DrawText(x+24,y+22,m_editorComboNew?"Add custom combo":"Edit custom combo",23,COL_WHITE);
            DrawClippedText(x+24,y+60,"Hold to press the selected Wii buttons together. Lift to release.",15,w-48,COL_MUTED);
            const char* names[]={"A","B","X","Y","L","R","ZL","ZR"};
            const uint16_t bits[]={MH3_CL_A,MH3_CL_B,MH3_CL_X,MH3_CL_Y,MH3_CL_L,MH3_CL_R,MH3_CL_ZL,MH3_CL_ZR};
            const float bw=(w-78)/4;
            for(int i=0;i<8;++i) {
                const float bx=x+24+(i%4)*(bw+10),by=y+96+(i/4)*58;
                const bool active=(m_editorComboMask&bits[i])!=0;
                DrawRoundedRect(bx,by,bw,46,8,active?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,COL_CHIP_BORDER);
                DrawText(bx+bw*.5f,by+14,names[i],17,COL_WHITE,true);
            }
            if(!m_editorComboNew) {
                DrawRoundedRect(x+24,y+214,w-204,32,7,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
                DrawText(x+24+(w-204)*.5f,y+223,m_editorComboVisible?"Show touch button: ON":"Show touch button: OFF",13,COL_WHITE,true);
                DrawRoundedRect(x+w-160,y+214,136,32,7,COL_INACTIVE_CHIP,COL_DANGER);
                DrawText(x+w-92,y+223,"Delete combo",13,COL_DANGER,true);
            } else DrawClippedText(x+24,y+222,"Create it, drag it into place, then Save & play.",15,w-48,COL_MUTED);
        }
        const float bw=(w-64)*.5f;
        DrawRoundedRect(x+24,y+266,bw,42,8,COL_INACTIVE_CHIP,COL_CHIP_BORDER);
        DrawText(x+24+bw*.5f,y+278,m_editorDeleteSlot>=0?"No, keep it":"Cancel",15,COL_WHITE,true);
        const bool available=m_editorDeleteSlot>=0 || m_editorResetConfirm || m_editorComboMask!=0;
        DrawRoundedRect(x+40+bw,y+266,bw,42,8,available?COL_ACTIVE_CHIP:COL_INACTIVE_CHIP,m_editorDeleteSlot>=0?COL_DANGER:COL_CYAN);
        const char* action=m_editorDeleteSlot>=0?"Yes, delete combo":m_editorResetConfirm?"Reset positions":m_editorComboNew?"Create combo":"Apply combo";
        DrawText(x+40+bw*1.5f,y+278,action,15,available?COL_WHITE:COL_MUTED,true);
    }

    void RenderTabOnline(float x, float y, float w) {
#ifdef MH3_PC_TOUCH_UI
        DrawText(x,y,"PC online play uses the existing community-server configuration.",18,COL_WHITE);
        DrawClippedText(x,y+32,"This menu does not change your existing network settings.",15,w,COL_MUTED);
#else

        // Card 1: Modo de Conexión
        float c1H = 108.0f;
        DrawRoundedRect(x, y, w, c1H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);
        DrawText(x + 18.0f, y + 14.0f, "Online multiplayer", 20.0f, COL_WHITE);
        DrawText(x + 18.0f, y + 36.0f, "Connect to the Monster Hunter 3 community servers (MH3SP).", 15.0f, COL_MUTED);

        float mChipW = (w - 36.0f - 12.0f) / 2.0f;
        float mChipH = 42.0f;
        float mChipY = y + 54.0f;

        // Chip 0: Comunidad MH3SP
        bool c0Active = (m_onlineMode == 3);
        float c0X = x + 18.0f;
        DrawRoundedRect(c0X, mChipY, mChipW, mChipH, 8.0f,
                        c0Active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        c0Active ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(c0X + mChipW * 0.5f, mChipY + 12.0f, "MH3SP community (mh3sp.com)", 14.0f,
                 c0Active ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Chip 1: Desconectado
        bool c1Active = (m_onlineMode == 0);
        float c1X = c0X + mChipW + 12.0f;
        DrawRoundedRect(c1X, mChipY, mChipW, mChipH, 8.0f,
                        c1Active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP,
                        c1Active ? COL_CYAN : COL_CHIP_BORDER);
        DrawText(c1X + mChipW * 0.5f, mChipY + 12.0f, "Offline", 14.0f,
                 c1Active ? COL_ACTIVE_TEXT : COL_WHITE, true);

        // Card 2: Detalles
        float c2Y = y + c1H + 12.0f;
        float c2H = 224.0f;
        DrawRoundedRect(x, c2Y, w, c2H, 12.0f, COL_PANEL_CARD, COL_PANEL_BORDER);

        if (m_onlineMode == 3) { // Community
            DrawText(x + 18.0f, c2Y + 14.0f, "Community server: mh3sp.com (SSL/TLS)", 19.0f, COL_CYAN);
            DrawText(x + 18.0f, c2Y + 44.0f, "Connect to the global Monster Hunter 3 community servers.", 14.5f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 74.0f, "Rotating events, event quests and rooms with other hunters.", 14.5f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 104.0f, "Includes the community server certificate.", 14.5f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 134.0f, "Uses the existing OPN, NAS and GameSpy connection paths.", 14.5f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 174.0f, "Enter Loc Lac from the Moga Village gate to play online.", 15.0f, COL_MUTED);
        } else { // Offline
            DrawText(x + 18.0f, c2Y + 14.0f, "Offline mode (Moga Village)", 20.0f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 44.0f, "Network connections are disabled.", 14.5f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 74.0f, "Play the single-player story quests in Moga Village.", 14.5f, COL_WHITE);
            DrawText(x + 18.0f, c2Y + 104.0f, "No multiplayer network activity.", 14.5f, COL_WHITE);
        }

        // Status Banner at bottom
        float bannerY = c2Y + c2H + 12.0f;
        float bannerH = 44.0f;
        DrawRoundedRect(x, bannerY, w, bannerH, 8.0f, COL_INACTIVE_CHIP, COL_PANEL_BORDER);
        DrawText(x + 18.0f, bannerY + 12.0f, "Status:", 14.0f, COL_CYAN);
        DrawText(x + 85.0f, bannerY + 12.0f, m_onlineStatusMsg.c_str(), 14.0f, COL_WHITE);
    #endif
    }

    // -------------------------------------------------------------
    // Menu & Edit Touch Handlers
    // -------------------------------------------------------------
    void HandleMenuTouchDown(float px, float py) {
        if(m_restoreRestarting)return;
        if(m_padEditorOpen){HandlePadEditorTouch(px,py);return;}
#ifdef MH3_PC_TOUCH_UI
        if(m_keyboardEditorOpen){HandleKeyboardEditorTouch(px,py);return;}
#endif
        if(m_saveBrowserOpen){HandleSaveBrowserTouch(px,py);return;}
        if (IsSaveConfirmationOpen()) {
            const float w = std::min(560.0f, float(m_width) - 48.0f);
            const float x = (m_width - w) * 0.5f, y = (m_height - 220.0f) * 0.5f;
            const float bw = (w - 64.0f) * 0.5f;
            if (py >= y + 148.0f && py <= y + 196.0f) {
                if (px >= x + 24.0f && px <= x + 24.0f + bw) CancelSaveConfirmation();
                else if (px >= x + 40.0f + bw && px <= x + w - 24.0f) {
                    const auto action = m_pendingSaveAction;
                    const auto importFolder = m_pendingImportFolder;
                    m_pendingSaveAction = SaveAction::None;
                    m_pendingImportFolder.clear();
                    switch (action) {
                    case SaveAction::Export: ExportSaveFiles(); break;
                    case SaveAction::Import: ImportSaveFiles(importFolder); break;
                    case SaveAction::Backup: CreateQuickBackup(); break;
                    default: break;
                    }
                }
            }
            return; // The confirmation consumes all taps on the background.
        }
        float padX = float(m_width) * 0.05f;
        float padY = float(m_height) * 0.04f;
        float panW = float(m_width) - padX * 2.0f;
        float panH = float(m_height) - padY * 2.0f;

        // Close button [✕] or click outside panel
        float closeX = padX + panW - 144.0f;
        float closeY = padY + 18.0f;
        float closeS = 38.0f;
        if (px >= closeX && px <= closeX + 126.0f && py >= closeY && py <= closeY + closeS + 10.0f) {
            SetMenuOpen(false);
            return;
        }
        if (px < padX || px > padX + panW || py < padY || py > padY + panH) {
            SetMenuOpen(false);
            return;
        }

        // Tabs
        float tabY = padY + 86.0f;
        float tabW = (panW - 40.0f - 60.0f) / 7.0f;
        float tabH = 44.0f;
        if (py >= tabY && py <= tabY + tabH) {
            for (int i = 0; i < 7; ++i) {
                float tx = padX + 20.0f + float(i) * (tabW + 10.0f);
                if (px >= tx && px <= tx + tabW) {
                    m_menuTab = i;
                    if (m_menuTab == 2) RefreshLastBackup();
#ifndef MH3_PC_TOUCH_UI
                    if (m_menuTab == 4) {
                        LanBeacon::StartScanner(); LanBeacon::TriggerScan();
                    }
#endif
                    return;
                }
            }
        }

        float contentY = tabY + tabH + 16.0f;
        float contentW = panW - 40.0f;
        float x = padX + 20.0f;
        if (m_menuTab == 5) { HandleComboTap(px, py, x, contentY, contentW); return; }

        if (m_menuTab == 0) { // Gráficos
            // Resolution chips
            float chipY = contentY + 54.0f;
            float chipW = (contentW - 36.0f - 25.0f) / 6.0f;
            float chipH = 42.0f;
            if (py >= chipY && py <= chipY + chipH) {
                for (int i = 0; i < 6; ++i) {
                    float cx = x + 18.0f + float(i) * (chipW + 5.0f);
                    if (px >= cx && px <= cx + chipW) {
                        m_resolutionIndex = i;
                        ApplyResolutionScale(m_resolutionIndex);
                        SaveSettings();
                        return;
                    }
                }
            }

            // Card 2: Pantalla Completa & Aspect de aspecto
            float c2Y = contentY + 108.0f + 12.0f;
            float colW = (contentW - 36.0f - 16.0f) * 0.5f;
            float fsBtnY = c2Y + 54.0f;
            if (py >= fsBtnY && py <= fsBtnY + chipH) {
                // Click on "Enable fullscreen"
                if (px >= x + 18.0f && px <= x + 18.0f + colW) {
                    ToggleFullscreen();
                    return;
                }
                // Click on Aspect ratio chips
                float rightColX = x + 18.0f + colW + 16.0f;
                float aChipW = (colW - 10.0f) / 3.0f;
                for (int i = 0; i < 3; ++i) {
                    float ax = rightColX + float(i) * (aChipW + 5.0f);
                    if (px >= ax && px <= ax + aChipW) {
                        m_aspectIndex = i;
                        ApplyAspectRatio(m_aspectIndex);
                        SaveSettings();
                        return;
                    }
                }
            }

            // Card 3: FPS Limit (30, 60, 90, 120) & V-Sync / HUD
            float c3Y = c2Y + 114.0f + 12.0f;
            float fpsColW = (contentW - 36.0f - 16.0f) * 0.52f;
            float fChipY = c3Y + 54.0f;
            if (py >= fChipY && py <= fChipY + chipH) {
                float fChipW = (fpsColW - 15.0f) / 4.0f;
                for (int i = 0; i < 4; ++i) {
                    float fx = x + 18.0f + float(i) * (fChipW + 5.0f);
                    if (px >= fx && px <= fx + fChipW) {
                        if (IsOnlinePlay()) {
                            // Locked to 30 FPS in Online City
                            return;
                        }
                        const int fpsValues[] = { 30, 60, 90, 120 };
                        m_fpsLimit = fpsValues[i];
                        MH3FrameRateControl::SetTargetFPS(m_fpsLimit);
                        SaveSettings();
                        return;
                    }
                }

                float syncColX = x + 18.0f + fpsColW + 16.0f;
                float syncColW = (contentW - 36.0f - 16.0f) * 0.48f;
                float sChipW = (syncColW - 10.0f) * 0.5f;
                // VSync click
                if (px >= syncColX && px <= syncColX + sChipW) {
                    m_vsync = !m_vsync;
                    MH3FrameRateControl::SetTargetFPS(IsOnlinePlay() ? 30 : m_fpsLimit);
                    aurora_enable_vsync(m_vsync);
                    SaveSettings();
                    return;
                }
                // HUD FPS click
                float hudX = syncColX + sChipW + 10.0f;
                if (px >= hudX && px <= hudX + sChipW) {
                    m_showFpsHud = !m_showFpsHud;
                    SaveSettings();
                    return;
                }
            }

            // Card 4: PC renderer or Android HD textures.
            float c4Y = c3Y + 114.0f + 12.0f;
#ifdef MH3_PC_TOUCH_UI
            const float rendererW=(contentW-46)*.5f;
            if(py>=c4Y+54 && py<=c4Y+96){
                for(int i=0;i<2;++i){
                    const float rx=x+18+i*(rendererW+10);
                    if(px>=rx && px<=rx+rendererW){m_rendererIndex=i;SaveSettings();return;}
                }
            }
#else
            float tBtnW = (contentW - 36.0f - 15.0f) * 0.50f;
            float tBtnY = c4Y + 54.0f;
            if (py >= tBtnY && py <= tBtnY + chipH) {
                if (px >= x + 18.0f && px <= x + 18.0f + tBtnW) {
#ifndef MH3_PC_TOUCH_UI
                    bool cur = aurora::gfx::texture_replacement::is_enabled();
#endif
#ifndef MH3_PC_TOUCH_UI
                    aurora::gfx::texture_replacement::set_enabled(!cur);
#endif
                    return;
                } else if (px >= x + 18.0f + tBtnW + 15.0f && px <= x + 18.0f + tBtnW * 2.0f + 15.0f) {
#ifndef MH3_PC_TOUCH_UI
                    aurora::gfx::texture_replacement::reload();
#endif
                    return;
                }
            }
#endif
        } else if (m_menuTab == 6) {
            int row=0;
            for(int i=0;i<3;++i) {
#ifndef __ANDROID__
                if(i==1)continue;
#endif
                const float top=contentY+row++*128.0f;
                if(px>=x+contentW-124 && px<=x+contentW-18 && py>=top+12 && py<=top+48) {
                    if(i==0)RowellFeatures::cutsceneSkip.store(!RowellFeatures::cutsceneSkip.load());
                    if(i==1)RowellFeatures::androidKeyboard.store(!RowellFeatures::androidKeyboard.load());
                    if(i==2)RowellFeatures::physicsLog.store(!RowellFeatures::physicsLog.load());
                    m_featureStatus=i==2?"Diagnostics are for this app session only.":RowellFeatures::Save()?"Saved.":"Applied for this session; could not save the option.";
                    return;
                }
            }
        } else if (m_menuTab == 1) { // Sistema
#ifdef MH3_PC_TOUCH_UI
            const float foldersY=contentY+420,folderW=(contentW-46)*.5f;
            if(py>=foldersY && py<=foldersY+44){
                if(px>=x+18 && px<=x+18+folderW){RowellTouch_OpenDataFolder();return;}
                if(px>=x+28+folderW && px<=x+28+folderW*2){RowellTouch_OpenAppFolder();return;}
            }
#endif
            const float speedY = contentY + 270.0f;
            if (py >= speedY + 62.0f && py <= speedY + 102.0f) {
                const float speedW = (contentW - 56.0f) / 3.0f;
                const int speedValues[] = {100, 125, 150};
                for (int i = 0; i < 3; ++i) {
                    const float sx = x + 18.0f + i * (speedW + 10.0f);
                    if (px >= sx && px <= sx + speedW) { MH3GameSpeed::Select(speedValues[i]); return; }
                }
            }
            float chipY = contentY + 60.0f;
            float chipW = (contentW - 36.0f - 20.0f) / 5.0f;
            float chipH = 44.0f;
            if (py >= chipY && py <= chipY + chipH) {
                for (int i = 0; i < 5; ++i) {
                    float lx = x + 18.0f + float(i) * (chipW + 5.0f);
                    if (px >= lx && px <= lx + chipW) {
                        m_langChoice = i;
                        SaveLanguage();
                        return;
                    }
                }
            }

            // Restart Button
            float c2Y = contentY + 130.0f + 12.0f + 28.0f;
            float rBtnW = 200.0f;
            float rBtnX = x + contentW - rBtnW - 18.0f;
            float rBtnY = c2Y + 22.0f;
            if (px >= rBtnX && px <= rBtnX + rBtnW && py >= rBtnY && py <= rBtnY + 44.0f) {
                SaveSettings();
                SaveLanguage();
                if(!RequestGameRestart())m_saveStatusMsg="Could not restart. Close and reopen the app.";
                return;
            }
        } else if (m_menuTab == 2) { // Guardados
            float btnW = (contentW - 36.0f - 20.0f) / 3.0f;
            float btnH = 50.0f;
            float btnY = contentY + 70.0f;

            float b1X = x + 18.0f;
            float b2X = b1X + btnW + 10.0f;
            float b3X = b2X + btnW + 10.0f;

            if (py >= btnY && py <= btnY + btnH) {
                if (px >= b1X && px <= b1X + btnW) {
                    m_pendingSaveAction = SaveAction::Export;
                    return;
                } else if (px >= b2X && px <= b2X + btnW) {
                    OpenSaveBrowser();
                    return;
                } else if (px >= b3X && px <= b3X + btnW) {
                    m_pendingSaveAction = SaveAction::Backup;
                    return;
                }
            }
        } else if (m_menuTab == 3) { // Controles
#ifdef MH3_PC_TOUCH_UI
            if(px>=x+contentW-228 && px<=x+contentW-18 && py>=contentY+12 && py<=contentY+44) {
                m_keyboardEditorOpen=true;m_captureAction=-1;ResetInput();return;
            }
#endif
            // Card 1: Touch Visibility & Opacity
            float c1H = 176.0f;
            float tvChipW = (contentW - 36.0f - 15.0f) / 2.0f;
            float tvChipH = 38.0f;
            float tvChipY = contentY + 54.0f;

            float tv1X = x + 18.0f;
            float tv2X = tv1X + tvChipW + 15.0f;
            if (py >= tvChipY && py <= tvChipY + tvChipH) {
                if (px >= tv1X && px <= tv1X + tvChipW) {
                    m_touchVisible = true;
                    SaveLayoutConfig();
                    return;
                } else if (px >= tv2X && px <= tv2X + tvChipW) {
                    m_touchVisible = false;
                    ResetInput(); SaveLayoutConfig();
                    return;
                }
            }

            // Opacity chips
            float opChipW = (contentW - 36.0f - 30.0f) / 4.0f;
            float opChipH = 36.0f;
            float opChipY = contentY + 124.0f;
            const float opValues[] = { 0.25f, 0.50f, 0.75f, 1.00f };
            if (py >= opChipY && py <= opChipY + opChipH) {
                for (int i = 0; i < 4; ++i) {
                    float ox = x + 18.0f + float(i) * (opChipW + 10.0f);
                    if (px >= ox && px <= ox + opChipW) {
                        m_buttonOpacity = opValues[i];
                        SaveLayoutConfig();
                        return;
                    }
                }
            }

            // Card 2: Gamepad Layout & Swap Triggers
            float c2Y = contentY + c1H + 12.0f;
            float c2H = 116.0f;
            float lChipW = (contentW * 0.60f - 36.0f - 10.0f) / 2.0f;
            float gpChipH = 42.0f;
            float gpChipY = c2Y + 58.0f;

            float l1X = x + 18.0f;
            float l2X = l1X + lChipW + 10.0f;
            float trigX = x + contentW * 0.62f;
            float trigW = contentW * 0.38f - 18.0f;

            if(py>=c2Y+10 && py<=c2Y+40 && px>=x+contentW-228 && px<=x+contentW-18) {
                m_padEditorOpen=true;m_padPage=0;m_padStatus="Choose a Wii action, then press a controller button.";return;
            }
            if (py >= gpChipY && py <= gpChipY + gpChipH) {
                if (px >= l1X && px <= l1X + lChipW) {
                    RequestPadPreset(0);
                    return;
                } else if (px >= l2X && px <= l2X + lChipW) {
                    RequestPadPreset(1);
                    return;
                } else if (px >= trigX && px <= trigX + trigW) {
                    RequestPadPreset(2);
                    return;
                }
            }

            const float c3Y=c2Y+c2H+12,bw=(contentW-51)*.5f,by=c3Y+64;
            if(py>=by && py<=by+38) {
                if(px>=x+18 && px<=x+18+bw)BeginLayoutEdit();
                else if(px>=x+33+bw && px<=x+33+bw*2)m_menuTab=5;
                return;
            }
        } else if (m_menuTab == 4) {
#ifndef MH3_PC_TOUCH_UI // Online / Multijugador
            HandleTabOnlineTouch(px, py, x, contentY, contentW);
        }
    }

    void HandleTabOnlineTouch(float px, float py, float x, float contentY, float contentW) {
        float mChipW = (contentW - 36.0f - 12.0f) / 2.0f;
        float mChipH = 42.0f;
        float mChipY = contentY + 54.0f;
        if (py >= mChipY && py <= mChipY + mChipH) {
            float c0X = x + 18.0f;
            float c1X = c0X + mChipW + 12.0f;
            if (px >= c0X && px <= c0X + mChipW) {
                m_onlineMode = 3;
                LocalOnlineServer::SetMode(LocalOnlineServer::Mode::Community, "mh3sp.com");
                m_onlineStatusMsg = "Community server mh3sp.com is selected.";
                return;
            }
            if (px >= c1X && px <= c1X + mChipW) {
                m_onlineMode = 0;
                LocalOnlineServer::SetMode(LocalOnlineServer::Mode::Offline);
                m_onlineStatusMsg = "Offline mode. Play in Moga Village.";
                return;
            }
#endif
        }
    }


    void HandleEditorDialogTouch(float px,float py) {
        const float w=std::min(600.0f,float(m_width)-48),x=(m_width-w)*.5f,y=(m_height-330)*.5f;
        const float choiceW=(w-64)*.5f;
        if(m_editorDeleteSlot>=0) {
            if(py<y+266 || py>y+308)return;
            if(px>=x+24 && px<=x+24+choiceW){m_editorDeleteSlot=-1;return;}
            if(px<x+40+choiceW || px>x+w-24)return;
            const int slot=m_editorDeleteSlot;
            if(slot<5 && m_comboMasks[slot]) {
                CheckpointLayout();m_comboMasks[slot]=0;ComboEnabled(slot)=false;
                ApplyComboMasks();RecalculateButtonPositions();ResetInput();m_selectedButtonIndex=-1;
                m_editorStatus="Combo removed. Undo restores it; Save & play keeps the change.";
            }
            m_editorDeleteSlot=-1;m_editorComboSlot=-1;return;
        }
        if(m_editorComboSlot>=0) {
            const uint16_t bits[]={MH3_CL_A,MH3_CL_B,MH3_CL_X,MH3_CL_Y,MH3_CL_L,MH3_CL_R,MH3_CL_ZL,MH3_CL_ZR};
            const float bw=(w-78)/4;
            for(int i=0;i<8;++i) {
                const float bx=x+24+(i%4)*(bw+10),by=y+96+(i/4)*58;
                if(px>=bx && px<=bx+bw && py>=by && py<=by+46){m_editorComboMask^=bits[i];return;}
            }
            if(!m_editorComboNew && py>=y+214 && py<=y+246) {
                if(px>=x+24 && px<=x+w-180){m_editorComboVisible=!m_editorComboVisible;return;}
                if(px>=x+w-160 && px<=x+w-24){RequestComboDelete(m_editorComboSlot);return;}
            }
        }
        if(py<y+266 || py>y+308)return;
        if(px>=x+24 && px<=x+24+choiceW){m_editorComboSlot=-1;m_editorResetConfirm=false;return;}
        if(px<x+40+choiceW || px>x+w-24)return;
        if(m_editorResetConfirm) {
            CheckpointLayout();InitDefaultButtons();m_normLeftStickPos={.12f,.76f};m_leftStickScale=1;
            ApplyComboMasks();RecalculateButtonPositions();m_selectedButtonIndex=-1;m_editorResetConfirm=false;
        } else if(m_editorComboSlot>=0 && m_editorComboMask) {
            CheckpointLayout();const int slot=m_editorComboSlot;
            m_comboMasks[slot]=m_editorComboMask;ComboEnabled(slot)=m_editorComboVisible;ApplyComboMasks();
            for(int i=0;i<int(m_buttons.size());++i)if(m_buttons[i].id=="M"+std::to_string(slot+1)) {
                m_selectedButtonIndex=m_editorComboVisible?i:-1;
                if(m_editorComboNew){m_buttons[i].normPos={.50f,.56f};m_buttons[i].scale=1;}
                break;
            }
            RecalculateButtonPositions();m_editorComboSlot=-1;m_editorStatus="Combo ready. Save & play keeps it; Cancel discards changes.";
        }
    }
    void HandleEditModeTouchDown(SDL_FingerID id, float px, float py) {
        if(m_editorComboSlot>=0 || m_editorResetConfirm || m_editorDeleteSlot>=0){HandleEditorDialogTouch(px,py);return;}
        if(m_editDragging && m_editFingerId!=id)return;
        const auto size=EditorPanelSize(),panel=EditorPanelPosition();
        const float x=panel.x,y=panel.y,w=size.x;
        if(px>=x && px<=x+w && py>=y && py<=y+size.y) {
            if(py<=y+42) {
                if(px>=x+w-46){m_editorCollapsed=!m_editorCollapsed;return;}
                m_editorPanelDragging=true;m_editDragging=true;m_editFingerId=id;
                m_editorDragOffset={px-x,py-y};return;
            }
            if(py>=y+78 && py<=y+118) {
                const float actionW=(w-140)*.5f;
                if((px>=x+14 && px<=x+58) || (px>=x+66 && px<=x+110)) {
                    if(m_selectedButtonIndex==-2 || m_selectedButtonIndex>=0) {
                        CheckpointLayout();AdjustSelectedButtonScale(px<x+62?-.10f:.10f);
                    } else m_editorStatus="Select a control before changing its size.";
                } else if(px>=x+118 && px<=x+118+actionW)OpenEditorCombo(true);
                else if(px>=x+126+actionW && px<=x+w-14)RequestComboDelete();
                return;
            }
            if(py>=y+128 && py<=y+168) {
                const float cw=(w-44)/3;
                if(px<=x+14+cw)OpenEditorCombo(false);
                else if(px<=x+22+cw*2) {
                    if(!m_editorUndo.empty()) {
                        const auto previous=m_editorUndo.back();m_editorUndo.pop_back();RestoreLayout(previous);
                        m_editorStatus="Last layout change undone.";
                    } else m_editorStatus="No layout change to undo.";
                } else m_editorResetConfirm=true;
                return;
            }
            if(py>=y+222 && py<=y+262) {
                if(px<x+w*.5f)CancelLayoutEdit();
                else if(SaveLayoutConfig()) {
                    m_editMode=false;m_editorFromLibrary=false;m_selectedButtonIndex=-1;m_editorUndo.clear();m_editorOriginal.reset();ResetInput();
                } else m_editorStatus="Could not save layout. Check available storage, then try again.";
            }
            return;
        }
        const float dx=px-m_leftStickCenter.x,dy=py-m_leftStickCenter.y;
        if(dx*dx+dy*dy<=m_leftStickRadius*m_leftStickRadius*1.69f) {
            CheckpointLayout();m_selectedButtonIndex=-2;m_editDragging=true;m_editFingerId=id;return;
        }
        if(auto* button=HitButton(px,py)) {
            CheckpointLayout();m_selectedButtonIndex=int(button-m_buttons.data());
            m_editDragging=true;m_editFingerId=id;
        }
    }

    void AdjustSelectedButtonScale(float delta) {
        if (m_selectedButtonIndex >= 0 && m_selectedButtonIndex < int(m_buttons.size())) {
            auto& btn = m_buttons[m_selectedButtonIndex];
            btn.scale = std::clamp(btn.scale + delta, 0.50f, 2.0f);
            btn.radius = btn.defaultNormRadius * m_unit * btn.scale;
        } else if (m_selectedButtonIndex == -2) {
            m_leftStickScale = std::clamp(m_leftStickScale + delta, 0.50f, 2.0f);
            m_leftStickRadius = m_unit * 0.15f * m_leftStickScale;
            m_leftStickKnobRadius = m_unit * 0.075f * m_leftStickScale;
        }
    }

    // -------------------------------------------------------------
    // Save, Language, & Graphics Management
    // -------------------------------------------------------------
    void ApplyResolutionScale(int idx) {
        constexpr auto scales = LiteProfile::ResolutionScales;
        idx = std::clamp(idx, 0, 5);
#ifdef MH3_PC_TOUCH_UI
        RowellTouch_SetResolutionScale(scales[idx]);
#else
        VISetFrameBufferScale(scales[idx]);
#endif
    }

    void ApplyAspectRatio(int idx) {
        idx = std::clamp(idx, 0, 2);
        const int nativeAspect=idx==0?2:idx==1?0:1;
#ifdef MH3_PC_TOUCH_UI
        RowellTouch_SetTargetAspect(nativeAspect);
#else
        aurora_set_native_target_aspect(nativeAspect);
#endif

        SetWidescreenSetting(idx != 1);
#if defined(__cpp_exceptions)
        try {
#endif
            if (Memory::Contains(0x80797F68, 4) && Memory::Contains(0x8065B4C8, 4)) {
                uint32_t aspectVal = Memory::Read32(idx != 1 ? 0x80797F68 : 0x80797F70);
                if (aspectVal != 0) {
                    Memory::Write32(0x8065B4C8, aspectVal);
                    Memory::Write8(0x8065B4D5, idx != 1 ? 1 : 0);
                }
            }
#if defined(__cpp_exceptions)
        } catch (...) {}
#endif
    }

    void LoadSettings() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::string dir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::string gfxPath = dir + "/MH3UI.graphics";

#ifdef MH3_PC_TOUCH_UI
        const auto settings=RowellTouch_ReadGraphicsSettings();
        m_fullscreen=settings.fullscreen;
        m_vsync=settings.vsync;
        m_resolutionIndex=settings.resolution;
        m_aspectIndex=settings.aspect;
        m_fpsLimit=settings.fps;
        m_showFpsHud=settings.showFps;
        m_rendererIndex=m_startupRendererIndex=settings.renderer;
#else
        std::ifstream in(gfxPath);
        int msaa = 1, vsync = 1, resIdx = LiteProfile::DefaultResolution, fpsLimit = LiteProfile::DefaultFps, aspect = 0, showFps = 1, fullscreen = 0;
        if (in >> msaa >> vsync >> resIdx >> fpsLimit >> aspect >> showFps) {
            in >> fullscreen;
            m_fullscreen = (fullscreen != 0);
            m_vsync = (vsync != 0);
            m_resolutionIndex = std::clamp(resIdx, 0, 5);
            m_aspectIndex = std::clamp(aspect, 0, 2);
            if (fpsLimit == 30 || fpsLimit == 60 || fpsLimit == 90 || fpsLimit == 120) {
                m_fpsLimit = fpsLimit;
            } else {
                m_fpsLimit = LiteProfile::DefaultFps;
            }
            m_showFpsHud = (showFps != 0);
        } else {
            m_vsync = true;
            m_resolutionIndex = LiteProfile::DefaultResolution; // Balanced subnative resolution for this separate Lite application.
            m_aspectIndex = 0;      // 0: 16:9 Widescreen by default!
            m_fpsLimit = LiteProfile::DefaultFps;
            m_showFpsHud = true;
            m_fullscreen = false;
        }
#endif

        ApplyResolutionScale(m_resolutionIndex);
        if (m_fullscreen) {
            ApplyFullscreen(true);
        } else {
            ApplyAspectRatio(m_aspectIndex);
        }
        MH3FrameRateControl::SetTargetFPS(m_fpsLimit);
        aurora_enable_vsync(m_vsync);

        std::string langPath = dir + "/MH3UI.language";
        std::ifstream langIn(langPath);
        std::string code;
        if (langIn >> code && !code.empty()) {
            if (code == "es") m_langChoice = 0;
            else if (code == "en") m_langChoice = 1;
            else if (code == "fr") m_langChoice = 2;
            else if (code == "de") m_langChoice = 3;
            else if (code == "it") m_langChoice = 4;
            else m_langChoice = 1;
        } else {
            m_langChoice = 1; // English for new development installations.
        }

#ifndef MH3_PC_TOUCH_UI
        m_onlineMode = static_cast<int>(LocalOnlineServer::GetMode());
#endif
        if (m_onlineMode != 0) m_onlineMode = 3;
        m_onlineHostIp = "mh3sp.com";
        m_onlineStatusMsg = (m_onlineMode == 0)
            ? "Offline mode. Play in Moga Village."
            : "Community server mh3sp.com is selected.";
    }

    void SaveSettings() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::string dir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::string gfxPath = dir + "/MH3UI.graphics";

        std::ofstream out(gfxPath, std::ios::trunc);
        if (out) {
            out << "1 " << (m_vsync ? 1 : 0) << ' ' << m_resolutionIndex << ' '
                << m_fpsLimit << ' ' << m_aspectIndex << ' ' << (m_showFpsHud ? 1 : 0) << ' '
                << (m_fullscreen ? 1 : 0);
#ifdef MH3_PC_TOUCH_UI
            out << ' ' << m_rendererIndex;
#endif
            out << '\n';
        }
    }

    void SaveLanguage() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::string dir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::string langPath = dir + "/MH3UI.language";

        const char* codes[] = { "es", "en", "fr", "de", "it" };
        std::ofstream out(langPath, std::ios::trunc);
        if (out && m_langChoice >= 0 && m_langChoice < 5) {
            out << codes[m_langChoice] << '\n';
        }
    }

    bool SaveLayoutConfig() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::string dir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::string cfgPath = dir + "/touch_layout.cfg";

        std::ofstream out(cfgPath, std::ios::trunc);
        if (!out) return false;

        out << "VERSION 1\n";
        out << "EDITOR_PANEL " << m_editorPanelNorm.x << ' ' << m_editorPanelNorm.y << ' ' << (m_editorCollapsed?1:0) << '\n';
        out << "TOUCH_VISIBLE " << (m_touchVisible ? 1 : 0) << '\n';
        out << "OPACITY " << m_buttonOpacity << '\n';
        out << "MACRO1 " << (m_macro1Enabled ? 1 : 0) << '\n';
        out << "MACRO2 " << (m_macro2Enabled ? 1 : 0) << '\n';
        out << "MACRO3 " << (m_macro3Enabled ? 1 : 0) << '\n';
        out << "MACRO4 " << (m_macro4Enabled ? 1 : 0) << '\n';
        out << "MACRO5 " << (m_macro5Enabled ? 1 : 0) << '\n';
        for (int i=0;i<5;++i) out << "COMBO " << i << ' ' << m_comboMasks[i] << '\n';
        out << "LEFT_STICK " << m_normLeftStickPos.x << ' ' << m_normLeftStickPos.y << ' ' << m_leftStickScale << '\n';

        for (const auto& btn : m_buttons) {
            out << "BTN " << btn.id << ' ' << btn.normPos.x << ' ' << btn.normPos.y << ' ' << btn.scale << '\n';
        }
        out.close();
        return bool(out);
    }

    void LoadLayoutConfig() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::string dir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::string cfgPath = dir + "/touch_layout.cfg";

        std::ifstream in(cfgPath);
        if (!in) {
            // New profiles reserve slots only for visible defaults. Existing files
            // keep all their saved/hidden definitions and original slot identities.
            for(int i=0;i<5;++i)if(!ComboEnabled(i) && !ComboHasShortcut(i))m_comboMasks[i]=0;
            InitDefaultButtons();ApplyComboMasks();
            m_normLeftStickPos = { 0.12f, 0.76f };m_leftStickScale=1;
            return;
        }

        InitDefaultButtons();
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream iss(line);
            std::string tag;
            if (!(iss >> tag)) continue;
            if (tag == "EDITOR_PANEL") {
                float x,y;int collapsed;
                if(iss>>x>>y>>collapsed && std::isfinite(x) && std::isfinite(y)) {
                    m_editorPanelNorm={std::clamp(x,0.0f,1.0f),std::clamp(y,0.0f,1.0f)};m_editorCollapsed=collapsed!=0;
                }
            }
            else if (tag == "TOUCH_VISIBLE") { int v; iss >> v; m_touchVisible = (v != 0); }
            else if (tag == "OPACITY") { iss >> m_buttonOpacity; m_buttonOpacity = std::clamp(m_buttonOpacity, 0.10f, 1.0f); }
            else if (tag == "MACRO1") { int v; iss >> v; m_macro1Enabled = (v != 0); }
            else if (tag == "MACRO2") { int v; iss >> v; m_macro2Enabled = (v != 0); }
            else if (tag == "MACRO3") { int v; iss >> v; m_macro3Enabled = (v != 0); }
            else if (tag == "MACRO4") { int v; iss >> v; m_macro4Enabled = (v != 0); }
            else if (tag == "MACRO5") { int v=1; if (iss>>v) m_macro5Enabled=(v!=0); }
            else if (tag == "COMBO") { int i; unsigned mask; if (iss>>i>>mask && i>=0 && i<5 && mask<=65535) m_comboMasks[i]=uint16_t(mask); }
            else if (tag == "LEFT_STICK") {
                iss >> m_normLeftStickPos.x >> m_normLeftStickPos.y >> m_leftStickScale;
            } else if (tag == "BTN") {
                std::string id;
                float nx, ny, sc;
                if (iss >> id >> nx >> ny >> sc) {
                    for (auto& btn : m_buttons) {
                        if (btn.id == id) {
                            if (std::isfinite(nx) && std::isfinite(ny) && std::isfinite(sc)) {
                                btn.normPos = { std::clamp(nx,0.02f,0.98f), std::clamp(ny,0.02f,0.98f) };
                                btn.scale = std::clamp(sc,0.4f,2.5f);
                            }
                            break;
                        }
                    }
                }
            }
        }
        ApplyComboMasks();
    }

    void ExportSaveFiles() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::filesystem::path userDir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::filesystem::path saveData = userDir / "nand/title/00010000/524d4850/data";
        std::filesystem::path exportParent = userDir / "exports";

        std::error_code ec;
        std::filesystem::create_directories(exportParent, ec);

        auto res = mh3_save_manager::ExportSave(saveData, exportParent);
        if (res.success) {
            m_saveStatusMsg = "Export created in MH3_Low_Spec/exports. Transfer the whole exported folder.";
        } else {
            m_saveStatusMsg = "Export failed: " + res.detail;
        }
    }

    void ImportSaveFiles(const std::filesystem::path& candidateFolder) {
        if (candidateFolder.empty()) {
            m_saveStatusMsg = "No import was confirmed. Your save was not changed.";
            return;
        }
        const char* user = std::getenv("MH3_USER_DIR");
        const std::filesystem::path userDir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        auto stageRes = mh3_save_manager::QueueRestore(candidateFolder,userDir);
        if (!stageRes.success) {
            m_saveStatusMsg = "Import failed: " + stageRes.detail;
            return;
        }
        m_restoreRestarting=true;ResetInput();
        m_saveStatusMsg="Restore prepared. Restarting the game...";
        if(!RequestGameRestart()) {m_restoreRestarting=false;m_saveStatusMsg="Restore prepared. Close and reopen the app to finish.";}
    }

    static std::string BackupDisplayTime(const std::filesystem::path& folder) {
        const auto value=mh3_save_manager::CopyCreatedTime(folder);
        return value=="Creation time not recorded"?std::string():value;
    }

    void RefreshLastBackup() {
        const char* user = std::getenv("MH3_USER_DIR");
        const std::filesystem::path dir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::error_code ec;
        std::filesystem::path newest;
        std::filesystem::directory_iterator it(dir / "saves", ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code entryError;
            if (!BackupDisplayTime(it->path()).empty() &&
                std::filesystem::is_directory(it->path() / "data", entryError) && !entryError &&
                (newest.empty() || it->path().filename() > newest.filename())) newest = it->path();
        }
        m_lastBackupTime = newest.empty() ? "None yet" : BackupDisplayTime(newest);
        if(!m_restoreStatusRead) {
            std::ifstream status(dir/"restore-status.txt");std::string line;
            if(std::getline(status,line) && !line.empty())m_saveStatusMsg=line;
            m_restoreStatusRead=true;
        }
    }

    void CreateQuickBackup() {
        const char* user = std::getenv("MH3_USER_DIR");
        std::filesystem::path userDir = (user && *user) ? user : "/storage/emulated/0/MH3_Low_Spec";
        std::filesystem::path saveData = userDir / "nand/title/00010000/524d4850/data";
        std::filesystem::path backupsRoot = userDir / "saves";

        auto res = mh3_save_manager::CreateBackup(saveData, backupsRoot);
        if (res.success) {
            m_saveStatusMsg = res.detail=="cleanup-incomplete"?"Backup verified, but older copies could not all be removed.":"Backup created. Keeping up to 3 recovery copies.";
            m_lastBackupTime = BackupDisplayTime(res.output);
        } else {
            m_saveStatusMsg = "Backup failed: " + res.detail;
        }
    }

    // State Variables
    bool m_initialized = false;
    std::string m_featureStatus;
    bool m_enabled = false;
    int m_width = 0;
    int m_height = 0;
    float m_unit = 0.0f;
    std::array<float,4> m_labelBox{};

    // Fixed Left Stick
    Vec2 m_normLeftStickPos{ 0.12f, 0.76f };
    float m_leftStickScale = 1.0f;
    Vec2 m_leftStickCenter{ 0, 0 };
    Vec2 m_leftStickPos{ 0, 0 };
    float m_leftStickRadius = 0.0f;
    float m_leftStickKnobRadius = 0.0f;
    std::optional<SDL_FingerID> m_leftFingerId;
    int16_t m_leftX = 0;
    int16_t m_leftY = 0;

    // Free Floating Right Stick
    bool m_rightStickActive = false;
    Vec2 m_rightFloatingOrigin{ 0, 0 };
    Vec2 m_rightStickPos{ 0, 0 };
    Vec2 m_rightLastPos{ 0, 0 };
    float m_rightStickRadius = 0.0f;
    float m_rightStickKnobRadius = 0.0f;
    std::optional<SDL_FingerID> m_rightFingerId;
    int16_t m_rightX = 0;
    int16_t m_rightY = 0;
    std::atomic<float> m_cameraDeltaX{ 0.0f };
    std::atomic<float> m_cameraDeltaY{ 0.0f };
    std::atomic<uint32_t> m_latchedMask{ 0 };

    // Buttons
    std::vector<ConfigurableButton> m_buttons;
    Vec2 m_gearPos{ 0, 0 };
    float m_gearRadius = 0.0f;

    // Settings Menu & Options State
    bool m_menuOpen = false;
    int m_menuTab = 3;
    int m_resolutionIndex = LiteProfile::DefaultResolution; // 0: Wii Original (1x)
    int m_aspectIndex = 1;     // 16:9 Widescreen
    bool m_fullscreen = false;
    bool m_vsync = true;
    int m_fpsLimit = LiteProfile::DefaultFps;
    bool m_showFpsHud = true;
    bool m_padEditorOpen=false;
    int m_padPage=0,m_padCaptureAction=-1,m_padConflictSource=-1,m_padConflictOwner=-1,m_padPendingPreset=-1;
    RowellPad::Capture m_padCapture;
    SDL_JoystickID m_padCaptureDevice=0;
    std::string m_padStatus="Touch controls and PC keyboard bindings stay unchanged.";
    bool m_touchVisible = true;
    float m_buttonOpacity = 1.0f;
    int m_langChoice = 1;      // 1: English
    double m_liveFps = 60.0;
    std::string m_saveStatusMsg = "Only this development app saves are used.";
    SaveAction m_pendingSaveAction = SaveAction::None;
    std::filesystem::path m_pendingImportFolder;
    std::string m_pendingImportLabel;
    bool m_saveBrowserOpen=false,m_browseBackups=true,m_restoreRestarting=false,m_restoreStatusRead=false;
    int m_savePage=0,m_selectedSave=-1;
    std::vector<mh3_save_manager::SaveCopy> m_saveCopies;
#ifdef MH3_PC_TOUCH_UI
    int m_rendererIndex=0,m_startupRendererIndex=0;
    bool m_keyboardEditorOpen=false;
    int m_keyPage=0,m_captureAction=-1,m_captureSlot=0;
    std::string m_keyStatus="F2 opens settings, F3 changes solo speed, and Escape returns. Reset is always available with the mouse.";
#endif
    std::string m_lastBackupTime = "None yet";
    int m_onlineMode = 3;
    std::string m_onlineHostIp = "mh3sp.com";
    std::string m_onlineStatusMsg = "Community server mh3sp.com is selected.";

    // Macros & Edit Mode
    bool m_macro1Enabled = true;
    bool m_macro2Enabled = false;
    bool m_macro3Enabled = false;
    bool m_macro4Enabled = false;
    bool m_macro5Enabled = true;
    bool m_editorFromLibrary=false,m_editorComboVisible=true;
    int m_editorDeleteSlot=-1;
    std::array<uint16_t, 5> m_comboMasks{{MH3_CL_A|MH3_CL_X, MH3_CL_B|MH3_CL_Y,
        MH3_CL_L|MH3_CL_R, MH3_CL_A|MH3_CL_B, MH3_CL_R|MH3_CL_X|MH3_CL_A}};
    bool m_editMode = false;
    std::optional<EditSnapshot> m_editorOriginal;
    std::vector<EditSnapshot> m_editorUndo;
    Vec2 m_editorPanelNorm{.5f,.02f},m_editorDragOffset{};
    bool m_editorCollapsed=false,m_editorPanelDragging=false,m_editorResetConfirm=false,m_editorComboNew=false;
    int m_editorComboSlot=-1;
    uint16_t m_editorComboMask=0;
    std::string m_editorStatus="Drag a control. Drag this panel by its title.";
    int m_selectedButtonIndex = -1;
    bool m_editDragging = false;
    std::optional<SDL_FingerID> m_editFingerId;

    // GL Resources
    GLuint m_shaderProgram = 0;
    GLint m_uScreenSize = -1;
    GLint m_uFontTex = -1;
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLuint m_fontTex = 0;

    mh3_touch::Contacts m_buttonGestures;
    mh3_touch::Contacts m_speedGestures;
    std::vector<UIVertex> m_verts;
};

TouchSettingsEngine g_touchSettingsEngine;
std::recursive_mutex g_touchMutex;

#ifndef MH3_TOUCH_INPUT_TESTS
bool SDLCALL TouchSettingsEventWatcher(void* userdata, SDL_Event* event) {
    (void)userdata;
    TouchControls_HandleSdlEvent(event);
    return true;
}
#endif

} // namespace

extern "C" {

#ifdef MH3_TOUCH_INPUT_TESTS
bool TouchControls_RunInputChecks(void) {
    std::lock_guard lock(g_touchMutex);
    return g_touchSettingsEngine.RunInputChecks();
}
#else
void TouchControls_Init(void) {
    std::lock_guard lock(g_touchMutex);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    g_touchSettingsEngine.Init();
    static bool watcherAdded = false;
    if (!watcherAdded) {
        SDL_AddEventWatch(TouchSettingsEventWatcher, nullptr);
        watcherAdded = true;
    }
}

bool TouchControls_RunInputChecks(void) {
    std::lock_guard lock(g_touchMutex);
    return g_touchSettingsEngine.RunInputChecks();
}

bool TouchControls_WantsPointer(void) {
    std::lock_guard lock(g_touchMutex);
    return g_touchSettingsEngine.WantsPointer();
}

void TouchControls_SetEnabled(bool enabled) {
    std::lock_guard lock(g_touchMutex);
    g_touchSettingsEngine.SetEnabled(enabled);
}

bool TouchControls_IsEnabled(void) {
    std::lock_guard lock(g_touchMutex);
    return g_touchSettingsEngine.IsEnabled();
}

void TouchControls_HandleSdlEvent(const void* sdlEvent) {
    std::lock_guard lock(g_touchMutex);
    if (!sdlEvent) return;
#ifdef __ANDROID__
    if (AndroidKeyboard_HandleSdlEvent(sdlEvent)) return;
#endif
    const SDL_Event* event = reinterpret_cast<const SDL_Event*>(sdlEvent);
#ifdef __ANDROID__
    if(event->type==SDL_EVENT_FINGER_DOWN && AndroidKeyboard_ShouldBeActive() && event->tfinger.y<0.65f) {
        g_touchSettingsEngine.ResetInput();
        AndroidKeyboard_OnScreenTouch();
        return;
    }
#endif

    float pointerScaleX=1, pointerScaleY=1;
    if (event->type==SDL_EVENT_MOUSE_BUTTON_DOWN || event->type==SDL_EVENT_MOUSE_BUTTON_UP || event->type==SDL_EVENT_MOUSE_MOTION) {
        if(auto* window=aurora_get_window()) {
            int ww=0,wh=0,pw=0,ph=0; SDL_GetWindowSize(window,&ww,&wh); SDL_GetWindowSizeInPixels(window,&pw,&ph);
            if(ww>0 && wh>0) { pointerScaleX=720.0f/float(ph)*float(pw)/float(ww); pointerScaleY=720.0f/float(wh); }
        }
    }
    switch (event->type) {
    case SDL_EVENT_KEY_DOWN: {
#ifdef MH3_PC_TOUCH_UI
        if(!event->key.repeat && g_touchSettingsEngine.HandleKeyboardEditorKey(event->key.scancode))break;
#endif
        if (g_touchSettingsEngine.IsModalOpen()) {
            if (!event->key.repeat && (event->key.key == SDLK_ESCAPE || event->key.key == SDLK_AC_BACK))
                g_touchSettingsEngine.CancelModal();
            break;
        }
        if (!event->key.repeat && event->key.key ==
#ifdef MH3_PC_TOUCH_UI
            SDLK_F2
#else
            SDLK_F1
#endif
        ) {
            g_touchSettingsEngine.ToggleMenu();
        } else if (!event->key.repeat && event->key.key == SDLK_F3 && !g_touchSettingsEngine.IsMenuOpen()) {
            MH3GameSpeed::Cycle();
        } else if (event->key.key == SDLK_ESCAPE) {
            if (g_touchSettingsEngine.IsMenuOpen()) {
                g_touchSettingsEngine.SetMenuOpen(false);
            }
        }
#ifndef MH3_PC_TOUCH_UI
        else if (event->key.key == SDLK_F11 ||
                   ((event->key.mod & SDL_KMOD_ALT) && (event->key.key == SDLK_RETURN || event->key.key == SDLK_KP_ENTER))) {
            g_touchSettingsEngine.ToggleFullscreen();
        }
#endif
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_touchSettingsEngine.HandleMouseDown(event->button.x*pointerScaleX, event->button.y*pointerScaleY);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_touchSettingsEngine.HandleMouseUp(event->button.x*pointerScaleX, event->button.y*pointerScaleY);
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
        g_touchSettingsEngine.HandleMouseMove(event->motion.x*pointerScaleX, event->motion.y*pointerScaleY);
        break;
    case SDL_EVENT_FINGER_DOWN:
        g_touchSettingsEngine.HandleTouchDown(event->tfinger.fingerID, event->tfinger.x, event->tfinger.y);
        break;
    case SDL_EVENT_FINGER_MOTION:
        g_touchSettingsEngine.HandleTouchMove(event->tfinger.fingerID, event->tfinger.x, event->tfinger.y);
        break;
    case SDL_EVENT_FINGER_UP:
        g_touchSettingsEngine.HandleTouchUp(event->tfinger.fingerID);
        break;
    case SDL_EVENT_FINGER_CANCELED:
        g_touchSettingsEngine.CancelTouch(event->tfinger.fingerID);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        g_touchSettingsEngine.UpdateLayout(event->window.data1, event->window.data2);
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        g_touchSettingsEngine.ResetInput();
        g_touchSettingsEngine.CancelControllerCapture();
        MH3GameSpeed::Reset();
        break;
    default:
        break;
    }
}

void TouchControls_UpdateSample(uint16_t* buttons, int16_t* leftX, int16_t* leftY,
                                int16_t* rightX, int16_t* rightY,
                                uint8_t* triggerL, uint8_t* triggerR) {
    std::lock_guard lock(g_touchMutex);
    g_touchSettingsEngine.UpdateSample(buttons, leftX, leftY, rightX, rightY, triggerL, triggerR);
}

void TouchControls_RenderGL(int windowWidth, int windowHeight) {
    std::lock_guard lock(g_touchMutex);
    g_touchSettingsEngine.RenderGL(windowWidth, windowHeight);
}

bool TouchControls_IsMenuOpen(void) {
    std::lock_guard lock(g_touchMutex);
    return g_touchSettingsEngine.IsMenuOpen();
}
bool TouchControls_IsModalOpen(void) {
    std::lock_guard lock(g_touchMutex);return g_touchSettingsEngine.IsModalOpen();
}
uint16_t TouchControls_GetComboMask(unsigned slot) {
    std::lock_guard lock(g_touchMutex);return g_touchSettingsEngine.GetComboMask(slot);
}

void TouchControls_ToggleMenu(void) {
    std::lock_guard lock(g_touchMutex);
    g_touchSettingsEngine.ToggleMenu();
}

void TouchControls_SetMenuOpen(bool open) {
    std::lock_guard lock(g_touchMutex);
    g_touchSettingsEngine.SetMenuOpen(open);
}
#endif

} // extern "C"
