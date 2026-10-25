// SPDX-License-Identifier: GPL-3.0-only
#include "Utils/SidePanelSlider.h"
#include "App/ControlMotionAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ImGuiExtras {
namespace {
thread_local SidePanelSliderScope* active=nullptr; // A restored drawing scope, not retained ownership.
double Number(ImGuiDataType type,const void* value) {
    switch(type) {
    case ImGuiDataType_Float:return *static_cast<const float*>(value);
    case ImGuiDataType_Double:return *static_cast<const double*>(value);
    case ImGuiDataType_S32:return *static_cast<const int*>(value);
    case ImGuiDataType_S64:return static_cast<double>(*static_cast<const ImS64*>(value));
    default:return 0.;
    }
}
}
void SidePanelSliderState::BeginFrame(bool requested,const std::string& owner) {
    if (!requested && enabled) splitter.ClearFreeMemory();
    material.BeginFrame(requested);
    enabled=requested;
    if (!requested || identity!=owner) { controls.clear(); identity=owner; }
    if (!requested) errorReported=false;
    // Only prune when admitting a new control would exceed the small panel budget.
    // Hidden controls do not advance; a skipped frame resets them on next use.
}
void SidePanelSliderState::Shutdown() {
    material.Shutdown(); splitter.ClearFreeMemory(); controls.clear(); identity.clear(); enabled=false; errorReported=false;
}
SidePanelSliderScope::SidePanelSliderScope(SidePanelSliderState& state,float opacity)
    :previous_(active),state_(state),window_(ImGui::GetCurrentWindow()),splitter_(state.splitter),opacity_(opacity) {
    active=this;
}
SidePanelSliderScope::~SidePanelSliderScope() {
    if (split_) splitter_.Merge(window_->DrawList);
    active=previous_;
}
bool HasSidePanelSliderScope() {
    return active && active->state_.enabled && active->window_==ImGui::GetCurrentWindow() &&
        !active->window_->SkipItems && active->window_->Viewport==ImGui::GetMainViewport() &&
        GImGui->CurrentTable==nullptr;
}
void SidePanelSliderScope::Draw(const ImRect& frame,const ImRect& grab,double value,double minimum,double maximum) {
    auto* draw=window_->DrawList;
    auto input=Stack::UiMotion::LastItemInput();
    // A native slider retains pressure during pointer capture, including outside
    // the frame. Its authoritative grab position comes directly from SliderBehavior.
    input.held=ImGui::IsItemActive();
    if (!input.visible || grab.GetWidth()<=0.f) return;
    const auto key=ImGui::GetItemID();
    if (state_.controls.find(key)==state_.controls.end() && state_.controls.size()>=128) {
        const int current=ImGui::GetFrameCount();
        for (auto it=state_.controls.begin();it!=state_.controls.end();)
            if (it->second.lastFrame<current-1) it=state_.controls.erase(it); else ++it;
        if (state_.controls.size()>=128) {
            draw->AddRectFilled(grab.Min,grab.Max,ImGui::GetColorU32(ImGuiCol_SliderGrab),ImGui::GetStyle().GrabRounding);
            return;
        }
    }
    auto& motion=state_.controls[key];
    const int current=ImGui::GetFrameCount();
    if (motion.lastFrame!=current && motion.lastFrame!=current-1) { motion.width.Reset(1.f); motion.held=false; }
    const auto snapshot=motion.slider.Update(input,value,minimum,maximum);
    if (motion.lastFrame!=current) {
        if (input.held) motion.width.Snap(1.16f);
        else if (motion.held) motion.width.SpringTo(1.f,-1.6f,Stack::Animation::MotionSpec::Spring(5.f,.72f));
        if (motion.width.IsActive()) motion.width.Advance(input.deltaSeconds,input.reducedMotion);
    }
    motion.held=input.held; motion.lastFrame=current;
    const float scale=ImGui::GetFontSize()/16.f;
    const float height=std::max(4.f,std::min(frame.GetHeight()-4.f,20.f*scale));
    const float width=std::min(frame.GetWidth()-4.f,height*1.65f*std::clamp(motion.width.Value(),.9f,1.2f));
    const ImVec2 center=grab.GetCenter();
    // Clamp decorative width at endpoints without moving the native grab center.
    const float halfWidth=std::max(1.f,std::min({width*.5f,center.x-frame.Min.x,frame.Max.x-center.x}));
    const ImVec2 low(center.x-halfWidth,center.y-height*.5f),high(center.x+halfWidth,center.y+height*.5f);
    if (!split_) {
        splitter_.Split(draw,3);
        split_=true;
    }
    splitter_.SetCurrentChannel(draw,0);
    const ImU32 track=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float line=std::max(1.f,2.f*scale);
    draw->AddLine({frame.Min.x+2.f,center.y},{frame.Max.x-2.f,center.y},track,line);
    splitter_.SetCurrentChannel(draw,1);
    if (!captured_) {
        // Tracks merge before this callback; all thumbs merge after it, and all
        // labels/values/ordinary controls merge last. No thumb captures a sibling.
        captured_=state_.material.AddBackdrop(draw,draw->GetClipRectMin(),draw->GetClipRectMax(),
            ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
    }
    ImVec4 backing=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); backing.w=1.f;
    draw->AddRectFilled(low,high,ImGui::GetColorU32(backing),height*.5f);
    if (captured_) state_.material.AddObject(draw,low,high,ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),
        height*.5f,snapshot.control.Elevation(),Stack::Renderer::GlassChrome::MaterialRole::SliderThumb,
        opacity_*ImGui::GetStyle().Alpha);
    splitter_.SetCurrentChannel(draw,2);
    // A small, neutral foreground response carries disabled/hover alpha even
    // when the GPU effect is unavailable. It never changes the hit rectangle.
    ImVec4 ink=ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ink.w*=.04f+.05f*snapshot.control.hover;
    draw->AddRectFilled(low,high,ImGui::GetColorU32(ink),height*.5f);
}

bool SidePanelSliderScalar(const char* label,ImGuiDataType type,void* value,
    const void* minimum,const void* maximum,const char* format,ImGuiSliderFlags flags) {
    if (!HasSidePanelSliderScope() || (flags&ImGuiSliderFlags_Vertical) ||
        (type!=ImGuiDataType_Float && type!=ImGuiDataType_Double && type!=ImGuiDataType_S32 && type!=ImGuiDataType_S64))
        return ImGui::SliderScalar(label,type,value,minimum,maximum,format,flags);
    auto* window=ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    auto& g=*GImGui;
    const auto& style=g.Style;
    const ImGuiID id=window->GetID(label);
    const ImU32 colorMarker=(g.NextItemData.HasFlags&ImGuiNextItemDataFlags_HasColorMarker)?g.NextItemData.ColorMarker:0;
    const char* labelEnd=ImGui::FindRenderedTextEnd(label);
    const ImVec2 labelSize=ImGui::CalcTextSize(label,labelEnd,false);
    const ImVec2 pos=window->DC.CursorPos;
    const ImRect frame(pos,{pos.x+ImGui::CalcItemWidth(),pos.y+labelSize.y+style.FramePadding.y*2.f});
    const ImRect total(frame.Min,{frame.Max.x+(labelSize.x>0.f?style.ItemInnerSpacing.x+labelSize.x:0.f),frame.Max.y});
    const bool textAllowed=(flags&ImGuiSliderFlags_NoInput)==0;
    ImGui::ItemSize(total,style.FramePadding.y);
    if (!ImGui::ItemAdd(total,id,&frame,textAllowed?ImGuiItemFlags_Inputable:0)) return false;
    if (!format) format=ImGui::DataTypeGetInfo(type)->PrintFmt;
    // Mirror the pinned native SliderScalar outer contract, reusing its one
    // SliderBehavior call for value mapping, logarithmic ranges and navigation.
    const bool hovered=ImGui::ItemHoverable(frame,id,g.LastItemData.ItemFlags);
    bool textActive=textAllowed && ImGui::TempInputIsActive(id);
    if (!textActive) {
        const bool clicked=hovered && ImGui::IsMouseClicked(0,ImGuiInputFlags_None,id);
        const bool activate=clicked || g.NavActivateId==id;
        if (activate && clicked) ImGui::SetKeyOwner(ImGuiKey_MouseLeft,id);
        if (activate && textAllowed && ((clicked && g.IO.KeyCtrl) ||
            (g.NavActivateId==id && (g.NavActivateFlags&ImGuiActivateFlags_PreferInput)))) textActive=true;
        if (activate) std::memcpy(&g.ActiveIdValueOnActivation,value,ImGui::DataTypeGetInfo(type)->Size);
        if (activate && !textActive) {
            ImGui::SetActiveID(id,window); ImGui::SetFocusID(id,window); ImGui::FocusWindow(window);
            g.ActiveIdUsingNavDirMask|=(1<<ImGuiDir_Left)|(1<<ImGuiDir_Right);
        }
    }
    if (textActive) {
        const bool clamp=(flags&ImGuiSliderFlags_ClampOnInput)!=0;
        return ImGui::TempInputScalar(frame,id,label,type,value,format,clamp?minimum:nullptr,clamp?maximum:nullptr);
    }
    ImRect grab;
    const bool changed=ImGui::SliderBehavior(frame,id,type,value,minimum,maximum,format,flags,&grab);
    if (changed) ImGui::MarkItemEdited(id);
    active->Draw(frame,grab,Number(type,value),Number(type,minimum),Number(type,maximum));
    ImGui::RenderNavCursor(frame,id);
    if (colorMarker && style.ColorMarkerSize>0.f)
        ImGui::RenderColorComponentMarker(frame,ImGui::GetColorU32(colorMarker),style.FrameRounding);
    char text[64];
    const char* end=text+ImGui::DataTypeFormatString(text,sizeof(text),type,value,format);
    if (g.LogEnabled) ImGui::LogSetNextTextDecoration("{","}");
    ImGui::RenderTextClipped(frame.Min,frame.Max,text,end,nullptr,{.5f,.5f});
    if (labelSize.x>0.f) ImGui::RenderText({frame.Max.x+style.ItemInnerSpacing.x,frame.Min.y+style.FramePadding.y},label,labelEnd,false);
    return changed;
}
bool SidePanelSliderFloat(const char* label,float* value,float minimum,float maximum,const char* format,ImGuiSliderFlags flags) {
    return SidePanelSliderScalar(label,ImGuiDataType_Float,value,&minimum,&maximum,format,flags);
}
bool SidePanelSliderInt(const char* label,int* value,int minimum,int maximum,const char* format,ImGuiSliderFlags flags) {
    return SidePanelSliderScalar(label,ImGuiDataType_S32,value,&minimum,&maximum,format,flags);
}
}
