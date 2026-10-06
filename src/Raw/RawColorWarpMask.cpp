#include "Raw/RawColorWarpMask.h"
#include "Raw/ImageGuidance.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace Stack::RawRecipe {
namespace {

constexpr float kInfiniteDistance = 1.0e20f;

bool Cancelled(const std::function<bool()>& callback, std::size_t index) {
    return callback && (index & 1023u) == 0u && callback();
}

bool InCircle(const RawColorWarpSampleCircle& circle, float u, float v) {
    const float du = (u - circle.centerU) / std::max(0.000001f, circle.radiusU);
    const float dv = (v - circle.centerV) / std::max(0.000001f, circle.radiusV);
    return du * du + dv * dv <= 1.0f;
}

std::vector<float> DistanceToState(
    int width,
    int height,
    const std::vector<std::uint8_t>& state,
    bool distanceToSet) {
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    std::vector<float> distance(pixelCount, kInfiniteDistance);
    for (std::size_t index = 0; index < pixelCount; ++index) {
        const bool set = state[index] != 0u;
        if (set == distanceToSet) distance[index] = 0.0f;
    }
    constexpr float diagonal = 1.41421356237f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            float value = distance[index];
            if (x > 0) value = std::min(value, distance[index - 1u] + 1.0f);
            if (y > 0) value = std::min(value, distance[index - width] + 1.0f);
            if (x > 0 && y > 0) value = std::min(value, distance[index - width - 1u] + diagonal);
            if (x + 1 < width && y > 0) value = std::min(value, distance[index - width + 1u] + diagonal);
            distance[index] = value;
        }
    }
    for (int y = height - 1; y >= 0; --y) {
        for (int x = width - 1; x >= 0; --x) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            float value = distance[index];
            if (x + 1 < width) value = std::min(value, distance[index + 1u] + 1.0f);
            if (y + 1 < height) value = std::min(value, distance[index + width] + 1.0f);
            if (x + 1 < width && y + 1 < height) value = std::min(value, distance[index + width + 1u] + diagonal);
            if (x > 0 && y + 1 < height) value = std::min(value, distance[index + width - 1u] + diagonal);
            distance[index] = value;
        }
    }
    return distance;
}

float SmoothUnit(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return value * value * (3.0f - 2.0f * value);
}

} // namespace

int RawColorWarpMaskLongEdge(RawColorWarpMaskQualityTier tier) {
    switch (tier) {
        case RawColorWarpMaskQualityTier::Interactive: return 384;
        case RawColorWarpMaskQualityTier::Settled: return 1024;
        case RawColorWarpMaskQualityTier::Native: return 2048;
    }
    return 384;
}

RawColorWarpMaskFields BuildRawColorWarpMaskFields(
    const RawColorWarpMaskGuide& guide,
    const RawColorWarpPin& pin,
    const RawColorWarpRegion* region,
    const std::function<bool()>& isCancelled) {
    RawColorWarpMaskFields result;
    result.width = guide.width;
    result.height = guide.height;
    if (guide.width <= 0 || guide.height <= 0 ||
        guide.pixels.size() != static_cast<std::size_t>(guide.width) * guide.height) {
        return result;
    }
    const std::size_t pixelCount = guide.pixels.size();
    result.gate.assign(pixelCount, 255u);
    result.support.assign(pixelCount, 0u);
    result.boundary.assign(pixelCount, 255u);
    result.edge.assign(pixelCount, 0u);
    if (region == nullptr || region->spatialMode == RawColorWarpSpatialMode::AllMatches) {
        return result;
    }
    result.spatialAvailable = true;
    result.gate.assign(pixelCount, 0u);

    std::vector<float> direct(pixelCount, 0.0f);
    std::vector<std::uint8_t> blocked(pixelCount, 0u);
    std::vector<std::uint8_t> seeds(pixelCount, 0u);
    for (int y = 0; y < guide.height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / guide.height;
        for (int x = 0; x < guide.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * guide.width + x;
            if (Cancelled(isCancelled, index)) {
                result.cancelled = true;
                return result;
            }
            const float u = (static_cast<float>(x) + 0.5f) / guide.width;
            const RawColorWarpCoordinate& color = guide.pixels[index];
            direct[index] = EvaluateColorWarpPinShapeWeight(pin, color.a, color.b) *
                EvaluateColorWarpPinLightnessWeight(pin, color.sceneEv);
            for (const RawColorWarpSampleCircle& circle : region->circles) {
                if (!InCircle(circle, u, v)) continue;
                if (circle.polarity == RawColorWarpSamplePolarity::Exclude) {
                    blocked[index] = 1u;
                } else {
                    seeds[index] = 1u;
                }
            }
        }
    }

    std::queue<std::size_t> frontier;
    for (std::size_t index = 0; index < pixelCount; ++index) {
        if (seeds[index] != 0u && blocked[index] == 0u && direct[index] > 0.01f) {
            result.gate[index] = 255u;
            frontier.push(index);
        }
    }
    const int neighborX[8] = { -1, 1, 0, 0, -1, 1, -1, 1 };
    const int neighborY[8] = { 0, 0, -1, 1, -1, -1, 1, 1 };
    while (!frontier.empty()) {
        const std::size_t index = frontier.front();
        frontier.pop();
        const int x = static_cast<int>(index % guide.width);
        const int y = static_cast<int>(index / guide.width);
        for (int direction = 0; direction < 8; ++direction) {
            const int nx = x + neighborX[direction];
            const int ny = y + neighborY[direction];
            if (nx < 0 || ny < 0 || nx >= guide.width || ny >= guide.height) continue;
            const std::size_t next = static_cast<std::size_t>(ny) * guide.width + nx;
            if (result.gate[next] != 0u || blocked[next] != 0u || direct[next] <= 0.01f) continue;
            result.gate[next] = 255u;
            frontier.push(next);
        }
    }

    if (region->spatialMode == RawColorWarpSpatialMode::Cohesive ||
        region->spatialMode == RawColorWarpSpatialMode::EdgeAwareReach ||
        region->spatialMode == RawColorWarpSpatialMode::AssistedRegion) {
        for (int pass = 0; pass < 2; ++pass) {
            std::vector<std::uint8_t> nextGate = result.gate;
            for (int y = 1; y + 1 < guide.height; ++y) {
                for (int x = 1; x + 1 < guide.width; ++x) {
                    const std::size_t index = static_cast<std::size_t>(y) * guide.width + x;
                    if (blocked[index] != 0u) continue;
                    int neighbors = 0;
                    for (int direction = 0; direction < 8; ++direction) {
                        const std::size_t neighbor = static_cast<std::size_t>(
                            y + neighborY[direction]) * guide.width + x + neighborX[direction];
                        neighbors += result.gate[neighbor] != 0u ? 1 : 0;
                    }
                    if (result.gate[index] == 0u && neighbors >= 5 && direct[index] > 0.001f) {
                        nextGate[index] = 255u;
                    } else if (result.gate[index] != 0u && neighbors <= 1 && seeds[index] == 0u) {
                        nextGate[index] = 0u;
                    }
                }
            }
            result.gate.swap(nextGate);
        }
    }

    if (region->spatialMode == RawColorWarpSpatialMode::EdgeAwareReach ||
        region->spatialMode == RawColorWarpSpatialMode::AssistedRegion) {
        struct QueueEntry {
            float cost;
            std::size_t index;
            bool operator<(const QueueEntry& rhs) const { return cost > rhs.cost; }
        };
        std::priority_queue<QueueEntry> queue;
        std::vector<float> cost(pixelCount, kInfiniteDistance);
        for (std::size_t index = 0; index < pixelCount; ++index) {
            if (result.gate[index] != 0u) {
                cost[index] = 0.0f;
                queue.push({ 0.0f, index });
            }
        }
        const float sourceScale = std::max(
            static_cast<float>(guide.sourceWidth) / std::max(1, guide.width),
            static_cast<float>(guide.sourceHeight) / std::max(1, guide.height));
        float reach = region->reachPixels / std::max(1.0f, sourceScale);
        if (region->spatialMode == RawColorWarpSpatialMode::AssistedRegion) reach *= 1.25f;
        reach = std::max(0.0f, reach);
        const float colorScale = 0.012f + (1.0f - region->edgeStop) * 0.10f;
        while (!queue.empty()) {
            const QueueEntry current = queue.top();
            queue.pop();
            if (current.cost != cost[current.index] || current.cost > reach) continue;
            const int x = static_cast<int>(current.index % guide.width);
            const int y = static_cast<int>(current.index / guide.width);
            const auto& source = guide.pixels[current.index];
            for (int direction = 0; direction < 8; ++direction) {
                const int nx = x + neighborX[direction];
                const int ny = y + neighborY[direction];
                if (nx < 0 || ny < 0 || nx >= guide.width || ny >= guide.height) continue;
                const std::size_t next = static_cast<std::size_t>(ny) * guide.width + nx;
                if (blocked[next] != 0u) continue;
                const auto& target = guide.pixels[next];
                const float colorEdge = ImageGuideColorDistance(source, target, colorScale);
                const float evEdge = ImageGuideBrightnessDistance(source, target,
                    0.35f + (1.0f - region->edgeStop) * 2.0f);
                const float step = direction < 4 ? 1.0f : 1.41421356237f;
                const float nextCost = current.cost + step * (1.0f + colorEdge + evEdge);
                if (nextCost < cost[next] && nextCost <= reach) {
                    cost[next] = nextCost;
                    queue.push({ nextCost, next });
                }
            }
        }
        for (std::size_t index = 0; index < pixelCount; ++index) {
            if (result.gate[index] != 0u || cost[index] >= kInfiniteDistance) continue;
            const float decay = reach <= 0.0001f ? 0.0f : 1.0f - cost[index] / reach;
            result.support[index] = static_cast<std::uint8_t>(
                std::lround(255.0f * SmoothUnit(decay)));
        }
    }

    const float sourceScale = std::max(
        static_cast<float>(guide.sourceWidth) / std::max(1, guide.width),
        static_cast<float>(guide.sourceHeight) / std::max(1, guide.height));
    const float feather = region->featherPixels / std::max(1.0f, sourceScale);
    if (feather > 0.01f) {
        std::vector<std::uint8_t> membership(pixelCount, 0u);
        for (std::size_t index = 0; index < pixelCount; ++index) {
            membership[index] = (result.gate[index] != 0u || result.support[index] != 0u)
                ? 255u
                : 0u;
        }
        const std::vector<float> distanceInside =
            DistanceToState(guide.width, guide.height, membership, false);
        const std::vector<float> distanceOutside =
            DistanceToState(guide.width, guide.height, membership, true);
        for (std::size_t index = 0; index < pixelCount; ++index) {
            const bool inside = membership[index] != 0u;
            float factor = 0.0f;
            if (region->featherDirection == RawColorWarpFeatherDirection::Inward) {
                factor = inside ? SmoothUnit(distanceInside[index] / feather) : 0.0f;
            } else if (region->featherDirection == RawColorWarpFeatherDirection::Outward) {
                factor = inside ? 1.0f : SmoothUnit(1.0f - distanceOutside[index] / feather);
            } else {
                factor = inside
                    ? SmoothUnit(0.5f + distanceInside[index] / (2.0f * feather))
                    : SmoothUnit(0.5f - distanceOutside[index] / (2.0f * feather));
            }
            result.boundary[index] = static_cast<std::uint8_t>(
                std::lround(255.0f * std::clamp(factor, 0.0f, 1.0f)));
            if (!inside && factor > 0.0f) {
                result.support[index] = std::max(
                    result.support[index],
                    static_cast<std::uint8_t>(std::lround(255.0f * factor)));
            }
        }
    }
    if (feather > 0.01f && region->edgeStop > 0.001f) {
        std::vector<std::uint8_t> visibleEdges(pixelCount, 0u);
        const float threshold = 0.75f +
            (1.0f - region->edgeStop) * 2.25f;
        for (int y = 0; y < guide.height; ++y) {
            for (int x = 0; x < guide.width; ++x) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * guide.width + x;
                float strongest = 0.0f;
                for (const auto [dx, dy] : {
                         std::pair<int, int>{ 1, 0 },
                         { 0, 1 }, { 1, 1 }, { -1, 1 } }) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 ||
                        nx >= guide.width || ny >= guide.height) {
                        continue;
                    }
                    const auto& lhs = guide.pixels[index];
                    const auto& rhs = guide.pixels[
                        static_cast<std::size_t>(ny) * guide.width + nx];
                    const float colorEdge =
                        std::hypot(rhs.a - lhs.a, rhs.b - lhs.b) / 0.025f;
                    const float evEdge =
                        std::abs(rhs.sceneEv - lhs.sceneEv) / 0.60f;
                    strongest = std::max(strongest, colorEdge + evEdge);
                }
                if (strongest >= threshold) visibleEdges[index] = 255u;
            }
        }
        if (std::any_of(
                visibleEdges.begin(),
                visibleEdges.end(),
                [](std::uint8_t value) { return value != 0u; })) {
            std::vector<std::uint8_t> membership(pixelCount, 0u);
            for (std::size_t index = 0; index < pixelCount; ++index) {
                membership[index] =
                    result.gate[index] != 0u || result.support[index] != 0u
                    ? 255u
                    : 0u;
            }
            const std::vector<float> distanceToEdge = DistanceToState(
                guide.width, guide.height, visibleEdges, true);
            const std::vector<float> distanceToMembership = DistanceToState(
                guide.width, guide.height, membership, true);
            for (std::size_t index = 0; index < pixelCount; ++index) {
                const float distance = distanceToEdge[index];
                if (distance > feather) continue;
                const bool inside = membership[index] != 0u;
                if (!inside && distanceToMembership[index] > feather) continue;
                const float normalized = std::clamp(
                    distance / feather, 0.0f, 1.0f);
                float factor = inside ? 1.0f : 0.0f;
                if (region->featherDirection ==
                    RawColorWarpFeatherDirection::Inward) {
                    factor = inside ? SmoothUnit(normalized) : 0.0f;
                } else if (region->featherDirection ==
                           RawColorWarpFeatherDirection::Outward) {
                    factor = inside ? 1.0f : SmoothUnit(1.0f - normalized);
                } else {
                    factor = inside
                        ? SmoothUnit(0.5f + normalized * 0.5f)
                        : SmoothUnit(0.5f - normalized * 0.5f);
                }
                const float influence = inside
                    ? 1.0f - factor
                    : factor;
                if (influence <= 0.001f) continue;
                result.edge[index] = static_cast<std::uint8_t>(
                    std::lround(255.0f * influence));
                if (inside) {
                    result.boundary[index] = std::min(
                        result.boundary[index],
                        static_cast<std::uint8_t>(
                            std::lround(255.0f * factor)));
                } else if (factor > 0.0f) {
                    const std::uint8_t admitted = static_cast<std::uint8_t>(
                        std::lround(255.0f * factor));
                    result.support[index] = std::max(
                        result.support[index], admitted);
                    result.boundary[index] = std::min(
                        result.boundary[index], admitted);
                }
            }
        }
    }
    return result;
}

} // namespace Stack::RawRecipe
