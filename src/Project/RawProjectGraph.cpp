#include "Project/RawProjectGraph.h"
#include "Editor/NodeGraph/CompoundInstantiation.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Graph/GraphDocumentRules.h"
#include "Graph/LayerOperationParameters.h"

#include <algorithm>
#include <cmath>
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

RawProjectGraph BuildRawProjectGraph(const RawLayerStackState& state, bool analysisOnly) {
    RawProjectGraph result;
    result.graph.SetAllowNoOutput(true);
    auto* source = result.graph.AddImageNode({},state.sourceCanvasPosition.value_or(Vec2{0,0}));
    // Stable view identities must also satisfy the graph's UUID contract.
    // These two view-only nodes are never written into the owned layer graphs.
    source->instanceUuid = "3d5421eb-6aaa-4aa3-829d-77ca0c2dc823";
    source->role = Role::OriginalImage;
    source->title = "Source preparation";
    source->notes = state.sourceCanvasNotes;
    source->channelOutputs = state.sourceChannelOutputs;
    result.preparedSource = source->id;
    std::unordered_map<std::string,int> endpoints;
    endpoints[Key(RawPreparedSourceEndpoint())] = source->id;
    for(const auto& output:source->channelOutputs) {
        auto endpoint=RawPreparedSourceEndpoint();endpoint.portId=output.id;
        endpoints[Key(endpoint)]=source->id;
    }
    float nextX = 300.f;
    int nextSettingIndex=0;
    for (const auto* layer : RawLayersInOrder(state)) {
        const auto origin = layer->canvasOrigin.value_or(Vec2{nextX,0});
        result.origins.emplace(layer->id,origin);
        float right = origin.x;
        for (const auto& authored : layer->graph.GetNodes()) {
            const int id = result.graph.AddImageNode({},{})->id;
            Node copy = analysisOnly ? CompoundInternal::CloneExecutionNode(authored) : authored;
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
            for(int& member:copy.compoundMembers) {
                const auto* node=layer->graph.FindNode(member);
                if(!node)throw std::runtime_error("A retained RAW group has a missing member.");
                member=endpoints.at(Key(RawLayerEndpoint(*layer,*node)));
            }
            result.graph.SetNextGroupId(copy.id + 1);
            copy.position.x += result.origins.at(layer->id).x;
            copy.position.y += result.origins.at(layer->id).y;
            result.groupOwners[copy.id] = {layer->id,group.id};
            result.graph.GetGroups().push_back(std::move(copy));
        }
    }
    auto* output = result.graph.AddOutputNode(state.outputCanvasPosition.value_or(Vec2{nextX,0}),true);
    output->instanceUuid = "bb276ad4-1b38-49b7-a9c5-34b977a28ec2";
    output->title = "Image output";
    output->notes = state.outputCanvasNotes;
    result.output = output->id;
    if (const auto input = RawMainOutputEndpoint(state)) connect(*input,result.output,"imageIn");
    if (!analysisOnly) {
        std::vector<int> starts;
        for (const auto& node:result.graph.GetNodes()) if(node.role==Role::CurrentImage) starts.push_back(node.id);
        for(int id:starts) {
            RawProjectGraph::HiddenStart hidden;
            hidden.owner=result.graph.FindNode(id)->rawLayerOwnerId;
            for(const auto& link:result.graph.GetLinks()) if(link.fromNodeId==id && link.fromSocketId=="imageOut")
                hidden.destinations.emplace_back(link.toNodeId,link.toSocketId);
            const auto* input=result.graph.FindInputLink(id,"imageIn");
            if(input) {
                const int from=input->fromNodeId;const auto socket=input->fromSocketId;
                std::unordered_map<std::string,std::string> outputs{{"imageOut",socket}};
                const auto declarations=result.graph.FindNode(id)->channelOutputs;
                for(const auto& output:declarations) {
                    std::string error;
                    auto port=ExposeChannelOutput(result.graph,from,socket,output.component,output.missingAlphaIsOpaque,&error);
                    if(port.empty())throw std::runtime_error("Cannot project a layer component output: "+error);
                    outputs.emplace(output.id,std::move(port));
                }
                for(auto& link:result.graph.EditLinks()) if(link.fromNodeId==id) {
                    const auto port=outputs.find(link.fromSocketId);
                    if(port==outputs.end())throw std::runtime_error("Unknown layer boundary output.");
                    link.fromNodeId=from;link.fromSocketId=port->second;
                }
            }
            for(auto& group:result.graph.GetGroups()) {
                auto& members=group.compoundMembers;
                if(std::find(members.begin(),members.end(),id)!=members.end())hidden.groups.push_back(group.id);
                members.erase(std::remove(members.begin(),members.end(),id),members.end());
            }
            // This is a view projection, not a user request to delete the
            // protected ownership boundary from its authored layer graph.
            auto& nodes=result.graph.EditNodes();
            nodes.erase(std::remove_if(nodes.begin(),nodes.end(),[&](const auto& node){return node.id==id;}),nodes.end());
            auto& links=result.graph.EditLinks();
            links.erase(std::remove_if(links.begin(),links.end(),[&](const auto& link){return link.fromNodeId==id || link.toNodeId==id;}),links.end());
            result.hiddenStarts.push_back(std::move(hidden));
        }
        result.hidesLayerStarts=true;
    }
    return result;
}

void RefreshRawProjectGraphNotes(const RawLayerStackState& state, const RawProjectGraph& projection, Graph& graph) {
    for (auto& node : graph.GetNodes()) {
        if (node.id == projection.preparedSource) node.notes = state.sourceCanvasNotes;
        else if (node.id == projection.output) node.notes = state.outputCanvasNotes;
        else if (const auto* layer = FindRawAdjustmentLayer(state, node.rawLayerOwnerId)) {
            for (const auto& owned : layer->graph.GetNodes())
                if (owned.instanceUuid == node.instanceUuid) { node.notes = owned.notes; break; }
        }
    }
}

bool RawLayerStackDocument::ApplyGraphLayout(const RawProjectGraph& projection, const Graph& edited,
    bool gesture, std::string& error, bool notesOnly) {
    struct NodeMove { Node* node; Vec2 position; std::string notes; };
    struct GroupMove { NodeGroup* group; Vec2 position, size; };
    std::vector<NodeMove> nodes;
    std::vector<GroupMove> groups;
    std::vector<std::pair<RawAdjustmentLayer*, Vec2>> origins;
    const auto finite=[](Vec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); };
    const auto equal=[](Vec2 a, Vec2 b) { return a.x==b.x && a.y==b.y; };
    const auto fail=[&] { error="Graph placement no longer matches its owning layer."; return false; };
    const auto* source=edited.FindNode(projection.preparedSource);
    const auto* output=edited.FindNode(projection.output);
    if(!source || !output || !finite(source->position) || !finite(output->position))return fail();
    const bool anchorsChanged=(!notesOnly && (!m_State.sourceCanvasPosition || !equal(*m_State.sourceCanvasPosition,source->position) ||
        !m_State.outputCanvasPosition || !equal(*m_State.outputCanvasPosition,output->position))) ||
        m_State.sourceCanvasNotes!=source->notes || m_State.outputCanvasNotes!=output->notes;
    // Prepare only placements and annotations of existing identities. No caller-supplied
    // operation, socket, link, compound definition or parameter enters the state.
    for (const auto& node : edited.GetNodes()) {
        if (node.id==projection.preparedSource || node.id==projection.output) continue;
        auto* layer=FindRawAdjustmentLayer(m_State,node.rawLayerOwnerId);
        const auto origin=projection.origins.find(node.rawLayerOwnerId);
        if (!layer || origin==projection.origins.end() || !finite(node.position)) return fail();
        auto& owned=layer->graph.GetNodes();
        const auto found=std::find_if(owned.begin(),owned.end(),[&](const auto& n) {return n.instanceUuid==node.instanceUuid;});
        if (found==owned.end()) return fail();
        const Vec2 position{node.position.x-origin->second.x,node.position.y-origin->second.y};
        if (!finite(position)) return fail();
        if ((!notesOnly && !equal(found->position,position)) || found->notes!=node.notes)
            nodes.push_back({&*found,notesOnly ? found->position : position,node.notes});
    }
    if (!notesOnly) for (const auto& group : edited.GetGroups()) {
        const auto binding=projection.groupOwners.find(group.id);
        if (binding==projection.groupOwners.end()) return fail();
        auto* layer=FindRawAdjustmentLayer(m_State,binding->second.first);
        const auto origin=projection.origins.find(binding->second.first);
        auto* owned=layer ? layer->graph.FindGroup(binding->second.second) : nullptr;
        if (!owned || origin==projection.origins.end() || !finite(group.position) || !finite(group.size) ||
            group.size.x<=0 || group.size.y<=0) return fail();
        const Vec2 position{group.position.x-origin->second.x,group.position.y-origin->second.y};
        if (!finite(position)) return fail();
        if (!equal(owned->position,position) || !equal(owned->size,group.size)) groups.push_back({owned,position,group.size});
    }
    if (!notesOnly) for (const auto& [owner,origin] : projection.origins) {
        auto* layer=FindRawAdjustmentLayer(m_State,owner);
        if (!layer || !finite(origin)) return fail();
        if (!layer->canvasOrigin || !equal(*layer->canvasOrigin,origin)) origins.emplace_back(layer,origin);
    }
    if (nodes.empty() && groups.empty() && origins.empty() && !anchorsChanged) {error.clear();return true;}
    std::string sourceNotes=source->notes, outputNotes=output->notes;
    if (gesture && !notesOnly) BeginGesture(); else EndGesture();
    if (m_Gesture) m_GestureChanged=true;
    else PushUndo(m_State,false,std::nullopt,notesOnly);
    m_Redo.clear();
    for (auto& move : nodes) {move.node->position=move.position;move.node->notes.swap(move.notes);}
    for (const auto& move : groups) {move.group->position=move.position;move.group->size=move.size;}
    for (const auto& [layer,origin] : origins) layer->canvasOrigin=origin;
    if (!notesOnly) {m_State.sourceCanvasPosition=source->position;m_State.outputCanvasPosition=output->position;}
    m_State.sourceCanvasNotes.swap(sourceNotes);m_State.outputCanvasNotes.swap(outputNotes);
    ++m_Revision;
    error.clear();return true;
}

static bool BuildRawProjectGraphCandidate(const RawLayerStackState& source, const RawProjectGraph& projection,
    const Graph& edited, const nlohmann::json& settings, const std::string& insertionOwner,
    RawLayerStackState& result, std::string& error, RawGraphGroupOwners* groupOwners = nullptr) {
    const auto fail = [&](const std::string& reason) { error = reason; return false; };
    if (!settings.is_array()) return fail("Invalid operation settings.");
    if (!MatchedLayerBoundaries(edited,error)) return false;
    if (!edited.FindNode(projection.preparedSource) || !edited.FindNode(projection.output))
        return fail("Keep the source preparation and image output nodes.");
    auto candidate = source;
    candidate.sourceCanvasPosition=edited.FindNode(projection.preparedSource)->position;
    candidate.outputCanvasPosition=edited.FindNode(projection.output)->position;
    candidate.sourceCanvasNotes=edited.FindNode(projection.preparedSource)->notes;
    candidate.sourceChannelOutputs=edited.FindNode(projection.preparedSource)->channelOutputs;
    candidate.outputCanvasNotes=edited.FindNode(projection.output)->notes;
    candidate.explicitRouting = true;
    candidate.imageOutput.reset();
    const auto* defaultOwner = FindRawAdjustmentLayer(source,insertionOwner);
    if (!defaultOwner) return fail("Choose a layer for the new operation.");
    std::unordered_map<int,GraphModel::Endpoint> identities;
    std::unordered_map<int,int> localIds;
    RawGraphGroupOwners nextGroupOwners;
    std::unordered_map<std::string,int> nextIds;
    std::unordered_map<std::string,std::unordered_map<std::string,const Node*>> originalNodes;
    for (const auto* layer : RawLayersInOrder(source)) {
        auto& nodes = originalNodes[layer->id];
        nodes.reserve(layer->graph.GetNodes().size());
        for (const auto& node : layer->graph.GetNodes()) nodes.emplace(node.instanceUuid,&node);
        auto* changed = FindRawAdjustmentLayer(candidate,layer->id);
        changed->graph.EditNodes().clear();
        changed->graph.EditLinks().clear();
        changed->graph.GetGroups().clear();
        changed->imageInput.reset();
        if(projection.hidesLayerStarts) {
            changed->imageInput=RawLayerInputEndpoint(source,layer->id);
            for(const auto& node:layer->graph.GetNodes()) if(node.role==Role::CurrentImage &&
                std::none_of(edited.GetNodes().begin(),edited.GetNodes().end(),[&](const auto& visible) {
                    return visible.instanceUuid==node.instanceUuid;
                })) changed->graph.EditNodes().push_back(node);
        }
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
        const auto& originals = originalNodes.at(owner);
        const auto previous = originals.find(node.instanceUuid);
        Node copy = node;
        if (previous == originals.end() && copy.kind == NodeKind::MaskGenerator && copy.role == Role::Ordinary &&
            (copy.maskKind == MaskGeneratorKind::LinearGradient || copy.maskKind == MaskGeneratorKind::RadialGradient || copy.maskKind == MaskGeneratorKind::Square) &&
            !edited.FindInputLink(node.id,"matchExtent")) {
            copy.kind = NodeKind::MaskTexture;
            copy.rawCoverage["canvas"] = "project";
            EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(copy);
        }
        const int id = previous != originals.end() ? previous->second->id : nextIds[owner]++;
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
        identities[node.id] = {owner,node.instanceUuid,"imageOut"};
        localIds[node.id] = id;
    }
    ScopedGraphOutputAnalysis connectionDescriptions(edited);
    for(const auto& hidden:projection.hiddenStarts) {
        std::optional<GraphModel::Endpoint> replacement;
        for(const auto& connection:edited.GetLinks()) {
            if(std::find(hidden.destinations.begin(),hidden.destinations.end(),
                std::pair{connection.toNodeId,connection.toSocketId})==hidden.destinations.end())continue;
            const auto identity=identities.find(connection.fromNodeId);
            if(identity==identities.end() || identity->second.graphId==hidden.owner)continue;
            auto endpoint=identity->second;endpoint.portId=connection.fromSocketId;
            const auto prior=RawLayerInputEndpoint(source,hidden.owner);
            if(prior && endpoint==*prior)continue;
            if(DescribeGraphOutput(edited,connection.fromNodeId,connection.fromSocketId).descriptor.logicalType!=NodeMath::LogicalValueType::ColorImage)continue;
            if(replacement && !(*replacement==endpoint))return fail("A layer's image ingress must have one source.");
            replacement=endpoint;
        }
        if(replacement)FindRawAdjustmentLayer(candidate,hidden.owner)->imageInput=*replacement;
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
            // The canvas draws the layer ingress directly from its source.
            // Restore that boundary without creating visible reference nodes.
            if(projection.hidesLayerStarts && layer->imageInput && endpoint==*layer->imageInput) {
                const auto* start=FindRawRole(*layer,Role::CurrentImage);
                if(!start)return fail("The layer has no internal image boundary.");
                layer->graph.EditLinks().push_back({start->id,"imageOut",localIds.at(to->id),connection.toSocketId});
                continue;
            }
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
        for(int& member:copy.compoundMembers) {
            const auto identity=identities.find(member);
            if(identity==identities.end() || identity->second.graphId!=owner || !localIds.count(member))
                return fail("A retained group must contain nodes from its owning RAW layer.");
            member=localIds.at(member);
        }
        for(const auto& hidden:projection.hiddenStarts)if(hidden.owner==owner &&
            std::find(hidden.groups.begin(),hidden.groups.end(),group.id)!=hidden.groups.end()) {
            if(const auto* start=FindRawRole(*layer,Role::CurrentImage))copy.compoundMembers.push_back(start->id);
        }
        if(groupOwners)nextGroupOwners.emplace(group.id,std::pair{owner,copy.id});
        layer->graph.SetNextGroupId(std::max(layer->graph.GetNextGroupId(),copy.id+1));
        if (layer->canvasOrigin) {
            copy.position.x -= layer->canvasOrigin->x;
            copy.position.y -= layer->canvasOrigin->y;
        }
        layer->graph.GetGroups().push_back(std::move(copy));
    }
    for (const auto* entry : RawLayersInOrder(candidate)) {
        auto* layer = FindRawAdjustmentLayer(candidate,entry->id);
        GraphModel::ReindexLayerParameterRecords(layer->graph);
        layer->graph.SetNextNodeId(nextIds.at(layer->id));
        layer->graph.GetCompoundDefinitions() = edited.GetCompoundDefinitions();
        RegisterRawMaskOutputs(candidate,layer->id);
    }
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
    if(groupOwners)*groupOwners=std::move(nextGroupOwners);
    error.clear();
    return true;
}

bool ApplyRawProjectGraph(const RawLayerStackState& source, const RawProjectGraph& projection,
    const EditorNodeGraph::Graph& edited, const nlohmann::json& settings,
    const std::string& insertionOwner, RawLayerStackState& result, std::string& error) {
    RawLayerStackState candidate;
    if (!BuildRawProjectGraphCandidate(source,projection,edited,settings,insertionOwner,candidate,error) ||
        !ValidateRawLayerStack(candidate,error)) return false;
    result = std::move(candidate);
    return true;
}

std::optional<RawLayerStackDocument::PreparedEdit> RawLayerStackDocument::PrepareGraphEdit(
    const RawProjectGraph& projection, const EditorNodeGraph::Graph& edited,
    const nlohmann::json& settings, const std::string& insertionOwner, std::string& error,
    const EditorNodeGraph::Graph* composition, int sourceNodeId, std::uint64_t dependencyRevision, RawGraphGroupOwners* groupOwners) const {
    RawLayerStackState candidate;
    RawGraphGroupOwners preparedGroups;
    if (!BuildRawProjectGraphCandidate(m_State,projection,edited,settings,insertionOwner,candidate,error,groupOwners?&preparedGroups:nullptr))
        return std::nullopt;
    auto prepared=Prepare(std::move(candidate),error,composition,sourceNodeId,dependencyRevision);
    if(prepared && groupOwners)*groupOwners=std::move(preparedGroups);
    return prepared;
}

} // namespace Stack::Project
