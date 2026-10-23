#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include <algorithm>

namespace EditorNodeGraph::ConnectionRules {

inline bool IsChannelProcessingBridge(
    const Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    const SocketDefinition& fromSocket,
    const Node& to,
    const std::string& toSocketId,
    const SocketDefinition& toSocket) {
    if (to.kind == NodeKind::Compound) {
        const auto port = std::find_if(to.compound.instance.interfaceSnapshot.begin(),
            to.compound.instance.interfaceSnapshot.end(), [&](const auto& p) { return p.id == toSocketId; });
        if (port != to.compound.instance.interfaceSnapshot.end() && port->numeric) {
            if (IsSingleChannelValue(port->logicalType)) {
                const auto value = DescribeGraphOutput(graph,fromNodeId,fromSocketId).descriptor.logicalType;
                return IsSingleChannelValue(value) || value == Stack::NodeMath::LogicalValueType::Scalar ||
                    value == Stack::NodeMath::LogicalValueType::Integer;
            }
            if (fromSocket.type == SocketType::Image || fromSocket.type == SocketType::Channel ||
                fromSocket.type == SocketType::Mask || fromSocket.type == SocketType::ScalarField ||
                fromSocket.type == SocketType::Scalar || fromSocket.type == SocketType::Integer) return true;
        }
    }
    // Numeric uniforms broadcast at the arithmetic consumer. They remain
    // uniforms in the graph and never acquire an image-sized allocation.
    if (fromSocket.type == SocketType::Scalar || fromSocket.type == SocketType::Integer) {
        const bool arithmetic = to.kind == NodeKind::DataMath &&
             IsDataMathInputSocketId(toSocketId);
        const bool scalarPromotion = fromSocket.type == SocketType::Integer &&
            toSocket.type == SocketType::Scalar;
        if (arithmetic || scalarPromotion) return true;
    }
    // A polymorphic image input accepts full color directly as well.
    if (toSocket.type==SocketType::ImageOrChannel && fromSocket.type==SocketType::Image) return true;
    // Channel is the payload shape; Mask and ScalarField are still staged
    // socket spellings for single-channel roles. Keep this bridge shared by
    // authoring, validation, and renderer scheduling so those paths cannot
    // disagree about a structurally executable Channel connection.
    const bool channelToSingleChannelTarget =
        fromSocket.type == SocketType::Channel &&
        (toSocket.type == SocketType::Mask ||
         toSocket.type == SocketType::ScalarField) &&
        graph.IsScalarTargetSocket(to.id, toSocketId);
    if (channelToSingleChannelTarget) {
        return true;
    }

    const bool namedChannelAssemblyTarget =
        to.kind == NodeKind::ChannelCombine &&
        toSocket.type == SocketType::Channel &&
        (toSocketId == "r" ||
         toSocketId == "g" ||
         toSocketId == "b" ||
         toSocketId == "a");
    const bool singleChannelFieldToAssembly =
        namedChannelAssemblyTarget &&
        (fromSocket.type == SocketType::Mask ||
         fromSocket.type == SocketType::ScalarField);
    if (singleChannelFieldToAssembly) {
        return true;
    }

    const bool outputValueTarget =
        to.kind == NodeKind::Output &&
        toSocketId == kImageInputSocketId &&
        toSocket.type == SocketType::ImageOrChannel;
    if (outputValueTarget) {
        if (fromSocket.type == SocketType::Channel || fromSocket.type == SocketType::Image) return true;
    }

    const bool channelToLayer =
        fromSocket.type == SocketType::Channel &&
        toSocket.type == SocketType::Image &&
        to.kind == NodeKind::Layer &&
        toSocketId == kImageInputSocketId;
    if (channelToLayer) {
        return true;
    }
    const bool channelToDataProcessor =
        fromSocket.type == SocketType::Channel &&
        toSocket.type == SocketType::Image &&
        ((to.kind == NodeKind::DataMath && to.dataMathMode != DataMathMode::ImageAverage &&
          (IsDataMathInputSocketId(toSocketId) || toSocketId == kDataMathBaseInputSocketId)) ||
         (to.kind == NodeKind::Mix && (toSocketId == kMixInputASocketId || toSocketId == kMixInputBSocketId)) ||
         (to.kind == NodeKind::Reformat && toSocketId == kImageInputSocketId));
    if (channelToDataProcessor) return true;

    const bool channelAssemblyTarget = namedChannelAssemblyTarget;
    const bool extentReferenceTarget =
        to.kind == NodeKind::ConstantChannel &&
        toSocketId == kMatchExtentInputSocketId;
    // Only unresolved polymorphic cases need an upstream descriptor. Ordinary
    // image/uniform/named-channel links above are decided by their contracts.
    const auto value=DescribeGraphOutput(graph,fromNodeId,fromSocketId).descriptor.logicalType;
    if (IsSingleChannelValue(value)) {
        const auto contract=EditorNodeGraphDefinitions::GetLivePortContract(graph,to,toSocketId);
        if (contract && std::find(contract->port.acceptedLogicalTypes.begin(),contract->port.acceptedLogicalTypes.end(),value)
            !=contract->port.acceptedLogicalTypes.end()) return true;
    }
    return fromSocket.type == SocketType::Image && toSocket.type == SocketType::Channel &&
        (channelAssemblyTarget || extentReferenceTarget) && IsSingleChannelValue(value);
}

} // namespace EditorNodeGraph::ConnectionRules
