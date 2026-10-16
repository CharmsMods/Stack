#include "Project/RawLayerStack.h"
#include "Graph/LayerOperationParameters.h"
#include "Graph/GraphDocumentRules.h"
#include "Project/RawNodeEditing.h"
#include "Project/RawCapturedMask.h"
#include "Editor/LayerRegistry.h"

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
    const std::vector<RawRecipe::GraphOperationKind> initial = layer.id == kRawBackgroundId
        ? std::vector<RawRecipe::GraphOperationKind>{RawRecipe::GraphOperationKind::LocalEv,
            RawRecipe::GraphOperationKind::ToneCurve, RawRecipe::GraphOperationKind::Calibration,
            RawRecipe::GraphOperationKind::ColorWarp}
        : std::vector<RawRecipe::GraphOperationKind>{};
    int position = 1;
    for (auto kind : initial) {
        auto* node = layer.graph.AddRawOperationNode(kind, {260.f * position++, 0});
        const int id = node->id;
        if (!layer.graph.TryConnectSockets(previous, "imageOut", id, "imageIn", &error))
            throw std::runtime_error(error);
        previous = id;
    }
    auto* result = layer.graph.AddOutputNode({260.f * position, 0}, true);
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
        type != NodeMath::LogicalValueType::Scalar) {
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

namespace {
std::string AddDefaultAdjustment(RawLayerStackState& state, const std::string& layerId,
    const std::string& afterOperationUuid, std::string& error) {
    error.clear();
    auto candidate = state;
    auto* layer = FindRawAdjustmentLayer(candidate, layerId);
    if (!layer) { error = "The selected layer no longer exists."; return {}; }
    const auto selected = std::find_if(layer->graph.GetNodes().begin(), layer->graph.GetNodes().end(),
        [&](const auto& node) {
            return node.instanceUuid == afterOperationUuid;
        });
    if (selected == layer->graph.GetNodes().end() || !RawNodeAcceptsMask(layer->graph,*selected)) {
        error = "Select an adjustment before drawing a mask."; return {};
    }
    std::vector<EditorNodeGraph::Link> outgoing;
    for (const auto& link : layer->graph.GetLinks())
        if (link.fromNodeId == selected->id && link.fromSocketId == "imageOut") outgoing.push_back(link);
    if (outgoing.size() != 1) {
        error = "Connect this adjustment to one next step in Graph before drawing a new mask."; return {};
    }
    const auto position = selected->position;
    const auto sourceType = *selected;
    for (auto& node : layer->graph.EditNodes())
        if (node.position.x > position.x) node.position.x += 260.f;
    // Copy only the operation type. Parameters, drivers and masks belong to the
    // original instance; the new operation starts with its definition's defaults.
    auto* added = layer->graph.AddDefaultOperationNode(sourceType,{position.x+260,position.y});
    if (!added) { error="This operation has no available default definition.";return {}; }
    added->title=sourceType.title;
    if (added->kind==EditorNodeGraph::NodeKind::Layer) {
        const auto defaults=LayerRegistry::CreateLayer(added->layerType);
        if(!defaults) {error="This effect is unavailable.";return {};}
        GraphModel::ReindexLayerParameterRecords(layer->graph);
        added->layerParameters=GraphModel::CaptureLayerParameters(*defaults);
    }
    const int addedId = added->id;
    const auto uuid = added->instanceUuid;
    const auto next = outgoing.front();
    // This command inserts the processed result. Generic socket matching can
    // choose the input-image tap instead and silently bypass the new edit.
    auto proposal = GraphModel::ProposeEdit(layer->graph,layer->graph.GetStructureRevision(),[&](auto& graph) {
        if (!graph.TryConnectSockets(next.fromNodeId,"imageOut",addedId,"imageIn",&error) ||
            !graph.TryConnectSockets(addedId,"imageOut",next.toNodeId,next.toSocketId,&error))
            throw std::runtime_error(error);
    });
    if (!GraphModel::ApplyEdit(layer->graph,layer->graph.GetStructureRevision(),std::move(proposal),error)) return {};
    if (!ValidateRawLayerStack(candidate,error)) return {};
    state = std::move(candidate);
    return uuid;
}

std::string MaskAdjustment(RawLayerStackState& state, const std::string& layerId,
    const std::string& operationUuid, std::string& error) {
    const auto* layer = FindRawAdjustmentLayer(state,layerId);
    if (!layer) { error="The selected layer no longer exists."; return {}; }
    if (layer->id == kRawBackgroundId) return AddDefaultAdjustment(state,layer->id,operationUuid,error);
    for (const auto& node : layer->graph.GetNodes())
        if (node.instanceUuid==operationUuid && RawNodeAcceptsMask(layer->graph,node)) return operationUuid;
    error="Select an adjustment with a mask input.";return {};
}

RawLocalAdjustment AttachShape(RawLayerStackState& state, const std::string& layerId,
    const std::string& uuid, EditorNodeGraph::MaskGeneratorKind shape, std::string& error) {
    using Mask = EditorNodeGraph::MaskGeneratorKind;
    auto candidate=state;
    auto* layer=FindRawAdjustmentLayer(candidate,layerId);
    const auto ownerId=layer->id;
    const auto operation=std::find_if(layer->graph.GetNodes().begin(),layer->graph.GetNodes().end(),
        [&](const auto& node){return node.instanceUuid==uuid;});
    if (operation==layer->graph.GetNodes().end()) {error="The selected adjustment no longer exists.";return {};}
    const auto position=operation->position;
    const auto mask = AddRawShapeMask(candidate,ownerId,shape,
        shape == Mask::LinearGradient ? "Linear" : shape == Mask::RadialGradient ? "Radial" : "Square");
    if (!SetRawOperationMask(candidate,ownerId,uuid,mask,error)) return {};
    const auto* publication = FindRawMaskOutput(candidate,mask);
    const auto* source = publication ? FindRawPublishedNode(*layer,*publication) : nullptr;
    if (!source) { error = "The new mask has no shape source."; return {}; }
    layer->graph.FindNode(source->id)->position = {position.x,position.y+220};
    if (!ValidateRawLayerStack(candidate,error)) return {};
    state = std::move(candidate);
    return {ownerId,uuid,mask};
}
}

RawLocalAdjustment AddRawLocalAdjustment(RawLayerStackState& state, const std::string& layerId,
    const std::string& operationUuid, EditorNodeGraph::MaskGeneratorKind shape, std::string& error) {
    using Mask=EditorNodeGraph::MaskGeneratorKind;
    if (shape!=Mask::LinearGradient && shape!=Mask::RadialGradient && shape!=Mask::Square) {
        error="Choose Linear, Radial or Square.";return {};
    }
    auto candidate=state;
    const auto uuid=AddDefaultAdjustment(candidate,layerId,operationUuid,error);
    if (uuid.empty()) return {};
    const auto created=AttachShape(candidate,layerId,uuid,shape,error);
    if (created.operationUuid.empty()) return {};
    state=std::move(candidate);return created;
}

RawLocalAdjustment ApplyRawShapeMask(RawLayerStackState& state, const std::string& layerId,
    const std::string& operationUuid, EditorNodeGraph::MaskGeneratorKind shape, std::string& error) {
    using Mask=EditorNodeGraph::MaskGeneratorKind;
    if (shape!=Mask::LinearGradient && shape!=Mask::RadialGradient && shape!=Mask::Square) {
        error="Choose Linear, Radial or Square.";return {};
    }
    auto candidate=state;
    const auto uuid=MaskAdjustment(candidate,layerId,operationUuid,error);
    if (uuid.empty()) return {};
    const auto created=AttachShape(candidate,layerId,uuid,shape,error);
    if (created.operationUuid.empty()) return {};
    state=std::move(candidate);return created;
}

RawLocalAdjustment ApplyRawCapturedMask(RawLayerStackState& state, const std::string& layerId,
    const std::string& operationUuid, const Assets::FloatData& pixels, const std::string& name, std::string& error) {
    error.clear();
    auto candidate=state;
    // Validate pixels before preparing the insertion. All changes are committed
    // together, including the mask and its attachment, or none of them are.
    EditorNodeGraph::CustomMaskPayload payload;
    try { payload=CapturedMaskPayload(pixels); }
    catch (const std::exception& failure) {error=failure.what();return {};}
    const auto uuid=MaskAdjustment(candidate,layerId,operationUuid,error);
    if (uuid.empty()) return {};
    auto* layer=FindRawAdjustmentLayer(candidate,layerId);
    const auto operation=std::find_if(layer->graph.GetNodes().begin(),layer->graph.GetNodes().end(),
        [&](const auto& node){return node.instanceUuid==uuid;});
    const auto position=operation->position;
    const auto ownerId=layer->id;
    auto* texture=layer->graph.AddCustomMaskNode(std::move(payload),{position.x,position.y+220});
    if (!texture) {error="Could not create the captured mask texture.";return {};}
    texture->title=name;texture->rawCoverage={{"canvas","project"}};
    const auto textureUuid=texture->instanceUuid;
    RegisterRawMaskOutputs(candidate,ownerId);
    const auto output=std::find_if(layer->maskOutputs.begin(),layer->maskOutputs.end(),
        [&](const auto& item){return item.nodeUuid==textureUuid;});
    if (output==layer->maskOutputs.end()) {error="The captured mask has no texture output.";return {};}
    const RawMaskReference mask{ownerId,output->id};
    if (!SetRawOperationMask(candidate,ownerId,uuid,mask,error)) return {};
    const int textureId=texture->id;
    int operationId=0;
    for(const auto& node:layer->graph.GetNodes()) if(node.instanceUuid==uuid) operationId=node.id;
    const int settingsIndex=static_cast<int>(GraphModel::LayerParameterRecords(layer->graph).size());
    const int blurId=layer->graph.AddLayerNode(LayerType::GaussianBlur,settingsIndex,
        {position.x+220,position.y+220})->id;
    layer->graph.FindNode(blurId)->layerParameters["amount"]=100.f;
    if (!layer->graph.TryConnectSockets(textureId,"maskOut",blurId,"imageIn",&error) ||
        !layer->graph.TryConnectSockets(blurId,"imageOut",operationId,"maskIn",&error) ||
        !ValidateRawLayerStack(candidate,error)) return {};
    state=std::move(candidate);return {ownerId,uuid,mask};
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
    const auto endpoint = RawMaskEndpoint(*owner, *producer);
    if (owner->id == layer->id && (producer->kind == EditorNodeGraph::NodeKind::MaskGenerator || producer->kind == EditorNodeGraph::NodeKind::MaskTexture || producer->kind == EditorNodeGraph::NodeKind::CustomMask))
        return layer->graph.TryConnectSockets(producer->id,endpoint.portId,target,"maskIn",&error);
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
    if (!reference) return std::nullopt;
    const bool shared = reference->role == Role::Reference;
    const auto* owner = shared ? FindRawAdjustmentLayer(state, reference->reference.graphId) : &layer;
    if (!owner) return std::nullopt;
    for (const auto& output : owner->maskOutputs) {
        const auto* node = FindRawPublishedNode(*owner, output);
        if (node && node->instanceUuid == (shared ? reference->reference.nodeUuid : reference->instanceUuid))
            return RawMaskReference{owner->id, output.id};
    }
    return std::nullopt;
}
} // namespace Stack::Project
