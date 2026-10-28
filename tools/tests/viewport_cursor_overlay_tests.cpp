#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <vector>
// PRODUCTION_READOUT
static ImVec2 MagnifierPlacement(ImVec2 mousePos) {
    const float magSize=132;
// PRODUCTION_MAGNIFIER_PLACEMENT
    return magPos;
}
struct Result {std::vector<ImDrawVert> vertices;ImVec2 magnifier;};
static Result Draw(ImVec2 origin,ImVec2 size,ImVec2 localMouse,float scale,bool detached) {
    auto&io=ImGui::GetIO();io.DisplaySize=detached?ImVec2(1200,900):size;
    io.DisplayFramebufferScale={scale,scale};ImGui::NewFrame();
    auto*main=ImGui::GetMainViewport();main->Pos=main->WorkPos={0,0};main->Size=main->WorkSize=io.DisplaySize;
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize(size);
    ImGui::Begin("host",nullptr,ImGuiWindowFlags_NoDecoration);
    auto*window=ImGui::GetCurrentWindow();auto*previous=window->Viewport;
    // A separate current-window viewport catches accidental use of main-window
    // bounds or main foreground draw list. No platform backend is simulated.
    ImGuiViewportP viewport;viewport.ID=0xABCD;viewport.Pos=viewport.WorkPos=origin;
    viewport.Size=viewport.WorkSize=size;window->Viewport=&viewport;ImGui::SetCurrentViewport(window,&viewport);
    const ImVec2 mouse(origin.x+localMouse.x,origin.y+localMouse.y);
    DrawPixelInspectionCursorReadout(mouse,{30,50,90,255},1,1,1);
    Result result;
    auto*draw=ImGui::GetForegroundDrawList(&viewport);
    for(auto vertex:draw->VtxBuffer) {vertex.pos.x-=origin.x;vertex.pos.y-=origin.y;result.vertices.push_back(vertex);}
    result.magnifier=MagnifierPlacement(mouse);result.magnifier.x-=origin.x;result.magnifier.y-=origin.y;
    window->Viewport=previous;ImGui::SetCurrentViewport(window,previous);ImGui::End();ImGui::EndFrame();return result;
}
static bool Near(float a,float b){return std::abs(a-b)<.001f;}
static bool Same(const Result&a,const Result&b) {
    if(a.vertices.empty()||a.vertices.size()!=b.vertices.size()||!Near(a.magnifier.x,b.magnifier.x)||!Near(a.magnifier.y,b.magnifier.y))return false;
    for(std::size_t i=0;i<a.vertices.size();++i) {
        const auto&x=a.vertices[i];const auto&y=b.vertices[i];
        if(!Near(x.pos.x,y.pos.x)||!Near(x.pos.y,y.pos.y)||x.uv.x!=y.uv.x||x.uv.y!=y.uv.y||x.col!=y.col)return false;
    }
    return true;
}
int main() {
    ImGui::CreateContext();auto&io=ImGui::GetIO();io.IniFilename=nullptr;
    unsigned char*p;int w,h;io.Fonts->GetTexDataAsRGBA32(&p,&w,&h);bool passed=true;
    for(float scale:{1.f,1.25f,2.f})for(ImVec2 size:{ImVec2(800,600),ImVec2(80,45)})
        for(ImVec2 mouse:{ImVec2(5,5),ImVec2(size.x/2,size.y/2),ImVec2(size.x-10,size.y-10)}) {
            const auto reference=Draw({0,0},size,mouse,scale,false);
            for(ImVec2 origin:{ImVec2(0,0),ImVec2(1000,100),ImVec2(-1600,100),ImVec2(137,-900)}) {
                if(!Same(reference,Draw(origin,size,mouse,scale,true))) {
                    std::cerr<<"Cursor overlay translation changed at "<<origin.x<<","<<origin.y<<" size="<<size.x<<","<<size.y<<" scale="<<scale<<'\n';passed=false;
                }
            }
        }
    // Origin-zero placement is unchanged: normal offsets/right-edge flip and
    // existing tiny-window overflow policy are deliberately not redesigned.
    const auto middle=Draw({0,0},{800,600},{400,300},1,false);
    passed=passed&&Near(middle.magnifier.x,420)&&Near(middle.magnifier.y,320);
    const auto edge=Draw({0,0},{800,600},{790,590},1,false);
    passed=passed&&Near(edge.magnifier.x,638)&&Near(edge.magnifier.y,438);
    ImGui::DestroyContext();if(!passed)return 1;
    std::cout<<"Viewport cursor readout/magnifier translation, edge and HiDPI geometry checks passed.\n";
}
