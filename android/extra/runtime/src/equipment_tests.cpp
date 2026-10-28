#include "abi_bridge.h"
#include "anm_obj_chr_res.h"
#include "g3d_native.h"
#include "guest_worker.h"
#include <array>
#include <cstring>
#include <stdexcept>

extern "C" void MH3_AnmChrGetAnmResult(CpuContext*);
extern "C" void MH3_G3dCalcSkinning(CpuContext*);
extern "C" void MH3_GetJointWpos(CpuContext*);

namespace {
void Require(bool condition, const char* detail) {
    if (!condition)
        MH3_THROW(std::runtime_error(detail));
}
struct Fixture {
    static constexpr uint32_t base = 0x817C0000;
    std::array<uint8_t, 0x10000> before;
    CpuContext* cpu;
    CpuContext saved;
    explicit Fixture(CpuContext* c) : cpu(c), saved(*c) {
        std::memcpy(before.data(), GuestFlat::Host(base), before.size());
        std::memset(GuestFlat::Host(base), 0, before.size());
        cpu->gpr[1] = base + 0xF000;
        cpu->gpr[2] = base + 0x7C00;
        Memory::WriteFloat32(cpu->gpr[2] - 0x7BB8, 1.0f);
        Memory::WriteFloat32(cpu->gpr[2] - 0x7BD0, 0.0f);
    }
    ~Fixture() {
        std::memcpy(GuestFlat::Host(base), before.data(), before.size());
        *cpu = saved;
    }
};
} // namespace

void RunEquipmentSelfTests(CpuContext* cpu) {
    Fixture fixture(cpu);
    constexpr uint32_t b = Fixture::base;
    constexpr uint32_t ref = b + 0x100, mdl = b + 0x200, dic = b + 0x300;
    constexpr uint32_t node = b + 0x600, result = b + 0x1800;
    Memory::Write32(ref, mdl);
    Memory::Write32(mdl + 0x14, dic - mdl);
    Memory::Write32(dic + 4, 3);
    for (uint32_t i = 0; i < 3; ++i)
        Memory::Write32(dic + (i + 1) * 16 + 0x14, node + i * 0x100 - dic);
    Require(G3dNative::ResDicAtOffset14(ref, 0) == node && G3dNative::ResDicAtOffset14(ref, 2) == node + 0x200, "Valid model node lookup changed");
    for (uint32_t index : {3u, 0xFFFFFFFFu, 0x80000000u})
        Require(G3dNative::ResDicAtOffset14(ref, index) == 0, "Invalid equipment node escaped its dictionary");
    Require(G3dNative::ResDicAtOffset14(0, 0) == 0, "Null model ref not handled");
    Memory::Write32(dic + 4, 0xFFFFFFFF);
    Require(G3dNative::ResDicAtOffset14(ref, 0) == 0, "Malformed signed model count accepted");
    Memory::Write32(dic + 4, 3);

    constexpr uint32_t animation = b + 0x1000, data = b + 0x1100, adic = b + 0x1200, anode = b + 0x1300;
    Memory::Write32(animation, data);
    Memory::Write32(data + 0x10, adic - data);
    Memory::Write32(adic + 4, 1);
    Memory::Write32(adic + 0x24, anode - adic);
    Memory::Write32(anode + 4, 1); // Valid type-0 unit-scale / identity animation.
    auto evaluate = [&](uint32_t input, uint32_t index) {
        Memory::Write32(result, 0xDEADBEEF);
        Memory::Write32(result + 0x4C, 0xABCDEF01);
        cpu->gpr[3] = input;
        cpu->gpr[4] = result;
        cpu->gpr[5] = index;
        cpu->fpr[1].d = 0;
        MH3_AnmChrGetAnmResult(cpu);
        Require(Memory::Read32(result + 0x4C) == 0xABCDEF01, "Animation wrote outside its result");
    };
    evaluate(animation, 0);
    Require(Memory::Read32(result) == 1 && Memory::ReadFloat32(result + 4) == 1 && Memory::ReadFloat32(result + 0x1C) == 1 && Memory::ReadFloat32(result + 0x30) == 1,
            "Valid animation result changed");
    for (uint32_t index : {1u, 3u, 0xFFFFFFFFu}) {
        evaluate(animation, index);
        Require(Memory::Read32(result) == 0, "Missing animation did not produce no-result flags");
    }
    evaluate(0, 0);
    Require(Memory::Read32(result) == 0, "Missing animation ref not handled");
    Memory::Write32(adic + 4, 0xFFFFFFFF);
    evaluate(animation, 0);
    Require(Memory::Read32(result) == 0, "Malformed signed animation count accepted");
    Memory::Write32(adic + 4, 1);

    constexpr uint32_t object = b + 0x2000, bindings = b + 0x2100;
    Memory::Write32(object + 0x10, 1);
    Memory::Write32(object + 0x14, bindings);
    Memory::Write16(bindings, 0x8000); // Unbound animation, with no callback required.
    GuestCall game{cpu};
    for (uint32_t index : {0u, 1u, 3u, 0xFFFFFFFFu}) {
        Memory::Write32(result, 0xDEADBEEF);
        Require(AnmObjChrResResultAt(game, object, result, index, 0) == result && Memory::Read32(result) == 0, "Invalid animation binding read outside its table");
    }

    Memory::Write32(b + 0x2400, 0);
    Memory::Write32(result, 0xDEADBEEF);
    G3dNative::ResNodeChrAnmResult(b + 0x2400, result);
    Require(Memory::Read32(result) == 0, "Missing static pose did not produce no-result flags");
    Memory::Write32(result, 0x12345678);
    G3dNative::ResNodePatchChrAnmResult(b + 0x2400, result);
    Require(Memory::Read32(result) == 0x12345678, "Missing patch damaged an existing pose");

    // A missing inverse bind matrix retains the existing world transform,
    // instead of reading guest address 0xA0 or reusing old scratch contents.
    constexpr uint32_t out = b + 0x2600, flags = b + 0x2700, code = b + 0x2800;
    const uint32_t scratch = G3dScratch();
    std::array<uint8_t, 48> oldScratch;
    std::memcpy(oldScratch.data(), GuestFlat::Host(scratch), oldScratch.size());
    for (uint32_t i = 0; i < 12; ++i)
        Memory::WriteFloat32(out + i * 4, float(i + 1));
    Memory::Write8(code, 5);
    Memory::Write16(code + 1, 0);
    Memory::Write16(code + 3, 3);
    Memory::Write8(code + 5, 1);
    cpu->gpr[3] = out;
    cpu->gpr[4] = flags;
    cpu->gpr[5] = ref;
    cpu->gpr[6] = code;
    MH3_G3dCalcSkinning(cpu);
    const bool copied = std::memcmp(GuestFlat::Host(out), GuestFlat::Host(scratch), 48) == 0;
    std::memcpy(GuestFlat::Host(scratch), oldScratch.data(), oldScratch.size());
    Require(copied, "Missing skinning node left an undefined transform");
    std::fprintf(stderr, "Equipment safety: valid lookups, missing nodes, animation bounds and skinning fallback passed.\n");
}
