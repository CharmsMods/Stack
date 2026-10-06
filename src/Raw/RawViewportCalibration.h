#pragma once
#include "Raw/RawViewportWorkload.h"
#include "Raw/RawViewportTimingPolicy.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>

namespace Raw {
struct ViewportCalibrationSample {
    int edge = 0;
    double startupMs = 0.0;
    double renderMs = 0.0;
    bool native = false;
    bool coldOnly = false;
    ViewportStageCosts stages {};
    ViewportStageCosts startupStages {};
    std::size_t firstMeasuredStage = 0;
    std::size_t changingStage = kViewportStageCount;
    // Equivalent linear extent for the pixels actually processed by each
    // stage. Zero retains the complete-image edge used by calibration.
    std::array<int, kViewportStageCount> stageEdges {};
    std::array<bool, kViewportStageCount> measuredStages {true,true,true,true,true,true,true,true};
};
struct ViewportCalibration {
    std::string source;
    std::uint64_t sourceHash = 0;
    std::uint64_t revision = 0;
    std::uint64_t historyEpoch = 0;
    std::string representation;
    std::uint64_t generation = 0;
    std::vector<int> edges;
    std::vector<ViewportCalibrationSample> samples;
    std::size_t next = 0;
    bool failed = false;
    double verifyAfter = -1.0;
    bool verification = false;
    std::size_t activeWorkload = 0;
    std::array<std::size_t, kViewportStageCount> activeKeys {};
    std::set<std::size_t> measuredWorkloads;
    bool Busy() const { return generation != 0; }
    bool Ready() const { return !samples.empty(); }
    int EdgeForFps(int fps) const {
        if (samples.empty()) return 0;
        const double budget = 1000.0 / std::max(1, fps);
        int chosen = samples.front().edge;
        double previousCost = 0.0;
        int previousEdge = 0;
        for (const auto& sample : samples) {
            // Noise must never imply that a larger raster is cheaper.
            const double cost = std::max(previousCost, sample.renderMs);
            if (cost <= budget) chosen = sample.edge;
            else {
                if (previousEdge > 0 && cost > previousCost && budget > previousCost) {
                    const double fraction = (budget - previousCost) / (cost - previousCost);
                    chosen = static_cast<int>(std::sqrt(previousEdge * double(previousEdge) +
                        fraction * (sample.edge * double(sample.edge) - previousEdge * double(previousEdge))));
                }
                break;
            }
            previousCost = cost;
            previousEdge = sample.edge;
        }
        return chosen;
    }
    double NativeFps() const {
        for (const auto& sample : samples)
            if (sample.native && sample.renderMs > 0.0) return 1000.0 / sample.renderMs;
        return 0.0;
    }
};

struct ViewportStageSample {
    int edge = 0;
    double coldMs = 0, warmMs = 0;
    double editMs = -1;
    unsigned warmSamples = 0, editSamples = 0, coldSamples = 0;
};
class ViewportTimingBank {
public:
    using Keys = std::array<std::size_t, kViewportStageCount>;
    void Clear() { m_Profiles.clear(); }
    void Record(const Keys& keys, const ViewportCalibrationSample& sample, bool onlyMissing = false) {
        if (sample.edge <= 0 || sample.firstMeasuredStage >= kViewportStageCount ||
            !std::isfinite(sample.renderMs) || sample.renderMs <= 0 ||
            !std::isfinite(sample.startupMs) || sample.startupMs < 0) return;
        for (std::size_t i = 0; i < kViewportStageCount; ++i)
            if (!std::isfinite(sample.stages[i]) || sample.stages[i] < 0 ||
                !std::isfinite(sample.startupStages[i]) || sample.startupStages[i] < 0) return;
        double sum = 0, startupSum = 0;
        for (double cost : sample.stages) sum += cost;
        for (double cost : sample.startupStages) startupSum += cost;
        for (std::size_t i = sample.firstMeasuredStage; i < keys.size(); ++i) {
            if (!sample.measuredStages[i]) continue;
            const int edge = sample.stageEdges[i] > 0 ? sample.stageEdges[i] : sample.edge;
            if (m_Profiles.size() >= 1024 && !m_Profiles.count({i, keys[i]})) continue;
            auto& profile = m_Profiles[{i, keys[i]}];
            auto at = std::lower_bound(profile.begin(), profile.end(), edge,
                [](const auto& entry, int edge) { return entry.edge < edge; });
            if (onlyMissing && at != profile.end() && at->edge == edge) continue;
            const double cost = sample.stages[i] + (i == keys.size() - 1
                ? std::max(0.0, sample.renderMs - sum) : 0.0);
            const double cold = startupSum > 0.0 ? std::max(cost, sample.startupStages[i] +
                (i == keys.size()-1 ? std::max(0.0,sample.startupMs-startupSum) : 0.0)) :
                cost + (i == 0 ? std::max(0.0,sample.startupMs-sample.renderMs) : 0.0);
            const bool changed = i == sample.changingStage;
            if (at != profile.end() && at->edge == edge) {
                at->coldMs = AverageViewportCost(at->coldMs,cold,at->coldSamples);
                if (sample.coldOnly) continue;
                if (changed) at->editMs = AverageViewportCost(at->editMs, cost, at->editSamples);
                else at->warmMs = AverageViewportCost(at->warmMs, cost, at->warmSamples);
            } else {
                if (profile.size() >= 48) {
                    // Replace a nearby interior point, retaining both ends.
                    const auto nearest = std::min_element(profile.begin()+1,profile.end()-1,
                        [edge](const auto& a,const auto& b) { return std::abs(a.edge-edge)<std::abs(b.edge-edge); });
                    profile.erase(nearest);
                    at = std::lower_bound(profile.begin(),profile.end(),edge,
                        [](const auto& entry,int value) { return entry.edge<value; });
                }
                profile.insert(at, {edge, cold, sample.coldOnly ? 0.0 : cost, changed && !sample.coldOnly ? cost : -1.0,
                    !changed && !sample.coldOnly ? 1u : 0u, changed && !sample.coldOnly ? 1u : 0u, 1u});
            }
        }
    }
    double Cost(const Keys& keys, int edge, ViewportStage first,
        bool cold, bool* measured = nullptr, double areaFraction = 1.0, std::size_t regionalFirst = 8,
        std::size_t changingStage = kViewportStageCount, bool estimateSingleSizePixelWork = false) const {
        if (changingStage == kViewportStageCount) changingStage = static_cast<std::size_t>(first);
        double total = 0;
        bool known = true;
        for (std::size_t i = cold ? 0 : static_cast<std::size_t>(first); i < keys.size(); ++i) {
            const int stageEdge = i >= regionalFirst
                ? std::max(1,int(std::lround(edge*std::sqrt(std::clamp(areaFraction,0.0,1.0))))) : edge;
            const auto found = m_Profiles.find({i, keys[i]});
            if (found == m_Profiles.end() || found->second.empty()) { known = false; continue; }
            std::vector<ViewportStageSample> curve;
            for (const auto& point : found->second)
                if (cold ? point.coldSamples>0 : point.warmSamples+point.editSamples>0) curve.push_back(point);
            if (curve.empty()) { known=false; continue; }
            const auto pointCost = [&](const ViewportStageSample& point) {
                if (cold) return point.coldMs;
                return i == changingStage && point.editMs >= 0 ? std::max(point.warmMs, point.editMs) : point.warmMs;
            };
            double previousMs = 0;
            int previousEdge = 0;
            bool bracketed = false;
            double stageCost = 0;
            for (const auto& point : curve) {
                const double cost = std::max(previousMs, pointCost(point));
                if (stageEdge <= point.edge) {
                    // Interpolate area while retaining measured fixed work.
                    // One sample cannot separate fixed and per-pixel costs.
                    const double fraction = previousEdge == 0 ? 1.0 : std::clamp(
                        (double(stageEdge) * stageEdge - double(previousEdge) * previousEdge) /
                        (double(point.edge) * point.edge - double(previousEdge) * previousEdge), 0.0, 1.0);
                    stageCost = previousMs + (cost - previousMs) * fraction;
                    if (previousEdge == 0 && curve.size() == 1 && estimateSingleSizePixelWork && !cold) {
                        // Bootstrap complete-graph editing at a smaller size.
                        // This provisional pixel estimate is replaced by the
                        // fixed-plus-pixel fit once a second size completes.
                        stageCost *= double(stageEdge)*stageEdge/(double(point.edge)*point.edge);
                    }
                    if (previousEdge == 0 && curve.size() >= 2) {
                        const auto& a = curve[0]; const auto& b = curve[1];
                        const double ta = pointCost(a);
                        const double tb = std::max(ta,pointCost(b));
                        const double fixed = std::clamp((ta*double(b.edge)*b.edge-tb*double(a.edge)*a.edge) /
                            (double(b.edge)*b.edge-double(a.edge)*a.edge),0.0,stageCost);
                        stageCost = fixed + (stageCost-fixed)*double(stageEdge)*stageEdge/(double(a.edge)*a.edge);
                    }
                    bracketed = true;
                    break;
                }
                previousEdge = point.edge;
                previousMs = cost;
            }
            if (!bracketed) {
                stageCost = previousMs;
                if (curve.size() >= 2 && !cold && stageEdge <= double(curve.back().edge) * 2.0) {
                    const auto& a = curve[curve.size()-2]; const auto& b = curve.back();
                    const double slope = std::max(0.0, pointCost(b)-pointCost(a)) /
                        (double(b.edge)*b.edge-double(a.edge)*a.edge);
                    stageCost += slope * (double(stageEdge)*stageEdge-double(b.edge)*b.edge);
                } else known = false;
            }
            total += stageCost;
        }
        if (measured) *measured = known;
        return total;
    }
    bool Covers(const Keys& keys, int edge, ViewportStage first, double area = 1.0,
        std::size_t regionalFirst = kViewportStageCount, bool exact = false) const {
        for (std::size_t i=std::size_t(first);i<keys.size();++i) {
            const int extent=i>=regionalFirst ? std::max(1,int(std::lround(edge*std::sqrt(area)))) : edge;
            const auto found=m_Profiles.find({i,keys[i]});
            if (found==m_Profiles.end() || found->second.empty()) return false;
            const auto& curve=found->second;
            const auto usable=[](const auto& point){return point.warmSamples+point.editSamples>0;};
            const auto begin=std::find_if(curve.begin(),curve.end(),usable);
            const auto end=std::find_if(curve.rbegin(),curve.rend(),usable);
            if (begin==curve.end() || extent<begin->edge || extent>end->edge) return false;
            if (exact && std::none_of(curve.begin(),curve.end(),[extent,&usable](const auto& point){return point.edge==extent && usable(point);})) return false;
        }
        return true;
    }
    double FixedCost(const Keys& keys, ViewportStage first, std::size_t changingStage = kViewportStageCount) const {
        // A single size cannot distinguish setup work from pixel work.
        for (std::size_t i = static_cast<std::size_t>(first); i < keys.size(); ++i) {
            const auto found = m_Profiles.find({i,keys[i]});
            if (found == m_Profiles.end() || found->second.size() < 2) return 0.0;
        }
        return Cost(keys, 1, first, false, nullptr, 1.0, kViewportStageCount, changingStage);
    }
    int EdgeForFps(const Keys& keys, int fps, int maximum,
        ViewportStage first, bool cold, double areaFraction = 1.0, std::size_t regionalFirst = 8, std::size_t changingStage = kViewportStageCount) const {
        // FPS is a target. If fixed work alone exceeds it, shrinking more
        // cannot reach it. Retain detail within 5% of the measured floor.
        const double budget = std::max(1000.0 / std::max(1, fps),
            cold ? 0.0 : FixedCost(keys, first, changingStage) * 1.05);
        int chosen = 0;
        int lo = 1, hi = maximum;
        while (lo <= hi) {
            int edge = lo + (hi - lo) / 2;
            bool known = false;
            const double cost = Cost(keys, edge, first, cold, &known, areaFraction, regionalFirst, changingStage);
            if (known && cost <= budget) { chosen = edge; lo = edge + 1; }
            else hi = edge - 1;
        }
        return chosen;
    }
private:
    friend class ViewportTimingHistory;
    std::map<std::pair<std::size_t, std::size_t>, std::vector<ViewportStageSample>> m_Profiles;
};

inline std::size_t ViewportMeasurementKey(const ViewportTimingBank::Keys& keys, int edge,
    std::size_t changingStage = kViewportStageCount) {
    std::size_t key = static_cast<std::size_t>(edge);
    for (auto part : keys) Stack::Renderer::RawDevelopmentCache::MixJsonHash(key, part);
    Stack::Renderer::RawDevelopmentCache::MixJsonHash(key, changingStage);
    return key;
}
}
