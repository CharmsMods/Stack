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
        preserve != model.preserveDetail ||
        (preserve && preferences.minimumDetailPercent != model.minimumDetailPercent) ||
        state.request.empty || model.visibleWidth != state.request.visible.width ||
        model.visibleHeight != state.request.visible.height ||
        model.physicalWidth != state.physicalWidth || model.physicalHeight != state.physicalHeight)
        return unknown;
    const int requested = GetRawViewportRequestedFps(), display = GetRawViewportMaximumFps();
    const double now = ImGui::GetTime();
    if (state.qualityBarRevision == model.revision && state.qualityBar.maximumFps == unknown.maximumFps &&
        state.qualityBarRequestedFps == requested && state.qualityBarDisplayFps == display &&
        state.qualityBarUpdatedAt >= 0 && now >= state.qualityBarUpdatedAt && now-state.qualityBarUpdatedAt < .25)
        return state.qualityBar;
    auto candidates = model.candidates;
    for (auto& candidate : candidates) {
        const auto estimate = state.timings.Estimate(candidate.executionIdentity,candidate.edge);
        candidate.predictedMs = estimate.predictedMs;
        candidate.costKnown = estimate.costKnown;
        candidate.timingCovered = estimate.timingCovered;
    }
    auto& bar = state.qualityBar;
    bar = unknown;
    const double ratio = double(bar.maximumFps)/bar.minimumFps;
    for (std::size_t i=0; i<bar.samples.size(); ++i) {
        const double position = (i+.5)/bar.samples.size();
        const int fps = int(std::lround(bar.minimumFps*std::pow(ratio,position)));
        bar.samples[i] = PredictViewportQuality(model.input,candidates,fps,display);
    }
    bar.current = PredictViewportQuality(model.input,candidates,requested,display);
    state.qualityBarRevision = model.revision;
    state.qualityBarRequestedFps = requested; state.qualityBarDisplayFps = display;
    state.qualityBarUpdatedAt = now;
    return bar;
}
