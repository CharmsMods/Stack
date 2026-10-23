#pragma once
#include "EditorNodeGraph.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_map>

namespace EditorNodeGraph {
// Pure frame preparation shared by authored placement and cached presentation.
inline std::unordered_map<int,GraphRect> MeasureCompoundGroupFrames(const Graph& graph,
    const std::function<GraphRect(const Graph&,const Node&)>& measure) {
    std::vector<const NodeGroup*> groups;
    for(const auto& group:graph.GetGroups())if(group.compoundBoundary.is_object())groups.push_back(&group);
    std::sort(groups.begin(),groups.end(),[](const auto* a,const auto* b) {
        return a->compoundMembers.size()!=b->compoundMembers.size() ? a->compoundMembers.size()<b->compoundMembers.size() : a->id>b->id;
    });
    std::unordered_map<int,GraphRect> result;
    const auto unite=[](GraphRect a,GraphRect b) {return GraphRect{{std::min(a.min.x,b.min.x),std::min(a.min.y,b.min.y)},
        {std::max(a.max.x,b.max.x),std::max(a.max.y,b.max.y)}};};
    for(const auto* group:groups) {
        GraphRect bounds{};bool first=true;
        for(int id:group->compoundMembers)if(const auto* node=graph.FindNode(id)) {
            auto r=measure(graph,*node);r.min.x+=node->position.x;r.min.y+=node->position.y;r.max.x+=node->position.x;r.max.y+=node->position.y;
            bounds=first?r:unite(bounds,r);first=false;
        }
        if(first)continue;
        for(const auto* child:groups) {
            const auto fitted=result.find(child->id);if(fitted==result.end() || child->compoundMembers.empty())continue;
            if(std::all_of(child->compoundMembers.begin(),child->compoundMembers.end(),[&](int id) {
                return std::find(group->compoundMembers.begin(),group->compoundMembers.end(),id)!=group->compoundMembers.end();
            }))bounds=unite(bounds,fitted->second);
        }
        result.emplace(group->id,GraphRect{{bounds.min.x-220.f,bounds.min.y-52.f},{bounds.max.x+32.f,bounds.max.y+32.f}});
    }
    return result;
}
// Membership changes on explicit edits; empty moved lists only refit active placement geometry.
inline bool UpdateCompoundGroupMembership(Graph& graph, const std::vector<int>& moved,
    const std::unordered_map<int, GraphRect>& initialFrames,
    const std::function<GraphRect(const Graph&, const Node&)>& measure, bool refitBounds = true) {
    const auto worldBounds=[&](const Node& node) {
        auto r=measure(graph,node);
        r.min.x+=node.position.x; r.min.y+=node.position.y;
        r.max.x+=node.position.x; r.max.y+=node.position.y; return r;
    };
    bool changed = false;
    // Decide each admission before changing any memberships, so group storage
    // order cannot select an outer container instead of its nested child.
    for(int id:moved) {
        const auto* node=graph.FindNode(id);if(!node)continue;
        const auto bounds=worldBounds(*node);
        const Vec2 center{(bounds.min.x+bounds.max.x)*.5f,(bounds.min.y+bounds.max.y)*.5f};
        const auto contains=[&](const GraphRect& r) { return center.x>=r.min.x && center.x<=r.max.x && center.y>=r.min.y && center.y<=r.max.y; };
        const bool connectedRight=std::any_of(graph.GetLinks().begin(),graph.GetLinks().end(),[id](const auto& link){return link.fromNodeId==id;});
        bool stillOwned=false;
        for(auto& group:graph.GetGroups())if(group.compoundBoundary.is_object()) {
            auto member=std::find(group.compoundMembers.begin(),group.compoundMembers.end(),id);
            if(member==group.compoundMembers.end())continue;
            const auto frame=initialFrames.find(group.id);
            if(frame!=initialFrames.end() && !contains(frame->second) && !connectedRight) {
                group.compoundMembers.erase(member);changed=true;
            } else stillOwned=true;
        }
        if(stillOwned || node->role!=Stack::GraphModel::NodeRole::Ordinary ||
            node->kind==NodeKind::Output || node->kind==NodeKind::Image || node->kind==NodeKind::RawSource)continue;
        int owner=-1;float area=std::numeric_limits<float>::max();
        for(const auto& group:graph.GetGroups())if(group.compoundBoundary.is_object()) {
            const auto frame=initialFrames.find(group.id);if(frame==initialFrames.end() || !contains(frame->second))continue;
            const auto& r=frame->second;const float candidate=(r.max.x-r.min.x)*(r.max.y-r.min.y);
            if(candidate<area){area=candidate;owner=group.id;}
        }
        if(owner<0)continue;
        const auto ownerMembers=graph.FindGroup(owner)->compoundMembers;
        for(auto& group:graph.GetGroups())if(group.compoundBoundary.is_object()) {
            // Ancestors are defined by retained ownership, not coincidental
            // overlapping rectangles. Preserve the complete nested membership.
            const bool ancestor=!ownerMembers.empty() && std::all_of(ownerMembers.begin(),ownerMembers.end(),[&](int child) {
                return std::find(group.compoundMembers.begin(),group.compoundMembers.end(),child)!=group.compoundMembers.end(); });
            if(group.id==owner || ancestor){group.compoundMembers.push_back(id);changed=true;}
        }
    }
    if(refitBounds) {
        const auto frames=MeasureCompoundGroupFrames(graph,measure);
        for(auto& group:graph.GetGroups())if(const auto found=frames.find(group.id);found!=frames.end()) {
            group.position=found->second.min;
            group.size={found->second.max.x-found->second.min.x,found->second.max.y-found->second.min.y};
        }
    }
    if(changed)graph.EditNodes(); // Publish one structural revision for ownership/entry caches.
    return changed;
}
}
