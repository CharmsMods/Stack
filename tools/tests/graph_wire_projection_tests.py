#!/usr/bin/env python3
"""Focused CPU check of production wire projection and adaptive tessellation.

Extracts the projection, sampling and path construction methods unchanged.
ImGui coordinate types, telemetry and the unused async router are stand-ins;
this does not measure full-window animation, GPU work, or input delivery.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def between(source, start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cxx", default="g++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    graph = root / "src/Editor/NodeGraph"
    ui = graph / "UI"
    projection = (ui / "GraphWireRouteCache.cpp").read_text()
    projection = projection[projection.index("const ProjectedWire& WireRouteCache::Project("):]
    sampling = between((ui / "ContinuousLinkStroke.cpp").read_text(),
        "namespace {", "void DrawContinuousLinkStroke(")
    make_path = between((graph / "GraphWireRouting.cpp").read_text(),
        "Path MakePath(", "Vec2 PointAt(")
    handle = between((graph / "EditorNodeGraphUIMetrics.h").read_text(),
        "inline float LinkBezierHandle(", "inline ImVec2 CubicBezierPoint(")
    projected = between((ui / "GraphWireRouteCache.h").read_text(),
        "struct ProjectedWire {", "// One queued")
    generated = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
bool countAllocations=false;
std::size_t allocations=0;
void* operator new(std::size_t n){if(countAllocations)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}
void operator delete(void* p,std::size_t) noexcept{std::free(p);}
struct ImVec2 {float x=0,y=0; ImVec2()=default;ImVec2(float x_,float y_):x(x_),y(y_){} };
namespace Stack::GraphRouting {
using Vec2=ImVec2;
struct GraphRect {Vec2 min,max;};
struct Path {std::vector<Vec2> points;GraphRect bounds;float length=0;};
''' + make_path + r'''
}
namespace EditorNodeGraphUIMetrics {
''' + handle + r'''
}
namespace Stack::UiPerformance {
enum class Counter {WireProjection};
int rebuilds=0;
void Add(Counter) {++rebuilds;}
}
namespace Stack::Editor::NodeGraphUIVisuals {
struct CurveSample {ImVec2 point;float t;};
''' + sampling + projected + r'''
class WireRouteCache {
    struct Result {std::uint64_t revision=0;} m_Result;
    std::unordered_map<std::string,ProjectedWire> m_Projected;
public:
    const ProjectedWire& Project(const std::string&,ImVec2,ImVec2,ImVec2,float,bool,const ImVec2* =nullptr,const ImVec2* =nullptr);
};
''' + projection + r'''
using namespace Stack::Editor::NodeGraphUIVisuals;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool Near(float a,float b){return std::abs(a-b)<.002f;}
void SamePoint(ImVec2 a,ImVec2 b){Check(Near(a.x,b.x)&&Near(a.y,b.y),"translated point differs from fresh tessellation");}
void Same(const ProjectedWire& a,const ProjectedWire& b) {
    Check(a.samples.size()==b.samples.size(),"translation changed sampling density");
    Check(a.geometry.points.size()==b.geometry.points.size(),"translation lost hit geometry");
    Check(Near(a.geometry.length,b.geometry.length),"translation changed arc length");
    SamePoint(a.geometry.bounds.min,b.geometry.bounds.min);SamePoint(a.geometry.bounds.max,b.geometry.bounds.max);
    for(std::size_t i=0;i<a.samples.size();++i){SamePoint(a.samples[i].point,b.samples[i].point);
        SamePoint(a.geometry.points[i],b.geometry.points[i]);Check(Near(a.samples[i].t,b.samples[i].t),"translation changed trace/label fractions");}
}
void Exact(const ProjectedWire& a,const ProjectedWire& b) {
    Check(a.samples.size()==b.samples.size()&&a.geometry.points.size()==b.geometry.points.size(),"rebuild changed sample count");
    auto same=[](ImVec2 x,ImVec2 y){return x.x==y.x&&x.y==y.y;};
    Check(a.geometry.length==b.geometry.length&&same(a.geometry.bounds.min,b.geometry.bounds.min)&&
        same(a.geometry.bounds.max,b.geometry.bounds.max),"rebuild changed path bounds/length");
    for(std::size_t i=0;i<a.samples.size();++i)
        Check(same(a.samples[i].point,b.samples[i].point)&&a.samples[i].t==b.samples[i].t&&
            same(a.geometry.points[i],b.geometry.points[i]),"rebuild changed sample/trace geometry");
}
int main()try{
    using Stack::UiPerformance::rebuilds;
    int translatedFrames=0;
    for(bool gateway:{false,true})for(bool straight:{false,true}) {
        WireRouteCache cache;
        ImVec2 from{32,45},to{824,301},via{324,207},exit{396,207};
        const auto first=cache.Project("wire",from,to,{},1,straight,gateway?&via:nullptr,gateway?&exit:nullptr);
        const auto* allocation=cache.Project("wire",from,to,{},1,straight,gateway?&via:nullptr,gateway?&exit:nullptr).samples.data();
        for(int frame=0;frame<48;++frame) {
            // Fractional slide, reverse and interrupted target: only geometry
            // matters, not a remembered transition direction or destination.
            const float x=frame<16?frame*1.125f:frame<32?(32-frame)*1.125f:(frame-40)*2.25f;
            const float y=frame*.25f;
            auto shift=[&](ImVec2 p){return ImVec2{p.x+x,p.y+y};};
            const auto a=shift(from),b=shift(to),c=shift(via),d=shift(exit);
            const int before=rebuilds;
            const auto& got=cache.Project("wire",a,b,{x,y},1,straight,gateway?&c:nullptr,gateway?&d:nullptr);
            Check(rebuilds==before,"pure translation re-tessellated a wire");
            Check(got.samples.data()==allocation,"translation allocated new sample storage");
            Check(got.geometry.length==first.geometry.length,"cached arc length drifted");
            WireRouteCache fresh;
            Same(got,fresh.Project("wire",a,b,{x,y},1,straight,gateway?&c:nullptr,gateway?&d:nullptr));
            ++translatedFrames;
        }
    }
    // Geometry buffers survive shape/zoom changes; adaptive sampling itself
    // remains unchanged and may still allocate. Growth is allowed to allocate.
    for(bool gateway:{false,true})for(bool straight:{false,true}) {
        WireRouteCache changing;
        const void* points=nullptr;std::size_t capacity=0;
        int frame=0;
        for(float extent:{800.f,600.f,1200.f,300.f,700.f,200.f}) {
            const float zoom=1.f+.01f*frame++;
            ImVec2 a{32,45},b{extent,301},via{extent*.4f,207},exit{extent*.4f+72,207};
            const auto& got=changing.Project("wire",a,b,{},zoom,straight,gateway?&via:nullptr,gateway?&exit:nullptr);
            WireRouteCache fresh;
            Exact(got,fresh.Project("wire",a,b,{},zoom,straight,gateway?&via:nullptr,gateway?&exit:nullptr));
            if(points&&got.geometry.points.size()<=capacity)
                Check(got.geometry.points.data()==points,"shape/zoom rebuilt an existing geometry allocation");
            points=got.geometry.points.data();capacity=got.geometry.points.capacity();
        }
    }
    WireRouteCache allocationCheck;
    allocationCheck.Project("wire",{32,45},{824,301},{},1,false);
    allocations=0;countAllocations=true;
    allocationCheck.Project("wire",{32,45},{823,301},{},1.01f,false);
    countAllocations=false;
    Check(allocations==1,"warm shape update should allocate sampling only, not geometry");
    WireRouteCache cache;
    ImVec2 via{300,100},exit{360,100};
    auto project=[&](ImVec2 end,float zoom,bool straight,const ImVec2* v,const ImVec2* e){
        const int before=rebuilds;cache.Project("wire",{10,20},end,{},zoom,straight,v,e);
        Check(rebuilds==before+1,"changed wire shape reused stale projection");
    };
    project({600,220},1,false,nullptr,nullptr);
    project({601,220},1,false,nullptr,nullptr); // one endpoint moved
    project({601,220},2,false,nullptr,nullptr); // zoom
    project({601,220},2,true,nullptr,nullptr);  // line-style setting
    project({601,220},2,true,&via,&exit);       // gateway admitted
    exit.x+=1;project({601,220},2,true,&via,&exit); // gateway width
    via.y+=1;project({601,220},2,true,&via,&exit); // gateway moved independently
    project({601,220},2,true,nullptr,nullptr);  // gateway removed
    const int before=rebuilds;
    cache.Project("other",{10,20},{601,220},{},2,true);
    WireRouteCache otherOwner;otherOwner.Project("wire",{10,20},{601,220},{},2,true);
    Check(rebuilds==before+2,"wire keys or graph owners shared projection state");
    std::cout<<"PASS "<<translatedFrames<<" translated frames: no tessellation rebuilds, stable storage, fresh-geometry agreement; exact rebuilt geometry/storage and endpoint/zoom/style/gateway/key/owner invalidations passed.\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    with tempfile.TemporaryDirectory(prefix="stack-wire-projection-") as temp:
        cpp = Path(temp) / "test.cpp"
        exe = Path(temp) / "test"
        cpp.write_text(generated)
        subprocess.run([args.cxx, "-std=c++17", "-O2", str(cpp), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
