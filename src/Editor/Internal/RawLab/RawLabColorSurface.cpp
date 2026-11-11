#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Raw/ColorWarpMath.h"
#include "Editor/NodeEditors/PanelRoller.h"
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
    if(args.panelControls)args.panelControls->selectedFamily=args.colorWarp.pins[index].family;
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
    if(args.panel)ImGui::TextDisabled("Custom colors");
    for(int row=args.panel?1:0;row<2;++row) {
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
    if(args.panel) {
        if(pin.family<0) {
            changed|=DrawColorWarpSampleScope(args,pin);
            if(BareTextButton("Preview affected area",args.ui.colorWarpDiagnosticLocked))args.ui.colorWarpDiagnosticLocked=!args.ui.colorWarpDiagnosticLocked;
            ImGui::BeginDisabled(!recipe.enabled || !pin.enabled || pin.protectColor);
            changed|=PanelRoller("Hue","custom-hue",pin.hueShift,-180,180,.4f,0,"%+.1f",*args.panelControls);
            float saturation=pin.saturation*100,brightness=pin.brightness*100;
            if(PanelRoller("Saturation","custom-saturation",saturation,-100,500,.4f,0,"%+.1f",*args.panelControls)){pin.saturation=saturation*.01f;changed=true;}
            if(PanelRoller("Brightness","custom-brightness",brightness,-100,100,.4f,0,"%+.1f",*args.panelControls)){pin.brightness=brightness*.01f;changed=true;}
            ImGui::EndDisabled();
            DrawColorWarpResultSwatches(args,pin);
        }
        DrawColorWarpSamplingActions(args);
        if(changed)++state.localRevision;
        return changed;
    }
    ImGui::TextUnformatted(pin.name.c_str());
    ImGui::SameLine();
    if(BypassEyeButton("##selection-enabled",pin.enabled,"Bypass this selection","Enable this selection")) {pin.enabled=!pin.enabled;changed=true;}
    ContinueRawLabControlRow("Reset");
    if(BareTextButton("Reset")) {
        pin.hueShift=pin.saturation=pin.brightness=pin.tintA=pin.tintB=0;
        changed=true;
    }
    LabTooltip("Reset this selection's Hue, Saturation, Brightness, and Tint. Keep its range.");
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
    changed|=DrawColorWarpSampleScope(args,pin);
    if(BareTextButton("Adjust",!state.selectRange))state.selectRange=false;
    ImGui::SameLine();
    if(BareTextButton("Select range",state.selectRange))state.selectRange=true;
    ContinueRawLabControlRow("Preview");
    if(BareTextButton("Preview",args.ui.colorWarpDiagnosticLocked))
        args.ui.colorWarpDiagnosticLocked=!args.ui.colorWarpDiagnosticLocked;
    LabTooltip("Preview this selection after strength, protection and overlap settings. Unadjusted colors remain visible for choosing a range. Hold H for a temporary preview.");
    if(!state.selectRange) {
        ImGui::BeginDisabled(pin.protectColor);
        if(BareTextButton("Move",pin.moveColors)) {pin.moveColors=true;changed=true;}
        LabTooltip("Slide the selected color group toward the wheel destination. Selection stays anchored to the input colors.");
        ImGui::SameLine();
        if(BareTextButton("Turn",!pin.moveColors)) {pin.moveColors=false;changed=true;}
        LabTooltip("Rotate hue and scale saturation around neutral, preserving variation within the group.");
        ImGui::EndDisabled();
    }
    const float size=std::clamp(ImGui::GetContentRegionAvail().y-(state.selectRange?(pin.sampleRadius>0?400.f:300.f):220.f),160.f,
        std::max(160.f,std::min(280.f,ImGui::GetContentRegionAvail().x-72.f)));
    ImGui::BeginGroup();
    if(ImGui::BeginChild("wheel-space",{size,size},ImGuiChildFlags_None,ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse))
        changed|=DrawColorWarpWheel(args,pin,size);
    ImGui::EndChild();ImGui::EndGroup();ImGui::SameLine();
    changed|=DrawColorWarpSideBrightness(args,pin,size);
    DrawColorWarpResultSwatches(args,pin);
    if(!pin.population.empty()) {
        ImGui::TextDisabled("Selected brightness profile");
        const auto origin=ImGui::GetCursorScreenPos();
        const float width=ImGui::GetContentRegionAvail().x;
        ImGui::Dummy({width,48.f});
        if(ImGui::IsItemVisible()) {
            auto* draw=ImGui::GetWindowDrawList();
            float low=32.f,high=-32.f;
            for(const auto& p:pin.population){low=std::min(low,p.sceneEv-pin.brightnessSoftness);high=std::max(high,p.sceneEv+pin.brightnessSoftness);}
            const float span=std::max(1.f,high-low);
            draw->AddLine({origin.x,origin.y+42},{origin.x+width,origin.y+42},ImGui::GetColorU32(ImGuiCol_Text,.25f));
            for(const auto& p:pin.population) {
                const float x=origin.x+width*(p.sceneEv-low)/span;
                const float half=width*pin.brightnessSoftness/span;
                const ImVec2 peak(x,origin.y+42-36*std::max(.15f,p.mass));
                draw->AddLine({std::max(origin.x,x-half),origin.y+42},peak,ImGui::GetColorU32(ImGuiCol_Text,.65f));
                draw->AddLine(peak,{std::min(origin.x+width,x+half),origin.y+42},ImGui::GetColorU32(ImGuiCol_Text,.65f));
            }
        }
    }

    if(state.selectRange) {
        changed|=DrawColorWarpBrightness(args,pin);
        if(pin.family<0) {
            if(BareTextButton("Soft color",pin.sampleRadius>0) && pin.sampleRadius==0) {
                pin.sampleRadius=std::max(.04f,std::hypot(pin.sourceA,pin.sourceB)*.4f);
                pin.hueRange.enabled=pin.saturationRange.enabled=false;
                pin.smoothSelection=.65f;changed=true;
            }
            LabTooltip("Select a soft neighborhood around the sampled color, including subtle near-gray variations. Resets the color range; keeps exposure limits.");
            ImGui::SameLine();
            if(BareTextButton("Hue / saturation",pin.sampleRadius==0) && pin.sampleRadius>0) {
                pin.sampleRadius=0;pin.hueRange.enabled=pin.saturationRange.enabled=true;changed=true;
            }
            if(pin.sampleRadius>0) {
                ImGui::TextUnformatted("Color reach");
                float reach=pin.sampleRadius*100.f;
                if(ImGuiExtras::FloatingSliderFloat("Color reach","##color-reach",&reach,1,100,"%.1f",ImGui::GetContentRegionAvail().x)) {
                    pin.sampleRadius=reach*.01f;changed=true;
                }
                args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
                LabTooltip("Widen the sampled color neighborhood. The outer edge always fades softly.");
            }
        }
        ImGui::TextUnformatted("Smooth selection");
        float smooth=pin.smoothSelection*100.f;
        if(ImGuiExtras::FloatingSliderFloat("Smooth selection","##smooth-selection",&smooth,0,100,"%.0f%%",ImGui::GetContentRegionAvail().x)) {
            pin.smoothSelection=smooth*.01f;changed=true;
        }
        args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
        LabTooltip("Even out selection speckles and small gaps while respecting image edges. Smooths the selection only, not image detail. Higher values can include nearby colors.");
    }
    if(!state.selectRange) {
    ImGui::BeginDisabled(pin.protectColor);
    const float width=ImGui::GetContentRegionAvail().x;
    ImGui::TextUnformatted(pin.moveColors?"Move hue":"Hue");
    changed|=ImGuiExtras::FloatingSliderFloat("Hue","##hue",&pin.hueShift,-180,180,"%+.1f deg",width);
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    ImGui::TextUnformatted(pin.moveColors?"Move saturation":"Saturation");
    float saturation=pin.saturation*100.f;
    if(ImGuiExtras::FloatingSliderFloat("Saturation","##sat",&saturation,-100,500,"%+.1f%%",width)) {
        pin.saturation=saturation*.01f;changed=true;
    }
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    if(pin.family<0) {
        changed|=ImGuiExtras::FloatingSliderFloat("Tint green / red","##tint-a",&pin.tintA,-.2f,.2f,"%+.3f",width);
        args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
        changed|=ImGuiExtras::FloatingSliderFloat("Tint blue / yellow","##tint-b",&pin.tintB,-.2f,.2f,"%+.3f",width);
    }
    args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
    ImGui::EndDisabled();
    }
    if(state.selectRange) {
        if(BareTextButton("Range details",state.details))state.details=!state.details;
        if(state.details)changed|=DrawColorWarpRanges(args,pin);
    }
    DrawColorWarpSamplingActions(args);
    if(changed)++state.localRevision;
    return changed;
}
}
