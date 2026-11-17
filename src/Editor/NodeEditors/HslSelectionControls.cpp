#include "Editor/NodeEditors/HslSelectionControls.h"
#include "Notifications/NotificationPresenter.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <imgui_internal.h>
#include <algorithm>

namespace Stack::Editor {
bool HslSegmented(const char* id,const char* const* labels,int count,int& selected,Animation::ScalarChannel& motion,bool allowOff) {
    ImGui::PushID(id);bool changed=false;
    if(selected>=0)motion.Retarget(float(selected),Animation::MotionSpec::Tween(.18f));
    if(motion.IsActive())motion.Advance(ImGui::GetIO().DeltaTime,Notifications::SystemReducedMotion());
    const auto origin=ImGui::GetCursorScreenPos();const float width=ImGui::GetContentRegionAvail().x,height=ImGui::GetFrameHeight()+4,cell=width/count;
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin,{origin.x+width,origin.y+height},ImGui::GetColorU32(ImGuiCol_FrameBg,.45f),height*.5f);
    if(selected>=0) {
        const float x=origin.x+motion.Value()*cell;
        draw->AddRectFilled({x+2,origin.y+2},{x+cell-2,origin.y+height-2},ImGui::GetColorU32(ImGuiCol_HeaderActive,.8f),height*.5f-2);
    }
    for(int i=0;i<count;++i) {
        if(i)ImGui::SameLine(0,0);ImGui::PushID(i);
        if(ImGui::InvisibleButton(labels[i],{cell,height},ImGuiButtonFlags_EnableNav)){selected=allowOff&&selected==i?-1:i;changed=true;}
        if(ImGui::IsItemVisible()) {
            const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
            if(ImGui::IsItemHovered())draw->AddRectFilled({min.x+2,min.y+2},{max.x-2,max.y-2},ImGui::GetColorU32(ImGuiCol_HeaderHovered,.25f),height*.5f);
            if(ImGui::IsItemFocused())draw->AddRect({min.x+2,min.y+2},{max.x-2,max.y-2},ImGui::GetColorU32(ImGuiCol_NavCursor),height*.5f);
            const auto size=ImGui::CalcTextSize(labels[i]);
            ImGui::RenderTextEllipsis(draw,{std::max(min.x+4,min.x+(cell-size.x)*.5f),min.y+(height-size.y)*.5f},{max.x-4,max.y},max.x-4,labels[i],nullptr,&size);
        }
        ImGui::PopID();
    }
    ImGui::PopID();return changed;
}
bool HslSelectionRow(const RawRecipe::HslSelection& s,bool selected,bool& toggle) {
    const float width=ImGui::GetContentRegionAvail().x,height=ImGui::GetTextLineHeight()*2+12,eye=ImGui::GetFrameHeight();
    const auto origin=ImGui::GetCursorScreenPos();
    const bool clicked=ImGui::InvisibleButton("selection",{std::max(32.f,width-eye-4),height},ImGuiButtonFlags_EnableNav);
    if(ImGui::IsItemVisible()) {
        auto* draw=ImGui::GetWindowDrawList();const auto min=ImGui::GetItemRectMin(),max=ImGui::GetItemRectMax();
        if(selected || ImGui::IsItemHovered())draw->AddRectFilled(min,max,ImGui::GetColorU32(selected?ImGuiCol_Header:ImGuiCol_HeaderHovered,selected?.45f:.2f),8);
        if(ImGui::IsItemFocused())draw->AddRect(min,max,ImGui::GetColorU32(ImGuiCol_NavCursor),8);
        const float textX=min.x+9,line=ImGui::GetTextLineHeight();
        ImGui::RenderTextEllipsis(draw,{textX,min.y+4},{max.x-8,min.y+4+line},max.x-8,s.name.c_str(),nullptr,nullptr);
        static const char* relationships[]={"Following global","Own hue","Own saturation","Own hue + saturation","Own brightness","Own hue + brightness","Own saturation + brightness","Own HSL values"};
        const char* relationship=relationships[(s.own[0]?1:0)+(s.own[1]?2:0)+(s.own[2]?4:0)];
        const float trackWidth=std::min(62.f,(max.x-min.x)*.22f),left=max.x-trackWidth-8,top=min.y+line+10;
        auto ink=ImGui::GetStyleColorVec4(ImGuiCol_Text);ink.w*=.7f;ImGui::PushStyleColor(ImGuiCol_Text,ink);
        ImGui::RenderTextEllipsis(draw,{textX,min.y+line+6},{left-8,max.y},left-8,relationship,nullptr,nullptr);ImGui::PopStyleColor();
        draw->AddRectFilled({left,top},{left+trackWidth,top+5},ImGui::GetColorU32(ImGuiCol_Text,.1f),2);
        if(s.bands.empty())draw->AddRectFilled({left,top},{left+trackWidth,top+5},ImGui::GetColorU32(ImGuiCol_Text,.6f),2);
        else for(const auto& b:s.bands){const float x0=left+trackWidth*std::clamp((b.low+12)/20,0.f,1.f),x1=left+trackWidth*std::clamp((b.high+12)/20,0.f,1.f);if(x1>x0)draw->AddRectFilled({x0,top},{x1,top+5},ImGui::GetColorU32(ImGuiCol_Text,.65f),2);}
    }
    ImGui::SameLine(0,4);ImGui::SetCursorScreenPos({origin.x+width-eye,origin.y+(height-eye)*.5f});
    toggle=RawLabInternal::BypassEyeButton("enable",s.enabled,"Bypass selection","Enable selection");
    ImGui::SetCursorScreenPos({origin.x,origin.y+height+ImGui::GetStyle().ItemSpacing.y});
    return clicked;
}
}
