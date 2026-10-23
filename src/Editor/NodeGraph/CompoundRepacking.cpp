#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <algorithm>
#include <unordered_set>

namespace EditorNodeGraph {
bool Graph::RepackCompoundGroup(int groupId, int* compoundNodeId, std::string* error) {
    const auto found=std::find_if(m_Groups.begin(),m_Groups.end(),[&](const auto& group){return group.id==groupId;});
    if(found==m_Groups.end() || !found->compoundBoundary.is_object()) {
        if(error)*error="This group has no retained compound boundary.";return false;
    }
    const NodeGroup boundary=*found;
    Graph candidate=CloneForAnalysis();
    int packed=-1;
    if(!candidate.CreateCompoundFromSelection(boundary.compoundMembers,boundary.title,&packed,error,&boundary))return false;
    auto* compound=candidate.FindNode(packed);
    if(!compound)return false;
    compound->notes=boundary.compoundBoundary.value("notes",std::string{});
    const std::string uuid=compound->instanceUuid;
    const std::unordered_set<int> members(boundary.compoundMembers.begin(),boundary.compoundMembers.end());
    // Descendant boundaries were saved inside the new canonical graph. They
    // must leave this canvas together with their member nodes.
    candidate.m_Groups.erase(std::remove_if(candidate.m_Groups.begin(), candidate.m_Groups.end(), [&](const auto& group) {
        return group.id == groupId || (group.compoundBoundary.is_object() && !group.compoundMembers.empty() &&
            std::all_of(group.compoundMembers.begin(), group.compoundMembers.end(), [&](int id) { return members.count(id) != 0; }));
    }), candidate.m_Groups.end());
    for(auto& parent:candidate.m_Groups) {
        if(!parent.compoundBoundary.is_object() || parent.id==groupId)continue;
        bool contained=false;
        for(int id:parent.compoundMembers)contained|=members.count(id)!=0;
        if(!contained)continue;
        parent.compoundMembers.erase(std::remove_if(parent.compoundMembers.begin(),parent.compoundMembers.end(),[&](int id){return members.count(id)!=0;}),parent.compoundMembers.end());
        parent.compoundMembers.push_back(packed);
        for(auto& port:parent.compoundBoundary["ports"])
            for(const auto& child:boundary.compoundBoundary["ports"])
                if(port["internalInstanceUuid"]==child["internalInstanceUuid"] && port["internalSocketId"]==child["internalSocketId"] && port["direction"]==child["direction"]) {
                    port["internalInstanceUuid"]=uuid;port["internalSocketId"]=child["id"];break;
                }
        for(auto& parameter:parent.compoundBoundary["parameters"])
            for(const auto& child:boundary.compoundBoundary["parameters"])
                if(parameter["internalInstanceUuid"]==child["internalInstanceUuid"] && parameter["internalParameterId"]==child["internalParameterId"]) {
                    parameter["internalInstanceUuid"]=uuid;parameter["internalParameterId"]="compound:"+child["id"].get<std::string>();break;
                }
    }
    const auto validation=candidate.Validate();
    if(!validation.valid){if(error)*error=validation.messages.empty()?"Repacked graph is invalid.":validation.messages.front();return false;}
    *this=std::move(candidate);
    if(compoundNodeId)*compoundNodeId=packed;
    return true;
}
}
