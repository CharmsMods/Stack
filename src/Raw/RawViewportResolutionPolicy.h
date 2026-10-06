#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Raw {
struct ViewportResolutionCandidate {
    int edge = 0;
    double milliseconds = 0;
};
template <std::size_t N>
int SelectViewportResolution(const std::array<ViewportResolutionCandidate,N>& candidates, int fps) {
    double fastest = std::numeric_limits<double>::infinity();
    for (const auto& candidate : candidates)
        if (candidate.edge > 0 && std::isfinite(candidate.milliseconds) && candidate.milliseconds >= 0)
            fastest = std::min(fastest,candidate.milliseconds);
    if (!std::isfinite(fastest)) return 0;
    // Compare complete render plans, including their available inputs. An
    // uncached rebuild's fixed cost must not set the budget for a cached edit.
    const double budget = std::max(1000.0/std::max(1,fps),fastest*1.05);
    int edge = 0;
    for (const auto& candidate : candidates)
        if (std::isfinite(candidate.milliseconds) && candidate.milliseconds >= 0 && candidate.milliseconds <= budget)
            edge = std::max(edge,candidate.edge);
    return edge;
}
}
