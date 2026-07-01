#include "Editor/NodeGraph/EditorNodeGraphSelectionExport.h"

#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace {

EditorNodeGraph::Node MakeTreeOnlyNode(EditorNodeGraph::Node node) {
    node.image = {};
    node.rawSource = {};
    node.rawNeuralDenoise = {};
    node.rawDevelop = {};
    node.rawDetailAutoMask = {};
    node.rawDetailFusion = {};
    node.hdrMerge = {};
    node.maskSettings = {};
    node.maskUtilitySettings = {};
    node.imageToMaskSettings = {};
    node.imageGeneratorSettings = {};
    node.mixBlendMode = EditorNodeGraph::MixBlendMode::Normal;
    node.mixFactor = 0.5f;
    node.dataMathMode = EditorNodeGraph::DataMathMode::Clamp;
    node.dataMathSettings = {};
    node.outputEnabled = true;
    return node;
}

bool GroupIntersectsSelection(
    const EditorNodeGraph::NodeGroup& group,
    const EditorNodeGraph::Graph& graph,
    const std::unordered_set<int>& selectedNodeIds) {
    const float groupMinX = group.position.x;
    const float groupMinY = group.position.y;
    const float groupMaxX = group.position.x + group.size.x;
    const float groupMaxY = group.position.y + group.size.y;

    for (int nodeId : selectedNodeIds) {
        const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
        if (!node) {
            continue;
        }
        if (node->position.x >= groupMinX && node->position.x <= groupMaxX &&
            node->position.y >= groupMinY && node->position.y <= groupMaxY) {
            return true;
        }
    }
    return false;
}

std::string SocketTypeToString(EditorNodeGraph::SocketType type) {
    switch (type) {
        case EditorNodeGraph::SocketType::Mask: return "mask";
        case EditorNodeGraph::SocketType::Value: return "value";
        case EditorNodeGraph::SocketType::Analysis: return "analysis";
        case EditorNodeGraph::SocketType::Raw: return "raw";
        case EditorNodeGraph::SocketType::Image:
        default:
            return "image";
    }
}

void AddBoundarySocket(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node* node,
    const std::string& socketId,
    const std::string& direction,
    std::unordered_set<std::string>& seen,
    std::vector<EditorNodeGraphSelectionExport::BoundarySocketSummary>& outSockets) {
    if (!node) {
        return;
    }

    const std::string key = direction + ":" + std::to_string(node->id) + ":" + socketId;
    if (!seen.insert(key).second) {
        return;
    }

    EditorNodeGraph::SocketDefinition socket;
    const bool hasSocket = graph.FindSocket(node->id, socketId, &socket);

    EditorNodeGraphSelectionExport::BoundarySocketSummary summary;
    summary.nodeTitle = node->title;
    summary.socketLabel = hasSocket && !socket.label.empty() ? socket.label : socketId;
    summary.direction = direction;
    summary.type = hasSocket ? SocketTypeToString(socket.type) : std::string("image");
    outSockets.push_back(std::move(summary));
}

} // namespace

namespace EditorNodeGraphSelectionExport {

ExportResult BuildExport(EditorModule* editor, const std::vector<int>& nodeIds, bool includeState, bool wholeGraph) {
    ExportResult result;
    result.clipboardPayload["format"] = "stack-node-graph";
    result.clipboardPayload["version"] = 1;
    result.clipboardPayload["scope"] = wholeGraph ? "fullGraph" : "selection";
    result.clipboardPayload["mode"] = includeState ? "tree+state" : "tree";

    if (!editor) {
        return result;
    }

    const EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const auto& layers = editor->GetLayers();
    std::unordered_set<int> includedNodeIds(nodeIds.begin(), nodeIds.end());

    std::unordered_map<int, int> oldLayerIndexToNew;
    nlohmann::json layerArray = nlohmann::json::array();
    EditorNodeGraph::Graph exportGraph;
    exportGraph.Clear();
    exportGraph.SetNextNodeId(1);
    exportGraph.SetNextGroupId(1);

    int maxNodeId = 0;
    for (int nodeId : nodeIds) {
        const EditorNodeGraph::Node* sourceNode = graph.FindNode(nodeId);
        if (!sourceNode) {
            continue;
        }

        EditorNodeGraph::Node nodeCopy = includeState ? *sourceNode : MakeTreeOnlyNode(*sourceNode);
        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Layer) {
            int remappedLayerIndex = -1;
            const auto it = oldLayerIndexToNew.find(sourceNode->layerIndex);
            if (it != oldLayerIndexToNew.end()) {
                remappedLayerIndex = it->second;
            } else if (sourceNode->layerIndex >= 0 && sourceNode->layerIndex < static_cast<int>(layers.size())) {
                remappedLayerIndex = static_cast<int>(layerArray.size());
                oldLayerIndexToNew[sourceNode->layerIndex] = remappedLayerIndex;
                std::shared_ptr<LayerBase> layerToSerialize = layers[sourceNode->layerIndex];
                if (!includeState) {
                    const std::string typeId = layerToSerialize->Serialize().value("type", std::string());
                    std::shared_ptr<LayerBase> defaultLayer = LayerRegistry::CreateLayerFromTypeId(typeId);
                    if (defaultLayer) {
                        defaultLayer->InitializeGL();
                        layerToSerialize = defaultLayer;
                    }
                }
                layerArray.push_back(layerToSerialize ? layerToSerialize->Serialize() : nlohmann::json::object());
            }
            nodeCopy.layerIndex = remappedLayerIndex;
        }

        exportGraph.GetNodes().push_back(std::move(nodeCopy));
        maxNodeId = std::max(maxNodeId, sourceNode->id);
    }

    std::unordered_set<std::string> seenBoundarySockets;
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        const bool fromIncluded = includedNodeIds.count(link.fromNodeId) > 0;
        const bool toIncluded = includedNodeIds.count(link.toNodeId) > 0;
        if (fromIncluded && toIncluded) {
            exportGraph.GetLinks().push_back(link);
        } else if (!fromIncluded && toIncluded) {
            AddBoundarySocket(
                graph,
                graph.FindNode(link.toNodeId),
                link.toSocketId,
                "input",
                seenBoundarySockets,
                result.boundarySockets);
        } else if (fromIncluded && !toIncluded) {
            AddBoundarySocket(
                graph,
                graph.FindNode(link.fromNodeId),
                link.fromSocketId,
                "output",
                seenBoundarySockets,
                result.boundarySockets);
        }
    }

    for (const EditorNodeGraph::NodeGroup& group : graph.GetGroups()) {
        if (wholeGraph || GroupIntersectsSelection(group, graph, includedNodeIds)) {
            exportGraph.GetGroups().push_back(group);
        }
    }

    exportGraph.SetNextNodeId(std::max(maxNodeId + 1, 1));
    exportGraph.SetNextGroupId(static_cast<int>(exportGraph.GetGroups().size()) + 1);
    if (!nodeIds.empty()) {
        exportGraph.SelectNode(nodeIds.front());
    }

    result.nodeCount = static_cast<std::uint32_t>(exportGraph.GetNodes().size());
    result.clipboardPayload["payload"] = EditorNodeGraph::SerializeGraphPayload(layerArray, exportGraph);
    result.exportedGraph = exportGraph;
    return result;
}

} // namespace EditorNodeGraphSelectionExport
