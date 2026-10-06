#pragma once

#include "Raw/MultiFrameDenoise/LocalMotion.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Raw::Mfd {

struct GpuLocalMotionDiagnostics {
    std::string deviceIdentity;
    std::uint32_t dispatchCount = 0u;
    std::uint64_t scoredCandidateCount = 0u;
    std::uint64_t uploadedLayers = 0u;
    double evaluationSeconds = 0;
};

// Evaluate, BeginPair and destruction require the same current OpenGL context.
// Resources survive between bounded dispatches. Each call restores GL state,
// allowing foreground rendering to use the context between compute batches.
class GpuLocalMotionDiscreteEvaluator {
public:
    GpuLocalMotionDiscreteEvaluator();
    ~GpuLocalMotionDiscreteEvaluator();

    GpuLocalMotionDiscreteEvaluator(
        const GpuLocalMotionDiscreteEvaluator&) = delete;
    GpuLocalMotionDiscreteEvaluator& operator=(
        const GpuLocalMotionDiscreteEvaluator&) = delete;

    bool Evaluate(
        const LocalMotionDirectionRequest& request,
        const std::vector<LocalMotionDiscreteCandidate>& candidates,
        std::vector<LocalMotionDiscreteScore>& scores,
        std::string& error);

    const GpuLocalMotionDiagnostics& Diagnostics() const;
    void BeginPair(const CfaPlanePyramid& reference,const CfaPlanePyramid& alternate);
    // Only after the owner context has become unavailable. Its teardown owns
    // the remaining driver resources; never issue GL deletion on a CPU worker.
    void AbandonContextResources();

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

} // namespace Raw::Mfd
