#pragma once
#include <array>
#include <cstddef>

// The Lite application has its own settings and data directory. These defaults
// never rewrite the normal application's preferences or change guest RAM.
namespace LiteProfile {
inline constexpr int DefaultFps = 30;
inline constexpr int DefaultResolution = 1;
inline constexpr std::array<float, 6> ResolutionScales = {0.5f, 0.75f, 1.0f, 720.0f / 448.0f, 1080.0f / 448.0f, 0.0f};
inline constexpr std::array<const char*, 6> ResolutionText = {"0.5", "0.75", "1.0", "1.6071429", "2.4107143", "0.0"};
inline constexpr std::array<const char*, 6> ResolutionLabels = {"Low (50%)", "Balanced (75%)", "Wii original", "720p", "1080p", "Native"};
inline constexpr unsigned CacheWorkers = 1;
inline constexpr unsigned ModelWorkers = 1;
inline constexpr std::size_t PendingAssetBytes = 8u << 20;
}
