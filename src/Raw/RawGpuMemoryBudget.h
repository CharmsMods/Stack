#pragma once

#include <cstdint>

namespace Raw {

struct RawGpuMemoryBudgetInput {
    std::uint64_t localBudgetBytes = 0;
    std::uint64_t localUsageBytes = 0;
    bool queryAvailable = false;
};

struct RawGpuMemoryBudgetDecision {
    std::uint64_t workingBudgetBytes = 0;
    std::uint64_t availableBytes = 0;
    std::uint64_t reserveBytes = 0;
    bool forceMinimumMemoryTiling = false;
    bool usedFallback = false;
};

struct RawFullFramePreviewDecision {
    std::uint64_t estimatedWorkingSetBytes = 0;
    bool fitsTextureLimits = false;
    bool fitsWorkingBudget = false;
    bool allowed = false;
};

RawGpuMemoryBudgetDecision ResolveRawGpuMemoryBudget(
    const RawGpuMemoryBudgetInput& input);

// A settled RAW preview keeps several intermediates resident while
// the accepted presentation remains visible. Use a conservative seven-surface
// estimate against current device headroom after a modest safety reserve.
// Callers evict optional caches before native work; tiled native refinement is
// a separate fallback and must not silently change image semantics.
RawFullFramePreviewDecision ResolveRawFullFramePreviewDecision(
    int width,
    int height,
    int maxTextureSize,
    std::uint64_t workingBudgetBytes,
    bool forceMinimumMemoryTiling,
    int surfaceBytesPerPixel = 8);

} // namespace Raw
