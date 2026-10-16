#include "Project/RawProjectGraph.h"
#include "Graph/GraphDocumentRules.h"
#include "Graph/LayerOperationParameters.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace Stack::Project {
namespace {
using namespace EditorNodeGraph;
using Role = GraphModel::NodeRole;
std::string Key(const GraphModel::Endpoint& endpoint) {
    return endpoint.graphId + "/" + endpoint.nodeUuid + "/" + endpoint.portId;
}
bool MatchedLayerBoundaries(const Graph& graph,std::string& error) {
    for (const auto& end:graph.GetNodes()) {
        if (end.role!=Role::LayerResult || end.rawLayerOwnerId.empty()) continue;
        std::vector<int> pending{end.id};
        std::unordered_set<int> visited;
        while (!pending.empty()) {
            const int id=pending.back(); pending.pop_back();
            if (!visited.insert(id).second) continue;
            const auto* node=graph.FindNode(id);
            if (!node) continue;
            if (node->role==Role::CurrentImage) {
                if (node->rawLayerOwnerId!=end.rawLayerOwnerId) {
                    error="A layer's main image chain must reach its own Start, not another layer's Start.";
                    return false;
                }
                continue;
            }
            if ((node->role==Role::LayerResult && id!=end.id) || node->role==Role::OriginalImage) continue;
            for (const auto& edge:graph.GetLinks()) if (edge.toNodeId==id) {
                const auto& port=edge.toSocketId;
                if (port=="maskIn" || port=="factor" || port==kMatchExtentInputSocketId ||
                    port.rfind("param:",0)==0 || port.rfind("gradient:",0)==0 || port.rfind("area:",0)==0) continue;
                pending.push_back(edge.fromNodeId);
            }
        }
    }
    return true;
}
}

RawProjectGraph BuildRawProjectGraph(const RawLayerStackState& state) {
    RawProjectGraph result;
    result.graph.SetAllowNoOutput(true);
    auto* source = result.graph.AddImageNode({}, {0,0});
    // Stable view identities must also satisfy the graph's UUID contract.
    // These two view-only nodes are never written into the owned layer graphs.
    source->instanceUuid = "3d5421eb-6aaa-4aa3-829d-77ca0c2dc823";
    source->role = Role::OriginalImage;
    source->title = "Source preparation";
    result.preparedSource = source->id;
    std::unordered_map<std::string,int> endpoints;
    endpoints[Key(RawPreparedSourceEndpoint())] = source->id;
    float nextX = 300.f;
    int nextSettingIndex=0;
    for (const auto* layer : RawLayersInOrder(state)) {
        const auto origin = layer->canvasOrigin.value_or(Vec2{nextX,0});
        result.origins.emplace(layer->id,origin);
        float right = origin.x;
        for (const auto& authored : layer->graph.GetNodes()) {
            const int id = result.graph.AddImageNode({},{})->id;
            Node copy = authored;
            copy.id = id;
            copy.rawLayerOwnerId = layer->id;
            copy.position.x += origin.x;
            copy.position.y += origin.y;
            copy.expanded = true;
            if (copy.kind == NodeKind::Layer) copy.layerIndex=nextSettingIndex++;
            if (copy.role == Role::CurrentImage) copy.title = layer->name + " Start";
            if (copy.role == Role::LayerResult) {
                copy.title = layer->name + " End";
                copy.outputEnabled = layer->enabled;
            }
            right = std::max(right,copy.position.x+300.f);
            *result.graph.FindNode(id) = std::move(copy);
            endpoints[Key(RawLayerEndpoint(*layer,authored))] = id;
            for (const auto& socket : layer->graph.GetSockets(authored,false))
                if (socket.direction == SocketDirection::Output)
                    endpoints[Key(RawLayerEndpoint(*layer,authored,socket.id))] = id;
        }
        nextX = right + 140.f;
    }
    auto connect = [&](const GraphModel::Endpoint& from, int target, const std::string& input) {
        const auto found = endpoints.find(Key(from));
        if (found != endpoints.end()) result.graph.EditLinks().push_back({found->second,from.portId,target,input});
    };
    for (const auto* layer : RawLayersInOrder(state)) {
        for (const auto& authored : layer->graph.GetNodes()) {
            const int id = endpoints.at(Key(RawLayerEndpoint(*layer,authored)));
            if (authored.role == Role::CurrentImage) {
                if (const auto input = RawLayerInputEndpoint(state,layer->id)) connect(*input,id,"imageIn");
            } else if (authored.role == Role::Reference) {
                connect(authored.reference,id,"referenceIn");
                if (!endpoints.count(Key(authored.reference)))
                    result.externalReferences[layer->id + "/" + authored.instanceUuid] = authored.reference;
            }
        }
        for (const auto& link : layer->graph.GetLinks()) {
            const auto* from = layer->graph.FindNode(link.fromNodeId);
            const auto* to = layer->graph.FindNode(link.toNodeId);
            if (from && to) connect(RawLayerEndpoint(*layer,*from,link.fromSocketId),
                endpoints.at(Key(RawLayerEndpoint(*layer,*to))),link.toSocketId);
        }
        for (const auto& definition : layer->graph.GetCompoundDefinitions()) {
            if (result.graph.FindCompoundDefinition(definition.identity)) continue;
            std::string error;
            if (!result.graph.AddCompoundDefinition(definition,&error)) throw std::runtime_error(error);
        }
        for (const auto& group : layer->graph.GetGroups()) {
            auto copy = group;
            copy.id = result.graph.GetNextGroupId();
            result.graph.SetNextGroupId(copy.id + 1);
            copy.position.x += result.origins.at(layer->id).x;
            copy.position.y += result.origins.at(layer->id).y;
            result.groupOwners[copy.id] = {layer->id,group.id};
            result.graph.GetGroups().push_back(std::move(copy));
        }
    }
    auto* output = result.graph.AddOutputNode({nextX,0},true);
    output->instanceUuid = "bb276ad4-1b38-49b7-a9c5-34b977a28ec2";
    output->title = "Image output";
    result.output = output->id;
    if (const auto input = RawMainOutputEndpoint(state)) connect(*input,result.output,"imageIn");
    return result;
}

bool ApplyRawProjectGraph(const RawLayerStackState& source, const RawProjectGraph& projection,
    const Graph& edited, const nlohmann::json& settings, const std::string& insertionOwner,
    RawLayerStackState& result, std::string& error) {
    const auto fail = [&](const std::string& reason) { error = reason; return false; };
    if (!settings.is_array()) return fail("Invalid operation settings.");
    if (!MatchedLayerBoundaries(edited,error)) return false;
    if (!edited.FindNode(projection.preparedSource) || !edited.FindNode(projection.output))
        return fail("Keep the source preparation and image output nodes.");
    auto candidate = source;
    candidate.explicitRouting = true;
    candidate.imageOutput.reset();
    const auto* defaultOwner = FindRawAdjustmentLayer(source,insertionOwner);
    if (!defaultOwner) return fail("Choose a layer for the new operation.");
    std::unordered_map<int,GraphModel::Endpoint> identities;
    std::unordered_map<int,int> localIds;
    std::unordered_map<std::string,int> nextIds;
    for (const auto* layer : RawLayersInOrder(source)) {
        auto* changed = FindRawAdjustmentLayer(candidate,layer->id);
        changed->graph.EditNodes().clear();
        changed->graph.EditLinks().clear();
        changed->graph.GetGroups().clear();
        changed->imageInput.reset();
        if (const auto origin = projection.origins.find(layer->id); origin != projection.origins.end())
            changed->canvasOrigin = origin->second;
        nextIds[layer->id] = layer->graph.GetNextNodeId();
    }
    identities[projection.preparedSource] = RawPreparedSourceEndpoint();
    for (const auto& node : edited.GetNodes()) {
        if (node.id == projection.preparedSource || node.id == projection.output) continue;
        const auto owner = node.rawLayerOwnerId.empty() ? defaultOwner->id : node.rawLayerOwnerId;
        auto* layer = FindRawAdjustmentLayer(candidate,owner);
        const auto* original = FindRawAdjustmentLayer(source,owner);
        if (!layer || !original) return fail("An operation has no owning layer.");
        const auto previous = std::find_if(original->graph.GetNodes().begin(),original->graph.GetNodes().end(),
            [&](const auto& item) { return item.instanceUuid == node.instanceUuid; });
        Node copy = node;
        if (previous == original->graph.GetNodes().end() && copy.kind == NodeKind::MaskGenerator && copy.role == Role::Ordinary &&
            (copy.maskKind == MaskGeneratorKind::LinearGradient || copy.maskKind == MaskGeneratorKind::RadialGradient || copy.maskKind == MaskGeneratorKind::Square) &&
            !edited.FindInputLink(node.id,"matchExtent")) {
            copy.kind = NodeKind::MaskTexture;
            copy.rawCoverage["canvas"] = "project";
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(copy);
        }
        const int id = previous != original->graph.GetNodes().end() ? previous->id : nextIds[owner]++;
        copy.id = id;
        copy.rawLayerOwnerId.clear();
        copy.position.x -= layer->canvasOrigin ? layer->canvasOrigin->x : 0.f;
        copy.position.y -= layer->canvasOrigin ? layer->canvasOrigin->y : 0.f;
        if (copy.role == Role::CurrentImage) copy.title = "Layer Start";
        if (copy.role == Role::LayerResult) {
            copy.title = "Layer End";
            layer->enabled = copy.outputEnabled;
            copy.outputEnabled = true;
        }
        if (copy.kind == NodeKind::Layer) {
            if (!GraphModel::ValidateLayerParameters(copy,error)) return false;
        }
        if (copy.role == Role::Reference) {
            const auto external = projection.externalReferences.find(owner + "/" + copy.instanceUuid);
            copy.reference = external == projection.externalReferences.end()
                ? GraphModel::Endpoint{"$missing",copy.instanceUuid,"imageOut"} : external->second;
        }
        layer->graph.EditNodes().push_back(std::move(copy));
        GraphModel::ReindexLayerParameterRecords(layer->graph);
        identities[node.id] = {owner,node.instanceUuid,"imageOut"};
        localIds[node.id] = id;
    }
    for (const auto& connection : edited.GetLinks()) {
        const auto* from = edited.FindNode(connection.fromNodeId);
        const auto* to = edited.FindNode(connection.toNodeId);
        const auto found = identities.find(connection.fromNodeId);
        if (!from || !to || found == identities.end()) return fail("A connection has no source.");
        auto endpoint = found->second;
        endpoint.portId = connection.fromSocketId;
        if (connection.toNodeId == projection.output) {
            if (connection.toSocketId != "imageIn") return fail("Invalid output input.");
            candidate.imageOutput = endpoint;
            continue;
        }
        const auto target = identities.find(connection.toNodeId);
        if (target == identities.end()) return fail("Source preparation cannot receive a creative result.");
        auto* layer = FindRawAdjustmentLayer(candidate,target->second.graphId);
        auto* node = layer->graph.FindNode(localIds.at(to->id));
        if (node->role == Role::CurrentImage) {
            if (connection.toSocketId != "imageIn") return fail("Invalid layer Start input.");
            layer->imageInput = endpoint;
            continue;
        }
        if (node->role == Role::Reference && connection.toSocketId == "referenceIn") {
            node->reference = endpoint;
            continue;
        }
        if (endpoint.graphId != layer->id) {
            if (node->role == Role::LayerResult)
                return fail("A layer Start and End must enclose their own operations.");
            layer->graph.SetNextNodeId(nextIds[layer->id]);
            const auto type = DescribeGraphOutput(edited,from->id,connection.fromSocketId).descriptor.logicalType;
            const bool mask = IsSingleChannelValue(type);
            Node* reference = nullptr;
            for (auto& existing : layer->graph.EditNodes())
                if (existing.role == Role::Reference && existing.reference == endpoint) { reference=&existing; break; }
            if (!reference) {
                const auto* original=FindRawAdjustmentLayer(source,layer->id);
                for (const auto& existing:original->graph.GetNodes())
                    if (existing.role==Role::Reference && existing.reference==endpoint && !layer->graph.FindNode(existing.id)) {
                        layer->graph.EditNodes().push_back(existing);
                        reference=&layer->graph.EditNodes().back(); break;
                    }
            }
            if (!reference) reference = mask ? layer->graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid,{-240,-300}) :
                layer->graph.AddImageNode({}, {-240,-300});
            reference->role = Role::Reference;
            reference->reference = endpoint;
            reference->referenceType = type;
            reference->title = from->title + " reference";
            const int referenceId = reference->id;
            nextIds[layer->id] = layer->graph.GetNextNodeId();
            layer->graph.EditLinks().push_back({referenceId,mask ? "maskOut" : "imageOut",localIds.at(to->id),connection.toSocketId});
        } else {
            layer->graph.EditLinks().push_back({localIds.at(from->id),connection.fromSocketId,localIds.at(to->id),connection.toSocketId});
        }
    }
    for (const auto& group : edited.GetGroups()) {
        const auto known = projection.groupOwners.find(group.id);
        const auto owner = known == projection.groupOwners.end() ? defaultOwner->id : known->second.first;
        auto* layer = FindRawAdjustmentLayer(candidate,owner);
        auto copy = group;
        copy.id = known == projection.groupOwners.end() ? layer->graph.GetNextGroupId() : known->second.second;
        layer->graph.SetNextGroupId(std::max(layer->graph.GetNextGroupId(),copy.id+1));
        if (layer->canvasOrigin) {
            copy.position.x -= layer->canvasOrigin->x;
            copy.position.y -= layer->canvasOrigin->y;
        }
        layer->graph.GetGroups().push_back(std::move(copy));
    }
    for (const auto* entry : RawLayersInOrder(candidate)) {
        auto* layer = FindRawAdjustmentLayer(candidate,entry->id);
        layer->graph.SetNextNodeId(nextIds.at(layer->id));
        layer->graph.GetCompoundDefinitions() = edited.GetCompoundDefinitions();
        RegisterRawMaskOutputs(candidate,layer->id);
    }
    if (!ValidateRawLayerStack(candidate,error)) return false;
    std::vector<std::string> order;
    auto cursor = candidate.imageOutput;
    std::unordered_set<std::string> visited;
    while (cursor && visited.insert(cursor->graphId).second) {
        const auto* layer = FindRawAdjustmentLayer(candidate,cursor->graphId);
        if (!layer) break;
        if (layer->id != kRawBackgroundId) order.insert(order.begin(),layer->id);
        cursor = layer->imageInput;
    }
    std::stable_sort(candidate.layers.begin(),candidate.layers.end(),[&](const auto& a,const auto& b) {
        return std::find(order.begin(),order.end(),a.id) < std::find(order.begin(),order.end(),b.id);
    });
    result = std::move(candidate);
    error.clear();
    return true;
}

} // namespace Stack::Project
