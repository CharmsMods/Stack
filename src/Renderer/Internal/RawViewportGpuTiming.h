#pragma once
#include "Raw/RawViewportWorkload.h"
#include <chrono>
#include <functional>
#include <vector>

namespace Raw {
// Query objects stay on the render owner's GL context. Results contain only
// numbers, so presentation never waits on or owns timing resources.
class ViewportGpuTiming {
public:
    struct Span {
        ViewportStage stage;
        unsigned int start = 0, end = 0;
        std::chrono::steady_clock::time_point cpuStart;
        double cpuMs = 0;
    };
    struct Batch {
        std::vector<Span> spans;
        unsigned int serviceStart = 0, serviceEnd = 0;
    };
    struct Measurement {
        ViewportStageCosts cpu {}, gpu {};
        std::array<bool,kViewportStageCount> executed {};
        double gpuServiceMs = 0;
    };
    ~ViewportGpuTiming();
    void Reset(bool enabled);
    void Mark(ViewportStage stage);
    void Finish() { End(); }
    ViewportStageCosts Collect(const std::function<bool()>& canceled);
    Batch Capture();
    static bool Poll(Batch& batch, Measurement& measurement);
    static void Release(Batch& batch);
private:
    void End();
    bool m_Enabled = false;
    unsigned int m_ServiceStart = 0;
    std::vector<Span> m_Spans;
};
}
