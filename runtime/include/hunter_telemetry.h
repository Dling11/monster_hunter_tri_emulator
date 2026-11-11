#pragma once
#include <cstdint>
namespace HunterTelemetry {
// Diagnostic observations only; never changes player position or physics.
void Update(uint32_t player);
}
