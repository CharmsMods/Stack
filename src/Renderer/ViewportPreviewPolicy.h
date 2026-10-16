#pragma once

#include "Renderer/ViewportPreviewTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <iterator>
#include <map>
#include <vector>

namespace Stack::Renderer {

// Resolution limits describe the visible raster, not the longest side of the
// complete photograph. A small crop can have native samples at modest cost.
inline int ViewportPreviewEdgeForVisiblePixels(const ImageRegion& visible,
    int nativeEdge, int visibleEdge) {
    if (!visible.Valid() || nativeEdge <= 0) return 0;
    return std::clamp(int(std::ceil(nativeEdge * std::min(1.0,
        double(std::max(1, visibleEdge)) / std::max(visible.width, visible.height)))), 1, nativeEdge);
}

inline bool ViewportPreviewCoversDisplay(const ViewportPreviewGeometry& geometry,
    const ImageRegion& visible, int physicalWidth, int physicalHeight) {
    if (!geometry.Valid() || physicalWidth <= 0 || physicalHeight <= 0 ||
        !ContainsImageRegion(geometry.resident, visible)) return false;
    if (geometry.NativeSamples()) return true;
    return double(geometry.resident.fullWidth) * visible.width / visible.fullWidth >= physicalWidth &&
        double(geometry.resident.fullHeight) * visible.height / visible.fullHeight >= physicalHeight;
}

inline ImageRegion PadViewportPreviewRegion(const ImageRegion& visible) {
    if (!visible.Valid()) return visible;
    // A small guard band avoids a new job for every pan pixel. Its sample grid
    // and origin remain those of the full image; this is only execution demand.
    const int x = std::max(0, visible.x - visible.width / 8);
    const int y = std::max(0, visible.y - visible.height / 8);
    const int right = visible.x + visible.width +
        std::min(visible.width / 8, visible.fullWidth - visible.x - visible.width);
    const int bottom = visible.y + visible.height +
        std::min(visible.height / 8, visible.fullHeight - visible.y - visible.height);
    return {visible.fullWidth, visible.fullHeight, x, y, right - x, bottom - y};
}

enum class ViewportPreviewLimit { None, Learning, FrameBudget, DetailFloor, FixedWork, Memory };

// The owning executor estimates complete dependencies, regional inputs,
// cache reuse, and peak allocations before handing candidates to this policy.
struct ViewportPreviewCandidate {
    int edge = 0;
    double predictedMs = 0.0;
    bool costKnown = false;
    bool timingCovered = false;
    std::uint64_t workingBytes = 0;
    std::size_t executionIdentity = 0;
};

struct ViewportPreviewPolicyInput {
    int displayEdge = 0;
    int minimumEdge = 1;
    int maximumEdge = 0;
    int targetFps = 30;
    int evaluationEdge = 0;
    std::uint64_t memoryBudgetBytes = 0; // Zero leaves admission to the caller.
    bool preserveDetail = false;
    int initialEdge = 0; // Optional provisional edge derived from visible pixels.
};

struct ViewportPreviewSelection {
    std::size_t candidateIndex = std::numeric_limits<std::size_t>::max();
    int edge = 0;
    double predictedMs = 0.0;
    bool measured = false;
    bool provisional = true;
    ViewportPreviewLimit limit = ViewportPreviewLimit::Learning;
};

inline ViewportPreviewSelection SelectViewportPreviewCandidate(
    const ViewportPreviewPolicyInput& input,
    const std::vector<ViewportPreviewCandidate>& candidates) {
    ViewportPreviewSelection result;
    if (input.displayEdge <= 0 || input.maximumEdge <= 0) return result;
    const int maximum = std::min(input.displayEdge, input.maximumEdge);
    const int minimum = std::clamp(input.minimumEdge, 1, maximum);
    const auto eligible = [&](const ViewportPreviewCandidate& candidate) {
        return candidate.edge >= minimum && candidate.edge <= maximum &&
            (!input.memoryBudgetBytes || candidate.workingBytes <= input.memoryBudgetBytes);
    };
    const auto known = [](const ViewportPreviewCandidate& candidate) {
        return candidate.costKnown && std::isfinite(candidate.predictedMs) && candidate.predictedMs >= 0.0;
    };

    bool allKnown = true;
    bool anyEligible = false;
    bool memoryRejected = false;
    double fastest = std::numeric_limits<double>::infinity();
    for (const auto& candidate : candidates) {
        if (candidate.edge >= minimum && candidate.edge <= maximum &&
            input.memoryBudgetBytes && candidate.workingBytes > input.memoryBudgetBytes) memoryRejected = true;
        if (!eligible(candidate)) continue;
        anyEligible = true;
        allKnown &= known(candidate);
        if (known(candidate)) fastest = std::min(fastest, candidate.predictedMs);
    }
    if (!anyEligible) {
        if (memoryRejected) result.limit = ViewportPreviewLimit::Memory;
        return result;
    }

    const double target = 1000.0 / std::max(1, input.targetFps);
    const double budget = std::max(target, std::isfinite(fastest) ? fastest * 1.05 : target);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        if (eligible(candidate) && known(candidate) && candidate.predictedMs <= budget && candidate.edge >= result.edge) {
            result.candidateIndex = i;
            result.edge = candidate.edge;
        }
    }
    if (!result.edge) {
        // Missing evidence stays provisional. Choose an executable candidate
        // near the initial display cap, never invent an unplanned raster edge.
        const int provisionalEdge = std::clamp(input.initialEdge > 0 ? input.initialEdge :
            std::min(input.displayEdge, 1024), minimum, maximum);
        int distance = std::numeric_limits<int>::max();
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& candidate = candidates[i];
            if (!eligible(candidate)) continue;
            const int candidateDistance = std::abs(candidate.edge - provisionalEdge);
            if (candidateDistance < distance || (candidateDistance == distance && candidate.edge < result.edge)) {
                distance = candidateDistance;
                result.candidateIndex = i;
                result.edge = candidate.edge;
            }
        }
    }

    const auto describe = [&]() {
        const auto& selected = candidates[result.candidateIndex];
        result.measured = known(selected);
        result.predictedMs = result.measured ? selected.predictedMs : 0.0;
        result.provisional = !allKnown || !selected.timingCovered;
    };
    describe();
    result.limit = !result.measured ? ViewportPreviewLimit::Learning :
        result.predictedMs > target * 1.05 ?
            (input.preserveDetail ? ViewportPreviewLimit::DetailFloor : ViewportPreviewLimit::FixedWork) :
        result.edge < maximum ? ViewportPreviewLimit::FrameBudget : ViewportPreviewLimit::None;
    if (result.limit == ViewportPreviewLimit::FixedWork && !allKnown) result.limit = ViewportPreviewLimit::Learning;

    // A caller may report the existing dispatched size during stabilization.
    // Only a supplied, admitted candidate can represent that execution.
    if (input.evaluationEdge > 0) {
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i].edge == input.evaluationEdge && eligible(candidates[i])) {
                result.candidateIndex = i;
                result.edge = candidates[i].edge;
                describe();
                if (result.limit == ViewportPreviewLimit::None && result.edge < maximum)
                    result.limit = ViewportPreviewLimit::FrameBudget;
                break;
            }
        }
    }
    if ((input.maximumEdge < input.displayEdge &&
            (result.edge >= maximum || input.maximumEdge < input.minimumEdge)) ||
        (memoryRejected && result.edge < maximum && result.limit == ViewportPreviewLimit::None))
        result.limit = ViewportPreviewLimit::Memory;
    if (memoryRejected) {
        for (const auto& candidate : candidates) {
            if (candidate.edge > result.edge && candidate.edge >= minimum && candidate.edge <= maximum &&
                candidate.workingBytes > input.memoryBudgetBytes && known(candidate) && candidate.predictedMs <= budget)
                result.limit = ViewportPreviewLimit::Memory;
        }
    }
    return result;
}

struct ViewportPreviewTimingEstimate {
    double predictedMs = 0.0;
    bool costKnown = false;
    bool timingCovered = false;
    bool provisional = true;
};

// Each view owns its timing curve. The adapter supplies a compatible execution
// identity that excludes camera position/generation and separates full-input
// requirements, cache residency, quality, and regional workload shape.
class ViewportPreviewTimingCurve {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto MaximumAge = std::chrono::seconds(30);

    void Record(std::size_t identity, int edge, double completedMs, bool cold = false,
        Clock::time_point now = Clock::now()) {
        if (cold || !identity || edge <= 0 || !std::isfinite(completedMs) || completedMs <= 0.0) return;
        if (m_Contexts.size() >= 64 && !m_Contexts.count(identity)) {
            const auto oldest = std::min_element(m_Contexts.begin(), m_Contexts.end(), [](const auto& a, const auto& b) {
                return a.second.lastUse < b.second.lastUse;
            });
            m_Contexts.erase(oldest);
        }
        auto& context = m_Contexts[identity];
        context.lastUse = ++m_Sequence;
        const auto previous = context.sizes.find(edge);
        if (previous != context.sizes.end() && now < previous->second.observedAt) return;
        for (auto it = context.sizes.begin(); it != context.sizes.end();) {
            if (now >= it->second.observedAt && now - it->second.observedAt > MaximumAge)
                it = context.sizes.erase(it);
            else ++it;
        }
        if (context.sizes.size() >= 16 && !context.sizes.count(edge)) {
            const auto oldest = std::min_element(context.sizes.begin(), context.sizes.end(), [](const auto& a, const auto& b) {
                return a.second.lastUse < b.second.lastUse;
            });
            context.sizes.erase(oldest);
        }
        auto& sample = context.sizes[edge];
        sample.lastUse = m_Sequence;
        // A sustained change in GPU load/cache behavior invalidates the other
        // sizes too. Otherwise old fast points can keep winning selection
        // while the currently selected size slowly learns the new workload.
        const int direction = sample.count >= 3 ?
            (completedMs > sample.milliseconds * 1.5 ? 1 :
             completedMs < sample.milliseconds / 1.5 ? -1 : 0) : 0;
        if (direction && direction == sample.driftDirection) ++sample.driftCount;
        else { sample.driftDirection = direction; sample.driftCount = direction ? 1 : 0; }
        if (sample.driftCount >= 3) {
            const auto lastUse = sample.lastUse;
            context.sizes.clear();
            auto& fresh = context.sizes[edge];
            fresh.milliseconds = completedMs; fresh.count = 1;
            fresh.lastUse = lastUse; fresh.observedAt = now;
            return;
        }
        // A transient change does not refresh old evidence. Three consecutive
        // measurements in the same direction establish a new cost regime.
        if (direction) return;
        sample.milliseconds = sample.count ? sample.milliseconds + 0.25 * (completedMs - sample.milliseconds) : completedMs;
        sample.count = std::min(sample.count + 1u, 32u);
        sample.observedAt = now;
    }

    ViewportPreviewTimingEstimate Estimate(std::size_t identity, int edge,
        Clock::time_point now = Clock::now()) const {
        if (!identity || edge <= 0) return {};
        const auto found = m_Contexts.find(identity);
        if (found == m_Contexts.end() || found->second.sizes.empty()) return {};
        // At most sixteen points. Ignore each expired point independently;
        // activity at one size must not keep stale sizes alive indefinitely.
        std::array<std::pair<int, const Sample*>, 16> sizes {};
        std::size_t count = 0;
        for (const auto& point : found->second.sizes)
            if (Fresh(point.second, now)) sizes[count++] = {point.first, &point.second};
        if (!count) return {};
        const auto end = sizes.begin() + count;
        const auto exact = std::find_if(sizes.begin(), end, [edge](const auto& point) { return point.first == edge; });
        if (exact != end) return {exact->second->milliseconds, true, true, false};
        const auto conservative = [&](int measuredEdge, double milliseconds) {
            const double ratio = std::max(1.0, double(edge) / measuredEdge);
            return ViewportPreviewTimingEstimate{milliseconds * ratio * ratio, true, false, true};
        };
        // One small render does not prove that larger renders cost the same.
        // Keep fixed work when shrinking; reserve pixel growth when enlarging.
        if (count == 1) return conservative(sizes.front().first, sizes.front().second->milliseconds);

        auto upper = std::lower_bound(sizes.begin(), end, edge,
            [](const auto& point, int value) { return point.first < value; });
        if (upper == sizes.begin()) ++upper;
        else if (upper == end) upper = std::prev(end);
        const auto lower = std::prev(upper);
        const double lowPixels = static_cast<double>(lower->first) * lower->first;
        const double highPixels = static_cast<double>(upper->first) * upper->first;
        const double lowMs = lower->second->milliseconds;
        const double highMs = upper->second->milliseconds;
        const double slope = (highMs - lowMs) / (highPixels - lowPixels);
        if (slope <= 0.0) return conservative(upper->first, std::max(lowMs, highMs));
        const double fixed = std::max(0.0, lowMs - slope * lowPixels);
        const double predicted = fixed + slope * static_cast<double>(edge) * edge;
        if (!std::isfinite(predicted)) return {};
        return {predicted, true, false, true};
    }

    void Reset() { m_Contexts.clear(); m_Sequence = 0; }

private:
    struct Sample {
        double milliseconds = 0.0;
        unsigned count = 0, driftCount = 0;
        int driftDirection = 0;
        std::uint64_t lastUse = 0;
        Clock::time_point observedAt {};
    };
    struct Context {
        std::map<int, Sample> sizes;
        std::uint64_t lastUse = 0;
    };
    std::map<std::size_t, Context> m_Contexts;
    std::uint64_t m_Sequence = 0;
    static bool Fresh(const Sample& sample, Clock::time_point now) {
        return now >= sample.observedAt && now - sample.observedAt <= MaximumAge;
    }
};

} // namespace Stack::Renderer
