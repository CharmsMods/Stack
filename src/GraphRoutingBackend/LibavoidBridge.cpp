#include "LibavoidBridge.h"
#include <libavoid/libavoid.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
bool Equal(StackAvoidPoint a,StackAvoidPoint b){return a.x==b.x && a.y==b.y;}
bool Finite(StackAvoidPoint a){return std::isfinite(a.x) && std::isfinite(a.y);}
Avoid::Rectangle Rectangle(const StackAvoidObstacle& r){return {{r.minimum.x,r.minimum.y},{r.maximum.x,r.maximum.y}};}
void Error(char* output,size_t capacity,const char* message) noexcept {
    if(!output || !capacity)return;
    const auto size=std::min(capacity-1,std::strlen(message));std::memcpy(output,message,size);output[size]=0;
}
struct Router : Avoid::Router {
    StackAvoidCancelled cancelled;void* context;
    struct Shape {Avoid::ShapeRef* value;StackAvoidObstacle bounds;};
    struct Wire {Avoid::ConnRef* value;StackAvoidPoint source,target;};
    std::unordered_map<int,Shape> shapes;
    std::unordered_map<uint64_t,Wire> wires;
    Router(StackAvoidCancelled cancel,void* owner):Avoid::Router(Avoid::OrthogonalRouting),cancelled(cancel),context(owner) {
        setRoutingParameter(Avoid::shapeBufferDistance,8);
        setRoutingParameter(Avoid::idealNudgingDistance,14);
        setRoutingParameter(Avoid::segmentPenalty,20);
        setRoutingParameter(Avoid::crossingPenalty,60);
        setRoutingParameter(Avoid::fixedSharedPathPenalty,100);
        setRoutingOption(Avoid::nudgeSharedPathsWithCommonEndPoint,true);
        setRoutingOption(Avoid::nudgeOrthogonalSegmentsConnectedToShapes,false);
        setRoutingOption(Avoid::improveHyperedgeRoutesMovingJunctions,false);
    }
    bool Cancelled() const{return cancelled && cancelled(context);}
    bool shouldContinueTransactionWithProgress(unsigned int,unsigned int,unsigned int,double) override {return !Cancelled();}
};
}
extern "C" {
void* StackAvoidCreate(StackAvoidCancelled cancelled,void* context,char* error,size_t capacity) {
    try {return new Router(cancelled,context);}
    catch(const std::exception& e){Error(error,capacity,e.what());}
    catch(...){Error(error,capacity,"Graph routing could not initialize.");}
    return nullptr;
}
void StackAvoidDestroy(void* router){delete static_cast<Router*>(router);}
int StackAvoidUpdate(void* handle,const StackAvoidObstacle* obstacles,size_t obstacleCount,
    const StackAvoidConnection* connections,size_t connectionCount,
    StackAvoidPathSink sink,void* context,char* error,size_t capacity) {
    try {
        if(!handle || !sink)throw std::runtime_error("Invalid graph routing request.");
        auto& state=*static_cast<Router*>(handle);
        if(state.Cancelled())return 1;
        std::unordered_set<int> shapeIds;std::unordered_set<uint64_t> wireIds;
        // Validate the immutable snapshot before mutating the incremental backend.
        for(size_t i=0;i<obstacleCount;++i) {
            const auto& r=obstacles[i];
            if(!Finite(r.minimum) || !Finite(r.maximum) || r.minimum.x>=r.maximum.x || r.minimum.y>=r.maximum.y || !shapeIds.insert(r.id).second)
                throw std::runtime_error("Invalid graph obstacle geometry.");
        }
        for(size_t i=0;i<connectionCount;++i)if(!Finite(connections[i].source) || !Finite(connections[i].target) || !wireIds.insert(connections[i].id).second)
            throw std::runtime_error("Invalid graph connection geometry.");
        for(size_t i=0;i<obstacleCount;++i) {
            const auto& obstacle=obstacles[i];auto found=state.shapes.find(obstacle.id);
            if(found==state.shapes.end())state.shapes.emplace(obstacle.id,Router::Shape{new Avoid::ShapeRef(&state,Rectangle(obstacle)),obstacle});
            else if(!Equal(found->second.bounds.minimum,obstacle.minimum) || !Equal(found->second.bounds.maximum,obstacle.maximum)) {
                state.moveShape(found->second.value,Rectangle(obstacle));found->second.bounds=obstacle;
            }
        }
        for(auto it=state.shapes.begin();it!=state.shapes.end();) {
            if(!shapeIds.count(it->first)){state.deleteShape(it->second.value);it=state.shapes.erase(it);}else ++it;
        }
        for(size_t i=0;i<connectionCount;++i) {
            const auto& wire=connections[i];auto found=state.wires.find(wire.id);
            if(found==state.wires.end())found=state.wires.emplace(wire.id,Router::Wire{new Avoid::ConnRef(&state),{},{}}).first;
            auto& stored=found->second;
            if(!Equal(stored.source,wire.source) || !Equal(stored.target,wire.target) || stored.value->displayRoute().size()==0) {
                stored.value->setSourceEndpoint(Avoid::ConnEnd({wire.source.x,wire.source.y},Avoid::ConnDirRight));
                stored.value->setDestEndpoint(Avoid::ConnEnd({wire.target.x,wire.target.y},Avoid::ConnDirLeft));
                // Free endpoint direction flags alone can be simplified into
                // a vertical first/last segment on backward links. Explicit
                // escape checkpoints preserve the native port direction.
                stored.value->setRoutingCheckpoints({
                    Avoid::Checkpoint({wire.source.x+16,wire.source.y},Avoid::ConnDirLeft,Avoid::ConnDirAll),
                    Avoid::Checkpoint({wire.target.x-16,wire.target.y},Avoid::ConnDirAll,Avoid::ConnDirRight)});
                stored.source=wire.source;stored.target=wire.target;
            }
        }
        for(auto it=state.wires.begin();it!=state.wires.end();) {
            if(!wireIds.count(it->first)){state.deleteConnector(it->second.value);it=state.wires.erase(it);}else ++it;
        }
        state.processTransaction();
        if(state.Cancelled())return 1;
        std::vector<StackAvoidPoint> points;
        for(size_t i=0;i<connectionCount;++i) {
            const auto& wire=connections[i];const auto& route=state.wires.at(wire.id).value->displayRoute();
            points.clear();points.reserve(route.ps.size());
            for(const auto& point:route.ps) {
                if(!std::isfinite(point.x) || !std::isfinite(point.y))throw std::runtime_error("Graph routing produced invalid geometry.");
                points.push_back({float(point.x),float(point.y)});
            }
            if(points.size()<2)throw std::runtime_error("No valid graph wire route was produced.");
            if(!sink(context,wire.id,points.data(),points.size()))throw std::runtime_error("Graph routing could not copy its result.");
        }
        return 1;
    } catch(const std::exception& e){Error(error,capacity,e.what());}
    catch(...){Error(error,capacity,"Graph routing failed.");}
    return 0;
}
}
