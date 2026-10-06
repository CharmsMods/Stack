#include "Editor/EditorModule.h"
#include <sstream>
#include <iomanip>

Raw::ViewportDecisionInput EditorModule::BuildRawViewportDecisionInput() const {
    Raw::ViewportDecisionInput input;
    input.nativeWidth=m_RawRenderSessionFullFrameWidth;
    input.nativeHeight=m_RawRenderSessionFullFrameHeight;
    input.physicalWidth=m_RawViewportPhysicalWidth;
    input.physicalHeight=m_RawViewportPhysicalHeight;
    input.maximumEdge=m_RawWorkspaceInteractivePreviewMaximumEdge;
    input.fps=GetRawViewportTargetFps();
    input.preferences=GetRawViewportPreferences();
    input.visible=m_RawViewportRequest.visible;
    input.coldStart=!HasRawWorkspaceLivePreviewForSource(GetActiveRawWorkspacePreviewIdentity());
    // Unified graphs currently execute complete rasters. Their masks and
    // spatial operations cannot use the source-only RAW region contract.
    input.regionalAllowed=UsesRawWorkspaceStageRender() &&
        m_Project->rawMode!=Stack::RawWorkspace::RawProjectMode::UnifiedLayers && !IsMultiFrameRawProjectActive();
    const auto recipe=RawViewportRecipe();
    input.regionalFirst=Raw::ViewportFirstRegionalStage(recipe);
    input.changingStage=m_RawViewportEditStage;
    input.keys=Raw::ViewportWorkloadKeys(recipe);
    input.completeGraph=m_RawViewportGraphWorkloadKeys.back()!=0;
    if (input.completeGraph) input.keys=m_RawViewportGraphWorkloadKeys;
    input.cached=m_RawViewportCachedStages;
    input.nativeCached=m_RawViewportNativeCachedStages;
    input.cachedEdge=m_RawViewportCachedEdge;
    input.cachedRequestEdge=m_RawViewportCachedRequestEdge;
    return input;
}
int EditorModule::ResolveRawViewportInteractiveEdge() {
    const int selected=GetCalibratedRawViewportEdge();
    if (selected<=0) return m_RawWorkspaceInteractivePreviewMaxDimension;
    auto input=BuildRawViewportDecisionInput();
    const int maximum=std::max(1,std::min(input.maximumEdge,Raw::ViewportDisplayDetailEdge(input)));
    const int minimum=input.preferences.mode==Raw::ViewportInteractionMode::PreserveDetail
        ? std::min(maximum,Raw::ViewportDisplayDetailEdge(input,input.preferences.minimumDetailPercent/100.0))
        : std::min(maximum,Raw::kMinimumInteractiveViewportEdge);
    int edge=selected;
    if (IsRawWorkspaceUiInteractionActive() && m_RawWorkspaceAdaptiveGestureActive)
        edge=int(std::lround(m_RawWorkspaceAdaptivePreviewScale*m_RawWorkspacePhysicalViewportMaxDimension));
    m_RawWorkspaceInteractivePreviewMaxDimension=std::clamp(edge,std::max(1,minimum),maximum);
    input.evaluationEdge=m_RawWorkspaceInteractivePreviewMaxDimension;
    m_RawViewportDecision=m_RawViewportController.Choose(input,m_RawViewportTimingBank,RawViewportRecipe());
    return m_RawWorkspaceInteractivePreviewMaxDimension;
}
std::string EditorModule::GetRawViewportDecisionStatus() const {
    if (!IsRawWorkspaceProjectActive()) return "Applies to the RAW viewport. Graph rendering is unchanged.";
    auto input=BuildRawViewportDecisionInput();
    input.evaluationEdge=m_RawWorkspaceInteractivePreviewMaxDimension;
    m_RawViewportDecision=m_RawViewportController.Choose(input,m_RawViewportTimingBank,RawViewportRecipe());
    const auto& decision=m_RawViewportDecision;
    std::ostringstream text;
    text << "Interactive raster: " << m_RawWorkspaceInteractivePreviewMaxDimension << " px longest side";
    if (decision.predictedMs>0) text << std::fixed << std::setprecision(1) << "; predicted " << decision.predictedMs << " ms";
    const double measured=m_RawViewportController.LastMeasuredMs();
    if (measured>0) text << std::fixed << std::setprecision(1) << "; measured " << measured << " ms";
    text << ". " << Raw::ViewportLimitLabel(decision.limit);
    if (decision.provisional && decision.limit!=Raw::ViewportLimit::Learning) text << ". Estimate outside measured coverage";
    if (measured>0 && decision.predictedMs>0) text << ". Recent prediction error " <<
        std::setprecision(0) << m_RawViewportController.PredictionError()*100 << "%";
    if (m_RawWorkspaceFullResolutionPreviewDeferredByBudget) text << ". Native refinement is limited by the memory or texture budget";
    const auto error=m_RawViewportPreferences ? m_RawViewportPreferences->SaveError() : std::string{};
    if (!error.empty()) text << ". " << error;
    return text.str();
}
void EditorModule::ResetRawViewportLearnedTimings() {
    CancelRawViewportCalibration();
    m_RawViewportTimingHistory.ResetHardware();
    m_RawViewportTimingHistory.MergeLoaded(m_RawViewportTimingBank);
    m_RawViewportController.Reset();
    m_RawViewportController.SetContext(m_RawViewportAdaptiveWorkload);
    m_RawViewportCalibration.measuredWorkloads.clear();
    m_RawViewportCalibration.samples.clear();
    m_RawViewportCalibration.failed=false;
    m_RawWorkspaceAdaptiveFrameTimeMs=0;
    m_RawWorkspacePreviewHealthyStreak=m_RawWorkspacePreviewSlowSamples=m_RawWorkspacePreviewScaleCooldown=0;
}
