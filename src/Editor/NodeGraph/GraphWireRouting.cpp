#include "GraphWireRouting.h"
#include "GraphWireRoutingRuntime.h"
#include "GraphRoutingBackend/LibavoidBridge.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

namespace Stack::GraphRouting {
Path MakePath(std::vector<Vec2> points) {
    Path path;path.points=std::move(points);
    if(path.points.empty())return path;
    path.bounds={path.points.front(),path.points.front()};
    for(std::size_t i=0;i<path.points.size();++i) {
        auto p=path.points[i];
        path.bounds.min.x=std::min(path.bounds.min.x,p.x);path.bounds.min.y=std::min(path.bounds.min.y,p.y);
        path.bounds.max.x=std::max(path.bounds.max.x,p.x);path.bounds.max.y=std::max(path.bounds.max.y,p.y);
        if(i)path.length+=std::hypot(p.x-path.points[i-1].x,p.y-path.points[i-1].y);
    }
    return path;
}
Vec2 PointAt(const Path& path,float fraction,Vec2* tangent) {
    if(path.points.empty())return {};
    float distance=std::clamp(fraction,0.f,1.f)*path.length;
    for(std::size_t i=1;i<path.points.size();++i) {
        const auto a=path.points[i-1],b=path.points[i];const float length=std::hypot(b.x-a.x,b.y-a.y);
        if(length<=1e-6f)continue;
        if(distance<=length || i+1==path.points.size()) {
            if(tangent)*tangent={(b.x-a.x)/length,(b.y-a.y)/length};
            const float t=std::clamp(distance/length,0.f,1.f);return {a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};
        }
        distance-=length;
    }
    if(tangent)*tangent={1,0};return path.points.back();
}
float DistanceToPath(const Path& path,Vec2 point) {
    float best=std::numeric_limits<float>::infinity();
    for(std::size_t i=1;i<path.points.size();++i) {
        const auto a=path.points[i-1],b=path.points[i];const float x=b.x-a.x,y=b.y-a.y;
        const float squared=x*x+y*y;
        const float t=squared>0?std::clamp(((point.x-a.x)*x+(point.y-a.y)*y)/squared,0.f,1.f):0;
        best=std::min(best,std::hypot(point.x-a.x-x*t,point.y-a.y-y*t));
    }
    return best;
}
struct Router::State {
    const std::atomic<bool>* cancelled;
    void* backend=nullptr;
    std::unordered_map<std::string,std::uint64_t> ids;
    std::uint64_t nextId=0;
    explicit State(const std::atomic<bool>* flag):cancelled(flag) {
        char error[512]{};
        backend=StackAvoidCreate([](void* context)->int {
            const auto* state=static_cast<const State*>(context);
            return state->cancelled && state->cancelled->load(std::memory_order_relaxed);
        },this,error,sizeof(error));
        if(!backend)throw std::runtime_error(error);
    }
    ~State(){StackAvoidDestroy(backend);}
};
Router::Router(const std::atomic<bool>* flag) {
    EnsureRuntime();m_State=std::make_unique<State>(flag);
}
Router::~Router()=default;
Result Router::Update(const Scene& scene) {
    Result result;result.revision=scene.revision;auto& state=*m_State;
    std::vector<StackAvoidObstacle> obstacles;obstacles.reserve(scene.obstacles.size());
    for(const auto& obstacle:scene.obstacles)obstacles.push_back({obstacle.id,
        {obstacle.bounds.min.x,obstacle.bounds.min.y},{obstacle.bounds.max.x,obstacle.bounds.max.y}});
    std::vector<StackAvoidConnection> connections;connections.reserve(scene.connections.size());
    std::unordered_map<std::uint64_t,const std::string*> keys;
    std::unordered_set<std::string> active;
    for(const auto& wire:scene.connections) {
        active.insert(wire.key);auto found=state.ids.find(wire.key);
        if(found==state.ids.end())found=state.ids.emplace(wire.key,++state.nextId).first;
        keys.emplace(found->second,&wire.key);
        connections.push_back({found->second,{wire.source.x,wire.source.y},{wire.target.x,wire.target.y}});
    }
    for(auto it=state.ids.begin();it!=state.ids.end();) {
        if(!active.count(it->first))it=state.ids.erase(it);else ++it;
    }
    struct Sink {Result& result;const std::unordered_map<std::uint64_t,const std::string*>& keys;} sink{result,keys};
    char error[512]{};
    const bool updated=StackAvoidUpdate(state.backend,obstacles.data(),obstacles.size(),connections.data(),connections.size(),
        [](void* context,std::uint64_t id,const StackAvoidPoint* points,std::size_t count)->int {
            // Copy into Stack-owned memory before returning to the DLL. Even
            // allocation failures stay within their originating C++ runtime.
            try {
                auto& sink=*static_cast<Sink*>(context);std::vector<Vec2> path;path.reserve(count);
                for(std::size_t i=0;i<count;++i)path.push_back({points[i].x,points[i].y});
                sink.result.paths.emplace(*sink.keys.at(id),MakePath(std::move(path)));return 1;
            } catch(...){return 0;}
        },&sink,error,sizeof(error))!=0;
    if(!updated)throw std::runtime_error(error);
    return result;
}
}
