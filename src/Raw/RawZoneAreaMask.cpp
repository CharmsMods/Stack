#include "Raw/RawZoneArea.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
namespace {
float SegmentCoverage(const RawZoneBrushStroke& s, float aspect,
    const RawZoneBrushPoint& a, const RawZoneBrushPoint& b, float u, float v) {
    const float sx = std::max(1.0f, aspect), sy = std::max(1.0f, 1.0f / aspect);
    const float x = (u - a.u) * sx, y = (v - a.v) * sy;
    const float dx = (b.u - a.u) * sx, dy = (b.v - a.v) * sy;
    const float t = std::clamp((x * dx + y * dy) / std::max(1e-20f, dx * dx + dy * dy), 0.0f, 1.0f);
    const float distance = std::hypot(x - dx * t, y - dy * t);
    if (distance >= s.radius) return 0.0f;
    const float inner = s.radius * (1.0f - s.softness);
    const float f = std::clamp((distance - inner) / std::max(1e-12f, s.radius - inner), 0.0f, 1.0f);
    // Quintic falloff has zero first and second derivatives at both ends.
    const float q = 1.0f - f;
    return s.opacity * q * q * q * (10.0f - 15.0f * q + 6.0f * q * q);
}
}
RawZoneBrushPoint ZoneAreaSourcePoint(float u, float v, const RawCropRotationRecipe& c, bool cropped) {
    if (cropped && c.cropEnabled) { u = c.cropX + u * c.cropWidth; v = c.cropY + v * c.cropHeight; }
    if (c.flipHorizontally) u = 1 - u;
    if (c.flipVertically) v = 1 - v;
    switch (c.rotationDegrees) {
        case 90: return {v, 1 - u};
        case 180: return {1 - u, 1 - v};
        case 270: return {1 - v, u};
        default: return {u, v};
    }
}
RawZoneBrushPoint ZoneAreaDisplayPoint(float u, float v, const RawCropRotationRecipe& c, bool cropped) {
    RawZoneBrushPoint p {u, v};
    switch (c.rotationDegrees) {
        case 90: p = {1 - v, u}; break;
        case 180: p = {1 - u, 1 - v}; break;
        case 270: p = {v, 1 - u}; break;
    }
    if (c.flipHorizontally) p.u = 1 - p.u;
    if (c.flipVertically) p.v = 1 - p.v;
    if (cropped && c.cropEnabled) { p.u = (p.u - c.cropX) / c.cropWidth; p.v = (p.v - c.cropY) / c.cropHeight; }
    return p;
}
float ZoneStrokeCoverage(const RawZoneBrushStroke& s, float aspect, float u, float v) {
    float mask = 0;
    for (std::size_t i = 0; i < s.path.size(); ++i)
        mask = std::max(mask, SegmentCoverage(s, aspect, s.path[i ? i - 1 : 0], s.path[i], u, v));
    return mask;
}
float ZoneAreaCoverage(const RawZoneArea& a, float u, float v) {
    float mask = 0;
    for (const auto& s : a.strokes) {
        const float coverage = ZoneStrokeCoverage(s, a.sourceAspect, u, v);
        mask = s.erase ? std::min(mask, 1.0f - coverage) : std::max(mask, coverage);
    }
    return mask;
}
std::vector<float> RasterizeZoneArea(const RawZoneArea& area, int width, int height,
    const RawCropRotationRecipe& transform, const std::function<bool()>& cancelled, const ImageGuide* guide) {
    if (width <= 0 || height <= 0) return {};
    if (ZoneAreaUsesGuidance(area)) {
        if (!guide || !guide->Valid() || guide->width != width || guide->height != height) return {};
        return RasterizeGuidedZoneArea(area, width, height, transform, *guide, cancelled);
    }
    std::vector<float> mask(static_cast<std::size_t>(width) * height, 0.0f);
    const bool rotated = transform.rotationDegrees == 90 || transform.rotationDegrees == 270;
    const float aspect = rotated ? 1 / area.sourceAspect : area.sourceAspect;
    const float sx = std::max(1.0f, aspect), sy = std::max(1.0f, 1.0f / aspect);
    for (const auto& authored : area.strokes) {
        auto s = authored;
        for (auto& p : s.path) p = ZoneAreaDisplayPoint(p.u, p.v, transform, false);
        for (std::size_t i = 0; i < s.path.size(); ++i) {
            if (cancelled && cancelled()) return {};
            const auto& a = s.path[i ? i - 1 : 0]; const auto& b = s.path[i];
            const int x0 = std::max(0, int(std::floor((std::min(a.u, b.u) - s.radius / sx) * width)));
            const int x1 = std::min(width, int(std::ceil((std::max(a.u, b.u) + s.radius / sx) * width)));
            const int y0 = std::max(0, int(std::floor((std::min(a.v, b.v) - s.radius / sy) * height)));
            const int y1 = std::min(height, int(std::ceil((std::max(a.v, b.v) + s.radius / sy) * height)));
            for (int y = y0; y < y1; ++y) {
                if ((y & 63) == 0 && cancelled && cancelled()) return {};
                for (int x = x0; x < x1; ++x) {
                    const float alpha = SegmentCoverage(s, aspect, a, b, (x + 0.5f) / width, (y + 0.5f) / height);
                    float& value = mask[static_cast<std::size_t>(height - 1 - y) * width + x];
                    value = s.erase ? std::min(value, 1.0f - alpha) : std::max(value, alpha);
                }
            }
        }
    }
    return mask;
}
} // namespace Stack::RawRecipe
