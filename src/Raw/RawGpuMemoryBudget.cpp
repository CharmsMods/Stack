#include "Raw/RawGpuMemoryBudget.h"

#include <algorithm>
#include <limits>

namespace Raw {
namespace {

constexpr std::uint64_t kMiB = 1024ull * 1024ull;
constexpr std::uint64_t kMinimumReserve = 512ull * kMiB;
constexpr std::uint64_t kTiledThreshold = 256ull * kMiB;

} // namespace

RawGpuMemoryBudgetDecision ResolveRawGpuMemoryBudget(
    const RawGpuMemoryBudgetInput& input) {
    RawGpuMemoryBudgetDecision decision;
    if (!input.queryAvailable || input.localBudgetBytes == 0u) {
        // An unavailable DXGI query is not evidence of low memory. Treat the
        // budget as unknown/unbounded for planning so native RAW presentation
        // is attempted; GL texture limits and allocation failure remain the
        // authoritative safety gates. Optional caches are still independently
        // capped by ResolveRawRenderCacheBudgetBytes().
        decision.workingBudgetBytes =
            std::numeric_limits<std::uint64_t>::max();
        decision.availableBytes = decision.workingBudgetBytes;
        decision.usedFallback = true;
        return decision;
    }

    decision.availableBytes = input.localBudgetBytes > input.localUsageBytes
        ? input.localBudgetBytes - input.localUsageBytes
        : 0u;
    decision.reserveBytes = std::max(
        kMinimumReserve,
        input.localBudgetBytes / 20u);
    const std::uint64_t availableAfterReserve =
        decision.availableBytes > decision.reserveBytes
        ? decision.availableBytes - decision.reserveBytes
        : 0u;
    // DXGI's current local-memory headroom is the adaptive governor. Avoid a
    // second arbitrary percentage/cap that permanently rejects large files
    // even when the device can hold them; preserve only a modest reserve for
    // the desktop, driver, and unavoidable non-RAW allocations.
    decision.workingBudgetBytes = availableAfterReserve;
    decision.forceMinimumMemoryTiling =
        decision.availableBytes < kTiledThreshold;
    return decision;
}

RawFullFramePreviewDecision ResolveRawFullFramePreviewDecision(
    int width,
    int height,
    int maxTextureSize,
    std::uint64_t workingBudgetBytes,
    bool forceMinimumMemoryTiling, int surfaceBytesPerPixel) {
    constexpr std::uint64_t kProtectedWorkingSurfaceCount = 7u;

    RawFullFramePreviewDecision decision;
    if (width <= 0 || height <= 0 || maxTextureSize <= 0) {
        return decision;
    }
    decision.fitsTextureLimits =
        width <= maxTextureSize && height <= maxTextureSize;

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);
    const std::uint64_t kBytesPerProtectedPixel =
        static_cast<std::uint64_t>(std::max(1,surfaceBytesPerPixel)) * kProtectedWorkingSurfaceCount;
    decision.estimatedWorkingSetBytes =
        pixelCount >
            std::numeric_limits<std::uint64_t>::max() /
                kBytesPerProtectedPixel
        ? std::numeric_limits<std::uint64_t>::max()
        : pixelCount * kBytesPerProtectedPixel;
    decision.fitsWorkingBudget =
        workingBudgetBytes > 0u &&
        decision.estimatedWorkingSetBytes <= workingBudgetBytes;
    decision.allowed =
        !forceMinimumMemoryTiling &&
        decision.fitsTextureLimits &&
        decision.fitsWorkingBudget;
    return decision;
}

} // namespace Raw
