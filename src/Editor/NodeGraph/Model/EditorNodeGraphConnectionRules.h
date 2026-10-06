#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace EditorNodeGraph::ConnectionRules {

inline bool IsChannelProcessingBridge(
    const Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    const SocketDefinition& fromSocket,
    const Node& to,
    const std::string& toSocketId,
    const SocketDefinition& toSocket) {
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
        if (fromSocket.type == SocketType::Channel) {
            return true;
        }
        if (fromSocket.type != SocketType::Image) {
            return false;
        }

        return true;
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
    if (fromSocket.type != SocketType::Image ||
        toSocket.type != SocketType::Channel ||
        (!channelAssemblyTarget &&
         !extentReferenceTarget)) {
        return false;
    }
    return graph.IsScalarSocketStream(fromNodeId, fromSocketId);
}

} // namespace EditorNodeGraph::ConnectionRules
