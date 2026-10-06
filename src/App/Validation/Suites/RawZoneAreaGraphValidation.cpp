#include "Editor/Internal/RawLab/RawLabAreaGraph.h"
#include <imgui.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
bool ValidateRawZoneAreaGraph() {
    auto* previous=ImGui::GetCurrentContext();
    auto* context=ImGui::CreateContext();
    bool ok=false;
    try {
        auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.DisplaySize=ImVec2(400,360); io.DeltaTime=1.0f/60;
        unsigned char* pixels=nullptr; int width=0,height=0; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        RawRecipe::RawZoneArea area; area.id="graph-fixture";
        area.points={{-8,-1},{6,2}};
        area.points[0].outgoing.manual=true; area.points[0].outgoing.offsetX=3; area.points[0].outgoing.offsetY=1;
        EditorModuleTypes::RawZoneAreaGraphState state;
        RawRecipe::RawZoneAreaStatistics stats; stats.areaId=area.id; stats.valid=true; stats.fullResolution=true;
        stats.minimumEv=-1; stats.maximumEv=1; stats.maskFingerprint=RawRecipe::ZoneAreaMaskFingerprint(area);
        ImVec2 origin;
        auto frame=[&](bool gesture=false) {
            ImGui::NewFrame(); ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImVec2(400,360));
            ImGui::Begin("Area graph fixture",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize);
            origin=ImGui::GetCursorScreenPos();
            const bool changed=Editor::RawLabInternal::DrawRawLabAreaGraph(area,state,&stats,gesture);
            ImGui::End(); ImGui::Render(); return changed;
        };
        auto require=[](bool value,const char* message){if(!value) throw std::runtime_error(message);};
        const auto original=RawRecipe::SerializeZoneAreas({area});
        frame(); frame();
        require(state.minimumEv==-1 && state.maximumEv==1,"Graph did not fit mask range");
        require(RawRecipe::SerializeZoneAreas({area})==original,"Fitting rewrote authored points or handles");
        stats.minimumEv=-12;stats.maximumEv=12;
        frame(true);
        require(state.minimumEv==-1 && state.maximumEv==1,"Graph refit during image gain drag");
        frame();
        require(state.minimumEv==-12 && state.maximumEv==12,"Completed mask range was not applied");
        require(RawRecipe::SerializeZoneAreas({area})==original,"Expanded fit changed physical curve coordinates");
        stats.minimumEv=-1;stats.maximumEv=1;frame();
        // Right click adds a point inside a fit whose original endpoints are off screen.
        const ImVec2 point(origin.x+34+(400-16-40)*.5f,origin.y+12+(230-40)*.25f);
        io.AddMousePosEvent(point.x,point.y);frame();io.AddMouseButtonEvent(1,true);frame();
        io.AddMouseButtonEvent(1,false);frame();
        require(area.points.size()==3,"Right click did not add a point in the fitted graph");
        require(area.points.front().ev==-8 && area.points.back().ev==6,"Adding a point moved off-screen endpoints");
        const float oldEv=area.points[1].ev,oldGain=area.points[1].deltaEv;
        io.AddMouseButtonEvent(0,true);frame();io.AddMousePosEvent(point.x,point.y-12);frame();
        io.AddMouseButtonEvent(0,false);frame();
        require(std::abs(area.points[1].ev-oldEv)<.001f && area.points[1].deltaEv>oldGain,"Vertical point drag did not change only gain");
        require(area.points.front().ev==-8 && area.points.back().ev==6,"Dragging a point moved off-screen endpoints");
        stats.maximumEv=stats.minimumEv=2;frame();
        require(state.maximumEv-state.minimumEv>=.099f,"Constant brightness graph has a degenerate axis");
        stats.maskFingerprint++;stats.minimumEv=-20;stats.maximumEv=20;frame();
        require(state.minimumEv>1,"Stale mask analysis changed graph framing");
        ok=true;
    } catch(const std::exception& error) {std::cerr<<"Zones graph validation failed: "<<error.what()<<'\n';}
    ImGui::DestroyContext(context);ImGui::SetCurrentContext(previous);
    if(ok) std::cout<<"Zones graph interaction validation passed.\n";
    return ok;
}
}
