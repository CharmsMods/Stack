#pragma once
#include "Project/NodeEditTarget.h"
#include "Project/RawLayerStack.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Graph/LayerOperationParameters.h"
#include <functional>

namespace Stack::Project {
inline const EditorNodeGraph::Node* ResolveRawNode(const RawLayerStackState& state,
    const NodeEditTarget& target, const std::string& documentId) {
    if (target.documentId != documentId) return nullptr;
    const auto* layer = FindRawAdjustmentLayer(state,target.layerId);
    if (layer) for (const auto& node : layer->graph.GetNodes())
        if (node.instanceUuid == target.nodeUuid) return &node;
    return nullptr;
}

inline bool RawNodeAcceptsMask(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) {
    if (node.role != GraphModel::NodeRole::Ordinary || !node.definitionResolved) return false;
    // RAW's coverage workflow inserts an operation into a serial image path.
    // A mask operand/filter on a data-processing node has a different meaning.
    if (!graph.FindSocket(node.id,"imageIn") || !graph.FindSocket(node.id,"imageOut")) return false;
    const auto port = EditorNodeGraphDefinitions::GetLivePortContract(graph,node,"maskIn");
    return port && port->port.optional && port->port.direction == NodeMath::PortDirection::Input &&
        (port->port.logicalType == NodeMath::LogicalValueType::Mask ||
         port->port.logicalType == NodeMath::LogicalValueType::Channel ||
         port->port.logicalType == NodeMath::LogicalValueType::ScalarField);
}

// A custom editor can replace parameter payloads, never instance ownership,
// graph layout or another node's legacy settings slot.
inline bool WriteRawNodeDraft(RawAdjustmentLayer& layer, EditorNodeGraph::Node& node,
    const EditorNodeGraph::Node& draft, const nlohmann::json& settings, std::string& error) {
    if(node.instanceUuid!=draft.instanceUuid || node.kind!=draft.kind || node.definitionId!=draft.definitionId) {
        error="The editor draft belongs to a different node definition.";return false;
    }
    auto updated=draft;
    updated.id=node.id;updated.instanceUuid=node.instanceUuid;updated.role=node.role;
    updated.rawLayerOwnerId=node.rawLayerOwnerId;updated.position=node.position;
    updated.layerIndex=node.layerIndex;updated.expanded=node.expanded;
    if(node.kind==EditorNodeGraph::NodeKind::Layer) {
        if(!settings.is_object()) {
            error="The effect settings are unavailable.";return false;
        }
        updated.layerParameters=settings;
        if (!GraphModel::ValidateLayerParameters(updated,error)) return false;
    }
    node=std::move(updated);return true;
}

inline std::optional<GraphModel::Endpoint> RawNodeInput(const RawLayerStackState& state,
    const NodeEditTarget& target,const std::string& documentId,const std::string& input) {
    const auto* node=ResolveRawNode(state,target,documentId);
    const auto* layer=node?FindRawAdjustmentLayer(state,target.layerId):nullptr;
    const auto* link=layer?layer->graph.FindInputLink(node->id,input):nullptr;
    const auto* source=link?layer->graph.FindNode(link->fromNodeId):nullptr;
    return source?std::optional<GraphModel::Endpoint>(RawLayerEndpoint(*layer,*source,link->fromSocketId)):std::nullopt;
}

// Prepare by identity and revision; callers commit through the same project
// action used for graph edits. Rejection never modifies the live document.
inline bool PrepareRawNodeEdit(const RawLayerStackDocument& document, const std::string& documentId,
    const NodeEditTarget& target, std::uint64_t revision, RawLayerStackState& candidate,
    const std::function<bool(RawAdjustmentLayer&, EditorNodeGraph::Node&, std::string&)>& edit,
    std::string& error) {
    if (revision != document.Revision() || !ResolveRawNode(document.State(),target,documentId)) {
        error = "The project or node changed while this edit was prepared."; return false;
    }
    candidate = document.State();
    auto* layer = FindRawAdjustmentLayer(candidate,target.layerId);
    for (auto& node : layer->graph.GetNodes()) if (node.instanceUuid == target.nodeUuid)
        return edit(*layer,node,error);
    return false;
}
}
