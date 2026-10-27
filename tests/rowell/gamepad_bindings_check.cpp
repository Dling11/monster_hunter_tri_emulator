#include "gamepad_bindings.h"
#include "../../runtime/native/mh3_classic_map.h"
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <set>

static void Check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"FAIL: %s (%s)\n",message,SDL_GetError()); std::exit(1); }
}
static uint64_t Bit(int input) { return uint64_t(1) << input; }
int main(int argc,char** argv) {
    Check(argc==2,"isolated fixture path required");
    const std::filesystem::path root=argv[1];
    std::filesystem::create_directories(root);
    const auto file=root/"controller_bindings.cfg";
    using namespace RowellPad;
    Store store(file);
    Check(!store.HasCustom(),"missing file keeps legacy presets");
    Check(store.Read(Bit(SDL_GAMEPAD_BUTTON_EAST),0,false).buttons==MH3_CL_A,"Nintendo default");
    Check(store.Read(Bit(SDL_GAMEPAD_BUTTON_EAST),1,false).buttons==MH3_CL_B,"Xbox default");
    Check(store.Read(Bit(LeftTrigger),0,false).buttons==MH3_CL_ZL,"original trigger");
    Check(store.Read(Bit(LeftTrigger),0,true).buttons==MH3_CL_L,"swapped trigger");
    Check(store.Assign(0,SDL_GAMEPAD_BUTTON_LEFT_STICK,0,false).empty(),"replace broken face button with L3");
    Check(store.Read(Bit(SDL_GAMEPAD_BUTTON_EAST),0,false).buttons==0,"old broken input disabled");
    Check(store.Read(Bit(SDL_GAMEPAD_BUTTON_LEFT_STICK),0,false).buttons==MH3_CL_A,"replacement input works");
    Check(!store.Assign(1,SDL_GAMEPAD_BUTTON_LEFT_STICK,0,false).empty(),"duplicate needs explicit swap");
    Check(store.Get(0,false)[1]==SDL_GAMEPAD_BUTTON_SOUTH,"rejected conflict leaves bindings unchanged");
    Check(store.Assign(1,SDL_GAMEPAD_BUTTON_LEFT_STICK,0,false,true).empty(),"confirmed swap");
    Check(store.Get(0,false)[0]==SDL_GAMEPAD_BUTTON_SOUTH,"swap retains displaced action");
    Check(store.Assign(19,SDL_GAMEPAD_BUTTON_RIGHT_STICK,0,false).empty(),"combo binding");
    Check(store.Read(Bit(SDL_GAMEPAD_BUTTON_RIGHT_STICK),0,false).combos==16,"combo 5 independent mask");
    Check(store.Read(0,0,false).buttons==0,"no controller input does not invent keys or touch presses");
    Check(store.Assign(0,Unassigned,0,false).empty(),"clear binding");
    Check(Store(file).Get(0,false)==store.Get(0,false),"bindings survive reload");
    const auto before=store.Get(0,false);
    Check(!store.Assign(-1,0,0,false).empty() && !store.Assign(0,SourceCount,0,false).empty(),"invalid inputs rejected");
    Check(store.Get(0,false)==before,"invalid input preserves bindings");
    Store unwritable(root/"missing-parent/map.cfg");
    Check(!unwritable.Assign(0,SDL_GAMEPAD_BUTTON_LEFT_STICK,0,false).empty(),"failed save reported");
    Check(!unwritable.HasCustom(),"failed save does not activate unsaved mapping");
    Check(store.Reset().empty() && !store.HasCustom() && !Store(file).HasCustom(),"persistent reset");
    {std::ofstream out(file);out<<"MH_TRI_ROWELL_PAD 1\n1\nA 999999\n";}
    Check(!Store(file).HasCustom(),"corrupt/truncated file falls back to defaults");
    {std::ofstream out(root/"keyboard.cfg");out<<"keyboard sentinel";}
    Check(store.Assign(0,SDL_GAMEPAD_BUTTON_LEFT_STICK,0,false).empty(),"save custom map again");
    {std::ifstream in(root/"keyboard.cfg");std::string s;std::getline(in,s);Check(s=="keyboard sentinel","keyboard file untouched");}

    // Capture must tolerate an already-held broken button and never switch devices.
    Capture capture;
    Snapshot state;state.connected=true;state.id=42;state.held=Bit(SDL_GAMEPAD_BUTTON_EAST);
    capture.Begin(state);
    Check(capture.Update(state)==Unassigned,"held button not captured");
    state.held|=Bit(LeftTrigger);
    Check(capture.Update(state)==LeftTrigger,"trigger captured despite stuck broken button");
    capture.Begin(state);state.id=43;
    Check(capture.Update(state)==Unassigned && capture.Disconnected(),"different controller cancels capture");

    // Exercise the real SDL polling path using a virtual physical controller.
    Check(SDL_Init(SDL_INIT_GAMEPAD),"SDL gamepad initialization");
    Check(!Poll().connected,"disconnected state");
    SDL_VirtualJoystickDesc desc{};SDL_INIT_INTERFACE(&desc);
    desc.type=SDL_JOYSTICK_TYPE_GAMEPAD;desc.naxes=SDL_GAMEPAD_AXIS_COUNT;desc.nbuttons=SDL_GAMEPAD_BUTTON_COUNT;
    desc.axis_mask=(1u<<SDL_GAMEPAD_AXIS_COUNT)-1;desc.button_mask=(1u<<SDL_GAMEPAD_BUTTON_COUNT)-1;
    desc.name="Rowell virtual DS4 test";
    const auto id=SDL_AttachVirtualJoystick(&desc);Check(id!=0,"attach virtual controller");
    auto* joy=SDL_OpenJoystick(id);Check(joy!=nullptr,"open virtual joystick");
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,-32768);
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,-32768);
    SDL_UpdateJoysticks();SDL_Delay(510);
    Check(Poll().connected,"controller recognized");
    Check(SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_LEFT_STICK,true),"L3 press");
    SDL_UpdateJoysticks();
    Check((ReadForGame().held&Bit(SDL_GAMEPAD_BUTTON_LEFT_STICK))!=0,"real SDL button polling");
    SuppressHeld();
    Check((ReadForGame().held&Bit(SDL_GAMEPAD_BUTTON_LEFT_STICK))==0,"captured button cannot leak into gameplay");
    SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_LEFT_STICK,false);SDL_UpdateJoysticks();ReadForGame();
    SDL_SetJoystickVirtualButton(joy,SDL_GAMEPAD_BUTTON_LEFT_STICK,true);SDL_UpdateJoysticks();
    Check((ReadForGame().held&Bit(SDL_GAMEPAD_BUTTON_LEFT_STICK))!=0,"release and repress works");
    SDL_SetJoystickVirtualAxis(joy,SDL_GAMEPAD_AXIS_LEFT_TRIGGER,32767);SDL_UpdateJoysticks();
    Check((Poll().held&Bit(LeftTrigger))!=0,"real SDL trigger polling");
    Check(SDL_DetachVirtualJoystick(id),"disconnect virtual controller");
    Check(!ReadForGame().connected && ReadForGame().held==0,"disconnect releases input");
    SDL_CloseJoystick(joy);SDL_Quit();
    std::puts("Controller bindings: defaults, persistence, conflicts, broken buttons, SDL input and disconnect checks passed.");
}
