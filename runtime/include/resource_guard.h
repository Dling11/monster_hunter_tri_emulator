#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>

// Optional model resources can disappear while equipment is being replaced.
// Keep the first useful rejected lookups in the normal bounded game log.
namespace ResourceGuard {
inline void Missing(const char* kind, uint32_t resource, uint32_t index = 0, uint32_t count = 0) {
    static std::atomic<unsigned> reports{0};
    if (reports.load(std::memory_order_relaxed) >= 16)
        return;
    if (reports.fetch_add(1, std::memory_order_relaxed) < 16)
        std::fprintf(stderr, "[resource] Missing %s: ref=%08x index=%u count=%u; using no result\n", kind, resource, index, count);
}
} // namespace ResourceGuard
