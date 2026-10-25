#pragma once
#include <SDL3/SDL.h>
#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <cstdint>

namespace RowellKeys {
inline constexpr int ActionCount=28, Slots=3;
struct Action { const char* id; const char* label; uint16_t mask; std::array<SDL_Scancode,Slots> defaults; };
const Action& Definition(int action);
struct Sample { uint16_t buttons=0; int16_t leftX=0,leftY=0,rightX=0,rightY=0; uint8_t combos=0; };
class Store {
public:
    explicit Store(std::filesystem::path file);
    std::array<SDL_Scancode,Slots> Get(int action) const;
    std::string Assign(int action,int slot,SDL_Scancode key);
    std::string Reset(int action=-1);
    Sample Read(const bool* keys,int count) const;
private:
    using Bindings=std::array<std::array<SDL_Scancode,Slots>,ActionCount>;
    static Bindings Defaults();
    std::string Commit(const Bindings& proposed);
    std::filesystem::path file_;
    Bindings bindings_;
    mutable std::mutex mutex_;
};
Store& Current();
}
