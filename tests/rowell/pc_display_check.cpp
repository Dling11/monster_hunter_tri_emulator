#include "rowell_touch.h"
#include "touch_controls.h"
#include "native_frame_packet.h"
#include <aurora/aurora.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
std::vector<std::function<void()>> pending;
bool graphicsOwner=false;
int aspect=-1;
float scale=-1;
void Check(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void Write(const std::filesystem::path& path,const char* text) { std::ofstream(path)<<text; }
}

void QueueNativeGraphicsTask(NativeTaskOrder order,std::function<void()> task) {
    Check(!graphicsOwner,"graphics owner incorrectly tried to enqueue a producer packet task");
    Check(order==NativeTaskOrder::BeforeDraws,"display work must run before frame draws");
    pending.push_back(std::move(task));
}
extern "C" void aurora_set_native_target_aspect(AuroraNativeTargetAspect value) {
    Check(graphicsOwner,"aspect API reached from the menu/producer thread");
    aspect=int(value);
}
extern "C" void VISetFrameBufferScale(float value) {
    Check(graphicsOwner,"resolution API reached from the menu/producer thread");
    scale=value;
}
namespace aurora::window {
void set_fullscreen(bool) {}
bool get_fullscreen() { return false; }
}
namespace aurora::gl { bool on_render_thread() noexcept { return graphicsOwner; } }
extern "C" void TouchControls_Init() {}
extern "C" void TouchControls_SetMenuOpen(bool) {}

int main(int argc,char** argv) try {
    if(argc!=2)return 2;
    const auto root=std::filesystem::path(argv[1]);
    const auto user=root/"user data",legacy=root/"old executable";
    std::filesystem::create_directories(user);
    std::filesystem::create_directories(legacy);
    auto settings=RowellPcGraphics::Read(user,legacy);
    Check(settings.resolution==0 && settings.aspect==0 && !settings.fullscreen,"fresh profile defaults changed");
    Write(legacy/"MH3UI.graphics","1 0 3 90 1 1\n");
    settings=RowellPcGraphics::Read(user,legacy);
    Check(!settings.vsync && settings.resolution==2 && settings.fps==90 && settings.renderer==1 && settings.fullscreen,"legacy graphics fallback lost useful settings");
    Write(user/"MH3UI.graphics","1 1 4 120 2 0 0\n");
    settings=RowellPcGraphics::Read(user,legacy);
    Check(settings.resolution==4 && settings.aspect==2 && settings.fps==120 && settings.renderer==1 && !settings.showFps && !settings.fullscreen,"old Rowell profile was confused with the legacy PC format");
    Write(user/"MH3UI.graphics","1 0 1 30 1 1 1 0\n");
    settings=RowellPcGraphics::Read(user,legacy);
    Check(settings.renderer==0 && settings.aspect==1 && settings.fullscreen && !settings.vsync,"new renderer field overwrote aspect or fullscreen");
    Write(user/"MH3UI.graphics","1 1 999 45 -7 1 0 999\n");
    settings=RowellPcGraphics::Read(user,legacy);
    Check(settings.resolution==5 && settings.fps==60 && settings.aspect==0 && settings.renderer==1,"invalid graphics values were not bounded");
    std::ifstream old(legacy/"MH3UI.graphics");std::string line;std::getline(old,line);
    Check(line=="1 0 3 90 1 1","reading graphics settings modified the legacy file");

    RowellTouch_SetTargetAspect(2);
    RowellTouch_SetTargetAspect(0);
    RowellTouch_SetTargetAspect(1);
    RowellTouch_SetResolutionScale(1.5f);
    Check(pending.size()==4 && aspect==-1 && scale==-1,"menu mutated graphics before the owner consumed its tasks");
    graphicsOwner=true;
    for(auto& task:pending)task();
    Check(aspect==1 && scale==1.5f,"queued display choices were lost or reordered");
    pending.clear();
    RowellTouch_SetTargetAspect(0);RowellTouch_SetResolutionScale(2.0f);
    Check(pending.empty() && aspect==0 && scale==2.0f,"render-thread startup/resize must apply directly rather than queue a producer task");
    pending.clear();graphicsOwner=false;
    RowellTouch_SetTargetAspect(-10);RowellTouch_SetTargetAspect(50);
    graphicsOwner=true;pending[0]();Check(aspect==0,"lower aspect bound was not applied");
    pending[1]();Check(aspect==2,"upper aspect bound was not applied");
    std::cout<<"PC display: legacy/current profile migration, independent renderer/aspect/fullscreen, and graphics-owner task routing passed.\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
