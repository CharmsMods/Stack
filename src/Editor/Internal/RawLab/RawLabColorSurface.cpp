#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Raw/ColorWarpMath.h"
#include "Utils/FloatingSlider.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
namespace {
ImVec4 Swatch(const RawRecipe::RawColorWarpPin& pin,Raw::RawWorkingSpace space) {
    if(pin.family>=0) {
        static const ImVec4 colors[]={{.95f,.18f,.16f,1},{1,.5f,.12f,1},{.95f,.85f,.15f,1},{.2f,.8f,.3f,1},
            {.1f,.85f,.85f,1},{.2f,.4f,.95f,1},{.6f,.3f,.95f,1},{.95f,.25f,.7f,1}};
        return colors[pin.family];
    }
    RawRecipe::RawColorWarpCoordinate c;
    c.lightness=.7f;c.a=pin.sourceA*.7f;c.b=pin.sourceB*.7f;
    const auto rgb=ColorDiscDisplayRgb(RawRecipe::ColorWarpCoordinateToWorkingRgb(c,space),space);
    const auto encode=[](float x){x=std::max(0.f,x);return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f;};
    return {encode(rgb[0]),encode(rgb[1]),encode(rgb[2]),1};
}
void Select(RawLabColorSurfaceArgs& args,int index) {
    args.ui.selectedColorWarpPin=index;
    args.ui.colorWarpHoveredSelection=-1;
    args.ui.colorWarpInspectedRegionId=args.colorWarp.pins[index].regionId;
    args.ui.colorWarpInspectionActive=false;
    args.state->EndInteraction();
}
}

bool RenderRawLabColorSurface(RawLabColorSurfaceArgs& args) {
    if(!args.state)return false;
    auto& state=*args.state;
    auto& recipe=args.colorWarp;
    bool changed=CommitColorWarpSampling(args);
    args.ui.colorWarpInteractionActive=false;
    if(recipe.pins.empty())return changed; // Valid documents always contain eight families.
    args.ui.selectedColorWarpPin=std::clamp(args.ui.selectedColorWarpPin,0,int(recipe.pins.size())-1);
    const float dot=std::max(18.f,ImGui::GetFontSize()*1.15f);
    for(int row=0;row<2;++row) {
        bool first=true;
        for(int i=0;i<int(recipe.pins.size());++i) {
            auto& pin=recipe.pins[i];
            if((pin.family<0)!=(row==1))continue;
            if(!first)ImGui::SameLine(0,7);first=false;
            ImGui::PushID(pin.id.c_str());
            const auto start=ImGui::GetCursorScreenPos();
            if(ImGui::InvisibleButton("swatch",{dot,dot}))Select(args,i);
            const ImVec2 center(start.x+dot*.5f,start.y+dot*.5f);
            auto* draw=ImGui::GetWindowDrawList();
            auto color=Swatch(pin,args.workingSpace);color.w=pin.enabled?1.f:.35f;
            draw->AddCircleFilled(center,dot*.34f,ImGui::GetColorU32(color));
            if(i==args.ui.selectedColorWarpPin)draw->AddCircle(center,dot*.48f,ImGui::GetColorU32(ImGuiCol_Text),32,1.4f);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",pin.name.c_str());
            ImGui::PopID();
        }
    }
    auto& pin=recipe.pins[args.ui.selectedColorWarpPin];
    ImGui::TextUnformatted(pin.name.c_str());
    ImGui::SameLine();
    if(BypassEyeButton("##selection-enabled",pin.enabled,"Bypass this selection","Enable this selection")) {pin.enabled=!pin.enabled;changed=true;}
    ContinueRawLabControlRow("Reset");
    if(BareTextButton("Reset")) {
        pin.hueShift=pin.saturation=pin.exposureEv=0;
        changed=true;
    }
    LabTooltip("Reset this selection's Hue, Saturation, and Exposure. Keep its range.");
    if(pin.family<0) {
        ContinueRawLabControlRow("Delete");
        if(BareTextButton("Delete")) {
            const auto regionId=pin.regionId;
            recipe.pins.erase(recipe.pins.begin()+args.ui.selectedColorWarpPin);
            const bool used=std::any_of(recipe.pins.begin(),recipe.pins.end(),[&](const auto& p){return p.regionId==regionId;});
            if(!used)recipe.regions.erase(std::remove_if(recipe.regions.begin(),recipe.regions.end(),[&](const auto& r){return r.id==regionId;}),recipe.regions.end());
            Select(args,0);++state.localRevision;return true;
        }
    }
    if(BareTextButton("Adjust",!state.selectRange))state.selectRange=false;
    ImGui::SameLine();
    if(BareTextButton("Select range",state.selectRange))state.selectRange=true;
    ImGui::SameLine();
    if(BareTextButton("Preview",args.ui.colorWarpDiagnosticLocked))
        args.ui.colorWarpDiagnosticLocked=!args.ui.colorWarpDiagnosticLocked;
    LabTooltip("Show where this selection matches the incoming image. Hold H for a temporary preview.");
    const float size=std::clamp(ImGui::GetContentRegionAvail().y-(state.selectRange?320.f:220.f),160.f,
        std::max(160.f,std::min(330.f,ImGui::GetContentRegionAvail().x)));
    changed|=DrawColorWarpWheel(args,pin,size);
    if(state.selectRange)changed|=DrawColorWarpBrightness(args,pin);
    ImGui::BeginDisabled(pin.protectColor);
    const float width=ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted("Hue");
    changed|=ImGuiExtras::FloatingSliderFloat("Hue","##hue",&pin.hueShift,-180,180,"%+.1f deg",width);
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    ImGui::TextUnformatted("Saturation");
    float saturation=pin.saturation*100.f;
    if(ImGuiExtras::FloatingSliderFloat("Saturation","##sat",&saturation,-100,500,"%+.1f%%",width)) {
        pin.saturation=saturation*.01f;changed=true;
    }
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    ImGui::TextUnformatted("Exposure");
    changed|=ImGuiExtras::FloatingSliderFloat("Exposure","##ev",&pin.exposureEv,-4,4,"%+.2f EV",width);
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    ImGui::EndDisabled();
    if(state.selectRange) {
        if(BareTextButton("Range details",state.details))state.details=!state.details;
        if(state.details)changed|=DrawColorWarpRanges(args,pin);
    }
    ImGui::BeginDisabled(RawRecipe::ColorWarpSampleCount(recipe)>=RawRecipe::kMaxRawColorWarpSamples);
    if(BareTextButton("Sample image",args.ui.colorWarpSamplingArmed)) {
        args.ui.colorWarpSamplingArmed=!args.ui.colorWarpSamplingArmed;
        args.ui.colorWarpAttachRegionPinId.clear();
        args.ui.colorWarpInspectionActive=false;
    }
    ImGui::EndDisabled();
    LabTooltip("Click or draw a circle on the image, then add the sampled color. It initially applies throughout the image.");
    if(args.ui.colorWarpPendingCircleActive && args.ui.colorWarpAreaOwner==args.areaOwner) {
        ImGui::SameLine();
        const bool ready=!args.ui.colorWarpPendingCircleRefining &&
            (args.ui.colorWarpPendingCircleAppend || !args.ui.colorWarpAttachRegionPinId.empty() || !args.ui.colorWarpAreaAnalysisResult.proposals.empty());
        ImGui::BeginDisabled(!ready);
        if(BareTextButton("Add selection"))args.ui.colorWarpPendingCircleCommitRequested=true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(BareTextButton("Cancel"))args.ui.ResetColorWarpAreaRequest();
    }
    if(!args.ui.colorWarpAreaStatus.empty())ImGui::TextWrapped("%s",args.ui.colorWarpAreaStatus.c_str());
    if(changed)++state.localRevision;
    return changed;
}
}
