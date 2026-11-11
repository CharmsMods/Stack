#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Raw/ColorWarpMath.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
bool DrawColorWarpSideBrightness(RawLabColorSurfaceArgs& args,RawRecipe::RawColorWarpPin& pin,float height) {
    ImGui::BeginGroup();ImGui::TextUnformatted("Light");
    ImGui::BeginDisabled(pin.protectColor || !pin.enabled);
    const float trackHeight=std::max(70.f,height-55.f);
    const auto pos=ImGui::GetCursorScreenPos();
    const auto id=ImGui::GetID("side-brightness");
    ImGui::InvisibleButton("side-brightness",{48,trackHeight});
    auto& io=ImGui::GetIO();auto* storage=ImGui::GetStateStorage();
    bool changed=false;
    if(ImGui::IsItemActivated())storage->SetFloat(id,pin.brightness);
    if(ImGui::IsItemActive()) {
        args.ui.colorWarpInteractionActive=true;
        if(io.MouseDelta.y!=0) {
            const float next=std::clamp(pin.brightness-io.MouseDelta.y*2.f/trackHeight*(io.KeyShift?.1f:1.f),-1.f,1.f);
            changed=next!=pin.brightness;pin.brightness=next;
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false) || io.AppFocusLost) {
            pin.brightness=storage->GetFloat(id);changed=true;ImGui::ClearActiveID();
        }
    }
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)){changed|=pin.brightness!=0;pin.brightness=0;ImGui::ClearActiveID();}
    if(ImGui::IsItemVisible()) {
        auto* draw=ImGui::GetWindowDrawList();const float x=pos.x+24,top=pos.y+6,bottom=pos.y+trackHeight-6;
        const float y=bottom-(pin.brightness+1)*.5f*(bottom-top);
        draw->AddLine({x,top},{x,bottom},ImGui::GetColorU32(ImGuiCol_Text,.45f));
        draw->AddLine({x-3,(top+bottom)*.5f},{x+3,(top+bottom)*.5f},ImGui::GetColorU32(ImGuiCol_Text,.35f));
        draw->AddCircleFilled({x,y},5,ImGui::GetColorU32(ImGuiCol_WindowBg));
        draw->AddCircle({x,y},5,ImGui::GetColorU32(ImGuiCol_Text),24,1.3f);
    }
    float value=pin.brightness*100;ImGui::SetNextItemWidth(58);
    if(ImGui::InputFloat("##brightness-exact",&value,0,0,"%+.1f",ImGuiInputTextFlags_EnterReturnsTrue) && std::isfinite(value)) {
        const float next=std::clamp(value*.01f,-1.f,1.f);changed|=pin.brightness!=next;pin.brightness=next;
    }
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    ImGui::EndDisabled();ImGui::EndGroup();return changed;
}
void DrawColorWarpResultSwatches(RawLabColorSurfaceArgs& args,const RawRecipe::RawColorWarpPin& pin) {
    auto& state=*args.state;
    if(state.swatchRevision!=args.nodeRevision || state.swatchLocalRevision!=state.localRevision ||
        state.swatchSelection!=args.ui.selectedColorWarpPin || state.swatchSpace!=args.workingSpace) {
        RawRecipe::RawColorWarpCoordinate c;c.lightness=.7f;c.a=pin.sourceA*.7f;c.b=pin.sourceB*.7f;
        const RawRecipe::RawColorWarpPopulation* population=nullptr;
        for(const auto& p:pin.population)if(!population || p.mass>population->mass)population=&p;
        if(population){c.a=population->a*c.lightness;c.b=population->b*c.lightness;}
        auto source=RawRecipe::ColorWarpCoordinateToWorkingRgb(c,args.workingSpace);
        if(population) {
            const auto coordinate=RawRecipe::WorkingRgbToColorWarpCoordinate(source,args.workingSpace);
            const float gain=std::exp2(population->sceneEv-coordinate.sceneEv);
            for(auto& channel:source)channel*=gain;
        }
        const auto result=RawRecipe::ApplyPreparedColorWarp(args.colorWarp,source,args.workingSpace);
        const auto display=[&](const std::array<float,3>& rgb) {
            auto mapped=ColorDiscDisplayRgb(rgb,args.workingSpace);
            for(auto& v:mapped){v=std::max(0.f,v);v=v<=.0031308f?12.92f*v:1.055f*std::pow(v,1.f/2.4f)-.055f;}
            return ImVec4(mapped[0],mapped[1],mapped[2],1);
        };
        state.sourceSwatch=display(source);state.resultSwatch=display(result);
        state.swatchRevision=args.nodeRevision;state.swatchLocalRevision=state.localRevision;
        state.swatchSelection=args.ui.selectedColorWarpPin;state.swatchSpace=args.workingSpace;
    }
    ImGui::ColorButton("Source",state.sourceSwatch,ImGuiColorEditFlags_NoTooltip,{28,20});ImGui::SameLine();
    ImGui::TextUnformatted("Source > Result");ImGui::SameLine();
    ImGui::ColorButton("Result",state.resultSwatch,ImGuiColorEditFlags_NoTooltip,{28,20});
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Representative source color through the shared Color Warp transform. Use Preview to inspect spatial influence on the image.");
}
}
