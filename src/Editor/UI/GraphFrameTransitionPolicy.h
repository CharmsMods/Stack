#pragma once
#include "Editor/GraphRenderPolicy.h"
#include <algorithm>
#include <cmath>

namespace Stack::GraphRendering {
struct FramePresentation {
    RequestTag identity;
    int width = 0, height = 0;
    bool encoded = false;
    double completedAt = -1.0;
};

inline double FrameBlendDuration(const FramePresentation& previous,
    const FramePresentation& next, double workMilliseconds) {
    if (!SameContext(previous.identity, next.identity) ||
        previous.width != next.width || previous.height != next.height ||
        next.width <= 0 || next.height <= 0 || previous.encoded != next.encoded ||
        previous.completedAt < 0 || !std::isfinite(workMilliseconds) || workMilliseconds < 33.0)
        return 0;
    const double cadence = next.completedAt - previous.completedAt;
    if (!std::isfinite(cadence) || cadence <= 0) return 0;
    // Follow actual completions, excluding idle time before an edit. Keep the
    // blend shorter than the cadence so it does not add a queue of old frames.
    return std::clamp(0.6 * std::min(cadence, workMilliseconds / 1000.0), 0.0, 0.20);
}
}
