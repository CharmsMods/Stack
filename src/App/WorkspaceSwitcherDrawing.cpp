#include "WorkspaceSwitcherDrawing.h"
#include "imgui_internal.h"
#include <cmath>
namespace Stack::Workspace {
ImDrawList* DrawSwitcher(const Switcher& s, ImGuiViewport* v) {
    if (!s.Visible()) return nullptr;
    ImGui::SetNextWindowPos(v->Pos); ImGui::SetNextWindowSize(v->Size);
    ImGui::SetNextWindowViewport(v->ID);
    ImGui::Begin("##WorkspaceSwitcherOverlay",nullptr,ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoDocking);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    auto* dl=ImGui::GetWindowDrawList();
    const ImVec2 center(v->Pos.x+v->Size.x*.5f,v->Pos.y+v->Size.y*.5f);
    const float a=s.Amount(), scale=s.scale, radius=s.config.ringRadius*scale;
    auto color=[&](float r,float g,float b,float alpha) { return ImGui::ColorConvertFloat4ToU32(ImVec4(r,g,b,alpha*a)); };
    auto ring=[&](ImVec2 origin, ImU32 tint, float thickness) {
        for(int i=0;i<96;++i) {
            const float angle=i*6.2831853f/96.f;
            const float y=std::sin(angle)*radius;
            dl->PathLineTo(ImVec2(origin.x+std::cos(angle)*radius-y*s.x/125.f*.035f,
                origin.y+y/(1.f+std::abs(s.y/125.f)*.025f)));
        }
        dl->PathStroke(tint,ImDrawFlags_Closed,thickness);
    };
    for(int i=12;i>=1;--i) ring(center,color(.36f,.58f,1.f,.018f),i*2.f*scale);
    dl->AddCircleFilled(center,radius,color(.07f,.10f,.17f,.28f),96);
    ring(ImVec2(center.x-1.1f*scale,center.y),color(.9f,.2f,.3f,.25f),scale);
    ring(ImVec2(center.x+1.1f*scale,center.y),color(.15f,.65f,1.f,.4f),scale);
    ring(center,color(.70f,.83f,1.f,.9f),1.5f*scale);
    if(s.selected>=0) {
        const auto& d=Destinations[s.selected]; const float angle=std::atan2(d.y,d.x);
        dl->PathArcTo(center,radius-5*scale,angle-.40f,angle+.40f,24);
        dl->PathStroke(color(.65f,.82f,1.f,.7f),0,4*scale);
    }
    for(int i=0;i<static_cast<int>(Destinations.size());++i) {
        const auto& d=Destinations[i]; const float size=ImGui::GetFontSize()*scale;
        ImVec2 extent=ImGui::GetFont()->CalcTextSizeA(size,FLT_MAX,0,d.label);
        ImVec2 p(center.x+d.x*s.config.labelRadius*scale-extent.x*.5f,center.y+d.y*s.config.labelRadius*scale-extent.y*.5f);
        dl->AddText(ImGui::GetFont(),size,p,color(.88f,.93f,1.f,s.selected==i?1.f:.48f),d.label);
    }
    const ImVec2 dot(center.x+s.x*scale,center.y+s.y*scale);
    for(int i=12;i>=1;--i) dl->AddCircleFilled(dot,(s.config.beadRadius+i*1.5f)*scale,color(.23f,.48f,1.f,.018f),40);
    dl->AddCircleFilled(dot,s.config.beadRadius*scale,color(.30f,.52f,.95f,.98f),40);
    for(int i=8;i>=1;--i) dl->AddCircleFilled(ImVec2(dot.x-2*scale,dot.y-2*scale),i*scale,color(.72f,.87f,1.f,.075f),32);
    dl->AddCircleFilled(ImVec2(dot.x-3*scale,dot.y-4*scale),2.4f*scale,color(.95f,.98f,1.f,.95f),24);
    ImGui::End();
    return dl;
}
}
