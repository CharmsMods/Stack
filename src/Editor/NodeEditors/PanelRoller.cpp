#include "Editor/NodeEditors/PanelRoller.h"
#include "Utils/ImGuiExtras.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor {
void PanelRollerPlaceholder(const char* label,float width) {
    const float scale=ImGui::GetFontSize()/13.f,height=24.f*scale;
    width=width>0?width:ImGui::GetContentRegionAvail().x;
    const bool compact=!label || !*label;
    const float readout=compact?std::min(52.f*scale,width*.55f):62.f*scale;
    const float gap=(compact || width<250.f*scale?4.f:8.f)*scale;
    const float grip=std::max(12.f*scale,width-readout-gap*2-(compact?0.f:80.f*scale));
    const auto origin=ImGui::GetCursorScreenPos();
    ImGui::Dummy({width,height});
    if(!ImGui::IsItemVisible())return;
    auto* draw=ImGui::GetWindowDrawList();
    const auto ink=ImGui::GetColorU32(ImGuiCol_Text);
    const float y=origin.y+(height-ImGui::GetFontSize())*.5f;
    if(!compact)ImGui::RenderTextEllipsis(draw,{origin.x,y},{origin.x+width-readout-grip-gap*2,origin.y+height},
        origin.x+width-readout-grip-gap*2,label,nullptr,nullptr);
    const float left=origin.x+width-readout-grip-gap,cy=origin.y+height*.5f;
    draw->AddLine({left+5*scale,cy},{left+grip-5*scale,cy},ImGui::GetColorU32(ImGuiCol_Text,.4f),scale);
    draw->AddCircle({left+grip*.5f,cy},5*scale,ink,24,scale);
    draw->AddText({origin.x+width-readout+(readout-ImGui::CalcTextSize("--").x)*.5f,y},ink,"--");
}
bool PanelRoller(const char* label,const char* id,float& value,float minimum,float maximum,
    float speed,float reset,const char* format,PanelControlState& state,float width) {
    ImGui::PushID(id);
    const auto key=ImGui::GetID("roller");
    auto& gesture=state.rollers[key];
    auto& io=ImGui::GetIO();
    const float scale=ImGui::GetFontSize()/13.f;
    width=width>0 ? width : ImGui::GetContentRegionAvail().x;
    const float height=24.f*scale;
    const bool compact=!label || !*label;
    const float readout=compact?std::min(52.f*scale,width*.55f):62.f*scale;
    const float gap=(compact || width<250.f*scale?4.f:8.f)*scale;
    const float grip=std::max(12.f*scale,width-readout-gap*2-(compact?0.f:80.f*scale));
    const auto origin=ImGui::GetCursorScreenPos();
    const bool enabled=!(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled);
    auto* draw=ImGui::GetWindowDrawList();
    const float labelWidth=std::max(0.f,width-readout-grip-gap*2);
    if(!compact)ImGui::RenderTextEllipsis(draw,{origin.x,origin.y+(height-ImGui::GetFontSize())*.5f},
        {origin.x+labelWidth,origin.y+height},origin.x+labelWidth,label,nullptr,nullptr);
    ImGui::SetCursorScreenPos({origin.x+std::max(0.f,width-readout-grip-gap),origin.y});
    ImGui::InvisibleButton("roller",{grip,height},ImGuiButtonFlags_EnableNav);
    const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
    bool changed=false;
    if(enabled && ImGui::IsItemActivated()) {gesture.before=value;gesture.press=io.MousePos;gesture.dragging=true;}
    if(gesture.dragging && (!enabled || io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false))) {
        changed=value!=gesture.before;value=gesture.before;gesture.dragging=false;
        if(ImGui::GetActiveID()==key)ImGui::ClearActiveID();
    }
    if(enabled && gesture.dragging && (active || ImGui::IsMouseReleased(0)) && ImGui::IsMouseDragging(0,1.f)) {
        const float delta=io.MouseDelta.x/scale*(io.KeyShift?.1f:1.f);
        const float next=std::clamp(value+delta*(maximum-minimum)/std::max(1.f,grip/scale-10.f),minimum,maximum);
        if(next!=value) {value=next;changed=true;gesture.phase=std::fmod(gesture.phase+delta,8.f);}

    }
    if(enabled && hovered && ImGui::IsMouseDoubleClicked(0)) {
        changed|=value!=reset;value=reset;gesture.before=reset;gesture.dragging=false;ImGui::ClearActiveID();
    }
    if(!io.MouseDown[0] || !active)gesture.dragging=false;
    if(enabled && ImGui::IsItemFocused() && !io.WantTextInput) {
        const float delta=(ImGui::IsKeyPressed(ImGuiKey_RightArrow)?1.f:0.f)-(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)?1.f:0.f);
        if(delta) {const float next=std::clamp(value+delta*speed*(io.KeyShift?.1f:1.f),minimum,maximum);changed|=next!=value;value=next;}
    }
    if(ImGui::IsItemVisible()) {
        const ImVec2 low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
        const float cy=(low.y+high.y)*.5f;
        const float left=low.x+5*scale,right=high.x-5*scale;
        const float t=std::clamp((value-minimum)/std::max(.000001f,maximum-minimum),0.f,1.f);
        const float neutral=std::clamp((reset-minimum)/std::max(.000001f,maximum-minimum),0.f,1.f);
        draw->AddLine({left,cy},{right,cy},ImGui::GetColorU32(ImGuiCol_Text,.4f),scale);
        const float tick=left+(right-left)*neutral;
        draw->AddLine({tick,cy-3*scale},{tick,cy+3*scale},ImGui::GetColorU32(ImGuiCol_Text,.3f),scale);
        const ImVec2 center(left+(right-left)*t,cy);
        draw->AddCircleFilled(center,5*scale,ImGui::GetColorU32(ImGuiCol_WindowBg));
        draw->AddCircle(center,5*scale,ImGui::GetColorU32(ImGuiCol_Text,active||hovered?1.f:.8f),24,1.3f*scale);
    }
    if(hovered && enabled)ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    ImGui::SetCursorScreenPos({origin.x+std::max(0.f,width-readout),origin.y});
    char text[64];std::snprintf(text,sizeof(text),format,value);
    const auto transparent=ImVec4(0,0,0,0);
    ImGui::PushStyleColor(ImGuiCol_Button,transparent);
    char button[80];std::snprintf(button,sizeof(button),"%s###value",text);
    if(ImGui::Button(button,{readout,height})) {gesture.exact=value;ImGui::OpenPopup("exact");}
    ImGui::PopStyleColor();
    if(ImGui::BeginPopup("exact")) {
        if(!compact)ImGui::TextUnformatted(label);
        if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(150.f*scale);
        const bool enter=ImGui::InputFloat("##number",&gesture.exact,0,0,"%.4f",ImGuiInputTextFlags_EnterReturnsTrue);
        const bool apply=ImGui::Button("Apply");
        if((enter||apply) && std::isfinite(gesture.exact)) {
            const float next=std::clamp(gesture.exact,minimum,maximum);changed|=next!=value;value=next;ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos({origin.x,origin.y+height+ImGui::GetStyle().ItemSpacing.y});
    ImGui::PopID();
    return changed;
}
}
