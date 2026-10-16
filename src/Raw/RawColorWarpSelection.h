#pragma once

#include "Raw/RawColorWarpMask.h"

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
    if (!pin.enabled || pin.strength <= 0.0001f ||
        EvaluateColorWarpPinShapeDistance(pin, input.a, input.b) >= 1.0f) return 0.0f;
    const float direct = EvaluateColorWarpPinShapeWeight(pin, input.a, input.b) *
        EvaluateColorWarpPinLightnessWeight(pin, input.sceneEv);
    return std::max(direct * mask.gate, mask.support * spatialSupport) *
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
        std::max(1.0f, totalWeight), 0.0f, 1.0f) : 0.0f;
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
        hash(pin.enabled);
        hash(pin.id);
        if (!pin.enabled) continue;
        hash(pin.sourceA); hash(pin.sourceB); hash(pin.radius); hash(pin.softness);
        hash(pin.qualifierDirectionality); hash(pin.qualifierOrientationRadians);
        hash(pin.qualifierAperture); hash(pin.regionId);
        for (const float sample : pin.evCurve.samples) hash(sample);
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
