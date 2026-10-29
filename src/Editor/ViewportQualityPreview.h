#pragma once
#include "Renderer/ViewportPreviewPolicy.h"
#include "Raw/RawViewportSettings.h"
#include <array>

namespace Stack::EditorRendering {
// A read-only forecast for one editing workload, not a benchmark or a promise
// of perceptual quality. Percent is linear sampling relative to display detail.
struct ViewportQualitySample {
    int requestedFps = 0, effectiveFps = 0;
    double displayDetailPercent = 0, predictedMs = 0;
    bool known = false, provisional = true, targetLimited = false;
};
struct ViewportQualityBar {
    static constexpr std::size_t SampleCount = 32;
    std::array<ViewportQualitySample, SampleCount> samples{};
    ViewportQualitySample current;
    int minimumFps = Raw::kMinimumViewportTargetFps, maximumFps = 240;
};
struct ViewportQualityModel {
    Renderer::ViewportPreviewPolicyInput input;
    std::vector<Renderer::ViewportPreviewCandidate> candidates;
    int visibleWidth = 0, visibleHeight = 0, physicalWidth = 0, physicalHeight = 0;
    int minimumDetailPercent = 100;
    bool preserveDetail = false, valid = false;
    std::uint64_t revision = 0, renderRevision = 0;
};
inline ViewportQualitySample PredictViewportQuality(
    Renderer::ViewportPreviewPolicyInput input,
    const std::vector<Renderer::ViewportPreviewCandidate>& candidates,
    int requestedFps, int displayFps) {
    ViewportQualitySample sample;
    sample.requestedFps = requestedFps;
    sample.effectiveFps = Raw::ResolveViewportTargetFps(requestedFps, displayFps);
    input.targetFps = sample.effectiveFps;
    const auto selected = Renderer::SelectViewportPreviewCandidate(input, candidates);
    sample.known = selected.measured && selected.edge > 0;
    sample.provisional = selected.provisional;
    if (sample.known) {
        sample.displayDetailPercent = std::clamp(100.0 * selected.edge / std::max(1,input.displayEdge),0.0,100.0);
        sample.predictedMs = selected.predictedMs;
        sample.targetLimited = selected.predictedMs > 1000.0 / sample.effectiveFps * 1.05;
    }
    return sample;
}
}
