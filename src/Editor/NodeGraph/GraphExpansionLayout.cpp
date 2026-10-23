#include "GraphExpansionLayout.h"
#include "Layout/GraphLayoutPlacement.h"
#include "GraphRoomLayout.h"
#include "Layout/GraphFlowLayout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Stack::GraphLayout {
namespace {
using namespace EditorNodeGraph;
using namespace Placement;
} // namespace

bool ArrangeExpansion(Graph& graph,const std::vector<int>& exposed,Vec2 origin,const std::string& instanceUuid,
    const std::vector<int>& containingGroups,const MeasureNode& measure,std::string& error,const MeasureSocket& measureSocket) {
    try {
        if(exposed.empty())throw std::runtime_error("Decompile produced no editable nodes.");
        std::unordered_set<int> opened(exposed.begin(),exposed.end());
        std::unordered_map<int,GraphRect> local;
        for(const auto& n:graph.GetNodes()) {
            // Headless commands can supply their own geometry. The canvas
            // always supplies the canonical native logical measurement.
            auto r=measure?measure(graph,n):GraphRect{{-12,0},{292,n.expanded?240.f:52.f}};
            if(!Valid(r))throw std::runtime_error("A node has invalid layout geometry.");
            local.emplace(n.id,r);
        }
        auto bounds=[&](int id){return Offset(local.at(id),graph.FindNode(id)->position);};
        auto& groups=graph.GetGroups();
        std::unordered_map<int,int> blockParent;
        for(int id:exposed)blockParent[id]=id;
        const auto blockRoot=[&](int id){int root=id;while(blockParent.at(root)!=root)root=blockParent.at(root);
            while(blockParent.at(id)!=id){int next=blockParent.at(id);blockParent[id]=root;id=next;}return root;};
        std::vector<int> internalFrames;
        for(std::size_t i=0;i<groups.size();++i) {
            const auto& g=groups[i];
            if(!g.compoundMembers.empty() && g.compoundBoundary.is_object() &&
                g.compoundBoundary.value("instanceUuid",std::string{})!=instanceUuid &&
                std::find(containingGroups.begin(),containingGroups.end(),g.id)==containingGroups.end() &&
                std::all_of(g.compoundMembers.begin(),g.compoundMembers.end(),[&](int id){return opened.count(id)!=0;}))
                internalFrames.push_back(int(i));
        }
        std::stable_sort(internalFrames.begin(),internalFrames.end(),[&](int a,int b){return groups[a].compoundMembers.size()<groups[b].compoundMembers.size();});
        std::vector<int> fittedInternal;
        for(int index:internalFrames) {
            auto& g=groups[index];GraphRect r=bounds(g.compoundMembers.front());
            for(int id:g.compoundMembers){r=Union(r,bounds(id));blockParent[blockRoot(id)]=blockRoot(g.compoundMembers.front());}
            for(int child:fittedInternal)if(std::all_of(groups[child].compoundMembers.begin(),groups[child].compoundMembers.end(),
                [&](int id){return std::find(g.compoundMembers.begin(),g.compoundMembers.end(),id)!=g.compoundMembers.end();}))r=Union(r,GroupRect(groups[child]));
            r={{r.min.x-220.f,r.min.y-52.f},{r.max.x+32.f,r.max.y+32.f}};g.position=r.min;g.size={r.max.x-r.min.x,r.max.y-r.min.y};fittedInternal.push_back(index);
        }
        std::unordered_map<int,Item> blocks;
        std::vector<int> blockIds;
        for(int id:exposed) {
            const int root=blockRoot(id);auto [it,inserted]=blocks.emplace(root,Item{bounds(id),{},{},false});
            if(inserted)blockIds.push_back(root);
            it->second.nodes.push_back(id);it->second.rect=Union(it->second.rect,bounds(id));
        }
        for(int index:internalFrames){auto& b=blocks.at(blockRoot(groups[index].compoundMembers.front()));
            b.groups.push_back(index);b.rect=Union(b.rect,GroupRect(groups[index]));}
        std::unordered_map<int,std::vector<int>> outputs;
        std::unordered_map<int,int> indegree,rank;
        for(int id:blockIds)indegree[id]=0;
        for(const auto& link:graph.GetLinks())if(opened.count(link.fromNodeId) && opened.count(link.toNodeId)) {
            const int from=blockRoot(link.fromNodeId),to=blockRoot(link.toNodeId);
            if(from!=to){outputs[from].push_back(to);++indegree[to];}
        }
        std::priority_queue<int,std::vector<int>,std::greater<int>> ready;
        for(int id:blockIds)if(!indegree[id])ready.push(id);
        std::size_t visited=0;
        while(!ready.empty()) {const int id=ready.top();ready.pop();++visited;
            for(int next:outputs[id]) {rank[next]=std::max(rank[next],rank[id]+1);if(!--indegree[next])ready.push(next);}
        }
        if(visited!=blockIds.size())throw std::runtime_error("Cannot arrange these interleaved retained groups without changing their membership.");
        int maxRank=0;for(int id:blockIds)maxRank=std::max(maxRank,rank[id]);
        std::vector<std::vector<int>> columns(maxRank+1);
        for(int id:blockIds)columns[rank[id]].push_back(id);
        std::unordered_map<int,FlowSize> sizes;
        for(int id:blockIds){const auto r=blocks.at(id).rect;sizes.emplace(id,FlowSize{r.max.x-r.min.x,r.max.y-r.min.y});}
        const auto portY=[&](int nodeId,const std::string& socket,SocketDirection direction) {
            const auto* node=graph.FindNode(nodeId);const auto r=blocks.at(blockRoot(nodeId)).rect;
            if(measureSocket)if(const auto p=measureSocket(graph,*node,socket,direction))return node->position.y+p->y-r.min.y;
            return (r.max.y-r.min.y)*.5f;
        };
        std::vector<FlowEdge> traffic;
        for(const auto& link:graph.GetLinks())if(opened.count(link.fromNodeId) && opened.count(link.toNodeId)) {
            const int from=blockRoot(link.fromNodeId),to=blockRoot(link.toNodeId);
            if(from!=to)traffic.push_back({from,to,portY(link.fromNodeId,link.fromSocketId,SocketDirection::Output),
                portY(link.toNodeId,link.toSocketId,SocketDirection::Input)});
        }
        const auto positions=ArrangeFlow(columns,sizes,traffic,origin);
        for(const auto& column:columns) {
            for(int id:column) {auto& block=blocks.at(id);auto r=block.rect;const auto p=positions.at(id);const Vec2 delta{p.x-r.min.x,p.y-r.min.y};
                for(int member:block.nodes){auto& p=graph.FindNode(member)->position;p.x+=delta.x;p.y+=delta.y;}
                for(int index:block.groups){auto& p=groups[index].position;p.x+=delta.x;p.y+=delta.y;}
            }
        }
        // Capture rigid sibling groups before fitting the newly expanded and
        // containing frames. Ordinary groups retain their geometric membership.
        int boundary=-1;
        std::unordered_map<int,std::vector<int>> members;
        std::vector<int> ancestors;
        for(std::size_t i=0;i<groups.size();++i) {
            auto& g=groups[i];auto& ids=members[g.id];ids=g.compoundMembers;
            if(!g.compoundBoundary.is_object())for(const auto& n:graph.GetNodes())
                if(!opened.count(n.id) && Contains(GroupRect(g),n.position))ids.push_back(n.id);
            const bool root=g.compoundBoundary.is_object() && g.compoundBoundary.value("instanceUuid",std::string{})==instanceUuid;
            if(root)boundary=static_cast<int>(i);
            else if(std::find(containingGroups.begin(),containingGroups.end(),g.id)!=containingGroups.end()) {
                ancestors.push_back(static_cast<int>(i));ids.insert(ids.end(),exposed.begin(),exposed.end());
            }
        }
        if(boundary<0)throw std::runtime_error("Decompile lost its retained boundary.");
        std::vector<int> fittedFrames;
        for(std::size_t i=0;i<groups.size();++i)if(int(i)!=boundary &&
            std::find(ancestors.begin(),ancestors.end(),int(i))==ancestors.end())fittedFrames.push_back(int(i));
        auto fit=[&](int index,bool retain) {
            auto& g=groups[index];GraphRect r{};bool first=true;
            for(int id:members[g.id])if(graph.FindNode(id)){r=first?bounds(id):Union(r,bounds(id));first=false;}
            if(first)throw std::runtime_error("Decompile boundary has no members.");
            for(int child:fittedFrames)if(!members[groups[child].id].empty() &&
                std::all_of(members[groups[child].id].begin(),members[groups[child].id].end(),
                    [&](int id){return std::find(members[g.id].begin(),members[g.id].end(),id)!=members[g.id].end();}))r=Union(r,GroupRect(groups[child]));
            r=g.compoundBoundary.is_object()?GraphRect{{r.min.x-220.f,r.min.y-52.f},{r.max.x+32.f,r.max.y+32.f}}:Frame(r);if(retain)r=Union(r,GroupRect(g));
            if(!InRange(r))throw std::runtime_error("Decompile exceeds the current graph coordinate limits.");
            g.position=r.min;g.size={r.max.x-r.min.x,r.max.y-r.min.y};
            fittedFrames.push_back(index);
        };
        // Internal retained frames have already travelled with their layout
        // blocks. Their bounds contribute to all containing frames as well.
        fit(boundary,false);
        RoomGesture room;
        const auto cachedMeasure=[&](const Graph&,const Node& node){return local.at(node.id);};
        PlacementEdit placement;
        if(!room.Begin(graph,exposed,cachedMeasure,error,{},{},true) || !room.Move({},placement,error) || !ApplyPlacement(graph,placement,error))return false;
        return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
} // namespace Stack::GraphLayout
