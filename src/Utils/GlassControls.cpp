// SPDX-License-Identifier: GPL-3.0-only
#include "Utils/GlassControls.h"
#include "App/ControlMotionAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include "Renderer/Glass/glass_presets.hpp"

namespace ImGuiExtras {
namespace {
thread_local GlassControlState* fallback=nullptr;
thread_local GlassControlScope* active=nullptr; // A restored drawing scope, not retained ownership.
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
void SetGlassFrameState(GlassControlState* state) { fallback=state; }
bool GlassFrameEnabled() { return fallback && fallback->enabled; }
bool DrawGlassSurface(ImDrawList& draw,ImVec2 min,ImVec2 max,ImVec4 tint,float radius,bool attachedLeft,
    Stack::Renderer::GlassChrome::MaterialRole role) {
    auto* state=fallback?fallback:(active?&active->state_:nullptr);
    if (!state || !state->enabled || ImGui::GetWindowViewport()!=ImGui::GetMainViewport()) return false;
    const auto source=state->material.AddBackdrop(&draw,min,max,tint);
    if (!source) return false;
    draw.AddRectFilled(min,max,ImGui::GetColorU32(tint),radius);
    if(attachedLeft)min.x-=2.f*radius;
    state->material.AddObject(&draw,source,min,max,tint,radius,1.f,role,ImGui::GetStyle().Alpha);
    return true;
}
bool GlassEnabledForCurrentWindow() {
    return (HasGlassControlScope() || (fallback && fallback->enabled)) &&
        ImGui::GetCurrentWindow()->Viewport==ImGui::GetMainViewport() && !ImGui::GetCurrentWindow()->SkipItems;
}
AutomaticGlassScope::AutomaticGlassScope() {
    if (active && active->elements_==GlassElements::FloatingTools) return;
    if (HasGlassControlScope()) return;
    bool descendant=false;
    for(auto* window=ImGui::GetCurrentWindow();active && window;window=window->ParentWindow)
        if(window==active->window_) {descendant=true;break;}
    auto* state=descendant?&active->state_:fallback;
    if (state && state->enabled && ImGui::GetCurrentWindow()->Viewport==ImGui::GetMainViewport())
        scope_.emplace(*state,active?active->opacity_:1.f,true);
}
void GlassControlState::BeginFrame(bool requested,const std::string& owner) {
    if (!requested && enabled) { splitter.ClearFreeMemory(); inlineSplitter.ClearFreeMemory(); }
    material.BeginFrame(requested);
    enabled=requested;
    if (!requested || identity!=owner) { controls.clear(); points.clear(); feedback.clear(); identity=owner; }
    if (!requested) errorReported=false;
    // Only prune when admitting a new control would exceed the small panel budget.
    // Hidden controls do not advance; a skipped frame resets them on next use.
}
void GlassControlState::Shutdown() {
    material.Shutdown(); splitter.ClearFreeMemory(); inlineSplitter.ClearFreeMemory(); controls.clear(); points.clear(); feedback.clear(); identity.clear(); enabled=false; errorReported=false;
}
GlassControlScope::GlassControlScope(GlassControlState& state,float opacity,bool inlineDrawing,GlassElements elements)
    :previous_(active),state_(state),window_(ImGui::GetCurrentWindow()),splitter_(inlineDrawing?state.inlineSplitter:state.splitter),inline_(inlineDrawing),opacity_(opacity),elements_(elements) {
    active=this;
}
GlassControlScope::~GlassControlScope() {
    if (split_) splitter_.Merge(window_->DrawList);
    active=previous_;
}
bool GlassControlScope::Ready() const {
    return state_.enabled && window_==ImGui::GetCurrentWindow() && !window_->SkipItems &&
        window_->Viewport==ImGui::GetMainViewport() && opacity_*ImGui::GetStyle().Alpha>.001f &&
        (inline_ || GImGui->CurrentTable==nullptr);
}
bool HasGlassControlScope() {return active && active->elements_==GlassElements::Controls && active->Ready();}
bool HasGlassPointScope() {return active && active->Ready();}
bool IsGraphPointPresentation() {
    return active && active->elements_==GlassElements::FloatingTools && active->window_==ImGui::GetCurrentWindow() &&
        active->window_->Viewport==ImGui::GetMainViewport();
}
bool GlassControlScope::Prepare(ImDrawList& draw,int channel) {
    if (!Ready() || &draw!=window_->DrawList) return false;
    if (!split_) { splitter_.Split(&draw,3); split_=true; }
    splitter_.SetCurrentChannel(&draw,channel);
    return true;
}
bool GlassControlScope::PrepareForeground(ImDrawList& draw) { return Prepare(draw,2); }
bool GlassControlScope::Capture(const ImRect* bounds) {
    auto* draw=window_->DrawList;
    Prepare(*draw,1);
    if (!captured_) {
        // The first object may be inside a plot clip. Capture the approved
        // surface once, so later buttons and sliders share the same source.
        draw->PushClipRect(window_->ClipRect.Min,window_->ClipRect.Max,false);
        ImRect region=window_->ClipRect;
        if (inline_ && bounds) { region=*bounds; region.Expand(.75f*std::min(bounds->GetWidth(),bounds->GetHeight())); }
        backdrop_=state_.material.AddBackdrop(draw,region.Min,region.Max,
            ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));
        draw->PopClipRect();
        captured_=bool(backdrop_);
    }
    return captured_;
}
GlassBackgroundScope::GlassBackgroundScope(ImDrawList& draw):draw_(draw) {
    if (active && active->split_) previousChannel_=active->splitter_._Current;
    if (active && active->Prepare(draw,0)) scope_=active;
}
GlassBackgroundScope::~GlassBackgroundScope() { End(); }
void GlassBackgroundScope::End() {
    if (scope_) scope_->Prepare(draw_,previousChannel_);
    scope_=nullptr;
}
GlassForegroundInkScope::GlassForegroundInkScope() {
    if (!HasGlassControlScope() || !active->captured_) return;
    draw_=ImGui::GetWindowDrawList();
    const auto& rect=GImGui->LastItemData.Rect;
    if (active->state_.material.BeginInk(draw_,active->backdrop_,rect.Min,rect.Max,ImGui::GetItemID()))
        material_=&active->state_.material;
}
GlassForegroundInkScope::~GlassForegroundInkScope() { if (material_) material_->EndInk(draw_); }
bool GlassControlScope::DrawControl(const ImRect& rect,Stack::Animation::ControlInput input,float radius) {
    const ImRect clip(window_->DrawList->GetClipRectMin(),window_->DrawList->GetClipRectMax());
    if (!HasGlassControlScope() || !input.identity || !rect.Overlaps(clip)) return false;
    if (state_.feedback.find(input.identity)==state_.feedback.end() && state_.feedback.size()>=256) {
        for (auto it=state_.feedback.begin();it!=state_.feedback.end();)
            if (it->second.lastFrame<int(input.frame)-1) it=state_.feedback.erase(it); else ++it;
        if (state_.feedback.size()>=256) return false;
    }
    auto& feedback=state_.feedback[input.identity];
    input.visible=true;
    const auto response=feedback.motion.Update(input);
    feedback.lastFrame=int(input.frame);
    auto* draw=window_->DrawList;
    Capture(&rect);
    ImVec4 backing=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); backing.w=1.f;
    draw->AddRectFilled(rect.Min,rect.Max,ImGui::GetColorU32(backing),radius);
    if (captured_) state_.material.AddObject(draw,backdrop_,rect.Min,rect.Max,
        ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),radius,response.Elevation(),
        Stack::Renderer::GlassChrome::MaterialRole::ClearControl,opacity_*ImGui::GetStyle().Alpha);
    PrepareForeground(*draw);
    ImVec4 wash=ImGui::GetStyleColorVec4(ImGuiCol_Text);
    wash.w*=.025f+.04f*response.hover+.045f*response.selection;
    draw->AddRectFilled(rect.Min,rect.Max,ImGui::GetColorU32(wash),radius);
    if (response.selection>.001f) {
        const float scale=ImGui::GetFontSize()/16.f;
        wash.w=.7f*response.selection;
        const float center=(rect.Min.x+rect.Max.x)*.5f;
        const float half=std::min(4.f*scale,rect.GetWidth()*.2f);
        draw->AddLine({center-half,rect.Max.y-2.f*scale},{center+half,rect.Max.y-2.f*scale},ImGui::GetColorU32(wash),scale);
    }
    return true;
}
bool DrawGlassLastItem(bool selected,float radius) {
    AutomaticGlassScope automatic;
    if (!HasGlassControlScope()) return false;
    const auto& rect=GImGui->LastItemData.Rect;
    auto input=Stack::UiMotion::LastItemInput(selected);
    return active->DrawControl(rect,input,radius<0.f?rect.GetHeight()*.42f:radius);
}
bool DrawGlassPoint(ImDrawList& draw,ImVec2 point,float radius,ImGuiID identity,bool selected,bool hovered,bool held,GlassMotionRole role,float aspect,const Stack::Animation::ControlInput* nativeInput,const Stack::Renderer::GlassChrome::JoinGroup* join) {
    AutomaticGlassScope automatic;
    if (!HasGlassPointScope() || &draw!=ImGui::GetWindowDrawList()) return false;
    auto input=nativeInput?*nativeInput:Stack::UiMotion::LastItemInput(selected);
    input.identity=identity;input.selected=selected;input.hovered=hovered;input.held=held && input.enabled;
    return active->DrawPoint(point,radius,input,role,aspect,join);
}
bool DrawGlassSelection(ImDrawList& draw,ImVec2 first,float step,int selected,float radius,ImGuiID identity,const GlassSelectionInteraction* interaction) {
    if(!HasGlassPointScope() || &draw!=ImGui::GetWindowDrawList())return false;
    return active->DrawSelection(first,step,selected,radius,identity,interaction);
}
float GlassSelectionPosition(ImGuiID identity,float fallback) {
    return HasGlassPointScope()?active->SelectionPosition(identity,fallback):fallback;
}
float GlassControlScope::SelectionPosition(ImGuiID identity,float fallback) const {
    const auto it=state_.points.find(identity);
    return it!=state_.points.end() && it->second.selection && it->second.frame>=ImGui::GetFrameCount()-1
        ? it->second.selection->sample(ImGui::GetTime()).center.x:fallback;
}
bool GlassControlScope::DrawSelection(ImVec2 first,float step,int selected,float radius,ImGuiID identity,const GlassSelectionInteraction* interaction) {
    if(!Ready())return false;
    if(state_.points.find(identity)==state_.points.end() && state_.points.size()>=128)return false;
    auto& motion=state_.points[identity];
    auto input=interaction?interaction->input:Stack::UiMotion::LastItemInput(true);
    input.identity=identity;
    const double now=ImGui::GetTime(),elapsed=now-motion.time;
    const bool gap=motion.frame!=int(input.frame)-1 || elapsed>.25 || ImGui::GetIO().AppFocusLost;
    const float position=interaction?interaction->position:float(selected);
    namespace ge=stack::glass::engine;
    if(motion.frame!=int(input.frame)) {
        if(!motion.selection || gap || (interaction && interaction->cancelled))motion.selection.emplace();
        const auto velocity=motion.visual && !gap?motion.visual->sample(now).velocity:stack::glass::Vec2{};
        motion.selection->submit(now,{{position,0},{radius,radius},radius},
            interaction && interaction->direct?ge::Placement::Direct:ge::Placement::Spring,
            {step>0?velocity.x/step:0,0},{7,.82},{8,.84},input.reducedMotion);
    }
    const auto shape=motion.selection->sample(now);
    // Decorative pressure also follows a click-triggered placement transition.
    // It never owns the native mode value or pointer capture.
    input.held=input.enabled && ((interaction && interaction->input.held) || !motion.selection->settled(now));
    return DrawPoint({first.x+shape.center.x*step,first.y},shape.halfSize.x,input,GlassMotionRole::Selection);
}
bool GlassControlScope::DrawPoint(ImVec2 point,float radius,Stack::Animation::ControlInput input,GlassMotionRole role,float aspect,const Stack::Renderer::GlassChrome::JoinGroup* join) {
    if(!Ready() || !input.identity)return false;
    auto* draw=window_->DrawList;
    const ImRect clip(draw->GetClipRectMin(),draw->GetClipRectMax());
    const ImRect bounds({point.x-radius*aspect*1.7f,point.y-radius*1.7f},{point.x+radius*aspect*1.7f,point.y+radius*1.7f});
    if(!bounds.Overlaps(clip))return false;
    if(state_.points.find(input.identity)==state_.points.end() && state_.points.size()>=128) {
        for(auto it=state_.points.begin();it!=state_.points.end();)
            if(it->second.frame<int(input.frame)-1)it=state_.points.erase(it);else ++it;
        if(state_.points.size()>=128)return false;
    }
    auto& motion=state_.points[input.identity];
    namespace gm=stack::glass::motion;
    if(motion.frame!=int(input.frame)) {
        const double now=ImGui::GetTime();
        const bool gap=motion.frame!=int(input.frame)-1 || now-motion.time>.25 || ImGui::GetIO().AppFocusLost;
        if(!motion.visual || motion.role!=role || gap) {
            auto tuning=role==GlassMotionRole::GraphPoint?gm::presets::graphPoint():
                role==GlassMotionRole::Slider?gm::presets::clearControl():gm::presets::selection();
            if(role==GlassMotionRole::Selection)tuning.pressGrowth=gm::presets::clearControl().pressGrowth;
            motion.visual.emplace(tuning);motion.role=role;
        }
        gm::MotionInput visualInput;
        visualInput.center={point.x,point.y};visualInput.halfSize={radius*aspect,radius};
        const auto& io=ImGui::GetIO();
        visualInput.hasPointer=ImGui::IsMousePosValid() && (input.hovered || input.held);
        // ImGui uses an out-of-range sentinel before pointer entry or after
        // focus loss. The engine requires finite, bounded coordinates even
        // when there is no pointer, so use the semantic anchor in that case.
        visualInput.pointer=visualInput.hasPointer?stack::glass::Vec2{io.MousePos.x,io.MousePos.y}:
            stack::glass::Vec2{point.x,point.y};
        visualInput.hovered=input.enabled && input.hovered;visualInput.pressed=input.enabled && input.held;
        visualInput.dragging=visualInput.pressed && ImGui::IsMouseDragging(0,0.f);
        visualInput.focused=input.focused;visualInput.enabled=input.enabled;
        visualInput.reduceMotion=input.reducedMotion;
        if(GImGui->ActiveIdSource==ImGuiInputSource_Keyboard)visualInput.modality=gm::InputModality::Keyboard;
        motion.visual->submit(now,visualInput);motion.output=motion.visual->sample(now);
        motion.frame=int(input.frame);motion.time=now;
    }
    const auto& visual=motion.output;
    if(!visual.valid)return false;
    // Keep the exact semantic anchor. Growth and full 2D strain are visual only.
    const ImVec2 half(radius*aspect*visual.uniformScale,radius*visual.uniformScale);
    const ImVec2 low(point.x-half.x,point.y-half.y),high(point.x+half.x,point.y+half.y);
    Capture(&bounds);
    const Stack::Renderer::GlassChrome::Interaction response{visual.press,
        {visual.tilt.x,visual.tilt.y},{visual.contactLocal.x,visual.contactLocal.y},
        {visual.strainTensor.x,visual.strainTensor.y},visual.contactGain,visual.elevation};
    const bool drawn=captured_ && state_.material.AddObject(draw,backdrop_,low,high,
        ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),std::min(half.x,half.y),1.f,
        Stack::Renderer::GlassChrome::MaterialRole::ClearControl,opacity_*ImGui::GetStyle().Alpha,&response,
        elements_==GlassElements::FloatingTools && role==GlassMotionRole::GraphPoint?join:nullptr);
    PrepareForeground(*draw);
    return drawn;
}
void GlassControlScope::Draw(const ImRect& frame,const ImRect& grab,double value,double minimum,double maximum) {
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
    const ImVec2 nativeCenter=grab.GetCenter();
    const double now=ImGui::GetTime();
    const bool reset=motion.lastFrame!=current && motion.lastFrame!=current-1;
    const double elapsed=now-motion.previousTime;
    const bool snap=reset || input.reducedMotion || elapsed>.25 || ImGui::GetIO().AppFocusLost;
    const auto snapshot=motion.slider.Update(input,value,minimum,maximum);
    if (motion.lastFrame!=current) {
        const float velocity=(!snap && elapsed>0 && input.held)?float((nativeCenter.x-motion.previousX)/elapsed):0.f;
        motion.activation.Retarget(input.held?1.f:0.f,Stack::Animation::MotionSpec::Spring(input.held?12.f:8.f,input.held?.9f:.85f));
        motion.strain.Retarget(input.held?.035f*std::tanh(std::abs(velocity)/240.f):0.f,Stack::Animation::MotionSpec::Spring(12.f,1.f));
        motion.tilt.Retarget(input.held?-.035f*std::tanh(velocity/240.f):0.f,Stack::Animation::MotionSpec::Spring(9.f,.95f));
        motion.contact.Retarget(input.held?1.f:0.f,Stack::Animation::MotionSpec::Spring(input.held?16.f:7.f,1.f));
        for (auto* channel:{&motion.activation,&motion.strain,&motion.tilt,&motion.contact}) {
            if (snap) channel->Snap(input.reducedMotion?0.f:channel->Target());
            else if(channel->IsActive()) channel->Advance(elapsed);
        }
        motion.previousX=nativeCenter.x;motion.previousTime=now;
    }
    motion.held=input.held; motion.lastFrame=current;
    const float scale=ImGui::GetFontSize()/16.f;
    const float strain=std::clamp(motion.strain.Value(),-.035f,.035f);
    const float height=std::max(4.f,std::min(frame.GetHeight()-4.f,20.f*scale))*std::exp(-strain);
    const float width=std::min(frame.GetWidth()-4.f,height*stack::glass::presets::clearControlV2AspectRatio*std::exp(2.f*strain));
    const ImVec2 center=grab.GetCenter();
    // Clamp decorative width at endpoints without moving the native grab center.
    const float halfWidth=std::max(1.f,std::min({width*.5f,center.x-frame.Min.x,frame.Max.x-center.x}));
    const ImVec2 low(center.x-halfWidth,center.y-height*.5f),high(center.x+halfWidth,center.y+height*.5f);
    Prepare(*draw,0);
    const ImU32 track=ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const float line=std::max(1.f,2.f*scale);
    draw->AddLine({frame.Min.x+2.f,center.y},{frame.Max.x-2.f,center.y},track,line);
    const ImRect opticalBounds(low,high);
    Capture(&opticalBounds);
    ImVec4 backing=ImGui::GetStyleColorVec4(ImGuiCol_FrameBg); backing.w=1.f;
    draw->AddRectFilled(low,high,ImGui::GetColorU32(backing),height*.5f);
    const Stack::Renderer::GlassChrome::Interaction interaction{motion.contact.Value(),{motion.tilt.Value(),0},{0,0}};
    if (captured_) state_.material.AddObject(draw,backdrop_,low,high,ImGui::GetStyleColorVec4(ImGuiCol_WindowBg),
        height*.5f,snapshot.control.Elevation(),Stack::Renderer::GlassChrome::MaterialRole::ClearControl,
        opacity_*ImGui::GetStyle().Alpha,&interaction);
    PrepareForeground(*draw);
    // A small, neutral foreground response carries disabled/hover alpha even
    // when the GPU effect is unavailable. It never changes the hit rectangle.
    ImVec4 ink=ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ink.w*=.04f+.05f*snapshot.control.hover;
    draw->AddRectFilled(low,high,ImGui::GetColorU32(ink),height*.5f);
}

bool GlassSliderScalar(const char* label,ImGuiDataType type,void* value,
    const void* minimum,const void* maximum,const char* format,ImGuiSliderFlags flags,const SliderSemanticEdit* semanticEdit) {
    AutomaticGlassScope automatic;
    if (!HasGlassControlScope() || (flags&ImGuiSliderFlags_Vertical) ||
        (type!=ImGuiDataType_Float && type!=ImGuiDataType_Double && type!=ImGuiDataType_S32 && type!=ImGuiDataType_S64))
    {
        const bool changed=ImGui::SliderScalar(label,type,value,minimum,maximum,format,flags);
        return (semanticEdit && semanticEdit->apply ? semanticEdit->apply(semanticEdit->user):false) || changed;
    }
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
    const bool semanticChanged=semanticEdit && semanticEdit->apply && semanticEdit->apply(semanticEdit->user);
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
    const bool changed=ImGui::SliderBehavior(frame,id,type,value,minimum,maximum,format,flags,&grab) || semanticChanged;
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
bool GlassSliderFloat(const char* label,float* value,float minimum,float maximum,const char* format,ImGuiSliderFlags flags) {
    return GlassSliderScalar(label,ImGuiDataType_Float,value,&minimum,&maximum,format,flags);
}
bool GlassSliderInt(const char* label,int* value,int minimum,int maximum,const char* format,ImGuiSliderFlags flags) {
    return GlassSliderScalar(label,ImGuiDataType_S32,value,&minimum,&maximum,format,flags);
}

namespace {
bool Button(const char* label,const ImVec2& size,bool selected,ImGuiButtonFlags flags) {
    active->PrepareForeground(*ImGui::GetWindowDrawList());
    for (const auto color:{ImGuiCol_Button,ImGuiCol_ButtonHovered,ImGuiCol_ButtonActive,ImGuiCol_Text,ImGuiCol_Border})
        ImGui::PushStyleColor(color,ImVec4(0,0,0,0));
    const bool pressed=ImGui::ButtonEx(label,size,flags);
    ImGui::PopStyleColor(5);
    if (ImGui::IsItemVisible()) {
        DrawGlassLastItem(selected);
        GlassForegroundInkScope ink;
        const auto& rect=GImGui->LastItemData.Rect;
        const auto padding=ImGui::GetStyle().FramePadding;
        ImGui::RenderTextClipped({rect.Min.x+padding.x,rect.Min.y+padding.y},
            {rect.Max.x-padding.x,rect.Max.y-padding.y},label,ImGui::FindRenderedTextEnd(label),nullptr,ImGui::GetStyle().ButtonTextAlign,&rect);
    }
    return pressed;
}
void TransparentFrame() {
    for (const auto color:{ImGuiCol_FrameBg,ImGuiCol_FrameBgHovered,ImGuiCol_FrameBgActive,ImGuiCol_Border})
        ImGui::PushStyleColor(color,ImVec4(0,0,0,0));
}
void Indicator(bool selected) {
    if (!ImGui::IsItemVisible()) return;
    const auto& rect=GImGui->LastItemData.Rect;
    const float size=ImGui::GetFrameHeight();
    active->DrawControl({rect.Min,{rect.Min.x+size,rect.Min.y+size}},Stack::UiMotion::LastItemInput(selected),size*.5f);
}
}
bool GlassButton(const char* label,const ImVec2& size,bool selected) {
    AutomaticGlassScope automatic;
    return HasGlassControlScope()?Button(label,size,selected,0):ImGui::Button(label,size);
}
bool GlassSmallButton(const char* label) {
    AutomaticGlassScope automatic;
    if (!HasGlassControlScope()) return ImGui::SmallButton(label);
    const auto padding=ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(padding.x,0));
    const bool pressed=Button(label,{0,0},false,ImGuiButtonFlags_AlignTextBaseLine);
    ImGui::PopStyleVar(); return pressed;
}
bool GlassCheckbox(const char* label,bool* value) {
    AutomaticGlassScope automatic;
    if (!HasGlassControlScope()) return ImGui::Checkbox(label,value);
    active->PrepareForeground(*ImGui::GetWindowDrawList());
    TransparentFrame(); const bool changed=ImGui::Checkbox(label,value); ImGui::PopStyleColor(4);
    Indicator(*value); return changed;
}
bool GlassRadioButton(const char* label,bool selected) {
    AutomaticGlassScope automatic;
    if (!HasGlassControlScope()) return ImGui::RadioButton(label,selected);
    active->PrepareForeground(*ImGui::GetWindowDrawList());
    TransparentFrame(); const bool pressed=ImGui::RadioButton(label,selected); ImGui::PopStyleColor(4);
    Indicator(selected); return pressed;
}
bool GlassRadioButton(const char* label,int* value,int buttonValue) {
    AutomaticGlassScope automatic;
    if (!HasGlassControlScope()) return ImGui::RadioButton(label,value,buttonValue);
    const bool pressed=GlassRadioButton(label,*value==buttonValue);
    if (pressed) *value=buttonValue;
    return pressed;
}
}
