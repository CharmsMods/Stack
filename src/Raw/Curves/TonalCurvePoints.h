#pragma once

#include "Raw/Curves/CurveTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Stack::RawRecipe {

float EvaluateRawBezierCurve(const std::vector<RawBezierCurvePoint>& points, float x);

enum class TonalCurveEditResult { Unchanged, Changed, CapacityReached, Conflict };

template<class Point>
inline bool IsTonalCurvePoint(const Point& point) {
    return point.tonal.region >= 0 && point.tonal.region < 5;
}

inline bool RemoveTonalCurvePoint(std::vector<RawBezierCurvePoint>& points, std::size_t index) {
    if (index >= points.size() || !IsTonalCurvePoint(points[index])) return false;
    auto& point = points[index];
    if (point.tonal.replacesPoint) {
        // Ownership never moves x. Keeping the current coordinate also
        // preserves a Local EV anchor if its authored plotting bounds change.
        point.y = point.tonal.baselineY;
        point.incoming = point.tonal.incoming;
        point.outgoing = point.tonal.outgoing;
        point.tonal = {};
    } else {
        points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
    }
    return true;
}

inline bool RemoveNeutralTonalCurvePoints(std::vector<RawBezierCurvePoint>& points) {
    bool changed = false;
    for (std::size_t i = points.size(); i-- > 0;) {
        if (IsTonalCurvePoint(points[i]) && points[i].y == points[i].tonal.baselineY)
            changed |= RemoveTonalCurvePoint(points, i);
    }
    return changed;
}

inline bool SameCurvePoints(const std::vector<RawBezierCurvePoint>& a,
    const std::vector<RawBezierCurvePoint>& b) {
    if (a.size() != b.size()) return false;
    const auto sameHandle = [](const auto& x, const auto& y) {
        return x.strength == y.strength && x.manual == y.manual &&
            x.offsetX == y.offsetX && x.offsetY == y.offsetY;
    };
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.x != y.x || x.y != y.y || !sameHandle(x.incoming, y.incoming) ||
            !sameHandle(x.outgoing, y.outgoing) || x.tonal.region != y.tonal.region ||
            x.tonal.baselineY != y.tonal.baselineY || x.tonal.replacesPoint != y.tonal.replacesPoint ||
            x.tonal.originalX != y.tonal.originalX ||
            !sameHandle(x.tonal.incoming, y.tonal.incoming) ||
            !sameHandle(x.tonal.outgoing, y.tonal.outgoing)) return false;
    }
    return true;
}

inline float TonalCurveSliderValue(const std::vector<RawBezierCurvePoint>& points, int region) {
    for (const auto& point : points) {
        if (!IsTonalCurvePoint(point) || point.tonal.region != region) continue;
        const float baseline = point.tonal.baselineY;
        const float delta = point.y - baseline;
        const float headroom = delta >= 0.0f ? 1.0f - baseline : baseline;
        return headroom > 0.0f ? std::clamp(delta / headroom, -1.0f, 1.0f) : 0.0f;
    }
    return 0.0f;
}

// Existing ownership keeps its authored x. fixedX applies only when a region
// first acquires a point; a display/domain redraw never relocates that point.
inline TonalCurveEditResult SetTonalCurveSlider(std::vector<RawBezierCurvePoint>& points,
    int region, float fixedX, float value, std::size_t maximumPoints) {
    if (region < 0 || region >= 5 || !std::isfinite(value))
        return TonalCurveEditResult::Conflict;
    value = std::clamp(value, -1.0f, 1.0f);
    const auto owned = std::find_if(points.begin(), points.end(), [region](const auto& point) {
        return IsTonalCurvePoint(point) && point.tonal.region == region;
    });
    if (value == 0.0f) {
        if (owned == points.end()) return TonalCurveEditResult::Unchanged;
        RemoveTonalCurvePoint(points, static_cast<std::size_t>(owned - points.begin()));
        return TonalCurveEditResult::Changed;
    }
    if (owned != points.end()) {
        const float baseline = owned->tonal.baselineY;
        const float y = baseline + value * (value >= 0.0f ? 1.0f - baseline : baseline);
        if (y == baseline) {
            RemoveTonalCurvePoint(points, static_cast<std::size_t>(owned - points.begin()));
            return TonalCurveEditResult::Changed;
        }
        if (owned->y == y) return TonalCurveEditResult::Unchanged;
        owned->y = y;
        return TonalCurveEditResult::Changed;
    }
    if (!std::isfinite(fixedX) || fixedX <= 0.0f || fixedX >= 1.0f || points.size() < 2)
        return TonalCurveEditResult::Conflict;
    // Match the editor's minimum point spacing. A near collision is retained
    // at its exact original coordinate and restored when this slider is zero.
    constexpr float spacing = 0.001f;
    auto collision = points.end();
    for (auto it = points.begin(); it != points.end(); ++it) {
        if (std::abs(it->x - fixedX) >= spacing) continue;
        if (collision != points.end() || IsTonalCurvePoint(*it) ||
            it == points.begin() || it + 1 == points.end())
            return TonalCurveEditResult::Conflict;
        collision = it;
    }
    if (collision == points.end() && maximumPoints && points.size() >= maximumPoints)
        return TonalCurveEditResult::CapacityReached;
    const float baseline = collision != points.end() ? collision->y :
        std::clamp(EvaluateRawBezierCurve(points, fixedX), 0.0f, 1.0f);
    const float y = baseline + value * (value >= 0.0f ? 1.0f - baseline : baseline);
    if (y == baseline) return TonalCurveEditResult::Unchanged;
    RawBezierCurvePoint point{fixedX, y, {1.0f}, {1.0f}};
    point.tonal.region = region;
    point.tonal.baselineY = baseline;
    if (collision != points.end()) {
        // Keep a colliding custom anchor's exact x, rather than nudging it to
        // the nominal center; attenuation and later restoration stay exact.
        point.x = collision->x;
        point.tonal.replacesPoint = true;
        point.tonal.originalX = collision->x;
        point.tonal.incoming = collision->incoming;
        point.tonal.outgoing = collision->outgoing;
        *collision = point;
    } else {
        points.insert(std::lower_bound(points.begin(), points.end(), fixedX,
            [](const auto& p, float x) { return p.x < x; }), point);
    }
    return TonalCurveEditResult::Changed;
}

} // namespace Stack::RawRecipe
