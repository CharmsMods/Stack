#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphEditorCommands.h"

#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Graph/GraphDocumentRules.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

using Stack::Editor::ApplyGraphCommand;
using Stack::Editor::AddGraphNode;

namespace {


void SetGraphMutationErrorNoThrow(
    std::string* error,
    std::string_view message) noexcept {
    if (!error) {
        return;
    }
    try {
        error->assign(message.data(), message.size());
    } catch (...) {
        error->clear();
    }
}

std::shared_ptr<LayerBase> CloneLayerInstance(const std::shared_ptr<LayerBase>& source) {
    if (!source) {
        return nullptr;
    }
    const nlohmann::json layerJson = source->Serialize();
    const std::string typeId = layerJson.value("type", std::string());
    std::shared_ptr<LayerBase> clone = LayerRegistry::CreateLayerFromTypeId(typeId);
    if (!clone) {
        return nullptr;
    }
    clone->InitializeGL();
    clone->Deserialize(layerJson);
    clone->SetVisible(source->IsVisible());
    return clone;
}

struct GraphReconnectPlan {
    int fromNodeId = 0;
    std::string fromSocketId;
    int toNodeId = 0;
    std::string toSocketId;
};

std::optional<GraphReconnectPlan> BuildReconnectSourcePlan(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
    if (!definition) return std::nullopt;
    std::optional<GraphReconnectPlan> selected;
    for (const auto& binding : definition->bypassBindings) {
        const auto* input = graph.FindAnyInputLink(node.id, binding.second);
        if (!input) continue;
        if (selected) return std::nullopt;
        selected = GraphReconnectPlan{input->fromNodeId, input->fromSocketId, 0, binding.first};
    }
    return selected;
}

std::vector<GraphReconnectPlan> BuildReconnectPlansForNodeRemoval(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) {
    const std::optional<GraphReconnectPlan> sourcePlan = BuildReconnectSourcePlan(graph, node);
    if (!sourcePlan.has_value()) {
        return {};
    }

    std::vector<GraphReconnectPlan> plans;
    graph.ForEachOutgoingLink(
        node.id,
        [&](const EditorNodeGraph::Link& link) {
        if (link.fromSocketId != sourcePlan->toSocketId) {
            return;
        }
        if (sourcePlan->fromNodeId == link.toNodeId) {
            return;
        }
        plans.push_back(GraphReconnectPlan{
            sourcePlan->fromNodeId,
            sourcePlan->fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    });
    return plans;
}

bool ConnectionUsesImageAsRenderSource(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to || from->kind != EditorNodeGraph::NodeKind::Image ||
        fromSocketId != EditorNodeGraph::kImageOutputSocketId) {
        return false;
    }

    if (to->kind == EditorNodeGraph::NodeKind::Layer && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::RawDetailFusion && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::HdrMerge &&
        toSocketId == EditorNodeGraph::kHdrMergeInput1SocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Mfsr &&
        toSocketId == EditorNodeGraph::kMfsrReferenceInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Output && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Mix &&
        (toSocketId == EditorNodeGraph::kMixInputASocketId ||
         toSocketId == EditorNodeGraph::kMixInputBSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::DataMath &&
        (EditorNodeGraph::IsDataMathInputSocketId(toSocketId) ||
         toSocketId == EditorNodeGraph::kDataMathBaseInputSocketId)) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::ImageToMask &&
        toSocketId == EditorNodeGraph::kImageToMaskInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::ChannelSplit &&
        toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    return false;
}

} // namespace

void EditorModule::AddLayer(LayerType type) {
    AddLayerNodeAt(type,{260.f*static_cast<float>(GetLayers().size()+1),0.f});
}

void EditorModule::AddLayerNodeAt(LayerType type, EditorNodeGraph::Vec2 graphPosition) {
    auto layer = LayerRegistry::CreateLayer(type);
    if (!layer) return;
    const int layerIndex = static_cast<int>(GetLayers().size());
    int count = 0;
    auto settings = nlohmann::json::array();
    for (const auto& existing : GetLayers()) {
        settings.push_back(existing->Serialize());
        if (std::strcmp(existing->GetDefaultName(),layer->GetDefaultName()) == 0) ++count;
    }
    if (count > 0) layer->SetInstanceName(std::string(layer->GetDefaultName())+" ("+std::to_string(count+1)+")");
    settings.push_back(layer->Serialize());
    auto context = GetGraphEditorContext();
    int nodeId = 0;
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& graph) {
        auto* node = graph.AddLayerNode(type,layerIndex,graphPosition);
        if (!node) throw std::runtime_error("The operation could not be created.");
        nodeId = node->id;
        node->title = layer->GetName();
        graph.SelectNode(nodeId);
    });
    std::string error;
    if (!context.applyDocumentEdit(std::move(proposal),std::move(settings),GetGraphAnimation(),error)) {
        PostNotification(UiNotificationSeverity::Error,error,"graph-add-operation");
        return;
    }
    SelectGraphNode(nodeId);
    m_FocusSelectedTabNextRender = true;
}

void EditorModule::RemoveLayer(int index) {
    const auto* node = GetNodeGraph().FindNodeByLayerIndex(index);
    if (node) RemoveGraphNode(node->id,true);
}

void EditorModule::MoveLayer(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= static_cast<int>(GetLayers().size())) return;
    if (to < 0 || to >= static_cast<int>(GetLayers().size())) return;

    // TODO: Promote this to an undoable editor command when command history lands.
    if (from < to) {
        std::rotate(GetLayers().begin() + from, GetLayers().begin() + from + 1, GetLayers().begin() + to + 1);
    } else {
        std::rotate(GetLayers().begin() + to, GetLayers().begin() + from, GetLayers().begin() + from + 1);
    }

    if (m_SelectedLayerIndex == from) {
        m_SelectedLayerIndex = to;
    } else if (from < m_SelectedLayerIndex && to >= m_SelectedLayerIndex) {
        m_SelectedLayerIndex--;
    } else if (from > m_SelectedLayerIndex && to <= m_SelectedLayerIndex) {
        m_SelectedLayerIndex++;
    }

    RefreshGraphLayerMetadata();
    MarkGraphEdited();
}

void EditorModule::SetLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(GetLayers().size())) return;
    GetLayers()[index]->SetVisible(visible);
    MarkGraphEdited();
}

void EditorModule::SelectLayer(int index) {
    if (index < -1 || index >= static_cast<int>(GetLayers().size())) {
        return;
    }

    m_SelectedLayerIndex = index;
}

void EditorModule::SelectGraphNode(int nodeId) {
    GetNodeGraph().SelectNode(nodeId);
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectLayer(node->layerIndex);
    } else {
        SelectLayer(-1);
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::Output) {
        m_CompositeSelectedOutputNodeId = nodeId;
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::RawOperation && IsEditingRawLayerMaskGraph()) {
        const auto kind = node->rawOperation.kind;
        m_Project->rawOperationSelection[m_RawLayerMaskWorkspace->layerId + "/" + Stack::RawRecipe::GraphOperationId(kind)] = node->instanceUuid;
        using Kind = Stack::RawRecipe::GraphOperationKind;
        switch (kind) {
            case Kind::Calibration: m_RawWorkspaceLabUi.activeTool = RawLabTool::Calibration; break;
            case Kind::Exposure: m_RawWorkspaceLabUi.activeTool = RawLabTool::Exposure; break;
            case Kind::LocalEv: m_RawWorkspaceLabUi.activeTool = RawLabTool::Zones; break;
            case Kind::LuminanceTone: m_RawWorkspaceLabUi.activeTool = RawLabTool::Tone; m_RawWorkspaceLabUi.sceneToneView = 0; break;
            case Kind::RgbCurves: m_RawWorkspaceLabUi.activeTool = RawLabTool::Tone; m_RawWorkspaceLabUi.sceneToneView = 2; break;
            case Kind::ColorWarp: m_RawWorkspaceLabUi.activeTool = RawLabTool::Color; break;
            case Kind::DetailContrast: m_RawWorkspaceLabUi.activeTool = RawLabTool::Detail; break;
            default: break;
        }
        ClearRawWorkspaceGraphScopeReadbackCaches();
    }

}

bool EditorModule::LayerUsesRichNodeSurface(int layerIndex) const {
    (void)layerIndex;
    // Rich expanded layer surfaces remain available in the sidebar complex editor,
    // but they no longer expand inline on the graph canvas.
    return false;
}

bool EditorModule::NodeUsesSidebarOnlyComplexEditor(int nodeId) const {
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (!node) {
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::Lut ||
        node->kind == EditorNodeGraph::NodeKind::CustomMask) {
        return true;
    }

    if (node->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }

    return GetLayerNodeSurfaceSpec(node->layerIndex).presentation == NodeSurfacePresentation::RichExpandedSurface;
}

bool EditorModule::NodeHasDedicatedComplexEditor(int nodeId) const {
    if (NodeUsesSidebarOnlyComplexEditor(nodeId)) {
        return true;
    }

    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (!node) {
        return false;
    }

    switch (node->kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
            return true;
        default:
            return false;
    }
}

NodeSurfaceSpec EditorModule::GetLayerNodeSurfaceSpec(int layerIndex) const {
    if (layerIndex < 0 || layerIndex >= static_cast<int>(GetLayers().size()) || !GetLayers()[layerIndex]) {
        return {};
    }
    return GetLayers()[layerIndex]->GetNodeSurfaceSpec();
}

NodeSurfaceSpec EditorModule::GetNodeSurfaceSpec(int nodeId) const {
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (!node) {
        return {};
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawSource) {
        NodeSurfaceSpec spec;
        spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
        spec.density = NodeSurfaceDensity::Dense;
        spec.preferredWidth = 420.0f;
        spec.maxWidth = 520.0f;
        return spec;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
        node->kind == EditorNodeGraph::NodeKind::RawDecode ||
        node->kind == EditorNodeGraph::NodeKind::RawDevelop ||
        node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask ||
        node->kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
        node->kind == EditorNodeGraph::NodeKind::HdrMerge ||
        node->kind == EditorNodeGraph::NodeKind::Mfsr ||
        node->kind == EditorNodeGraph::NodeKind::Lut) {
        NodeSurfaceSpec spec;
        spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
        spec.density = NodeSurfaceDensity::Dense;
        spec.preferredWidth = 420.0f;
        spec.maxWidth = 520.0f;
        return spec;
    }
    if (node->kind != EditorNodeGraph::NodeKind::Layer) {
        return {};
    }
    return GetLayerNodeSurfaceSpec(node->layerIndex);
}

void EditorModule::BeginCanvasColorPick(
    int ownerNodeId,
    const std::string& statusText,
    std::function<void(float, float, float)> callback) {
    m_CanvasToolKind = CanvasToolKind::PickColor;
    m_CanvasToolOwnerNodeId = ownerNodeId;
    m_CanvasToolStatusText = statusText.empty() ? "Click canvas to sample color" : statusText;
    m_IsPickingColor = true;
    m_CanvasColorPickSamplesNodeInput = false;
    m_ColorPickerCallback = std::move(callback);
}

void EditorModule::BeginCanvasColorPickFromNodeInput(
    int ownerNodeId,
    const std::string& statusText,
    std::function<void(float, float, float)> callback) {
    BeginCanvasColorPick(ownerNodeId, statusText, std::move(callback));
    m_CanvasColorPickSamplesNodeInput = true;
}

void EditorModule::BeginToneCurveTargeting(int ownerNodeId, const std::string& statusText) {
    m_CanvasToolKind = CanvasToolKind::ToneCurveTarget;
    m_CanvasToolOwnerNodeId = ownerNodeId;
    m_CanvasToolStatusText = statusText.empty()
        ? "Click and drag in the main viewport to adjust the sampled tone"
        : statusText;
    m_IsPickingColor = false;
    m_ColorPickerCallback = nullptr;
}

void EditorModule::CancelCanvasTool() {
    if (m_CanvasToolKind == CanvasToolKind::ToneCurveTarget) {
        EndToneCurveViewportTargetDrag();
        ClearTrackedToneCurveProbe();
    }
    m_CanvasToolKind = CanvasToolKind::None;
    m_CanvasToolOwnerNodeId = -1;
    m_CanvasToolStatusText.clear();
    m_IsPickingColor = false;
    m_CanvasColorPickSamplesNodeInput = false;
    m_ColorPickerCallback = nullptr;
}

bool EditorModule::SampleCanvasColorPickPixel(float u, float v, std::array<float, 4>& outRgba) const {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (!m_CanvasColorPickSamplesNodeInput || m_CanvasToolOwnerNodeId <= 0 || !CanRefreshPreviewLikeNodes()) {
        return false;
    }

    const EditorNodeGraph::Link* input =
        GetNodeGraph().FindInputLink(m_CanvasToolOwnerNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (IsEditingRawLayerMaskGraph() ||
        !TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Raw and generated streams do not always retain a serializable image
        // source. The active pipeline source is still the correct graph seed;
        // the probe below evaluates the connected upstream chain from there.
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    int sourceNodeId = input->fromNodeId;
    if (IsEditingRawLayerMaskGraph()) {
        const auto owner = snapshot.rawLayerMaskNodeIds.find(m_RawLayerMaskWorkspace->layerId);
        if (owner == snapshot.rawLayerMaskNodeIds.end()) return false;
        const auto runtime = owner->second.find(sourceNodeId);
        if (runtime == owner->second.end()) return false;
        sourceNodeId = runtime->second;
    }
    const int syntheticOutputId = -360000 - m_CanvasToolOwnerNodeId;
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
    return probePipeline.SampleOutputPixel(u, v, outRgba);
}

void EditorModule::RestoreIntegratedToneTransientState(int ownerNodeId, ToneCurveLayer& toneCurve) const {
    const auto it = m_IntegratedToneViewportInteractionCache.find(ownerNodeId);
    if (it == m_IntegratedToneViewportInteractionCache.end()) {
        toneCurve.RestoreViewportInteractionState(ToneCurveLayer::ViewportInteractionState{});
        return;
    }

    ToneCurveLayer::ViewportInteractionState state;
    state.probeValid = it->second.probeValid;
    state.probeSamplingBasis = static_cast<ToneCurveSamplingBasis>(std::clamp(it->second.probeSamplingBasis, 0, 1));
    state.probeU = it->second.probeU;
    state.probeV = it->second.probeV;
    state.probeRgba = it->second.probeRgba;
    state.selectionSeedValid = it->second.selectionSeedValid;
    state.selectionSeedU = it->second.selectionSeedU;
    state.selectionSeedV = it->second.selectionSeedV;
    state.selectionSeedInputX = it->second.selectionSeedInputX;
    state.selectionSeedSceneValue = it->second.selectionSeedSceneValue;
    state.selectionSeedRgba = it->second.selectionSeedRgba;
    state.onImageDragPointIndex = it->second.onImageDragPointIndex;
    state.onImageDragAnchorInputX = it->second.onImageDragAnchorInputX;
    state.onImageDragAnchorOutputY = it->second.onImageDragAnchorOutputY;
    toneCurve.RestoreViewportInteractionState(state);
}

void EditorModule::StoreIntegratedToneTransientState(int ownerNodeId, const ToneCurveLayer& toneCurve) const {
    ToneCurveViewportInteractionCache cache;
    const ToneCurveLayer::ViewportInteractionState state = toneCurve.CaptureViewportInteractionState();
    cache.probeValid = state.probeValid;
    cache.probeSamplingBasis = static_cast<int>(state.probeSamplingBasis);
    cache.probeU = state.probeU;
    cache.probeV = state.probeV;
    cache.probeRgba = state.probeRgba;
    cache.selectionSeedValid = state.selectionSeedValid;
    cache.selectionSeedU = state.selectionSeedU;
    cache.selectionSeedV = state.selectionSeedV;
    cache.selectionSeedInputX = state.selectionSeedInputX;
    cache.selectionSeedSceneValue = state.selectionSeedSceneValue;
    cache.selectionSeedRgba = state.selectionSeedRgba;
    cache.onImageDragPointIndex = state.onImageDragPointIndex;
    cache.onImageDragAnchorInputX = state.onImageDragAnchorInputX;
    cache.onImageDragAnchorOutputY = state.onImageDragAnchorOutputY;
    m_IntegratedToneViewportInteractionCache[ownerNodeId] = cache;
}

void EditorModule::ClearIntegratedToneTransientState(int ownerNodeId) const {
    m_IntegratedToneViewportInteractionCache.erase(ownerNodeId);
}

void EditorModule::OnCanvasColorPicked(float r, float g, float b) {
    if (m_ColorPickerCallback) {
        m_ColorPickerCallback(r, g, b);
    }
    CancelCanvasTool();
}

bool EditorModule::SelectAdjacentMainChainNode(int direction) {
    if (direction == 0) {
        return false;
    }

    const int selectedNodeId = GetNodeGraph().GetSelectedNodeId();
    if (selectedNodeId <= 0) {
        return false;
    }

    const int adjacentNodeId = GetNodeGraph().FindAdjacentMainChainNodeId(selectedNodeId, direction);
    if (adjacentNodeId <= 0 || adjacentNodeId == selectedNodeId) {
        return false;
    }

    SelectGraphNode(adjacentNodeId);
    return true;
}

void EditorModule::ApplyGraphLayerOrder() {
    const std::vector<int> order = GetNodeGraph().GetRenderLayerIndexPath();
    if (order.empty()) {
        return;
    }

    std::vector<int> uniqueOrder;
    for (int index : order) {
        if (index >= 0 && index < static_cast<int>(GetLayers().size()) &&
            std::find(uniqueOrder.begin(), uniqueOrder.end(), index) == uniqueOrder.end()) {
            uniqueOrder.push_back(index);
        }
    }
    if (uniqueOrder.size() != GetLayers().size()) {
        return;
    }

    std::vector<std::shared_ptr<LayerBase>> reordered;
    reordered.reserve(GetLayers().size());
    for (int index : uniqueOrder) {
        reordered.push_back(GetLayers()[index]);
    }
    GetLayers() = std::move(reordered);

    std::vector<int> remap(uniqueOrder.size());
    for (int i = 0; i < static_cast<int>(uniqueOrder.size()); ++i) remap[uniqueOrder[i]] = i;
    for (auto& node : GetNodeGraph().EditNodes())
        if (node.kind == EditorNodeGraph::NodeKind::Layer && node.layerIndex >= 0 && node.layerIndex < static_cast<int>(remap.size()))
            node.layerIndex = remap[node.layerIndex];
    RefreshGraphLayerMetadata();
}

bool EditorModule::SplitLayerNodeIntoChannels(int layerNodeId) {
    const auto& graph = GetNodeGraph();
    const auto* original = graph.FindNode(layerNodeId);
    if (!original || original->kind != EditorNodeGraph::NodeKind::Layer || original->layerIndex < 0 ||
        original->layerIndex >= static_cast<int>(GetLayers().size())) return false;
    const auto* source = graph.FindInputLink(layerNodeId, "imageIn");
    if (!source) return false;
    const auto sourceLink = *source;
    const auto originalNode = *original;
    auto settings = nlohmann::json::array();
    for (const auto& layer : GetLayers()) settings.push_back(layer->Serialize());
    const auto originalSettings = settings[originalNode.layerIndex];
    const auto originalAnimation = GetGraphAnimation();
    auto animation = originalAnimation;
    animation.tracks.erase(std::remove_if(animation.tracks.begin(),animation.tracks.end(),[&](const auto& track) {
        return track.target.nodeUuid == originalNode.instanceUuid;
    }),animation.tracks.end());
    int combineId = 0;
    auto proposal = Stack::GraphModel::ProposeEdit(graph,graph.GetStructureRevision(),[&](auto& candidate) {
        std::vector<EditorNodeGraph::Link> outputs, secondaryInputs;
        for (const auto& link : graph.GetLinks()) {
            if (link.fromNodeId == layerNodeId) outputs.push_back(link);
            if (link.toNodeId == layerNodeId && link.toSocketId != "imageIn") secondaryInputs.push_back(link);
        }
        if (outputs.empty()) throw std::runtime_error("Connect an output before splitting this operation.");
        for (int id : graph.GetDownstreamRenderNodeIds(layerNodeId))
            if (id != layerNodeId) if (auto* node = candidate.FindNode(id)) node->position.x += 620;
        const auto position = originalNode.position;
        const int splitId = candidate.AddChannelSplitNode({position.x-250,position.y})->id;
        combineId = candidate.AddChannelCombineNode({position.x+370,position.y})->id;
        std::string error;
        const auto connect = [&](int from,const std::string& output,int to,const std::string& input) {
            if (!candidate.TryConnectSockets(from,output,to,input,&error)) throw std::runtime_error(error);
        };
        connect(sourceLink.fromNodeId,sourceLink.fromSocketId,splitId,"imageIn");
        const char* channels[]{"r","g","b","a"};
        for (int i=0;i<4;++i) {
            auto* node = candidate.AddLayerNode(originalNode.layerType,static_cast<int>(settings.size()),
                {position.x+60,position.y-240+160.f*i});
            const auto cloneId = node->id;
            const auto cloneUuid = node->instanceUuid;
            node->typeId = originalNode.typeId;
            node->exposedParameterIds = originalNode.exposedParameterIds;
            settings.push_back(originalSettings);
            connect(splitId,channels[i],cloneId,"imageIn");
            connect(cloneId,"imageOut",combineId,channels[i]);
            for (const auto& link : secondaryInputs) connect(link.fromNodeId,link.fromSocketId,cloneId,link.toSocketId);
            for (const auto& track : originalAnimation.tracks) if (track.target.nodeUuid == originalNode.instanceUuid) {
                auto clone = track; clone.target.nodeId = cloneId; clone.target.nodeUuid = cloneUuid;
                animation.tracks.push_back(std::move(clone));
            }
        }
        for (const auto& link : outputs) connect(combineId,"imageOut",link.toNodeId,link.toSocketId);
        candidate.RemoveLayerNode(originalNode.layerIndex);
        settings.erase(settings.begin()+originalNode.layerIndex);
        candidate.SelectNode(combineId);
    });
    std::string error;
    if (!GetGraphEditorContext().applyDocumentEdit(std::move(proposal),std::move(settings),std::move(animation),error)) {
        PostNotification(UiNotificationSeverity::Error,error,"graph-layer-split");
        return false;
    }
    ClearGraphAutoFocusIfTrackedNode(layerNodeId);
    if (m_CanvasToolOwnerNodeId == layerNodeId) CancelCanvasTool();
    SelectGraphNode(combineId);
    return true;
}

bool EditorModule::SplitImageAverageNodeIntoChannelAverages(int dataMathNodeId) {
    const auto& graph = GetNodeGraph();
    const auto* original = graph.FindNode(dataMathNodeId);
    if (!original || original->kind != EditorNodeGraph::NodeKind::DataMath ||
        original->dataMathMode != EditorNodeGraph::DataMathMode::ImageAverage) return false;
    const auto position = original->position;
    std::vector<EditorNodeGraph::Link> inputs, outputs;
    for (int i=0;i<EditorNodeGraph::kMaxDataMathInputCount;++i) {
        const auto* link = graph.FindInputLink(dataMathNodeId,EditorNodeGraph::DataMathInputSocketId(i));
        if (!link) continue;
        if (graph.IsScalarSocketStream(link->fromNodeId,link->fromSocketId)) return false;
        inputs.push_back(*link);
    }
    if (inputs.size()<2) return false;
    for (const auto& link : graph.GetLinks())
        if (link.fromNodeId==dataMathNodeId && link.fromSocketId=="imageOut") outputs.push_back(link);
    int combineId=0;
    auto proposal=Stack::GraphModel::ProposeEdit(graph,graph.GetStructureRevision(),[&](auto& candidate) {
        std::string error;
        const auto connect=[&](int from,const std::string& out,int to,const std::string& in) {
            if (!candidate.TryConnectSockets(from,out,to,in,&error)) throw std::runtime_error(error);
        };
        for (int id : graph.GetDownstreamRenderNodeIds(dataMathNodeId))
            if (id!=dataMathNodeId) if (auto* node=candidate.FindNode(id)) node->position.x+=520;
        std::vector<int> splits;
        for (std::size_t i=0;i<inputs.size();++i) {
            const int id=candidate.AddChannelSplitNode({position.x-360,position.y-float(inputs.size()-1)*84+float(i)*168})->id;
            splits.push_back(id);
            connect(inputs[i].fromNodeId,inputs[i].fromSocketId,id,"imageIn");
        }
        combineId=candidate.AddChannelCombineNode({position.x+360,position.y})->id;
        const char* channels[]{"r","g","b","a"};
        for (int channel=0;channel<4;++channel) {
            const int id=candidate.AddDataMathNode(EditorNodeGraph::DataMathMode::Average,
                {position.x,position.y-210+float(channel)*140})->id;
            for (std::size_t i=0;i<splits.size();++i)
                connect(splits[i],channels[channel],id,EditorNodeGraph::DataMathInputSocketId(static_cast<int>(i)));
            connect(id,"imageOut",combineId,channels[channel]);
        }
        for (const auto& link:outputs) connect(combineId,"imageOut",link.toNodeId,link.toSocketId);
        if (!candidate.RemoveNode(dataMathNodeId)) throw std::runtime_error("The original average could not be replaced.");
        candidate.SelectNode(combineId);
    });
    std::string error;
    if (!GetGraphEditorContext().applyEdit(std::move(proposal),error)) {
        PostNotification(UiNotificationSeverity::Error,error,"graph-average-split");
        return false;
    }
    SelectGraphNode(combineId);
    return true;
}
bool EditorModule::ToggleOutputNodeEnabled(int outputNodeId) {
    EditorNodeGraph::Node* outputNode = GetNodeGraph().FindNode(outputNodeId);
    if (!outputNode || outputNode->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }

    const bool enabled = !outputNode->outputEnabled;
    std::string error;
    if (!ApplyGraphCommand(*this,[&](auto& graph) {
            if (!graph.SetOutputNodeEnabled(outputNodeId,enabled)) throw std::runtime_error("The output could not be changed.");
            EditorNodeGraphDefinitions::ApplyNodeMetadata(*graph.FindNode(outputNodeId));
        },&error)) {
        PostNotification(UiNotificationSeverity::Error,error,"graph-output-enabled");
        return false;
    }
    if (IsEditingRawLayerMaskGraph()) return true;
    outputNode = GetNodeGraph().FindNode(outputNodeId);
    if (!outputNode) {
        return false;
    }
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*outputNode);

    if (!outputNode->outputEnabled && m_CompositeSelectedOutputNodeId == outputNodeId) {
        int replacementOutputNodeId = GetNodeGraph().ResolvePreviewOutputNodeId();
        if (replacementOutputNodeId == outputNodeId) {
            replacementOutputNodeId = -1;
        }
        m_CompositeSelectedOutputNodeId = replacementOutputNodeId;
    }
    if (!outputNode->outputEnabled) {
        if (CompositeSceneItem* item = FindCompositeSceneItem(outputNodeId)) {
            if (item->texture != 0) {
                glDeleteTextures(1, &item->texture);
                item->texture = 0;
            }
        }
        m_CompositeSceneItems.erase(
            std::remove_if(
                m_CompositeSceneItems.begin(),
                m_CompositeSceneItems.end(),
                [outputNodeId](const CompositeSceneItem& item) { return item.outputNodeId == outputNodeId; }),
            m_CompositeSceneItems.end());
        m_CompositeZOrder.erase(
            std::remove(m_CompositeZOrder.begin(), m_CompositeZOrder.end(), outputNodeId),
            m_CompositeZOrder.end());
        m_CompositeOutputDirtyGenerations.erase(outputNodeId);
        m_CompositeOutputRequestedGenerations.erase(outputNodeId);
        m_CompositeOutputCompletedGenerations.erase(outputNodeId);
    }
    if (!IsEditingRawLayerMaskGraph() && !GetNodeGraph().IsOutputConnected()) {
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    return true;
}

bool EditorModule::ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = GetNodeGraph().FindNode(fromNodeId);
    const std::string fromSocket = from ? GetNodeGraph().DefaultOutputSocket(*from) : std::string();
    const EditorNodeGraph::Node* pendingTo = GetNodeGraph().FindNode(toNodeId);
    const std::string toSocket = pendingTo ? GetNodeGraph().DefaultInputSocket(*pendingTo) : std::string();
    return ConnectGraphSockets(fromNodeId, fromSocket, toNodeId, toSocket, errorMessage);
}

int EditorModule::FindDirectDownstreamToneCurveNode(int sourceNodeId) const {
    for (const EditorNodeGraph::Link& link : GetNodeGraph().GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            GetNodeGraph().GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        const EditorNodeGraph::Node* downstream = GetNodeGraph().FindNode(link.toNodeId);
        if (downstream &&
            downstream->kind == EditorNodeGraph::NodeKind::Layer &&
            downstream->layerType == LayerType::ToneCurve) {
            return downstream->id;
        }
    }
    return -1;
}

int EditorModule::FindNearestDownstreamToneCurveNode(int sourceNodeId) const {
    int currentNodeId = sourceNodeId;
    const std::size_t maxHops = GetNodeGraph().GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        currentNodeId = GetNodeGraph().FindAdjacentMainChainNodeId(currentNodeId, 1);
        if (currentNodeId <= 0) {
            return -1;
        }
        const EditorNodeGraph::Node* currentNode = GetNodeGraph().FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::Layer &&
            currentNode->layerType == LayerType::ToneCurve) {
            return currentNode->id;
        }
    }
    return -1;
}

int EditorModule::FindNearestUpstreamRawDevelopNode(int sourceNodeId) const {
    int currentNodeId = sourceNodeId;
    const std::size_t maxHops = GetNodeGraph().GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        const EditorNodeGraph::Node* currentNode = GetNodeGraph().FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            return currentNode->id;
        }
        currentNodeId = GetNodeGraph().FindAdjacentMainChainNodeId(currentNodeId, -1);
    }
    return -1;
}

bool EditorModule::RawDevelopNodeUsesIntegratedTone(int nodeId) const {
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    return node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled;
}

bool EditorModule::CanAbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId, std::string* reason) const {
    const EditorNodeGraph::Node* sourceNode = GetNodeGraph().FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        if (reason) {
            *reason = "No Develop node was found for this merge action.";
        }
        return false;
    }

    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);
    if (directToneNodeId <= 0) {
        if (reason) {
            *reason = "No direct downstream Tone Curve is connected to this Develop node.";
        }
        return false;
    }

    const EditorNodeGraph::Node* toneNode = GetNodeGraph().FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        if (reason) {
            *reason = "The direct downstream node is not a Tone Curve layer.";
        }
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        GetNodeGraph().FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        GetNodeGraph().FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);
    if (toneMaskLink && developMaskLink &&
        (toneMaskLink->fromNodeId != developMaskLink->fromNodeId ||
         toneMaskLink->fromSocketId != developMaskLink->fromSocketId)) {
        if (reason) {
            *reason = "Develop already has a different finish mask connected, so this legacy Tone Curve cannot be absorbed automatically.";
        }
        return false;
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

bool EditorModule::SelectOrCreateToneFinishAfterNode(int sourceNodeId) {
    const EditorNodeGraph::Node* sourceNode = GetNodeGraph().FindNode(sourceNodeId);
    if (!sourceNode) {
        return false;
    }

    if (const int existingToneNodeId = FindNearestDownstreamToneCurveNode(sourceNodeId);
        existingToneNodeId > 0) {
        SelectGraphNode(existingToneNodeId);
        return true;
    }

    std::vector<EditorNodeGraph::Link> downstreamLinks;
    EditorNodeGraph::Vec2 tonePosition{ sourceNode->position.x + 280.0f, sourceNode->position.y };
    for (const EditorNodeGraph::Link& link : GetNodeGraph().GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            GetNodeGraph().GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        downstreamLinks.push_back(link);
    }

    if (!downstreamLinks.empty()) {
        if (const EditorNodeGraph::Node* firstDownstream = GetNodeGraph().FindNode(downstreamLinks.front().toNodeId)) {
            tonePosition.x = (sourceNode->position.x + firstDownstream->position.x) * 0.5f;
            tonePosition.y = (sourceNode->position.y + firstDownstream->position.y) * 0.5f;
        }
    }

    AddLayerNodeAt(LayerType::ToneCurve, tonePosition);
    const int toneNodeId = GetNodeGraph().GetSelectedNodeId();
    if (toneNodeId <= 0) {
        PostNotification(
            UiNotificationSeverity::Error,
            "Could not create a downstream Tone Curve node.",
            "raw-develop-tone-finish-create");
        return false;
    }

    std::string errorMessage;
    if (!ConnectGraphSockets(
            sourceNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            toneNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &errorMessage)) {
        PostNotification(
            UiNotificationSeverity::Error,
            errorMessage.empty() ? "Could not connect Develop to the new Tone Curve node." : errorMessage,
            "raw-develop-tone-finish-connect");
        return false;
    }

    for (const EditorNodeGraph::Link& downstreamLink : downstreamLinks) {
        if (!ConnectGraphSockets(
                toneNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                downstreamLink.toNodeId,
                downstreamLink.toSocketId,
                &errorMessage)) {
            PostNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty() ? "Could not reconnect one of the downstream finish-tone links." : errorMessage,
                "raw-develop-tone-finish-rewire");
            SelectGraphNode(toneNodeId);
            return false;
        }
    }

    SelectGraphNode(toneNodeId);
    return true;
}

bool EditorModule::AbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId) {
    EditorNodeGraph::Node* sourceNode = GetNodeGraph().FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    std::string absorbReason;
    if (!CanAbsorbDirectDownstreamToneFinishIntoDevelop(sourceNodeId, &absorbReason)) {
        if (!absorbReason.empty()) {
            PostNotification(
                UiNotificationSeverity::Warning,
                absorbReason,
                "raw-develop-tone-finish-absorb-unsafe");
        }
        return false;
    }
    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);

    const EditorNodeGraph::Node* toneNode = GetNodeGraph().FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerIndex < 0 ||
        toneNode->layerIndex >= static_cast<int>(GetLayers().size()) ||
        !GetLayers()[toneNode->layerIndex]) {
        return false;
    }

    ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(GetLayers()[toneNode->layerIndex].get());
    if (!toneCurve) {
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        GetNodeGraph().FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        GetNodeGraph().FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);

    sourceNode->rawDevelop.integratedToneEnabled = true;
    sourceNode->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();

    if (toneMaskLink && !developMaskLink) {
        std::string errorMessage;
        if (!ConnectGraphSockets(
                toneMaskLink->fromNodeId,
                toneMaskLink->fromSocketId,
                sourceNodeId,
                EditorNodeGraph::kMaskInputSocketId,
                &errorMessage)) {
            PostNotification(
                UiNotificationSeverity::Error,
                errorMessage.empty()
                    ? "Could not transfer the legacy Tone Curve finish mask into Develop."
                    : errorMessage,
                "raw-develop-tone-finish-mask-transfer");
            return false;
        }
    }

    if (!RemoveGraphNode(directToneNodeId)) {
        return false;
    }

    SelectGraphNode(sourceNodeId);
    MarkGraphEdited(sourceNodeId);
    return true;
}

bool EditorModule::SelectUpstreamDevelopForToneNode(int toneNodeId) {
    const EditorNodeGraph::Node* toneNode = GetNodeGraph().FindNode(toneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        return false;
    }

    const int rawDevelopNodeId = FindNearestUpstreamRawDevelopNode(toneNodeId);
    if (rawDevelopNodeId <= 0) {
        return false;
    }

    SelectGraphNode(rawDevelopNodeId);
    return true;
}

bool EditorModule::ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    auto context = GetGraphEditorContext();
    bool connected = false;
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph, context.revision, [&](auto& candidate) {
        connected = candidate.TryConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, errorMessage);
    });
    if (!connected) return false;
    std::string error;
    if (!context.applyEdit(std::move(proposal), error)) {
        if (errorMessage) *errorMessage = std::move(error);
        return false;
    }
    if (!IsEditingRawLayerMaskGraph()) {
        const auto* source = GetNodeGraph().FindNode(fromNodeId);
        if (source && source->kind == EditorNodeGraph::NodeKind::Image && !source->image.pixels.empty() &&
            ConnectionUsesImageAsRenderSource(GetNodeGraph(), fromNodeId, fromSocketId, toNodeId, toSocketId)) {
            LoadSourceFromPixels(source->image.pixels.data(), source->image.width, source->image.height, source->image.channels);
            GetNodeGraph().SetActiveImageNodeId(fromNodeId);
            MarkNodeBrowserThumbnailSourceChanged();
        }
        ApplyGraphLayerOrder();
    }
    const auto* target = GetNodeGraph().FindNode(toNodeId);
    SelectGraphNode(target && (target->kind == EditorNodeGraph::NodeKind::Layer || target->kind == EditorNodeGraph::NodeKind::RawOperation)
        ? toNodeId : fromNodeId);
    return true;
}

bool EditorModule::OutputPathNeedsViewTransform(int outputNodeId) const {
    if (IsEditingRawLayerMaskGraph()) return false;
    const EditorNodeGraph::Node* output = GetNodeGraph().FindNode(outputNodeId);
    if (!output || output->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }
    if (GetNodeGraph().IsOutputChannelInspection(outputNodeId)) {
        return false;
    }
    const EditorNodeGraph::Link* input = GetNodeGraph().FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }
    const EditorNodeGraph::ScenePathInfo scenePath =
        EditorNodeGraph::AnalyzeScenePath(GetNodeGraph(), input->fromNodeId);
    return scenePath.sceneReferred && !scenePath.hasViewTransform;
}

bool EditorModule::SelectedLayerInputContainsViewTransform() const {
    if (m_SelectedLayerIndex < 0) {
        return false;
    }
    const EditorNodeGraph::Node* selectedNode = GetNodeGraph().FindNodeByLayerIndex(m_SelectedLayerIndex);
    if (!selectedNode || selectedNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }

    const EditorNodeGraph::Link* input = GetNodeGraph().FindInputLink(selectedNode->id, EditorNodeGraph::kImageInputSocketId);
    return input &&
        EditorNodeGraph::AnalyzeScenePath(GetNodeGraph(), input->fromNodeId)
            .hasViewTransform;
}

bool EditorModule::RenderLayerControlsWithDirtyTracking(
    EditorNodeGraph::Node& node,
    const std::function<void(LayerBase&)>& renderControls) {
    if (node.kind != EditorNodeGraph::NodeKind::Layer ||
        node.layerIndex < 0 ||
        node.layerIndex >= static_cast<int>(GetLayers().size()) ||
        !GetLayers()[node.layerIndex]) {
        return false;
    }

    LayerBase& layer = *GetLayers()[node.layerIndex];
    const nlohmann::json before = layer.Serialize();
    const bool beforeEnabled = layer.IsEnabled();
    const bool beforeVisible = layer.IsVisible();
    renderControls(layer);
    const nlohmann::json after = layer.Serialize();
    if (before != after ||
        beforeEnabled != layer.IsEnabled() ||
        beforeVisible != layer.IsVisible()) {
        if (before != after && !IsEditingRawLayerMaskGraph()) {
            UpdateTimelineExistingKeyframesForLayerEdit(node, before, after);
        }
        MarkGraphEdited(node.id);
        return true;
    }
    return false;
}

void EditorModule::MarkSelectedLayerRenderDirty() {
    if (m_SelectedLayerIndex >= 0) {
        if (const EditorNodeGraph::Node* node = GetNodeGraph().FindNodeByLayerIndex(m_SelectedLayerIndex)) {
            MarkGraphEdited(node->id);
            return;
        }
    }
    MarkGraphEdited();
}

bool EditorModule::RemoveGraphLink(int fromNodeId, int toNodeId) {
    const EditorNodeGraph::Node* from = GetNodeGraph().FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = GetNodeGraph().FindNode(toNodeId);
    const std::string fromSocketId = from ? GetNodeGraph().DefaultOutputSocket(*from) : std::string();
    const std::string toSocketId = to ? GetNodeGraph().DefaultInputSocket(*to) : std::string();
    for (const EditorNodeGraph::Link& link : GetNodeGraph().GetLinks()) {
        if (link.fromNodeId == fromNodeId && link.toNodeId == toNodeId &&
            link.ownership == EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
            PostNotification(
                UiNotificationSeverity::Warning,
                "MFD frame links are managed by the project. Exclude or remove the frame from RAW Lab instead.",
                "mfd-managed-link-protected");
            return false;
        }
    }

    auto context = GetGraphEditorContext();
    bool removed = false;
    auto proposal = Stack::GraphModel::ProposeEdit(GetNodeGraph(), context.revision,
        [&](auto& graph) { removed = graph.RemoveLink(fromNodeId, toNodeId); });
    std::string error;
    if (!removed || !context.applyEdit(std::move(proposal), error)) return false;
    if (removed) {
        if (!IsEditingRawLayerMaskGraph() && !GetNodeGraph().IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        }
    }
    return removed;
}

bool EditorModule::RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    for (const EditorNodeGraph::Link& link : GetNodeGraph().GetLinks()) {
        if (link.fromNodeId == fromNodeId && link.fromSocketId == fromSocketId &&
            link.toNodeId == toNodeId && link.toSocketId == toSocketId &&
            link.ownership == EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
            PostNotification(
                UiNotificationSeverity::Warning,
                "MFD frame links are managed by the project. Exclude or remove the frame from RAW Lab instead.",
                "mfd-managed-link-protected");
            return false;
        }
    }

    auto context = GetGraphEditorContext();
    bool removed = false;
    auto proposal = Stack::GraphModel::ProposeEdit(GetNodeGraph(), context.revision,
        [&](auto& graph) { removed = graph.RemoveLink(fromNodeId, fromSocketId, toNodeId, toSocketId); });
    std::string error;
    if (!removed || !context.applyEdit(std::move(proposal), error)) return false;
    if (removed) {
        if (!IsEditingRawLayerMaskGraph() && !GetNodeGraph().IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        }
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphLink() {
    const auto* selected = GetNodeGraph().GetSelectedLink();
    if (!selected) return false;
    const auto link = *selected;
    return RemoveGraphLink(link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId);
}

bool EditorModule::RemoveGraphNode(int nodeId, bool reconnect) {
    EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (!node) {
        return false;
    }

    if (node->role == Stack::GraphModel::NodeRole::OriginalImage ||
        node->role == Stack::GraphModel::NodeRole::CurrentImage ||
        node->role == Stack::GraphModel::NodeRole::LayerResult) return false;
    if (node->kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
        node->rawProjectSourceSet.managed) {
        m_PendingDeleteMultiFrameSourceSetId =
            node->rawProjectSourceSet.sourceSetId;
        m_OpenMultiFrameDeletePopup = true;
        return false;
    }
    if (node->kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
        node->multiFrameDenoise.managed) {
        m_PendingDeleteMultiFrameSourceSetId =
            node->multiFrameDenoise.sourceSetId;
        m_OpenMultiFrameDeletePopup = true;
        return false;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
        node->rawProjectFrame.managed) {
        m_PendingDeleteMultiFrameFrameSetId =
            node->rawProjectFrame.sourceSetId;
        m_PendingDeleteMultiFrameFrameId = node->rawProjectFrame.frameId;
        m_OpenMultiFrameFrameDeletePopup = true;
        return false;
    }

    if (!IsEditingRawLayerMaskGraph() && IsRawWorkspaceProjectActive() &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelopment && nodeId == ResolveRawWorkspaceStageOutputNodeId()) {
        const std::string& sourceKey =
            node->rawDevelopment.recipe.source.relativePathKey;
        if (sourceKey.empty() || sourceKey == m_Project->rawSourceKey) {
            PostNotification(
                UiNotificationSeverity::Warning,
                "The RAW Development node owns this RAW project and cannot be deleted. You can freely edit or replace nodes downstream from it.",
                "raw-workspace-owner-node-delete");
            return false;
        }
    }

    const auto reconnectPlans = reconnect ? BuildReconnectPlansForNodeRemoval(GetNodeGraph(), *node) : std::vector<GraphReconnectPlan>{};
    const int removedLayerIndex = node->kind == EditorNodeGraph::NodeKind::Layer ? node->layerIndex : -1;
    auto settings = nlohmann::json::array();
    for (const auto& layer : GetLayers()) settings.push_back(layer->Serialize());
    if (removedLayerIndex >= 0) settings.erase(settings.begin() + removedLayerIndex);
    auto context = GetGraphEditorContext();
    auto proposal = Stack::GraphModel::ProposeEdit(GetNodeGraph(), context.revision, [&](auto& graph) {
        if (removedLayerIndex >= 0) graph.RemoveLayerNode(removedLayerIndex);
        else graph.RemoveNode(nodeId);
        for (const auto& plan : reconnectPlans) {
            std::string error;
            if (!graph.TryConnectSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &error))
                throw std::runtime_error(error);
        }
    });
    if (!context.applyDocumentEdit(std::move(proposal), std::move(settings), GetGraphAnimation(), m_RawLayerStatus)) return false;
    ClearGraphAutoFocusIfTrackedNode(nodeId);
    if (m_CanvasToolOwnerNodeId == nodeId) CancelCanvasTool();
    return true;
}

bool EditorModule::DeleteSelectedGraphNodes() {
    std::vector<int> nodeIds = GetNodeGraph().GetSelectedNodeIds();
    if (nodeIds.empty()) {
        return false;
    }

    std::sort(nodeIds.begin(), nodeIds.end(), [this](int a, int b) {
        const EditorNodeGraph::Node* nodeA = GetNodeGraph().FindNode(a);
        const EditorNodeGraph::Node* nodeB = GetNodeGraph().FindNode(b);
        const int layerA = nodeA && nodeA->kind == EditorNodeGraph::NodeKind::Layer ? nodeA->layerIndex : -1;
        const int layerB = nodeB && nodeB->kind == EditorNodeGraph::NodeKind::Layer ? nodeB->layerIndex : -1;
        return layerA > layerB;
    });

    auto settings = nlohmann::json::array();
    for (const auto& layer : GetLayers()) settings.push_back(layer->Serialize());
    auto context = GetGraphEditorContext();
    bool removedAny = false;
    auto proposal = Stack::GraphModel::ProposeEdit(GetNodeGraph(), context.revision, [&](auto& graph) {
        for (const int id : nodeIds) {
            const auto* node = graph.FindNode(id);
            if (!node || node->role == Stack::GraphModel::NodeRole::OriginalImage ||
                node->role == Stack::GraphModel::NodeRole::CurrentImage || node->role == Stack::GraphModel::NodeRole::LayerResult) continue;
            if (!IsEditingRawLayerMaskGraph() && (id == ResolveRawWorkspaceStageOutputNodeId() && IsRawWorkspaceProjectActive())) continue;
            if ((node->kind == EditorNodeGraph::NodeKind::RawProjectSourceSet && node->rawProjectSourceSet.managed) ||
                (node->kind == EditorNodeGraph::NodeKind::MultiFrameDenoise && node->multiFrameDenoise.managed) ||
                (node->kind == EditorNodeGraph::NodeKind::RawProjectFrame && node->rawProjectFrame.managed)) continue;
            const auto reconnect = BuildReconnectPlansForNodeRemoval(graph, *node);
            if (node->kind == EditorNodeGraph::NodeKind::Layer) {
                const int index = node->layerIndex;
                graph.RemoveLayerNode(index); settings.erase(settings.begin() + index);
            } else graph.RemoveNode(id);
            for (const auto& connection : reconnect) {
                std::string error;
                if (!graph.TryConnectSockets(connection.fromNodeId,connection.fromSocketId,connection.toNodeId,connection.toSocketId,&error))
                    throw std::runtime_error(error);
            }
            removedAny = true;
        }
        graph.ClearSelection();
    });
    if (!removedAny || !context.applyDocumentEdit(std::move(proposal),std::move(settings),GetGraphAnimation(),m_RawLayerStatus)) return false;
    for (const int id : nodeIds) {
        ClearGraphAutoFocusIfTrackedNode(id);
        if (m_CanvasToolOwnerNodeId == id) CancelCanvasTool();
    }
    return true;
}

void EditorModule::AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition) {
    const bool createOutput = !IsEditingRawLayerMaskGraph() && GetConnectedOutputCount() == 0;
    int nodeId = 0;
    std::string error;
    if (!ApplyGraphCommand(*this,[&](auto& graph) {
            auto* node = graph.AddImageGeneratorNode(generatorKind,graphPosition);
            if (!node) throw std::runtime_error("The image generator could not be created.");
            nodeId = node->id;
            if (createOutput) {
                auto* output = graph.AddOutputNode({graphPosition.x+330.f,graphPosition.y});
                if (!output || !graph.TryConnectSockets(nodeId,"imageOut",output->id,"imageIn",&error))
                    throw std::runtime_error(error.empty() ? "The generator output could not be created." : error);
            }
            graph.SelectNode(nodeId);
        },&error)) {
        PostNotification(UiNotificationSeverity::Error,error,"generator-auto-output");
        return;
    }
    SelectGraphNode(nodeId);
    if (createOutput && GetCompletedChainCount() == 1 && m_Pipeline.GetSourcePixelsRaw().empty())
        EnterSingleOutputPreviewMode();
}

void EditorModule::AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddMixNode(graphPosition); });
}

void EditorModule::AddDataMathNodeAt(EditorNodeGraph::DataMathMode mode, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddDataMathNode(mode, graphPosition); });
}

void EditorModule::AddValueNodeAt(
    Stack::NodeMath::FirstClassValue value,
    EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddValueNode(std::move(value), graphPosition); });
}

void EditorModule::AddFieldMeanNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddFieldMeanNode(graphPosition); });
}

void EditorModule::AddReformatNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddReformatNode(graphPosition); });
}

void EditorModule::AddRawOperationNodeAt(Stack::RawRecipe::GraphOperationKind kind, EditorNodeGraph::Vec2 position) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddRawOperationNode(kind,position); });
}

void EditorModule::AddTechnicalImageNodeAt(
    Stack::NodeMath::TechnicalImageOperation operation,
    EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddTechnicalImageNode(operation, graphPosition); });
}

void EditorModule::AddCompoundTemplateNodeAt(
    std::size_t templateIndex,
    EditorNodeGraph::Vec2 graphPosition) {
    const Stack::NodeMath::CompoundDefinition* definition =
        EditorNodeGraphDefinitions::FindShippedCompoundTemplate(templateIndex);
    if (!definition) return;
    const auto& templates = EditorNodeGraphDefinitions::GetShippedCompoundTemplates();
    std::string closureError;
    const auto closure = Stack::NodeMath::CollectCompoundDependencyClosure(
        templates, { definition->identity }, &closureError);
    if (closure.empty()) {
        ShowUiNotification(UiNotificationSeverity::Error, closureError, "compound-add");
        return;
    }
    int addedId = -1;
    if (!ApplyGraphCommand(*this,[&](auto& graph) {
        for (const auto& reference : closure) {
            const auto* dependency = Stack::NodeMath::FindExactCompoundDefinition(templates,reference);
            std::string reason;
            if (!dependency || !graph.AddCompoundDefinition(*dependency,&reason)) throw std::runtime_error(reason);
        }
        const auto* node = graph.AddCompoundNode(definition->identity,graphPosition);
        if (!node) throw std::runtime_error("Could not add the compound instance.");
        addedId = node->id;
    },&closureError)) {
        ShowUiNotification(UiNotificationSeverity::Error,closureError,"compound-add");
        return;
    }
    SelectGraphNode(addedId);
}

bool EditorModule::MakeCompoundNodeUnique(int nodeId, std::string* error) {
    if (!ApplyGraphCommand(*this,[&](auto& graph) {
            std::string reason;
            if (!graph.MakeCompoundNodeUnique(nodeId,&reason)) throw std::runtime_error(reason);
        },error)) return false;
    SelectGraphNode(nodeId);
    return true;
}

bool EditorModule::UnpackCompoundNode(int nodeId, std::string* error) {
    return ApplyGraphCommand(*this,[&](auto& graph) {
        std::vector<int> unpacked;
        std::string reason;
        if (!graph.UnpackCompoundNode(nodeId,&unpacked,&reason)) throw std::runtime_error(reason);
    },error);
}

bool EditorModule::UpdateCompoundNodeToLatestEmbeddedVersion(int nodeId, std::string* error) {
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Compound) {
        if (error) *error = "Selected node is not a compound instance.";
        return false;
    }
    const auto greater = [](const Stack::NodeMath::SemanticVersion& left,
                            const Stack::NodeMath::SemanticVersion& right) {
        if (left.major != right.major) return left.major > right.major;
        if (left.minor != right.minor) return left.minor > right.minor;
        return left.patch > right.patch;
    };
    const Stack::NodeMath::CompoundDefinition* latest = nullptr;
    for (const Stack::NodeMath::CompoundDefinition& candidate :
            GetNodeGraph().GetCompoundDefinitions()) {
        if (candidate.identity.id != node->compound.instance.definition.id ||
            !greater(candidate.identity.version, node->compound.instance.definition.version)) continue;
        if (!latest || greater(candidate.identity.version, latest->identity.version)) latest = &candidate;
    }
    if (!latest) {
        if (error) *error = "No newer exact embedded definition is available.";
        return false;
    }
    const Stack::NodeMath::DefinitionReference reference = latest->identity;
    return ApplyGraphCommand(*this,[&](auto& graph) {
        std::string reason;
        if (!graph.UpdateCompoundNodeDefinition(nodeId,reference,&reason)) throw std::runtime_error(reason);
    },error);
}

bool EditorModule::CreateCompoundFromSelection(const std::string& label, std::string* error) {
    int compoundNodeId = -1;
    if (!ApplyGraphCommand(*this,[&](auto& graph) {
            std::string reason;
            if (!graph.CreateCompoundFromSelection(graph.GetSelectedNodeIds(),label,&compoundNodeId,&reason))
                throw std::runtime_error(reason);
        },error)) return false;
    SelectGraphNode(compoundNodeId);
    return true;
}

void EditorModule::AddFrequencyFilterNodeAt(EditorNodeGraph::FrequencyFilterMode mode, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddFrequencyFilterNode(mode, graphPosition); });
}

void EditorModule::AddFrequencyResponseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddFrequencyResponseNode(graphPosition); });
}

void EditorModule::AddFrequencyFftNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddFrequencyFftNode(graphPosition); });
}

void EditorModule::AddFrequencyIfftNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddFrequencyIfftNode(graphPosition); });
}

void EditorModule::AddSpectrumViewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddSpectrumViewNode(graphPosition); });
}

void EditorModule::AddApplyFrequencyResponseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddApplyFrequencyResponseNode(graphPosition); });
}

void EditorModule::AddCombineSpectraNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddCombineSpectraNode(graphPosition); });
}

void EditorModule::AddSpectrumSeparateNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddSpectrumSeparateNode(graphPosition); });
}

void EditorModule::AddSpectrumRecombineNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddSpectrumRecombineNode(graphPosition); });
}

bool EditorModule::SetFrequencyParameterExposed(
    int nodeId,
    const std::string& parameterId,
    bool exposed) {
    const EditorNodeGraph::Node* node = GetNodeGraph().FindNode(nodeId);
    if (node == nullptr) return false;
    const EditorNodeGraphDefinitions::LiveNodeDefinition* definition =
        EditorNodeGraphDefinitions::FindLiveNodeDefinition(*node);
    if (definition == nullptr) return false;
    const auto parameter = std::find_if(
        definition->parameters.begin(),
        definition->parameters.end(),
        [&](const EditorNodeGraphDefinitions::LiveParameterDefinition& candidate) {
            return candidate.id == parameterId && candidate.graphInputCapable;
        });
    if (parameter == definition->parameters.end()) {
        bool dynamicNotchParameter = false;
        if (node->kind == EditorNodeGraph::NodeKind::FrequencyResponse) {
            for (const EditorNodeGraph::FrequencyNotch& notch :
                 node->frequencyResponseSettings.notches) {
                for (const char* field : { "frequency", "direction", "width" }) {
                    if (parameterId ==
                        EditorNodeGraph::FrequencyNotchParameterId(
                            notch.id, field)) {
                        dynamicNotchParameter = true;
                        break;
                    }
                }
                if (dynamicNotchParameter) break;
            }
        }
        if (!dynamicNotchParameter) return false;
    }
    std::string error;
    const bool applied=ApplyGraphCommand(*this,[&](auto& graph) {
        if (!graph.SetParameterExposed(nodeId,parameterId,exposed))
            throw std::runtime_error("The parameter connection could not be changed.");
    },&error);
    if(!applied) PostNotification(UiNotificationSeverity::Error,error,"graph-parameter-exposed");
    return applied;
}

bool EditorModule::ApplyFrequencyGraphHistoryPatch(
    FrequencyGraphHistoryPatch& patch,
    bool applyAfter) {
    auto context = GetGraphEditorContext();
    auto candidate = GetNodeGraph();
    const std::uint64_t expectedRevision =
        applyAfter
            ? patch.expectedBeforeRevision
            : patch.expectedAfterRevision;
    if (candidate.GetStructureRevision() !=
        expectedRevision) {
        return false;
    }
    const std::vector<FrequencyGraphHistoryNode>& removeNodes =
        applyAfter ? patch.beforeNodes : patch.afterNodes;
    const std::vector<FrequencyGraphHistoryNode>& targetNodes =
        applyAfter ? patch.afterNodes : patch.beforeNodes;
    const std::vector<FrequencyGraphHistoryLink>& targetLinks =
        applyAfter ? patch.afterLinks : patch.beforeLinks;
    const std::vector<int>& targetSelection =
        applyAfter ? patch.afterSelection : patch.beforeSelection;
    const std::optional<EditorNodeGraph::Link>& targetSelectedLink =
        applyAfter
            ? patch.afterSelectedLink
            : patch.beforeSelectedLink;
    const int targetNextNodeId =
        applyAfter
            ? patch.afterNextNodeId
            : patch.beforeNextNodeId;

    for (const FrequencyGraphHistoryNode& item : removeNodes) {
        if (!candidate.FindNode(item.node.id)) {
            return false;
        }
    }
    for (const FrequencyGraphHistoryNode& item : targetNodes) {
        if (candidate.FindNode(item.node.id)) {
            return false;
        }
    }

    // Finish every potentially allocating copy/reserve before removing the
    // currently authored side of the patch.
    std::vector<FrequencyGraphHistoryNode> preparedNodes =
        targetNodes;
    std::vector<FrequencyGraphHistoryLink> preparedLinks =
        targetLinks;
    std::vector<int> preparedSelection = targetSelection;
    if (preparedNodes.size() >
        candidate.GetNodes().max_size() -
            candidate.GetNodes().size()) {
        return false;
    }
    if (preparedLinks.size() >
        candidate.GetLinks().max_size() -
            candidate.GetLinks().size()) {
        return false;
    }
    candidate.EditNodes().reserve(
        candidate.GetNodes().size() +
        preparedNodes.size());
    candidate.EditLinks().reserve(
        candidate.GetLinks().size() +
        preparedLinks.size());

    for (const FrequencyGraphHistoryNode& item : removeNodes) {
        if (!candidate.RemoveNode(item.node.id)) {
            return false;
        }
    }
    for (FrequencyGraphHistoryNode& item : preparedNodes) {
        std::vector<EditorNodeGraph::Node>& nodes =
            candidate.EditNodes();
        const std::size_t insertionIndex =
            std::min(item.index, nodes.size());
        nodes.insert(
            nodes.begin() +
                static_cast<std::ptrdiff_t>(insertionIndex),
            std::move(item.node));
    }
    for (FrequencyGraphHistoryLink& item : preparedLinks) {
        std::vector<EditorNodeGraph::Link>& links =
            candidate.EditLinks();
        const std::size_t insertionIndex =
            std::min(item.index, links.size());
        links.insert(
            links.begin() +
                static_cast<std::ptrdiff_t>(insertionIndex),
            std::move(item.link));
    }
    candidate.SetNextNodeId(targetNextNodeId);
    candidate.ClearSelection();
    for (const int nodeId : preparedSelection) {
        candidate.SelectNode(nodeId, true);
    }
    if (targetSelectedLink) {
        candidate.SelectLink(
            targetSelectedLink->fromNodeId,
            targetSelectedLink->fromSocketId,
            targetSelectedLink->toNodeId,
            targetSelectedLink->toSocketId);
    }
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph, context.revision,
        [&](auto& graph) { graph = std::move(candidate); });
    std::string reason;
    if (!context.applyEdit(std::move(proposal), reason)) return false;
    if (applyAfter) {
        patch.expectedAfterRevision =
            GetNodeGraph().GetStructureRevision();
    } else {
        patch.expectedBeforeRevision =
            GetNodeGraph().GetStructureRevision();
    }
    return true;
}

bool EditorModule::ExtractFrequencyResponseNode(
    int filterNodeId,
    std::string* error) {
    auto context = GetGraphEditorContext();
    auto candidate = GetNodeGraph();
    const EditorNodeGraph::Node* filter = candidate.FindNode(filterNodeId);
    if (filter == nullptr ||
        filter->kind != EditorNodeGraph::NodeKind::FrequencyFilter) {
        if (error) *error = "Extract Response Node requires a Frequency Filter.";
        return false;
    }
    if (candidate.FindAnyInputLink(
            filterNodeId, EditorNodeGraph::kFrequencyResponseInputSocketId) != nullptr) {
        if (error) *error = "This Frequency Filter already has a connected Response.";
        return false;
    }

    FrequencyGraphHistoryPatch patch;
    patch.beforeNextNodeId = candidate.GetNextNodeId();
    patch.expectedBeforeRevision =
        candidate.GetStructureRevision();
    patch.beforeSelection =
        candidate.GetSelectedNodeIds();
    if (const EditorNodeGraph::Link* selected =
            candidate.GetSelectedLink()) {
        patch.beforeSelectedLink = *selected;
    }
    const EditorNodeGraph::FrequencyResponseSettings responseSettings =
        filter->frequencyFilterSettings.localResponse;
    const EditorNodeGraph::Vec2 position {
        filter->position.x - 260.0f,
        filter->position.y + 28.0f
    };
    int responseNodeId = -1;
    const auto rollbackExtraction = [&]() {
        if (responseNodeId > 0) {
            candidate.RemoveNode(responseNodeId);
        }
        candidate.SetNextNodeId(
            patch.beforeNextNodeId);
        candidate.ClearSelection();
        for (const int selectedNodeId :
             patch.beforeSelection) {
            candidate.SelectNode(
                selectedNodeId,
                true);
        }
        if (patch.beforeSelectedLink) {
            const EditorNodeGraph::Link& selected =
                *patch.beforeSelectedLink;
            candidate.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
    };

    try {
        EditorNodeGraph::Node* response =
            candidate.AddFrequencyResponseNode(
                position);
        if (!response) {
            rollbackExtraction();
            SetGraphMutationErrorNoThrow(
                error,
                "Could not create the Frequency Response node.");
            return false;
        }
        responseNodeId = response->id;
        response->frequencyResponseSettings =
            responseSettings;
        std::string connectionError;
        if (!candidate.TryConnectSockets(
                responseNodeId,
                EditorNodeGraph::kFrequencyResponseOutputSocketId,
                filterNodeId,
                EditorNodeGraph::kFrequencyResponseInputSocketId,
                &connectionError)) {
            rollbackExtraction();
            if (error) *error = std::move(connectionError);
            return false;
        }
        candidate.SelectNode(responseNodeId);

        const auto responseIt = std::find_if(
            candidate.GetNodes().begin(),
            candidate.GetNodes().end(),
            [responseNodeId](
                const EditorNodeGraph::Node& node) {
                return node.id == responseNodeId;
            });
        const EditorNodeGraph::Link* responseLink =
            candidate.FindAnyInputLink(
                filterNodeId,
                EditorNodeGraph::kFrequencyResponseInputSocketId);
        if (responseIt == candidate.GetNodes().end() ||
            !responseLink ||
            responseLink->fromNodeId != responseNodeId) {
            rollbackExtraction();
            SetGraphMutationErrorNoThrow(
                error,
                "Could not record the extracted response topology.");
            return false;
        }
        patch.afterNodes.push_back({
            static_cast<std::size_t>(
                std::distance(
                    candidate.GetNodes().begin(),
                    responseIt)),
            *responseIt
        });
        const auto responseLinkIt = std::find_if(
            candidate.GetLinks().begin(),
            candidate.GetLinks().end(),
            [responseLink](
                const EditorNodeGraph::Link& link) {
                return &link == responseLink;
            });
        patch.afterLinks.push_back({
            static_cast<std::size_t>(
                std::distance(
                    candidate.GetLinks().begin(),
                    responseLinkIt)),
            *responseLink
        });
        patch.afterNextNodeId =
            candidate.GetNextNodeId();
        patch.expectedAfterRevision =
            candidate.GetStructureRevision();
        patch.afterSelection =
            candidate.GetSelectedNodeIds();
        if (const EditorNodeGraph::Link* selected =
                candidate.GetSelectedLink()) {
            patch.afterSelectedLink = *selected;
        }

    } catch (const std::bad_alloc&) {
        rollbackExtraction();
        SetGraphMutationErrorNoThrow(
            error,
            "Could not record frequency graph history because memory is exhausted.");
        return false;
    } catch (const std::length_error&) {
        rollbackExtraction();
        SetGraphMutationErrorNoThrow(
            error,
            "Could not record frequency graph history because its size limit was reached.");
        return false;
    } catch (...) {
        rollbackExtraction();
        throw;
    }
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph, context.revision,
        [&](auto& graph) { graph = std::move(candidate); });
    std::string reason;
    if (!context.applyEdit(std::move(proposal), reason)) {
        if (error) *error = std::move(reason);
        return false;
    }
    patch.expectedAfterRevision = GetNodeGraph().GetStructureRevision();
    if (!IsEditingRawLayerMaskGraph()) m_FrequencyGraphUndo = std::move(patch);
    else m_FrequencyGraphUndo.reset();
    m_FrequencyGraphRedo.reset();
    return true;
}

bool EditorModule::ExpandFrequencyFilterNode(
    int filterNodeId,
    std::string* error) {
    auto context = GetGraphEditorContext();
    auto candidate = GetNodeGraph();
    const EditorNodeGraph::Node* filter = candidate.FindNode(filterNodeId);
    if (filter == nullptr ||
        filter->kind != EditorNodeGraph::NodeKind::FrequencyFilter) {
        if (error) *error = "Expand requires a Frequency Filter node.";
        return false;
    }

    FrequencyGraphHistoryPatch patch;
    patch.beforeNextNodeId =
        candidate.GetNextNodeId();
    patch.expectedBeforeRevision =
        candidate.GetStructureRevision();
    patch.beforeSelection =
        candidate.GetSelectedNodeIds();
    if (const EditorNodeGraph::Link* selected =
            candidate.GetSelectedLink()) {
        patch.beforeSelectedLink = *selected;
    }
    const auto filterIt = std::find_if(
        candidate.GetNodes().begin(),
        candidate.GetNodes().end(),
        [filterNodeId](
            const EditorNodeGraph::Node& node) {
            return node.id == filterNodeId;
        });
    if (filterIt == candidate.GetNodes().end()) {
        if (error) *error = "Could not locate the original Frequency Filter.";
        return false;
    }
    patch.beforeNodes.push_back({
        static_cast<std::size_t>(
            std::distance(
                candidate.GetNodes().begin(),
                filterIt)),
        *filterIt
    });
    const EditorNodeGraph::FrequencyFilterSettings filterSettings =
        filter->frequencyFilterSettings;
    const std::vector<std::string> exposedParameters =
        filter->exposedParameterIds;
    const EditorNodeGraph::Vec2 origin = filter->position;
    std::vector<EditorNodeGraph::Link> incoming;
    std::vector<EditorNodeGraph::Link> outgoing;
    for (std::size_t linkIndex = 0;
         linkIndex < candidate.GetLinks().size();
         ++linkIndex) {
        const EditorNodeGraph::Link& link =
            candidate.GetLinks()[linkIndex];
        if (link.toNodeId == filterNodeId) incoming.push_back(link);
        if (link.fromNodeId == filterNodeId) outgoing.push_back(link);
        if (link.toNodeId == filterNodeId ||
            link.fromNodeId == filterNodeId) {
            patch.beforeLinks.push_back({
                linkIndex,
                link
            });
        }
    }
    FrequencyGraphHistoryNode rollbackFilter =
        patch.beforeNodes.front();
    std::vector<FrequencyGraphHistoryLink> rollbackLinks =
        patch.beforeLinks;
    std::vector<int> createdNodeIds;
    createdNodeIds.reserve(4u);
    bool rolledBack = false;
    const auto rollbackAdvancedChain = [&]() {
        if (rolledBack) {
            return;
        }
        rolledBack = true;
        for (const int createdNodeId : createdNodeIds) {
            candidate.RemoveNode(createdNodeId);
        }
        if (!candidate.FindNode(filterNodeId)) {
            std::vector<EditorNodeGraph::Node>& nodes =
                candidate.EditNodes();
            const std::size_t insertionIndex =
                std::min(
                    rollbackFilter.index,
                    nodes.size());
            nodes.insert(
                nodes.begin() +
                    static_cast<std::ptrdiff_t>(
                        insertionIndex),
                std::move(rollbackFilter.node));
        }
        for (FrequencyGraphHistoryLink& item :
             rollbackLinks) {
            std::vector<EditorNodeGraph::Link>& links =
                candidate.EditLinks();
            const std::size_t insertionIndex =
                std::min(item.index, links.size());
            links.insert(
                links.begin() +
                    static_cast<std::ptrdiff_t>(
                        insertionIndex),
                std::move(item.link));
        }
        candidate.SetNextNodeId(
            patch.beforeNextNodeId);
        candidate.ClearSelection();
        for (const int selectedNodeId :
             patch.beforeSelection) {
            candidate.SelectNode(
                selectedNodeId,
                true);
        }
        if (patch.beforeSelectedLink) {
            const EditorNodeGraph::Link& selected =
                *patch.beforeSelectedLink;
            candidate.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
    };

    try {
        constexpr std::size_t kAdvancedNodeCount = 4u;
        if (incoming.size() >
                std::numeric_limits<std::size_t>::max() - 3u ||
            outgoing.size() >
                std::numeric_limits<std::size_t>::max() -
                    3u - incoming.size()) {
            throw std::length_error(
                "advanced frequency link count overflow");
        }
        const std::size_t maximumAddedLinks =
            3u + incoming.size() + outgoing.size();
        if (kAdvancedNodeCount >
                candidate.GetNodes().max_size() -
                    candidate.GetNodes().size() ||
            maximumAddedLinks >
                candidate.GetLinks().max_size() -
                    candidate.GetLinks().size()) {
            throw std::length_error(
                "advanced frequency graph capacity exhausted");
        }
        candidate.EditNodes().reserve(
            candidate.GetNodes().size() +
            kAdvancedNodeCount);
        candidate.EditLinks().reserve(
            candidate.GetLinks().size() +
            maximumAddedLinks);
        if (!candidate.RemoveNode(filterNodeId)) {
            if (error) *error = "Could not remove the original Frequency Filter.";
            return false;
        }

        EditorNodeGraph::Node* created =
            candidate.AddFrequencyFftNode(
                { origin.x - 330.0f, origin.y });
        const int transformId = created ? created->id : -1;
        if (transformId > 0) createdNodeIds.push_back(transformId);
        created = candidate.AddFrequencyResponseNode(
            { origin.x - 110.0f, origin.y + 190.0f });
        const int responseId = created ? created->id : -1;
        if (responseId > 0) createdNodeIds.push_back(responseId);
        created = candidate.AddApplyFrequencyResponseNode(
            { origin.x - 90.0f, origin.y });
        const int applyId = created ? created->id : -1;
        if (applyId > 0) createdNodeIds.push_back(applyId);
        created = candidate.AddFrequencyIfftNode(
            { origin.x + 150.0f, origin.y });
        const int inverseId = created ? created->id : -1;
        if (inverseId > 0) createdNodeIds.push_back(inverseId);
        if (transformId <= 0 || responseId <= 0 ||
            applyId <= 0 || inverseId <= 0) {
            rollbackAdvancedChain();
            if (error) *error = "Could not create the advanced frequency chain.";
            return false;
        }
        candidate.FindNode(transformId)->
            frequencyFftSettings.edgePolicy =
                filterSettings.edgePolicy;
        candidate.FindNode(responseId)->
            frequencyResponseSettings =
                filterSettings.localResponse;
        candidate.FindNode(applyId)->
            applyFrequencyResponseSettings.strength =
                filterSettings.strength;
        if (std::find(
                exposedParameters.begin(),
                exposedParameters.end(),
                EditorNodeGraph::kStrengthParameterId) !=
            exposedParameters.end()) {
            candidate.SetParameterExposed(
                applyId,
                EditorNodeGraph::kStrengthParameterId,
                true);
        }

        const auto connect =
            [&](int fromNodeId,
                const std::string& fromSocket,
                int toNodeId,
                const std::string& toSocket) {
                std::string message;
                if (candidate.TryConnectSockets(
                        fromNodeId,
                        fromSocket,
                        toNodeId,
                        toSocket,
                        &message)) {
                    return true;
                }
                if (error) *error = std::move(message);
                return false;
            };

        bool ok = connect(
            transformId,
            EditorNodeGraph::kSpectrumOutputSocketId,
            applyId,
            EditorNodeGraph::kSpectrumInputSocketId);
        ok = ok && connect(
            applyId,
            EditorNodeGraph::kSpectrumOutputSocketId,
            inverseId,
            EditorNodeGraph::kSpectrumInputSocketId);

        const EditorNodeGraph::Link* externalResponse = nullptr;
        for (const EditorNodeGraph::Link& link : incoming) {
            if (link.toSocketId ==
                EditorNodeGraph::kChannelInputSocketId) {
                ok = ok && connect(
                    link.fromNodeId,
                    link.fromSocketId,
                    transformId,
                    EditorNodeGraph::kChannelInputSocketId);
            } else if (link.toSocketId ==
                       EditorNodeGraph::kFrequencyResponseInputSocketId) {
                externalResponse = &link;
            } else if (link.toSocketId ==
                       EditorNodeGraph::ParameterInputSocketId(
                           EditorNodeGraph::kStrengthParameterId)) {
                candidate.SetParameterExposed(
                    applyId,
                    EditorNodeGraph::kStrengthParameterId,
                    true);
                ok = ok && connect(
                    link.fromNodeId,
                    link.fromSocketId,
                    applyId,
                    EditorNodeGraph::ParameterInputSocketId(
                        EditorNodeGraph::kStrengthParameterId));
            }
        }
        if (externalResponse) {
            ok = ok && connect(
                externalResponse->fromNodeId,
                externalResponse->fromSocketId,
                applyId,
                EditorNodeGraph::kFrequencyResponseInputSocketId);
            candidate.RemoveNode(responseId);
        } else {
            ok = ok && connect(
                responseId,
                EditorNodeGraph::kFrequencyResponseOutputSocketId,
                applyId,
                EditorNodeGraph::kFrequencyResponseInputSocketId);
        }
        for (const EditorNodeGraph::Link& link : outgoing) {
            if (link.fromSocketId ==
                EditorNodeGraph::kChannelOutputSocketId) {
                ok = ok && connect(
                    inverseId,
                    EditorNodeGraph::kChannelOutputSocketId,
                    link.toNodeId,
                    link.toSocketId);
            }
        }
        if (!ok) {
            rollbackAdvancedChain();
            return false;
        }

        candidate.SelectNode(applyId);
        std::unordered_set<int> addedNodeIds(
            createdNodeIds.begin(),
            createdNodeIds.end());
        for (std::size_t nodeIndex = 0;
             nodeIndex < candidate.GetNodes().size();
             ++nodeIndex) {
            const EditorNodeGraph::Node& node =
                candidate.GetNodes()[nodeIndex];
            if (addedNodeIds.count(node.id) != 0u) {
                patch.afterNodes.push_back({
                    nodeIndex,
                    node
                });
            }
        }
        for (std::size_t linkIndex = 0;
             linkIndex < candidate.GetLinks().size();
             ++linkIndex) {
            const EditorNodeGraph::Link& link =
                candidate.GetLinks()[linkIndex];
            if (addedNodeIds.count(link.fromNodeId) != 0u ||
                addedNodeIds.count(link.toNodeId) != 0u) {
                patch.afterLinks.push_back({
                    linkIndex,
                    link
                });
            }
        }
        patch.afterNextNodeId =
            candidate.GetNextNodeId();
        patch.expectedAfterRevision =
            candidate.GetStructureRevision();
        patch.afterSelection =
            candidate.GetSelectedNodeIds();
        if (const EditorNodeGraph::Link* selected =
                candidate.GetSelectedLink()) {
            patch.afterSelectedLink = *selected;
        }

    } catch (const std::bad_alloc&) {
        rollbackAdvancedChain();
        SetGraphMutationErrorNoThrow(
            error,
            "Could not record frequency graph history because memory is exhausted.");
        return false;
    } catch (const std::length_error&) {
        rollbackAdvancedChain();
        SetGraphMutationErrorNoThrow(
            error,
            "Could not record frequency graph history because its size limit was reached.");
        return false;
    } catch (...) {
        rollbackAdvancedChain();
        throw;
    }
    auto proposal = Stack::GraphModel::ProposeEdit(*context.graph, context.revision,
        [&](auto& graph) { graph = std::move(candidate); });
    std::string reason;
    if (!context.applyEdit(std::move(proposal), reason)) {
        if (error) *error = std::move(reason);
        return false;
    }
    patch.expectedAfterRevision = GetNodeGraph().GetStructureRevision();
    if (!IsEditingRawLayerMaskGraph()) m_FrequencyGraphUndo = std::move(patch);
    else m_FrequencyGraphUndo.reset();
    m_FrequencyGraphRedo.reset();
    return true;
}

bool EditorModule::UndoFrequencyGraphAction() {
    if (IsEditingRawLayerMaskGraph()) return UndoRawLayerEdit();
    if (!m_FrequencyGraphUndo.has_value()) return false;
    if (GetNodeGraph().GetStructureRevision() !=
        m_FrequencyGraphUndo->expectedAfterRevision) {
        m_FrequencyGraphUndo.reset();
        m_FrequencyGraphRedo.reset();
        return false;
    }
    if (!ApplyFrequencyGraphHistoryPatch(
            *m_FrequencyGraphUndo,
            false)) {
        return false;
    }
    m_FrequencyGraphRedo =
        std::move(*m_FrequencyGraphUndo);
    m_FrequencyGraphUndo.reset();

    return true;
}

bool EditorModule::RedoFrequencyGraphAction() {
    if (IsEditingRawLayerMaskGraph()) return RedoRawLayerEdit();
    if (!m_FrequencyGraphRedo.has_value()) return false;
    if (GetNodeGraph().GetStructureRevision() !=
        m_FrequencyGraphRedo->expectedBeforeRevision) {
        m_FrequencyGraphUndo.reset();
        m_FrequencyGraphRedo.reset();
        return false;
    }
    if (!ApplyFrequencyGraphHistoryPatch(
            *m_FrequencyGraphRedo,
            true)) {
        return false;
    }
    m_FrequencyGraphUndo =
        std::move(*m_FrequencyGraphRedo);
    m_FrequencyGraphRedo.reset();

    return true;
}

void EditorModule::AddFrequencyMaskNodeAt(EditorNodeGraph::FrequencyMaskShape shape, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddFrequencyMaskNode(shape, graphPosition); });
}

void EditorModule::AddSpectrumMathNodeAt(EditorNodeGraph::SpectrumMathMode mode, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddSpectrumMathNode(mode, graphPosition); });
}

void EditorModule::AddMagnitudePhaseNodeAt(EditorNodeGraph::MagnitudePhaseMode mode, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddMagnitudePhaseNode(mode, graphPosition); });
}

void EditorModule::AddSpectrumAnalyzerNodeAt(EditorNodeGraph::SpectrumAnalyzerMode mode, EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddSpectrumAnalyzerNode(mode, graphPosition); });
}

void EditorModule::AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddPreviewNode(graphPosition); });
}

void EditorModule::AddChannelSplitNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddChannelSplitNode(graphPosition); });
}

void EditorModule::AddChannelCombineNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddChannelCombineNode(graphPosition); });
}

void EditorModule::AddConstantChannelNodeAt(
    EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddConstantChannelNode(graphPosition); });
}

void EditorModule::AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    AddGraphNode(*this,[&](auto& graph) { return graph.AddOutputNode(graphPosition); });
}

void EditorModule::AutoLayoutGraph() {
    GetNodeGraph().AutoLayout();
    MarkGraphEdited(-1, false);
}

void EditorModule::DisconnectGraphOutput() {
    std::string error;
    if (!ApplyGraphCommand(*this,[](auto& graph) { graph.DisconnectOutput(); },&error)) {
        PostNotification(UiNotificationSeverity::Error,error,"graph-disconnect-output");
        return;
    }
    if (!IsEditingRawLayerMaskGraph()) {
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
    }
}

void EditorModule::RefreshGraphLayerMetadata() {
    GetNodeGraph().SyncLayerNodes(static_cast<int>(GetLayers().size()));

    for (int i = 0; i < static_cast<int>(GetLayers().size()); ++i) {
        EditorNodeGraph::Node* node = GetNodeGraph().FindNodeByLayerIndex(i);
        if (!node) {
            continue;
        }

        const nlohmann::json layerJson = GetLayers()[i]->Serialize();
        const std::string typeId = layerJson.value("type", std::string());
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(typeId);
        if (descriptor) {
            GetNodeGraph().SetLayerNodeType(node->id, descriptor->type);
        } else {
            node->typeId = typeId;
            node->title = GetLayers()[i]->GetDefaultName();
        }
    }
}

