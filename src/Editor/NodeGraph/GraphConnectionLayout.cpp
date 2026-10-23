#include "GraphRoomLayout.h"
#include "Layout/GraphLayoutPlacement.h"

namespace Stack::GraphLayout {
bool ArrangeConnection(EditorNodeGraph::Graph& graph,int from,int to,const MeasureNode& measure,std::string& error) {
    using namespace EditorNodeGraph;
    using namespace Placement;
    std::unordered_map<int,std::vector<int>> inputs,outputs;
    for(const auto& link:graph.GetLinks()) {
        auto& in=inputs[link.toNodeId];auto& out=outputs[link.fromNodeId];
        if(std::find(in.begin(),in.end(),link.fromNodeId)==in.end())in.push_back(link.fromNodeId);
        if(std::find(out.begin(),out.end(),link.toNodeId)==out.end())out.push_back(link.toNodeId);
    }
    // Include junctions as fixed endpoints. Their other branches keep their
    // authored arrangement while the newly connected chain can align to them.
    std::vector<int> chain{from,to};std::unordered_set<int> visited{from,to};
    while(chain.size()<256 && inputs[chain.front()].size()==1 && outputs[chain.front()].size()==1) {
        const int id=inputs[chain.front()].front();
        if(!visited.insert(id).second)break;
        chain.insert(chain.begin(),id);
    }
    while(chain.size()<256 && outputs[chain.back()].size()==1 && inputs[chain.back()].size()==1) {
        const int id=outputs[chain.back()].front();
        if(!visited.insert(id).second)break;
        chain.push_back(id);
    }
    std::unordered_map<int,GraphRect> footprints;
    std::vector<float> distances{0};
    for(int id:chain) {
        const auto* node=graph.FindNode(id);if(!node){error="The connected chain changed.";return false;}
        const auto rect=measure?measure(graph,*node):GraphRect{{-12,0},{292,node->expanded?240.f:52.f}};
        if(!Valid(rect)){error="A connected node has invalid layout geometry.";return false;}
        footprints.emplace(id,rect);
        if(footprints.size()>1) {
            const auto prior=footprints.at(chain[footprints.size()-2]);
            distances.push_back(distances.back()+prior.max.x-rect.min.x+kColumnGap);
        }
    }
    const auto first=graph.FindNode(chain.front())->position,last=graph.FindNode(chain.back())->position;
    const auto junction=[&](int id){return inputs[id].size()>1 || outputs[id].size()>1;};
    const bool pinFirst=junction(chain.front()),pinLast=junction(chain.back());
    // Insufficient space between two fixed junctions leaves the wire valid and
    // the existing arrangement intact; neither branch is silently dragged away.
    if(pinFirst && pinLast && last.x-first.x<distances.back())return true;
    const float span=std::max(distances.back(),last.x-first.x),left=pinLast?last.x-span:first.x;
    const float row=pinFirst?first.y:pinLast?last.y:(first.y+last.y)*.5f;
    std::unordered_map<int,Vec2> anchors;
    for(std::size_t i=0;i<chain.size();++i) {
        const float t=distances[i]/distances.back();
        anchors.emplace(chain[i],Vec2{left+t*span,pinFirst && pinLast?first.y+(last.y-first.y)*t:row});
    }
    RoomGesture room;PlacementEdit edit;
    const auto cached=[&](const Graph& g,const Node& n) {
        const auto found=footprints.find(n.id);
        return found!=footprints.end()?found->second:measure?measure(g,n):GraphRect{{-12,0},{292,n.expanded?240.f:52.f}};
    };
    return room.Begin(graph,chain,cached,error,anchors,{},true) && room.Move({},edit,error) && ApplyPlacement(graph,edit,error);
}
} // namespace Stack::GraphLayout
