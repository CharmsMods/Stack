#include "Editor/NodeGraph/EditorNodeGraph.h"

#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"

#include <string>
#include <vector>

namespace EditorNodeGraph {
namespace {

struct NodeMetadataState {
    std::string instanceUuid;
    std::string typeId;
    std::string title;
    std::string definitionId;
    std::string definitionVersion;
    std::string definitionHash;
    bool definitionResolved = false;
    std::string definitionResolutionError;
};

NodeMetadataState CaptureMetadataState(const Node& node) {
    return NodeMetadataState{
        node.instanceUuid,
        node.typeId,
        node.title,
        node.definitionId,
        node.definitionVersion,
        node.definitionHash,
        node.definitionResolved,
        node.definitionResolutionError
    };
}

bool MetadataChanged(const NodeMetadataState& before, const Node& after) {
    return before.instanceUuid != after.instanceUuid ||
        before.typeId != after.typeId ||
        before.title != after.title ||
        before.definitionId != after.definitionId ||
        before.definitionVersion != after.definitionVersion ||
        before.definitionHash != after.definitionHash ||
        before.definitionResolved != after.definitionResolved ||
        before.definitionResolutionError != after.definitionResolutionError;
}

} // namespace

bool Graph::SetDataMathMode(int nodeId, DataMathMode mode) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::DataMath) {
        return false;
    }

    const DataMathMode previousMode = node->dataMathMode;
    const NodeMetadataState before = CaptureMetadataState(*node);
    node->dataMathMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*node);

    std::vector<Link> invalidLinks;
    if (previousMode != mode) {
        ForEachIncomingLink(nodeId, [&](const Link& link) {
            if (!CanConnectSockets(
                    link.fromNodeId,
                    link.fromSocketId,
                    link.toNodeId,
                    link.toSocketId)) {
                invalidLinks.push_back(link);
            }
        });
    }
    for (const Link& link : invalidLinks) {
        RemoveLink(
            link.fromNodeId,
            link.fromSocketId,
            link.toNodeId,
            link.toSocketId);
    }

    const bool changed =
        previousMode != mode ||
        MetadataChanged(before, *node) ||
        !invalidLinks.empty();
    if (changed) {
        TouchStructure();
    }
    return changed;
}

bool Graph::SetMaskCombineMode(int nodeId, MaskCombineMode mode) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::MaskCombine) {
        return false;
    }

    const MaskCombineMode previousMode = node->maskCombineMode;
    const NodeMetadataState before = CaptureMetadataState(*node);
    node->maskCombineMode = mode;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*node);
    const bool changed =
        previousMode != mode ||
        MetadataChanged(before, *node);
    if (changed) {
        TouchStructure();
    }
    return changed;
}

bool Graph::SetImageToMaskKind(int nodeId, ImageToMaskKind kind) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::ImageToMask) {
        return false;
    }

    const ImageToMaskKind previousKind = node->imageToMaskKind;
    const NodeMetadataState before = CaptureMetadataState(*node);
    node->imageToMaskKind = kind;
    EditorNodeGraphDefinitions::ApplyNodeMetadata(*node);
    const bool changed =
        previousKind != kind ||
        MetadataChanged(before, *node);
    if (changed) {
        TouchStructure();
    }
    return changed;
}

bool Graph::SetLayerNodeType(int nodeId, LayerType type) {
    Node* node = FindNode(nodeId);
    if (!node || node->kind != NodeKind::Layer) {
        return false;
    }

    const LayerType previousType = node->layerType;
    const NodeMetadataState before = CaptureMetadataState(*node);
    node->layerType = type;
    if (node->definitionResolved) {
        EditorNodeGraphDefinitions::ApplyNodeMetadata(*node);
    } else if (const LayerDescriptor* descriptor =
                   LayerRegistry::GetDescriptor(type)) {
        // Metadata synchronization must not silently "repair" an exact saved
        // definition mismatch. Preserve its unresolved identity until the user
        // explicitly chooses a replacement.
        node->typeId = descriptor->typeId ? descriptor->typeId : "";
        node->title =
            descriptor->displayName ? descriptor->displayName : "Layer";
    }
    const bool changed =
        previousType != type ||
        MetadataChanged(before, *node);
    if (changed) {
        TouchStructure();
    }
    return changed;
}

} // namespace EditorNodeGraph
