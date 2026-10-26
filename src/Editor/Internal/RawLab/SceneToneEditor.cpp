#include "Editor/Internal/RawLab/SceneToneEditor.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Editor::RawLabInternal {
bool DrawSceneToneEditor(RawRecipe::SceneTone& tone, bool contrastView,
    int& selected, int& dragging, float& viewMin, float& viewMax, const RawLabGraphHistogram& histogram, const std::function<bool(const std::string&)>& driven,
    RawLabControlSection section) {
    using namespace RawRecipe;
    const auto slider = [&](const char* id, const char* label, float* value, float minimum, float maximum, const char* format, ImGuiSliderFlags flags = 0) {
        const bool connected = driven && driven(id);
        ImGui::BeginDisabled(connected);
        if (section == RawLabControlSection::Settings) {
            ImGui::TextUnformatted(label);
            ImGui::SetNextItemWidth(-1);
        }
        const std::string widget = section == RawLabControlSection::Settings ? std::string("##") + id : label;
        const bool edited = ImGui::SliderFloat(widget.c_str(), value, minimum, maximum, format, flags);
        ImGui::EndDisabled();
        if (connected && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Driven by a graph input. This is the stored fallback value.");
        return edited;
    };
    bool changed=false;
    if (ShowsRawLabSettings(section)) {
        changed=ImGui::Checkbox("Enabled##SceneTone",&tone.enabled);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .52f);
        changed|=slider("contrast", "Contrast",&tone.contrast,0.1f,3.0f,"%.2fx");
        LabTooltip("Adds contrast around the pivot. The graph includes all custom edits and transition compression.");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .52f);
        changed|=slider("pivot", "Pivot",&tone.pivotEv,-10,6,"%.2f EV");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .52f);
        changed|=slider("width", "Range half-width",&tone.coreHalfWidthEv,0.125f,6,"%.2f EV");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .52f);
        changed|=slider("transition", "Transition width",&tone.transitionEv,0.25f,12,"%.2f EV");
        float protection=tone.outerProtection*100;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * .52f);
        if(slider("protection", "Outer tone protection",&protection,0,100,"%.0f%%")) {
            tone.outerProtection=protection/100; changed=true;
        }
        LabTooltip("Reduce brightness shifts beyond the transition boundaries. At 100%, this adjustment leaves those tones unchanged.");
        const float effective=EffectiveSceneToneContrast(tone);
        if(std::abs(effective-tone.contrast)>0.001f)
            ImGui::TextWrapped("Contrast limited to %.2fx. Widen the transition or reduce protection for more separation.",effective);
    }
    constexpr float minEv=-16,maxEv=16;
    if (ShowsRawLabGraph(section)) {
        if(dragging<0) {
            viewMin=contrastView?0:minEv; viewMax=contrastView?2:maxEv;
            for(int i=0;i<=256;++i) {
                const float e=minEv+(maxEv-minEv)*i/256;
                const float value=contrastView?EvaluateSceneToneContrast(tone,e):EvaluateSceneToneEv(tone,e);
                viewMin=std::min(viewMin,value-(contrastView?0:1));
                viewMax=std::max(viewMax,value+(contrastView?.1f:1));
            }
        }
        const float minY=viewMin,maxY=viewMax;
        const ImVec2 origin=ImGui::GetCursorScreenPos();
        const float width=std::max(160.0f,ImGui::GetContentRegionAvail().x),height=230;
        ImGui::InvisibleButton("##SceneToneGraph",ImVec2(width,height),ImGuiButtonFlags_MouseButtonLeft|ImGuiButtonFlags_MouseButtonRight);
        const bool hovered=ImGui::IsItemHovered();
        const ImRect box(ImVec2(origin.x+30,origin.y+10),ImVec2(origin.x+width-8,origin.y+height-28));
        const auto x=[&](float v){return box.Min.x+(v-minEv)/(maxEv-minEv)*box.GetWidth();};
        const auto y=[&](float v){return box.Max.y-(v-minY)/(maxY-minY)*box.GetHeight();};
        const auto output=[&](float e){return contrastView?EvaluateSceneToneContrast(tone,e):EvaluateSceneToneEv(tone,e);};
        auto* draw=ImGui::GetWindowDrawList();
        const auto text=ImGui::GetColorU32(ImGuiCol_Text),muted=ImGui::GetColorU32(ImGuiCol_TextDisabled);
        draw->AddRect(box.Min,box.Max,ImGui::GetColorU32(ImGuiCol_Border));
        draw->PushClipRect(box.Min,box.Max,true);
        const float outside=tone.coreHalfWidthEv+tone.transitionEv;
        for(float side:{-1.f,1.f}) {
            float a=x(tone.pivotEv+side*tone.coreHalfWidthEv),b=x(tone.pivotEv+side*outside);
            draw->AddRectFilled(ImVec2(std::min(a,b),box.Min.y),ImVec2(std::max(a,b),box.Max.y),ImGui::GetColorU32(ImGuiCol_Text,0.045f));
            draw->AddLine(ImVec2(b,box.Min.y),ImVec2(b,box.Max.y),ImGui::GetColorU32(ImGuiCol_Text,0.2f));
        }
        if(histogram.valid) {
            const float peak=*std::max_element(histogram.luma.begin(),histogram.luma.end());
            for(std::size_t i=0;i<histogram.luma.size();++i) {
                const float px=box.Min.x+box.GetWidth()*i/(histogram.luma.size()-1);
                const float h=peak>0?histogram.luma[i]/peak*box.GetHeight()*0.6f:0;
                draw->AddLine(ImVec2(px,box.Max.y),ImVec2(px,box.Max.y-h),ImGui::GetColorU32(ImGuiCol_Text,0.10f));
            }
        }
        if(contrastView) draw->AddLine(ImVec2(box.Min.x,y(1)),ImVec2(box.Max.x,y(1)),muted);
        else draw->AddLine(ImVec2(x(minEv),y(minEv)),ImVec2(x(maxEv),y(maxEv)),muted);
        ImVec2 previous(x(minEv),y(output(minEv)));
        for(int i=1;i<=256;++i) {
            const float e=minEv+(maxEv-minEv)*i/256;
            const ImVec2 next(x(e),y(output(e)));draw->AddLine(previous,next,text,1.5f);previous=next;
        }
        int nearest=-1;float distance=12*12;
        for(std::size_t i=0;i<tone.points.size();++i) {
            const auto& p=tone.points[i];const ImVec2 pos(x(p.ev),y(output(p.ev)));
            draw->AddCircleFilled(pos,selected==int(i)?5.f:3.5f,text);
            const ImVec2 delta(ImGui::GetIO().MousePos.x-pos.x,ImGui::GetIO().MousePos.y-pos.y);
            const float d=delta.x*delta.x+delta.y*delta.y;
            if(d<distance){distance=d;nearest=int(i);}
        }
        draw->PopClipRect();
        for(float e:{-16.f,-8.f,0.f,8.f,16.f}) {
            char label[16];std::snprintf(label,sizeof(label),"%.0f",e);
            const auto size=ImGui::CalcTextSize(label);
            draw->AddText(ImVec2(x(e)-size.x*.5f,box.Max.y+5),muted,label);
        }
        for(float value : {minY,contrastView?1.f:0.f,maxY}) {
            char label[16];std::snprintf(label,sizeof(label),contrastView?"%.1f":"%.0f",value);
            draw->AddText(ImVec2(origin.x,y(value)-6),muted,label);
        }
        if(hovered&&ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {selected=nearest;dragging=nearest;}
        if(hovered&&nearest>=0&&ImGui::IsMouseClicked(ImGuiMouseButton_Right)&&tone.points.size()>2) {
            tone.points.erase(tone.points.begin()+nearest);selected=dragging=-1;changed=true;
        }
        if(hovered&&nearest<0&&ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)&&tone.points.size()<32) {
            const float e=std::clamp(minEv+(ImGui::GetIO().MousePos.x-box.Min.x)/box.GetWidth()*(maxEv-minEv),minEv,maxEv);
            // Insert on the authored base slope so adding a point alone is neutral.
            auto base=tone;base.contrast=1;base.enabled=true;
            tone.points.push_back({e,EvaluateSceneToneContrast(base,e)});
            tone=SanitizeSceneTone(std::move(tone));changed=true;
        }
        if(dragging>=0&&dragging<int(tone.points.size())&&ImGui::IsMouseDown(ImGuiMouseButton_Left)&&ImGui::IsMouseDragging(ImGuiMouseButton_Left,0)) {
            const float target=maxY-(ImGui::GetIO().MousePos.y-box.Min.y)/box.GetHeight()*(maxY-minY);
            if(contrastView) {
                auto& p=tone.points[dragging];
                const float delta=target-EvaluateSceneToneContrast(tone,p.ev);
                p.contrast=std::clamp(p.contrast+delta,0.05f,8.f);
            } else SetSceneToneOutput(tone,dragging,target);
            changed=true;
        }
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) dragging=-1;
        ImGui::TextDisabled(contrastView?"Input EV / contrast multiplier":"Input EV / output EV");
    }
    if (ShowsRawLabSettings(section)) {
        if(selected>=0&&selected<int(tone.points.size())) {
            auto& point=tone.points[selected];
            float ev=point.ev;
            const auto drag = [&](const char* label, float* value, float speed, float minimum, float maximum, const char* format) {
                if (section == RawLabControlSection::Settings) {
                    ImGui::TextUnformatted(label);
                    ImGui::SetNextItemWidth(-1);
                }
                const std::string widget = section == RawLabControlSection::Settings ? std::string("##") + label : label;
                return ImGui::DragFloat(widget.c_str(), value, speed, minimum, maximum, format);
            };
            if(drag("Point input",&ev,.05f,minEv,maxEv,"%.2f EV")) {
                const float lo=selected? tone.points[selected-1].ev+.05f:minEv;
                const float hi=selected+1<int(tone.points.size())?tone.points[selected+1].ev-.05f:maxEv;
                point.ev=std::clamp(ev,lo,hi);changed=true;
            }
            if(contrastView) changed|=drag("Base contrast",&point.contrast,.01f,.05f,8,"%.2fx");
            else {
                float outputEv=EvaluateSceneToneEv(tone,point.ev);
                if(drag("Point output",&outputEv,.05f,-100,100,"%.2f EV")) {
                    SetSceneToneOutput(tone,selected,outputEv);changed=true;
                }
            }
        }
        if(ImGui::CollapsingHeader("Brightness anchor"))
            changed|=slider("anchor", "Anchor offset",&tone.anchorOffsetEv,-6,6,"%.2f EV");
    }
    LabTooltip("Double-click the graph to add a point. Drag vertically to edit. Right-click a point to remove it.");
    return changed;
}
}
