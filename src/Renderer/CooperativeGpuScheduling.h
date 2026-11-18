#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Stack::Renderer {
// Keep each fragment submission bounded independently of CPU enqueue speed.
// The scissor rectangle changes scheduling only, never the source raster grid.
inline int CooperativeRasterTileHeight(bool interacting) { return interacting ? 128 : 512; }
inline constexpr int kCooperativeRasterTileWidth = 1024;
inline std::int64_t CooperativeRasterPixels(bool interacting) {
    return std::int64_t(kCooperativeRasterTileWidth) * CooperativeRasterTileHeight(interacting);
}

// Called after actual GPU submissions, not CPU cancellation probes. A fast
// CPU must not accumulate an arbitrary number of expensive draws before a
// time-only checkpoint expires. These are command limits, not GPU deadlines.
class CooperativeGpuSubmission {
public:
    using Clock = std::chrono::steady_clock;
    explicit CooperativeGpuSubmission(int displayRefreshRate, Clock::time_point now = Clock::now())
        : m_LastBatch(now), m_IntervalMs(std::clamp(250.0 / std::max(1,
              displayRefreshRate > 0 ? displayRefreshRate : 60), .25, 2.0)) {}

    bool Checkpoint(bool interacting, Clock::time_point now = Clock::now()) {
        const unsigned maximum = interacting ? 2u : 8u;
        return ++m_Submissions >= maximum ||
            std::chrono::duration<double, std::milli>(now - m_LastBatch).count() >= m_IntervalMs;
    }
    void BatchSubmitted(Clock::time_point now = Clock::now()) { m_Submissions = 0; m_LastBatch = now; }

private:
    Clock::time_point m_LastBatch;
    double m_IntervalMs;
    unsigned m_Submissions = 0;
};
}
