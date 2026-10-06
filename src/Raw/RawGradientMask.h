#pragma once

#include <algorithm>
#include <cmath>
#include <string>

namespace Stack::RawRecipe {

enum class RawGradientShape { Linear, Radial };

// Coordinates refer to the complete oriented image, before output crop.
// Distances and radii are measured in image-height units. The image aspect
// makes rotation and falloff independent of the source pixel aspect ratio.
struct RawGradientMask {
    std::string id;
    int geometryVersion = 1;
    RawGradientShape shape = RawGradientShape::Linear;
    bool enabled = true;
    bool inverted = false;
    float centerU = 0.5f;
    float centerV = 0.5f;
    float angleRadians = 0.0f;
    float lowBoundary = -0.15f;
    float highBoundary = 0.15f;
    float radiusX = 0.25f;
    float radiusY = 0.25f;
    float innerScale = 0.65f;
};

inline float EvaluateRawGradientMask(
    const RawGradientMask& mask, float u, float v, float imageAspect,
    float cs, float sn) {
    const float aspect = std::max(imageAspect, 0.001f);
    const float x = (u - mask.centerU) * aspect;
    const float y = v - mask.centerV;
    float weight = 0.0f;
    if (mask.shape == RawGradientShape::Linear) {
        const float distance = x * cs + y * sn;
        const float span = std::max(mask.highBoundary - mask.lowBoundary, 0.0001f);
        const float t = std::clamp((distance - mask.lowBoundary) / span, 0.0f, 1.0f);
        weight = 1.0f - t * t * (3.0f - 2.0f * t);
    } else {
        const float localX = x * cs + y * sn;
        const float localY = -x * sn + y * cs;
        const float nx = localX / std::max(mask.radiusX, 0.0001f);
        const float ny = localY / std::max(mask.radiusY, 0.0001f);
        const float radius = std::sqrt(nx * nx + ny * ny);
        const float span = std::max(1.0f - mask.innerScale, 0.0001f);
        const float t = std::clamp((radius - mask.innerScale) / span, 0.0f, 1.0f);
        weight = 1.0f - t * t * (3.0f - 2.0f * t);
    }
    return mask.inverted ? 1.0f - weight : weight;
}

inline float EvaluateRawGradientMask(
    const RawGradientMask& mask, float u, float v, float imageAspect) {
    return EvaluateRawGradientMask(mask, u, v, imageAspect,
        std::cos(mask.angleRadians), std::sin(mask.angleRadians));
}

} // namespace Stack::RawRecipe
