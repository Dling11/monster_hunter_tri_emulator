#pragma once
#include <array>
#include <cstdint>

namespace mh3_touch {
// Fixed storage keeps pointer tracking allocation-free during play. A contact
// is identified by the complete SDL finger ID, including zero and the high bits.
class Contacts {
    struct Slot { uint64_t id = 0; bool active = false; };
    std::array<Slot, 16> slots{};
public:
    bool Contains(uint64_t id) const {
        for (const auto& s : slots) if (s.active && s.id == id) return true;
        return false;
    }
    bool Press(uint64_t id) {
        if (Contains(id)) return false;
        for (auto& s : slots) if (!s.active) { s = {id, true}; return true; }
        return false;
    }
    void Release(uint64_t id) {
        for (auto& s : slots) if (s.active && s.id == id) s.active = false;
    }
    bool Held() const {
        for (const auto& s : slots) if (s.active) return true;
        return false;
    }
    void Clear() { for (auto& s : slots) s.active = false; }
};
}
