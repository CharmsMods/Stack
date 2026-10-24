#include "Editor/EditorModule.h"
#include "Editor/NodeEditors/ToneCurveEditorState.h"
#include "Editor/NodeEditors/NodeInputAnalysisIdentity.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"

#include "Editor/Layers/ToneLayers.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

namespace {

int ProbeNodeId(const RenderGraphSnapshot& snapshot, const std::string& layerId, int authoredId) {
    if (layerId.empty()) return authoredId;
    const auto layer = snapshot.rawLayerMaskNodeIds.find(layerId);
    if (layer == snapshot.rawLayerMaskNodeIds.end()) return 0;
    const auto node = layer->second.find(authoredId);
    return node == layer->second.end() ? 0 : node->second;
}

bool SampleTexturePixel(
    unsigned int texture,
    int width,
    int height,
    float u,
    float v,
    std::array<float, 4>& outRgba) {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (texture == 0 || width <= 0 || height <= 0) {
        return false;
    }

    const int px = std::clamp(static_cast<int>(std::round(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(std::max(0, width - 1)))), 0, width - 1);
    const int py = std::clamp(static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(std::max(0, height - 1)))), 0, height - 1);
    const int readY = std::clamp(height - 1 - py, 0, height - 1);

    const Stack::Renderer::GLState::FramebufferState savedState;
    const Stack::Renderer::GLState::PixelPackState savedPackState;
    savedPackState.ConfigureTightCpuReadback();
    const unsigned int readFbo = GLHelpers::CreateFBO(texture);
    if (readFbo == 0) {
        savedPackState.Restore();
        return false;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);

    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(px, readY, 1, 1, GL_RGBA, GL_FLOAT, outRgba.data());
    const bool success = glGetError() == GL_NO_ERROR;

    savedPackState.Restore();
    savedState.Restore();
    glDeleteFramebuffers(1, &readFbo);
    return success;
}

bool SampleViewportTileSet(
    const EditorRenderWorker::SharedTextureTileSet& tileSet,
    float u,
    float v,
    std::array<float, 4>& outRgba) {
    if (!tileSet.tiled || !tileSet.complete || tileSet.fullWidth <= 0 || tileSet.fullHeight <= 0) {
        return false;
    }
    const int fullX = std::clamp(
        static_cast<int>(std::round(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(std::max(0, tileSet.fullWidth - 1)))),
        0,
        tileSet.fullWidth - 1);
    const int fullY = std::clamp(
        static_cast<int>(std::round(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(std::max(0, tileSet.fullHeight - 1)))),
        0,
        tileSet.fullHeight - 1);
    for (const EditorRenderWorker::SharedTextureTile& tile : tileSet.tiles) {
        if (fullX < tile.x || fullX >= tile.x + tile.width ||
            fullY < tile.y || fullY >= tile.y + tile.height) {
            continue;
        }
        const float localU =
            (static_cast<float>(fullX - tile.haloX) + 0.5f) /
            static_cast<float>(std::max(1, tile.haloWidth));
        const float localV =
            (static_cast<float>(fullY - tile.haloY) + 0.5f) /
            static_cast<float>(std::max(1, tile.haloHeight));
        return SampleTexturePixel(tile.texture, tile.haloWidth, tile.haloHeight, localU, localV, outRgba);
    }
    return false;
}

} // namespace

bool EditorModule::ProbeViewTransformInputStats(int viewTransformNodeId, RenderTextureStats& outStats) const {
    outStats = {};

    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(viewTransformNodeId);
    if (!node ||
        node->kind != EditorNodeGraph::NodeKind::Layer ||
        node->layerType != LayerType::ViewTransform) {
        return false;
    }

    const EditorNodeGraph::Link* input = GetNodeGraph().FindInputLink(viewTransformNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (IsEditingRawLayerMaskGraph() ||
        !TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int sourceNodeId = ProbeNodeId(snapshot,
        IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->layerId : "", input->fromNodeId);
    if (sourceNodeId <= 0) return false;
    const int syntheticOutputId = -300000 - viewTransformNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));
    snapshot.links.push_back(RenderGraphLink{
        sourceNodeId,
        input->fromSocketId,
        syntheticOutputId,
        EditorNodeGraph::kImageInputSocketId
    });
    snapshot.outputNodeId = syntheticOutputId;
    snapshot.outputSocketId = EditorNodeGraph::kImageInputSocketId;

    RenderPipeline probePipeline;
    probePipeline.Initialize();
    probePipeline.LoadSourceFromPixels(
        sourcePixels.empty() ? nullptr : sourcePixels.data(),
        sourceW,
        sourceH,
        std::max(1, sourceCh));
    probePipeline.ExecuteGraph(snapshot);
    outStats = probePipeline.GetOutputTextureStats();
    return outStats.valid;
}

ToneCurveLayer* EditorModule::ResolveCanonicalToneEditor(int nodeId) {
    const auto* node=SelectedEditingNode();
    if (!node || node->id!=nodeId || !m_Project->nodeEditor ||
        node->kind!=EditorNodeGraph::NodeKind::RawOperation ||
        node->rawOperation.kind!=Stack::RawRecipe::GraphOperationKind::ToneCurve ||
        node->rawOperation.parameters.value("curveMode",std::string{})!="advanced") return nullptr;
    auto& state=m_NodeEditorState.Get<Stack::Editor::ToneCurveEditorState>(*m_Project->nodeEditor);
    if (!state.advanced) state.advanced=std::make_shared<ToneCurveLayer>();
    const auto& parameters=node->rawOperation.parameters.at("advanced");
    if (state.appliedAdvanced!=parameters) {
        ToneCurveLayer defaults;
        state.advanced->Deserialize(defaults.Serialize());
        state.advanced->Deserialize(parameters);state.appliedAdvanced=parameters;
    }
    return state.advanced.get();
}

void EditorModule::CommitCanonicalToneEditor(int nodeId,const ToneCurveLayer& tone,bool active) {
    const auto* node=SelectedEditingNode();
    if (!node || node->id!=nodeId || !m_Project->nodeEditor ||
        node->kind!=EditorNodeGraph::NodeKind::RawOperation) return;
    RawWorkspaceEditContext context;
    const auto phase=m_Project->lifecycle.Phase();
    context.canEdit=phase!=Stack::Project::ProjectLifecyclePhase::Loading &&
        phase!=Stack::Project::ProjectLifecyclePhase::Conflict &&
        phase!=Stack::Project::ProjectLifecyclePhase::Importing &&
        phase!=Stack::Project::ProjectLifecyclePhase::ReadOnlyRecovery && !IsWorkspaceTransitionPending();
    BindRawOperationContext(context);
    if (!context.canEdit) return;
    const auto payload=tone.Serialize();
    const bool changed=context.recipe.finishTone.layerJson.at("advanced")!=payload;
    context.recipe.finishTone.layerJson["advanced"]=payload;
    CommitRawWorkspaceEditContext(context,changed,active);
    if (context.error.empty())
        m_NodeEditorState.Get<Stack::Editor::ToneCurveEditorState>(*m_Project->nodeEditor).appliedAdvanced=payload;
}

int EditorModule::ResolveFocusedToneCurveNodeId() const {
    if (const auto* selected=SelectedEditingNode(); selected &&
        selected->kind==EditorNodeGraph::NodeKind::RawOperation &&
        selected->rawOperation.kind==Stack::RawRecipe::GraphOperationKind::ToneCurve &&
        selected->rawOperation.parameters.value("curveMode",std::string{})=="advanced") return selected->id;
    auto isToneCurveNodeId = [&](int nodeId) {
        const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
        if (!node) {
            return false;
        }
        if (node->kind == EditorNodeGraph::NodeKind::Layer &&
            node->layerType == LayerType::ToneCurve &&
            node->layerIndex >= 0 &&
            node->layerIndex < static_cast<int>(GetLayers().size())) {
            return true;
        }
        return node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
            node->rawDevelop.integratedToneEnabled;
    };

    if (m_CanvasToolKind == CanvasToolKind::ToneCurveTarget &&
        isToneCurveNodeId(m_CanvasToolOwnerNodeId)) {
        return m_CanvasToolOwnerNodeId;
    }
    if (isToneCurveNodeId(m_ActiveComplexNodeId)) {
        return m_ActiveComplexNodeId;
    }
    const int selectedNodeId = GetNodeGraph().GetSelectedNodeId();
    if (isToneCurveNodeId(selectedNodeId)) {
        return selectedNodeId;
    }
    return -1;
}

bool EditorModule::HasFocusedToneCurveViewportInteraction() const {
    if (const auto* selected=SelectedEditingNode(); selected &&
        selected->kind==EditorNodeGraph::NodeKind::RawOperation &&
        selected->rawOperation.kind==Stack::RawRecipe::GraphOperationKind::ToneCurve)
        return selected->rawOperation.parameters.value("curveMode",std::string{})=="advanced";
    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        return false;
    }

    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(toneCurveNodeId);
    if (!node) {
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        return IsCanvasToolActiveForNode(toneCurveNodeId, CanvasToolKind::ToneCurveTarget);
    }

    return true;
}

void EditorModule::ClearTrackedToneCurveProbe() {
    m_ToneCurveProbeCache.Clear();
    if (m_LastToneCurveProbeNodeId <= 0) {
        m_LastToneCurveProbeNodeId = -1;
        return;
    }
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(m_LastToneCurveProbeNodeId);
    if (auto* canonical=ResolveCanonicalToneEditor(m_LastToneCurveProbeNodeId)) {
        canonical->ClearViewportProbe();canonical->EndViewportTargetDrag();
    } else if (node &&
        node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size())) {
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[static_cast<std::size_t>(node->layerIndex)].get())) {
            toneCurve->ClearViewportProbe();
            toneCurve->EndViewportTargetDrag();
        }
    } else if (node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        ClearIntegratedToneTransientState(m_LastToneCurveProbeNodeId);
    }
    m_LastToneCurveProbeNodeId = -1;
}

void EditorModule::ClearToneCurveViewportProbe() {
    if (const auto* selected=SelectedEditingNode()) if (auto* canonical=ResolveCanonicalToneEditor(selected->id)) {
        canonical->ClearViewportProbe();return;
    }
    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        ClearTrackedToneCurveProbe();
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }

    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(toneCurveNodeId);
    if (!node) {
        m_LastToneCurveProbeNodeId = -1;
        return;
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size())) {
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[static_cast<std::size_t>(node->layerIndex)].get())) {
            toneCurve->ClearViewportProbe();
            toneCurve->EndViewportTargetDrag();
            m_LastToneCurveProbeNodeId = toneCurveNodeId;
        }
        return;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        ClearIntegratedToneTransientState(toneCurveNodeId);
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        return;
    }

    m_LastToneCurveProbeNodeId = -1;
}

bool EditorModule::SampleToneCurveViewportPixel(
    int toneCurveNodeId,
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    std::array<float, 4>& outRgba) const {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };

    const auto* selected=SelectedEditingNode();
    const EditorNodeGraph::Node* node = selected && selected->id==toneCurveNodeId &&
        selected->kind==EditorNodeGraph::NodeKind::RawOperation ? selected : GetNodeGraph().FindNode(toneCurveNodeId);
    if (!node) {
        m_ToneCurveProbeCache.Clear();
        return false;
    }
    if (node->kind==EditorNodeGraph::NodeKind::RawOperation && basis==ToneCurveSamplingBasis::CurveInput) {
        RawWorkspaceEditContext context;BindRawOperationContext(context);
        if (!context.nodeTarget || !m_RawWorkspaceGraphScopeReadback.valid ||
            m_RawWorkspaceFinishToneInputGraphScopeCache.sourceKey!=context.nodeTarget->Key() ||
            m_RawWorkspaceFinishToneInputGraphScopeCache.inputFingerprint!=Stack::Editor::NodeInputAnalysisIdentity(
                *context.nodeTarget,m_Project->editRevision,CurrentUniformSourceIdentity(),m_TimelineUi.currentFrame)) return false;
        const auto& scope=m_RawWorkspaceGraphScopeReadback;
        const int x=std::clamp(int(u*scope.width),0,scope.width-1);
        const int y=std::clamp(int(v*scope.height),0,scope.height-1);
        const auto index=(static_cast<std::size_t>(y)*scope.width+x)*3u;
        if (index+2>=scope.pixels.size()) return false;
        outRgba={scope.pixels[index],scope.pixels[index+1],scope.pixels[index+2],1};return true;
    }

    if (basis == ToneCurveSamplingBasis::FinalPreview && !IsEditingRawLayerMaskGraph()) {
        if (HasViewportOutputTiles()) {
            return SampleViewportTileSet(m_ViewportOutputTiles, u, v, outRgba);
        }
        return m_Pipeline.SampleOutputPixel(u, v, outRgba);
    }

    if (!CanRefreshPreviewLikeNodes()) {
        m_ToneCurveProbeCache.Clear();
        return false;
    }

    // RAW scope and FinalPreview returned above. Only settled raster graphs
    // have a revisioned, immutable source contract suitable for this tiny cache.
    const bool rasterNodesOnly = std::all_of(GetNodeGraph().GetNodes().begin(),
        GetNodeGraph().GetNodes().end(), [](const auto& candidate) {
            return Stack::Editor::IsRasterToneProbeCacheNode(candidate.kind);
        });
    const bool cacheable = basis == ToneCurveSamplingBasis::CurveInput &&
        Stack::Editor::CanCacheRasterToneProbe(
            IsRawWorkspaceProjectActive() || IsMultiFrameRawProjectActive(),
            IsEditingRawLayerMaskGraph(), !m_TimelineUi.liveEditPreviewTargets.empty(),
            rasterNodesOnly);
    Stack::Editor::ToneCurveProbeKey probeKey;
    if (cacheable) {
        probeKey.owner = m_Project;
        probeKey.documentId = GetProjectDocumentId();
        probeKey.nodeInstance = node->instanceUuid;
        probeKey.nodeId = node->id;
        probeKey.editRevision = m_Project->editRevision;
        probeKey.renderRevision = m_RenderRevision;
        probeKey.structureRevision = GetNodeGraph().GetStructureRevision();
        probeKey.dirtyGeneration = m_NodeDirtyGenerationCounter;
        probeKey.sourceLoadGeneration = m_SourceLoadGeneration;
        probeKey.acceptedRenderGeneration = m_LastCompletedRenderGeneration;
        // EnsureSharedImagePixels reuses the established immutable fingerprint.
        probeKey.sourceIdentity = CurrentUniformSourceIdentity();
        probeKey.frame = m_TimelineUi.currentFrame;
        probeKey.duration = m_TimelineUi.durationFrames;
        probeKey.fps = m_TimelineUi.framesPerSecond;
        probeKey.livePreviewFrame = m_TimelineUi.liveEditPreviewFrame;
        probeKey.u = u;
        probeKey.v = v;
        if (m_ToneCurveProbeCache.Find(probeKey, outRgba)) return true;
    } else {
        m_ToneCurveProbeCache.Clear();
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -350000 - toneCurveNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));

    if ((node->kind == EditorNodeGraph::NodeKind::Layer && node->layerType == LayerType::ToneCurve) ||
        (node->kind==EditorNodeGraph::NodeKind::RawOperation && node->rawOperation.kind==Stack::RawRecipe::GraphOperationKind::ToneCurve)) {
        const EditorNodeGraph::Link* input = GetNodeGraph().FindInputLink(toneCurveNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            return false;
        }

        if (IsEditingRawLayerMaskGraph() ||
            !TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
            if (!IsEditingRawLayerMaskGraph() &&
                TryResolveReferenceSourceDimensions(input->fromNodeId, input->fromSocketId, sourceW, sourceH) &&
                sourceW > 0 &&
                sourceH > 0) {
                sourceCh = 4;
                sourcePixels.clear();
            } else {
                sourcePixels = m_Pipeline.GetSourcePixelsRaw();
                sourceW = m_Pipeline.GetCanvasWidth();
                sourceH = m_Pipeline.GetCanvasHeight();
                sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
            }
        }
        const int sourceNodeId = ProbeNodeId(snapshot,
            IsEditingRawLayerMaskGraph() ? m_RawLayerMaskWorkspace->layerId : "", input->fromNodeId);
        if (sourceNodeId <= 0) return false;
        snapshot.links.push_back(RenderGraphLink{
            sourceNodeId,
            input->fromSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (!TryResolveReferenceSourcePixels(toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
            sourcePixels = m_Pipeline.GetSourcePixelsRaw();
            sourceW = m_Pipeline.GetCanvasWidth();
            sourceH = m_Pipeline.GetCanvasHeight();
            sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
        }

        bool foundRawDevelopNode = false;
        for (RenderGraphNode& renderNode : snapshot.nodes) {
            if (renderNode.nodeId == toneCurveNodeId && renderNode.kind == RenderGraphNodeKind::RawDevelop) {
                renderNode.rawDevelop.integratedToneEnabled = false;
                foundRawDevelopNode = true;
                break;
            }
        }
        if (!foundRawDevelopNode) {
            return false;
        }

        snapshot.links.push_back(RenderGraphLink{
            toneCurveNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
    } else {
        return false;
    }

    if (sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    snapshot.outputNodeId = syntheticOutputId;
    snapshot.outputSocketId = EditorNodeGraph::kImageInputSocketId;

    RenderPipeline probePipeline;
    probePipeline.Initialize();
    probePipeline.LoadSourceFromPixels(
        sourcePixels.empty() ? nullptr : sourcePixels.data(),
        sourceW,
        sourceH,
        std::max(1, sourceCh));
    probePipeline.ExecuteGraph(snapshot);
    const bool sampled = probePipeline.SampleOutputPixel(u, v, outRgba);
    if (sampled && cacheable) m_ToneCurveProbeCache.StoreSuccess(probeKey, outRgba);
    return sampled;
}

void EditorModule::UpdateToneCurveViewportProbe(float u, float v) {
    if (const auto* selected=SelectedEditingNode()) if (auto* canonical=ResolveCanonicalToneEditor(selected->id)) {
        std::array<float,4> rgba{};
        const auto basis=canonical->GetSamplingBasis();
        if (SampleToneCurveViewportPixel(selected->id,basis,u,v,rgba)) canonical->UpdateViewportProbe(basis,u,v,rgba);
        else canonical->ClearViewportProbe();
        m_LastToneCurveProbeNodeId=selected->id;return;
    }
    if (m_CanvasToolKind == CanvasToolKind::PickColor || m_CanvasToolKind == CanvasToolKind::AdjustAberrationCenter) {
        ClearTrackedToneCurveProbe();
        return;
    }

    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        ClearTrackedToneCurveProbe();
        return;
    }

    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(toneCurveNodeId);
    if (!node) {
        ClearTrackedToneCurveProbe();
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = ResolveCanonicalToneEditor(node->id);
    if (!toneCurve && node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        ClearTrackedToneCurveProbe();
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }

    std::array<float, 4> rgba {};
    ToneCurveSamplingBasis sampledBasis = toneCurve->GetSamplingBasis();
    bool sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    if (!sampled && node->kind!=EditorNodeGraph::NodeKind::RawOperation && sampledBasis == ToneCurveSamplingBasis::CurveInput) {
        sampledBasis = ToneCurveSamplingBasis::FinalPreview;
        sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    }
    if (!sampled) {
        toneCurve->ClearViewportProbe();
        if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
        }
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        return;
    }

    toneCurve->UpdateViewportProbe(sampledBasis, u, v, rgba);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
    }
    m_LastToneCurveProbeNodeId = toneCurveNodeId;
}

void EditorModule::BeginToneCurveViewportTargetDrag(float u, float v) {
    if (const auto* selected=SelectedEditingNode()) if (auto* canonical=ResolveCanonicalToneEditor(selected->id)) {
        std::array<float,4> rgba{};const auto basis=canonical->GetSamplingBasis();
        if (SampleToneCurveViewportPixel(selected->id,basis,u,v,rgba) && canonical->BeginViewportTargetDrag(basis,u,v,rgba))
            CommitCanonicalToneEditor(selected->id,*canonical,true);
        return;
    }
    const int toneCurveNodeId = m_CanvasToolKind == CanvasToolKind::ToneCurveTarget
        ? m_CanvasToolOwnerNodeId
        : ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        return;
    }

    EditorNodeGraph::Node* node = GetNodeGraph().FindNode(toneCurveNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = ResolveCanonicalToneEditor(node->id);
    if (!toneCurve && node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    std::array<float, 4> rgba {};
    ToneCurveSamplingBasis sampledBasis = toneCurve->GetSamplingBasis();
    bool sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    if (!sampled && node->kind!=EditorNodeGraph::NodeKind::RawOperation && sampledBasis == ToneCurveSamplingBasis::CurveInput) {
        sampledBasis = ToneCurveSamplingBasis::FinalPreview;
        sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    }
    if (!sampled) {
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }
    if (toneCurve->BeginViewportTargetDrag(sampledBasis, u, v, rgba)) {
        if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            node->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();
            StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
        }
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        if (node->kind==EditorNodeGraph::NodeKind::RawOperation) CommitCanonicalToneEditor(toneCurveNodeId,*toneCurve,true);
        else MarkGraphEdited(toneCurveNodeId);
    }
}

void EditorModule::UpdateToneCurveViewportTargetDrag(float deltaCurveY) {
    if (const auto* selected=SelectedEditingNode()) if (auto* canonical=ResolveCanonicalToneEditor(selected->id)) {
        canonical->UpdateViewportTargetDrag(deltaCurveY);CommitCanonicalToneEditor(selected->id,*canonical,true);return;
    }
    if (m_CanvasToolKind != CanvasToolKind::ToneCurveTarget || m_CanvasToolOwnerNodeId <= 0) {
        return;
    }
    EditorNodeGraph::Node* node = GetNodeGraph().FindNode(m_CanvasToolOwnerNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = ResolveCanonicalToneEditor(node->id);
    if (!toneCurve && node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(m_CanvasToolOwnerNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    toneCurve->UpdateViewportTargetDrag(deltaCurveY);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        node->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();
        StoreIntegratedToneTransientState(m_CanvasToolOwnerNodeId, *toneCurve);
    }
    if (node->kind==EditorNodeGraph::NodeKind::RawOperation) CommitCanonicalToneEditor(m_CanvasToolOwnerNodeId,*toneCurve,true);
    else MarkGraphEdited(m_CanvasToolOwnerNodeId);
}

void EditorModule::EndToneCurveViewportTargetDrag() {
    if (const auto* selected=SelectedEditingNode()) if (auto* canonical=ResolveCanonicalToneEditor(selected->id)) {
        canonical->EndViewportTargetDrag();CommitCanonicalToneEditor(selected->id,*canonical,false);return;
    }
    const int toneCurveNodeId = m_CanvasToolKind == CanvasToolKind::ToneCurveTarget
        ? m_CanvasToolOwnerNodeId
        : m_LastToneCurveProbeNodeId;
    if (toneCurveNodeId <= 0) {
        return;
    }

    EditorNodeGraph::Node* node = GetNodeGraph().FindNode(toneCurveNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = ResolveCanonicalToneEditor(node->id);
    if (!toneCurve && node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(GetLayers().size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    toneCurve->EndViewportTargetDrag();
    if (node->kind==EditorNodeGraph::NodeKind::RawOperation) CommitCanonicalToneEditor(toneCurveNodeId,*toneCurve,false);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
    }
}
