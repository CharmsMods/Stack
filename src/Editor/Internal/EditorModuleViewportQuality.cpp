#include "Editor/EditorModule.h"
#include <imgui.h>
#include <cmath>

Stack::EditorRendering::ViewportQualityBar EditorModule::GetViewportQualityBar(int maximumFps) const {
    using namespace Stack::EditorRendering;
    ViewportQualityBar unknown;
    unknown.maximumFps = std::max(unknown.minimumFps,maximumFps);
    if (!UsesGraphViewportPreview() || GetViewportMode() == ViewportMode::CompositeCanvas) return unknown;
    const auto& state = ActiveViewportPreviewState();
    const auto& model = state.qualityModel;
    const auto preferences = GetRawViewportPreferences();
    const bool preserve = preferences.mode == Raw::ViewportInteractionMode::PreserveDetail;
    if (!model.valid || model.renderRevision != m_RenderRevision ||
        state.request.empty || model.visibleWidth != state.request.visible.width ||
        model.visibleHeight != state.request.visible.height ||
        model.physicalWidth != state.physicalWidth || model.physicalHeight != state.physicalHeight)
        return unknown;
    const int requested = GetRawViewportRequestedFps(), display = GetRawViewportMaximumFps();
    auto input = model.input;
    input.preserveDetail = preserve;
    input.minimumEdge = preserve ?
        std::max(1,int(std::ceil(input.displayEdge*preferences.minimumDetailPercent/100.0))) :
        std::min(input.displayEdge,Stack::Renderer::ViewportPreviewEdgeForVisiblePixels(
            state.request.visible,input.maximumEdge,256));
    const double now = ImGui::GetTime();
    if (state.qualityBarRevision == model.revision && state.qualityBar.maximumFps == unknown.maximumFps &&
        state.qualityBarRequestedFps == requested && state.qualityBarDisplayFps == display &&
        state.qualityBarMinimumEdge == input.minimumEdge && state.qualityBarPreserveDetail == preserve &&
        state.qualityBarUpdatedAt >= 0 && now >= state.qualityBarUpdatedAt && now-state.qualityBarUpdatedAt < .25)
        return state.qualityBar;
    auto candidates = model.candidates;
    for (auto& candidate : candidates) {
        const auto estimate = state.timings.Estimate(candidate.executionIdentity,candidate.regionalPixels,candidate.completePixels);
        candidate.predictedMs = estimate.predictedMs;
        candidate.costKnown = estimate.costKnown;
        candidate.timingCovered = estimate.timingCovered;
    }
    auto& bar = state.qualityBar;
    bar = unknown;
    double fastestMs = 0;
    bool allEligibleMeasured = true;
    for (const auto& candidate : candidates) {
        if (candidate.edge < input.minimumEdge ||
            candidate.edge > input.displayEdge ||
            (input.memoryBudgetBytes && candidate.workingBytes > input.memoryBudgetBytes))
            continue;
        allEligibleMeasured &= candidate.costKnown && candidate.timingCovered;
        if (candidate.costKnown && candidate.predictedMs > 0 &&
            (!fastestMs || candidate.predictedMs < fastestMs))
            fastestMs = candidate.predictedMs;
    }
    if (fastestMs > 0) bar.estimatedCapacityFps = std::min(double(display),1000.0 / fastestMs);
    bar.capacityProvisional = !allEligibleMeasured;
    const double ratio = double(bar.maximumFps)/bar.minimumFps;
    for (std::size_t i=0; i<bar.samples.size(); ++i) {
        const double position = (i+.5)/bar.samples.size();
        const int fps = int(std::lround(bar.minimumFps*std::pow(ratio,position)));
        bar.samples[i] = PredictViewportQuality(input,candidates,fps,display);
    }
    bar.current = PredictViewportQuality(input,candidates,
        Raw::UsesFullDisplayDetail(preferences) ? display : requested,display);
    state.qualityBarRevision = model.revision;
    state.qualityBarRequestedFps = requested; state.qualityBarDisplayFps = display;
    state.qualityBarMinimumEdge = input.minimumEdge; state.qualityBarPreserveDetail = preserve;
    state.qualityBarUpdatedAt = now;
    return bar;
}
