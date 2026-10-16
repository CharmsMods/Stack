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
    int fadeBelowFps = Raw::kDefaultViewportFadeBelowFps) {
    const double oldDensity = previous.region.Valid() ?
        double(previous.width) * previous.region.fullWidth / previous.region.width : previous.width;
    const double newDensity = next.region.Valid() ?
        double(next.width) * next.region.fullWidth / next.region.width : next.width;
    const bool refinement = oldDensity > 0 && newDensity > oldDensity * 1.01;
    if (!CompatibleFrameGeometry(previous, next) ||
        next.width <= 0 || next.height <= 0 || previous.width <= 0 || previous.height <= 0 ||
        previous.completedAt < 0 || !std::isfinite(workMilliseconds) || workMilliseconds <= 0 ||
        (workMilliseconds < 1000.0 / Raw::ClampViewportFadeBelowFps(fadeBelowFps) && !refinement))
        return 0;
    const double cadence = next.completedAt - previous.completedAt;
    if (!std::isfinite(cadence) || cadence <= 0) return 0;
    // Follow actual completions, excluding idle time before an edit. Keep the
    // blend shorter than the cadence so it does not add a queue of old frames.
    const double duration = 0.6 * std::min(cadence, workMilliseconds / 1000.0);
    return refinement ? std::clamp(duration, .020, .20) : std::clamp(duration, 0.0, .20);
}
}
