#pragma once
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "NodeMath/ArithmeticContract.h"
#include "Graph/GraphImageRules.h"
#include <algorithm>

namespace EditorNodeGraph {
inline bool IsFoundationNode(const Node& node) {
    return (node.kind == NodeKind::Layer && node.layerType == LayerType::Brightness) ||
           (node.kind == NodeKind::DataMath && Stack::NodeMath::Primitive(node.dataMathMode)) ||
           (node.kind == NodeKind::Value &&
            node.value.value.logicalType == Stack::NodeMath::LogicalValueType::Scalar);
}

inline bool IsLayoutSensitivePort(const Node& node, const std::string& port) {
    if (port == kMaskInputSocketId || node.kind == NodeKind::Mix || node.kind == NodeKind::ChannelCombine ||
        node.kind == NodeKind::ChannelSplit || node.role == Stack::GraphModel::NodeRole::LayerResult)
        return true;
    if (node.kind == NodeKind::Output)
        return node.outputSettings.maskOutput ||
               node.outputSettings.publishedType != Stack::NodeMath::LogicalValueType::Invalid;
    if (port != kImageInputSocketId)
        return false;
    const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(node);
    return definition && definition->requiresSceneLinearRgb;
}

// Called for both proposed wires and retained wires, using the corresponding
// descriptors from the same graph revision. An empty message means compatible.
inline std::string FoundationWireError(const Graph& graph, const Node& target, const std::string& port,
                                       const Stack::NodeMath::ValueDescriptor& source,
                                       const Stack::NodeMath::ValueDescriptor* other) {
    using namespace Stack::NodeMath;
    using T = LogicalValueType;
    if (target.kind == NodeKind::DataMath && Primitive(target.dataMathMode))
        return port == kMixInputASocketId ? ResolveArithmetic(&source, other).error
                                          : ResolveArithmetic(other, &source).error;
    if (source.logicalType == T::Invalid)
        return {};
    if (target.kind == NodeKind::Layer && target.layerType == LayerType::Brightness &&
        port == kImageInputSocketId) {
        const auto shape = ShapeOf(&source);
        if (shape == ArithmeticShape::Number || shape == ArithmeticShape::Unsupported)
            return "Brightness needs a Channel, RGB or RGBA input.";
    }
    if (target.kind == NodeKind::Mix && other && (port == kMixInputASocketId || port == kMixInputBSocketId)) {
        std::string reason;
        if (!Stack::GraphModel::ValidateImageCombination(source, *other, reason))
            return reason;
    }
    auto contract = EditorNodeGraphDefinitions::GetLivePortContract(graph, target, port);
    if (!contract)
        return "The destination socket is unavailable.";
    if (contract->port.semanticRequirement == Stack::NodeMath::RequirementPolicy::Strict) {
        const auto& expected = contract->requiredSemantics;
        if ((expected.transfer.state == KnowledgeState::Known && source.transfer.state == KnowledgeState::Known &&
             expected.transfer.value.kind != source.transfer.value.kind) ||
            (expected.reference.state == KnowledgeState::Known &&
             source.reference.state == KnowledgeState::Known &&
             expected.reference.value != source.reference.value) ||
            (!contract->acceptedAlpha.empty() && source.alpha.state == KnowledgeState::Known &&
             std::find(contract->acceptedAlpha.begin(), contract->acceptedAlpha.end(), source.alpha.value) ==
                 contract->acceptedAlpha.end()))
            return "This input requires " + contract->port.requirementDescription + ".";
    }
    const auto& types = contract->port.acceptedLogicalTypes;
    if (std::find(types.begin(), types.end(), source.logicalType) != types.end())
        return {};
    if (ArithmeticChannel(source.logicalType) && std::any_of(types.begin(), types.end(), ArithmeticChannel))
        return {};
    if (port == kMaskInputSocketId)
        return "Coverage needs one Channel. This wire carries " + std::string(ShapeName(ShapeOf(&source))) + ".";
    if (types.size() == 1 && types.front() == T::Scalar)
        return "This input needs one Number.";
    return target.title + " cannot accept this value at " + port + ".";
}
}
