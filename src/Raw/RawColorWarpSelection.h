#pragma once

#include "Raw/RawColorWarpMask.h"
#include "Raw/ColorWarpMath.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <type_traits>

namespace Stack::RawRecipe {

// Sampled in input-image coordinates, before any pin displacement. The
// caller folds the renderer's guide attenuation into gate and support.
struct RawColorWarpMaskSample {
    float gate = 1.0f;
    float support = 0.0f;
    float boundary = 1.0f;
};

inline float EvaluateColorWarpPinWeight(const RawColorWarpPin& pin,
    const RawColorWarpCoordinate& input, const RawColorWarpMaskSample& mask = {},
    float spatialSupport = 0.0f) {
    const float colorWeight=EvaluateColorWarpPinColorWeight(pin,input);
    if (!pin.enabled || pin.strength <= 0.0f || colorWeight <= 0.0f) return 0.0f;
    const float direct = colorWeight *
        EvaluateColorWarpPinLightnessWeight(pin, input.sceneEv);
    return direct * std::max(mask.gate, mask.support * spatialSupport) *
        mask.boundary * pin.strength;
}

inline float ColorWarpPinSpatialSupport(const RawColorWarpRecipe& recipe,
    const RawColorWarpPin& pin) {
    const auto region = std::find_if(recipe.regions.begin(), recipe.regions.end(),
        [&](const auto& candidate) { return candidate.id == pin.regionId; });
    return region == recipe.regions.end() ? 0.0f : region->spatialSupport;
}

inline float ColorWarpEffectiveSelectionWeight(const RawColorWarpRecipe& recipe,
    float selectedWeight, float totalWeight) {
    return recipe.enabled ? std::clamp(recipe.strength * selectedWeight /
        (recipe.combine==RawColorWarpCombine::Blend?std::max(1.0f,totalWeight):1.f), 0.0f, 1.0f) : 0.0f;
}

// Includes only membership/field inputs. Destinations, lightness shifts,
// protection and strength do not change the spatial fields.
inline std::size_t ColorWarpSelectionFingerprint(const RawColorWarpRecipe& recipe) {
    std::size_t seed = 0x712e531du;
    const auto hash = [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull +
            (seed << 6u) + (seed >> 2u);
    };
    hash(recipe.version);
    hash(recipe.pins.size());
    for (const auto& pin : recipe.pins) {
        hash(pin.id);hash(pin.sourceA);hash(pin.sourceB);hash(pin.regionId);
        for(const auto* range:{&pin.hueRange,&pin.saturationRange,&pin.exposureRange}) {
            hash(range->enabled);hash(range->low);hash(range->high);hash(range->featherLow);hash(range->featherHigh);
        }
    }
    hash(recipe.regions.size());
    for (const auto& region : recipe.regions) {
        hash(region.id); hash(static_cast<int>(region.spatialMode));
        hash(region.reachPixels); hash(region.spatialSupport); hash(region.edgeStop);
        hash(region.featherPixels); hash(static_cast<int>(region.featherDirection));
        hash(region.circles.size());
        for (const auto& circle : region.circles) {
            hash(circle.id); hash(circle.centerU); hash(circle.centerV);
            hash(circle.radiusU); hash(circle.radiusV);
            hash(static_cast<int>(circle.interpretation));
            hash(static_cast<int>(circle.polarity));
        }
    }
    return seed == 0u ? 1u : seed;
}

} // namespace Stack::RawRecipe
