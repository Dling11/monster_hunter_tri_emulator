#include "pc_keybindings.h"
#include "../native/mh3_classic_map.h"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <windows.h>

namespace RowellKeys {
namespace {
constexpr Action actions[ActionCount]={
 {"A","Wii A",MH3_CL_A,{SDL_SCANCODE_F}}, {"B","Wii B",MH3_CL_B,{SDL_SCANCODE_SPACE,SDL_SCANCODE_BACKSPACE}},
 {"X","Wii X",MH3_CL_X,{SDL_SCANCODE_X}}, {"Y","Wii Y",MH3_CL_Y,{SDL_SCANCODE_E}},
 {"L","Wii L",MH3_CL_L,{SDL_SCANCODE_Q}}, {"R","Wii R",MH3_CL_R,{SDL_SCANCODE_LSHIFT,SDL_SCANCODE_RSHIFT,SDL_SCANCODE_R}},
 {"ZL","Wii ZL",MH3_CL_ZL,{SDL_SCANCODE_Z}}, {"ZR","Wii ZR",MH3_CL_ZR,{SDL_SCANCODE_C}},
 {"PLUS","Wii +",MH3_CL_PLUS,{SDL_SCANCODE_RETURN}}, {"MINUS","Wii -",MH3_CL_MINUS,{SDL_SCANCODE_TAB}},
 {"HOME","Wii Home",MH3_CL_HOME,{SDL_SCANCODE_HOME}},
 {"UP","D-pad up",MH3_CL_UP,{SDL_SCANCODE_UP}}, {"DOWN","D-pad down",MH3_CL_DOWN,{SDL_SCANCODE_DOWN}},
 {"LEFT","D-pad left",MH3_CL_LEFT,{SDL_SCANCODE_LEFT}}, {"RIGHT","D-pad right",MH3_CL_RIGHT,{SDL_SCANCODE_RIGHT}},
 {"MOVE_UP","Move forward",0,{SDL_SCANCODE_W}}, {"MOVE_DOWN","Move backward",0,{SDL_SCANCODE_S}},
 {"MOVE_LEFT","Move left",0,{SDL_SCANCODE_A}}, {"MOVE_RIGHT","Move right",0,{SDL_SCANCODE_D}},
 {"CAM_UP","Camera up",0,{SDL_SCANCODE_I}}, {"CAM_DOWN","Camera down",0,{SDL_SCANCODE_K}},
 {"CAM_LEFT","Camera left",0,{SDL_SCANCODE_J}}, {"CAM_RIGHT","Camera right",0,{SDL_SCANCODE_L}},
 {"COMBO_1","Combo 1",0,{}}, {"COMBO_2","Combo 2",0,{}}, {"COMBO_3","Combo 3",0,{}},
 {"COMBO_4","Combo 4",0,{}}, {"COMBO_5","Combo 5",0,{}}
};
bool Valid(SDL_Scancode key) {
 return key==SDL_SCANCODE_UNKNOWN || (key>0 && key<SDL_SCANCODE_COUNT && *SDL_GetScancodeName(key) &&
 key!=SDL_SCANCODE_ESCAPE && key!=SDL_SCANCODE_F1 && key!=SDL_SCANCODE_F2 && key!=SDL_SCANCODE_F3);
}
std::string Conflict(const std::array<std::array<SDL_Scancode,Slots>,ActionCount>& bindings) {
 std::array<int,SDL_SCANCODE_COUNT> owner;owner.fill(-1);
 for(int action=0;action<ActionCount;++action)for(auto key:bindings[action])if(key!=SDL_SCANCODE_UNKNOWN) {
  if(!Valid(key))return "Reserved or invalid key. F1/F2/F3 and Escape stay available for settings.";
  if(owner[key]>=0 && owner[key]!=action)return std::string("Key already used by ")+actions[owner[key]].label+". Clear that binding first.";
  owner[key]=action;
 }
 return {};
}
}
const Action& Definition(int action) {return actions[std::clamp(action,0,ActionCount-1)];}
Store::Bindings Store::Defaults() {Bindings result;for(int i=0;i<ActionCount;++i)result[i]=actions[i].defaults;return result;}
Store::Store(std::filesystem::path file):file_(std::move(file)),bindings_(Defaults()) {
 std::ifstream input(file_);std::string line;
 if(!std::getline(input,line) || line!="MH_TRI_ROWELL_KEYS 1")return;
 auto proposed=bindings_;std::array<bool,ActionCount> seen{};
 while(std::getline(input,line)) {
  std::istringstream row(line);std::string id;int a,b,c;std::string extra;
  if(!(row>>id>>a>>b>>c) || (row>>extra))continue;
  for(int i=0;i<ActionCount;++i)if(id==actions[i].id) {
   if(seen[i])return;
   seen[i]=true;
   std::array<SDL_Scancode,Slots> values{SDL_Scancode(a),SDL_Scancode(b),SDL_Scancode(c)};
   if(std::all_of(values.begin(),values.end(),Valid))proposed[i]=values;
   break;
  }
 }
 if(Conflict(proposed).empty())bindings_=proposed;
}
std::array<SDL_Scancode,Slots> Store::Get(int action) const {
 std::lock_guard lock(mutex_);return action>=0&&action<ActionCount?bindings_[action]:std::array<SDL_Scancode,Slots>{};
}
std::string Store::Commit(const Bindings& proposed) {
 if(auto issue=Conflict(proposed);!issue.empty())return issue;
 auto temporary=file_;temporary+=".tmp";
 std::ofstream output(temporary,std::ios::trunc);
 output << "MH_TRI_ROWELL_KEYS 1\n";
 for(int i=0;i<ActionCount;++i)output << actions[i].id << ' ' << int(proposed[i][0]) << ' ' << int(proposed[i][1]) << ' ' << int(proposed[i][2]) << '\n';
 output.close();
 if(!output)return "Could not save bindings. The previous settings are still active.";
 if(!MoveFileExW(temporary.c_str(),file_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
  return "Could not replace the bindings file. The previous settings are still active.";
 bindings_=proposed;return {};
}
std::string Store::Assign(int action,int slot,SDL_Scancode key) {
 std::lock_guard lock(mutex_);
 if(action<0 || action>=ActionCount || slot<0 || slot>=Slots)return "Invalid binding.";
 if(!Valid(key))return "Reserved key: F1/F2/F3 and Escape are kept for settings. Choose another key.";
 auto proposed=bindings_;proposed[action][slot]=key;return Commit(proposed);
}
std::string Store::Reset(int action) {
 std::lock_guard lock(mutex_);auto proposed=bindings_;
 if(action<0)proposed=Defaults();else if(action<ActionCount)proposed[action]=actions[action].defaults;
 return Commit(proposed);
}
Sample Store::Read(const bool* keys,int count) const {
 Sample result;if(!keys || count<=0)return result;std::lock_guard lock(mutex_);
 std::array<bool,ActionCount> pressed{};
 for(int i=0;i<ActionCount;++i)for(auto key:bindings_[i])if(key>0 && key<count && keys[key])pressed[i]=true;
 for(int i=0;i<15;++i)if(pressed[i])result.buttons|=actions[i].mask;
 result.leftX=int16_t((int(pressed[18])-int(pressed[17]))*511);
 result.leftY=int16_t((int(pressed[15])-int(pressed[16]))*511);
 result.rightX=int16_t((int(pressed[22])-int(pressed[21]))*511);
 result.rightY=int16_t((int(pressed[19])-int(pressed[20]))*511);
 for(int i=0;i<5;++i)if(pressed[23+i])result.combos|=uint8_t(1u<<i);
 return result;
}
Store& Current() {
 static Store store([] { const char* user=std::getenv("MH3_USER_DIR");return (user&&*user?std::filesystem::path(user):std::filesystem::path("MH3_Rowell_Dev"))/"keyboard.cfg"; }());
 return store;
}
}
