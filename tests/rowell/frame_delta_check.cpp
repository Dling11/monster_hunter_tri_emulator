#include "rowell_frame_delta.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

static void Check(bool condition, const char *text) {
  if (!condition) {
    std::fprintf(stderr, "Frame pacing: %s\n", text);
    std::exit(1);
  }
}
static bool Near(double a, double b) { return std::abs(a - b) < 1e-6; }
int main() {
  RowellFrameDelta delta;
  Check(Near(delta.Sample(1.0 / 60, 60, false), 1.0 / 60),
        "normal startup period");
  for (int i = 0; i < 120; ++i)
    delta.Sample(1.0 / 15, 60, false);
  double total = 0;
  for (int i = 0; i < 150; ++i)
    total += delta.Sample(1.0 / 15, 60, false);
  Check(std::abs(total - 10.0) < 0.001,
        "15 FPS must advance ten seconds in 150 frames");
  for (int i = 0; i < 120; ++i)
    delta.Sample(0.125, 60, false);
  Check(Near(delta.Sample(0.125, 60, false), 0.1),
        "low-end step cap must be 100ms, not 50ms");
  Check(Near(delta.Sample(5.0, 60, false), 1.0 / 60),
        "resume must discard suspended time");
  Check(Near(delta.Sample(0.09, 60, true), 1.0 / 60),
        "loading step must be stable");
  Check(Near(delta.Sample(0.09, 60, false), 1.0 / 60),
        "loading exit must not introduce catch-up");
  Check(Near(delta.Sample(0.08, 30, false), 1.0 / 30),
        "FPS change must reset smoothing");
  Check(Near(delta.Sample(std::numeric_limits<double>::quiet_NaN(), 30, false),
             1.0 / 30),
        "invalid sample must not poison the clock");
  for (int i = 0; i < 120; ++i)
    delta.Sample(1.0 / 60, 120, false);
  Check(Near(delta.Sample(1.0 / 60, 120, false), 1.0 / 60),
        "120 FPS limit on a 60 FPS host must keep real-time speed");
  std::puts("Android frame pacing: slow-host timing, loading, resume and FPS "
            "transitions passed.");
}
