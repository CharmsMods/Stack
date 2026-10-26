#include "Editor/Internal/RawLab/CurveModeSelector.h"
#include "Editor/GraphUI/GraphPlotStyle.h"
#include "Utils/GlassControls.h"
#include "App/ControlMotionAdapter.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
bool DrawCurveControlGlyph(int index,bool active,const char* tip,const GraphUI::GraphPlotStyle& style) {
    ImGui::PushID(index);
    const bool hit=ImGui::InvisibleButton("mode",{style.controlHeight,style.controlHeight},
        index<3?ImGuiButtonFlags_EnableNav:ImGuiButtonFlags_None);
    const auto r=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
    const auto c=r.GetCenter(); auto* d=ImGui::GetWindowDrawList();
    const auto color=ImGui::GetColorU32(ImGuiCol_Text,active?1.f:.55f);
    const auto p=[&](float x,float y){return ImVec2(c.x+x*style.scale,c.y+y*style.scale);};
    if((index>=3 || !style.liquidPoints) && (active || ImGui::IsItemHovered())) d->AddCircle(c,10*style.scale,ImGui::GetColorU32(ImGuiCol_Text,active?.45f:.2f),0,style.scale);
    switch(index) {
    case 0:d->AddLine(p(-6,5),p(6,-5),color,style.scale);d->AddCircle(c,3*style.scale,color,0,style.scale);break;
    case 1:for(float x:{-4.f,4.f}) for(float y:{-4.f,4.f})d->AddCircle(p(x,y),2*style.scale,color,0,style.scale);break;
    case 2:d->AddBezierCubic(p(-7,4),p(-4,-8),p(4,-8),p(7,4),color,1.5f*style.scale);break;
    case 3:d->AddRect(p(-5,-1),p(5,6),color,2*style.scale,0,style.scale);d->AddCircle(p(0,-3),4*style.scale,color,0,style.scale);break;
    case 4:for(float y:{-5.f,0.f,5.f}) {d->AddLine(p(-7,y),p(7,y),color,style.scale);const float x=y==0?3.f:-3.f;d->AddCircleFilled(p(x,y),2.f*style.scale,color);}break;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s",tip);
    ImGui::PopID();return hit;
}
bool DrawCurveModeSelector(int& selected,CurveModeSelectorState& state,const GraphUI::GraphPlotStyle& style) {
    const int before=selected,frame=ImGui::GetFrameCount();
    if(state.lastFrame!=frame-1){state.capture=0;state.dragging=false;}
    state.lastFrame=frame;
    const auto origin=ImGui::GetCursorScreenPos();
    const float step=style.controlHeight+style.controlGap,radius=style.controlHeight*.56f;
    const ImVec2 first(origin.x+style.controlHeight*.5f,origin.y+style.controlHeight*.5f);
    const auto identity=ImGui::GetID("mode-selection");
    auto* draw=ImGui::GetWindowDrawList();
    auto& io=ImGui::GetIO();
    if(style.liquidPoints) {
        ImGuiExtras::GlassBackgroundScope underlay(*draw);
        draw->AddRectFilled(origin,{origin.x+2*step+style.controlHeight,origin.y+style.controlHeight},
            ImGui::GetColorU32(ImGuiCol_Text,.045f),style.controlHeight*.5f);
    }
    const char* tips[]={"Edit points. Double-click to add; right-click a point to remove.",
        "Select points with clicks; drag empty space to move the group.",
        "Edit a soft brightness band; wheel changes its width."};
    Stack::Animation::ControlInput native;
    bool hovered=false,focused=false,held=false;
    int clicked=-1;
    for(int i=0;i<3;++i) {
        if(i)ImGui::SameLine(0,style.controlGap);
        if(DrawCurveControlGlyph(i,selected==i,tips[i],style))clicked=i;
        const auto item=Stack::UiMotion::LastItemInput(true);
        if(i==0)native=item;
        hovered|=item.hovered;focused|=item.focused;held|=ImGui::IsItemActive();
        if(item.enabled && ImGui::IsItemActivated() && GImGui->ActiveIdSource==ImGuiInputSource_Mouse) {
            state.capture=ImGuiID(item.identity);state.initial=selected;
            state.position=float(i);state.dragging=false;
            const float pointer=(io.MousePos.x-first.x)/step;
            const float displayed=ImGuiExtras::GlassSelectionPosition(identity,float(selected));
            const float anchor=std::abs(pointer-displayed)*step<=radius*1.3f?displayed:float(i);
            state.grabOffset=pointer-anchor;
        }
    }
    bool cancelled=false;
    if(state.capture) {
        cancelled=!native.enabled || io.AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape,false) ||
            (io.MouseDown[0] && ImGui::GetActiveID()!=state.capture);
        if(!cancelled && (io.MouseDown[0] || ImGui::IsMouseReleased(0))) {
            // Include the final pointer-up position, including a complete swipe
            // arriving between two frames. The native mode still commits below.
            // The pinned ImGui tracks maximum drag distance only while down.
            // Include this position explicitly when movement and release share
            // one input frame, without changing its button/capture ownership.
            const float dx=io.MousePos.x-io.MouseClickedPos[0].x,dy=io.MousePos.y-io.MouseClickedPos[0].y;
            state.dragging|=ImGui::IsMouseDragPastThreshold(0) ||
                (ImGui::IsMousePosValid() && dx*dx+dy*dy>=io.MouseDragThreshold*io.MouseDragThreshold);
            if(state.dragging)state.position=std::clamp((io.MousePos.x-first.x)/step-state.grabOffset,0.f,2.f);
        }
        if(cancelled) {
            selected=state.initial;
            if(ImGui::GetActiveID()==state.capture)ImGui::ClearActiveID();
            state.capture=0;state.dragging=false;clicked=-1;
        } else if(ImGui::IsMouseReleased(0)) {
            if(state.dragging)selected=std::clamp(int(std::lround(state.position)),0,2);
            else if(clicked>=0)selected=clicked;
            state.capture=0;state.dragging=false;
        } else if(!io.MouseDown[0]) {state.capture=0;state.dragging=false;}
    } else if(clicked>=0 && !cancelled)selected=clicked;
    native.hovered=hovered;native.focused=focused;native.held=held || (state.capture && io.MouseDown[0]);
    ImGuiExtras::GlassSelectionInteraction interaction{native,
        state.capture?state.position:float(selected),state.capture && state.dragging,cancelled};
    if(style.liquidPoints && !ImGuiExtras::DrawGlassSelection(*draw,first,step,selected,radius,identity,&interaction)) {
        ImGuiExtras::GlassBackgroundScope underlay(*draw);
        draw->AddCircle({first.x+interaction.position*step,first.y},radius,
            ImGui::GetColorU32(ImGuiCol_Text,.45f),32,style.scale);
    }
    return selected!=before;
}
}
