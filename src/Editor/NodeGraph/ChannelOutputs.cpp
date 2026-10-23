#include "ChannelOutputs.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {
bool Component(const std::string& id) { return id == "r" || id == "g" || id == "b" || id == "a"; }
bool ImageOutput(const SocketDefinition& socket) {
    return socket.direction == SocketDirection::Output &&
        socket.logicalType == Stack::NodeMath::LogicalValueType::ColorImage;
}
}
const ChannelOutput* FindChannelOutput(const Node& node, const std::string& socket) {
    for (const auto& output : node.channelOutputs) if (output.id == socket) return &output;
    return nullptr;
}
std::string ChannelOutputId(const std::string& source, const std::string& component, bool opaque) {
    // Reversible encoding keeps native socket case/punctuation distinct while
    // also satisfying compound public-port token rules.
    constexpr char hex[]="0123456789abcdef";
    std::string id="component-";id.reserve(12+source.size()*2+component.size());
    for(unsigned char byte:source) {id+=hex[byte>>4];id+=hex[byte&15];}
    return id+"-"+component+(opaque && component=="a"?"-opaque":"");
}
SocketDefinition ChannelOutputSocket(int nodeId, const ChannelOutput& output) {
    SocketDefinition socket{output.id,nodeId,SocketDirection::Output,SocketType::Channel,
        output.component.empty()?"?":std::string(1,static_cast<char>(std::toupper(output.component.front()))),false,true};
    socket.logicalType = Stack::NodeMath::LogicalValueType::Channel;
    socket.declaredChannels = Stack::NodeMath::SemanticField<Stack::NodeMath::ChannelDescriptor>::Known(
        {Stack::NodeMath::ChannelLayout::Gray,{socket.label}});
    return socket;
}
bool ValidateChannelOutputs(const Node& node, const std::vector<SocketDefinition>& sockets, std::string& error) {
    if (node.channelOutputs.size() > 256) { error="Too many component outputs on one node."; return false; }
    std::unordered_set<std::string> ids;
    for (const auto& output : node.channelOutputs) {
        if (!Component(output.component) || output.id != ChannelOutputId(output.sourceSocket,output.component,output.missingAlphaIsOpaque) ||
            (output.missingAlphaIsOpaque && output.component != "a") || !ids.insert(output.id).second ||
            FindChannelOutput(node,output.sourceSocket)) { error="Invalid component output declaration."; return false; }
        const auto base=std::find_if(sockets.begin(),sockets.end(),[&](const auto& socket) { return socket.id==output.sourceSocket; });
        if (base==sockets.end() || !ImageOutput(*base)) { error="A component output requires a native image output."; return false; }
        if (std::count_if(sockets.begin(),sockets.end(),[&](const auto& socket){return socket.id==output.id;})!=1) {
            error="A component output collides with a native socket."; return false;
        }
    }
    return true;
}
std::string ExposeChannelOutput(Graph& graph, int nodeId, const std::string& source, const std::string& component,
    bool opaque, std::string* error) {
    const auto fail=[&](const char* message) {if(error)*error=message;return std::string{};};
    auto* node=graph.FindNode(nodeId);
    SocketDefinition base;
    if (!node || !Component(component) || FindChannelOutput(*node,source) ||
        !graph.FindSocket(nodeId,source,&base) || !ImageOutput(base)) return fail("Choose an image output and an R, G, B or alpha component.");
    opaque &= component=="a";
    const auto id=ChannelOutputId(source,component,opaque);
    if (FindChannelOutput(*node,id)) {if(error)error->clear();return id;}
    if(graph.FindSocket(nodeId,id,nullptr))return fail("A native output already uses this component port identity.");
    if (node->channelOutputs.size()>=256) return fail("Too many component outputs on this node.");
    // Availability is checked at connection/render time as well. Declared
    // image outputs may be incomplete while the user constructs a graph.
    node->channelOutputs.push_back({id,source,component,opaque,{}});
    graph.EditNodes();
    if(error)error->clear();
    return id;
}
bool NeedsOutputLowering(const Graph& graph) {
    for (const auto& node:graph.GetNodes()) if (node.kind==NodeKind::Compound || !node.channelOutputs.empty()) return true;
    return false;
}
nlohmann::json SerializeChannelOutputs(const std::vector<ChannelOutput>& outputs) {
    auto value=nlohmann::json::array();
    for (const auto& output:outputs) value.push_back({{"id",output.id},{"source",output.sourceSocket},
        {"component",output.component},{"opaqueAlpha",output.missingAlphaIsOpaque},{"notes",output.notes}});
    return value;
}
std::vector<ChannelOutput> DeserializeChannelOutputs(const nlohmann::json& value) {
    if (!value.is_array() || value.size()>256) throw std::runtime_error("Invalid component output declarations.");
    std::vector<ChannelOutput> outputs;
    for (const auto& item:value) outputs.push_back({item.at("id").get<std::string>(),item.at("source").get<std::string>(),
        item.at("component").get<std::string>(),item.value("opaqueAlpha",false),item.value("notes",std::string{})});
    return outputs;
}
std::vector<ChannelSplitFold> FoldChannelSplits(Graph& graph, const std::vector<int>& candidates) {
    std::vector<int> splits=candidates;
    if (splits.empty()) for (const auto& node:graph.GetNodes()) if(node.kind==NodeKind::ChannelSplit)splits.push_back(node.id);
    std::vector<ChannelSplitFold> folded;
    for (int id:splits) {
        const auto* split=graph.FindNode(id);
        if (!split || split->kind!=NodeKind::ChannelSplit || split->role!=Stack::GraphModel::NodeRole::Ordinary) continue;
        const auto* input=graph.FindInputLink(id,kImageInputSocketId);
        if (!input) continue;
        const int producer=input->fromNodeId;
        const std::string sourceSocket=input->fromSocketId, splitUuid=split->instanceUuid, notes=split->notes;
        const bool opaque=split->splitMissingAlphaIsOpaque;
        const auto* source=graph.FindNode(producer);
        if (!source || (source->rawLayerOwnerId!=split->rawLayerOwnerId &&
            !(source->rawLayerOwnerId.empty() && source->role==Stack::GraphModel::NodeRole::OriginalImage))) continue;
        bool safe=true;
        for (const auto& group:graph.GetGroups()) {
            if (!group.compoundBoundary.is_object() ||
                std::find(group.compoundMembers.begin(),group.compoundMembers.end(),id)==group.compoundMembers.end()) continue;
            if (std::find(group.compoundMembers.begin(),group.compoundMembers.end(),producer)==group.compoundMembers.end()) {safe=false;break;}
            for(const auto& port:group.compoundBoundary.at("ports"))
                if(port.value("internalInstanceUuid",std::string{})==splitUuid && port.value("direction",std::string{})=="input")safe=false;
        }
        std::vector<Link> outgoing;
        graph.ForEachOutgoingLink(id,[&](const auto& link){outgoing.push_back(link);safe &= Component(link.fromSocketId);});
        if (!safe || outgoing.empty()) continue;
        std::unordered_map<std::string,std::string> aliases;
        for (const auto& link:outgoing) {
            if (aliases.count(link.fromSocketId)) continue;
            std::string error;
            auto alias=ExposeChannelOutput(graph,producer,sourceSocket,link.fromSocketId,opaque,&error);
            if(alias.empty())throw std::runtime_error(error);
            auto* owner=graph.FindNode(producer);
            for(auto& output:owner->channelOutputs)if(output.id==alias && output.notes.empty())output.notes=notes;
            aliases.emplace(link.fromSocketId,std::move(alias));
        }
        for(auto& link:graph.EditLinks())if(link.fromNodeId==id) {link.fromNodeId=producer;link.fromSocketId=aliases.at(link.fromSocketId);}
        const auto producerUuid=graph.FindNode(producer)->instanceUuid;
        for(auto& group:graph.GetGroups()) {
            group.compoundMembers.erase(std::remove(group.compoundMembers.begin(),group.compoundMembers.end(),id),group.compoundMembers.end());
            if(group.compoundBoundary.is_object())for(auto& port:group.compoundBoundary["ports"])
                if(port.value("internalInstanceUuid",std::string{})==splitUuid && aliases.count(port.value("internalSocketId",std::string{}))) {
                    port["internalInstanceUuid"]=producerUuid;port["internalSocketId"]=aliases.at(port["internalSocketId"].get<std::string>());
                }
        }
        if(!graph.RemoveNode(id))throw std::runtime_error("The redundant channel split could not be removed.");
        folded.push_back({id,producer});
    }
    return folded;
}
}
