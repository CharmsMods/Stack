#include "Project/RawLayerStack.h"
#include "Graph/GraphDocumentRules.h"

#include <algorithm>
#include <stdexcept>

namespace Stack::Project {
using Role = GraphModel::NodeRole;

RawAdjustmentLayer MakeRawLayer(std::string id, std::string name) {
    RawAdjustmentLayer layer;
    layer.id = std::move(id); layer.name = std::move(name);
    layer.graph.SetAllowNoOutput(true);
    auto* original = layer.graph.AddImageNode({}, {0, -240});
    original->role = Role::OriginalImage; original->title = "Original image";
    auto* current = layer.graph.AddImageNode({}, {0, 0});
    current->role = Role::CurrentImage; current->title = "Current location image";
    int previous = current->id;
    std::string error;
    for (int i = 0; i < static_cast<int>(RawRecipe::GraphOperationKind::Count); ++i) {
        auto* node = layer.graph.AddRawOperationNode(static_cast<RawRecipe::GraphOperationKind>(i),
            {260.f * (i + 1), 0});
        const int id = node->id;
        if (!layer.graph.TryConnectSockets(previous, "imageOut", id, "imageIn", &error))
            throw std::runtime_error(error);
        previous = id;
    }
    auto* result = layer.graph.AddOutputNode({2080, 0}, true);
    result->role = Role::LayerResult; result->title = "Layer result";
    if (!layer.graph.TryConnectSockets(previous, "imageOut", result->id, "imageIn", &error))
        throw std::runtime_error(error);
    return layer;
}

bool PublishRawLayerResult(RawLayerStackState& state, const GraphModel::Endpoint& producer,
    const std::string& recipientId, std::string& error) {
    auto candidate = state;
    auto* owner = FindRawAdjustmentLayer(candidate,producer.graphId);
    auto* recipient = FindRawAdjustmentLayer(candidate,recipientId);
    if (!owner || !recipient) { error = "Choose an existing producing and receiving layer."; return false; }
    auto node = std::find_if(owner->graph.GetNodes().begin(),owner->graph.GetNodes().end(),
        [&](const auto& n) { return n.instanceUuid == producer.nodeUuid; });
    if (node == owner->graph.GetNodes().end()) { error = "The producing operation is unavailable."; return false; }
    const auto description = EditorNodeGraph::DescribeGraphOutput(owner->graph,node->id,producer.portId);
    const auto type = description.descriptor.logicalType;
    const bool mask = EditorNodeGraph::IsSingleChannelValue(type) ||
        (node->kind == EditorNodeGraph::NodeKind::Output && node->outputSettings.maskOutput);
    if (!mask && type != NodeMath::LogicalValueType::ColorImage && type != NodeMath::LogicalValueType::Invalid &&
        type != NodeMath::LogicalValueType::Scalar && type != NodeMath::LogicalValueType::ComplexSpectrum &&
        type != NodeMath::LogicalValueType::FrequencyResponse && type != NodeMath::LogicalValueType::SpectrumMagnitude &&
        type != NodeMath::LogicalValueType::SpectrumPhase) {
        error = "This result type has no executable cross-graph input yet."; return false;
    }
    auto publication = producer;
    const std::string title = node->title + " result";
    if (node->kind != EditorNodeGraph::NodeKind::Output) {
        const int nodeId = node->id;
        const auto position = node->position;
        auto* output = owner->graph.AddOutputNode({position.x+250,position.y+170},false);
        const int outputId = output->id;
        output->title = title; output->outputSettings.maskOutput = mask;
        if (!mask && type != NodeMath::LogicalValueType::ColorImage)
            output->outputSettings.publishedType = type;
        publication = RawLayerEndpoint(*owner,*output);
        if (!owner->graph.TryConnectSockets(nodeId,producer.portId,outputId,"imageIn",&error)) return false;
    }
    auto* reference = mask ? recipient->graph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid,{0,-400}) :
        recipient->graph.AddImageNode({}, {0,-400});
    reference->role = Role::Reference; reference->reference = publication; reference->title = owner->name + " / " + title;
    reference->referenceType = type;
    RegisterRawMaskOutputs(candidate,owner->id);
    if (!ValidateRawLayerStack(candidate,error)) return false;
    state = std::move(candidate); return true;
}

std::vector<const RawAdjustmentLayer*> RawLayersInOrder(const RawLayerStackState& state) {
    std::vector<const RawAdjustmentLayer*> layers{&state.background};
    for (const auto& layer : state.layers) layers.push_back(&layer);
    return layers;
}
const EditorNodeGraph::Node* FindRawOperation(const RawAdjustmentLayer& layer,
    RawRecipe::GraphOperationKind kind, const std::string& preferredUuid) {
    const EditorNodeGraph::Node* first = nullptr;
    for (const auto& node : layer.graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawOperation || node.rawOperation.kind != kind) continue;
        if (node.instanceUuid == preferredUuid) return &node;
        if (!first) first = &node;
    }
    return preferredUuid.empty() ? first : nullptr;
}
EditorNodeGraph::Node* FindRawOperation(RawAdjustmentLayer& layer,
    RawRecipe::GraphOperationKind kind, const std::string& preferredUuid) {
    const auto* found = FindRawOperation(static_cast<const RawAdjustmentLayer&>(layer), kind, preferredUuid);
    return found ? layer.graph.FindNode(found->id) : nullptr;
}
const EditorNodeGraph::Node* FindRawRole(const RawAdjustmentLayer& layer, Role role) {
    for (const auto& node : layer.graph.GetNodes()) if (node.role == role) return &node;
    return nullptr;
}
GraphModel::Endpoint RawLayerEndpoint(const RawAdjustmentLayer& layer,
    const EditorNodeGraph::Node& node, const std::string& port) {
    return {layer.id, node.instanceUuid, port};
}

std::string AddRawMaskedCurve(RawLayerStackState& state, const std::string& layerId,
    const std::string& afterUuid, RawRecipe::GraphOperationKind kind,
    EditorNodeGraph::MaskGeneratorKind maskKind, std::string& error) {
    auto* layer = FindRawAdjustmentLayer(state,layerId);
    if (!layer || (kind != RawRecipe::GraphOperationKind::LuminanceTone && kind != RawRecipe::GraphOperationKind::RgbCurves)) {
        error = "Select a tone operation first."; return {};
    }
    const auto* selected = FindRawOperation(*layer,kind,afterUuid);
    if (!selected) { error = "The selected curve no longer exists."; return {}; }
    std::vector<EditorNodeGraph::Link> outgoing;
    for (const auto& link : layer->graph.GetLinks())
        if (link.fromNodeId == selected->id && link.fromSocketId == "imageOut") outgoing.push_back(link);
    if (outgoing.size() != 1) { error = "Choose the insertion point in Graph for this branched or disconnected curve."; return {}; }
    const auto position = selected->position;
    auto* added = layer->graph.AddRawOperationNode(kind,{position.x+240,position.y});
    const int addedId = added->id;
    const auto uuid = added->instanceUuid;
    auto proposal = GraphModel::ProposeInsertion(layer->graph,layer->graph.GetStructureRevision(),addedId,outgoing.front());
    if (!GraphModel::ApplyEdit(layer->graph,layer->graph.GetStructureRevision(),std::move(proposal),error)) return {};
    const auto mask = AddRawGeneratedMask(state,layerId,maskKind,"Local curve coverage");
    if (!SetRawOperationMask(state,layerId,uuid,mask,error)) return {};
    const auto* publication = FindRawMaskOutput(state,mask);
    const auto* output = publication ? FindRawPublishedNode(*layer,*publication) : nullptr;
    const auto* coverage = output ? layer->graph.FindInputLink(output->id,"imageIn") : nullptr;
    if (coverage && !layer->graph.TryConnectSockets(addedId,"inputImageOut",coverage->fromNodeId,"matchExtent",&error)) return {};
    return ValidateRawLayerStack(state,error) ? uuid : std::string{};
}

bool SetRawOperationMask(RawLayerStackState& state, const std::string& layerId,
    const std::string& nodeUuid, const std::optional<RawMaskReference>& mask, std::string& error) {
    auto* layer = FindRawAdjustmentLayer(state, layerId);
    if (!layer) { error = "The operation's layer no longer exists."; return false; }
    int target = 0;
    for (const auto& node : layer->graph.GetNodes()) if (node.instanceUuid == nodeUuid) target = node.id;
    if (!target) { error = "The selected operation no longer exists."; return false; }
    if (const auto* link = layer->graph.FindInputLink(target, "maskIn")) {
        const auto old = *link;
        layer->graph.RemoveLink(old.fromNodeId, old.fromSocketId, old.toNodeId, old.toSocketId);
    }
    if (!mask) return true;
    const auto* owner = FindRawAdjustmentLayer(state, mask->layerId);
    const auto* output = FindRawMaskOutput(state, *mask);
    const auto* producer = owner && output ? FindRawPublishedNode(*owner, *output) : nullptr;
    if (!producer) { error = "The mask output no longer exists."; return false; }
    const auto endpoint = RawLayerEndpoint(*owner, *producer);
    auto* reference = layer->graph.AddMaskGeneratorNode(EditorNodeGraph::MaskGeneratorKind::Solid, {0, 300});
    reference->role = Role::Reference; reference->reference = endpoint;
    reference->title = output->name;
    return layer->graph.TryConnectSockets(reference->id, "maskOut", target, "maskIn", &error);
}

std::optional<RawMaskReference> GetRawOperationMask(const RawLayerStackState& state,
    const RawAdjustmentLayer& layer, const std::string& nodeUuid) {
    const EditorNodeGraph::Node* target = nullptr;
    for (const auto& node : layer.graph.GetNodes()) if (node.instanceUuid == nodeUuid) target = &node;
    const auto* link = target ? layer.graph.FindInputLink(target->id, "maskIn") : nullptr;
    const auto* reference = link ? layer.graph.FindNode(link->fromNodeId) : nullptr;
    if (!reference || reference->role != Role::Reference) return std::nullopt;
    const auto* owner = FindRawAdjustmentLayer(state, reference->reference.graphId);
    if (!owner) return std::nullopt;
    for (const auto& output : owner->maskOutputs) {
        const auto* node = FindRawPublishedNode(*owner, output);
        if (node && node->instanceUuid == reference->reference.nodeUuid) return RawMaskReference{owner->id, output.id};
    }
    return std::nullopt;
}
} // namespace Stack::Project
