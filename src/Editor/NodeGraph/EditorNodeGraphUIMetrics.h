#pragma once

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace EditorNodeGraphUIMetrics {

inline float NodeUiScaleFromZoom(float zoom) {
    return zoom;
}

inline float PinRadiusForZoom(float zoom) {
    return std::max(0.35f, 5.3f * NodeUiScaleFromZoom(zoom));
}

inline float LinkThicknessScaleFromZoom(float zoom) {
    return std::max(0.20f, NodeUiScaleFromZoom(zoom));
}

inline float LinkHitRadiusForZoom(float zoom) {
    return std::max(1.0f, 8.0f * NodeUiScaleFromZoom(zoom));
}

inline float LinkBezierHandle(const ImVec2& p1, const ImVec2& p2) {
    return std::max(0.0f, std::abs(p2.x - p1.x) * 0.45f);
}

inline ImVec2 CubicBezierPoint(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
    const float u = 1.0f - t;
    const float tt = t * t;
    const float uu = u * u;
    const float uuu = uu * u;
    const float ttt = tt * t;
    return ImVec2(
        (uuu * p0.x) + (3.0f * uu * t * p1.x) + (3.0f * u * tt * p2.x) + (ttt * p3.x),
        (uuu * p0.y) + (3.0f * uu * t * p1.y) + (3.0f * u * tt * p2.y) + (ttt * p3.y));
}

inline float DistancePointToSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b) {
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const ImVec2 ap(point.x - a.x, point.y - a.y);
    const float abLenSq = ab.x * ab.x + ab.y * ab.y;
    if (abLenSq <= 1e-6f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f);
    const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
    const float dx = point.x - closest.x;
    const float dy = point.y - closest.y;
    return std::sqrt(dx * dx + dy * dy);
}

inline bool IsPointNearCubicBezier(
    const ImVec2& point,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    float hitRadius) {
    constexpr int kSegments = 28;
    ImVec2 previous = p0;
    for (int index = 1; index <= kSegments; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kSegments);
        const ImVec2 current = CubicBezierPoint(p0, p1, p2, p3, t);
        if (DistancePointToSegment(point, previous, current) <= hitRadius) {
            return true;
        }
        previous = current;
    }
    return false;
}

} // namespace EditorNodeGraphUIMetrics
