#include "GraphRoomLayout.h"
#include "Layout/GraphLayoutPlacement.h"

#include <numeric>

namespace Stack::GraphLayout {
namespace {
using namespace EditorNodeGraph;
using namespace Placement;
bool Equal(Vec2 a,Vec2 b) {return a.x==b.x && a.y==b.y;}
bool Equal(GraphRect a,GraphRect b) {return Equal(a.min,b.min) && Equal(a.max,b.max);}
}

struct RoomGesture::State {
    struct Body { int id; std::string uuid; Vec2 baseline,origin,position,rest; GraphRect local; bool held=false; };
    struct Group { int id; GraphRect baseline,origin,rect,rest; std::vector<std::size_t> members,children;
        bool ancestor=false,held=false; };
    std::vector<Body> bodies;
    std::vector<Group> groups;
    std::vector<Item> units;
    std::vector<std::size_t> ancestors;
    std::vector<std::size_t> held;
    Vec2 offset;
    bool initialized=false;
    bool preserveFlow=false;
};

RoomGesture::RoomGesture()=default;
RoomGesture::~RoomGesture()=default;
RoomGesture::RoomGesture(RoomGesture&&) noexcept=default;
RoomGesture& RoomGesture::operator=(RoomGesture&&) noexcept=default;

bool RoomGesture::Begin(const Graph& graph,const std::vector<int>& held,const MeasureNode& measure,std::string& error,
    const std::unordered_map<int,Vec2>& heldStarts,const std::unordered_map<int,GraphRect>& heldGroupStarts,bool preserveFlow,
    const std::unordered_map<int,Vec2>& restingPositions,const std::unordered_map<int,GraphRect>& restingGroups) {
    try {
        auto state=std::make_unique<State>();
        state->preserveFlow=preserveFlow;
        std::unordered_set<int> heldIds(held.begin(),held.end());
        std::unordered_map<int,std::size_t> indices;
        for(const auto& node:graph.GetNodes()) {
            const auto local=measure?measure(graph,node):GraphRect{{-12,0},{292,node.expanded?240.f:52.f}};
            if(!Valid(local))throw std::runtime_error("A node has invalid layout geometry.");
            const auto index=state->bodies.size();indices.emplace(node.id,index);
            const auto anchor=heldStarts.find(node.id);
            state->bodies.push_back({node.id,node.instanceUuid,node.position,
                heldIds.count(node.id) && anchor!=heldStarts.end()?anchor->second:node.position,node.position,
                restingPositions.count(node.id)?restingPositions.at(node.id):node.position,local,heldIds.count(node.id)!=0});
            if(heldIds.count(node.id))state->held.push_back(index);
        }
        if(state->held.empty() || state->held.size()!=heldIds.size())throw std::runtime_error("The held selection no longer exists.");
        for(const auto& group:graph.GetGroups()) {
            State::Group item{group.id,GroupRect(group),GroupRect(group),GroupRect(group),
                restingGroups.count(group.id)?restingGroups.at(group.id):GroupRect(group)};
            if(!Valid(item.rect))throw std::runtime_error("A group has invalid layout geometry.");
            if(group.compoundBoundary.is_object()) {
                for(int id:group.compoundMembers)if(indices.count(id))item.members.push_back(indices.at(id));
            } else for(std::size_t i=0;i<state->bodies.size();++i)
                if(Contains(item.rect,state->bodies[i].baseline))item.members.push_back(i);
            std::sort(item.members.begin(),item.members.end());
            item.members.erase(std::unique(item.members.begin(),item.members.end()),item.members.end());
            const auto count=std::count_if(item.members.begin(),item.members.end(),[&](auto i){return state->bodies[i].held;});
            item.ancestor=count>0;item.held=count>0 && std::size_t(count)==item.members.size();
            if(item.held && heldGroupStarts.count(group.id))item.origin=heldGroupStarts.at(group.id);
            state->groups.push_back(std::move(item));
        }
        for(std::size_t i=0;i<state->groups.size();++i) {
            auto& group=state->groups[i];
            if(group.ancestor && !group.held)state->ancestors.push_back(i);
            if(!group.ancestor)continue;
            for(std::size_t j=0;j<state->groups.size();++j)if(i!=j && !state->groups[j].members.empty() &&
                (state->groups[j].members.size()<group.members.size() ||
                    (state->groups[j].members.size()==group.members.size() && j>i)) && std::includes(group.members.begin(),group.members.end(),
                    state->groups[j].members.begin(),state->groups[j].members.end()))group.children.push_back(j);
        }
        std::stable_sort(state->ancestors.begin(),state->ancestors.end(),[&](auto a,auto b){return state->groups[a].members.size()==state->groups[b].members.size()
            ? a>b:state->groups[a].members.size()<state->groups[b].members.size();});
        std::vector<std::size_t> parents(state->bodies.size());std::iota(parents.begin(),parents.end(),0);
        const auto find=[&](std::size_t i){while(parents[i]!=i){parents[i]=parents[parents[i]];i=parents[i];}return i;};
        for(const auto& group:state->groups)if(!group.ancestor && !group.members.empty())
            for(auto member:group.members)parents[find(member)]=find(group.members.front());
        std::unordered_map<std::size_t,std::size_t> units;
        for(std::size_t i=0;i<state->bodies.size();++i)if(!state->bodies[i].held) {
            const auto root=find(i);const auto& body=state->bodies[i];
            const auto rect=Offset(body.local,body.rest);auto [it,inserted]=units.emplace(root,state->units.size());
            if(inserted)state->units.push_back({rect,{},{},false});
            auto& unit=state->units[it->second];unit.rect=Union(unit.rect,rect);unit.nodes.push_back(int(i));
            unit.changed=unit.changed || !Equal(body.rest,body.baseline);
        }
        for(std::size_t i=0;i<state->groups.size();++i)if(!state->groups[i].ancestor) {
            const auto& group=state->groups[i];
            if(group.members.empty())state->units.push_back({group.rest,{},{int(i)},!Equal(group.rest,group.baseline)});
            else {auto& unit=state->units.at(units.at(find(group.members.front())));
                unit.groups.push_back(int(i));unit.rect=Union(unit.rect,group.rest);
                unit.changed=unit.changed || !Equal(group.rest,group.baseline);}
        }
        // A later manual edit can invalidate an old automatic rest preference.
        // An unheld sibling group is rigid: accept only a common translation,
        // never independently restore its members into a different shape.
        for(auto& unit:state->units)if(!unit.groups.empty()) {
            const auto shift=!unit.nodes.empty()?Vec2{state->bodies[unit.nodes.front()].rest.x-state->bodies[unit.nodes.front()].baseline.x,
                state->bodies[unit.nodes.front()].rest.y-state->bodies[unit.nodes.front()].baseline.y}:
                Vec2{state->groups[unit.groups.front()].rest.min.x-state->groups[unit.groups.front()].baseline.min.x,
                    state->groups[unit.groups.front()].rest.min.y-state->groups[unit.groups.front()].baseline.min.y};
            const auto near=[](Vec2 a,Vec2 b){return std::abs(a.x-b.x)<.002f && std::abs(a.y-b.y)<.002f;};
            bool coherent=true;
            for(int member:unit.nodes) {const auto& b=state->bodies[member];coherent &= near(b.rest,{b.baseline.x+shift.x,b.baseline.y+shift.y});}
            for(int index:unit.groups) {const auto& g=state->groups[index];const auto desired=Offset(g.baseline,shift);
                coherent &= near(g.rest.min,desired.min) && near(g.rest.max,desired.max);}
            if(!coherent) {
                bool first=true;unit.changed=false;
                const auto include=[&](GraphRect r){unit.rect=first?r:Union(unit.rect,r);first=false;};
                for(int member:unit.nodes) {auto& b=state->bodies[member];b.rest=b.baseline;include(Offset(b.local,b.rest));}
                for(int index:unit.groups) {auto& g=state->groups[index];g.rest=g.baseline;include(g.rest);}
            }
        }
        // Direction is a preference. Preserve upstream/downstream sketch during
        // zero-offset room-making (decompile); regular dragging prefers small moves.
        std::unordered_map<std::size_t,int> flow;
        for(const auto& link:graph.GetLinks())if(indices.count(link.fromNodeId) && indices.count(link.toNodeId)) {
            const auto from=indices.at(link.fromNodeId),to=indices.at(link.toNodeId);
            if(state->bodies[from].held && !state->bodies[to].held)flow[find(to)]|=2;
            if(state->bodies[to].held && !state->bodies[from].held)flow[find(from)]|=1;
        }
        if(preserveFlow)for(const auto& [root,index]:units)state->units[index].flow=flow[root]==1?-1:flow[root]==2?1:0;
        m_State=std::move(state);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

bool RoomGesture::Move(Vec2 offset,PlacementEdit& edit,std::string& error) {
    edit={};
    if(!m_State){error="The placement gesture is not prepared.";return false;}
    auto& state=*m_State;
    if(state.initialized && Equal(offset,state.offset)){error.clear();return true;}
    try {
        if(!std::isfinite(offset.x) || !std::isfinite(offset.y))throw std::runtime_error("The drag position is not finite.");
        // These are compact geometry arrays, not authored node/recipe/image copies.
        // A spring's resting target is stable for the gesture. Re-solve from it,
        // not from yesterday's displacement, so vacated space is reclaimed.
        std::vector<Vec2> positions;for(const auto& body:state.bodies)positions.push_back(body.rest);
        std::vector<GraphRect> frames;for(const auto& group:state.groups)frames.push_back(group.rest);
        std::vector<GraphRect> reserved;
        for(auto index:state.held) {
            const auto& body=state.bodies[index];positions[index]={body.origin.x+offset.x,body.origin.y+offset.y};
            const auto rect=Offset(body.local,positions[index]);
            if(!InRange(rect))throw std::runtime_error("The held selection reached the graph coordinate limit.");
            reserved.push_back(rect);
        }
        for(std::size_t i=0;i<state.groups.size();++i)if(state.groups[i].held) {
            frames[i]=Offset(state.groups[i].origin,offset);
        }
        if(state.preserveFlow) {
            std::vector<std::size_t> fit;
            for(std::size_t i=0;i<state.groups.size();++i)if(state.groups[i].held)fit.push_back(i);
            std::stable_sort(fit.begin(),fit.end(),[&](auto a,auto b){return state.groups[a].members.size()==state.groups[b].members.size()
                ? a>b:state.groups[a].members.size()<state.groups[b].members.size();});
            for(auto i:fit) {
                const auto& group=state.groups[i];auto bounds=Offset(state.bodies[group.members.front()].local,positions[group.members.front()]);
                for(auto member:group.members)bounds=Union(bounds,Offset(state.bodies[member].local,positions[member]));
                for(auto child:group.children)bounds=Union(bounds,frames[child]);
                frames[i]=Union(frames[i],Frame(bounds));
            }
        }
        for(std::size_t i=0;i<state.groups.size();++i)if(state.groups[i].held) {
            if(!InRange(frames[i]))throw std::runtime_error("The held group reached the graph coordinate limit.");
            reserved.push_back(frames[i]);
        }
        std::vector<GraphRect> unitRects;unitRects.reserve(state.units.size());
        for(const auto& unit:state.units)unitRects.push_back(unit.rect);
        std::vector<bool> changed;for(const auto& unit:state.units)changed.push_back(unit.changed);
        std::size_t totalWork=0;
        const auto solve=[&](const std::vector<GraphRect>& roots,const std::vector<std::size_t>& inside) {
            std::vector<std::size_t> order;GraphRect envelope=roots.front();for(auto r:roots)envelope=Union(envelope,r);
            for(std::size_t i=0;i<state.units.size();++i)if(!std::any_of(state.units[i].nodes.begin(),state.units[i].nodes.end(),
                [&](int index){return std::binary_search(inside.begin(),inside.end(),std::size_t(index));}))order.push_back(i);
            const auto distance=[&](std::size_t i){const auto r=unitRects[i];const float dx=std::max({envelope.min.x-r.max.x,r.min.x-envelope.max.x,0.f});
                const float dy=std::max({envelope.min.y-r.max.y,r.min.y-envelope.max.y,0.f});return dx*dx+dy*dy;};
            std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return distance(a)<distance(b);});
            Reservations occupied;occupied.work=totalWork;occupied.limit=state.preserveFlow?kWorkLimit:200000;
            occupied.rootBounds=envelope;for(auto root:roots)occupied.Add({root,{},{},true});
            for(auto index:order) {
                const auto& unit=state.units[index];const auto delta=Place({unitRects[index],{},{},changed[index],unit.flow},occupied);
                if(delta.x!=0 || delta.y!=0) {
                    for(int member:unit.nodes){positions[member].x+=delta.x;positions[member].y+=delta.y;}
                    for(int group:unit.groups)frames[group]=Offset(frames[group],delta);
                    unitRects[index]=Offset(unitRects[index],delta);changed[index]=true;
                }
                occupied.Add({unitRects[index],{},{},changed[index],unit.flow});
            }
            totalWork=occupied.work;
        };
        solve(reserved,{});
        for(auto index:state.ancestors) {
            const auto& group=state.groups[index];GraphRect bounds=Offset(state.bodies[group.members.front()].local,positions[group.members.front()]);
            for(auto member:group.members)bounds=Union(bounds,Offset(state.bodies[member].local,positions[member]));
            for(auto child:group.children)bounds=Union(bounds,frames[child]);
            frames[index]=Union(group.rest,Frame(bounds));
            if(!InRange(frames[index]))throw std::runtime_error("The containing group reached the graph coordinate limit.");
            solve({frames[index]},group.members);
        }
        // Match the graph's position/size representation exactly, including
        // floating-point subtraction/addition after fractional translations.
        for(auto& frame:frames)frame.max={frame.min.x+(frame.max.x-frame.min.x),frame.min.y+(frame.max.y-frame.min.y)};
        for(std::size_t i=0;i<state.bodies.size();++i)if(!Equal(state.bodies[i].position,positions[i])) {
            const auto& body=state.bodies[i];edit.nodes.push_back({body.id,body.uuid,body.position,positions[i]});
        }
        for(std::size_t i=0;i<state.groups.size();++i)if(!Equal(state.groups[i].rect,frames[i]))
            edit.groups.push_back({state.groups[i].id,state.groups[i].rect,frames[i]});
        for(std::size_t i=0;i<positions.size();++i)state.bodies[i].position=positions[i];
        for(std::size_t i=0;i<frames.size();++i)state.groups[i].rect=frames[i];
        state.offset=offset;state.initialized=true;error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}

PlacementEdit RoomGesture::Restore() const {
    PlacementEdit edit;if(!m_State)return edit;
    for(const auto& body:m_State->bodies)if(!Equal(body.position,body.baseline))edit.nodes.push_back({body.id,body.uuid,body.position,body.baseline});
    for(const auto& group:m_State->groups)if(!Equal(group.rect,group.baseline))edit.groups.push_back({group.id,group.rect,group.baseline});
    return edit;
}

bool ApplyPlacement(Graph& graph,const PlacementEdit& edit,std::string& error) {
    for(const auto& move:edit.nodes){const auto* node=graph.FindNode(move.id);
        if(!node || node->instanceUuid!=move.uuid || !Equal(node->position,move.before) || !std::isfinite(move.after.x) || !std::isfinite(move.after.y)) {
            error="Node placement no longer matches the gesture's owner.";return false;}}
    for(const auto& move:edit.groups){const auto* group=graph.FindGroup(move.id);
        if(!group || !Equal(GroupRect(*group),move.before) || !InRange(move.after)) {error="Group placement no longer matches the gesture's owner.";return false;}}
    for(const auto& move:edit.nodes)graph.FindNode(move.id)->position=move.after;
    for(const auto& move:edit.groups){auto* group=graph.FindGroup(move.id);group->position=move.after.min;
        group->size={move.after.max.x-move.after.min.x,move.after.max.y-move.after.min.y};}
    error.clear();return true;
}
} // namespace Stack::GraphLayout
