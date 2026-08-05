#include "Editor/EditorModule.h"

#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
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
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer: {
            const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::MaskUtility: {
            const EditorNodeGraph::Link* input = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kMaskOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::MaskCombine: {
            const EditorNodeGraph::Link* inputA = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskCombineInputASocketId);
            const EditorNodeGraph::Link* inputB = graph.FindAnyInputLink(node.id, EditorNodeGraph::kMaskCombineInputBSocketId);
            const EditorNodeGraph::Link* selectedInput =
                inputA && !inputB ? inputA :
                inputB && !inputA ? inputB :
                nullptr;
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kMaskOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: {
            const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::kRawInputSocketId);
            if (!input) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                input->fromNodeId,
                input->fromSocketId,
                0,
                EditorNodeGraph::kRawOutputSocketId
            };
        }
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::DataMath: {
            const EditorNodeGraph::Link* selectedInput = nullptr;
            int connectedInputCount = 0;
            if (node.kind == EditorNodeGraph::NodeKind::Mix) {
                const EditorNodeGraph::Link* inputA = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputASocketId);
                const EditorNodeGraph::Link* inputB = graph.FindInputLink(node.id, EditorNodeGraph::kMixInputBSocketId);
                selectedInput =
                    inputA && !inputB ? inputA :
                    inputB && !inputA ? inputB :
                    nullptr;
            } else {
                for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
                    if (const EditorNodeGraph::Link* input = graph.FindInputLink(node.id, EditorNodeGraph::DataMathInputSocketId(inputIndex))) {
                        ++connectedInputCount;
                        selectedInput = input;
                        if (connectedInputCount > 1) {
                            selectedInput = nullptr;
                            break;
                        }
                    }
                }
            }
            if (!selectedInput) {
                return std::nullopt;
            }
            return GraphReconnectPlan{
                selectedInput->fromNodeId,
                selectedInput->fromSocketId,
                0,
                EditorNodeGraph::kImageOutputSocketId
            };
        }
        default:
            break;
    }
    return std::nullopt;
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
    std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayer(type);

    if (newLayer) {
        newLayer->InitializeGL();

        const char* defaultName = newLayer->GetDefaultName();
        int count = 0;
        for (const auto& existing : m_Layers) {
            if (strcmp(existing->GetDefaultName(), defaultName) == 0) {
                count++;
            }
        }

        if (count > 0) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix), "%s (%d)", defaultName, count + 1);
            newLayer->SetInstanceName(suffix);
        }

        m_Layers.push_back(newLayer);
        SelectLayer(static_cast<int>(m_Layers.size()) - 1);
        m_FocusSelectedTabNextRender = true;
        MarkRenderDirty();
    }
}

void EditorModule::AddLayerNodeAt(LayerType type, EditorNodeGraph::Vec2 graphPosition) {
    const int layerIndex = static_cast<int>(m_Layers.size());
    AddLayer(type);
    if (static_cast<int>(m_Layers.size()) == layerIndex + 1) {
        m_NodeGraph.AddLayerNode(type, layerIndex, graphPosition);
        RefreshGraphLayerMetadata();
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(layerIndex)) {
            SelectGraphNode(node->id);
        }
        MarkRenderDirty();
    }
}

void EditorModule::RemoveLayer(int index) {
    if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
        if (m_CanvasToolOwnerNodeId > 0) {
            const EditorNodeGraph::Node* ownerNode = m_NodeGraph.FindNode(m_CanvasToolOwnerNodeId);
            if (ownerNode && ownerNode->kind == EditorNodeGraph::NodeKind::Layer && ownerNode->layerIndex == index) {
                CancelCanvasTool();
            }
        }
        // TODO: Promote this to an undoable editor command when command history lands.
        m_Layers.erase(m_Layers.begin() + index);
        m_NodeGraph.RemoveLayerNode(index);
        RefreshGraphLayerMetadata();
        if (m_SelectedLayerIndex >= static_cast<int>(m_Layers.size())) {
            m_SelectedLayerIndex = static_cast<int>(m_Layers.size()) - 1;
        }
        MarkRenderDirty();
    }
}

void EditorModule::MoveLayer(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= static_cast<int>(m_Layers.size())) return;
    if (to < 0 || to >= static_cast<int>(m_Layers.size())) return;

    // TODO: Promote this to an undoable editor command when command history lands.
    if (from < to) {
        std::rotate(m_Layers.begin() + from, m_Layers.begin() + from + 1, m_Layers.begin() + to + 1);
    } else {
        std::rotate(m_Layers.begin() + to, m_Layers.begin() + from, m_Layers.begin() + from + 1);
    }

    if (m_SelectedLayerIndex == from) {
        m_SelectedLayerIndex = to;
    } else if (from < m_SelectedLayerIndex && to >= m_SelectedLayerIndex) {
        m_SelectedLayerIndex--;
    } else if (from > m_SelectedLayerIndex && to <= m_SelectedLayerIndex) {
        m_SelectedLayerIndex++;
    }

    RefreshGraphLayerMetadata();
    MarkRenderDirty();
}

void EditorModule::SetLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    // TODO: Promote this to an undoable editor command when command history lands.
    m_Layers[index]->SetVisible(visible);
    MarkRenderDirty();
}

void EditorModule::SelectLayer(int index) {
    if (index < -1 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    m_SelectedLayerIndex = index;
}

void EditorModule::SelectGraphNode(int nodeId) {
    m_NodeGraph.SelectNode(nodeId);
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
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
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
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

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
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
    if (layerIndex < 0 || layerIndex >= static_cast<int>(m_Layers.size()) || !m_Layers[layerIndex]) {
        return {};
    }
    return m_Layers[layerIndex]->GetNodeSurfaceSpec();
}

NodeSurfaceSpec EditorModule::GetNodeSurfaceSpec(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
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
        m_NodeGraph.FindInputLink(m_CanvasToolOwnerNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (!TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
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
    const int syntheticOutputId = -360000 - m_CanvasToolOwnerNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));
    snapshot.links.push_back(RenderGraphLink{
        input->fromNodeId,
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

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (selectedNodeId <= 0) {
        return false;
    }

    const int adjacentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(selectedNodeId, direction);
    if (adjacentNodeId <= 0 || adjacentNodeId == selectedNodeId) {
        return false;
    }

    SelectGraphNode(adjacentNodeId);
    return true;
}

void EditorModule::ApplyGraphLayerOrder() {
    const std::vector<int> order = m_NodeGraph.GetRenderLayerIndexPath();
    if (order.empty()) {
        return;
    }

    std::vector<int> uniqueOrder;
    for (int index : order) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size()) &&
            std::find(uniqueOrder.begin(), uniqueOrder.end(), index) == uniqueOrder.end()) {
            uniqueOrder.push_back(index);
        }
    }
    if (uniqueOrder.size() != m_Layers.size()) {
        return;
    }

    std::vector<std::shared_ptr<LayerBase>> reordered;
    reordered.reserve(m_Layers.size());
    for (int index : uniqueOrder) {
        reordered.push_back(m_Layers[index]);
    }
    m_Layers = std::move(reordered);

    for (int i = 0; i < static_cast<int>(uniqueOrder.size()); ++i) {
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(uniqueOrder[i])) {
            node->layerIndex = i;
        }
    }
    RefreshGraphLayerMetadata();
}

bool EditorModule::SplitLayerNodeIntoChannels(int layerNodeId) {
    EditorNodeGraph::Node* layerNode = m_NodeGraph.FindNode(layerNodeId);
    if (!layerNode || layerNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }
    if (layerNode->layerIndex < 0 || layerNode->layerIndex >= static_cast<int>(m_Layers.size())) {
        return false;
    }

    const EditorNodeGraph::Link* imageInput = m_NodeGraph.FindInputLink(layerNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!imageInput) {
        return false;
    }

    struct IndexedOutputLink {
        std::size_t index = 0;
        EditorNodeGraph::Link link;
    };
    std::vector<IndexedOutputLink> outputLinks;
    std::vector<EditorNodeGraph::Link> maskLinks;
    const std::vector<EditorNodeGraph::Link>& graphLinks =
        m_NodeGraph.GetLinks();
    for (std::size_t linkIndex = 0;
            linkIndex < graphLinks.size();
            ++linkIndex) {
        const EditorNodeGraph::Link& link = graphLinks[linkIndex];
        if (link.fromNodeId == layerNodeId &&
            link.fromSocketId ==
                EditorNodeGraph::kImageOutputSocketId) {
            outputLinks.push_back(
                IndexedOutputLink{ linkIndex, link });
        }
    }
    m_NodeGraph.ForEachIncomingLink(
        layerNodeId,
        [&](const EditorNodeGraph::Link& link) {
        if (link.toSocketId == EditorNodeGraph::kMaskInputSocketId) {
            maskLinks.push_back(link);
        }
    });
    if (outputLinks.empty()) {
        return false;
    }

    const EditorNodeGraph::Vec2 originalPos = layerNode->position;
    const int originalLayerIndex = layerNode->layerIndex;
    const LayerType originalLayerType = layerNode->layerType;
    const std::string originalTypeId = layerNode->typeId;
    const std::shared_ptr<LayerBase> originalLayer = m_Layers[originalLayerIndex];
    if (!originalLayer) {
        return false;
    }
    const int nextNodeIdBefore = m_NodeGraph.GetNextNodeId();
    const int outputNodeIdBefore = m_NodeGraph.GetOutputNodeId();
    const int activeImageNodeIdBefore =
        m_NodeGraph.GetActiveImageNodeId();
    const std::vector<int> selectionBefore =
        m_NodeGraph.GetSelectedNodeIds();
    const EditorNodeGraph::Link* selectedLinkBefore =
        m_NodeGraph.GetSelectedLink();
    const std::optional<EditorNodeGraph::Link>
        selectedLinkSnapshot = selectedLinkBefore
            ? std::optional<EditorNodeGraph::Link>(
                *selectedLinkBefore)
            : std::nullopt;
    const int selectedLayerBefore = m_SelectedLayerIndex;
    const bool focusSelectedTabBefore =
        m_FocusSelectedTabNextRender;
    ManagedRawGraphMutationConfirmState
        managedRawConfirmationBefore =
            m_ManagedRawGraphMutationConfirm;
    const bool executingManagedRawConfirmationBefore =
        m_ExecutingManagedRawGraphMutationConfirmation;
    std::vector<std::shared_ptr<LayerBase>> layersSnapshot =
        m_Layers;
    std::vector<int> originalNodeIds;
    originalNodeIds.reserve(m_NodeGraph.GetNodes().size());
    std::vector<std::pair<int, int>> originalLayerNodeIndices;
    originalLayerNodeIndices.reserve(m_Layers.size());
    for (const EditorNodeGraph::Node& node :
            m_NodeGraph.GetNodes()) {
        originalNodeIds.push_back(node.id);
        if (node.kind == EditorNodeGraph::NodeKind::Layer) {
            originalLayerNodeIndices.emplace_back(
                node.id,
                node.layerIndex);
        }
    }

    std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(layerNodeId);
    downstreamNodeIds.erase(
        std::remove(downstreamNodeIds.begin(), downstreamNodeIds.end(), layerNodeId),
        downstreamNodeIds.end());

    std::vector<std::pair<int, EditorNodeGraph::Vec2>> originalDownstreamPositions;
    originalDownstreamPositions.reserve(downstreamNodeIds.size());
    for (const int downstreamNodeId : downstreamNodeIds) {
        if (const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(downstreamNodeId)) {
            originalDownstreamPositions.emplace_back(downstreamNodeId, downstream->position);
        }
    }

    std::array<std::shared_ptr<LayerBase>, 4> cloneLayers;
    for (std::shared_ptr<LayerBase>& cloneLayer : cloneLayers) {
        try {
            cloneLayer = CloneLayerInstance(originalLayer);
        } catch (const std::bad_alloc&) {
            return false;
        } catch (const std::length_error&) {
            return false;
        }
        if (!cloneLayer) {
            return false;
        }
    }

    const auto isOriginalNodeId =
        [&originalNodeIds](int nodeId) {
            return std::find(
                       originalNodeIds.begin(),
                       originalNodeIds.end(),
                       nodeId) != originalNodeIds.end();
        };
    const auto linksMatch =
        [](const EditorNodeGraph::Link& lhs,
           const EditorNodeGraph::Link& rhs) {
            return lhs.fromNodeId == rhs.fromNodeId &&
                lhs.fromSocketId == rhs.fromSocketId &&
                lhs.toNodeId == rhs.toNodeId &&
                lhs.toSocketId == rhs.toSocketId;
        };
    const auto rollback = [&]() {
        std::vector<EditorNodeGraph::Link>& links =
            m_NodeGraph.EditLinks();
        links.erase(
            std::remove_if(
                links.begin(),
                links.end(),
                [&](const EditorNodeGraph::Link& link) {
                    if (!isOriginalNodeId(link.fromNodeId) ||
                        !isOriginalNodeId(link.toNodeId)) {
                        return true;
                    }
                    return std::any_of(
                        outputLinks.begin(),
                        outputLinks.end(),
                        [&](const IndexedOutputLink& original) {
                            return linksMatch(
                                link,
                                original.link);
                        });
                }),
            links.end());
        for (IndexedOutputLink& original : outputLinks) {
            const std::size_t insertIndex = std::min(
                original.index,
                links.size());
            links.insert(
                links.begin() +
                    static_cast<std::ptrdiff_t>(insertIndex),
                std::move(original.link));
        }

        std::vector<EditorNodeGraph::Node>& nodes =
            m_NodeGraph.EditNodes();
        nodes.erase(
            std::remove_if(
                nodes.begin(),
                nodes.end(),
                [&isOriginalNodeId](
                    const EditorNodeGraph::Node& node) {
                    return !isOriginalNodeId(node.id);
                }),
            nodes.end());
        m_Layers.swap(layersSnapshot);
        for (const std::pair<int, int>& entry :
                originalLayerNodeIndices) {
            if (EditorNodeGraph::Node* node =
                    m_NodeGraph.FindNode(entry.first)) {
                node->layerIndex = entry.second;
            }
        }
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream =
                    m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        m_NodeGraph.SetNextNodeId(nextNodeIdBefore);
        m_NodeGraph.SetOutputNodeId(outputNodeIdBefore);
        m_NodeGraph.SetActiveImageNodeId(
            activeImageNodeIdBefore);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId : selectionBefore) {
            m_NodeGraph.SelectNode(selectedNodeId, true);
        }
        if (selectedLinkSnapshot) {
            const EditorNodeGraph::Link& selected =
                *selectedLinkSnapshot;
            m_NodeGraph.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
        m_SelectedLayerIndex = selectedLayerBefore;
        m_FocusSelectedTabNextRender =
            focusSelectedTabBefore;
        std::swap(
            m_ManagedRawGraphMutationConfirm,
            managedRawConfirmationBefore);
        m_ExecutingManagedRawGraphMutationConfirmation =
            executingManagedRawConfirmationBefore;
        MarkRenderDirty();
        return false;
    };

    int splitNodeId = -1;
    int combineNodeId = -1;
    std::array<int, 4> cloneNodeIds{ -1, -1, -1, -1 };
    constexpr const char* kChannels[4] = { "r", "g", "b", "a" };
    constexpr float kRowOffsets[4] = { -240.0f, -80.0f, 80.0f, 240.0f };
    try {
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream =
                    m_NodeGraph.FindNode(entry.first)) {
                downstream->position.x =
                    entry.second.x + 620.0f;
            }
        }

        AddChannelSplitNodeAt(EditorNodeGraph::Vec2{
            originalPos.x - 250.0f,
            originalPos.y });
        splitNodeId = m_NodeGraph.GetSelectedNodeId();
        const EditorNodeGraph::Node* splitNode =
            m_NodeGraph.FindNode(splitNodeId);
        if (!splitNode ||
            splitNode->kind !=
                EditorNodeGraph::NodeKind::ChannelSplit ||
            isOriginalNodeId(splitNodeId)) {
            return rollback();
        }
        AddChannelCombineNodeAt(EditorNodeGraph::Vec2{
            originalPos.x + 370.0f,
            originalPos.y });
        combineNodeId = m_NodeGraph.GetSelectedNodeId();
        const EditorNodeGraph::Node* combineNode =
            m_NodeGraph.FindNode(combineNodeId);
        if (!combineNode ||
            combineNode->kind !=
                EditorNodeGraph::NodeKind::ChannelCombine ||
            isOriginalNodeId(combineNodeId)) {
            return rollback();
        }

        for (int i = 0; i < 4; ++i) {
            const int newLayerIndex =
                static_cast<int>(m_Layers.size());
            m_Layers.push_back(cloneLayers[i]);
            EditorNodeGraph::Node* cloneNode =
                m_NodeGraph.AddLayerNode(
                    originalLayerType,
                    newLayerIndex,
                    EditorNodeGraph::Vec2{
                        originalPos.x + 60.0f,
                        originalPos.y + kRowOffsets[i]
                    });
            if (!cloneNode) {
                return rollback();
            }
            cloneNode->typeId = originalTypeId;
            cloneNodeIds[i] = cloneNode->id;
        }

        std::string errorMessage;
        bool ok = ConnectGraphSockets(
            imageInput->fromNodeId,
            imageInput->fromSocketId,
            splitNodeId,
            EditorNodeGraph::kImageInputSocketId,
            &errorMessage);
        for (int i = 0; ok && i < 4; ++i) {
            ok = ConnectGraphSockets(
                splitNodeId,
                kChannels[i],
                cloneNodeIds[i],
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage);
            if (ok) {
                ok = ConnectGraphSockets(
                    cloneNodeIds[i],
                    EditorNodeGraph::kImageOutputSocketId,
                    combineNodeId,
                    kChannels[i],
                    &errorMessage);
            }
        }

        for (const EditorNodeGraph::Link& maskLink :
                maskLinks) {
            for (int cloneNodeId : cloneNodeIds) {
                if (!ok) {
                    break;
                }
                ok = ConnectGraphSockets(
                    maskLink.fromNodeId,
                    maskLink.fromSocketId,
                    cloneNodeId,
                    maskLink.toSocketId,
                    &errorMessage);
            }
            if (!ok) {
                break;
            }
        }

        for (const IndexedOutputLink& outputLink :
                outputLinks) {
            if (!ok) {
                break;
            }
            if (outputLink.link.toNodeId == combineNodeId) {
                continue;
            }
            ok = ConnectGraphSockets(
                combineNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                outputLink.link.toNodeId,
                outputLink.link.toSocketId,
                &errorMessage);
        }

        if (!ok) {
            return rollback();
        }
    } catch (const std::bad_alloc&) {
        return rollback();
    } catch (const std::length_error&) {
        return rollback();
    } catch (const std::exception&) {
        return rollback();
    }

    ClearGraphAutoFocusIfTrackedNode(layerNodeId);
    if (m_CanvasToolOwnerNodeId == layerNodeId) {
        CancelCanvasTool();
    }
    RemoveLayer(originalLayerIndex);
    RefreshGraphLayerMetadata();
    SelectGraphNode(combineNodeId);
    MarkRenderDirty(combineNodeId);
    return true;
}

bool EditorModule::SplitImageAverageNodeIntoChannelAverages(int dataMathNodeId) {
    EditorNodeGraph::Node* averageNode = m_NodeGraph.FindNode(dataMathNodeId);
    if (!averageNode ||
        averageNode->kind != EditorNodeGraph::NodeKind::DataMath ||
        averageNode->dataMathMode != EditorNodeGraph::DataMathMode::ImageAverage) {
        return false;
    }

    struct AverageInputLink {
        std::string socketId;
        EditorNodeGraph::Link link;
    };

    std::vector<AverageInputLink> inputLinks;
    inputLinks.reserve(EditorNodeGraph::kMaxDataMathInputCount);
    for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
        const std::string socketId = EditorNodeGraph::DataMathInputSocketId(inputIndex);
        if (const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(dataMathNodeId, socketId)) {
            if (m_NodeGraph.IsScalarSocketStream(input->fromNodeId, input->fromSocketId)) {
                return false;
            }
            inputLinks.push_back(AverageInputLink{ socketId, *input });
        }
    }
    if (inputLinks.size() < 2) {
        return false;
    }

    const EditorNodeGraph::Vec2 originalPos = averageNode->position;
    struct IndexedOutputLink {
        std::size_t index = 0;
        EditorNodeGraph::Link link;
    };
    std::vector<IndexedOutputLink> outputLinks;
    const std::vector<EditorNodeGraph::Link>& graphLinks =
        m_NodeGraph.GetLinks();
    for (std::size_t linkIndex = 0;
            linkIndex < graphLinks.size();
            ++linkIndex) {
        const EditorNodeGraph::Link& link = graphLinks[linkIndex];
        if (link.fromNodeId == dataMathNodeId &&
            link.fromSocketId ==
                EditorNodeGraph::kImageOutputSocketId) {
            outputLinks.push_back(
                IndexedOutputLink{ linkIndex, link });
        }
    }

    const int nextNodeIdBefore = m_NodeGraph.GetNextNodeId();
    const int outputNodeIdBefore = m_NodeGraph.GetOutputNodeId();
    const int activeImageNodeIdBefore =
        m_NodeGraph.GetActiveImageNodeId();
    const std::vector<int> selectionBefore =
        m_NodeGraph.GetSelectedNodeIds();
    const EditorNodeGraph::Link* selectedLinkBefore =
        m_NodeGraph.GetSelectedLink();
    const std::optional<EditorNodeGraph::Link>
        selectedLinkSnapshot = selectedLinkBefore
            ? std::optional<EditorNodeGraph::Link>(
                *selectedLinkBefore)
            : std::nullopt;

    std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(dataMathNodeId);
    downstreamNodeIds.erase(
        std::remove(downstreamNodeIds.begin(), downstreamNodeIds.end(), dataMathNodeId),
        downstreamNodeIds.end());
    std::vector<std::pair<int, EditorNodeGraph::Vec2>>
        downstreamPositions;
    downstreamPositions.reserve(downstreamNodeIds.size());
    for (const int downstreamNodeId : downstreamNodeIds) {
        if (const EditorNodeGraph::Node* downstream =
                m_NodeGraph.FindNode(downstreamNodeId)) {
            downstreamPositions.emplace_back(
                downstreamNodeId,
                downstream->position);
        }
    }

    std::vector<int> createdNodeIds;
    createdNodeIds.reserve(inputLinks.size() + 5);
    const auto isCreatedNodeId =
        [&createdNodeIds](int nodeId) {
            return std::find(
                       createdNodeIds.begin(),
                       createdNodeIds.end(),
                       nodeId) != createdNodeIds.end();
        };
    const auto linksMatch =
        [](const EditorNodeGraph::Link& lhs,
           const EditorNodeGraph::Link& rhs) {
            return lhs.fromNodeId == rhs.fromNodeId &&
                lhs.fromSocketId == rhs.fromSocketId &&
                lhs.toNodeId == rhs.toNodeId &&
                lhs.toSocketId == rhs.toSocketId;
        };
    const auto rollback = [&]() {
        std::vector<EditorNodeGraph::Link>& links =
            m_NodeGraph.EditLinks();
        links.erase(
            std::remove_if(
                links.begin(),
                links.end(),
                [&](const EditorNodeGraph::Link& link) {
                    if (isCreatedNodeId(link.fromNodeId) ||
                        isCreatedNodeId(link.toNodeId)) {
                        return true;
                    }
                    return std::any_of(
                        outputLinks.begin(),
                        outputLinks.end(),
                        [&](const IndexedOutputLink& original) {
                            return linksMatch(
                                link,
                                original.link);
                        });
                }),
            links.end());
        for (IndexedOutputLink& original : outputLinks) {
            const std::size_t insertIndex = std::min(
                original.index,
                links.size());
            links.insert(
                links.begin() +
                    static_cast<std::ptrdiff_t>(insertIndex),
                std::move(original.link));
        }

        std::vector<EditorNodeGraph::Node>& nodes =
            m_NodeGraph.EditNodes();
        nodes.erase(
            std::remove_if(
                nodes.begin(),
                nodes.end(),
                [&isCreatedNodeId](
                    const EditorNodeGraph::Node& node) {
                    return isCreatedNodeId(node.id);
                }),
            nodes.end());
        for (const auto& entry : downstreamPositions) {
            if (EditorNodeGraph::Node* downstream =
                    m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        m_NodeGraph.SetNextNodeId(nextNodeIdBefore);
        m_NodeGraph.SetOutputNodeId(outputNodeIdBefore);
        m_NodeGraph.SetActiveImageNodeId(
            activeImageNodeIdBefore);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId : selectionBefore) {
            m_NodeGraph.SelectNode(selectedNodeId, true);
        }
        if (selectedLinkSnapshot) {
            const EditorNodeGraph::Link& selected =
                *selectedLinkSnapshot;
            m_NodeGraph.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
        MarkRenderDirty();
        return false;
    };

    std::vector<int> splitNodeIds;
    splitNodeIds.reserve(inputLinks.size());
    for (const auto& entry : downstreamPositions) {
        if (EditorNodeGraph::Node* downstream =
                m_NodeGraph.FindNode(entry.first)) {
            downstream->position.x = entry.second.x + 520.0f;
        }
    }

    std::array<int, 4> averageNodeIds{ -1, -1, -1, -1 };
    constexpr const char* kChannels[4] = { "r", "g", "b", "a" };
    constexpr float kChannelRows[4] = { -210.0f, -70.0f, 70.0f, 210.0f };
    int combineNodeId = -1;
    try {
        const float inputStartY =
            originalPos.y -
            (static_cast<float>(inputLinks.size() - 1) *
             84.0f);
        for (std::size_t inputIndex = 0;
                inputIndex < inputLinks.size();
                ++inputIndex) {
            EditorNodeGraph::Node* splitNode =
                m_NodeGraph.AddChannelSplitNode(
                    EditorNodeGraph::Vec2{
                        originalPos.x - 360.0f,
                        inputStartY +
                            static_cast<float>(inputIndex) *
                                168.0f
                    });
            if (!splitNode) {
                return rollback();
            }
            splitNodeIds.push_back(splitNode->id);
            createdNodeIds.push_back(splitNode->id);
        }

        for (int channelIndex = 0;
                channelIndex < 4;
                ++channelIndex) {
            EditorNodeGraph::Node* channelAverage =
                m_NodeGraph.AddDataMathNode(
                    EditorNodeGraph::DataMathMode::Average,
                    EditorNodeGraph::Vec2{
                        originalPos.x,
                        originalPos.y +
                            kChannelRows[channelIndex]
                    });
            if (!channelAverage) {
                return rollback();
            }
            averageNodeIds[channelIndex] =
                channelAverage->id;
            createdNodeIds.push_back(channelAverage->id);
        }

        EditorNodeGraph::Node* combineNode =
            m_NodeGraph.AddChannelCombineNode(
                EditorNodeGraph::Vec2{
                    originalPos.x + 360.0f,
                    originalPos.y
                });
        if (!combineNode) {
            return rollback();
        }
        combineNodeId = combineNode->id;
        createdNodeIds.push_back(combineNodeId);

        std::string errorMessage;
        bool ok = true;
        for (std::size_t inputIndex = 0;
                ok && inputIndex < inputLinks.size();
                ++inputIndex) {
            const EditorNodeGraph::Link& input =
                inputLinks[inputIndex].link;
            ok = m_NodeGraph.TryConnectSockets(
                input.fromNodeId,
                input.fromSocketId,
                splitNodeIds[inputIndex],
                EditorNodeGraph::kImageInputSocketId,
                &errorMessage);
        }
        for (int channelIndex = 0;
                ok && channelIndex < 4;
                ++channelIndex) {
            for (std::size_t inputIndex = 0;
                    ok && inputIndex < splitNodeIds.size();
                    ++inputIndex) {
                ok = m_NodeGraph.TryConnectSockets(
                    splitNodeIds[inputIndex],
                    kChannels[channelIndex],
                    averageNodeIds[channelIndex],
                    EditorNodeGraph::DataMathInputSocketId(
                        static_cast<int>(inputIndex)),
                    &errorMessage);
            }
            if (ok) {
                ok = m_NodeGraph.TryConnectSockets(
                    averageNodeIds[channelIndex],
                    EditorNodeGraph::kImageOutputSocketId,
                    combineNodeId,
                    kChannels[channelIndex],
                    &errorMessage);
            }
        }
        for (const IndexedOutputLink& outputLink :
                outputLinks) {
            if (!ok) {
                break;
            }
            ok = m_NodeGraph.TryConnectSockets(
                combineNodeId,
                EditorNodeGraph::kImageOutputSocketId,
                outputLink.link.toNodeId,
                outputLink.link.toSocketId,
                &errorMessage);
        }

        if (!ok) {
            return rollback();
        }
    } catch (const std::bad_alloc&) {
        return rollback();
    } catch (const std::length_error&) {
        return rollback();
    } catch (const std::exception&) {
        return rollback();
    }

    if (!m_NodeGraph.RemoveNode(dataMathNodeId)) {
        return rollback();
    }

    SelectGraphNode(combineNodeId);
    MarkRenderDirty(combineNodeId);
    ValidateActiveRawWorkspaceManagedGraph(true);
    return true;
}

bool EditorModule::ToggleOutputNodeEnabled(int outputNodeId) {
    EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(outputNodeId);
    if (!outputNode || outputNode->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }

    const bool enabled = !outputNode->outputEnabled;
    if (!m_NodeGraph.SetOutputNodeEnabled(outputNodeId, enabled)) {
        return false;
    }

    outputNode = m_NodeGraph.FindNode(outputNodeId);
    if (!outputNode) {
        return false;
    }
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*outputNode);

    if (!outputNode->outputEnabled && m_CompositeSelectedOutputNodeId == outputNodeId) {
        int replacementOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
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
    if (!m_NodeGraph.IsOutputConnected()) {
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
    }
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    MarkRenderDirty(outputNodeId);
    return true;
}

bool EditorModule::ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    const std::string fromSocket = from ? m_NodeGraph.DefaultOutputSocket(*from) : std::string();
    const EditorNodeGraph::Node* pendingTo = m_NodeGraph.FindNode(toNodeId);
    const std::string toSocket = pendingTo ? m_NodeGraph.DefaultInputSocket(*pendingTo) : std::string();
    return ConnectGraphSockets(fromNodeId, fromSocket, toNodeId, toSocket, errorMessage);
}

int EditorModule::FindDirectDownstreamToneCurveNode(int sourceNodeId) const {
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            m_NodeGraph.GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        const EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(link.toNodeId);
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
    const std::size_t maxHops = m_NodeGraph.GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        currentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(currentNodeId, 1);
        if (currentNodeId <= 0) {
            return -1;
        }
        const EditorNodeGraph::Node* currentNode = m_NodeGraph.FindNode(currentNodeId);
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
    const std::size_t maxHops = m_NodeGraph.GetNodes().size();
    for (std::size_t hop = 0; hop < maxHops && currentNodeId > 0; ++hop) {
        const EditorNodeGraph::Node* currentNode = m_NodeGraph.FindNode(currentNodeId);
        if (!currentNode) {
            return -1;
        }
        if (currentNode->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            return currentNode->id;
        }
        currentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(currentNodeId, -1);
    }
    return -1;
}

bool EditorModule::RawDevelopNodeUsesIntegratedTone(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    return node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled;
}

bool EditorModule::CanAbsorbDirectDownstreamToneFinishIntoDevelop(int sourceNodeId, std::string* reason) const {
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
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

    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerType != LayerType::ToneCurve) {
        if (reason) {
            *reason = "The direct downstream node is not a Tone Curve layer.";
        }
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        m_NodeGraph.FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        m_NodeGraph.FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);
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
    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
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
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId != sourceNodeId ||
            link.fromSocketId != EditorNodeGraph::kImageOutputSocketId ||
            m_NodeGraph.GetLinkRole(link) != EditorNodeGraph::LinkRole::Render) {
            continue;
        }
        downstreamLinks.push_back(link);
    }

    if (!downstreamLinks.empty()) {
        if (const EditorNodeGraph::Node* firstDownstream = m_NodeGraph.FindNode(downstreamLinks.front().toNodeId)) {
            tonePosition.x = (sourceNode->position.x + firstDownstream->position.x) * 0.5f;
            tonePosition.y = (sourceNode->position.y + firstDownstream->position.y) * 0.5f;
        }
    }

    AddLayerNodeAt(LayerType::ToneCurve, tonePosition);
    const int toneNodeId = m_NodeGraph.GetSelectedNodeId();
    if (toneNodeId <= 0) {
        QueueUiNotification(
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
        QueueUiNotification(
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
            QueueUiNotification(
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
    EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(sourceNodeId);
    if (!sourceNode || sourceNode->kind != EditorNodeGraph::NodeKind::RawDevelop) {
        return false;
    }

    std::string absorbReason;
    if (!CanAbsorbDirectDownstreamToneFinishIntoDevelop(sourceNodeId, &absorbReason)) {
        if (!absorbReason.empty()) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                absorbReason,
                "raw-develop-tone-finish-absorb-unsafe");
        }
        return false;
    }
    const int directToneNodeId = FindDirectDownstreamToneCurveNode(sourceNodeId);

    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(directToneNodeId);
    if (!toneNode ||
        toneNode->kind != EditorNodeGraph::NodeKind::Layer ||
        toneNode->layerIndex < 0 ||
        toneNode->layerIndex >= static_cast<int>(m_Layers.size()) ||
        !m_Layers[toneNode->layerIndex]) {
        return false;
    }

    ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[toneNode->layerIndex].get());
    if (!toneCurve) {
        return false;
    }

    const EditorNodeGraph::Link* toneMaskLink =
        m_NodeGraph.FindAnyInputLink(directToneNodeId, EditorNodeGraph::kMaskInputSocketId);
    const EditorNodeGraph::Link* developMaskLink =
        m_NodeGraph.FindAnyInputLink(sourceNodeId, EditorNodeGraph::kMaskInputSocketId);

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
            QueueUiNotification(
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
    MarkRenderDirty(sourceNodeId);
    return true;
}

bool EditorModule::SelectUpstreamDevelopForToneNode(int toneNodeId) {
    const EditorNodeGraph::Node* toneNode = m_NodeGraph.FindNode(toneNodeId);
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

bool EditorModule::QueueManagedRawGraphMutationConfirmation(
    ManagedRawGraphMutationConfirmAction action,
    Stack::RawWorkspace::ManagedRawGraphMutationWarning warning,
    int nodeId,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId,
    std::vector<int> nodeIds) {
    if (m_ExecutingManagedRawGraphMutationConfirmation ||
        !warning.requiresConfirmation ||
        !IsRawWorkspaceProjectActive() ||
        m_ActiveRawWorkspaceMode != Stack::RawWorkspace::RawProjectMode::ManagedDecomposed) {
        return false;
    }

    if (m_ManagedRawGraphMutationConfirm.action != ManagedRawGraphMutationConfirmAction::None) {
        return true;
    }

    m_ManagedRawGraphMutationConfirm = {};
    m_ManagedRawGraphMutationConfirm.action = action;
    m_ManagedRawGraphMutationConfirm.openPopup = true;
    m_ManagedRawGraphMutationConfirm.nodeId = nodeId;
    m_ManagedRawGraphMutationConfirm.fromNodeId = fromNodeId;
    m_ManagedRawGraphMutationConfirm.fromSocketId = fromSocketId;
    m_ManagedRawGraphMutationConfirm.toNodeId = toNodeId;
    m_ManagedRawGraphMutationConfirm.toSocketId = toSocketId;
    m_ManagedRawGraphMutationConfirm.nodeIds = std::move(nodeIds);
    m_ManagedRawGraphMutationConfirm.warning = std::move(warning);
    return true;
}

void EditorModule::ExecuteManagedRawGraphMutationConfirmation() {
    ManagedRawGraphMutationConfirmState pending = std::move(m_ManagedRawGraphMutationConfirm);
    m_ManagedRawGraphMutationConfirm = {};
    if (pending.action == ManagedRawGraphMutationConfirmAction::None) {
        return;
    }

    m_ExecutingManagedRawGraphMutationConfirmation = true;
    switch (pending.action) {
        case ManagedRawGraphMutationConfirmAction::Connect: {
            std::string errorMessage;
            if (!ConnectGraphSockets(
                    pending.fromNodeId,
                    pending.fromSocketId,
                    pending.toNodeId,
                    pending.toSocketId,
                    &errorMessage) &&
                !errorMessage.empty()) {
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    errorMessage,
                    "raw-workspace-managed-confirm-connect");
            }
            break;
        }
        case ManagedRawGraphMutationConfirmAction::RemoveLink:
            RemoveGraphLink(
                pending.fromNodeId,
                pending.fromSocketId,
                pending.toNodeId,
                pending.toSocketId);
            break;
        case ManagedRawGraphMutationConfirmAction::RemoveNode:
            RemoveGraphNode(pending.nodeId);
            break;
        case ManagedRawGraphMutationConfirmAction::RemoveNodes: {
            std::vector<int> nodeIds = std::move(pending.nodeIds);
            std::sort(nodeIds.begin(), nodeIds.end(), [this](int a, int b) {
                const EditorNodeGraph::Node* nodeA = m_NodeGraph.FindNode(a);
                const EditorNodeGraph::Node* nodeB = m_NodeGraph.FindNode(b);
                const int layerA = nodeA && nodeA->kind == EditorNodeGraph::NodeKind::Layer ? nodeA->layerIndex : -1;
                const int layerB = nodeB && nodeB->kind == EditorNodeGraph::NodeKind::Layer ? nodeB->layerIndex : -1;
                return layerA > layerB;
            });
            bool removedAny = false;
            for (int id : nodeIds) {
                removedAny = RemoveGraphNode(id) || removedAny;
            }
            m_NodeGraph.ClearSelection();
            RefreshGraphLayerMetadata();
            if (removedAny) {
                MarkRenderDirty();
            }
            break;
        }
        case ManagedRawGraphMutationConfirmAction::None:
            break;
    }
    m_ExecutingManagedRawGraphMutationConfirmation = false;
}

void EditorModule::RenderManagedRawGraphMutationConfirmPopup() {
    constexpr const char* kPopupName = "Managed RAW Graph Change##Editor";
    if (m_ManagedRawGraphMutationConfirm.openPopup) {
        ImGui::OpenPopup(kPopupName);
        m_ManagedRawGraphMutationConfirm.openPopup = false;
    }

    if (ImGui::BeginPopupModal(kPopupName, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string summary = m_ManagedRawGraphMutationConfirm.warning.summary.empty()
            ? std::string("This graph change affects the managed RAW chain.")
            : m_ManagedRawGraphMutationConfirm.warning.summary;
        const std::string detail = m_ManagedRawGraphMutationConfirm.warning.detail.empty()
            ? std::string("Continuing will switch this image to Custom Graph Mode. RAW tab editing will become read-only until the chain is repaired or re-adopted.")
            : m_ManagedRawGraphMutationConfirm.warning.detail;

        ImGui::TextWrapped("%s", summary.c_str());
        ImGui::Spacing();
        ImGui::TextWrapped("%s", detail.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Continue", ImVec2(120.0f, 0.0f))) {
            ExecuteManagedRawGraphMutationConfirmation();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
            m_ManagedRawGraphMutationConfirm = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

bool EditorModule::ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    if (QueueManagedRawGraphMutationConfirmation(
            ManagedRawGraphMutationConfirmAction::Connect,
            Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                m_ActiveManagedRawSection,
                fromNodeId,
                fromSocketId,
                toNodeId,
                toSocketId),
            0,
            fromNodeId,
            fromSocketId,
            toNodeId,
            toSocketId)) {
        return false;
    }

    const EditorNodeGraph::Node* targetNode = m_NodeGraph.FindNode(toNodeId);
    if (targetNode &&
        targetNode->kind == EditorNodeGraph::NodeKind::Output &&
        toSocketId == EditorNodeGraph::kImageInputSocketId &&
        fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
        const EditorNodeGraph::ScenePathInfo scenePath =
            EditorNodeGraph::AnalyzeScenePath(m_NodeGraph, fromNodeId);
        nlohmann::json inheritedViewTransform;
        bool compactRawViewTransformEnabled = false;
        int upstreamNodeId = fromNodeId;
        const std::size_t maxUpstreamHops = m_NodeGraph.GetNodes().size();
        for (std::size_t hop = 0;
             hop < maxUpstreamHops && upstreamNodeId > 0;
             ++hop) {
            const EditorNodeGraph::Node* upstreamNode =
                m_NodeGraph.FindNode(upstreamNodeId);
            if (!upstreamNode) {
                break;
            }
            if (upstreamNode->kind == EditorNodeGraph::NodeKind::RawDevelopment) {
                const auto& rawRecipe = upstreamNode->rawDevelopment.recipe;
                compactRawViewTransformEnabled =
                    Stack::RawRecipe::IsViewTransformEnabled(rawRecipe);
                inheritedViewTransform = rawRecipe.viewTransform.layerJson;
                if (inheritedViewTransform.is_object()) {
                    inheritedViewTransform.erase("enabled");
                }
                break;
            }
            upstreamNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(
                upstreamNodeId,
                -1);
        }
        if (scenePath.sceneReferred &&
            !scenePath.hasViewTransform &&
            !compactRawViewTransformEnabled) {
            const EditorNodeGraph::Vec2 fromPosition = from ? from->position : EditorNodeGraph::Vec2{};
            const EditorNodeGraph::Vec2 toPosition = targetNode->position;
            const EditorNodeGraph::Vec2 viewPosition{
                (fromPosition.x + toPosition.x) * 0.5f,
                (fromPosition.y + toPosition.y) * 0.5f
            };

            const std::vector<int> selectionBefore =
                m_NodeGraph.GetSelectedNodeIds();
            const EditorNodeGraph::Link* selectedLinkBefore =
                m_NodeGraph.GetSelectedLink();
            const std::optional<EditorNodeGraph::Link>
                selectedLinkSnapshot = selectedLinkBefore
                    ? std::optional<EditorNodeGraph::Link>(
                        *selectedLinkBefore)
                    : std::nullopt;
            const int selectedLayerBefore = m_SelectedLayerIndex;
            const bool focusSelectedTabBefore =
                m_FocusSelectedTabNextRender;
            const std::size_t layerCountBefore = m_Layers.size();
            int viewNodeId = -1;
            const auto rollbackViewInsertion = [&]() {
                while (m_Layers.size() > layerCountBefore) {
                    RemoveLayer(
                        static_cast<int>(m_Layers.size() - 1u));
                }
                m_NodeGraph.ClearSelection();
                for (const int selectedNodeId : selectionBefore) {
                    m_NodeGraph.SelectNode(selectedNodeId, true);
                }
                if (selectedLinkSnapshot) {
                    const EditorNodeGraph::Link& link =
                        *selectedLinkSnapshot;
                    m_NodeGraph.SelectLink(
                        link.fromNodeId,
                        link.fromSocketId,
                        link.toNodeId,
                        link.toSocketId);
                }
                m_SelectedLayerIndex = selectedLayerBefore;
                m_FocusSelectedTabNextRender =
                    focusSelectedTabBefore;
            };

            try {
                AddLayerNodeAt(
                    LayerType::ViewTransform,
                    viewPosition);
                const EditorNodeGraph::Node* viewNode =
                    m_NodeGraph.FindNodeByLayerIndex(
                        static_cast<int>(layerCountBefore));
                if (m_Layers.size() != layerCountBefore + 1u ||
                    !viewNode ||
                    viewNode->layerType != LayerType::ViewTransform) {
                    rollbackViewInsertion();
                    SetGraphMutationErrorNoThrow(
                        errorMessage,
                        "Could not create View Transform node.");
                    return false;
                }
                viewNodeId = viewNode->id;
                if (inheritedViewTransform.is_object() &&
                    layerCountBefore < m_Layers.size() &&
                    m_Layers[layerCountBefore]) {
                    m_Layers[layerCountBefore]->Deserialize(
                        inheritedViewTransform);
                }
                if (!m_NodeGraph.TryConnectSockets(
                        fromNodeId,
                        fromSocketId,
                        viewNodeId,
                        EditorNodeGraph::kImageInputSocketId,
                        errorMessage) ||
                    !m_NodeGraph.TryConnectSockets(
                        viewNodeId,
                        EditorNodeGraph::kImageOutputSocketId,
                        toNodeId,
                        toSocketId,
                        errorMessage)) {
                    rollbackViewInsertion();
                    return false;
                }
            } catch (const std::bad_alloc&) {
                rollbackViewInsertion();
                SetGraphMutationErrorNoThrow(
                    errorMessage,
                    "View Transform insertion failed because memory is exhausted.");
                return false;
            } catch (const std::length_error&) {
                rollbackViewInsertion();
                SetGraphMutationErrorNoThrow(
                    errorMessage,
                    "View Transform insertion reached the graph size limit.");
                return false;
            } catch (const std::exception&) {
                rollbackViewInsertion();
                SetGraphMutationErrorNoThrow(
                    errorMessage,
                    "View Transform insertion could not apply its inherited RAW settings.");
                return false;
            } catch (...) {
                rollbackViewInsertion();
                throw;
            }

            ApplyGraphLayerOrder();
            MarkRenderDirty();
            SelectGraphNode(viewNodeId);
            ValidateActiveRawWorkspaceManagedGraph(true);
            return true;
        }
    }

    if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, errorMessage)) {
        return false;
    }

    if (from && from->kind == EditorNodeGraph::NodeKind::Image &&
        ConnectionUsesImageAsRenderSource(m_NodeGraph, fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        LoadSourceFromPixels(from->image.pixels.data(), from->image.width, from->image.height, from->image.channels);
        m_NodeGraph.SetActiveImageNodeId(fromNodeId);
        MarkNodeBrowserThumbnailSourceChanged();
    }

    ApplyGraphLayerOrder();
    MarkRenderDirty();
    const EditorNodeGraph::Node* to = m_NodeGraph.FindNode(toNodeId);
    if (to && to->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectGraphNode(toNodeId);
    } else if (from) {
        SelectGraphNode(fromNodeId);
    }
    ValidateActiveRawWorkspaceManagedGraph(true);
    return true;
}

bool EditorModule::OutputPathNeedsViewTransform(int outputNodeId) const {
    const EditorNodeGraph::Node* output = m_NodeGraph.FindNode(outputNodeId);
    if (!output || output->kind != EditorNodeGraph::NodeKind::Output) {
        return false;
    }
    if (m_NodeGraph.IsOutputChannelInspection(outputNodeId)) {
        return false;
    }
    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }
    const EditorNodeGraph::ScenePathInfo scenePath =
        EditorNodeGraph::AnalyzeScenePath(m_NodeGraph, input->fromNodeId);
    return scenePath.sceneReferred && !scenePath.hasViewTransform;
}

bool EditorModule::SelectedLayerInputContainsViewTransform() const {
    if (m_SelectedLayerIndex < 0) {
        return false;
    }
    const EditorNodeGraph::Node* selectedNode = m_NodeGraph.FindNodeByLayerIndex(m_SelectedLayerIndex);
    if (!selectedNode || selectedNode->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }

    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(selectedNode->id, EditorNodeGraph::kImageInputSocketId);
    return input &&
        EditorNodeGraph::AnalyzeScenePath(m_NodeGraph, input->fromNodeId)
            .hasViewTransform;
}

bool EditorModule::RenderLayerControlsWithDirtyTracking(
    EditorNodeGraph::Node& node,
    const std::function<void(LayerBase&)>& renderControls) {
    if (node.kind != EditorNodeGraph::NodeKind::Layer ||
        node.layerIndex < 0 ||
        node.layerIndex >= static_cast<int>(m_Layers.size()) ||
        !m_Layers[node.layerIndex]) {
        return false;
    }

    LayerBase& layer = *m_Layers[node.layerIndex];
    const nlohmann::json before = layer.Serialize();
    const bool beforeEnabled = layer.IsEnabled();
    const bool beforeVisible = layer.IsVisible();
    renderControls(layer);
    const nlohmann::json after = layer.Serialize();
    if (before != after ||
        beforeEnabled != layer.IsEnabled() ||
        beforeVisible != layer.IsVisible()) {
        if (before != after) {
            UpdateTimelineExistingKeyframesForLayerEdit(node, before, after);
        }
        MarkRenderDirty(node.id);
        return true;
    }
    return false;
}

void EditorModule::MarkSelectedLayerRenderDirty() {
    if (m_SelectedLayerIndex >= 0) {
        if (const EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(m_SelectedLayerIndex)) {
            MarkRenderDirty(node->id);
            return;
        }
    }
    MarkRenderDirty();
}

bool EditorModule::RemoveGraphLink(int fromNodeId, int toNodeId) {
    const EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = m_NodeGraph.FindNode(toNodeId);
    const std::string fromSocketId = from ? m_NodeGraph.DefaultOutputSocket(*from) : std::string();
    const std::string toSocketId = to ? m_NodeGraph.DefaultInputSocket(*to) : std::string();
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == fromNodeId && link.toNodeId == toNodeId &&
            link.ownership == EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "MFD frame links are managed by the project. Exclude or remove the frame from RAW Lab instead.",
                "mfd-managed-link-protected");
            return false;
        }
    }
    if (QueueManagedRawGraphMutationConfirmation(
            ManagedRawGraphMutationConfirmAction::RemoveLink,
            Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                m_ActiveManagedRawSection,
                fromNodeId,
                fromSocketId,
                toNodeId,
                toSocketId),
            0,
            fromNodeId,
            fromSocketId,
            toNodeId,
            toSocketId)) {
        return false;
    }

    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, toNodeId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
        ValidateActiveRawWorkspaceManagedGraph(true);
    }
    return removed;
}

bool EditorModule::GraphLinkRequiresManagedRawConfirmation(
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) const {
    return Stack::RawWorkspace::
        BuildManagedRawGraphLinkRemovalWarning(
            m_ActiveManagedRawSection,
            fromNodeId,
            fromSocketId,
            toNodeId,
            toSocketId)
            .requiresConfirmation;
}

bool EditorModule::RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == fromNodeId && link.fromSocketId == fromSocketId &&
            link.toNodeId == toNodeId && link.toSocketId == toSocketId &&
            link.ownership == EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "MFD frame links are managed by the project. Exclude or remove the frame from RAW Lab instead.",
                "mfd-managed-link-protected");
            return false;
        }
    }
    if (QueueManagedRawGraphMutationConfirmation(
            ManagedRawGraphMutationConfirmAction::RemoveLink,
            Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                m_ActiveManagedRawSection,
                fromNodeId,
                fromSocketId,
                toNodeId,
                toSocketId),
            0,
            fromNodeId,
            fromSocketId,
            toNodeId,
            toSocketId)) {
        return false;
    }

    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
        ValidateActiveRawWorkspaceManagedGraph(true);
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphLink() {
    if (const EditorNodeGraph::Link* selected = m_NodeGraph.GetSelectedLink()) {
        if (selected->ownership ==
            EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "MFD frame links are managed by the project. Exclude or remove the frame from RAW Lab instead.",
                "mfd-managed-link-protected");
            return false;
        }
        if (QueueManagedRawGraphMutationConfirmation(
                ManagedRawGraphMutationConfirmAction::RemoveLink,
                Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                    m_ActiveManagedRawSection,
                    selected->fromNodeId,
                    selected->fromSocketId,
                    selected->toNodeId,
                    selected->toSocketId),
                0,
                selected->fromNodeId,
                selected->fromSocketId,
                selected->toNodeId,
                selected->toSocketId)) {
            return false;
        }
    }

    const bool removed = m_NodeGraph.RemoveSelectedLink();
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
        ValidateActiveRawWorkspaceManagedGraph(true);
    }
    return removed;
}

bool EditorModule::RemoveGraphNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawProjectSourceSet &&
        node->rawProjectSourceSet.managed) {
        m_PendingDeleteMultiFrameSourceSetId =
            node->rawProjectSourceSet.sourceSetId;
        m_OpenMultiFrameDeletePopup = true;
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Confirm deletion to remove the source set, managed node, and downstream links together.",
            "raw-project-source-set-delete-confirm");
        return false;
    }
    if (node->kind == EditorNodeGraph::NodeKind::MultiFrameDenoise &&
        node->multiFrameDenoise.managed) {
        m_PendingDeleteMultiFrameSourceSetId =
            node->multiFrameDenoise.sourceSetId;
        m_OpenMultiFrameDeletePopup = true;
        RequestOpenRawLabTab();
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Confirm deletion to remove the MFD burst, its frame nodes, and downstream links together.",
            "mfd-delete-confirm");
        return false;
    }
    if (node->kind == EditorNodeGraph::NodeKind::RawProjectFrame &&
        node->rawProjectFrame.managed) {
        m_PendingDeleteMultiFrameFrameSetId =
            node->rawProjectFrame.sourceSetId;
        m_PendingDeleteMultiFrameFrameId = node->rawProjectFrame.frameId;
        m_OpenMultiFrameFrameDeletePopup = true;
        RequestOpenRawLabTab();
        QueueUiNotification(
            UiNotificationSeverity::Info,
            "Confirm removal of this frame from the MFD project in RAW Lab.",
            "mfd-frame-delete-confirm");
        return false;
    }

    if (IsRawWorkspaceProjectActive() &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelopment) {
        const std::string& sourceKey =
            node->rawDevelopment.recipe.source.relativePathKey;
        if (sourceKey.empty() || sourceKey == m_ActiveRawWorkspaceSourceKey) {
            QueueUiNotification(
                UiNotificationSeverity::Info,
                "The RAW Development node owns this RAW project and cannot be deleted. You can freely edit or replace nodes downstream from it.",
                "raw-workspace-owner-node-delete");
            return false;
        }
    }

    if (QueueManagedRawGraphMutationConfirmation(
            ManagedRawGraphMutationConfirmAction::RemoveNode,
            Stack::RawWorkspace::BuildManagedRawGraphNodeRemovalWarning(
                m_ActiveManagedRawSection,
                nodeId),
            nodeId)) {
        return false;
    }

    const std::vector<GraphReconnectPlan> reconnectPlans =
        BuildReconnectPlansForNodeRemoval(m_NodeGraph, *node);

    ClearGraphAutoFocusIfTrackedNode(nodeId);
    if (m_CanvasToolOwnerNodeId == nodeId) {
        CancelCanvasTool();
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer) {
        const int layerIndex = node->layerIndex;
        RemoveLayer(node->layerIndex);
        for (const GraphReconnectPlan& plan : reconnectPlans) {
            std::string errorMessage;
            ConnectGraphSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &errorMessage);
        }
        if (layerIndex >= 0) {
            RefreshGraphLayerMetadata();
        }
        ValidateActiveRawWorkspaceManagedGraph(true);
        return true;
    }

    const bool removed = m_NodeGraph.RemoveNode(nodeId);
    if (removed && !m_NodeGraph.IsOutputConnected()) {
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
    }
    if (removed) {
        for (const GraphReconnectPlan& plan : reconnectPlans) {
            std::string errorMessage;
            ConnectGraphSockets(plan.fromNodeId, plan.fromSocketId, plan.toNodeId, plan.toSocketId, &errorMessage);
        }
        MarkRenderDirty();
        ValidateActiveRawWorkspaceManagedGraph(true);
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphNodes() {
    std::vector<int> nodeIds = m_NodeGraph.GetSelectedNodeIds();
    if (nodeIds.empty()) {
        return false;
    }

    Stack::RawWorkspace::ManagedRawGraphMutationWarning warning;
    int managedNodeCount = 0;
    for (int nodeId : nodeIds) {
        Stack::RawWorkspace::ManagedRawGraphMutationWarning candidate =
            Stack::RawWorkspace::BuildManagedRawGraphNodeRemovalWarning(
                m_ActiveManagedRawSection,
                nodeId);
        if (!candidate.requiresConfirmation) {
            continue;
        }
        ++managedNodeCount;
        if (!warning.requiresConfirmation) {
            warning = std::move(candidate);
        }
    }
    if (managedNodeCount > 1) {
        warning.requiresConfirmation = true;
        warning.summary = "Selected nodes include managed RAW chain nodes.";
        warning.detail = "Removing them will switch this image to Custom Graph Mode and make RAW tab editing read-only until the chain is repaired or re-adopted.";
    }
    if (QueueManagedRawGraphMutationConfirmation(
            ManagedRawGraphMutationConfirmAction::RemoveNodes,
            std::move(warning),
            0,
            0,
            {},
            0,
            {},
            nodeIds)) {
        return false;
    }

    std::sort(nodeIds.begin(), nodeIds.end(), [this](int a, int b) {
        const EditorNodeGraph::Node* nodeA = m_NodeGraph.FindNode(a);
        const EditorNodeGraph::Node* nodeB = m_NodeGraph.FindNode(b);
        const int layerA = nodeA && nodeA->kind == EditorNodeGraph::NodeKind::Layer ? nodeA->layerIndex : -1;
        const int layerB = nodeB && nodeB->kind == EditorNodeGraph::NodeKind::Layer ? nodeB->layerIndex : -1;
        return layerA > layerB;
    });

    bool removedAny = false;
    for (int nodeId : nodeIds) {
        removedAny = RemoveGraphNode(nodeId) || removedAny;
    }
    m_NodeGraph.ClearSelection();
    RefreshGraphLayerMetadata();
    if (removedAny) {
        MarkRenderDirty();
    }
    return removedAny;
}

void EditorModule::AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageGeneratorNode(generatorKind, graphPosition)) {
        const int nodeId = node->id;
        SelectGraphNode(nodeId);
        if (GetConnectedOutputCount() == 0) {
            const int nextNodeIdBeforeOutput =
                m_NodeGraph.GetNextNodeId();
            const int primaryOutputBefore =
                m_NodeGraph.GetOutputNodeId();
            EditorNodeGraph::Node* outputNode = nullptr;
            try {
                outputNode = m_NodeGraph.AddOutputNode(
                    EditorNodeGraph::Vec2{
                        graphPosition.x + 330.0f,
                        graphPosition.y
                    });
            } catch (const std::bad_alloc&) {
                m_NodeGraph.SetNextNodeId(
                    nextNodeIdBeforeOutput);
                m_NodeGraph.SetOutputNodeId(
                    primaryOutputBefore);
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "The automatic Output could not be created because memory is exhausted.",
                    "generator-auto-output");
            } catch (const std::length_error&) {
                m_NodeGraph.SetNextNodeId(
                    nextNodeIdBeforeOutput);
                m_NodeGraph.SetOutputNodeId(
                    primaryOutputBefore);
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    "The automatic Output could not be created because the graph size limit was reached.",
                    "generator-auto-output");
            }
            if (outputNode) {
                const int outputNodeId = outputNode->id;
                std::string errorMessage;
                bool connected = false;
                try {
                    connected = ConnectGraphNodes(
                        nodeId,
                        outputNodeId,
                        &errorMessage);
                } catch (const std::bad_alloc&) {
                    SetGraphMutationErrorNoThrow(
                        &errorMessage,
                        "The automatic Output connection failed because memory is exhausted.");
                } catch (const std::length_error&) {
                    SetGraphMutationErrorNoThrow(
                        &errorMessage,
                        "The automatic Output connection reached the graph size limit.");
                }
                if (!connected) {
                    m_NodeGraph.RemoveNode(outputNodeId);
                    m_NodeGraph.SetNextNodeId(
                        nextNodeIdBeforeOutput);
                    m_NodeGraph.SetOutputNodeId(
                        primaryOutputBefore);
                    SelectGraphNode(nodeId);
                    if (!errorMessage.empty()) {
                        QueueUiNotification(
                            UiNotificationSeverity::Error,
                            errorMessage,
                            "generator-auto-output");
                    }
                } else if (GetCompletedChainCount() == 1 &&
                           m_Pipeline.GetSourcePixelsRaw().empty()) {
                    EnterSingleOutputPreviewMode();
                }
            }
        }
        MarkRenderDirty();
    }
}

void EditorModule::AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMixNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddDataMathNodeAt(EditorNodeGraph::DataMathMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddDataMathNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddValueNodeAt(
    Stack::NodeMath::FirstClassValue value,
    EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddValueNode(std::move(value), graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddFieldMeanNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddFieldMeanNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddReformatNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddReformatNode(graphPosition)) {
        MarkRenderDirty(node->id);
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddTechnicalImageNodeAt(
    Stack::NodeMath::TechnicalImageOperation operation,
    EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddTechnicalImageNode(operation, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
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
    for (const Stack::NodeMath::DefinitionReference& reference : closure) {
        const Stack::NodeMath::CompoundDefinition* dependency =
            Stack::NodeMath::FindExactCompoundDefinition(templates, reference);
        std::string definitionError;
        if (dependency && !m_NodeGraph.AddCompoundDefinition(*dependency, &definitionError)) {
            ShowUiNotification(UiNotificationSeverity::Error, definitionError, "compound-add");
            return;
        }
    }
    if (EditorNodeGraph::Node* node =
            m_NodeGraph.AddCompoundNode(definition->identity, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

bool EditorModule::MakeCompoundNodeUnique(int nodeId, std::string* error) {
    if (!m_NodeGraph.MakeCompoundNodeUnique(nodeId, error)) return false;
    SelectGraphNode(nodeId);
    MarkRenderDirty(nodeId);
    return true;
}

bool EditorModule::UnpackCompoundNode(int nodeId, std::string* error) {
    std::vector<int> unpacked;
    if (!m_NodeGraph.UnpackCompoundNode(nodeId, &unpacked, error)) return false;
    MarkRenderDirty();
    return true;
}

bool EditorModule::UpdateCompoundNodeToLatestEmbeddedVersion(int nodeId, std::string* error) {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
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
            m_NodeGraph.GetCompoundDefinitions()) {
        if (candidate.identity.id != node->compound.instance.definition.id ||
            !greater(candidate.identity.version, node->compound.instance.definition.version)) continue;
        if (!latest || greater(candidate.identity.version, latest->identity.version)) latest = &candidate;
    }
    if (!latest) {
        if (error) *error = "No newer exact embedded definition is available.";
        return false;
    }
    const Stack::NodeMath::DefinitionReference reference = latest->identity;
    if (!m_NodeGraph.UpdateCompoundNodeDefinition(nodeId, reference, error)) return false;
    MarkRenderDirty(nodeId);
    return true;
}

bool EditorModule::CreateCompoundFromSelection(const std::string& label, std::string* error) {
    int compoundNodeId = -1;
    if (!m_NodeGraph.CreateCompoundFromSelection(
            m_NodeGraph.GetSelectedNodeIds(), label, &compoundNodeId, error)) return false;
    SelectGraphNode(compoundNodeId);
    MarkRenderDirty(compoundNodeId);
    return true;
}

void EditorModule::AddFrequencyFilterNodeAt(EditorNodeGraph::FrequencyFilterMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddFrequencyFilterNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddFrequencyResponseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddFrequencyResponseNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddFrequencyFftNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddFrequencyFftNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddFrequencyIfftNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddFrequencyIfftNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddSpectrumViewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddSpectrumViewNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddApplyFrequencyResponseNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddApplyFrequencyResponseNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddCombineSpectraNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddCombineSpectraNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddSpectrumSeparateNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddSpectrumSeparateNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddSpectrumRecombineNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddSpectrumRecombineNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

bool EditorModule::SetFrequencyParameterExposed(
    int nodeId,
    const std::string& parameterId,
    bool exposed) {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
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
    if (!m_NodeGraph.SetParameterExposed(nodeId, parameterId, exposed)) return false;
    MarkRenderDirty(nodeId);
    return true;
}

bool EditorModule::ApplyFrequencyGraphHistoryPatch(
    FrequencyGraphHistoryPatch& patch,
    bool applyAfter) {
    const std::uint64_t expectedRevision =
        applyAfter
            ? patch.expectedBeforeRevision
            : patch.expectedAfterRevision;
    if (m_NodeGraph.GetStructureRevision() !=
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
        if (!m_NodeGraph.FindNode(item.node.id)) {
            return false;
        }
    }
    for (const FrequencyGraphHistoryNode& item : targetNodes) {
        if (m_NodeGraph.FindNode(item.node.id)) {
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
        m_NodeGraph.GetNodes().max_size() -
            m_NodeGraph.GetNodes().size()) {
        return false;
    }
    if (preparedLinks.size() >
        m_NodeGraph.GetLinks().max_size() -
            m_NodeGraph.GetLinks().size()) {
        return false;
    }
    m_NodeGraph.EditNodes().reserve(
        m_NodeGraph.GetNodes().size() +
        preparedNodes.size());
    m_NodeGraph.EditLinks().reserve(
        m_NodeGraph.GetLinks().size() +
        preparedLinks.size());

    for (const FrequencyGraphHistoryNode& item : removeNodes) {
        if (!m_NodeGraph.RemoveNode(item.node.id)) {
            return false;
        }
    }
    for (FrequencyGraphHistoryNode& item : preparedNodes) {
        std::vector<EditorNodeGraph::Node>& nodes =
            m_NodeGraph.EditNodes();
        const std::size_t insertionIndex =
            std::min(item.index, nodes.size());
        nodes.insert(
            nodes.begin() +
                static_cast<std::ptrdiff_t>(insertionIndex),
            std::move(item.node));
    }
    for (FrequencyGraphHistoryLink& item : preparedLinks) {
        std::vector<EditorNodeGraph::Link>& links =
            m_NodeGraph.EditLinks();
        const std::size_t insertionIndex =
            std::min(item.index, links.size());
        links.insert(
            links.begin() +
                static_cast<std::ptrdiff_t>(insertionIndex),
            std::move(item.link));
    }
    m_NodeGraph.SetNextNodeId(targetNextNodeId);
    m_NodeGraph.ClearSelection();
    for (const int nodeId : preparedSelection) {
        m_NodeGraph.SelectNode(nodeId, true);
    }
    if (targetSelectedLink) {
        m_NodeGraph.SelectLink(
            targetSelectedLink->fromNodeId,
            targetSelectedLink->fromSocketId,
            targetSelectedLink->toNodeId,
            targetSelectedLink->toSocketId);
    }
    if (applyAfter) {
        patch.expectedAfterRevision =
            m_NodeGraph.GetStructureRevision();
    } else {
        patch.expectedBeforeRevision =
            m_NodeGraph.GetStructureRevision();
    }
    return true;
}

bool EditorModule::ExtractFrequencyResponseNode(
    int filterNodeId,
    std::string* error) {
    const EditorNodeGraph::Node* filter = m_NodeGraph.FindNode(filterNodeId);
    if (filter == nullptr ||
        filter->kind != EditorNodeGraph::NodeKind::FrequencyFilter) {
        if (error) *error = "Extract Response Node requires a Frequency Filter.";
        return false;
    }
    if (m_NodeGraph.FindAnyInputLink(
            filterNodeId, EditorNodeGraph::kFrequencyResponseInputSocketId) != nullptr) {
        if (error) *error = "This Frequency Filter already has a connected Response.";
        return false;
    }

    FrequencyGraphHistoryPatch patch;
    patch.beforeNextNodeId = m_NodeGraph.GetNextNodeId();
    patch.expectedBeforeRevision =
        m_NodeGraph.GetStructureRevision();
    patch.beforeSelection =
        m_NodeGraph.GetSelectedNodeIds();
    if (const EditorNodeGraph::Link* selected =
            m_NodeGraph.GetSelectedLink()) {
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
            m_NodeGraph.RemoveNode(responseNodeId);
        }
        m_NodeGraph.SetNextNodeId(
            patch.beforeNextNodeId);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId :
             patch.beforeSelection) {
            m_NodeGraph.SelectNode(
                selectedNodeId,
                true);
        }
        if (patch.beforeSelectedLink) {
            const EditorNodeGraph::Link& selected =
                *patch.beforeSelectedLink;
            m_NodeGraph.SelectLink(
                selected.fromNodeId,
                selected.fromSocketId,
                selected.toNodeId,
                selected.toSocketId);
        }
    };

    try {
        EditorNodeGraph::Node* response =
            m_NodeGraph.AddFrequencyResponseNode(
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
        if (!m_NodeGraph.TryConnectSockets(
                responseNodeId,
                EditorNodeGraph::kFrequencyResponseOutputSocketId,
                filterNodeId,
                EditorNodeGraph::kFrequencyResponseInputSocketId,
                &connectionError)) {
            rollbackExtraction();
            if (error) *error = std::move(connectionError);
            return false;
        }
        SelectGraphNode(responseNodeId);

        const auto responseIt = std::find_if(
            m_NodeGraph.GetNodes().begin(),
            m_NodeGraph.GetNodes().end(),
            [responseNodeId](
                const EditorNodeGraph::Node& node) {
                return node.id == responseNodeId;
            });
        const EditorNodeGraph::Link* responseLink =
            m_NodeGraph.FindAnyInputLink(
                filterNodeId,
                EditorNodeGraph::kFrequencyResponseInputSocketId);
        if (responseIt == m_NodeGraph.GetNodes().end() ||
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
                    m_NodeGraph.GetNodes().begin(),
                    responseIt)),
            *responseIt
        });
        const auto responseLinkIt = std::find_if(
            m_NodeGraph.GetLinks().begin(),
            m_NodeGraph.GetLinks().end(),
            [responseLink](
                const EditorNodeGraph::Link& link) {
                return &link == responseLink;
            });
        patch.afterLinks.push_back({
            static_cast<std::size_t>(
                std::distance(
                    m_NodeGraph.GetLinks().begin(),
                    responseLinkIt)),
            *responseLink
        });
        patch.afterNextNodeId =
            m_NodeGraph.GetNextNodeId();
        patch.expectedAfterRevision =
            m_NodeGraph.GetStructureRevision();
        patch.afterSelection =
            m_NodeGraph.GetSelectedNodeIds();
        if (const EditorNodeGraph::Link* selected =
                m_NodeGraph.GetSelectedLink()) {
            patch.afterSelectedLink = *selected;
        }
        m_FrequencyGraphUndo = std::move(patch);
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
    m_FrequencyGraphRedo.reset();
    MarkRenderDirty(filterNodeId);
    return true;
}

bool EditorModule::ExpandFrequencyFilterNode(
    int filterNodeId,
    std::string* error) {
    const EditorNodeGraph::Node* filter = m_NodeGraph.FindNode(filterNodeId);
    if (filter == nullptr ||
        filter->kind != EditorNodeGraph::NodeKind::FrequencyFilter) {
        if (error) *error = "Expand requires a Frequency Filter node.";
        return false;
    }

    FrequencyGraphHistoryPatch patch;
    patch.beforeNextNodeId =
        m_NodeGraph.GetNextNodeId();
    patch.expectedBeforeRevision =
        m_NodeGraph.GetStructureRevision();
    patch.beforeSelection =
        m_NodeGraph.GetSelectedNodeIds();
    if (const EditorNodeGraph::Link* selected =
            m_NodeGraph.GetSelectedLink()) {
        patch.beforeSelectedLink = *selected;
    }
    const auto filterIt = std::find_if(
        m_NodeGraph.GetNodes().begin(),
        m_NodeGraph.GetNodes().end(),
        [filterNodeId](
            const EditorNodeGraph::Node& node) {
            return node.id == filterNodeId;
        });
    if (filterIt == m_NodeGraph.GetNodes().end()) {
        if (error) *error = "Could not locate the original Frequency Filter.";
        return false;
    }
    patch.beforeNodes.push_back({
        static_cast<std::size_t>(
            std::distance(
                m_NodeGraph.GetNodes().begin(),
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
         linkIndex < m_NodeGraph.GetLinks().size();
         ++linkIndex) {
        const EditorNodeGraph::Link& link =
            m_NodeGraph.GetLinks()[linkIndex];
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
            m_NodeGraph.RemoveNode(createdNodeId);
        }
        if (!m_NodeGraph.FindNode(filterNodeId)) {
            std::vector<EditorNodeGraph::Node>& nodes =
                m_NodeGraph.EditNodes();
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
                m_NodeGraph.EditLinks();
            const std::size_t insertionIndex =
                std::min(item.index, links.size());
            links.insert(
                links.begin() +
                    static_cast<std::ptrdiff_t>(
                        insertionIndex),
                std::move(item.link));
        }
        m_NodeGraph.SetNextNodeId(
            patch.beforeNextNodeId);
        m_NodeGraph.ClearSelection();
        for (const int selectedNodeId :
             patch.beforeSelection) {
            m_NodeGraph.SelectNode(
                selectedNodeId,
                true);
        }
        if (patch.beforeSelectedLink) {
            const EditorNodeGraph::Link& selected =
                *patch.beforeSelectedLink;
            m_NodeGraph.SelectLink(
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
                m_NodeGraph.GetNodes().max_size() -
                    m_NodeGraph.GetNodes().size() ||
            maximumAddedLinks >
                m_NodeGraph.GetLinks().max_size() -
                    m_NodeGraph.GetLinks().size()) {
            throw std::length_error(
                "advanced frequency graph capacity exhausted");
        }
        m_NodeGraph.EditNodes().reserve(
            m_NodeGraph.GetNodes().size() +
            kAdvancedNodeCount);
        m_NodeGraph.EditLinks().reserve(
            m_NodeGraph.GetLinks().size() +
            maximumAddedLinks);
        if (!m_NodeGraph.RemoveNode(filterNodeId)) {
            if (error) *error = "Could not remove the original Frequency Filter.";
            return false;
        }

        EditorNodeGraph::Node* created =
            m_NodeGraph.AddFrequencyFftNode(
                { origin.x - 330.0f, origin.y });
        const int transformId = created ? created->id : -1;
        if (transformId > 0) createdNodeIds.push_back(transformId);
        created = m_NodeGraph.AddFrequencyResponseNode(
            { origin.x - 110.0f, origin.y + 190.0f });
        const int responseId = created ? created->id : -1;
        if (responseId > 0) createdNodeIds.push_back(responseId);
        created = m_NodeGraph.AddApplyFrequencyResponseNode(
            { origin.x - 90.0f, origin.y });
        const int applyId = created ? created->id : -1;
        if (applyId > 0) createdNodeIds.push_back(applyId);
        created = m_NodeGraph.AddFrequencyIfftNode(
            { origin.x + 150.0f, origin.y });
        const int inverseId = created ? created->id : -1;
        if (inverseId > 0) createdNodeIds.push_back(inverseId);
        if (transformId <= 0 || responseId <= 0 ||
            applyId <= 0 || inverseId <= 0) {
            rollbackAdvancedChain();
            if (error) *error = "Could not create the advanced frequency chain.";
            return false;
        }
        m_NodeGraph.FindNode(transformId)->
            frequencyFftSettings.edgePolicy =
                filterSettings.edgePolicy;
        m_NodeGraph.FindNode(responseId)->
            frequencyResponseSettings =
                filterSettings.localResponse;
        m_NodeGraph.FindNode(applyId)->
            applyFrequencyResponseSettings.strength =
                filterSettings.strength;
        if (std::find(
                exposedParameters.begin(),
                exposedParameters.end(),
                EditorNodeGraph::kStrengthParameterId) !=
            exposedParameters.end()) {
            m_NodeGraph.SetParameterExposed(
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
                if (m_NodeGraph.TryConnectSockets(
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
                m_NodeGraph.SetParameterExposed(
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
            m_NodeGraph.RemoveNode(responseId);
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

        SelectGraphNode(applyId);
        std::unordered_set<int> addedNodeIds(
            createdNodeIds.begin(),
            createdNodeIds.end());
        for (std::size_t nodeIndex = 0;
             nodeIndex < m_NodeGraph.GetNodes().size();
             ++nodeIndex) {
            const EditorNodeGraph::Node& node =
                m_NodeGraph.GetNodes()[nodeIndex];
            if (addedNodeIds.count(node.id) != 0u) {
                patch.afterNodes.push_back({
                    nodeIndex,
                    node
                });
            }
        }
        for (std::size_t linkIndex = 0;
             linkIndex < m_NodeGraph.GetLinks().size();
             ++linkIndex) {
            const EditorNodeGraph::Link& link =
                m_NodeGraph.GetLinks()[linkIndex];
            if (addedNodeIds.count(link.fromNodeId) != 0u ||
                addedNodeIds.count(link.toNodeId) != 0u) {
                patch.afterLinks.push_back({
                    linkIndex,
                    link
                });
            }
        }
        patch.afterNextNodeId =
            m_NodeGraph.GetNextNodeId();
        patch.expectedAfterRevision =
            m_NodeGraph.GetStructureRevision();
        patch.afterSelection =
            m_NodeGraph.GetSelectedNodeIds();
        if (const EditorNodeGraph::Link* selected =
                m_NodeGraph.GetSelectedLink()) {
            patch.afterSelectedLink = *selected;
        }
        m_FrequencyGraphUndo = std::move(patch);
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
    m_FrequencyGraphRedo.reset();
    MarkRenderDirty();
    return true;
}

bool EditorModule::UndoFrequencyGraphAction() {
    if (!m_FrequencyGraphUndo.has_value()) return false;
    if (m_NodeGraph.GetStructureRevision() !=
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
    MarkRenderDirty();
    return true;
}

bool EditorModule::RedoFrequencyGraphAction() {
    if (!m_FrequencyGraphRedo.has_value()) return false;
    if (m_NodeGraph.GetStructureRevision() !=
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
    MarkRenderDirty();
    return true;
}

void EditorModule::AddFrequencyMaskNodeAt(EditorNodeGraph::FrequencyMaskShape shape, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddFrequencyMaskNode(shape, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddSpectrumMathNodeAt(EditorNodeGraph::SpectrumMathMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddSpectrumMathNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddMagnitudePhaseNodeAt(EditorNodeGraph::MagnitudePhaseMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMagnitudePhaseNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddSpectrumAnalyzerNodeAt(EditorNodeGraph::SpectrumAnalyzerMode mode, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddSpectrumAnalyzerNode(mode, graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddPreviewNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddChannelSplitNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddChannelSplitNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddChannelCombineNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddChannelCombineNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddConstantChannelNodeAt(
    EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node =
            m_NodeGraph.AddConstantChannelNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty(node->id);
    }
}

void EditorModule::AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddOutputNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty();
    }
}

void EditorModule::AutoLayoutGraph() {
    m_NodeGraph.AutoLayout();
}

void EditorModule::DisconnectGraphOutput() {
    m_NodeGraph.DisconnectOutput();
    ClearViewportOutputTiles();
    m_Pipeline.ClearOutput();
    MarkRenderDirty();
}

void EditorModule::RefreshGraphLayerMetadata() {
    m_NodeGraph.SyncLayerNodes(static_cast<int>(m_Layers.size()));

    for (int i = 0; i < static_cast<int>(m_Layers.size()); ++i) {
        EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(i);
        if (!node) {
            continue;
        }

        const nlohmann::json layerJson = m_Layers[i]->Serialize();
        const std::string typeId = layerJson.value("type", std::string());
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(typeId);
        if (descriptor) {
            m_NodeGraph.SetLayerNodeType(node->id, descriptor->type);
        } else {
            node->typeId = typeId;
            node->title = m_Layers[i]->GetDefaultName();
        }
    }
}

