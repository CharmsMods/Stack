#include "GraphWireRouteCache.h"
#include "Async/TaskSystem.h"
#include "App/UiPerformanceTrace.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <algorithm>
#include <cmath>

namespace Stack::Editor::NodeGraphUIVisuals {
struct WireRouteCache::Work {
    mutable std::mutex mutex;
    std::atomic<bool> cancelled=false;
    std::atomic<bool> ready=false;
    std::optional<GraphRouting::Scene> pending;
    std::optional<GraphRouting::Result> completed;
    bool running=false;
    std::unique_ptr<GraphRouting::Router> router;
};
WireRouteCache::WireRouteCache():m_Work(std::make_shared<Work>()) {}
WireRouteCache::~WireRouteCache(){m_Work->cancelled=true;}
void WireRouteCache::Request(GraphRouting::Scene scene) {
    UiPerformance::Add(UiPerformance::Counter::WireRouteRequest);
    auto work=m_Work;
    {
        std::lock_guard lock(work->mutex);work->pending=std::move(scene);
        if(work->running)return;
        work->running=true;
    }
    if(!Async::TaskSystem::Get().Submit([work] {
        try {
            if(!work->router)work->router=std::make_unique<GraphRouting::Router>(&work->cancelled);
            while(!work->cancelled) {
                GraphRouting::Scene scene;
                {std::lock_guard lock(work->mutex);
                    if(!work->pending){work->running=false;return;}
                    scene=std::move(*work->pending);work->pending.reset();}
                auto result=work->router->Update(scene);
                {std::lock_guard lock(work->mutex);
                    if(!work->cancelled){work->completed=std::move(result);work->ready.store(true,std::memory_order_release);}}
            }
        } catch(const std::exception& error) {
            std::lock_guard lock(work->mutex);work->completed=GraphRouting::Result{};
            work->completed->error=error.what();work->pending.reset();work->ready.store(true,std::memory_order_release);
        }
        std::lock_guard lock(work->mutex);work->running=false;
    })) {std::lock_guard lock(work->mutex);work->running=false;work->pending.reset();
        work->completed=GraphRouting::Result{};work->completed->error="The graph routing worker could not start.";
        work->ready.store(true,std::memory_order_release);}
}
bool WireRouteCache::Pending() const {std::lock_guard lock(m_Work->mutex);return m_Work->running;}
bool WireRouteCache::Poll() {
    if(!m_Work->ready.load(std::memory_order_acquire))return false;
    std::lock_guard lock(m_Work->mutex);
    if(!m_Work->completed)return false;
    // A completed geometry snapshot may be one input behind during a drag.
    // Retarget its endpoints below; never withhold all feedback until release.
    if(m_Work->completed->error.empty())m_Result=std::move(*m_Work->completed);
    else m_Result.error=std::move(m_Work->completed->error);
    m_Work->completed.reset();
    m_Work->ready.store(false,std::memory_order_release);
    UiPerformance::Add(UiPerformance::Counter::WireRoutePublish);return true;
}
const ProjectedWire& WireRouteCache::Project(const std::string& key,ImVec2 source,ImVec2 target,ImVec2 origin,float zoom,bool straight,const ImVec2* via,const ImVec2* exit) {
    const ImVec2* departure=exit?exit:via;
    auto& cache=m_Projected[key];
    cache.routeRevision=m_Result.revision;
    if(cache.zoom==zoom && cache.origin.x==origin.x && cache.origin.y==origin.y &&
        cache.source.x==source.x && cache.source.y==source.y && cache.target.x==target.x && cache.target.y==target.y &&
        cache.straight==straight && cache.hasVia==(via!=nullptr) && (!via || (cache.via.x==via->x && cache.via.y==via->y && cache.exit.x==departure->x && cache.exit.y==departure->y)) && !cache.samples.empty())return cache;
    UiPerformance::Add(UiPerformance::Counter::WireProjection);
    cache.source=source;cache.target=target;cache.origin=origin;cache.zoom=zoom;cache.routeRevision=m_Result.revision;cache.straight=straight;
    cache.hasVia=via!=nullptr;if(via){cache.via=*via;cache.exit=*departure;}
    // The visible wire keeps Stack's original endpoint-based cubic shape.
    // The optional obstacle backend remains separate from presentation; its
    // orthogonal waypoints must not turn these curves into right-angle paths.
    if(via) {
        const float a=straight?0:EditorNodeGraphUIMetrics::LinkBezierHandle(source,*via);
        const float b=straight?0:EditorNodeGraphUIMetrics::LinkBezierHandle(*departure,target);
        cache.samples=SampleLinkCurve(source,{source.x+a,source.y},{via->x-a,via->y},*via);
        // The gateway card covers the short interior span. Both visible curves
        // terminate exactly at its own input/output anchors; width is UI-owned.
        if(via->x!=departure->x || via->y!=departure->y)cache.samples.push_back({*departure,0});
        const auto tail=SampleLinkCurve(*departure,{departure->x+b,departure->y},{target.x-b,target.y},target);
        cache.samples.insert(cache.samples.end(),tail.begin()+1,tail.end());
    } else {
        const float handle=straight?0:EditorNodeGraphUIMetrics::LinkBezierHandle(source,target);
        cache.samples=SampleLinkCurve(source,{source.x+handle,source.y},{target.x-handle,target.y},target);
    }
    std::vector<GraphRouting::Vec2> geometry;geometry.reserve(cache.samples.size());
    for(const auto& sample:cache.samples)geometry.push_back({sample.point.x,sample.point.y});
    cache.geometry=GraphRouting::MakePath(std::move(geometry));
    float distance=0;
    for(std::size_t i=0;i<cache.samples.size();++i) {
        if(i)distance+=std::hypot(cache.samples[i].point.x-cache.samples[i-1].point.x,cache.samples[i].point.y-cache.samples[i-1].point.y);
        cache.samples[i].t=cache.geometry.length>0?distance/cache.geometry.length:0;
    }
    return cache;
}
}
