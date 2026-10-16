#include "Editor/EditorModule.h"
#include "Editor/RawLayerTools.h"
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
    if (type==LayerType::ToneCurve) { AddRawOperationNodeAt(Stack::RawRecipe::GraphOperationKind::ToneCurve,graphPosition); return; }
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
        node->layerParameters=layer->Serialize();
        node->layerParameters["visible"]=layer->IsVisible();
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
    if (from==to || from<0 || to<0 || from>=static_cast<int>(GetLayers().size()) ||
        to>=static_cast<int>(GetLayers().size())) return;
    auto context=GetGraphEditorContext();
    auto proposal=Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& graph) {
        std::vector<EditorNodeGraph::Node> ordered;
        for (auto& node:graph.EditNodes()) if (node.kind==EditorNodeGraph::NodeKind::Layer) {
            if (node.layerIndex==from) node.layerIndex=to;
            else if (from<to && node.layerIndex>from && node.layerIndex<=to) --node.layerIndex;
            else if (from>to && node.layerIndex>=to && node.layerIndex<from) ++node.layerIndex;
            ordered.push_back(node);
        }
        std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a.layerIndex<b.layerIndex;});
        auto next=ordered.begin();
        for (auto& node:graph.EditNodes()) if (node.kind==EditorNodeGraph::NodeKind::Layer)
            node=std::move(*next++);
    });
    std::string error;
    if (!context.applyEdit(std::move(proposal),error)) {
        PostNotification(UiNotificationSeverity::Error,error,"graph-effect-order"); return;
    }
    const auto* selected=GetNodeGraph().FindNode(GetNodeGraph().GetSelectedNodeId());
    m_SelectedLayerIndex=selected && selected->kind==EditorNodeGraph::NodeKind::Layer ? selected->layerIndex : -1;
}
void EditorModule::SetLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(GetLayers().size())) return;
    const auto* node=GetNodeGraph().FindNodeByLayerIndex(index);
    if (!node) return;
    const auto uuid=node->instanceUuid;
    auto context=GetGraphEditorContext();
    auto proposal=Stack::GraphModel::ProposeEdit(*context.graph,context.revision,[&](auto& graph) {
        for (auto& candidate:graph.EditNodes()) if (candidate.instanceUuid==uuid)
            candidate.layerParameters["visible"]=visible;
    });
    std::string error;
    if (!context.applyEdit(std::move(proposal),error))
        PostNotification(UiNotificationSeverity::Error,error,"graph-effect-visibility");
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
    if (node && IsEditingRawLayerMaskGraph() && !node->rawLayerOwnerId.empty()) {
        m_RawLayerMaskWorkspace->layerId=node->rawLayerOwnerId;
        m_SelectedRawAdjustmentLayer=node->rawLayerOwnerId==Stack::Project::kRawBackgroundId?std::string{}:node->rawLayerOwnerId;
        if(m_Project->nodeEditor && m_Project->nodeEditor->layerId!=node->rawLayerOwnerId) m_Project->nodeEditor.reset();
        const auto* owner=Stack::Project::FindRawAdjustmentLayer(m_Project->rawLayers.State(),node->rawLayerOwnerId);
        const auto tools=owner ? Stack::Editor::RawLayerToolNodes(owner->graph) : std::vector<const EditorNodeGraph::Node*>{};
        if (std::any_of(tools.begin(),tools.end(),[&](const auto* tool){return tool->instanceUuid==node->instanceUuid;}))
            SelectRawNodeEditor(node->rawLayerOwnerId,node->instanceUuid);
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectLayer(node->layerIndex);
    } else {
        SelectLayer(-1);
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::Output) {
        m_CompositeSelectedOutputNodeId = nodeId;
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
    if (node->kind == EditorNodeGraph::NodeKind::RawDecode ||
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
    const auto originalSettings = originalNode.layerParameters;
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
            node->layerParameters=originalSettings;
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
            const bool changedType=node->typeId!=descriptor->typeId;
            GetNodeGraph().SetLayerNodeType(node->id, descriptor->type);
            if (changedType) node->layerParameters=layerJson;
        } else {
            node->typeId = typeId;
            node->title = GetLayers()[i]->GetDefaultName();
        }
    }
}


bool EditorModule::SetNodeParameterExposed(int nodeId, const std::string& parameterId, bool exposed) {
    const auto* node = GetNodeGraph().FindNode(nodeId);
    if (!node) return false;
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(*node);
    if (!definition) return false;
    const auto parameter = std::find_if(definition->parameters.begin(), definition->parameters.end(),
        [&](const auto& candidate) { return candidate.id == parameterId && candidate.graphInputCapable; });
    if (parameter == definition->parameters.end()) return false;
    std::string error;
    const bool applied = ApplyGraphCommand(*this, [&](auto& graph) {
        if (!graph.SetParameterExposed(nodeId, parameterId, exposed))
            throw std::runtime_error("The parameter connection could not be changed.");
    }, &error);
    if (!applied) PostNotification(UiNotificationSeverity::Error, error, "graph-parameter-exposed");
    return applied;
}


bool EditorModule::SetDataMathDomain(int nodeId, Stack::NodeMath::ArithmeticDomain domain) {
    std::string error;
    const bool applied = ApplyGraphCommand(*this, [&](auto& graph) {
        auto* node = graph.FindNode(nodeId);
        if (!node || node->kind != EditorNodeGraph::NodeKind::DataMath)
            throw std::runtime_error("The math node is unavailable.");
        if (domain == Stack::NodeMath::ArithmeticDomain::Values && node->dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage)
            throw std::runtime_error("Average Images requires spatial image inputs.");
        node->dataMathSettings.domain = domain;
    }, &error);
    if (!applied) PostNotification(UiNotificationSeverity::Error, error, "graph-math-domain");
    return applied;
}
