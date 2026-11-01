#include "lite_profile.h"
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>

int main() {
    assert(LiteProfile::DefaultFps == 30);
    assert(LiteProfile::DefaultResolution >= 0 && LiteProfile::DefaultResolution < 6);
    for (std::size_t i=0;i<LiteProfile::ResolutionScales.size();++i) {
        // Startup and live changes must agree, including Native's sentinel.
        assert(std::abs(std::strtof(LiteProfile::ResolutionText[i], nullptr) - LiteProfile::ResolutionScales[i]) < 0.000001f);
    }
    const float scale=LiteProfile::ResolutionScales[LiteProfile::DefaultResolution];
    assert(scale > 0 && scale < 1);
    assert(scale * scale <= 0.5625f);
    assert(LiteProfile::ResolutionScales[0] == 0.5f);
    assert(LiteProfile::CacheWorkers == 1 && LiteProfile::ModelWorkers == 1);
    assert(LiteProfile::PendingAssetBytes <= (8u << 20));
    std::cout << "Lite startup/live resolution agreement, subnative pixel cost and memory limits passed.\n";
}
