#include "Editor/NodeEditors/LightBandEditor.h"
#include "Editor/NodeEditors/RawWorkspaceEditContext.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Renderer/RenderPipeline.h"
#include <imgui_internal.h>
#include <algorithm>

namespace Stack::Editor {
bool DrawLightRangeSettings(RawWorkspaceEditContext& c,RawRecipe::LightRange fallback,float measuredExposure) {
    if(!c.nodeTarget || !c.editorState)return false;
    auto fit=c.inputAnalysis ? c.inputAnalysis->lightRange : fallback;
    if(fit.valid) {
        const float shift=c.recipe.LocalEvInputExposureEv()-(c.inputAnalysis ? c.inputAnalysis->inputExposureEv : measuredExposure);
        fit.lowEv+=shift;fit.highEv+=shift;
    }
    if(fit.valid)ImGui::TextDisabled("Image range: %.2f to %.2f EV",fit.lowEv,fit.highEv);
    else ImGui::TextDisabled("Measuring incoming image...");
    return false;
}
bool DrawLightBandGraph(RawWorkspaceEditContext& c,RawRecipe::LightRange range,ImVec2 size) {
    auto& state=c.State<LightBandViewState>();
    auto& local=c.recipe.localRange;
    bool changed=false;
    const bool inputEnabled=!(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled);
    if(state.dragging>=0) {
        if(ImGui::GetIO().AppFocusLost || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            changed=local.light.amounts[state.dragging]!=state.before || local.enabled!=state.enabledBefore;
            local.light.amounts[state.dragging]=state.before;
            local.enabled=state.enabledBefore;
            if(ImGui::GetActiveID()==state.activeId)ImGui::ClearActiveID();
            state.EndInteraction();
        } else if(!inputEnabled || ImGui::GetActiveID()!=state.activeId) {
            if(ImGui::GetActiveID()==state.activeId)ImGui::ClearActiveID();
            state.EndInteraction();
        }
    }
    if(!local.light.automatic)range={local.light.lowEv,local.light.highEv,true};
    // Accepted analysis can refresh during a gesture; keep its original mapping
    // until release rather than moving the graph under the held handle.
    if(state.dragging>=0)range=state.retainedRange;
    else if(range.valid){state.retainedRange=range;state.retainedInputRevision=c.inputAnalysisRevision;}
    if(!range.valid){ImGui::TextDisabled("Measuring incoming image...");return changed;}
    size.x=std::max(80.f,size.x);size.y=std::max(90.f,size.y);
    const auto origin=ImGui::GetCursorScreenPos();
    const float pad=12.f,h=size.y-28.f,w=size.x-pad*2;
    const auto point=[&](float x,float delta){return ImVec2(origin.x+pad+x*w,origin.y+8+h*(.5f-std::clamp(delta,-8.f,8.f)/16.f));};
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddLine(point(0,0),point(1,0),ImGui::GetColorU32(ImGuiCol_Border));

    const auto sameHandle=[](const auto& a,const auto& b){return a.strength==b.strength && a.manual==b.manual && a.offsetX==b.offsetX && a.offsetY==b.offsetY;};
    const bool samePoints=local.points.size()==state.responsePoints.size() && std::equal(local.points.begin(),local.points.end(),state.responsePoints.begin(),[&](const auto& a,const auto& b){
        return a.ev==b.ev && a.deltaEv==b.deltaEv && sameHandle(a.incoming,b.incoming) && sameHandle(a.outgoing,b.outgoing);
    });
    if(!state.responseValid || !samePoints || local.light.amounts!=state.responseBands.amounts ||
        range.lowEv!=state.responseRange.lowEv || range.highEv!=state.responseRange.highEv ||
        local.minEv!=state.responseLow || local.maxEv!=state.responseHigh) {
        const auto points=RawRecipe::RawLocalRangeBezierPoints(local);
        for(int i=0;i<=128;++i){
            const float x=i/128.f,ev=range.lowEv+x*(range.highEv-range.lowEv);
            state.response[i]=RawRecipe::LightBandDelta(local.light,range,ev)-4.f+8.f*
                RawRecipe::EvaluateRawBezierCurve(points,std::clamp((ev-local.minEv)/std::max(.1f,local.maxEv-local.minEv),0.f,1.f));
        }
        state.responseBands=local.light;state.responseRange=range;state.responsePoints=local.points;
        state.responseLow=local.minEv;state.responseHigh=local.maxEv;state.responseValid=true;
    }
    ImVec2 previous{};
    for(int i=0;i<=128;++i){
        const auto p=point(i/128.f,state.response[i]);
        if(i)draw->AddLine(previous,p,ImGui::GetColorU32(ImGuiCol_Text),1.4f);
        previous=p;
    }
    constexpr const char* labels[]{"Blacks","Shadows","Midtones","Highlights","Whites"};
    for(int i=0;i<5;++i){
        ImGui::PushID(i);
        const float x=i*.25f;
        const auto p=point(x,state.response[i*32]);
        ImGui::SetCursorScreenPos({p.x-9,p.y-9});
        ImGui::InvisibleButton("band",{18,18});
        if(ImGui::IsItemActivated()){state.dragging=i;state.before=local.light.amounts[i];state.enabledBefore=local.enabled;state.activeId=ImGui::GetItemID();}
        if(inputEnabled && state.dragging==i && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const float amount=std::clamp(local.light.amounts[i]-ImGui::GetIO().MouseDelta.y*400.f/h*(ImGui::GetIO().KeyShift?.1f:1.f),-100.f,100.f);
            if(amount!=local.light.amounts[i]){local.light.amounts[i]=amount;local.enabled=true;changed=true;}
        }
        if(inputEnabled && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {local.light.amounts[i]=0;local.enabled=true;changed=true;}
        if(state.dragging==i && !ImGui::IsItemActive())state.EndInteraction();
        draw->AddCircle(p,5.f,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_SliderGrabActive:ImGuiCol_Text),16,1.5f);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("%s  %+.1f\nDrag vertically; Shift for fine adjustment; double-click to reset",labels[i],local.light.amounts[i]);
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
    ImGui::SetCursorScreenPos({origin.x,origin.y+size.y});
    ImGui::TextDisabled("%.2f EV",range.lowEv);ImGui::SameLine();ImGui::TextDisabled("to %.2f EV",range.highEv);
    return changed;
}
}
