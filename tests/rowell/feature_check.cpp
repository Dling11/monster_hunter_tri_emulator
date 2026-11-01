#include "rowell_features.h"
#include "rowell_text_input.h"
#include "bounded_log.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

static unsigned checks=0;
static void Check(bool value,const char* message) {
    ++checks;
    if(!value)throw std::runtime_error(message);
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    const std::filesystem::path directory=argv[1];
    std::filesystem::create_directories(directory);
#ifdef _WIN32
    _putenv_s("MH3_USER_DIR",directory.string().c_str());
#else
    setenv("MH3_USER_DIR",directory.string().c_str(),1);
#endif
    using namespace RowellFeatures;
    Load();
    Check(!cutsceneSkip && androidKeyboard && !physicsLog,"Unsafe defaults");
    cutsceneSkip=true;androidKeyboard=false;physicsLog=true;
    Check(Save(),"Feature preferences did not save");
    cutsceneSkip=false;androidKeyboard=true;
    Load();
    Check(cutsceneSkip && !androidKeyboard && !physicsLog,"Preferences or session-only logging were not respected");
    for(const auto name:{"touch_layout.cfg","keyboard.cfg","gamepad_bindings.cfg","nand"})
        Check(!std::filesystem::exists(directory/name),"Preferences touched saves or input bindings");
    {std::ofstream f(directory/"features.cfg");f<<"CUTSCENE_SKIP 9\nANDROID_KEYBOARD -1\n";}
    Load();Check(!cutsceneSkip && androidKeyboard,"Malformed settings enabled a feature");
    SkipPress gate;
    Check(!gate.Poll(1,true,true),"A held button skipped the start of a scene");
    Check(!gate.Poll(1,true,true),"A held button skipped the scene");
    Check(!gate.Poll(1,false,true),"A release skipped the scene");
    Check(gate.Poll(1,true,true),"A fresh press did not skip");
    Check(!gate.Poll(1,true,true),"A held button repeated a skip");
    Check(!gate.Poll(2,true,true),"The skip leaked into the next scene");
    gate.Poll(2,false,true);
    Check(!gate.Poll(2,true,true,false),"A movie closed with a read pending");
    Check(gate.Poll(2,false,true,true),"A safely deferred skip was lost");
    gate.Poll(2,false,true);
    gate.Poll(2,true,true,false);
    Check(!gate.Poll(2,false,false,true),"Disabling skipping retained a pending request");
    Check(!gate.Poll(2,false,true,true),"A discarded request was replayed");
    for(const auto mode:{std::pair{0,0},{2,5},{1,3},{1,5},{1,6}})
        Check(IsOfflineMode(uint8_t(mode.first),uint8_t(mode.second)),"An expected offline mode was rejected");
    for(const auto mode:{std::pair{2,4},{1,4},{3,2},{255,255}})
        Check(!IsOfflineMode(uint8_t(mode.first),uint8_t(mode.second)),"Online, Arena or an unknown mode allowed skipping");
    std::vector<uint16_t> text;
    RowellTextInput::Append("R\xC3\xB6well",text,16);
    Check(text==std::vector<uint16_t>{'R',0xF6,'w','e','l','l'},"UTF-8 name decoding failed");
    text.clear();RowellTextInput::Append("A\xC0\xAF\xED\xA0\x80\xF0\x9F\x98\x80\nB",text,16);
    Check(text==std::vector<uint16_t>{'A','B'},"Malformed, surrogate, emoji or control data reached UCS-2");
    text.clear();RowellTextInput::Append("abcdef",text,3);
    Check(text.size()==3 && text.back()=='c',"Text exceeded the game field capacity");
    text.clear();RowellTextInput::Append("A\xE2",text,16);
    Check(text==std::vector<uint16_t>{'A'},"Truncated UTF-8 was not rejected");
    const auto logPath=directory/"hunter_telemetry.log";
    {
        BoundedLog::File log;
        Check(log.Open(logPath.string(),1024*1024),"Telemetry log did not open");
        const std::string record(900,'x');
        for(unsigned i=0;i<2500;++i)Check(log.Append(record),"Telemetry log write failed");
    }
    Check(std::filesystem::file_size(logPath)<=1024*1024,"Current physics log exceeded 1 MiB");
    Check(std::filesystem::file_size(logPath.string()+".previous")<=1024*1024,"Previous physics log exceeded 1 MiB");
    std::cout<<"Optional features, skip gates, text safety and telemetry retention: "<<checks<<" checks passed\n";
}
