#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Stack::App {
// Background completions should leave most of a high-refresh frame for input,
// drawing and presentation. Image processing quality is unrelated to this limit.
inline double MainThreadCompletionBudgetMs(int displayRefreshRate) {
    const int rate = displayRefreshRate > 0 ? displayRefreshRate : 60;
    return std::min(1.0, 100.0 / static_cast<double>(rate));
}

struct UiFrameWorkLimits {
    double totalMs = 0.0;
    double completionMs = 0.0;
    std::size_t completionCount = 0;
    double projectApplyMs = 0.0;
    double libraryUploadMs = 0.0;
    std::size_t backgroundProjects = 0;
};

// Shell-owned admission policy for UI-thread housekeeping, never image quality.
// React to one hitch immediately and hold back optional work until a short run
// of healthy frames, rather than averaging a severe stall out of existence.
class UiFrameBudget {
public:
    UiFrameWorkLimits Limits(int displayRefreshRate, bool interacting) const {
        const double intervalMs = FrameIntervalMs(displayRefreshRate);
        const bool recovering = m_RecoveryMs > 0.0;
        const double scale = recovering ? 0.35 : interacting ? 0.5 : 1.0;
        UiFrameWorkLimits limits;
        limits.totalMs = std::min(4.0, intervalMs * 0.25) * scale;
        limits.completionMs = std::min(MainThreadCompletionBudgetMs(displayRefreshRate),
            limits.totalMs * 0.35);
        limits.completionCount = recovering || interacting ? 1 : 4;
        limits.projectApplyMs = std::min(2.5, limits.totalMs * 0.65);
        limits.libraryUploadMs = std::min(2.0, limits.totalMs * 0.5);
        limits.backgroundProjects = recovering || interacting ? 1 : 2;
        return limits;
    }

    void ObserveFrame(double frameMs, double swapMs, int displayRefreshRate) {
        if (!std::isfinite(frameMs) || !std::isfinite(swapMs) || frameMs <= 0.0)
            return;
        const double intervalMs = FrameIntervalMs(displayRefreshRate);
        // A normal refresh wait is not CPU pressure. Excess presentation wait
        // still counts: a congested GPU can stall swap as well as submission.
        const double workMs = std::max(0.0,
            frameMs - std::clamp(swapMs, 0.0, intervalMs));
        if (workMs >= intervalMs * 0.85 ||
            frameMs >= std::max(50.0, intervalMs * 3.0)) {
            m_RecoveryMs = 250.0;
        } else {
            m_RecoveryMs = std::max(0.0, m_RecoveryMs - std::min(frameMs, 50.0));
        }
    }

private:
    static double FrameIntervalMs(int displayRefreshRate) {
        return 1000.0 / static_cast<double>(displayRefreshRate > 0 ? displayRefreshRate : 60);
    }
    double m_RecoveryMs = 0.0;
};

// Charge admitted shell housekeeping against one allowance. Each mandatory
// progress lane may overrun it by one indivisible operation; follow-up work yields.
class UiFrameWorkBudget {
public:
    explicit UiFrameWorkBudget(UiFrameWorkLimits limits)
        : m_Limits(limits), m_RemainingMs(limits.totalMs) {}

    const UiFrameWorkLimits& Limits() const { return m_Limits; }
    double RemainingMs() const { return m_RemainingMs; }
    double ProgressSliceMs(double maximumMs) const {
        // TaskSystem uses zero to request unlimited draining, not no work.
        return std::max(0.05, std::min(maximumMs, m_RemainingMs));
    }
    void Charge(double elapsedMs) {
        if (std::isfinite(elapsedMs) && elapsedMs > 0.0)
            m_RemainingMs = std::max(0.0, m_RemainingMs - elapsedMs);
    }

private:
    UiFrameWorkLimits m_Limits;
    double m_RemainingMs = 0.0;
};
}
