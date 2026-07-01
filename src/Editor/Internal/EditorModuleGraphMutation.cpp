#include "Editor/EditorModule.h"

#include "Editor/Layers/ToneLayers.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Renderer/GLHelpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <optional>
#include <unordered_set>

namespace {

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

struct ScenePathState {
    bool sceneReferred = false;
    bool hasViewTransform = false;
};

ScenePathState MergeScenePathState(ScenePathState a, const ScenePathState& b) {
    a.sceneReferred = a.sceneReferred || b.sceneReferred;
    a.hasViewTransform = a.hasViewTransform || b.hasViewTransform;
    return a;
}

ScenePathState AnalyzeScenePathFromNode(
    const EditorNodeGraph::Graph& graph,
    const std::vector<std::shared_ptr<LayerBase>>& layers,
    int nodeId,
    std::unordered_set<int>& visiting) {
    if (!visiting.insert(nodeId).second) {
        return {};
    }

    ScenePathState state;
    const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
    if (!node) {
        visiting.erase(nodeId);
        return state;
    }

    auto mergeInput = [&](const std::string& socketId) {
        if (const EditorNodeGraph::Link* input = graph.FindInputLink(nodeId, socketId)) {
            state = MergeScenePathState(state, AnalyzeScenePathFromNode(graph, layers, input->fromNodeId, visiting));
        }
    };

    switch (node->kind) {
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kRawInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::HdrMerge:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kHdrMergeInput1SocketId);
            mergeInput(EditorNodeGraph::kHdrMergeInput2SocketId);
            mergeInput(EditorNodeGraph::kHdrMergeInput3SocketId);
            break;
        case EditorNodeGraph::NodeKind::Mfsr:
            state.sceneReferred = true;
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxMfsrInputCount; ++inputIndex) {
                mergeInput(EditorNodeGraph::MfsrInputSocketId(inputIndex));
            }
            break;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            state.sceneReferred = true;
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            mergeInput(EditorNodeGraph::kRawInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            if (node->layerType == LayerType::ToneCurve) {
                state.sceneReferred = true;
            } else if (node->layerType == LayerType::ViewTransform) {
                state.hasViewTransform = true;
            }
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Lut:
            if (graph.FindInputLink(node->id, EditorNodeGraph::kImageInputSocketId)) {
                mergeInput(EditorNodeGraph::kImageInputSocketId);
            } else {
                mergeInput("r");
                mergeInput("g");
                mergeInput("b");
                mergeInput("a");
            }
            break;
        case EditorNodeGraph::NodeKind::Mix:
            mergeInput(EditorNodeGraph::kMixInputASocketId);
            mergeInput(EditorNodeGraph::kMixInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
                mergeInput(EditorNodeGraph::DataMathInputSocketId(inputIndex));
            }
            mergeInput(EditorNodeGraph::kDataMathBaseInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            mergeInput(EditorNodeGraph::kImageInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            mergeInput("r");
            mergeInput("g");
            mergeInput("b");
            mergeInput("a");
            break;
        case EditorNodeGraph::NodeKind::Output:
            if (graph.FindInputLink(node->id, EditorNodeGraph::kImageInputSocketId)) {
                mergeInput(EditorNodeGraph::kImageInputSocketId);
            } else {
                mergeInput("r");
                mergeInput("g");
                mergeInput("b");
                mergeInput("a");
            }
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            mergeInput(EditorNodeGraph::kImageToMaskInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            mergeInput(EditorNodeGraph::kMaskCombineInputASocketId);
            mergeInput(EditorNodeGraph::kMaskCombineInputBSocketId);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            mergeInput(EditorNodeGraph::kMaskUtilityInputSocketId);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::Preview:
            break;
    }

    visiting.erase(nodeId);
    (void)layers;
    return state;
}

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
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (link.fromNodeId != node.id || link.fromSocketId != sourcePlan->toSocketId) {
            continue;
        }
        if (sourcePlan->fromNodeId == link.toNodeId) {
            continue;
        }
        plans.push_back(GraphReconnectPlan{
            sourcePlan->fromNodeId,
            sourcePlan->fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }
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
    m_ColorPickerCallback = std::move(callback);
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
    m_ColorPickerCallback = nullptr;
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

    std::vector<EditorNodeGraph::Link> outputLinks;
    std::vector<EditorNodeGraph::Link> maskLinks;
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == layerNodeId && link.fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
            outputLinks.push_back(link);
        } else if (link.toNodeId == layerNodeId && link.toSocketId == EditorNodeGraph::kMaskInputSocketId) {
            maskLinks.push_back(link);
        }
    }
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
    const EditorNodeGraph::Graph graphSnapshot = m_NodeGraph;
    const std::vector<std::shared_ptr<LayerBase>> layersSnapshot = m_Layers;

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
        cloneLayer = CloneLayerInstance(originalLayer);
        if (!cloneLayer) {
            return false;
        }
    }

    for (const auto& entry : originalDownstreamPositions) {
        if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
            downstream->position.x = entry.second.x + 620.0f;
        }
    }

    AddChannelSplitNodeAt(EditorNodeGraph::Vec2{ originalPos.x - 250.0f, originalPos.y });
    const int splitNodeId = m_NodeGraph.GetSelectedNodeId();
    AddChannelCombineNodeAt(EditorNodeGraph::Vec2{ originalPos.x + 370.0f, originalPos.y });
    const int combineNodeId = m_NodeGraph.GetSelectedNodeId();
    if (splitNodeId <= 0 || combineNodeId <= 0) {
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        if (splitNodeId > 0) {
            RemoveGraphNode(splitNodeId);
        }
        if (combineNodeId > 0) {
            RemoveGraphNode(combineNodeId);
        }
        return false;
    }

    std::array<int, 4> cloneNodeIds{ -1, -1, -1, -1 };
    constexpr const char* kChannels[4] = { "r", "g", "b", "a" };
    constexpr float kRowOffsets[4] = { -240.0f, -80.0f, 80.0f, 240.0f };
    bool createdAllClones = true;
    for (int i = 0; i < 4; ++i) {
        const int newLayerIndex = static_cast<int>(m_Layers.size());
        m_Layers.push_back(cloneLayers[i]);
        EditorNodeGraph::Node* cloneNode = m_NodeGraph.AddLayerNode(
            originalLayerType,
            newLayerIndex,
            EditorNodeGraph::Vec2{ originalPos.x + 60.0f, originalPos.y + kRowOffsets[i] });
        if (!cloneNode) {
            createdAllClones = false;
            break;
        }
        cloneNode->typeId = originalTypeId;
        cloneNodeIds[i] = cloneNode->id;
    }
    if (!createdAllClones) {
        while (static_cast<int>(m_Layers.size()) > originalLayerIndex + 1) {
            m_Layers.pop_back();
        }
        for (int cloneNodeId : cloneNodeIds) {
            if (cloneNodeId > 0) {
                RemoveGraphNode(cloneNodeId);
            }
        }
        RemoveGraphNode(splitNodeId);
        RemoveGraphNode(combineNodeId);
        for (const auto& entry : originalDownstreamPositions) {
            if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(entry.first)) {
                downstream->position = entry.second;
            }
        }
        RefreshGraphLayerMetadata();
        MarkRenderDirty();
        return false;
    }

    std::string errorMessage;
    bool ok = ConnectGraphSockets(imageInput->fromNodeId, imageInput->fromSocketId, splitNodeId, EditorNodeGraph::kImageInputSocketId, &errorMessage);
    for (int i = 0; ok && i < 4; ++i) {
        ok = ConnectGraphSockets(splitNodeId, kChannels[i], cloneNodeIds[i], EditorNodeGraph::kImageInputSocketId, &errorMessage);
        if (ok) {
            ok = ConnectGraphSockets(cloneNodeIds[i], EditorNodeGraph::kImageOutputSocketId, combineNodeId, kChannels[i], &errorMessage);
        }
    }

    for (const EditorNodeGraph::Link& maskLink : maskLinks) {
        for (int cloneNodeId : cloneNodeIds) {
            if (!ok) {
                break;
            }
            ok = ConnectGraphSockets(maskLink.fromNodeId, maskLink.fromSocketId, cloneNodeId, maskLink.toSocketId, &errorMessage);
        }
        if (!ok) {
            break;
        }
    }

    for (const EditorNodeGraph::Link& outputLink : outputLinks) {
        if (!ok) {
            break;
        }
        if (outputLink.toNodeId == combineNodeId) {
            continue;
        }
        ok = ConnectGraphSockets(combineNodeId, EditorNodeGraph::kImageOutputSocketId, outputLink.toNodeId, outputLink.toSocketId, &errorMessage);
    }

    if (!ok) {
        m_NodeGraph = graphSnapshot;
        m_Layers = layersSnapshot;
        RefreshGraphLayerMetadata();
        MarkRenderDirty();
        return false;
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

    std::vector<EditorNodeGraph::Link> outputLinks;
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (link.fromNodeId == dataMathNodeId && link.fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
            outputLinks.push_back(link);
        }
    }

    const EditorNodeGraph::Vec2 originalPos = averageNode->position;
    const EditorNodeGraph::Graph graphSnapshot = m_NodeGraph;

    std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(dataMathNodeId);
    downstreamNodeIds.erase(
        std::remove(downstreamNodeIds.begin(), downstreamNodeIds.end(), dataMathNodeId),
        downstreamNodeIds.end());
    for (const int downstreamNodeId : downstreamNodeIds) {
        if (EditorNodeGraph::Node* downstream = m_NodeGraph.FindNode(downstreamNodeId)) {
            downstream->position.x += 520.0f;
        }
    }

    std::vector<int> splitNodeIds;
    splitNodeIds.reserve(inputLinks.size());
    const float inputStartY = originalPos.y - (static_cast<float>(inputLinks.size() - 1) * 84.0f);
    for (std::size_t inputIndex = 0; inputIndex < inputLinks.size(); ++inputIndex) {
        EditorNodeGraph::Node* splitNode = m_NodeGraph.AddChannelSplitNode(EditorNodeGraph::Vec2{
            originalPos.x - 360.0f,
            inputStartY + static_cast<float>(inputIndex) * 168.0f
        });
        if (!splitNode) {
            m_NodeGraph = graphSnapshot;
            MarkRenderDirty();
            return false;
        }
        splitNodeIds.push_back(splitNode->id);
    }

    std::array<int, 4> averageNodeIds{ -1, -1, -1, -1 };
    constexpr const char* kChannels[4] = { "r", "g", "b", "a" };
    constexpr float kChannelRows[4] = { -210.0f, -70.0f, 70.0f, 210.0f };
    for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
        EditorNodeGraph::Node* channelAverage = m_NodeGraph.AddDataMathNode(
            EditorNodeGraph::DataMathMode::Average,
            EditorNodeGraph::Vec2{ originalPos.x, originalPos.y + kChannelRows[channelIndex] });
        if (!channelAverage) {
            m_NodeGraph = graphSnapshot;
            MarkRenderDirty();
            return false;
        }
        averageNodeIds[channelIndex] = channelAverage->id;
    }

    EditorNodeGraph::Node* combineNode = m_NodeGraph.AddChannelCombineNode(EditorNodeGraph::Vec2{
        originalPos.x + 360.0f,
        originalPos.y
    });
    if (!combineNode) {
        m_NodeGraph = graphSnapshot;
        MarkRenderDirty();
        return false;
    }
    const int combineNodeId = combineNode->id;

    std::string errorMessage;
    bool ok = true;
    for (std::size_t inputIndex = 0; ok && inputIndex < inputLinks.size(); ++inputIndex) {
        const EditorNodeGraph::Link& input = inputLinks[inputIndex].link;
        ok = m_NodeGraph.TryConnectSockets(
            input.fromNodeId,
            input.fromSocketId,
            splitNodeIds[inputIndex],
            EditorNodeGraph::kImageInputSocketId,
            &errorMessage);
    }
    for (int channelIndex = 0; ok && channelIndex < 4; ++channelIndex) {
        for (std::size_t inputIndex = 0; ok && inputIndex < splitNodeIds.size(); ++inputIndex) {
            ok = m_NodeGraph.TryConnectSockets(
                splitNodeIds[inputIndex],
                kChannels[channelIndex],
                averageNodeIds[channelIndex],
                EditorNodeGraph::DataMathInputSocketId(static_cast<int>(inputIndex)),
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
    for (const EditorNodeGraph::Link& outputLink : outputLinks) {
        if (!ok) {
            break;
        }
        ok = m_NodeGraph.TryConnectSockets(
            combineNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            outputLink.toNodeId,
            outputLink.toSocketId,
            &errorMessage);
    }

    if (!ok || !m_NodeGraph.RemoveNode(dataMathNodeId)) {
        m_NodeGraph = graphSnapshot;
        MarkRenderDirty();
        return false;
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
        std::unordered_set<int> visiting;
        const ScenePathState scenePath = AnalyzeScenePathFromNode(m_NodeGraph, m_Layers, fromNodeId, visiting);
        if (scenePath.sceneReferred && !scenePath.hasViewTransform) {
            const EditorNodeGraph::Vec2 fromPosition = from ? from->position : EditorNodeGraph::Vec2{};
            const EditorNodeGraph::Vec2 toPosition = targetNode->position;
            const EditorNodeGraph::Vec2 viewPosition{
                (fromPosition.x + toPosition.x) * 0.5f,
                (fromPosition.y + toPosition.y) * 0.5f
            };
            AddLayerNodeAt(LayerType::ViewTransform, viewPosition);
            const int viewNodeId = m_NodeGraph.GetSelectedNodeId();
            if (viewNodeId <= 0) {
                if (errorMessage) *errorMessage = "Could not create View Transform node.";
                return false;
            }
            if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, viewNodeId, EditorNodeGraph::kImageInputSocketId, errorMessage)) {
                return false;
            }
            if (!m_NodeGraph.TryConnectSockets(viewNodeId, EditorNodeGraph::kImageOutputSocketId, toNodeId, toSocketId, errorMessage)) {
                return false;
            }
            ApplyGraphLayerOrder();
            MarkRenderDirty();
            SelectGraphNode(viewNodeId);
            ValidateActiveRawWorkspaceManagedGraph(true);
            return true;
        }
    }

    std::string extractorError;
    if (m_NodeGraph.CanInsertImageToScalarExtractor(
            fromNodeId,
            fromSocketId,
            toNodeId,
            toSocketId,
            &extractorError)) {
        const EditorNodeGraph::Vec2 fromPosition = from ? from->position : EditorNodeGraph::Vec2{};
        const EditorNodeGraph::Vec2 toPosition = targetNode ? targetNode->position : fromPosition;
        EditorNodeGraph::Node* extractorNode = m_NodeGraph.AddImageToMaskNode(
            EditorNodeGraph::ImageToMaskKind::Luminance,
            EditorNodeGraph::Vec2{
                (fromPosition.x + toPosition.x) * 0.5f,
                (fromPosition.y + toPosition.y) * 0.5f
            });
        if (!extractorNode) {
            if (errorMessage) *errorMessage = "Could not create Image To Mask extractor.";
            return false;
        }

        const int extractorNodeId = extractorNode->id;
        if (!m_NodeGraph.TryConnectSockets(
                fromNodeId,
                fromSocketId,
                extractorNodeId,
                EditorNodeGraph::kImageToMaskInputSocketId,
                errorMessage)) {
            m_NodeGraph.RemoveNode(extractorNodeId);
            return false;
        }
        if (!m_NodeGraph.TryConnectSockets(
                extractorNodeId,
                EditorNodeGraph::kMaskOutputSocketId,
                toNodeId,
                toSocketId,
                errorMessage)) {
            m_NodeGraph.RemoveNode(extractorNodeId);
            return false;
        }

        if (const EditorNodeGraph::Node* currentFrom = m_NodeGraph.FindNode(fromNodeId);
            currentFrom && currentFrom->kind == EditorNodeGraph::NodeKind::Image &&
            ConnectionUsesImageAsRenderSource(
                m_NodeGraph,
                fromNodeId,
                fromSocketId,
                extractorNodeId,
                EditorNodeGraph::kImageToMaskInputSocketId)) {
            LoadSourceFromPixels(currentFrom->image.pixels.data(), currentFrom->image.width, currentFrom->image.height, currentFrom->image.channels);
            m_NodeGraph.SetActiveImageNodeId(fromNodeId);
            MarkNodeBrowserThumbnailSourceChanged();
        }

        ApplyGraphLayerOrder();
        MarkRenderDirty();
        SelectGraphNode(extractorNodeId);
        ValidateActiveRawWorkspaceManagedGraph(true);
        return true;
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
    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }
    std::unordered_set<int> visiting;
    const ScenePathState scenePath = AnalyzeScenePathFromNode(m_NodeGraph, m_Layers, input->fromNodeId, visiting);
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

    std::unordered_set<int> visiting;
    std::function<bool(int)> inputContainsViewTransform = [&](int nodeId) -> bool {
        if (!visiting.insert(nodeId).second) {
            return false;
        }
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            visiting.erase(nodeId);
            return false;
        }
        if (node->kind == EditorNodeGraph::NodeKind::Layer && node->layerType == LayerType::ViewTransform) {
            visiting.erase(nodeId);
            return true;
        }
        auto checkInput = [&](const std::string& socketId) {
            const EditorNodeGraph::Link* link = m_NodeGraph.FindInputLink(nodeId, socketId);
            return link ? inputContainsViewTransform(link->fromNodeId) : false;
        };
        bool found = false;
        if (node->kind == EditorNodeGraph::NodeKind::Mix) {
            found = checkInput(EditorNodeGraph::kMixInputASocketId) ||
                checkInput(EditorNodeGraph::kMixInputBSocketId);
        } else if (node->kind == EditorNodeGraph::NodeKind::DataMath) {
            for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
                if (checkInput(EditorNodeGraph::DataMathInputSocketId(inputIndex))) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                found = checkInput(EditorNodeGraph::kDataMathBaseInputSocketId);
            }
        } else if (node->kind == EditorNodeGraph::NodeKind::HdrMerge) {
            found = checkInput(EditorNodeGraph::kHdrMergeInput1SocketId) ||
                checkInput(EditorNodeGraph::kHdrMergeInput2SocketId) ||
                checkInput(EditorNodeGraph::kHdrMergeInput3SocketId);
        } else if (node->kind == EditorNodeGraph::NodeKind::ChannelCombine) {
            found = checkInput("r") || checkInput("g") || checkInput("b") || checkInput("a");
        } else if (node->kind != EditorNodeGraph::NodeKind::RawSource &&
            node->kind != EditorNodeGraph::NodeKind::Image) {
            found = checkInput(EditorNodeGraph::kImageInputSocketId);
        }
        visiting.erase(nodeId);
        return found;
    };

    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(selectedNode->id, EditorNodeGraph::kImageInputSocketId);
    return input ? inputContainsViewTransform(input->fromNodeId) : false;
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

bool EditorModule::RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
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
            if (EditorNodeGraph::Node* outputNode = m_NodeGraph.AddOutputNode(
                EditorNodeGraph::Vec2{ graphPosition.x + 330.0f, graphPosition.y })) {
                const int outputNodeId = outputNode->id;
                std::string errorMessage;
                ConnectGraphNodes(nodeId, outputNodeId, &errorMessage);
                if (GetCompletedChainCount() == 1 && m_Pipeline.GetSourcePixelsRaw().empty()) {
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
        node->typeId = typeId;
        if (descriptor) {
            node->layerType = descriptor->type;
            node->title = descriptor->displayName;
        } else {
            node->title = m_Layers[i]->GetDefaultName();
        }
    }
}

