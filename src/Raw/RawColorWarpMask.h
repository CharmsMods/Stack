#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace Stack::RawRecipe {

enum class RawColorWarpMaskQualityTier {
    Interactive = 0,
    Settled,
    Native
};

struct RawColorWarpMaskGuide {
    int width = 0;
    int height = 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    std::vector<RawColorWarpCoordinate> pixels;
};

struct RawColorWarpMaskFields {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> gate;
    std::vector<std::uint8_t> support;
    std::vector<std::uint8_t> boundary;
    // Non-zero where visible source-image edges participate in the final
    // inward, centered, or outward fade. This is diagnostic metadata; the
    // resulting strength is already folded into support and boundary.
    std::vector<std::uint8_t> edge;
    bool spatialAvailable = false;
    bool cancelled = false;
};

int RawColorWarpMaskLongEdge(RawColorWarpMaskQualityTier tier);

// Builds one pin's bounded proxy fields. This subsystem is intentionally
// independent from Local Range state, textures, caches, and UI lifecycle.
RawColorWarpMaskFields BuildRawColorWarpMaskFields(
    const RawColorWarpMaskGuide& guide,
    const RawColorWarpPin& pin,
    const RawColorWarpRegion* region,
    const std::function<bool()>& isCancelled = {});

} // namespace Stack::RawRecipe
