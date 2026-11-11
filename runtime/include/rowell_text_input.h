#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace RowellTextInput {
// The Wii font uses UCS-2. Reject malformed UTF-8, controls, surrogates and
// unsupported supplementary characters rather than creating corrupt glyphs.
inline void Append(std::string_view utf8, std::vector<uint16_t>& output, size_t maximum) {
    for (size_t i = 0; i < utf8.size() && output.size() < maximum;) {
        const auto first = uint8_t(utf8[i]);
        uint32_t value = 0;
        size_t bytes = 0;
        uint32_t minimum = 0;
        if (first < 0x80) { value = first; bytes = 1; }
        else if ((first & 0xE0) == 0xC0) { value = first & 0x1F; bytes = 2; minimum = 0x80; }
        else if ((first & 0xF0) == 0xE0) { value = first & 0x0F; bytes = 3; minimum = 0x800; }
        else if ((first & 0xF8) == 0xF0) { value = first & 7; bytes = 4; minimum = 0x10000; }
        else { ++i; continue; }
        if (i + bytes > utf8.size()) break;
        bool valid = true;
        for (size_t j = 1; j < bytes; ++j) {
            const auto next = uint8_t(utf8[i+j]);
            if ((next & 0xC0) != 0x80) { valid = false; break; }
            value = (value << 6) | (next & 0x3F);
        }
        if (!valid) { ++i; continue; }
        i += bytes;
        if (value >= minimum && value >= 32 && value != 127 &&
            value <= 0xFFFF && !(value >= 0xD800 && value <= 0xDFFF))
            output.push_back(uint16_t(value));
    }
}
} // namespace RowellTextInput
