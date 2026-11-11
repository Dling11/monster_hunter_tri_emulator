// Adapted from Kiridor123/OceanTriRecomp-Android, 93b886f2, GPL-3.0.
// Rowell: opt-in, local user directory, bounded retention and rate limiting.
#include "hunter_telemetry.h"
#include "bounded_log.h"
#include "memory.h"
#include "rowell_features.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace HunterTelemetry {
namespace {
struct State {
    float x = 0, y = 0, z = 0, water = 0, ground = 0;
    uint8_t map = 0, area = 0, swimming = 0, inWater = 0;
    bool valid = false;
};
State previous;
BoundedLog::File log;
bool opened = false, attempted = false;
auto nextSample = std::chrono::steady_clock::time_point{};
auto nextRecord = std::chrono::steady_clock::time_point{};
}

void Update(uint32_t player) {
    // This hook belongs to the game thread; UI only publishes the atomic flag.
    if (!RowellFeatures::physicsLog.load(std::memory_order_relaxed)) {
        if (attempted) log.Close();
        opened = attempted = false;
        previous.valid = false;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < nextSample) return;
    nextSample = now + std::chrono::milliseconds(100);
    if (!Memory::Contains(player, 0x600) || Memory::Read8(player + 0xB6) != 0 ||
        !Memory::Contains(0x806BB805, 2)) return;
    if (!attempted) {
        attempted = true;
        std::error_code error;
        const auto directory = RowellFeatures::UserDir() / "logs";
        std::filesystem::create_directories(directory, error);
        // Current + one previous file, at most 2 MiB combined.
        opened = !error && log.Open((directory / "hunter_telemetry.log").string(), 1024 * 1024);
        if (opened) opened = log.Append("[START] Optional local physics diagnostics; observations are not confirmed bugs.\n");
    }
    if (!opened) return; // Failed storage is not retried every frame.
    State state;
    state.x = Memory::ReadFloat32(player + 60);
    state.y = Memory::ReadFloat32(player + 64);
    state.z = Memory::ReadFloat32(player + 68);
    state.ground = Memory::ReadFloat32(player + 96);
    state.water = Memory::ReadFloat32(player + 100);
    state.swimming = Memory::Read8(player + 9);
    state.inWater = Memory::Read8(player + 116);
    state.map = Memory::Read8(0x806BB805);
    state.area = Memory::Read8(0x806BB806);
    if (!std::isfinite(state.x) || !std::isfinite(state.y) || !std::isfinite(state.z) ||
        !std::isfinite(state.water) || !std::isfinite(state.ground)) {
        previous.valid = false;
        return;
    }
    state.valid = true;
    const bool changedArea = previous.valid && (state.map != previous.map || state.area != previous.area);
    const float dx = state.x - previous.x, dz = state.z - previous.z;
    // This threshold covers a 100 ms sample, including solo speed mode.
    const bool jump = previous.valid && !changedArea && dx*dx + dz*dz > 10000;
    const bool water = state.inWater && state.swimming != 3 &&
        state.y >= state.water - 5 && state.ground < state.water - 8;
    if (now >= nextRecord && (!previous.valid || changedArea || jump || water)) {
        char record[384];
        std::snprintf(record, sizeof(record),
            "[%s] map=%u area=%u pos=(%.2f,%.2f,%.2f) ground=%.2f water=%.2f state=%u waterFlag=%u\n",
            jump ? "POSITION JUMP" : water ? "WATER STATE" : "AREA", unsigned(state.map), unsigned(state.area),
            state.x, state.y, state.z, state.ground, state.water, unsigned(state.swimming), unsigned(state.inWater));
        opened = log.Append(record);
        nextRecord = now + std::chrono::seconds(1);
    }
    previous = state;
}
} // namespace HunterTelemetry
