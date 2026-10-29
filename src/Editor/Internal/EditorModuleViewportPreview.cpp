#include "Editor/EditorModule.h"
#include "App/UiPerformanceProbe.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Editor/GraphViewportAnalysisPolicy.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Renderer/RenderTiling.h"
#include "Renderer/GraphViewportPlanner.h"
#include "Utils/HashUtils.h"
#include "Utils/DisplayRefreshRate.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <iostream>

namespace {
using namespace Stack::Renderer;
struct ViewportDemand {
    ImageRegion region;
    int physicalWidth = 1, physicalHeight = 1;
    bool empty = true;
};
ViewportDemand ClipViewportDemand(int width, int height, ImVec2 imageMin, ImVec2 imageMax,
    ImVec2 clipMin, ImVec2 clipMax, ImVec2 framebufferScale) {
    ViewportDemand demand;
    if (width <= 0 || height <= 0 || imageMax.x <= imageMin.x || imageMax.y <= imageMin.y) return demand;
    ImRect visible(clipMin, clipMax); visible.ClipWithFull(ImRect(imageMin, imageMax));
    if (visible.GetWidth() <= 0 || visible.GetHeight() <= 0) return demand;
    const int left = std::clamp(int(std::floor((visible.Min.x-imageMin.x)*width/(imageMax.x-imageMin.x))),0,width-1);
    const int top = std::clamp(int(std::floor((visible.Min.y-imageMin.y)*height/(imageMax.y-imageMin.y))),0,height-1);
    const int right = std::clamp(int(std::ceil((visible.Max.x-imageMin.x)*width/(imageMax.x-imageMin.x))),left+1,width);
    const int bottom = std::clamp(int(std::ceil((visible.Max.y-imageMin.y)*height/(imageMax.y-imageMin.y))),top+1,height);
    demand.region = {width,height,left,top,right-left,bottom-top};
    demand.physicalWidth = std::max(1,int(std::ceil(visible.GetWidth()*framebufferScale.x)));
    demand.physicalHeight = std::max(1,int(std::ceil(visible.GetHeight()*framebufferScale.y)));
    demand.empty = false;
    return demand;
}
std::size_t ContentIdentity(const EditorRenderWorker::Snapshot& snapshot) {
    using namespace Stack::Renderer::RawDevelopmentCache;
    std::size_t key = Stack::EditorRendering::BuildRawPresentationFingerprint(
        snapshot.rawWorkspace.recipe, snapshot.graph);
    HashTypedValue(key, snapshot.graph.outputNodeId);
    HashTypedValue(key, snapshot.graph.outputSocketId);
    HashTypedValue(key, snapshot.graph.semanticFingerprint);
    for (const auto& node : snapshot.graph.nodes) {
        HashTypedValue(key, node.nodeId);
        HashTypedValue(key, node.requestRevision);
        HashTypedValue(key, node.semanticDescriptorIdentity);
        HashTypedJson(key, node.layerJson);
        HashTypedJson(key, node.rawCoverage);
        HashTypedValue(key, node.rawSource.sourcePath);
    }
    return key;
}
std::size_t ExecutionIdentity(const EditorRenderWorker::Snapshot& snapshot,
    const RenderGraphViewportPlan& plan, const ViewportPreviewRequest& request, int changingNode) {
    using namespace Stack::Renderer::RawDevelopmentCache;
    std::size_t key = 1;
    HashTypedValue(key, snapshot.graph.outputNodeId);
    HashTypedValue(key, snapshot.graph.outputSocketId);
    HashTypedValue(key, changingNode);
    HashTypedValue(key, plan.logicalWidth);
    HashTypedValue(key, plan.logicalHeight);
    HashTypedValue(key, snapshot.rawWorkspace.sourceHash);
    if (snapshot.rawWorkspace.hasRecipe)
        for (auto stage : Raw::ViewportWorkloadKeys(snapshot.rawWorkspace.recipe)) HashTypedValue(key,stage);
    // Cost depends on coverage size. Camera position and generation do not
    // create new profiles for a pan across an otherwise identical workload.
    HashTypedValue(key, request.visible.width);
    HashTypedValue(key, request.visible.height);
    HashTypedValue(key, snapshot.prepareViewportOverview);
    HashTypedValue(key, snapshot.telemetry.interactionActive);
    for (const auto& preview : snapshot.previews) {
        HashTypedValue(key, preview.sourceNodeId);
        HashTypedValue(key, preview.width); HashTypedValue(key, preview.height);
    }
    HashTypedValue(key, snapshot.developCandidateRenders.size());
    for (const auto& node : snapshot.graph.nodes) {
        HashTypedValue(key, node.nodeId);
        HashTypedValue(key, static_cast<int>(node.kind));
        HashTypedValue(key, node.definitionHash);
        // Only Layer nodes carry layer JSON. Sources, outputs and typed RAW
        // operations legitimately leave it null.
        if (node.kind == RenderGraphNodeKind::Layer) {
            HashTypedValue(key, node.layerJson.value("type", std::string{}));
        }
        if (node.kind == RenderGraphNodeKind::RawOperation) {
            HashTypedValue(key, static_cast<int>(node.rawOperation.kind));
            HashTypedValue(key, node.rawOperation.enabled);
            for (auto stage : Raw::ViewportWorkloadKeys(Stack::RawRecipe::ReadGraphOperation(node.rawOperation)))
                HashTypedValue(key, stage);
        }
    }
    for (const auto& link : snapshot.graph.links) {
        HashTypedValue(key, link.fromNodeId); HashTypedValue(key, link.fromSocketId);
        HashTypedValue(key, link.toNodeId); HashTypedValue(key, link.toSocketId);
    }
    for (const auto& stage : plan.stages) {
        HashTypedValue(key, stage.second.regionalExecutable);
        HashTypedValue(key, stage.second.completeReason);
    }
    return key;
}
}

bool EditorModule::UsesGraphViewportPreview() const {
    return !m_RawWorkspaceRootTabActive || !IsRawWorkspaceProjectActive() ||
        !UsesRawWorkspaceStageRender() ||
        m_Project->rawMode == Stack::RawWorkspace::RawProjectMode::UnifiedLayers;
}
Stack::EditorRendering::ViewportPreviewState& EditorModule::ActiveViewportPreviewState() {
    return m_ViewportPreviewStates[m_RawWorkspaceRootTabActive ? 0 : 1];
}
const Stack::EditorRendering::ViewportPreviewState& EditorModule::ActiveViewportPreviewState() const {
    return m_ViewportPreviewStates[m_RawWorkspaceRootTabActive ? 0 : 1];
}
bool EditorModule::TryGetViewportPreviewGeometry(ViewportPreviewGeometry& geometry) const {
    geometry = ActiveViewportPreviewState().geometry;
    if (UsesGraphViewportPreview()) {
        for (const auto& state : m_ViewportPreviewStates)
            if (state.acceptedGeneration == m_ViewportOutputRenderGeneration && state.geometry.Valid()) {
                geometry = state.geometry; return true;
            }
        return false;
    }
    const auto& texture = m_RawWorkspacePresentationTexture;
    const auto region = m_RawViewportPresentedRegion.Valid() ? m_RawViewportPresentedRegion :
        WholeImageRegion(texture.width, texture.height);
    geometry = {m_ViewportOutputExpectedNativeWidth, m_ViewportOutputExpectedNativeHeight,
        region, texture.width, texture.height};
    return geometry.Valid();
}
bool EditorModule::TryGetViewportPreviewOverview(unsigned int& texture, int& width, int& height) const {
    texture = 0; width = height = 0;
    if (!UsesGraphViewportPreview()) {
        if (!m_RawViewportOverviewContent || m_RawViewportOverviewContent != m_RawViewportPresentationContent ||
            !IsViewportTextureSafeForDrawing(m_RawViewportOverviewTexture.texture)) return false;
        texture = m_RawViewportOverviewTexture.texture;
        width = m_RawViewportOverviewTexture.width; height = m_RawViewportOverviewTexture.height;
        return true;
    }
    for (const auto& state : m_ViewportPreviewStates) {
        if (state.acceptedGeneration != m_ViewportOutputRenderGeneration || !state.overviewContentIdentity ||
            state.overviewContentIdentity != state.acceptedContentIdentity ||
            !IsViewportTextureSafeForDrawing(state.overview.texture)) continue;
        texture = state.overview.texture; width = state.overview.width; height = state.overview.height;
        return true;
    }
    return false;
}
void EditorModule::UpdateGraphViewportCoverage(ImVec2 imageMin, ImVec2 imageMax,
    ImVec2 clipMin, ImVec2 clipMax, bool navigationActive) {
    if (!UsesGraphViewportPreview() || imageMax.x <= imageMin.x || imageMax.y <= imageMin.y) return;
    const auto* viewport = ImGui::GetWindowViewport();
    const int refresh = DisplayRefreshRate::ForWindow(viewport ? static_cast<GLFWwindow*>(viewport->PlatformHandle) : nullptr);
    m_RawViewportDisplayRefreshRate = refresh > 0 ? refresh : 60;
    SyncRawViewportPreferences();
    auto& state = ActiveViewportPreviewState();
    state.mainDemandFrame = ImGui::GetFrameCount();
    ViewportPreviewGeometry presented;
    TryGetViewportPreviewGeometry(presented);
    const int width = presented.logicalWidth, height = presented.logicalHeight;
    if (width <= 0 || height <= 0) return;
    auto demand = ClipViewportDemand(width,height,imageMin,imageMax,clipMin,clipMax,ImGui::GetIO().DisplayFramebufferScale);
    state.mainEmpty = demand.empty;
    if (demand.empty && state.backdropDemandFrame >= ImGui::GetFrameCount()-1 &&
        state.backdropVisible.Valid() && state.backdropVisible.fullWidth == width && state.backdropVisible.fullHeight == height) {
        // A fixed-fit photo remains visible when the navigable image leaves
        // its clip. Merge that consumer before comparing the effective demand,
        // so consecutive main/backdrop publications do not alternate generations.
        demand = {state.backdropVisible,state.backdropPhysicalWidth,state.backdropPhysicalHeight,false};
    }
    const auto& region = demand.region;
    const bool empty = demand.empty;
    const int physicalWidth = demand.physicalWidth, physicalHeight = demand.physicalHeight;
    state.navigationActive = navigationActive;
    if (region == state.request.visible && empty == state.request.empty &&
        physicalWidth == state.physicalWidth && physicalHeight == state.physicalHeight) return;
    const auto previousRegion = state.request.visible;
    state.request.visible = region; state.request.enabled = true; state.request.empty = empty;
    ++state.request.generation;
    state.physicalWidth = physicalWidth; state.physicalHeight = physicalHeight;
    if (!previousRegion.Valid() || region.fullWidth != previousRegion.fullWidth ||
        region.fullHeight != previousRegion.fullHeight ||
        (region.width < previousRegion.width && region.height <= previousRegion.height) ||
        (region.height < previousRegion.height && region.width <= previousRegion.width))
        state.refinementBlocked = false;
    // In-flight native pixels may still cover the new camera. Keep their
    // request ownership until completion instead of rearming every UI frame.
    state.refinementFailures = 0;
    state.refinementRetryAt = -1; state.refinementError.clear();
    m_Viewport.FrameTransition().Clear();
    if (m_RawWorkspaceRootTabActive) ClearRawViewportTransition();
    // Camera movement is presentation state. The scheduler requests missing
    // detail when a worker slot is available, without invalidating the graph
    // and rebuilding its snapshot on every pan/zoom animation frame.
}

void EditorModule::UpdateGraphBackdropViewportCoverage(ImVec2 imageMin, ImVec2 imageMax,
    ImVec2 clipMin, ImVec2 clipMax) {
    if (!UsesGraphViewportPreview()) return;
    auto& state = ActiveViewportPreviewState();
    ViewportPreviewGeometry presented;
    TryGetViewportPreviewGeometry(presented);
    const auto demand = ClipViewportDemand(presented.logicalWidth,presented.logicalHeight,
        imageMin,imageMax,clipMin,clipMax,ImGui::GetIO().DisplayFramebufferScale);
    state.backdropVisible = demand.region;
    state.backdropPhysicalWidth = demand.physicalWidth; state.backdropPhysicalHeight = demand.physicalHeight;
    state.backdropDemandFrame = ImGui::GetFrameCount();
    if (!state.mainEmpty && state.mainDemandFrame >= ImGui::GetFrameCount()-1) return;
    const int mainDemandFrame = state.mainDemandFrame;
    const bool mainEmpty = state.mainEmpty;
    const bool navigationActive = state.navigationActive && mainDemandFrame >= ImGui::GetFrameCount()-1;
    UpdateGraphViewportCoverage(imageMin,imageMax,clipMin,clipMax,navigationActive);
    state.mainDemandFrame = mainDemandFrame;
    state.mainEmpty = mainEmpty;
}

void EditorModule::ConfigureViewportPreview(EditorRenderWorker::Snapshot& snapshot) {
    static int diagnosticRequests = 0;
    const bool traceRequest = Stack::UiPerformance::Probe::Active() && diagnosticRequests++ < 16;
    if (traceRequest) std::clog << "Viewport request generation=" << snapshot.generation
        << " purpose=" << RawRenderPurposeName(snapshot.rawRenderPurpose)
        << " outputConnected=" << snapshot.outputConnected << '\n';
    if (!UsesGraphViewportPreview() || !snapshot.outputConnected ||
        (snapshot.rawRenderPurpose != RawRenderPurpose::InteractivePresentation &&
         snapshot.rawRenderPurpose != RawRenderPurpose::ViewportRefinement &&
         snapshot.rawRenderPurpose != RawRenderPurpose::AnalysisScopes) ||
        GetViewportMode() == ViewportMode::CompositeCanvas) return;
    auto& state = ActiveViewportPreviewState();
    const auto content = ContentIdentity(snapshot);
    const bool tracePlanning = Stack::UiPerformance::Probe::Active();
    auto planningCheckpoint = tracePlanning ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const auto checkpoint = [&](const char* phase) {
        if (!tracePlanning) return;
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double,std::milli>(now-planningCheckpoint).count();
        if (elapsed > 5.0) std::clog << "Viewport planning " << phase << " ms=" << elapsed << '\n';
        planningCheckpoint = now;
    };
    if (snapshot.rawRenderPurpose == RawRenderPurpose::AnalysisScopes) {
        // An edit can make optional analysis and the main image dirty together.
        // Publish the changed image first; an auxiliary packet must not consume
        // its revision while leaving the previous image on screen.
        // A remembered request is not proof of a displayed image: Graph may
        // have replaced the shared output since this View last ran, or the
        // request may have been cancelled before adoption. In those cases an
        // analysis-only refresh would leave that other output on screen until
        // another authored edit. Check the accepted owner/content instead.
        const bool presentationAvailable = HasViewportOutputTiles() ||
            (snapshot.rawWorkspace.sourceKey.empty() ? m_Pipeline.GetOutputTexture() != 0 :
                m_RawWorkspacePresentationTexture.texture != 0);
        const bool currentPresentation = presentationAvailable && state.acceptedGeneration != 0 &&
            state.acceptedGeneration == m_ViewportOutputRenderGeneration &&
            state.acceptedContentIdentity == content && state.geometry.Valid() &&
            Stack::GraphRendering::SameContext(state.acceptedTag, CurrentGraphRenderTag());
        if (currentPresentation && !state.refinement) return;
        snapshot.rawRenderPurpose = state.refinement ? RawRenderPurpose::ViewportRefinement :
            RawRenderPurpose::InteractivePresentation;
    }
    RefreshRawWorkspaceGpuMemoryBudget();
    auto request = state.request; request.enabled = true;
    snapshot.rawWorkspace.viewport = {};
    snapshot.rawWorkspace.analysisRequested = false;
    snapshot.rawWorkspace.graphScopeStage = RawDevelopmentGraphScopeStage::None;
    snapshot.graph.rawLayerScopeNodeId = 0;
    snapshot.graph.rawLayerScopeOperationId = 0;
    snapshot.rawWorkspace.gradingScopeSource = RawDevelopmentGradingScopeSource::None;
    if (request.empty) {
        snapshot.viewportPreview = request;
        snapshot.viewportViewId = m_RawWorkspaceRootTabActive ? 1 : 2;
        return;
    }
    const int width = snapshot.rawWorkspace.fullFrameWidth > 0 ? snapshot.rawWorkspace.fullFrameWidth : snapshot.width;
    const int height = snapshot.rawWorkspace.fullFrameHeight > 0 ? snapshot.rawWorkspace.fullFrameHeight : snapshot.height;
    RenderTiling::GraphViewportPlanner planner(snapshot.graph);
    auto nativePlan = planner.Plan(width,height,0,request);
    checkpoint("native-plan");
    if (!nativePlan.valid || nativePlan.logicalWidth <= 0 || nativePlan.logicalHeight <= 0) return;
    const int nativeEdge = std::max(nativePlan.logicalWidth,nativePlan.logicalHeight);
    if (!request.visible.Valid() || request.visible.fullWidth != nativePlan.logicalWidth ||
        request.visible.fullHeight != nativePlan.logicalHeight) {
        request.visible = WholeImageRegion(nativePlan.logicalWidth,nativePlan.logicalHeight);
        request.empty = false;
        if (state.request.visible.Valid()) ++request.generation;
        state.request = request;
        state.refinement = false; state.refinementBlocked = false;
        state.refinementGeneration = 0; state.refinementFailures = 0;
        state.refinementRetryAt = -1; state.refinementError.clear();
    }
    if (traceRequest) std::clog << "Viewport content=" << content << " accepted="
        << state.acceptedContentIdentity << " refine=" << state.refinement << '\n';
    if (content != state.contentIdentity) {
        state.contentIdentity = content; state.contentChangedAt = ImGui::GetTime();
        state.qualityModel.valid = false;
        ++state.qualityModel.revision;
        state.refinement = false; state.refinementBlocked = false;
        state.refinementGeneration = 0; state.refinementFailures = 0;
        state.refinementRetryAt = -1; state.refinementError.clear();
    }
    // A changed render revision remains untrusted until snapshot content has
    // been reconciled above. Pure refinement can then reuse the editing model.
    state.qualityModel.renderRevision = m_RenderRevision;
    const auto visible = request.visible;
    request.visible = PadViewportPreviewRegion(visible);
    const auto preferences = GetRawViewportPreferences();
    // Accepted pixels can arrive before this surface has ever drawn a photo.
    // Their geometry does not make the default 1x1 physical demand real. Graph
    // also renders while its image viewport is hidden. Use the provisional
    // preview until a current image consumer has supplied actual coverage.
    const int frame = ImGui::GetFrameCount();
    const bool measuredDemand = state.request.enabled && !state.request.empty &&
        state.request.visible.Valid() &&
        (state.mainDemandFrame >= frame-1 || state.backdropDemandFrame >= frame-1);
    const double displayScale = measuredDemand ? std::min(1.0,std::max(
        double(state.physicalWidth)/visible.width,double(state.physicalHeight)/visible.height)) :
        std::min(1.0,1024.0/nativeEdge);
    const int displayEdge = std::clamp(int(std::ceil(nativeEdge*displayScale)),1,nativeEdge);
    const int minimum = preferences.mode == Raw::ViewportInteractionMode::PreserveDetail ?
        std::max(1,int(std::ceil(displayEdge*preferences.minimumDetailPercent/100.0))) :
        std::min(displayEdge,ViewportPreviewEdgeForVisiblePixels(visible,nativeEdge,256));
    const int initialEdge = std::clamp(
        ViewportPreviewEdgeForVisiblePixels(visible,nativeEdge,1024),minimum,displayEdge);
    snapshot.prepareViewportOverview = request.visible.Partial() && state.overviewContentIdentity != content;
    snapshot.telemetry.interactionActive = !state.refinement &&
        (IsRawWorkspaceImageEditActive() || ImGui::GetTime()-state.contentChangedAt < .10);
    auto identity = ExecutionIdentity(snapshot,nativePlan,request,m_GraphPerformanceStats.lastTouchedNodeId);
    checkpoint("execution-identity");
    Stack::Renderer::RawDevelopmentCache::HashTypedValue(identity,CurrentGraphRenderTag().sourceIdentity);
    // Stabilize within a compatible workload only. A changed crop or quality
    // mode must not inherit the previous zoom's full-image sampling edge.
    if (identity != state.executionIdentity) state.interactiveEdge = 0;
    std::set<int> edges{minimum,initialEdge,displayEdge,nativeEdge};
    for (int edge = std::max(1,minimum); edge < displayEdge;) {
        edges.insert(edge); const int next = std::max(edge+1,int(std::ceil(edge*1.25))); edge = next;
    }
    if (state.interactiveEdge >= minimum && state.interactiveEdge <= displayEdge) edges.insert(state.interactiveEdge);
    const std::uint64_t retained = std::uint64_t(std::max(0,state.geometry.storageWidth))*
        std::max(0,state.geometry.storageHeight)*16u*2u +
        std::uint64_t(std::max(0,state.overview.width))*std::max(0,state.overview.height)*16u;
    const std::uint64_t budget = m_RawWorkspaceVramWorkingBudgetBytes > retained ?
        m_RawWorkspaceVramWorkingBudgetBytes-retained : 1u;
    std::vector<ViewportPreviewCandidate> candidates;
    for (int edge : edges) {
        auto plan = planner.Plan(width,height,edge == nativeEdge ? 0 : edge,request);
        if (!plan.valid) continue;
        bool fits = true;
        for (const auto& stage : plan.stages) if (stage.second.renderWidth > m_RawWorkspaceGlMaxTextureSize ||
            stage.second.renderHeight > m_RawWorkspaceGlMaxTextureSize) fits = false;
        const auto estimate = state.timings.Estimate(identity,edge);
        candidates.push_back({edge,estimate.predictedMs,estimate.costKnown,estimate.timingCovered,
            fits ? plan.workingBytes : std::numeric_limits<std::uint64_t>::max(),identity});
    }
    state.selection = SelectViewportPreviewCandidate({displayEdge,minimum,nativeEdge,GetRawViewportTargetFps(),0,budget,
        preferences.mode == Raw::ViewportInteractionMode::PreserveDetail,initialEdge},candidates);
    // Retain admitted plans for the settings forecast. Reuse these plans and
    // timing evidence; drawing/hovering a setting must never plan or render graphs.
    if (snapshot.telemetry.interactionActive && measuredDemand) {
        auto& model = state.qualityModel;
        model.input = {displayEdge,minimum,nativeEdge,GetRawViewportTargetFps(),0,budget,
            preferences.mode == Raw::ViewportInteractionMode::PreserveDetail,initialEdge};
        model.candidates = candidates;
        model.visibleWidth = visible.width; model.visibleHeight = visible.height;
        model.physicalWidth = state.physicalWidth; model.physicalHeight = state.physicalHeight;
        model.preserveDetail = preferences.mode == Raw::ViewportInteractionMode::PreserveDetail;
        model.minimumDetailPercent = preferences.minimumDetailPercent;
        model.valid = true; ++model.revision;
    }
    int edge = state.selection.edge;
    const auto measuredSizes = std::count_if(candidates.begin(), candidates.end(),
        [](const auto& candidate) { return candidate.timingCovered; });
    if (!state.refinement && state.interactiveEdge > minimum && measuredSizes == 1 &&
        state.selection.predictedMs > 1000.0 / std::max(1,GetRawViewportTargetFps())) {
        // One slow size cannot identify the fixed dependency cost. Learn one
        // smaller executable size on the next edit before assuming a floor.
        int probe = 0;
        for (const auto& candidate : candidates)
            if (candidate.edge < state.interactiveEdge && candidate.workingBytes <= budget)
                probe = std::max(probe,candidate.edge);
        if (probe) edge = probe;
    }
    if (state.refinement) {
        checkpoint("candidates");
        // Once an edit settles, retain the complete native image whenever its
        // existing planner admits it. Later camera movement then samples that
        // same resident output instead of repeatedly rendering native crops.
        auto completeRequest = request;
        completeRequest.visible = WholeImageRegion(nativePlan.logicalWidth, nativePlan.logicalHeight);
        completeRequest.samplingScale = 1.0;
        const auto completePlan = planner.Plan(width, height, 0, completeRequest);
        const bool completeFits = completePlan.valid && completePlan.workingBytes <= budget &&
            std::all_of(completePlan.stages.begin(), completePlan.stages.end(), [&](const auto& stage) {
                return stage.second.renderWidth <= m_RawWorkspaceGlMaxTextureSize &&
                    stage.second.renderHeight <= m_RawWorkspaceGlMaxTextureSize;
            });
        const auto native = std::find_if(candidates.begin(),candidates.end(),[&](const auto& candidate){return candidate.edge==nativeEdge;});
        if (completeFits) {
            request = completeRequest;
            edge = nativeEdge;
            identity = ExecutionIdentity(snapshot, completePlan, request, m_GraphPerformanceStats.lastTouchedNodeId);
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(identity, CurrentGraphRenderTag().sourceIdentity);
        } else if (native != candidates.end() && native->workingBytes <= budget) edge = nativeEdge;
        else {
            // Navigation/refinement must not intentionally replace retained
            // detail with the editing governor's low-resolution candidate.
            for (const auto& candidate : candidates)
                if (candidate.edge <= displayEdge && candidate.workingBytes <= budget)
                    edge = std::max(edge,candidate.edge);
            state.refinement = false; state.refinementBlocked = true;
            state.blockedBudget = m_RawWorkspaceVramWorkingBudgetBytes;
            state.refinementError = native == candidates.end() ? "No valid native viewport plan" :
                native->workingBytes == std::numeric_limits<std::uint64_t>::max() ?
                    "Native viewport exceeds the texture-size limit" :
                    "Native viewport estimate " + std::to_string(native->workingBytes / (1024u*1024u)) +
                    " MiB exceeds " + std::to_string(budget / (1024u*1024u)) + " MiB headroom";
        }
    }
    if (edge <= 0) {
        // The dependency estimate can exceed headroom even at minimum
        // resolution. Try the smallest planned foreground raster while
        // keeping optional native refinement deferred and cache reserves low.
        edge = candidates.empty() ? minimum : candidates.front().edge;
        state.refinementBlocked = true; state.blockedBudget = m_RawWorkspaceVramWorkingBudgetBytes;
    }
    if (!state.refinement && !state.refinementBlocked && state.interactiveEdge > 0 && state.selection.measured) {
        const int low = std::max(minimum,int(state.interactiveEdge*.75));
        const int high = std::min(displayEdge,std::max(state.interactiveEdge+1,int(state.interactiveEdge*1.3)));
        int bounded = 0;
        for (const auto& candidate : candidates) if (candidate.edge >= low && candidate.edge <= high &&
            candidate.workingBytes <= budget && (!bounded || std::abs(candidate.edge-edge)<std::abs(bounded-edge))) bounded=candidate.edge;
        if (bounded) edge = bounded;
    }
    state.interactiveEdge = state.refinement ? state.interactiveEdge : edge;
    state.executionIdentity = identity;
    snapshot.previewMaxDimension = edge >= nativeEdge ? 0 : edge;
    snapshot.rawRenderPurpose = state.refinement ? RawRenderPurpose::ViewportRefinement : RawRenderPurpose::InteractivePresentation;
    if (state.refinement) state.refinementGeneration = snapshot.generation;
    request.samplingScale = std::min(1.0,double(edge)/nativeEdge);
    snapshot.viewportPreview = request;
    snapshot.viewportViewId = m_RawWorkspaceRootTabActive ? 1 : 2;
    state.submittedViewGeneration = request.generation;
    snapshot.viewportExecutionIdentity = identity;
    snapshot.viewportContentIdentity = content;
    snapshot.prepareViewportOverview = request.visible.Partial() && state.overviewContentIdentity != content;
    snapshot.viewportTiling.mode = ViewportTilingMode::Off;
    const auto chosen = state.timings.Estimate(identity,edge);
    state.plannedEdge = edge;
    state.plannedTiming = chosen;
    snapshot.telemetry.predictedMs = chosen.costKnown ? chosen.predictedMs : 0.0;
    // An active ImGui item can be a floating window move, camera drag or FPS
    // setting. Only image edits should request approximate processing kernels.
    snapshot.telemetry.interactionActive = snapshot.rawRenderPurpose == RawRenderPurpose::InteractivePresentation &&
        (IsRawWorkspaceImageEditActive() || ImGui::GetTime()-state.contentChangedAt < .10);
    state.submittedTag = CurrentGraphRenderTag();
    checkpoint("selection-and-tag");
    state.submittedPurpose = snapshot.rawRenderPurpose;
}

void EditorModule::UpdateViewportPreviewRefinement(double now) {
    if (!UsesGraphViewportPreview() || GetViewportMode()==ViewportMode::CompositeCanvas) return;
    auto& state = ActiveViewportPreviewState();
    // Failed submission or a completion discarded before adoption must not
    // hold the view in a permanently pending refinement state. Deferred GPU
    // fences still own their requests and are allowed to finish normally.
    if (state.refinement && !m_RenderDirty && !m_RenderPending && !IsAnyRenderBackendBusy() &&
        m_DeferredRenderResults.empty()) {
        state.refinement = false;
        state.refinementGeneration = 0;
    }
    // The shell pumps hidden projects too. Refine only for an image consumer
    // drawn in this frame or the previous one (the pre-UI pump runs first).
    const int frame=ImGui::GetFrameCount();
    if (!state.request.enabled ||
        (state.mainDemandFrame<frame-1 && state.backdropDemandFrame<frame-1)) return;
    if (state.request.empty || !state.geometry.Valid() || state.refinement || m_RenderDirty || m_RenderPending || IsAnyRenderBackendBusy() ||
        !m_DeferredRenderResults.empty() || IsRawWorkspaceImageEditActive() ||
        now-state.contentChangedAt<.10) return;
    if (!m_FullQualityRenderRequest.Pending() &&
        ((m_RenderPresentation.failed && m_RenderPresentation.graphRequest.revision == m_RenderRevision) ||
         (DidFullQualityRenderFail() && m_FullQualityRenderRequest.graphRequest.revision == m_RenderRevision))) return;
    if (IsPresentedRenderComplete() && !state.acceptedInteractive && state.geometry.NativeSamples() && ContainsImageRegion(state.geometry.resident,state.request.visible) &&
        state.acceptedContentIdentity == state.contentIdentity) return;
    // Reuse the accepted detail and matching overview throughout navigation.
    // Missing coverage can wait for release; settling an actual edit still runs.
    if (state.navigationActive && !state.acceptedInteractive &&
        state.acceptedContentIdentity == state.contentIdentity) return;
    if (state.refinementFailures >= 3 || now < state.refinementRetryAt) return;
    if (state.refinementBlocked) {
        RefreshRawWorkspaceGpuMemoryBudget();
        if (m_RawWorkspaceVramWorkingBudgetBytes <= state.blockedBudget + 64u*1024u*1024u) {
            // Keep an adequate retained frame while native detail is deferred.
            // A pan exposing missing pixels may still need one admitted proxy
            // for its new demand, even if native allocation remains blocked.
            if (state.submittedViewGeneration == state.request.generation ||
                (state.acceptedContentIdentity == state.contentIdentity &&
                 ViewportPreviewCoversDisplay(state.geometry,state.request.visible,
                     state.physicalWidth,state.physicalHeight))) return;
        } else state.refinementBlocked = false;
    }
    Stack::UiPerformance::Probe::Refinement(state.contentIdentity,state.acceptedContentIdentity,
        state.geometry.NativeSamples(),ContainsImageRegion(state.geometry.resident,state.request.visible),
        state.geometry.logicalWidth,state.geometry.storageWidth,this);
    MarkRenderRefreshDirty();
    state.refinement = true;
}
bool EditorModule::TrySubmitGraphViewportAnalysis(double now) {
    if(m_GalleryNavigationMotion && (m_GalleryNavigationMotion->Waiting(m_GalleryNavigationWorkspace) ||
        m_GalleryNavigationMotion->ImageMoving(m_GalleryNavigationWorkspace)))return false;
    if (!m_RawWorkspaceAnalysisPending || !m_RawWorkspaceRootTabActive || !IsRawWorkspaceProjectActive() ||
        !UsesGraphViewportPreview() || GetViewportMode() == ViewportMode::CompositeCanvas ||
        m_RawAssetCapture.pending || m_RawAssetCapture.armed || IsExportBusy() ||
        IsRawWorkspaceProjectLoadBusy() || m_FullQualityRenderRequest.Pending() ||
        m_RawWorkspaceExportRenderRequested || m_RawWorkspaceExplicitFullQualityRenderRequested) return false;
    auto& state = ActiveViewportPreviewState();
    const int frame = ImGui::GetFrameCount();
    // A legacy view may have queued analysis without submitting it before
    // switching to this scheduler. Requested means in flight here; only adopt
    // that old intent after every worker/deferred owner has released it.
    if (Stack::EditorRendering::ShouldRequeueGraphViewportAnalysis(m_RawWorkspaceAnalysisPending,
            m_RawWorkspaceAnalysisRequested, state.analysisGeneration,
            m_RenderPending || IsAnyRenderBackendBusy() || !m_DeferredRenderResults.empty())) {
        m_RawWorkspaceAnalysisRequested = false;
    }
    const bool workOutstanding = m_RenderDirty || m_RenderPending || IsAnyRenderBackendBusy() ||
        state.refinement || !m_DeferredRenderResults.empty() || state.analysisGeneration != 0;
    // Pending edits can span many UI frames. Do not rescan graph/source tags
    // for optional work until the foreground request actually becomes idle.
    if (workOutstanding || m_RawWorkspaceAnalysisRequested || IsRawWorkspaceImageEditActive()) return false;
    const bool currentPresentation = state.acceptedGeneration &&
        state.acceptedGeneration == m_ViewportOutputRenderGeneration && state.geometry.Valid() &&
        state.acceptedContentIdentity == state.contentIdentity &&
        Stack::EditorRendering::IsCurrentGraphViewportAnalysis(state.acceptedTag, CurrentGraphRenderTag());
    const bool nativeReady = state.geometry.NativeSamples() &&
        ContainsImageRegion(state.geometry.resident, state.request.visible);
    const Stack::EditorRendering::GraphViewportAnalysisDemand demand{
        m_RawWorkspaceAnalysisPending, m_RawWorkspaceAnalysisRequested,
        currentPresentation, !state.acceptedInteractive && IsPresentedRenderComplete(),
        nativeReady || state.refinementBlocked,
        workOutstanding, false,
        (m_RawWorkspaceAnalysisQuietUntilTime <= 0 || now >= m_RawWorkspaceAnalysisQuietUntilTime) &&
            state.request.enabled && !state.request.empty &&
            (state.mainDemandFrame >= frame-1 || state.backdropDemandFrame >= frame-1)};
    if (!Stack::EditorRendering::ShouldSubmitGraphViewportAnalysis(demand)) return false;

    // This is an independent, complete-demand packet, not another main-image
    // revision. Advancing that revision would make the accepted native frame
    // appear incomplete and cause an unnecessary main render after every scope.
    const auto generation = Stack::EditorRenderScheduling::NextGlobalGeneration();
    m_RawWorkspaceAnalysisRequested = true;
    EditorRenderWorker::Snapshot snapshot;
    bool submitted = false;
    if (TryBuildRenderSnapshot(generation, snapshot) && snapshot.outputConnected &&
        ContentIdentity(snapshot) == state.acceptedContentIdentity) {
        snapshot.rawRenderPurpose = RawRenderPurpose::AnalysisScopes;
        snapshot.rawWorkspace.analysisRequested = true;
        if (m_NodeEditorPanel.visible && m_NodeEditorPanel.analysisTarget) {
            const auto target=*m_NodeEditorPanel.analysisTarget;
            const auto identity=Stack::Editor::NodeInputAnalysisIdentity(target,
                m_Project->editRevision+m_Project->rawLayers.ProcessingRevision(),CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame);
            m_NodeEditorState.Get<Stack::Editor::NodeEditorPanelAnalysis>(target).attemptedFingerprint=identity;
            bool configured=false;
            const auto* authored=ResolveNodeEditorTarget(target);
            if (authored) {
                const auto compiled=std::find_if(snapshot.graph.nodes.begin(),snapshot.graph.nodes.end(),[&](const auto& node) {
                    return node.authoredGraphId==target.layerId && node.authoredNodeUuid==target.nodeUuid;
                });
                if (compiled!=snapshot.graph.nodes.end()) {
                    const auto input=std::find_if(snapshot.graph.links.begin(),snapshot.graph.links.end(),[&](const auto& link) {
                        return link.toNodeId==compiled->nodeId && link.toSocketId=="imageIn";
                    });
                    if (input!=snapshot.graph.links.end()) {
                        configured=true;
                        snapshot.graph.rawLayerScopeNodeId=input->fromNodeId;
                        snapshot.graph.rawLayerScopeSocketId=input->fromSocketId;
                        snapshot.graph.rawLayerScopeOperationId=compiled->nodeId;
                        snapshot.graph.rawLayerScopeDescriptor=input->semanticDescriptor;
                        snapshot.rawWorkspace.graphScopeStage=Stack::Editor::FindNodeEditor(*authored).inputAnalysis;
                        snapshot.rawWorkspace.graphScopeOwnerKey=target.Key();
                        snapshot.rawWorkspace.graphScopeInputFingerprint=Stack::Editor::NodeInputAnalysisIdentity(target,
                            m_Project->editRevision+m_Project->rawLayers.ProcessingRevision(),CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame);
                        m_NodeEditorState.Get<Stack::Editor::NodeEditorPanelAnalysis>(target).attemptedFingerprint=
                            snapshot.rawWorkspace.graphScopeInputFingerprint;
                    }
                }
            }
            if (!configured) {
                m_NodeEditorPanel.analysisTarget.reset();
                m_RawWorkspaceAnalysisPending=m_RawWorkspaceAnalysisRequested=false;
                return false;
            }
        }
        snapshot.telemetry.interactionActive = false;
        snapshot.rawWorkspace.viewport = {};
        snapshot.viewportViewId = 1;
        snapshot.viewportContentIdentity = state.contentIdentity;
        snapshot.graphRequest = CurrentGraphRenderTag();
        m_RenderGeneration = generation;
        try { submitted = SubmitRenderSnapshot(std::move(snapshot)); }
        catch (...) { /* Release this optional request below; retain the main image. */ }
    }
    if (submitted) {
        state.analysisGeneration = generation;
        state.submittedPurpose = RawRenderPurpose::AnalysisScopes;
        m_RenderPending = true;
    } else {
        m_RawWorkspaceAnalysisRequested = false;
        m_RawWorkspaceAnalysisPending = m_RawWorkspaceAnalysisScopeRetryCount++ == 0;
        m_RawWorkspaceAnalysisQuietUntilTime = m_RawWorkspaceAnalysisPending ? now + .25 : -1;
    }
    return true;
}

void EditorModule::ObserveViewportPreviewTiming(const EditorRenderWorker::Result& result) {
    if (!result.viewportPreview.enabled || result.telemetry.superseded || !result.timingOnly) return;
    const int edge = result.previewMaxDimension > 0 ? result.previewMaxDimension :
        std::max(result.viewportPreviewGeometry.logicalWidth,result.viewportPreviewGeometry.logicalHeight);
    const auto& stats = result.mainGraphStats;
    const bool executed = stats.imageCacheMisses || stats.rawStageCacheMisses ||
        !stats.pointwiseGroups.empty() || stats.reductionPasses;
    const bool train = result.rawRenderPurpose == RawRenderPurpose::InteractivePresentation &&
        result.telemetry.interactionActive && executed && !result.viewportCold;
    Stack::UiPerformance::Probe::CompletedViewport({result.generation,result.viewportExecutionIdentity,
        edge,result.viewportPreviewGeometry.storageWidth,result.viewportPreviewGeometry.storageHeight,
        result.telemetry.predictedMs,result.telemetry.queueWaitMs,result.telemetry.cpuDispatchMs,
        result.telemetry.gpuServiceMs,result.telemetry.completedServiceMs,result.telemetry.interactionActive,
        result.viewportCold,result.rawRenderPurpose == RawRenderPurpose::ViewportRefinement,train,
        stats.viewportRegionalPixels,stats.viewportCompleteDependencyPixels},this);
    if (!train) return;
    auto& state = m_ViewportPreviewStates[result.viewportViewId == 1 ? 0 : 1];
    // The execution identity already includes coverage size and dependencies.
    // Moving the camera must not discard useful measurements from that work.
    using Clock = Stack::Renderer::ViewportPreviewTimingCurve::Clock;
    const auto completedAt = result.telemetry.queuedAt.time_since_epoch().count() ?
        result.telemetry.queuedAt + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double,std::milli>(result.telemetry.queueWaitMs + result.telemetry.completedServiceMs)) :
        Clock::now();
    state.timings.Record(result.viewportExecutionIdentity,
        edge,result.telemetry.completedServiceMs,false,completedAt);
    ++state.qualityModel.revision;
}
bool EditorModule::IsViewportPreviewResultCurrent(const EditorRenderWorker::Result& result) const {
    if (result.rawRenderPurpose == RawRenderPurpose::AnalysisScopes && result.viewportViewId) {
        return result.viewportViewId == (m_RawWorkspaceRootTabActive ? 1 : 2) && UsesGraphViewportPreview() &&
            Stack::EditorRendering::IsCurrentGraphViewportAnalysis(result.graphRequest, CurrentGraphRenderTag()) &&
            result.viewportContentIdentity == ActiveViewportPreviewState().contentIdentity;
    }
    if (!result.viewportPreview.enabled) return true;
    if (result.viewportViewId != (m_RawWorkspaceRootTabActive ? 1 : 2)) return false;
    if (!UsesGraphViewportPreview()) return false;
    const auto& state = ActiveViewportPreviewState();
    if (state.request.empty || (result.success && !result.viewportPreviewGeometry.Valid())) return false;
    if (result.success && result.viewportContentIdentity == state.acceptedContentIdentity &&
        ViewportPreviewCoversDisplay(state.geometry,state.request.visible,state.physicalWidth,state.physicalHeight) &&
        result.viewportPreviewGeometry.SampleSpacingX() > state.geometry.SampleSpacingX() &&
        result.viewportPreviewGeometry.SampleSpacingY() > state.geometry.SampleSpacingY()) {
        // A delayed proxy or memory fallback must not reduce detail already
        // available for this same image and current camera.
        return false;
    }
    if (result.rawRenderPurpose == RawRenderPurpose::ViewportRefinement &&
        result.viewportContentIdentity != state.contentIdentity) return false;
    if (result.viewportPreview.generation == state.request.generation) return true;
    return result.viewportContentIdentity == state.contentIdentity &&
        ViewportPreviewCoversDisplay(result.viewportPreviewGeometry,state.request.visible,
            state.physicalWidth,state.physicalHeight);
}
void EditorModule::AdoptViewportPreviewState(EditorRenderWorker::Result& result) {
    Stack::UiPerformance::Probe::AcceptedImage(result.generation,
        result.graphRequest.enabled ? result.graphRequest.revision : result.rawWorkspace.recipeRevision,this,
        !result.telemetry.interactionActive);
    auto& state = ActiveViewportPreviewState();
    if (!result.viewportPreview.enabled) {
        // Complete textures still carry explicit geometry for picking and fitting.
        const int width = !m_ViewportOutputTiles.tiles.empty() ? m_ViewportOutputTiles.fullWidth :
            (!result.rawWorkspace.sourceKey.empty() ? m_RawWorkspacePresentationTexture.width : m_Pipeline.GetCanvasWidth());
        const int height = !m_ViewportOutputTiles.tiles.empty() ? m_ViewportOutputTiles.fullHeight :
            (!result.rawWorkspace.sourceKey.empty() ? m_RawWorkspacePresentationTexture.height : m_Pipeline.GetCanvasHeight());
        const auto region = result.rawWorkspace.viewportRegion.Valid() ? result.rawWorkspace.viewportRegion : WholeImageRegion(width,height);
        state.geometry = {result.rawWorkspace.expectedNativeOutputWidth > 0 ? result.rawWorkspace.expectedNativeOutputWidth : region.fullWidth,
            result.rawWorkspace.expectedNativeOutputHeight > 0 ? result.rawWorkspace.expectedNativeOutputHeight : region.fullHeight,
            region,width,height};
        state.acceptedGeneration = result.generation;
        state.acceptedContentIdentity = 0;
        state.acceptedInteractive = result.rawRenderPurpose == RawRenderPurpose::InteractivePresentation &&
            result.telemetry.interactionActive;
        state.refinement = false;
        return;
    }
    state.geometry = result.viewportPreviewGeometry;
    state.acceptedTag = result.graphRequest;
    state.acceptedContentIdentity = result.viewportContentIdentity;
    // Native-sized interactive pixels can still use approximate kernels.
    // Finishing the edit must also replace those with settled-quality work.
    state.acceptedInteractive = result.rawRenderPurpose == RawRenderPurpose::InteractivePresentation &&
        result.telemetry.interactionActive;
    state.acceptedGeneration = result.generation;
    if (result.viewportOverview.texture) {
        QueueViewportOutputTextureRelease(state.overview);
        state.overview = std::move(result.viewportOverview);
        state.overviewContentIdentity = result.viewportContentIdentity;
    } else if (state.overviewContentIdentity != result.viewportContentIdentity) {
        QueueViewportOutputTextureRelease(state.overview); state.overviewContentIdentity = 0;
    }
    state.refinement = false;
}
void EditorModule::FinishViewportPreviewRequest(const EditorRenderWorker::Result& result, bool adopted) {
    if (result.rawRenderPurpose == RawRenderPurpose::AnalysisScopes && result.viewportViewId == 1) {
        auto& state = m_ViewportPreviewStates[0];
        const bool owns = Stack::EditorRendering::OwnsGraphViewportAnalysisCompletion(result.generation,
            state.analysisGeneration, m_LatestRawAuxiliaryGeneration);
        if (result.generation == state.analysisGeneration) state.analysisGeneration = 0;
        // Successful auxiliary adoption already resolves Pending/Requested.
        // Discarding/canceling an owned packet must also release its request,
        // but an older result cannot clear a newer auxiliary consumer's state.
        if (owns && m_RawWorkspaceAnalysisRequested && (!adopted || !result.rawWorkspace.analysisCaptured)) {
            m_RawWorkspaceAnalysisRequested = false;
            const bool superseded = result.telemetry.superseded || result.error == "Render superseded by a newer snapshot.";
            m_RawWorkspaceAnalysisPending = superseded || m_RawWorkspaceAnalysisScopeRetryCount++ == 0;
            m_RawWorkspaceAnalysisQuietUntilTime = m_RawWorkspaceAnalysisPending ? ImGui::GetTime() + .25 : -1;
        }
        return;
    }
    if (!result.viewportPreview.enabled || result.rawRenderPurpose != RawRenderPurpose::ViewportRefinement) return;
    if (result.viewportViewId != 1 && result.viewportViewId != 2) return;
    auto& state = m_ViewportPreviewStates[result.viewportViewId - 1];
    // A stale completion may finish its own request, but must never clear a
    // newer native job. Camera/content validity is checked separately when
    // deciding whether the completion may publish an image.
    if (!state.refinementGeneration || result.generation != state.refinementGeneration) return;
    state.refinementGeneration = 0;
    state.refinement = false;
    if (result.viewportPreview.generation != state.request.generation ||
        result.viewportContentIdentity != state.contentIdentity) return;
    if (adopted && result.viewportPreviewGeometry.NativeSamples()) {
        state.refinementFailures = 0; state.refinementRetryAt = -1; state.refinementError.clear();
    } else if (!result.telemetry.superseded && result.error != "Render superseded by a newer snapshot." &&
        (!result.success || !result.error.empty() || adopted)) {
        // Transient execution/synchronization failures are not memory-budget
        // rejections. Retry twice after the initial attempt, then retain the
        // error until a new edit or camera demand resets this workload.
        ++state.refinementFailures;
        state.refinementRetryAt = ImGui::GetTime() + .25 * state.refinementFailures;
        state.refinementError = result.error.empty() ? "Native render returned reduced sampling" : result.error;
    }
}
void EditorModule::ClearViewportPreviewState() {
    for (auto& state : m_ViewportPreviewStates) {
        QueueViewportOutputTextureRelease(state.overview);
        state = {};
    }
    m_Viewport.FrameTransition().Clear();
}
