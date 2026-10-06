#pragma once
#include <algorithm>
#include <limits>

namespace Raw {
constexpr int kMinimumInteractiveViewportEdge = 128;
constexpr int kDefaultViewportFadeBelowFps = 30;
constexpr int kMinimumViewportFadeBelowFps = 5;
constexpr int kMaximumViewportFadeBelowFps = std::numeric_limits<int>::max();
inline int ClampViewportFadeBelowFps(int fps) {
    return std::clamp(fps, kMinimumViewportFadeBelowFps, kMaximumViewportFadeBelowFps);
}
constexpr int kDefaultViewportTargetFps = 30;
constexpr int kMinimumViewportTargetFps = 5;
constexpr int kMaximumViewportTargetFps = std::numeric_limits<int>::max();
inline int ClampViewportTargetFps(int fps) {
    return std::clamp(fps, kMinimumViewportTargetFps, kMaximumViewportTargetFps);
}
inline int ResolveViewportTargetFps(int requested, int monitorRefreshRate) {
    const int maximum = std::max(kMinimumViewportTargetFps,
        monitorRefreshRate > 0 ? monitorRefreshRate : 60);
    return std::clamp(requested, kMinimumViewportTargetFps, maximum);
}
}
