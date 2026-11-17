#pragma once
#include "Raw/RawGpuMemoryBudget.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace Stack::GraphRendering {

inline std::uint64_t AddBytes(std::uint64_t a, std::uint64_t b) {
    return b > UINT64_MAX - a ? UINT64_MAX : a + b;
}

inline std::uint64_t ImageBytes(int width, int height, std::uint64_t surfaces = 1) {
    if (width <= 0 || height <= 0) return 0;
    const auto pixels = std::uint64_t(width) * std::uint64_t(height);
    if (surfaces > UINT64_MAX / 8 || pixels > UINT64_MAX / (8 * std::max(UINT64_C(1), surfaces)))
        return UINT64_MAX;
    return pixels * 8 * surfaces;
}

inline std::uint64_t ViewportSectionUploadBytes(int width, int height, bool hdr) {
    if (width <= 0 || height <= 0) return 0;
    const auto surfaces = hdr ? 4u : 2u; // RGBA32F, with an optional paired HDR rendition.
    return AddBytes(ImageBytes(width, height, surfaces),
        ImageBytes(width / 4 + (width % 4 != 0), height / 4 + (height % 4 != 0), surfaces));
}

inline std::uint64_t CacheAllowance(bool budgetKnown, std::uint64_t headroom,
    std::uint64_t residentCaches, std::uint64_t workingBytes,
    std::uint64_t ceiling = 1280ull * 1024 * 1024) {
    if (!budgetKnown) {
        // The fallback is a total planning allowance, not measured free VRAM.
        // Adding retained allocations back would grow it on every request.
        const auto allowance = std::min(headroom, Raw::kUnmeasuredGpuWorkingBudgetBytes);
        return std::min({ceiling, Raw::kUnmeasuredGpuCacheBudgetBytes,
            allowance > workingBytes ? allowance - workingBytes : 0});
    }
    const auto reclaimableHeadroom = AddBytes(headroom, residentCaches);
    return std::min(ceiling, reclaimableHeadroom > workingBytes ?
        reclaimableHeadroom - workingBytes : 0);
}

} // namespace Stack::GraphRendering
