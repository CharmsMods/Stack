#include "Raw/RawZoneArea.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {

std::vector<RawBezierCurvePoint> ZoneAreaBezierPoints(const RawZoneArea& area,
    float lo, float hi, float bottom, float top) {
    const float dx = std::max(0.001f, hi - lo), dy = std::max(0.001f, top - bottom);
    std::vector<RawBezierCurvePoint> result;
    for (const auto& p : area.points) {
        RawBezierCurvePoint q;
        q.x = (p.ev - lo) / dx;
        q.y = (p.deltaEv + area.offsetEv - bottom) / dy;
        q.incoming = p.incoming;
        q.outgoing = p.outgoing;
        for (auto* h : {&q.incoming, &q.outgoing}) { h->offsetX /= dx; h->offsetY /= dy; }
        result.push_back(q);
    }
    return result;
}

void StoreZoneAreaBezierPoints(RawZoneArea& area, const std::vector<RawBezierCurvePoint>& points,
    float lo, float hi, float bottom, float top) {
    const float dx = std::max(0.001f, hi - lo), dy = std::max(0.001f, top - bottom);
    area.points.clear();
    for (const auto& p : points) {
        RawZoneAreaPoint q;
        q.ev = lo + p.x * dx;
        q.deltaEv = bottom + p.y * dy - area.offsetEv;
        q.incoming = p.incoming;
        q.outgoing = p.outgoing;
        for (auto* h : {&q.incoming, &q.outgoing}) { h->offsetX *= dx; h->offsetY *= dy; }
        area.points.push_back(q);
    }
}

float ZoneAreaCurveGain(const RawZoneArea& area, float ev) {
    if (area.points.empty() || !std::isfinite(ev)) return 0.0f;
    if (ev <= area.points.front().ev) return area.points.front().deltaEv;
    if (ev >= area.points.back().ev) return area.points.back().deltaEv;
    // Fixed normalization preserves tangent geometry through every graph fit.
    auto points = ZoneAreaBezierPoints(area, -32.0f, 32.0f, -32.0f, 32.0f);
    for (auto& p : points) p.y -= area.offsetEv / 64.0f;
    return EvaluateRawBezierCurve(points, (ev + 32.0f) / 64.0f) * 64.0f - 32.0f;
}

float ZoneAreaReferenceEv(float luma) {
    if (!std::isfinite(luma) || luma <= 0.0f) return kZoneMinimumEv;
    return std::log2(luma / 0.18f);
}

bool HasZoneAreaGain(const RawLocalRangeRecipe& recipe) {
    if (!recipe.enabled || recipe.strength <= 0.0f) return false;
    for (const auto& a : recipe.areas) {
        if (!a.enabled || a.strokes.empty()) continue;
        if (std::abs(a.offsetEv) > 0.000001f) return true;
        for (const auto& p : a.points)
            if (std::abs(p.deltaEv) > 0.000001f ||
                (p.incoming.manual && std::abs(p.incoming.offsetY) > 0.000001f) ||
                (p.outgoing.manual && std::abs(p.outgoing.offsetY) > 0.000001f)) return true;
    }
    return false;
}
} // namespace Stack::RawRecipe
