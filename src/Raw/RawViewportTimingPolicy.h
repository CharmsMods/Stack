#pragma once
#include "Raw/RawViewportWorkload.h"
#include <cmath>

namespace Raw {
class ViewportEditTimingWindow {
public:
    void Reset() { m_Count = m_Next = 0; }
    double Observe(double milliseconds) {
        if (!std::isfinite(milliseconds) || milliseconds <= 0) return 0;
        m_Values[m_Next] = milliseconds;
        m_Next = (m_Next + 1) % m_Values.size();
        m_Count = std::min(m_Count + 1, m_Values.size());
        if (m_Count < m_Values.size()) return 0;
        auto sorted = m_Values;
        std::sort(sorted.begin(), sorted.end());
        return sorted[1];
    }
private:
    std::array<double,3> m_Values {};
    std::size_t m_Count = 0, m_Next = 0;
};
inline double ViewportMeasuredWorkMs(const ViewportStageCosts& costs, std::size_t first = 0) {
    double total = 0;
    for (std::size_t i = first; i < costs.size(); ++i) {
        if (!std::isfinite(costs[i]) || costs[i] < 0) return 0;
        total += costs[i];
    }
    return total;
}
inline double AverageViewportCost(double previous, double sample, unsigned& samples) {
    if (!samples) { samples = 1; return sample; }
    // One scheduling/driver stall cannot rewrite a learned resolution curve.
    // Sustained slower work still moves the average over later observations.
    const double bounded = std::min(sample, previous + std::max(2.0, previous));
    samples = std::min(32u, samples + 1);
    return previous + (bounded - previous) / samples;
}
inline bool CanLearnViewportEdit(bool success, bool superseded, bool firstPresentation,
    bool evaluated, bool diagnostic, bool analysis, int denoisePasses,
    std::size_t preparedThrough, std::size_t changingStage) {
    return success && !superseded && !firstPresentation && evaluated && !diagnostic && !analysis &&
        preparedThrough >= changingStage && (changingStage <= 1 || denoisePasses == 0);
}
}
