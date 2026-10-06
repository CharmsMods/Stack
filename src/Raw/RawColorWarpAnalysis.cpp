#include "Raw/RawColorWarpAnalysis.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <unordered_map>

namespace Stack::RawRecipe {
namespace {

float Square(float value) { return value * value; }

float ColorDistance(const RawColorWarpAnalysisSample& lhs,
                    const RawColorWarpAnalysisSample& rhs) {
    return std::sqrt(Square(lhs.color.a - rhs.color.a) +
                     Square(lhs.color.b - rhs.color.b));
}

float Median(std::vector<float> values) {
    if (values.empty()) return 0.0f;
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    float result = values[middle];
    if ((values.size() & 1u) == 0u) {
        const auto lower = std::max_element(values.begin(), values.begin() + middle);
        result = (*lower + result) * 0.5f;
    }
    return result;
}

std::size_t RobustMedoid(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const std::vector<std::size_t>& candidates) {
    if (candidates.empty()) return 0u;
    const std::size_t stride = std::max<std::size_t>(1u, candidates.size() / 192u);
    float bestCost = std::numeric_limits<float>::max();
    std::size_t best = candidates.front();
    for (std::size_t outer = 0; outer < candidates.size(); outer += stride) {
        const std::size_t candidate = candidates[outer];
        float cost = 0.0f;
        for (std::size_t inner = 0; inner < candidates.size(); inner += stride) {
            cost += ColorDistance(samples[candidate], samples[candidates[inner]]);
        }
        if (cost < bestCost) {
            bestCost = cost;
            best = candidate;
        }
    }
    return best;
}

RawColorWarpEvCurve BuildEvCurve(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const std::vector<std::size_t>& members,
    bool constrainBrightness) {
    if (!constrainBrightness) {
        return MakeUniformColorWarpEvCurve();
    }
    std::vector<float> evs;
    evs.reserve(members.size());
    for (const std::size_t index : members) {
        if (std::isfinite(samples[index].color.sceneEv)) {
            evs.push_back(std::clamp(
                samples[index].color.sceneEv,
                kRawColorWarpEvMinimum,
                kRawColorWarpEvMaximum));
        }
    }
    if (evs.empty()) return MakeColorWarpEvCurveHump(0.0f);
    std::sort(evs.begin(), evs.end());
    if (evs.size() < 4u) {
        return MakeColorWarpEvCurveHump(evs[evs.size() / 2u]);
    }

    RawColorWarpEvCurve curve = MakeUniformColorWarpEvCurve(0.0f);
    std::vector<float> histogram(curve.samples.size(), 0.0f);
    for (const float ev : evs) {
        const float normalized = (ev - kRawColorWarpEvMinimum) /
            (kRawColorWarpEvMaximum - kRawColorWarpEvMinimum);
        const std::size_t bin = std::min(
            histogram.size() - 1u,
            static_cast<std::size_t>(std::round(
                normalized * static_cast<float>(histogram.size() - 1u))));
        histogram[bin] += 1.0f;
    }

    constexpr float sigmaEv = 0.50f;
    const float samplesPerEv = static_cast<float>(histogram.size() - 1u) /
        (kRawColorWarpEvMaximum - kRawColorWarpEvMinimum);
    const float sigmaSamples = sigmaEv * samplesPerEv;
    const int radius = static_cast<int>(std::ceil(sigmaSamples * 3.0f));
    float maximum = 0.0f;
    for (std::size_t destination = 0; destination < curve.samples.size(); ++destination) {
        float weighted = 0.0f;
        float kernelWeight = 0.0f;
        for (int offset = -radius; offset <= radius; ++offset) {
            const int source = static_cast<int>(destination) + offset;
            if (source < 0 || source >= static_cast<int>(histogram.size())) continue;
            const float distance = static_cast<float>(offset) / sigmaSamples;
            const float weight = std::exp(-0.5f * distance * distance);
            weighted += histogram[static_cast<std::size_t>(source)] * weight;
            kernelWeight += weight;
        }
        curve.samples[destination] = kernelWeight > 0.0f
            ? weighted / kernelWeight
            : 0.0f;
        maximum = std::max(maximum, curve.samples[destination]);
    }
    if (maximum <= 0.000001f) {
        return MakeColorWarpEvCurveHump(evs[evs.size() / 2u]);
    }
    const float noiseFloor = maximum * 0.025f;
    const float normalization = std::max(0.000001f, maximum - noiseFloor);
    for (float& value : curve.samples) {
        value = std::pow(
            std::clamp((value - noiseFloor) / normalization, 0.0f, 1.0f),
            0.72f);
    }
    return curve;
}

RawColorWarpAreaProposal BuildProposal(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const std::vector<std::size_t>& members,
    bool constrainBrightness) {
    RawColorWarpAreaProposal proposal;
    if (members.empty()) return proposal;
    std::vector<std::size_t> memberCopy = members;
    const std::size_t medoid = RobustMedoid(samples, memberCopy);
    proposal.sourceA = samples[medoid].color.a;
    proposal.sourceB = samples[medoid].color.b;
    proposal.evCurve = BuildEvCurve(samples, members, constrainBrightness);
    proposal.supportingPixelCount = members.size();
    return proposal;
}

std::vector<std::vector<std::size_t>> ClusterColors(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const std::vector<std::size_t>& members,
    std::size_t requestedClusters) {
    const std::size_t clusterCount = std::clamp<std::size_t>(
        requestedClusters, 1u, std::min<std::size_t>(4u, members.size()));
    std::vector<std::array<float, 2>> centers;
    centers.reserve(clusterCount);
    centers.push_back({ samples[RobustMedoid(samples, members)].color.a,
                        samples[RobustMedoid(samples, members)].color.b });
    while (centers.size() < clusterCount) {
        float farthestDistance = -1.0f;
        std::size_t farthest = members.front();
        for (const std::size_t index : members) {
            float nearest = std::numeric_limits<float>::max();
            for (const auto& center : centers) {
                nearest = std::min(
                    nearest,
                    Square(samples[index].color.a - center[0]) +
                        Square(samples[index].color.b - center[1]));
            }
            if (nearest > farthestDistance) {
                farthestDistance = nearest;
                farthest = index;
            }
        }
        if (farthestDistance < Square(0.018f)) break;
        centers.push_back({ samples[farthest].color.a, samples[farthest].color.b });
    }
    std::vector<std::size_t> assignment(members.size(), 0u);
    for (int iteration = 0; iteration < 8; ++iteration) {
        for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
            const auto& sample = samples[members[memberIndex]];
            float nearest = std::numeric_limits<float>::max();
            for (std::size_t centerIndex = 0; centerIndex < centers.size(); ++centerIndex) {
                const float distance = Square(sample.color.a - centers[centerIndex][0]) +
                    Square(sample.color.b - centers[centerIndex][1]);
                if (distance < nearest) {
                    nearest = distance;
                    assignment[memberIndex] = centerIndex;
                }
            }
        }
        std::vector<std::array<float, 3>> sums(centers.size(), { 0.0f, 0.0f, 0.0f });
        for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
            auto& sum = sums[assignment[memberIndex]];
            sum[0] += samples[members[memberIndex]].color.a;
            sum[1] += samples[members[memberIndex]].color.b;
            sum[2] += 1.0f;
        }
        for (std::size_t centerIndex = 0; centerIndex < centers.size(); ++centerIndex) {
            if (sums[centerIndex][2] > 0.0f) {
                centers[centerIndex][0] = sums[centerIndex][0] / sums[centerIndex][2];
                centers[centerIndex][1] = sums[centerIndex][1] / sums[centerIndex][2];
            }
        }
    }
    std::vector<std::vector<std::size_t>> clusters(centers.size());
    for (std::size_t memberIndex = 0; memberIndex < members.size(); ++memberIndex) {
        clusters[assignment[memberIndex]].push_back(members[memberIndex]);
    }
    clusters.erase(
        std::remove_if(
            clusters.begin(),
            clusters.end(),
            [&](const auto& cluster) {
                return cluster.size() < std::max<std::size_t>(2u, members.size() / 50u);
            }),
        clusters.end());
    std::sort(clusters.begin(), clusters.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.size() > rhs.size();
    });
    return clusters;
}

std::vector<std::size_t> ConnectedPopulation(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const std::vector<std::size_t>& members,
    std::size_t seed) {
    if (members.empty()) return {};
    std::vector<float> uniqueU;
    std::vector<float> uniqueV;
    uniqueU.reserve(members.size());
    uniqueV.reserve(members.size());
    for (const std::size_t index : members) {
        uniqueU.push_back(samples[index].sourceU);
        uniqueV.push_back(samples[index].sourceV);
    }
    const auto uniqueSorted = [](std::vector<float>& values) {
        std::sort(values.begin(), values.end());
        values.erase(
            std::unique(
                values.begin(), values.end(),
                [](float lhs, float rhs) { return std::abs(lhs - rhs) < 0.000001f; }),
            values.end());
    };
    uniqueSorted(uniqueU);
    uniqueSorted(uniqueV);
    if (uniqueU.empty() || uniqueV.empty()) return members;
    const auto coordinateIndex = [](const std::vector<float>& values, float value) {
        return static_cast<int>(std::lower_bound(values.begin(), values.end(), value - 0.000001f) -
            values.begin());
    };
    const auto keyFor = [](int x, int y) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32u) |
            static_cast<std::uint32_t>(x);
    };
    std::unordered_map<std::uint64_t, std::size_t> memberAt;
    memberAt.reserve(members.size());
    for (const std::size_t index : members) {
        memberAt[keyFor(
            coordinateIndex(uniqueU, samples[index].sourceU),
            coordinateIndex(uniqueV, samples[index].sourceV))] = index;
    }
    const int seedX = coordinateIndex(uniqueU, samples[seed].sourceU);
    const int seedY = coordinateIndex(uniqueV, samples[seed].sourceV);
    if (memberAt.find(keyFor(seedX, seedY)) == memberAt.end()) return members;
    std::queue<std::pair<int, int>> frontier;
    std::unordered_map<std::uint64_t, bool> visited;
    frontier.push({ seedX, seedY });
    visited[keyFor(seedX, seedY)] = true;
    std::vector<std::size_t> result;
    while (!frontier.empty()) {
        const auto [x, y] = frontier.front();
        frontier.pop();
        const auto found = memberAt.find(keyFor(x, y));
        if (found == memberAt.end()) continue;
        result.push_back(found->second);
        for (const auto [dx, dy] : {
                 std::pair<int, int>{ -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
                 { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 } }) {
            const auto key = keyFor(x + dx, y + dy);
            if (memberAt.find(key) != memberAt.end() && !visited[key]) {
                visited[key] = true;
                frontier.push({ x + dx, y + dy });
            }
        }
    }
    return result;
}

} // namespace

RawColorWarpAreaAnalysisResult AnalyzeRawColorWarpArea(
    const std::vector<RawColorWarpAnalysisSample>& samples,
    const RawColorWarpSampleCircle& circle,
    std::size_t availablePinSlots,
    const std::function<bool()>& isCancelled) {
    RawColorWarpAreaAnalysisResult result;
    std::vector<std::size_t> inside;
    std::vector<std::size_t> inner;
    inside.reserve(samples.size());
    inner.reserve(samples.size());
    const float radiusU = std::max(0.000001f, circle.radiusU);
    const float radiusV = std::max(0.000001f, circle.radiusV);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (isCancelled && (index & 255u) == 0u && isCancelled()) {
            result.cancelled = true;
            return result;
        }
        const auto& sample = samples[index];
        const float du = (sample.sourceU - circle.centerU) / radiusU;
        const float dv = (sample.sourceV - circle.centerV) / radiusV;
        const float distanceSquared = du * du + dv * dv;
        if (distanceSquared > 1.0f) continue;
        if (!sample.valid || !std::isfinite(sample.color.a) ||
            !std::isfinite(sample.color.b) || !std::isfinite(sample.color.sceneEv)) {
            ++result.rejectedPixelCount;
            continue;
        }
        inside.push_back(index);
        if (distanceSquared <= 0.25f) inner.push_back(index);
    }
    result.sampledPixelCount = inside.size();
    if (inside.empty() || availablePinSlots == 0u) {
        result.capacityLimited = !inside.empty() && availablePinSlots == 0u;
        return result;
    }
    if (inner.empty()) inner = inside;

    std::vector<std::vector<std::size_t>> populations;
    const bool multiple = circle.interpretation ==
        RawColorWarpInterpretationMode::MultipleColors;
    if (multiple || circle.interpretation == RawColorWarpInterpretationMode::FullContents) {
        populations = ClusterColors(samples, inside, multiple ? 4u : 1u);
    } else if (circle.interpretation == RawColorWarpInterpretationMode::DominantFamily) {
        populations = ClusterColors(samples, inside, 3u);
        if (populations.size() > 1u) populations.resize(1u);
    } else {
        const std::size_t medoid = RobustMedoid(samples, inner);
        std::vector<float> distances;
        distances.reserve(inside.size());
        for (const std::size_t index : inside) {
            distances.push_back(ColorDistance(samples[index], samples[medoid]));
        }
        const float medianDistance = Median(distances);
        std::vector<float> deviations;
        deviations.reserve(distances.size());
        for (const float distance : distances) {
            deviations.push_back(std::abs(distance - medianDistance));
        }
        const float threshold = std::clamp(
            medianDistance + std::max(0.012f, 3.0f * Median(deviations)),
            0.018f,
            0.16f);
        std::vector<std::size_t> family;
        for (const std::size_t index : inside) {
            if (ColorDistance(samples[index], samples[medoid]) <= threshold) {
                family.push_back(index);
            } else {
                ++result.rejectedPixelCount;
            }
        }
        if (circle.interpretation ==
                RawColorWarpInterpretationMode::ConnectedFamily &&
            !family.empty()) {
            family = ConnectedPopulation(samples, family, medoid);
        }
        if (!family.empty()) populations.push_back(std::move(family));
    }

    const bool constrainBrightness =
        circle.interpretation != RawColorWarpInterpretationMode::ColorOnly;
    const std::size_t proposalLimit = std::min<std::size_t>(
        4u,
        availablePinSlots);
    result.capacityLimited = populations.size() > proposalLimit;
    for (std::size_t index = 0;
         index < populations.size() && result.proposals.size() < proposalLimit;
         ++index) {
        result.proposals.push_back(
            BuildProposal(samples, populations[index], constrainBrightness));
    }
    return result;
}

} // namespace Stack::RawRecipe
