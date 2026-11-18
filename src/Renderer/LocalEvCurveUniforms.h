#pragma once

#include "Raw/Curves/LocalEvCurveNormalization.h"

#include <algorithm>
#include <array>

namespace Stack::Renderer {

struct LocalEvCurveUniforms {
    int pointCount = 0;
    std::array<std::array<float, 2>, 12> points{};
    std::array<std::array<float, 4>, 11> handles{};
    float globalOffsetEv = 0;
    bool curveActive = false;
    bool lightActive = false;
};

// Receives the same sanitized recipe used by image and overlay execution.
inline LocalEvCurveUniforms PrepareLocalEvCurveUniforms(
    const Stack::RawRecipe::RawLocalRangeRecipe& recipe) {
    LocalEvCurveUniforms result;
    result.pointCount = std::min<int>(static_cast<int>(recipe.points.size()), 12);
    const auto segments = Stack::RawRecipe::BuildRawBezierSegments(
        Stack::RawRecipe::RawLocalRangeBezierPoints(recipe));
    // Only an exactly zero total curve can bypass the inverse solve. Manual
    // handles can bend a curve whose anchors are all zero, so include them.
    result.curveActive = std::any_of(recipe.points.begin(), recipe.points.end(),
        [](const auto& point) { return point.deltaEv != 0.f; });
    for (const auto& segment : segments)
        result.curveActive |= segment.leftHandleY != .5f || segment.rightHandleY != .5f;
    result.lightActive = std::any_of(recipe.light.amounts.begin(), recipe.light.amounts.end(),
        [](float amount) { return amount != 0.f; });
    if (result.curveActive)
        result.globalOffsetEv = Stack::RawRecipe::LocalEvGlobalOffsetEv(recipe);
    const float span = std::max(.1f, recipe.maxEv - recipe.minEv);
    for (int i = 0; i < result.pointCount; ++i)
        result.points[i] = {recipe.points[i].ev, recipe.points[i].deltaEv - result.globalOffsetEv};
    for (int i = 0; i + 1 < result.pointCount; ++i) {
        const auto& segment = segments[static_cast<std::size_t>(i)];
        result.handles[i] = {
            recipe.minEv + segment.leftHandleX * span,
            -4.f + segment.leftHandleY * 8.f - result.globalOffsetEv,
            recipe.minEv + segment.rightHandleX * span,
            -4.f + segment.rightHandleY * 8.f - result.globalOffsetEv};
    }
    return result;
}

inline bool LocalEvNeedsLightAnalysis(const Stack::RawRecipe::RawLocalRangeRecipe& recipe,
    const LocalEvCurveUniforms& curve) {
    return recipe.smoothness > .0001f || (recipe.light.automatic && curve.lightActive);
}

} // namespace Stack::Renderer
