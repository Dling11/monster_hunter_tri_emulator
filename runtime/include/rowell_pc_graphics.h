#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace RowellPcGraphics {
struct Settings {
    bool vsync = true;
    int resolution = 0, fps = 60, aspect = 0;
    bool showFps = true, fullscreen = false;
    int renderer = 0; // 0: Vulkan (ANGLE), 1: OpenGL ES.
};

inline int FrameLimit(int value) {
    return value == 30 || value == 60 || value == 90 || value == 120 ? value : 60;
}

inline Settings Read(const std::filesystem::path& user, const std::filesystem::path& executableDirectory) {
    Settings result;
    // Read the old PC format only as a fallback. Never rewrite it or confuse
    // its renderer column with the shared menu's aspect-ratio column.
    std::ifstream legacy(executableDirectory / "MH3UI.graphics");
    int samples = 1, vsync = 1, resolution = 0, fps = 60, renderer = 0, fullscreen = 0;
    if (legacy >> samples >> vsync >> resolution >> fps >> renderer) {
        legacy >> fullscreen;
        constexpr int resolutionMap[] = {0, 1, 2, 2, 3, 4};
        result.vsync = vsync != 0;
        result.resolution = resolutionMap[std::clamp(resolution, 0, 5)];
        result.fps = FrameLimit(fps);
        result.renderer = std::clamp(renderer, 0, 1);
        result.fullscreen = fullscreen != 0;
    }
    std::ifstream current(user / "MH3UI.graphics");
    int aspect = 0, showFps = 1;
    if (current >> samples >> vsync >> resolution >> fps >> aspect >> showFps) {
        fullscreen = 0;
        current >> fullscreen;
        // Existing seven-column Rowell profiles retain the legacy renderer.
        renderer = result.renderer;
        current >> renderer;
        result.vsync = vsync != 0;
        result.resolution = std::clamp(resolution, 0, 5);
        result.fps = FrameLimit(fps);
        result.aspect = std::clamp(aspect, 0, 2);
        result.showFps = showFps != 0;
        result.fullscreen = fullscreen != 0;
        result.renderer = std::clamp(renderer, 0, 1);
    }
    return result;
}
} // namespace RowellPcGraphics
