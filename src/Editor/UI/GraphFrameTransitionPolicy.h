#pragma once
#include "Editor/GraphRenderPolicy.h"
#include "Renderer/ViewportPreviewTypes.h"
#include "Raw/RawViewportSettings.h"
#include <algorithm>
#include <cmath>

namespace Stack::GraphRendering {
struct FramePresentation {
    RequestTag identity;
    int width = 0, height = 0;
    bool encoded = false;
    double completedAt = -1.0;
    Stack::Renderer::ImageRegion region;
    int logicalWidth = 0, logicalHeight = 0;
    std::uint64_t viewGeneration = 0;
    bool diagnostic = false;
};

inline int FrameLogicalWidth(const FramePresentation& frame) {
    return frame.logicalWidth > 0 ? frame.logicalWidth : frame.width;
}
inline int FrameLogicalHeight(const FramePresentation& frame) {
    return frame.logicalHeight > 0 ? frame.logicalHeight : frame.height;
}
inline bool CompatibleFrameGeometry(const FramePresentation& previous, const FramePresentation& next) {
    return !previous.diagnostic && !next.diagnostic && SameContext(previous.identity, next.identity) &&
        FrameLogicalWidth(previous) == FrameLogicalWidth(next) &&
        FrameLogicalHeight(previous) == FrameLogicalHeight(next) &&
        previous.encoded == next.encoded && previous.viewGeneration == next.viewGeneration;
}

inline double FrameBlendDuration(const FramePresentation& previous,
    const FramePresentation& next, double workMilliseconds,
    int fadeBelowFps = Raw::kDefaultViewportFadeBelowFps,
    int targetFps = Raw::kDefaultViewportTargetFps) {
    const double oldDensity = previous.region.Valid() ?
        double(previous.width) * previous.region.fullWidth / previous.region.width : previous.width;
    const double newDensity = next.region.Valid() ?
        double(next.width) * next.region.fullWidth / next.region.width : next.width;
    const bool refinement = oldDensity > 0 && newDensity > oldDensity * 1.01;
    if (!CompatibleFrameGeometry(previous, next) ||
        next.width <= 0 || next.height <= 0 || previous.width <= 0 || previous.height <= 0 ||
        previous.completedAt < 0)
        return 0;
    const double cadence = next.completedAt - previous.completedAt;
    if (!std::isfinite(cadence) || cadence <= 0 ||
        (cadence < 1.0 / Raw::ClampViewportFadeBelowFps(fadeBelowFps) && !refinement)) return 0;
    // The threshold describes visible update cadence, including intentional
    // FPS pacing, not just worker cost. Bound duration separately so an idle
    // gap does not make the first cheap edit fade for hundreds of milliseconds.
    const double workSeconds = std::isfinite(workMilliseconds)
        ? std::max(0.0, workMilliseconds) / 1000.0 : 0.0;
    const double expectedInterval = 1.0 / Raw::ClampViewportTargetFps(targetFps);
    const double duration = 0.6 * std::min(cadence, std::max(workSeconds, expectedInterval));
    return refinement ? std::clamp(duration, .020, .20) : std::clamp(duration, 0.0, .20);
}
}
