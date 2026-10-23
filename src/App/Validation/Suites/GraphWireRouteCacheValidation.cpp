#include "Editor/NodeGraph/UI/GraphWireRouteCache.h"
#include "App/UiPerformanceTrace.h"
#include "ThirdParty/json.hpp"
#include <chrono>
#include <algorithm>
#include <thread>

namespace Stack::Validation {
nlohmann::json CheckGraphWireRouteCache() {
    using namespace Editor::NodeGraphUIVisuals;
    namespace Perf=UiPerformance;
    auto checks=nlohmann::json::array();
    const auto check=[&](bool pass,const char* id,const char* detail){checks.push_back({{"id",id},{"passed",pass},{"detail",detail}});};
    const auto finish=[] {Perf::FinishFrame({},0,0,0,0,0,0,0);};
    const auto counter=[](Perf::Counter id){return Perf::LastFrame().counters[std::size_t(id)];};
    const auto wait=[](WireRouteCache& cache) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(8);
        while(cache.Pending() && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        cache.Poll();return !cache.Pending() && cache.Error().empty();
    };
    Perf::EnableFrameCapture(true);Perf::BeginFrame();
    auto cache=std::make_unique<WireRouteCache>();
    GraphRouting::Scene scene{1,{{1,{{200,-40},{300,80}}}},{{"wire",{0,20},{500,20}}}};
    cache->Request(scene);
    check(wait(*cache),"Wire.async","A geometry request completes on the existing worker pool");
    const auto original=cache->Project("wire",{0,20},{500,20},{0,0},1,false);
    check(original.routeRevision==1 && original.samples.size()>2 && std::abs(original.geometry.length-500.f)<.01f,
        "Wire.publish","Visible links preserve the original endpoint curve instead of displaying orthogonal detours");
    finish();Perf::BeginFrame();
    const auto& same=cache->Project("wire",{0,20},{500,20},{0,0},1,false);
    (void)same;finish();
    check(counter(Perf::Counter::WireProjection)==0 && counter(Perf::Counter::WireRouteRequest)==0,
        "Wire.idle","Settled frames neither resample wires nor request routing");
    Perf::BeginFrame();
    const auto projected=cache->Project("wire",{40,90},{1040,90},{40,50},2,false);
    bool matching=true;
    for(int i=0;matching && i<=20;++i) {
        const auto a=GraphRouting::PointAt(original.geometry,i/20.f),b=GraphRouting::PointAt(projected.geometry,i/20.f);
        matching=std::abs(b.x-(40+2*a.x))<.01f && std::abs(b.y-(50+2*a.y))<.01f;
    }
    finish();
    check(matching && counter(Perf::Counter::WireRouteRequest)==0,"Wire.camera","Pan and zoom only project cached graph geometry");
    for(int i=2;i<=33;++i){scene.revision=i;scene.obstacles[0].bounds.min.y=float(i);scene.obstacles[0].bounds.max.y=float(i+120);cache->Request(scene);}
    check(wait(*cache),"Wire.coalesce","Continuous input drains its coalesced final request");
    const auto final=cache->Project("wire",{0,20},{500,20},{0,0},1,false);
    check(final.routeRevision==33,"Wire.final","Release retains the exact latest geometry revision");
    const auto live=cache->Project("wire",{7,25},{507,25},{0,0},1,false);
    check(live.samples.front().point.x==7 && live.samples.front().point.y==25 && live.samples.back().point.x==507,
        "Wire.live-ports","Wire feedback stays attached to displayed sockets while new geometry is pending");
    ImVec2 entry{180,140},exit{340,140};
    const auto includes=[](const ProjectedWire& path,ImVec2 p) {
        return std::any_of(path.samples.begin(),path.samples.end(),[&](const auto& sample) {
            return sample.point.x==p.x && sample.point.y==p.y;
        });
    };
    const auto gateway=cache->Project("gateway",{0,-80},{600,320},{0,0},1,false,&entry,&exit);
    check(includes(gateway,entry) && includes(gateway,exit),"Wire.gateway-anchors",
        "Curved gateway wires meet both distinct pins even when the external endpoints have different heights");
    finish();Perf::BeginFrame();
    cache->Project("gateway",{0,-80},{600,320},{0,0},1,false,&entry,&exit);
    finish();
    check(counter(Perf::Counter::WireProjection)==0,"Wire.gateway-idle","Stable gateway anchors reuse sampled geometry");
    Perf::BeginFrame();entry.y+=35;
    const auto moved=cache->Project("gateway",{0,-80},{600,320},{0,0},1,false,&entry,&exit);
    finish();
    check(includes(moved,entry) && includes(moved,exit) && counter(Perf::Counter::WireProjection)==1,
        "Wire.gateway-move","Changing only the entry anchor reprojects both correctly attached curves once");
    Perf::BeginFrame();exit.x+=40;
    const auto resized=cache->Project("gateway",{0,-80},{600,320},{0,0},1,false,&entry,&exit);
    finish();
    check(includes(resized,entry) && includes(resized,exit) && counter(Perf::Counter::WireProjection)==1,
        "Wire.gateway-exit","Changing only the exit anchor invalidates the projection without a hardcoded gateway width");
    Perf::BeginFrame();
    scene.revision=34;cache->Request(scene);cache.reset();
    cache=std::make_unique<WireRouteCache>();scene.revision=99;scene.obstacles.clear();scene.connections[0].source={2000,20};scene.connections[0].target={2500,20};
    cache->Request(scene);
    check(wait(*cache) && cache->Project("wire",{2000,20},{2500,20},{0,0},1,false).routeRevision==99,
        "Wire.owner","Replacing an active canvas cannot publish its old result into the replacement");
    finish();Perf::EnableFrameCapture(false);
    return checks;
}
}
