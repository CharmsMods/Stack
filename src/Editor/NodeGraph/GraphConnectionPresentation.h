#pragma once

#include "App/settings/AppearanceTheme.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace EditorNodeGraph::ConnectionPresentation {

struct HorizontalRange {
    float minimum = 0.0f;
    float maximum = 0.0f;
    bool HasUsableSpace() const { return maximum > minimum; }
};

struct RotatedBounds {
    ImVec2 minimum {};
    ImVec2 maximum {};
};

inline constexpr float kConnectionLabelRevealMinimumAlpha = 0.001f;

inline bool ConnectionLabelsAreVisible(bool revealKeyDown, float revealAlpha) {
    return revealKeyDown || revealAlpha > kConnectionLabelRevealMinimumAlpha;
}

inline float ConnectionTextSize(
    float baseSize,
    float zoom,
    StackAppearance::GraphConnectionTextSizing sizing) {
    const float clampedBase = std::clamp(
        baseSize,
        StackAppearance::kGraphConnectionTextSizeMin,
        StackAppearance::kGraphConnectionTextSizeMax);
    if (sizing == StackAppearance::GraphConnectionTextSizing::Fixed) {
        return clampedBase;
    }
    const float zoomScale = std::clamp(std::sqrt(std::max(0.0f, zoom)), 0.75f, 1.35f);
    return clampedBase * zoomScale;
}

inline float NormalizeUprightAngle(float radians) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kHalfPi = kPi * 0.5f;
    while (radians > kPi) radians -= 2.0f * kPi;
    while (radians <= -kPi) radians += 2.0f * kPi;
    if (radians > kHalfPi) radians -= kPi;
    if (radians < -kHalfPi) radians += kPi;
    return radians;
}

inline ImVec2 NormalizedTangent(const ImVec2& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y);
    if (length <= 1e-5f) return ImVec2(1.0f, 0.0f);
    ImVec2 tangent(value.x / length, value.y / length);
    if (tangent.x < 0.0f) {
        tangent.x = -tangent.x;
        tangent.y = -tangent.y;
    }
    return tangent;
}

inline ImVec2 CubicBezierTangent(
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - clampedT;
    return NormalizedTangent(ImVec2(
        3.0f * u * u * (p1.x - p0.x) +
            6.0f * u * clampedT * (p2.x - p1.x) +
            3.0f * clampedT * clampedT * (p3.x - p2.x),
        3.0f * u * u * (p1.y - p0.y) +
            6.0f * u * clampedT * (p2.y - p1.y) +
            3.0f * clampedT * clampedT * (p3.y - p2.y)));
}

inline RotatedBounds BoundsForRotatedRect(
    const ImVec2& center,
    const ImVec2& size,
    float radians) {
    const float c = std::abs(std::cos(radians));
    const float s = std::abs(std::sin(radians));
    const float halfWidth = (size.x * c + size.y * s) * 0.5f;
    const float halfHeight = (size.x * s + size.y * c) * 0.5f;
    return RotatedBounds{
        ImVec2(center.x - halfWidth, center.y - halfHeight),
        ImVec2(center.x + halfWidth, center.y + halfHeight)
    };
}

inline HorizontalRange PinLabelRange(
    float contentMinimumX,
    float contentMaximumX,
    float pinX,
    bool input,
    float pinPadding,
    float centerPadding) {
    const float centerX = (contentMinimumX + contentMaximumX) * 0.5f;
    return input
        ? HorizontalRange{ pinX + pinPadding, centerX - centerPadding }
        : HorizontalRange{ centerX + centerPadding, pinX - pinPadding };
}

inline int VisibleLineCount(
    StackAppearance::GraphConnectionLabelVisibility visibility,
    float zoom,
    float screenLength,
    bool interactionReveal) {
    using Visibility = StackAppearance::GraphConnectionLabelVisibility;
    if (visibility == Visibility::Off) return 0;
    if (interactionReveal || visibility == Visibility::Always) return 2;
    if (visibility == Visibility::InteractionOnly) return 0;
    if (zoom >= 0.75f && screenLength >= 140.0f) return 2;
    if (zoom >= 0.55f && screenLength >= 90.0f) return 1;
    return 0;
}

inline bool BreakLineGap(
    float screenLength,
    float textWidth,
    float centerT,
    float& startT,
    float& endT) {
    const float gapWidth = std::max(0.0f, textWidth) + 16.0f;
    const float visibleEachSide = (screenLength - gapWidth) * 0.5f;
    if (screenLength <= 1.0f || visibleEachSide < 24.0f) {
        startT = -1.0f;
        endT = -1.0f;
        return false;
    }
    const float halfGapT = std::min(0.45f, gapWidth / (2.0f * screenLength));
    startT = std::clamp(centerT - halfGapT, 0.0f, 1.0f);
    endT = std::clamp(centerT + halfGapT, 0.0f, 1.0f);
    return endT > startT;
}

} // namespace EditorNodeGraph::ConnectionPresentation
