#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Graph/GraphImageRules.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Raw/RawGraphParameters.h"
#include <algorithm>

namespace EditorNodeGraphDefinitions {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;

std::vector<EditorNodeGraph::SocketDefinition> BuildRegisteredSockets(
    const Node& node,
    bool visibleOnly) {
    // MFD input sockets are stable per-frame bindings generated from the
    // project manifest. The live definition establishes the node identity,
    // while the instance remains authoritative for its dynamic RAW inputs.
    if (node.kind == NodeKind::RawOperation) {
        auto sockets = BuildSockets(node, visibleOnly);
        for (const auto& parameter : Stack::RawRecipe::GraphParameters(node.rawOperation)) {
            const bool exposed = std::find(node.exposedParameterIds.begin(), node.exposedParameterIds.end(),
                parameter.id) != node.exposedParameterIds.end();
            if (visibleOnly && !exposed) continue;
            EditorNodeGraph::SocketDefinition socket{EditorNodeGraph::ParameterInputSocketId(parameter.id), node.id,
                EditorNodeGraph::SocketDirection::Input, EditorNodeGraph::SocketType::Scalar, parameter.label, true, exposed};
            socket.logicalType = LogicalValueType::Scalar;
            sockets.push_back(std::move(socket));
        }
        return sockets;
    }
    if (node.kind == NodeKind::MultiFrameDenoise ||
        node.kind == NodeKind::MultiFrameHdr) {
        return BuildSockets(node, visibleOnly);
    }
    const LiveNodeDefinition* definition = FindLiveNodeDefinition(node);
    if (!definition) return BuildSockets(node, visibleOnly);
    std::vector<EditorNodeGraph::SocketDefinition> sockets;
    for (EditorNodeGraph::SocketDefinition socket : definition->sockets) {
        if (visibleOnly && !socket.visible) continue;
        socket.nodeId = node.id;
        sockets.push_back(std::move(socket));
    }
    if (node.kind == NodeKind::Layer) for (const auto& parameter : definition->parameters) {
        if (!parameter.graphInputCapable) continue;
        const bool exposed = std::find(node.exposedParameterIds.begin(),node.exposedParameterIds.end(),
            parameter.id) != node.exposedParameterIds.end();
        if (visibleOnly && !exposed) continue;
        EditorNodeGraph::SocketDefinition socket{EditorNodeGraph::ParameterInputSocketId(parameter.id),node.id,
            EditorNodeGraph::SocketDirection::Input,EditorNodeGraph::SocketType::Scalar,parameter.label,true,exposed};
        socket.logicalType=parameter.logicalType;
        sockets.push_back(std::move(socket));
    }
    return sockets;
}

bool DefinitionSupportsGraphRole(const LiveNodeDefinition& definition, LiveGraphRole role) {
    return definition.executable && (definition.graphRoles & static_cast<unsigned>(role)) != 0;
}

std::optional<LivePortContract> GetLivePortContract(const EditorNodeGraph::Graph& graph,
    const Node& node, const std::string& socketId) {
    using namespace Stack::NodeMath;
    EditorNodeGraph::SocketDefinition socket;
    if (!graph.FindSocket(node.id, socketId, &socket)) return std::nullopt;
    LivePortContract result;
    result.port.id = socket.id;
    result.port.direction = socket.direction == EditorNodeGraph::SocketDirection::Input ? PortDirection::Input : PortDirection::Output;
    result.port.logicalType = socket.logicalType;
    result.port.acceptedLogicalTypes = {socket.logicalType};
    result.port.optional = socket.optional;
    result.port.minimumConnections = socket.optional ? 0 : 1;
    result.port.maximumConnections = 1;
    result.requiredSemantics = MakeUnknownDescriptor(socket.logicalType);
    if (socket.type == EditorNodeGraph::SocketType::ImageOrChannel ||
        (socket.type == EditorNodeGraph::SocketType::Image && node.kind == NodeKind::Layer))
        result.port.acceptedLogicalTypes = {LogicalValueType::ColorImage,LogicalValueType::Channel,LogicalValueType::ScalarField,LogicalValueType::Mask};
    if (socket.type == EditorNodeGraph::SocketType::Mask || socket.type == EditorNodeGraph::SocketType::Channel ||
        socket.type == EditorNodeGraph::SocketType::ScalarField)
        result.port.acceptedLogicalTypes = {LogicalValueType::Channel,LogicalValueType::ScalarField,LogicalValueType::Mask};
    const auto* definition = FindLiveNodeDefinition(node);
    const bool sceneInput = (definition && definition->requiresSceneLinearRgb && socketId == "imageIn") ||
        (node.role == Stack::GraphModel::NodeRole::LayerResult && socketId == "imageIn");
    if (sceneInput) {
        result.port.acceptedLogicalTypes = {LogicalValueType::ColorImage};
        result.port.semanticRequirement = RequirementPolicy::Strict;
        result.port.requirementDescription = "Scene-linear RGB with straight or opaque alpha";
        result.requiredSemantics.transfer = SemanticField<TransferDescriptor>::Known({TransferKind::Linear,0,{}});
        result.requiredSemantics.reference = SemanticField<ReferenceState>::Known(ReferenceState::Scene);
        result.acceptedAlpha = {AlphaMode::Absent,AlphaMode::Straight,AlphaMode::Opaque};
    }
    if (socketId == "maskIn") result.defaultValue = 1.0;
    if (socketId.rfind("area:",0) == 0 || socketId.rfind("gradient:",0) == 0) result.defaultValue = 0.0;
    if (socketId.rfind("param:",0) == 0 && definition) {
        for (const auto& parameter : definition->parameters) if (socketId.substr(6) == parameter.id) {
            result.defaultValue = parameter.defaultValue;
            UnitDescriptor unit; unit.kind = parameter.units == "unitless" ? UnitKind::Unitless : UnitKind::Custom; unit.customKey = parameter.units == "unitless" ? std::string{} : parameter.units;
            result.port.units = SemanticField<UnitDescriptor>::Known(unit);
        }
    }
    if (socket.type == EditorNodeGraph::SocketType::Mask) {
        const auto* conversion = FindLiveNodeDefinition(NodeKind::ImageToMask, static_cast<int>(EditorNodeGraph::ImageToMaskKind::Luminance));
        if (conversion) result.explicitConversions.push_back(conversion->identity);
    }
    return result;
}

bool AcceptsTypedParameterInput(const Node& node, const std::string& socketId, EditorNodeGraph::SocketType type) {
    if (type != EditorNodeGraph::SocketType::Scalar) return false;
    if (node.kind == NodeKind::TechnicalImage) return socketId == EditorNodeGraph::kExposureValueInputSocketId;
    if (node.kind == NodeKind::DataMath && node.dataMathSettings.domain == Stack::NodeMath::ArithmeticDomain::Values)
        return EditorNodeGraph::IsDataMathInputSocketId(socketId);
    if (socketId.rfind("param:", 0) != 0) return false;
    const auto id = socketId.substr(6);
    if (node.kind == NodeKind::RawOperation || node.kind == NodeKind::Layer) {
        const auto* definition = FindLiveNodeDefinition(node);
        if (!definition) return false;
        return std::any_of(definition->parameters.begin(), definition->parameters.end(), [&](const auto& p) {
            return p.id == id && p.graphInputCapable;
        });
    }
    return std::find(node.exposedParameterIds.begin(), node.exposedParameterIds.end(), id) != node.exposedParameterIds.end();
}

bool OutputDependsOnInput(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node,
    const std::string& output, const std::string& input) {
    if (const auto* channel=EditorNodeGraph::FindChannelOutput(node,output))
        return OutputDependsOnInput(graph,node,channel->sourceSocket,input);
    if (node.role == Stack::GraphModel::NodeRole::LayerResult ||
        (!node.rawLayerOwnerId.empty() && node.role == Stack::GraphModel::NodeRole::CurrentImage))
        return output == "imageOut" && input == "imageIn";
    if (!node.rawLayerOwnerId.empty() && node.role == Stack::GraphModel::NodeRole::Reference)
        return input == "referenceIn";
    if (node.kind == EditorNodeGraph::NodeKind::Compound) {
        std::vector<std::string> dependencies;
        if (graph.ResolveCompoundOutputInputDependencies(node.id, output, dependencies))
            return std::find(dependencies.begin(), dependencies.end(), input) != dependencies.end();
    }
    return OutputDependsOnInput(node, output, input);
}

bool OutputDependsOnInput(const EditorNodeGraph::Node& node,
    const std::string& output, const std::string& input) {
    const auto* definition = FindLiveNodeDefinition(node);
    return !definition || Stack::GraphModel::OutputDependsOnInput(
        definition->outputDependencies, output, input);
}

bool ValidateInputDescriptor(const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node, const std::string& socketId,
    const Stack::NodeMath::ValueDescriptor& value, std::string& error) {
    using namespace Stack::NodeMath;
    const bool single = value.logicalType == LogicalValueType::Channel || value.logicalType == LogicalValueType::Mask || value.logicalType == LogicalValueType::ScalarField;
    if (node.kind == NodeKind::Mix && (socketId == "imageA" || socketId == "imageB")) {
        const auto* other = graph.FindInputLink(node.id,socketId == "imageA" ? "imageB" : "imageA");
        if (other && !Stack::GraphModel::ValidateImageCombination(value,
                EditorNodeGraph::DescribeGraphOutput(graph,other->fromNodeId,other->fromSocketId).descriptor,error)) return false;
    }
    const auto* definition=FindLiveNodeDefinition(node);
    const bool photo = (definition && definition->requiresSceneLinearRgb && socketId == "imageIn") ||
        (node.role == Stack::GraphModel::NodeRole::LayerResult && socketId == "imageIn");
    const bool coverage = socketId == "maskIn" || (node.kind == NodeKind::Output && node.outputSettings.maskOutput && socketId == "imageIn");
    if (photo && value.logicalType != LogicalValueType::Invalid && value.logicalType != LogicalValueType::ColorImage) {
        error = "Photo operations require a full scene-linear color image."; return false;
    }
    if (coverage && value.logicalType == LogicalValueType::ColorImage) {
        error = "Coverage needs one channel. Select a channel or convert the image to a mask."; return false;
    }
    const auto contract = GetLivePortContract(graph, node, socketId);
    if (!contract || contract->port.semanticRequirement != RequirementPolicy::Strict || single) return true;
    const auto& required = contract->requiredSemantics;
    if ((required.transfer.state == KnowledgeState::Known && value.transfer.state == KnowledgeState::Known && value.transfer.value.kind != required.transfer.value.kind) ||
        (required.reference.state == KnowledgeState::Known && value.reference.state == KnowledgeState::Known && value.reference.value != required.reference.value)) {
        error = "This input requires " + contract->port.requirementDescription + ". Add an explicit conversion."; return false;
    }
    if (!contract->acceptedAlpha.empty() && value.alpha.state == KnowledgeState::Known &&
        std::find(contract->acceptedAlpha.begin(),contract->acceptedAlpha.end(),value.alpha.value) == contract->acceptedAlpha.end()) {
        error = "This input requires straight or opaque RGB. Unpremultiply alpha explicitly first."; return false;
    }
    if (!contract->acceptsUnknownSemantics && value.logicalType != LogicalValueType::Invalid &&
        ((required.transfer.state == KnowledgeState::Known && value.transfer.state != KnowledgeState::Known) ||
         (required.reference.state == KnowledgeState::Known && value.reference.state != KnowledgeState::Known))) {
        error = "This input needs known " + contract->port.requirementDescription + ". Declare or convert the source explicitly."; return false;
    }
    return true;
}


} // namespace EditorNodeGraphDefinitions
