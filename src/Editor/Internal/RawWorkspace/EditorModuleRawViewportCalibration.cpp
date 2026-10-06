#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/RawRenderPlanning.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Editor/RawRenderSourceRecipe.h"
#include "Raw/RawViewportResolutionPolicy.h"
#include <imgui.h>

Stack::RawRecipe::RawDevelopmentRecipe EditorModule::RawViewportRecipe() const {
    if (IsMultiFrameRawProjectActive()) {
        Stack::RawRecipe::RawDevelopmentRecipe recipe;
        if (m_Project->snapshot && Stack::EditorRendering::ReadMergedRenderingRecipe(*m_Project->snapshot,
                GetActiveRawWorkspacePreviewIdentity(),GetActiveRawWorkspacePreviewSourceHash(),recipe)) return recipe;
    }
    return m_Project->rawInteractionDraft.active ? m_Project->rawInteractionDraft.recipe : m_Project->rawRecipe;
}
std::uint64_t EditorModule::RawViewportSourceHash() const {
    if (IsMultiFrameRawProjectActive()) return GetActiveRawWorkspacePreviewSourceHash();
    const auto* source = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    return source ? BuildRawWorkspaceAutoBaseSourceHash(*source) : 0;
}
void EditorModule::UpdateRawViewportEditTiming(const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    const bool viewChanged = m_RawViewportTimingViewGeneration != m_RawViewportRequest.generation;
    if (m_RawViewportHasPreviousRecipe) {
        const auto a = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(m_RawViewportPreviousRecipe, 0);
        const auto b = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, 0);
        if (a.postOutputCrop != b.postOutputCrop)
            m_RawViewportEditStage = Raw::ChangedViewportStage(m_RawViewportPreviousRecipe, recipe);
        else if (viewChanged)
            m_RawViewportEditStage = Raw::ViewportFirstRegionalStage(recipe);
    }
    m_RawViewportTimingViewGeneration = m_RawViewportRequest.generation;
    auto keys = Raw::ViewportWorkloadKeys(recipe);
    if (m_RawViewportGraphWorkloadKeys.back()) keys=m_RawViewportGraphWorkloadKeys;
    std::size_t workload = Raw::ViewportMeasurementKey(keys, static_cast<int>(m_RawViewportEditStage));
    Stack::Renderer::RawDevelopmentCache::HashTypedValue(workload,m_RawViewportRequest.visible.width);
    Stack::Renderer::RawDevelopmentCache::HashTypedValue(workload,m_RawViewportRequest.visible.height);
    Stack::Renderer::RawDevelopmentCache::HashTypedValue(workload,UsesRawWorkspaceStageRender());
    if (workload != m_RawViewportAdaptiveWorkload) {
        m_RawViewportEditTimingWindow.Reset();
        m_RawViewportAdaptiveWorkload = workload;
        m_RawWorkspaceAdaptiveFrameTimeMs = 0;
    }
    m_RawViewportController.SetContext(workload);
    m_RawViewportPreviousRecipe = recipe;
    m_RawViewportHasPreviousRecipe = true;
    RefreshRawViewportTimingHistory();
    if (viewChanged) {
        const int edge = GetCalibratedRawViewportEdge();
        if (edge > 0) {
            m_RawWorkspaceAdaptivePreviewScale = static_cast<float>(edge) /
                std::max(1, m_RawWorkspacePhysicalViewportMaxDimension);
            m_RawWorkspaceInteractivePreviewMaxDimension = std::clamp(
                ((edge + 63) / 64) * 64, Raw::kMinimumInteractiveViewportEdge, m_RawWorkspaceInteractivePreviewMaximumEdge);
        }
    }
}
bool EditorModule::IsRawViewportCalibrationBusy() const { return m_RawViewportCalibration.Busy(); }
std::string EditorModule::GetRawViewportCalibrationStatus() const {
    const auto& state = m_RawViewportCalibration;
    if (!IsRawWorkspaceProjectActive()) return "Open a RAW project to measure viewport timing";
    if (state.Busy()) return std::string(state.verification ? "Verifying viewport timing " : "Preparing RAW editing ") +
        std::to_string(state.next + 1) + "/" + std::to_string(state.edges.size());
    if (state.verifyAfter >= 0.0) return "Viewport timing verification pending";
    if (state.failed) return "Some viewport timings are unavailable; editing measurements remain active";
    const auto saved = m_RawViewportTimingHistory.Status();
    return GetRawViewportPreferences().backgroundLearning ? saved : "Background probes paused. " + saved;
}
double EditorModule::GetRawViewportNativeFps() const {
    if ((m_RawViewportCalibration.source != GetActiveRawWorkspacePreviewIdentity() || m_RawViewportCalibration.sourceHash != RawViewportSourceHash())) return 0;
    bool known = false;
    const double ms = m_RawViewportTimingBank.Cost(BuildRawViewportDecisionInput().keys,
        std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight),
        Raw::ViewportStage::RawBase, false, &known);
    return known && ms > 0 ? 1000.0 / ms : 0.0;
}
int EditorModule::GetCalibratedRawViewportEdge() const {
    if ((m_RawViewportCalibration.source != GetActiveRawWorkspacePreviewIdentity() || m_RawViewportCalibration.sourceHash != RawViewportSourceHash())) return 0;
    m_RawViewportDecision = m_RawViewportController.Choose(BuildRawViewportDecisionInput(),m_RawViewportTimingBank,RawViewportRecipe());
    return m_RawViewportDecision.edge;
}

double EditorModule::GetRawViewportVisibleNativeFps() const {
    if ((m_RawViewportCalibration.source != GetActiveRawWorkspacePreviewIdentity() || m_RawViewportCalibration.sourceHash != RawViewportSourceHash())) return 0;
    const auto& region = m_RawViewportRequest.visible;
    const double area = BuildRawViewportDecisionInput().regionalAllowed && region.Partial() ? double(region.width) * region.height /
        std::max(1.0, double(m_RawRenderSessionFullFrameWidth) * m_RawRenderSessionFullFrameHeight) : 1.0;
    bool known = false;
    const auto& recipe = RawViewportRecipe();
    const double cost = m_RawViewportTimingBank.Cost(BuildRawViewportDecisionInput().keys,
        std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight), m_RawViewportEditStage,
        false, &known, area, static_cast<std::size_t>(Raw::ViewportFirstRegionalStage(recipe)));
    return known && cost > 0.0 ? 1000.0 / cost : 0.0;
}
void EditorModule::ObserveRawViewportTiming(const EditorRenderWorker::Result& result) {
    if (result.rawWorkspace.sourceKey.empty() || result.rawWorkspace.sourceKey != GetActiveRawWorkspacePreviewIdentity() ||
        result.rawWorkspace.sourceHash != RawViewportSourceHash() || result.graphRequest.enabled ||
        !result.success || result.telemetry.superseded) return;
    // Delayed numeric evidence must never replace current cache residency.
    if (result.cacheStateMeasured && !result.timingOnly) {
        m_RawViewportCachedStages = result.cachedStages;
        m_RawViewportNativeCachedStages = result.nativeCachedStages;
        m_RawViewportCachedRequestEdge = result.cacheEdge;
        m_RawViewportCachedEdge = result.cacheEdge > 0 ? result.cacheEdge :
            std::max(m_RawRenderSessionFullFrameWidth,m_RawRenderSessionFullFrameHeight);
    }
    auto& state = m_RawViewportCalibration;
    if (state.source != result.rawWorkspace.sourceKey || state.sourceHash != result.rawWorkspace.sourceHash) {
        state = {};
        state.source = result.rawWorkspace.sourceKey;
        state.sourceHash = result.rawWorkspace.sourceHash;
        state.revision = Raw::kViewportTimingVersion;
        m_RawViewportTimingBank.Clear();
        m_RawViewportTimingHistory.Reset();
        m_RawViewportController.Reset();
    }
    if (!result.timingRepresentation.empty()) m_RawViewportTimingRepresentation = result.timingRepresentation;
    RefreshRawViewportTimingHistory();
    if (result.telemetry.timingHistoryEpoch!=m_RawViewportTimingHistory.Epoch()) return;
    if (!result.timingOnly || result.rawWorkspace.viewportGeneration != m_RawViewportRequest.generation ||
        result.rawWorkspace.viewportDiagnostic || result.rawWorkspace.analysisCaptured ||
        (result.mainGraphStats.rawStageCacheMisses == 0 && result.mainGraphStats.imageCacheMisses == 0)) return;
    // Cold/reconstruction work is useful for initial preparation, but cannot
    // teach the steady edit controller that a slider repeats that work.
    const bool generic=std::none_of(result.measuredStages.begin(),result.measuredStages.end(),[](bool value){return value;});
    const bool cold = (!generic && result.firstMeasuredStage == 0) || result.mainGraphStats.rawRgbDenoisePasses > 0;
    Raw::ViewportCalibrationSample sample;
    sample.coldOnly=cold;
    sample.edge = result.cacheEdge > 0 ? result.cacheEdge : std::max(result.timingNativeWidth,result.timingNativeHeight);
    sample.firstMeasuredStage = std::min(result.firstMeasuredStage,Raw::kViewportStageCount-1);
    sample.changingStage = static_cast<std::size_t>(result.editStage);
    sample.stages = result.stageCosts;
    sample.measuredStages = result.measuredStages;
    const auto& region=result.rawWorkspace.viewportRegion;
    const auto regionalFirst=std::size_t(Raw::ViewportFirstRegionalStage(RawViewportRecipe()));
    for (std::size_t i=0;i<sample.stages.size();++i) {
        if (i<sample.firstMeasuredStage) { sample.stages[i]=0; sample.measuredStages[i]=false; }
        if (region.Valid() && i>=regionalFirst) sample.stageEdges[i]=std::max(1,int(std::lround(sample.edge*
            std::sqrt(double(region.width)*region.height/(double(region.fullWidth)*region.fullHeight)))));
    }
    if (generic || m_RawViewportGraphWorkloadKeys.back()) {
        sample.stages.fill(0);
        sample.measuredStages.fill(true);
        sample.firstMeasuredStage=0;
        sample.changingStage=Raw::kViewportStageCount;
        sample.stages.back()=result.telemetry.completedServiceMs;
        sample.coldOnly=result.mainGraphStats.rawGpuPreprocessDispatches>0 || result.mainGraphStats.rawRgbDenoisePasses>0;
    }
    sample.renderMs=std::max(Raw::ViewportMeasuredWorkMs(sample.stages),result.telemetry.completedServiceMs);
    sample.startupMs=sample.renderMs;
    if (sample.renderMs<=0) return;
    m_RawViewportTimingHistory.Record(result.workloadKeys,sample,result.telemetry.timingHistoryEpoch);
    m_RawViewportTimingHistory.MergeLoaded(m_RawViewportTimingBank);
    if (!sample.coldOnly) ObserveRawViewportFeedback(result);
}
void EditorModule::CancelRawViewportCalibration() {
    if (!m_RawViewportCalibration.Busy()) return;
    Stack::EditorRendering::RawRenderService::Get().CancelPurpose(
        m_RawRenderClientId, RawRenderPurpose::ViewportCalibration);
    m_RawViewportCalibration.generation = 0;
}
bool EditorModule::TrySubmitRawViewportCalibration(double now) {
    RefreshRawViewportTimingHistory();
    auto& state = m_RawViewportCalibration;
    if (!GetRawViewportPreferences().backgroundLearning) return false;
    // Recipe probes do not invalidate creative graph operation instances.
    // Foreground completions supply the actual graph workload measurements.
    if (m_RawViewportGraphWorkloadKeys.back()) return false;
    if (!m_RawWorkspaceRootTabActive || !IsRawWorkspaceProjectActive() || m_RawRenderClientId == 0 ||
        m_RenderDirty || m_RenderPending || IsAnyRenderBackendBusy() || IsRawWorkspaceUiInteractionActive() ||
        ImGui::IsAnyItemActive() || m_RawWorkspaceFullResolutionPreviewRequested ||
        m_RawWorkspaceAnalysisRequested || m_RawWorkspaceExportRenderRequested ||
        m_RawWorkspaceExplicitFullQualityRenderRequested || IsRawWorkspaceProjectLoadBusy() || IsExportBusy()) return false;
    const bool verificationDue = state.verifyAfter >= 0.0 && now >= state.verifyAfter;
    if (!verificationDue && now - m_LastRenderDirtyTime < 1.0) return false;
    const std::string source = GetActiveRawWorkspacePreviewIdentity();
    if (source.empty() || !HasRawWorkspaceLivePreviewForSource(source) || state.Busy()) return false;
    const auto context = Raw::kViewportTimingVersion;
    if (state.source != source || state.sourceHash != RawViewportSourceHash() || state.revision != context) {
        const double verifyAfter = state.verifyAfter;
        state = {};
        state.source = source;
        state.sourceHash = RawViewportSourceHash();
        state.revision = context;
        state.verifyAfter = verifyAfter;
        m_RawViewportTimingBank.Clear();
    }
    RefreshRawWorkspaceGpuMemoryBudget();
    const int native = std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight);
    const int fit = std::min(native, m_RawWorkspacePhysicalViewportMaxDimension);
    if (native <= 0 || fit <= 0) return false;
    std::vector<int> edges {std::min(native, Raw::kMinimumInteractiveViewportEdge),
        std::min(native, std::max(Raw::kMinimumInteractiveViewportEdge, fit / 4)),
        std::min(native, std::max(Raw::kMinimumInteractiveViewportEdge, fit / 2)), fit};
    // The private benchmark pipeline coexists with foreground resources.
    const auto decision = Raw::ResolveRawFullFramePreviewDecision(m_RawRenderSessionFullFrameWidth,
        m_RawRenderSessionFullFrameHeight, m_RawWorkspaceGlMaxTextureSize,
        m_RawWorkspaceVramWorkingBudgetBytes / 2, m_RawWorkspaceMinimumMemoryTiling,16);
    if (decision.allowed) edges.push_back(native);
    std::sort(edges.begin(), edges.end());
    edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
    Raw::ViewportStage selectedStage = m_RawViewportEditStage;
    switch (m_RawWorkspaceLabUi.activeTool) {
    case RawLabTool::Transform: case RawLabTool::Denoise: selectedStage = Raw::ViewportStage::RawBase; break;
    case RawLabTool::RgbDenoise: selectedStage = Raw::ViewportStage::NeutralPlacement; break;
    case RawLabTool::Light: case RawLabTool::Calibration: selectedStage = Raw::ViewportStage::RawPlacement; break;
    case RawLabTool::Zones: selectedStage = Raw::ViewportStage::PostLocalRange; break;
    case RawLabTool::Detail: selectedStage = Raw::ViewportStage::PostColorWarp; break;
    case RawLabTool::Tone: selectedStage = Raw::ViewportStage::PostFinishTone; break;
    case RawLabTool::Color: selectedStage = Raw::ViewportStage::PostColorWarp; break;
    case RawLabTool::View: selectedStage = Raw::ViewportStage::PostViewTransform; break;
    default: break;
    }
    std::vector<Raw::ViewportProbe> probes {{RawViewportRecipe(), selectedStage, false}};
    const bool reuseReconstruction = Stack::RawRecipe::IsRgbDenoiseActive(probes.front().recipe.rgbDenoise);
    int dependencyEdge = -1;
    if (reuseReconstruction) {
        const auto& recipe = probes.front().recipe;
        const auto nativeFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, 0).neutralPlacement;
        const auto cachedFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, m_RawViewportCachedRequestEdge).neutralPlacement;
        if (m_RawViewportNativeCachedStages[1] == nativeFingerprint) dependencyEdge = 0;
        else if (m_RawViewportCachedStages[1] == cachedFingerprint && m_RawViewportCachedEdge > 0) {
            dependencyEdge = m_RawViewportCachedRequestEdge;
            for (int& candidate : edges) candidate = std::min(candidate, m_RawViewportCachedEdge);
        } else edges.clear();
        std::sort(edges.begin(),edges.end());
        edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
        if (edges.empty()) {
            state.verifyAfter = -1.0;
            state.failed = true;
            return false;
        }
        probes.erase(std::remove_if(probes.begin(),probes.end(),[](const auto& probe) {
            return probe.hypothetical && static_cast<int>(probe.stage) < 2;
        }),probes.end());
    }
    Raw::ViewportProbe chosen;
    int edge = 0;
    std::size_t key = 0;
    state.edges.clear();
    state.next = 0;
    for (const auto& probe : probes) for (int candidate : edges) {
        const auto candidateKey = Raw::ViewportMeasurementKey(Raw::ViewportWorkloadKeys(probe.recipe), candidate, static_cast<std::size_t>(probe.stage));
        state.edges.push_back(candidate);
        const auto keys=Raw::ViewportWorkloadKeys(probe.recipe);
        const auto first=reuseReconstruction ? Raw::ViewportStage::RawPlacement : Raw::ViewportStage::RawBase;
        const bool measured=m_RawViewportTimingBank.Covers(keys,candidate,first,1.0,Raw::kViewportStageCount,true);
        if (!edge && !measured && !state.measuredWorkloads.count(candidateKey)) {
            chosen = probe;
            edge = candidate;
            key = candidateKey;
            state.next = state.edges.size() - 1;
        }
    }
    state.verification = verificationDue;
    if (verificationDue) {
        chosen = {RawViewportRecipe(), m_RawViewportEditStage, false};
        edge = GetCalibratedRawViewportEdge();
        if (reuseReconstruction && std::find(edges.begin(),edges.end(),edge) == edges.end()) edge = edges.back();
        if (!edge) edge = std::clamp(m_RawWorkspaceInteractivePreviewMaxDimension, edges.front(), edges.back());
        key = Raw::ViewportMeasurementKey(Raw::ViewportWorkloadKeys(chosen.recipe), edge, static_cast<std::size_t>(chosen.stage));
        state.next = 0;
        state.edges = {edge};
    }
    if (edge <= 0) return false;
    const auto generation = Stack::EditorRenderScheduling::NextGlobalGeneration();
    EditorRenderWorker::Snapshot snapshot;
    if (!TryBuildRenderSnapshot(generation, snapshot)) { state.failed = true; state.measuredWorkloads.insert(key); return false; }
    snapshot.rawRenderPurpose = RawRenderPurpose::ViewportCalibration;
    snapshot.schedulingSerial = generation;
    snapshot.previewMaxDimension = edge;
    snapshot.width = snapshot.rawWorkspace.fullFrameWidth;
    snapshot.height = snapshot.rawWorkspace.fullFrameHeight;
    snapshot.telemetry.interactionActive = false;
    snapshot.telemetry.snapshotReadyAt = std::chrono::steady_clock::now();
    snapshot.rawWorkspace.recipe = chosen.recipe;
    snapshot.rawWorkspace.viewportDependencyEdge = dependencyEdge;
    snapshot.rawWorkspace.editStage = chosen.stage;
    snapshot.rawWorkspace.calibrationFirstUse = !state.measuredWorkloads.count(key);
    Stack::EditorRendering::ApplyRawRecipeOverlayToRenderGraph(snapshot.graph, chosen.recipe,
        snapshot.rawWorkspace.managedRawDecodeNodeId, snapshot.rawWorkspace.managedToneCurveNodeId,
        snapshot.rawWorkspace.managedViewTransformNodeId);
    snapshot.rawWorkspace.analysisRequested = false;
    snapshot.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::None;
    snapshot.rawWorkspace.gradingScopeSource = RawDevelopmentGradingScopeSource::None;
    snapshot.rawWorkspace.preciseSolveRequest.reset();
    snapshot.rawWorkspace.cachePrewarmStage.reset();
    snapshot.rawWorkspace.localRangeTargetSampleRequested = false;
    snapshot.rawWorkspace.localRangeOverlayMode = "none";
    snapshot.graph.rawWorkspaceLocalRangeOverlayMode = "none";
    snapshot.graph.rawWorkspaceLocalRangeTargetSampleRequested = false;
    snapshot.rawWorkspace.startPointCandidateRenderRequests.clear();
    snapshot.rawWorkspace.minimumRawStageCacheBytes = 0;
    snapshot.developCandidateRenders.clear();
    snapshot.compositeOutputs.clear();
    snapshot.previews.clear();
    snapshot.rawWorkspace.gpuWorkingBudgetBytes = m_RawWorkspaceVramWorkingBudgetBytes / 2;
    snapshot.rawWorkspace.gpuCacheBudgetBytes = Stack::EditorRendering::ResolveRawRenderCacheBudgetBytes(
        snapshot.rawWorkspace.gpuWorkingBudgetBytes, Stack::EditorRendering::EstimateRawRenderWorkingSetBytes(
            snapshot.width, snapshot.height, edge));
    state.activeKeys = Raw::ViewportWorkloadKeys(chosen.recipe);
    if (!Stack::EditorRendering::RawRenderService::Get().Submit(m_RawRenderClientId, std::move(snapshot))) return false;
    state.activeWorkload = key;
    state.generation = generation;
    m_RenderPending = true;
    return true;
}
void EditorModule::AdoptRawViewportCalibration(const EditorRenderWorker::Result& result) {
    auto& state = m_RawViewportCalibration;
    // A canceled probe may have completed a pass before cancellation. Keep
    // that completed measurement without treating verification as finished.
    if (result.calibration.renderMs > 0.0 && result.rawWorkspace.sourceKey == GetActiveRawWorkspacePreviewIdentity() &&
        result.rawWorkspace.sourceHash == RawViewportSourceHash()) {
        if (!result.timingRepresentation.empty()) m_RawViewportTimingRepresentation = result.timingRepresentation;
        RefreshRawViewportTimingHistory();
        m_RawViewportTimingHistory.Record(result.workloadKeys,result.calibration,result.telemetry.timingHistoryEpoch);
        m_RawViewportTimingHistory.MergeLoaded(m_RawViewportTimingBank);
    }
    if (result.generation != state.generation) return;
    state.generation = 0;
    if (state.source != GetActiveRawWorkspacePreviewIdentity() || result.telemetry.superseded) return;
    if (!result.success || result.calibration.renderMs <= 0.0) {
        state.failed = true;
        // Failed configurations are not retried in an idle loop.
        state.measuredWorkloads.insert(state.activeWorkload);
        if (state.verification) state.verifyAfter = -1.0;
        return;
    }
    state.samples = {result.calibration};
    state.measuredWorkloads.insert(state.activeWorkload);
    if (state.verification) state.verifyAfter = -1.0;
    m_RawViewportWarmupEdge = m_RawViewportTimingBank.EdgeForFps(Raw::ViewportWorkloadKeys(RawViewportRecipe()),
        GetRawViewportTargetFps(), std::max(m_RawRenderSessionFullFrameWidth, m_RawRenderSessionFullFrameHeight),
        m_RawViewportEditStage, false);
    const int edge = GetCalibratedRawViewportEdge();
    if (edge > 0 && !IsRawWorkspaceUiInteractionActive()) {
        m_RawWorkspaceAdaptivePreviewScale = static_cast<float>(edge) /
            std::max(1, m_RawWorkspacePhysicalViewportMaxDimension);
        m_RawWorkspacePreviewHealthyStreak = 0;
        m_RawWorkspacePreviewSlowSamples = 0;
    }
}
