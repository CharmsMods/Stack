#pragma once

#include "Renderer/ViewportPreviewTypes.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Stack::EditorRendering {

// Normalized top-left image coordinates preserve fractional coverage across
// different sample grids; integer pixel rounding could hide an uncovered gap.
struct ViewportSectionDrawRect {
    double left = 0, top = 0, right = 0, bottom = 0;
    bool Valid() const { return left < right && top < bottom; }
};

struct ViewportSectionPreviewDrawPlan {
    std::array<ViewportSectionDrawRect, 4> clips{};
    std::size_t count = 0;
};

// The caller supplies the current accepted base and, when present, its matching
// overview. The overview is drawn only outside the base's resident rectangle.
// A section display copy may fill gaps, but cannot reduce detail on either axis.
inline ViewportSectionPreviewDrawPlan PlanViewportSectionPreviewDraw(
    const Renderer::ImageRegion& core, const Renderer::ImageRegion& raster,
    int previewWidth, int previewHeight, const Renderer::ViewportPreviewGeometry& base,
    int overviewWidth = 0, int overviewHeight = 0) {
    ViewportSectionPreviewDrawPlan plan;
    if (!core.Valid() || !raster.Valid() || !base.Valid() ||
        core.fullWidth != raster.fullWidth || core.fullHeight != raster.fullHeight ||
        core.fullWidth != base.logicalWidth || core.fullHeight != base.logicalHeight ||
        !Renderer::ContainsImageRegion(raster, core) || previewWidth <= 0 || previewHeight <= 0)
        return plan;
    const auto finer = [&](int sampleWidth, int sampleHeight) {
        // Compare logical pixels per texel without rounded quarter-scale or
        // floating-point estimates, including small partial edge sections.
        return std::int64_t(core.fullWidth) * previewWidth < std::int64_t(raster.width) * sampleWidth ||
            std::int64_t(core.fullHeight) * previewHeight < std::int64_t(raster.height) * sampleHeight;
    };
    const bool finerBase = finer(base.resident.fullWidth, base.resident.fullHeight);
    const bool finerOverview = overviewWidth > 0 && overviewHeight > 0 && finer(overviewWidth, overviewHeight);
    const auto rectangle = [](const Renderer::ImageRegion& region) {
        return ViewportSectionDrawRect{double(region.x) / region.fullWidth, double(region.y) / region.fullHeight,
            double(region.x + region.width) / region.fullWidth, double(region.y + region.height) / region.fullHeight};
    };
    const auto section = rectangle(core), coverage = rectangle(base.resident);
    const ViewportSectionDrawRect overlap{std::max(section.left, coverage.left), std::max(section.top, coverage.top),
        std::min(section.right, coverage.right), std::min(section.bottom, coverage.bottom)};
    const auto append = [&](ViewportSectionDrawRect rect) {
        if (rect.Valid()) plan.clips[plan.count++] = rect;
    };
    if (finerOverview) {
        // Only the base rectangle can improve: the overview owns every gap.
        if (!finerBase) append(overlap);
    } else if (!finerBase || !overlap.Valid()) {
        append(section);
    } else {
        // Four disjoint strips keep partial coverage sharp without skipping
        // the rest of a section or drawing its translucent tint twice.
        append({section.left, section.top, section.right, overlap.top});
        append({section.left, overlap.bottom, section.right, section.bottom});
        append({section.left, overlap.top, overlap.left, overlap.bottom});
        append({overlap.right, overlap.top, section.right, overlap.bottom});
    }
    return plan;
}

} // namespace Stack::EditorRendering
