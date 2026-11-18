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

// Submission-thread policy. A fast CPU must not accumulate an arbitrary number
// of expensive draws before a time-only checkpoint expires. These are command
// limits, not a guarantee about driver execution time or hardware preemption.
class CooperativeGpuSubmission {
public:
    using Clock = std::chrono::steady_clock;
    explicit CooperativeGpuSubmission(int displayRefreshRate, Clock::time_point now = Clock::now())
        : m_LastDrain(now), m_IntervalMs(std::clamp(250.0 / std::max(1,
              displayRefreshRate > 0 ? displayRefreshRate : 60), .25, 2.0)) {}

    bool Checkpoint(bool interacting, bool nativeRefinement, Clock::time_point now = Clock::now()) {
        const unsigned maximum = interacting || nativeRefinement ? 2u : 8u;
        return ++m_Checkpoints >= maximum ||
            std::chrono::duration<double, std::milli>(now - m_LastDrain).count() >= m_IntervalMs;
    }
    void Drained(Clock::time_point now = Clock::now()) { m_Checkpoints = 0; m_LastDrain = now; }

private:
    Clock::time_point m_LastDrain;
    double m_IntervalMs;
    unsigned m_Checkpoints = 0;
};
}
