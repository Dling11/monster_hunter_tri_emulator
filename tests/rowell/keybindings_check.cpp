#include "pc_keybindings.h"
#include "runtime/native/mh3_classic_map.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void Check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
int main(int argc,char** argv)try {
 if(argc!=2)return 2;std::filesystem::path root=argv[1];std::filesystem::create_directories(root);
 RowellKeys::Store keys(root/"keyboard.cfg");std::array<bool,SDL_SCANCODE_COUNT> state{};
 state[SDL_SCANCODE_W]=true;state[SDL_SCANCODE_D]=true;state[SDL_SCANCODE_F]=true;state[SDL_SCANCODE_LSHIFT]=true;
 auto sample=keys.Read(state.data(),int(state.size()));
 Check(sample.leftX==511 && sample.leftY==511 && sample.buttons==(MH3_CL_A|MH3_CL_R),"defaults did not match original PC controls");
 Check(keys.Assign(0,0,SDL_SCANCODE_G).empty(),"custom key rejected");
 RowellKeys::Store loaded(root/"keyboard.cfg");state.fill(false);state[SDL_SCANCODE_F]=true;
 Check(loaded.Read(state.data(),int(state.size())).buttons==0,"old key remained active");state[SDL_SCANCODE_G]=true;
 Check(loaded.Read(state.data(),int(state.size())).buttons==MH3_CL_A,"saved key was not loaded");
 Check(!loaded.Assign(2,0,SDL_SCANCODE_G).empty() && loaded.Get(2)[0]==SDL_SCANCODE_X,"conflict silently overwrote another action");
 Check(!loaded.Assign(0,0,SDL_SCANCODE_ESCAPE).empty(),"recovery key was remapped");
 Check(loaded.Assign(27,0,SDL_SCANCODE_H).empty(),"combo binding rejected");state.fill(false);state[SDL_SCANCODE_H]=true;
 Check(loaded.Read(state.data(),int(state.size())).combos==16,"combo key did not select configured combo slot");
 Check(loaded.Reset(0).empty() && loaded.Get(0)[0]==SDL_SCANCODE_F,"single reset failed");
 Check(loaded.Reset().empty() && loaded.Get(27)[0]==SDL_SCANCODE_UNKNOWN,"reset all failed");
 {std::ofstream bad(root/"keyboard.cfg");bad<<"MH_TRI_ROWELL_KEYS 1\nA -55 0 0\nMOVE_UP 99999 0 0\n";}
 RowellKeys::Store repaired(root/"keyboard.cfg");Check(repaired.Get(0)[0]==SDL_SCANCODE_F && repaired.Get(15)[0]==SDL_SCANCODE_W,"invalid entries did not fall back to defaults");
 RowellKeys::Store failed(root/"missing-parent/keyboard.cfg");Check(!failed.Assign(0,0,SDL_SCANCODE_G).empty() && failed.Get(0)[0]==SDL_SCANCODE_F,"failed persistence changed active controls");
 std::cout<<"PC bindings: original defaults, persistence, conflicts, reserved keys, combos, resets and malformed/missing config checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
