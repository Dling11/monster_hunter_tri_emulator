#pragma once

#include <algorithm>
#include <cmath>

// Android's host-time pacer, separate from Rowell's offline speed multiplier.
// Normal frame cadence keeps its existing smoothing. Slow frames contribute up
// to 100 ms; loading or a long suspension starts from the selected frame period.
class RowellFrameDelta {
    double smoothed_ = 1.0 / 60.0;
    int fps_ = 0;
    bool loading_ = false;

  public:
    double Sample(double elapsed, int targetFps, bool loading) {
        const int fps = std::max(1, targetFps);
        const double nominal = 1.0 / fps;
        const bool reset = fps != fps_ || loading || loading_ || !std::isfinite(elapsed) || elapsed <= 0 || elapsed > 0.25;
        fps_ = fps;
        loading_ = loading;
        if (reset)
            smoothed_ = nominal;
        else
            smoothed_ = smoothed_ * 0.85 + std::clamp(elapsed, 1.0 / 240.0, 0.1) * 0.15;
        return smoothed_;
    }
};
