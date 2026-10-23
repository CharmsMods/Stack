#include "Editor/NodeGraph/GraphWireRouting.h"
#include "Editor/NodeGraph/Layout/GraphFlowLayout.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack::GraphRouting;
void Check(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
    std::cout<<"PASS "<<message<<'\n';
}
bool Equal(Vec2 a,Vec2 b){return std::hypot(a.x-b.x,a.y-b.y)<.001f;}
bool Clear(const Path& path,GraphRect rect) {
    // Strict interior: touching an expanded boundary is legal.
    for(std::size_t i=1;i<path.points.size();++i) {
        const auto a=path.points[i-1],b=path.points[i];
        if(std::abs(a.y-b.y)<.001f && a.y>rect.min.y && a.y<rect.max.y &&
            std::max(a.x,b.x)>rect.min.x && std::min(a.x,b.x)<rect.max.x)return false;
        if(std::abs(a.x-b.x)<.001f && a.x>rect.min.x && a.x<rect.max.x &&
            std::max(a.y,b.y)>rect.min.y && std::min(a.y,b.y)<rect.max.y)return false;
    }
    return true;
}
}
void RunGraphWireRoutingTests() {
    Router router;
    Scene scene{1,{{1,{{0,0},{100,100}}},{2,{{500,0},{600,100}}},{3,{{230,-30},{350,130}}}},
        {{"main",{112,50},{488,50}}}};
    auto result=router.Update(scene);const auto first=result.paths.at("main");
    Check(result.error.empty() && first.points.size()>2,"wire detours around an unrelated node");
    Check(Equal(first.points.front(),{112,50}) && Equal(first.points.back(),{488,50}),"route preserves exact socket endpoints");
    Check(first.points[1].x>112 && first.points[first.points.size()-2].x<488,"output escapes right and input arrives from the left");
    Check(Clear(first,{{222,-38},{358,138}}),"route clears the obstacle including its eight-unit buffer");
    Check(DistanceToPath(first,PointAt(first,.5f))<.001f,"drawing and picking share arc-length geometry");
    scene.obstacles[2].bounds={{230,300},{350,460}};++scene.revision;
    result=router.Update(scene);const auto unblocked=result.paths.at("main");
    Check(unblocked.length<first.length,"moving an unrelated obstacle releases a shorter route");
    scene.obstacles[2].bounds={{230,-30},{350,130}};++scene.revision;
    result=router.Update(scene);
    Check(Clear(result.paths.at("main"),{{222,-38},{358,138}}),"returning obstacle invalidates a cable with unchanged endpoints");
    scene.connections.push_back({"second",{112,65},{488,65}});++scene.revision;
    result=router.Update(scene);
    const auto& a=result.paths.at("main");const auto& b=result.paths.at("second");
    Check(DistanceToPath(b,PointAt(a,.5f))>=10,"unrelated parallel cables occupy distinct routing tracks");
    scene.connections.push_back({"fanout",{112,50},{488,80}});++scene.revision;
    result=router.Update(scene);
    Check(Equal(result.paths.at("fanout").points.front(),result.paths.at("main").points.front()),"same-port fan-out retains its real common endpoint");
    scene.connections={{"backward",{612,50},{-12,50}}};++scene.revision;
    result=router.Update(scene);const auto& backward=result.paths.at("backward");
    Check(backward.points.size()>3 && backward.points[1].x>612 && backward.points[backward.points.size()-2].x<-12,
        "backward links respect both port directions");
    Check(result.paths.size()==1 && !result.paths.count("main"),"deleted wires leave no stale route");
    std::atomic<bool> cancelled=true;Router stopped(&cancelled);
    Check(stopped.Update(scene).paths.empty(),"cancelled geometry owners publish no paths");

    using namespace Stack::GraphLayout;
    std::unordered_map<int,FlowSize> sizes{{1,{100,100}},{2,{100,60}},{3,{100,60}},{4,{100,100}}};
    auto placed=ArrangeFlow({{1},{2,3}},sizes,{{1,2,85,30},{1,3,15,30}},{0,0});
    Check(placed.at(3).y<placed.at(2).y,"measured source port order reduces branch crossings");
    Check(placed.at(2).y-(placed.at(3).y+sizes.at(3).height)>=127.99f,"parallel branches receive more vertical breathing room");
    const auto continued=ArrangeFlow({{1},{2,3},{4}},sizes,{{1,2,15,30},{1,3,85,30},{2,4,30,50}},{0,0});
    Check(std::abs((continued.at(2).y+30)-(continued.at(4).y+50))<.01f,
        "a continuing branch keeps its vertical lane when its sibling ends");
    std::vector<FlowEdge> crowded;for(int i=0;i<8;++i)crowded.push_back({1,4,10.f*i,90.f-10.f*i});
    const auto quiet=ArrangeFlow({{1},{4}},sizes,{{1,4,50,50}},{0,0});
    placed=ArrangeFlow({{1},{4}},sizes,crowded,{0,0});
    Check(placed.at(4).x>quiet.at(4).x,"only a crowded corridor requests extra column width");
    auto plain=ArrangeFlow({{1},{2},{4}},sizes,{{1,2,50,30},{2,4,30,50}},{0,0});
    placed=ArrangeFlow({{1},{2},{4}},sizes,{{1,2,50,30},{2,4,30,50},{1,4,50,50}},{0,0});
    Check(placed.size()==3 && placed.at(2).y!=plain.at(2).y,"long cables reserve intermediate passage without creating authored nodes");
    const std::unordered_map<int,FlowSize> feederSizes{{1,{100,80}},{2,{100,80}},
        {3,{100,80}},{4,{100,80}},{5,{100,100}}};
    const std::vector<FlowEdge> feederEdges{{1,2,40,40},{2,3,40,40},
        {3,5,40,45},{4,5,40,55}};
    const auto feeders=ArrangeFlow({{1,4},{2},{3},{5}},feederSizes,feederEdges,{0,0});
    Check(std::abs(feeders.at(3).y-feeders.at(4).y)>=220.f,
        "unequal length feeder chains retain separate vertical approach bands");
    Check(std::abs(feeders.at(1).y-feeders.at(3).y)<.01f,
        "merge approach separation propagates through the whole feeder chain");
    const auto repeated=ArrangeFlow({{1,4},{2},{3},{5}},feederSizes,feederEdges,{0,0});
    bool stable=true;for(const auto& [id,p]:feeders)stable=stable && Equal(p,repeated.at(id));
    Check(stable,"finite feeder placement is deterministic and creates no virtual authored nodes");
}
