#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/NodeEditors/ColorWarpEditorState.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Raw/ColorWarpMath.h"
#include "Utils/FloatingSlider.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
namespace {
bool SelectionToggle(const char* label,bool& value) {
    if(!BareTextButton(label,value))return false;
    value=!value;return true;
}
bool Setting(RawLabColorSurfaceArgs& args,const char* label,const char* id,float& value,
    float low,float high,float reset,const char* format) {
    if(args.panelControls)return PanelRoller(label,id,value,low,high,(high-low)*.0025f,reset,format,*args.panelControls);
    return ImGuiExtras::FloatingSliderFloat(label,id,&value,low,high,format,ImGui::GetContentRegionAvail().x);
}
}
bool DrawColorWarpSampleScope(RawLabColorSurfaceArgs& args,RawRecipe::RawColorWarpPin& pin) {
    if(pin.family>=0)return false;
    bool changed=false;
    auto region=std::find_if(args.colorWarp.regions.begin(),args.colorWarp.regions.end(),[&](const auto& r){return r.id==pin.regionId;});
    const bool everywhere=region==args.colorWarp.regions.end() || region->spatialMode==RawRecipe::RawColorWarpSpatialMode::AllMatches;
    if(BareTextButton("Everywhere",everywhere) && !everywhere) {
        region->spatialMode=RawRecipe::RawColorWarpSpatialMode::AllMatches;changed=true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(region==args.colorWarp.regions.end());
    if(BareTextButton("This region",!everywhere) && everywhere && region!=args.colorWarp.regions.end()) {
        region->spatialMode=RawRecipe::RawColorWarpSpatialMode::Connected;changed=true;
    }
    ImGui::EndDisabled();
    if(ImGui::TreeNode("Selection details")) {
    if(!pin.population.empty()) {
        changed|=Setting(args,"Brightness softness","##population-softness",pin.brightnessSoftness,.1f,8.f,.75f,"%.2f EV");
        args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
        LabTooltip("Softness around each retained brightness population. Color and brightness are always matched together.");
    }
    if(BareTextButton("Resample color")) {
        args.ui.ResetColorWarpAreaRequest();
        args.ui.colorWarpResamplePinId=pin.id;
        args.ui.colorWarpAttachRegionPinId.clear();
        args.ui.colorWarpSamplingArmed=true;args.ui.colorWarpInspectionActive=false;
    }
    LabTooltip("Draw a new sample and accept a suggested color to replace this selection's profile. Keeps adjustments and spatial restriction.");
    ImGui::TreePop();
    }
    return changed;
}

bool DrawColorWarpRanges(RawLabColorSurfaceArgs& args,RawRecipe::RawColorWarpPin& pin) {
    bool changed=false;
    static const auto familyDefaults=[] {
        std::array<RawRecipe::RawColorWarpPin,8> pins;
        for(int i=0;i<8;++i)pins[i]=RawRecipe::MakeColorWarpFamily(i);
        return pins;
    }();
    static const RawRecipe::RawColorWarpPin sampledDefaults;
    const auto& defaults=pin.family>=0 && pin.family<8?familyDefaults[pin.family]:sampledDefaults;
    const auto range=[&](const char* label,RawRecipe::RawColorWarpRange& r,const RawRecipe::RawColorWarpRange& initial,float lo,float hi,float scale,const char* format) {
        ImGui::PushID(label);
        bool edit=SelectionToggle(label,r.enabled);
        if(r.enabled) {
            float values[]{r.low*scale,r.high*scale,r.featherLow*scale,r.featherHigh*scale};
            const float resets[]{initial.low*scale,initial.high*scale,initial.featherLow*scale,initial.featherHigh*scale};
            const char* names[]{"Minimum","Maximum","Lower soft edge","Upper soft edge"};
            for(int i=0;i<4;++i) {
                if(!args.panelControls)ImGui::TextUnformatted(names[i]);
                if(args.panelControls)edit|=Setting(args,names[i],names[i],values[i],i<2?lo:0,hi,resets[i],format);
                else edit|=ImGuiExtras::FloatingSliderFloat(names[i],names[i],&values[i],i<2?lo:0,hi,format,ImGui::GetContentRegionAvail().x,scale!=100);
                args.ui.colorWarpInteractionActive|=ImGui::IsItemActive();
            }
            if(edit) {
                r.low=values[0]/scale;r.high=std::max(r.low,values[1]/scale);
                r.featherLow=values[2]/scale;r.featherHigh=values[3]/scale;
            }
        }
        ImGui::PopID();return edit;
    };
    changed|=range("Hue range",pin.hueRange,defaults.hueRange,-180,180,1,"%.1f deg");
    LabTooltip("Degrees on either side of the selected source hue. Soft edges fade beyond the full-strength range.");
    changed|=range("Restrict saturation",pin.saturationRange,defaults.saturationRange,0,100,100,"%.1f%%");
    LabTooltip("Exposure-normalized chroma: OKLab chroma divided by lightness. This range stays put when exposure changes.");
    changed|=range("Restrict exposure",pin.exposureRange,defaults.exposureRange,-32,32,1,"%.2f EV");
    LabTooltip("Input exposure relative to 18% gray. This is an additional limit; disabling it keeps the sampled joint profile.");
    if(changed)RawRecipe::SanitizeColorWarpPin(pin);
    return changed;
}

bool RenderRawLabColorSettings(RawLabColorSurfaceArgs& args) {
    if(!args.state)return false;
    auto& colorWarp=args.colorWarp;bool changed=args.panelControls?CommitColorWarpSampling(args):false;
    changed|=SelectionToggle("Color Warp enabled",colorWarp.enabled);
    ImGui::TextUnformatted("Combine edits");
    if(BareTextButton("Accumulate",colorWarp.combine==RawRecipe::RawColorWarpCombine::Accumulate)) {
        changed|=colorWarp.combine!=RawRecipe::RawColorWarpCombine::Accumulate;
        colorWarp.combine=RawRecipe::RawColorWarpCombine::Accumulate;
    }
    ContinueRawLabControlRow("Blend overlaps");
    if(BareTextButton("Blend overlaps",colorWarp.combine==RawRecipe::RawColorWarpCombine::Blend)) {
        changed|=colorWarp.combine!=RawRecipe::RawColorWarpCombine::Blend;
        colorWarp.combine=RawRecipe::RawColorWarpCombine::Blend;
    }
    LabTooltip("Accumulate adds matching edits. Blend overlaps averages them where the total participating weight exceeds one. Untouched selections never dilute edits.");
    ImGui::BeginDisabled(args.strengthDriven);
    changed|=Setting(args,"Overall strength","##strength",colorWarp.strength,0,2,1,"%.2f");
    ImGui::EndDisabled();
    if(SelectionToggle("Adjusted color cloud",args.ui.colorWarpLiveCloud))args.saveAppState();
    if(colorWarp.pins.empty())return changed;
    if(args.panelControls) {
        ImGui::Spacing();
        ImGui::TextDisabled("Color settings");
        const float start=ImGui::GetCursorPosX();
        for(std::size_t i=0;i<colorWarp.pins.size();++i) {
            const auto& pin=colorWarp.pins[i];
            const char* label=pin.family>=0?RawRecipe::ColorWarpFamilyName(pin.family):pin.name.c_str();
            if(i && ImGui::GetCursorPosX()+ImGui::CalcTextSize(label).x+16>ImGui::GetWindowContentRegionMax().x)ImGui::NewLine();
            ImGui::PushID(pin.id.c_str());
            if(BareTextButton(label,args.ui.selectedColorWarpPin==static_cast<int>(i))) {
                args.ui.selectedColorWarpPin=static_cast<int>(i);args.panelControls->selectedFamily=pin.family;
            }
            ImGui::PopID();
            if(i+1<colorWarp.pins.size())ImGui::SameLine(0,6);
        }
        ImGui::SetCursorPosX(start);
    }
    args.ui.selectedColorWarpPin=std::clamp(args.ui.selectedColorWarpPin,0,int(colorWarp.pins.size())-1);
    auto& selected=colorWarp.pins[args.ui.selectedColorWarpPin];
    ImGui::Spacing();
    ImGui::TextUnformatted(selected.name.c_str());
    if(args.panelControls && selected.family<0) {
        ImGui::SameLine();
        if(BareTextButton("Reset adjustment")) {
            selected.hueShift=selected.saturation=selected.brightness=selected.tintA=selected.tintB=0;changed=true;
        }
        if(BareTextButton("Delete selection")) {
            const auto regionId=selected.regionId;
            colorWarp.pins.erase(colorWarp.pins.begin()+args.ui.selectedColorWarpPin);
            if(std::none_of(colorWarp.pins.begin(),colorWarp.pins.end(),[&](const auto& p){return p.regionId==regionId;}))
                colorWarp.regions.erase(std::remove_if(colorWarp.regions.begin(),colorWarp.regions.end(),[&](const auto& r){return r.id==regionId;}),colorWarp.regions.end());
            args.ui.selectedColorWarpPin=0;args.panelControls->selectedFamily=0;
            ++args.state->localRevision;return true;
        }
    }
    if(!args.panelControls)changed|=DrawColorWarpSampleScope(args,selected);
    if(args.panelControls) {
        changed|=ImGui::Checkbox("Apply this color",&selected.enabled);
        if(BareTextButton("Turn",!selected.moveColors)) {changed|=selected.moveColors;selected.moveColors=false;}
        ImGui::SameLine();
        if(BareTextButton("Move",selected.moveColors)) {changed|=!selected.moveColors;selected.moveColors=true;}
        changed|=Setting(args,"Strength","pin-strength",selected.strength,0,2,1,"%.2f");
        changed|=Setting(args,"Smoothing","pin-smooth",selected.smoothSelection,0,1,0,"%.2f");
        if(selected.family<0) {
            changed|=Setting(args,"Green / red tint","pin-tint-a",selected.tintA,-.2f,.2f,0,"%+.3f");
            changed|=Setting(args,"Blue / yellow tint","pin-tint-b",selected.tintB,-.2f,.2f,0,"%+.3f");
        }
    }
    changed|=SelectionToggle("Protect this color",selected.protectColor);
    LabTooltip("Suppress overlapping edits here instead of adding a color adjustment.");
    if(selected.protectColor)changed|=Setting(args,"Protection","##protection",selected.strength,0,1,1,"%.2f");
    if(BareTextButton("Selection ranges",args.state->details))args.state->details=!args.state->details;
    if(args.state->details)changed|=DrawColorWarpRanges(args,selected);
    ImGui::Spacing();
    ImGui::TextUnformatted("Spatial restriction");
    if(BareTextButton("Choose area")) {
        args.ui.colorWarpResamplePinId.clear();
        args.ui.colorWarpAttachRegionPinId=selected.id;
        args.ui.colorWarpSamplingArmed=true;args.ui.colorWarpInspectionActive=false;
    }
    LabTooltip("Draw a sample circle on the image to restrict this selection to its connected region.");
    if(!colorWarp.regions.empty() && ImGui::BeginCombo("Sample reference",selected.regionId.empty()?"None":selected.regionId.c_str())) {
        if(ImGui::Selectable("None",selected.regionId.empty())){selected.regionId.clear();changed=true;}
        for(const auto& region:colorWarp.regions)if(ImGui::Selectable(region.id.c_str(),region.id==selected.regionId)){selected.regionId=region.id;changed=true;}
        ImGui::EndCombo();
    }
    auto selectedRegion = std::find_if(
        colorWarp.regions.begin(), colorWarp.regions.end(),
        [&](const auto& region) { return region.id == selected.regionId; });
    if (selectedRegion != colorWarp.regions.end()) {
        ImGui::TextDisabled("Spatial relation");
        const struct {
            const char* label;
            Stack::RawRecipe::RawColorWarpSpatialMode mode;
            const char* description;
        } spatialModes[] = {
            { "All", Stack::RawRecipe::RawColorWarpSpatialMode::AllMatches,
              "Affects every pixel matching this selection's color and exposure ranges." },
            { "Connected", Stack::RawRecipe::RawColorWarpSpatialMode::Connected,
              "Keeps only qualifying pixels connected to the sampled circle." },
            { "Cohesive", Stack::RawRecipe::RawColorWarpSpatialMode::Cohesive,
              "Keeps the sampled connected section, then closes small holes and removes isolated specks." },
            { "Reach", Stack::RawRecipe::RawColorWarpSpatialMode::EdgeAwareReach,
              "Extends from the sampled section through weak matches while stopping at strong image edges." },
            { "Assisted", Stack::RawRecipe::RawColorWarpSpatialMode::AssistedRegion,
              "Combines include circles, short gap bridging, and exclusion circles into one region." }
        };
        const char* selectedSpatialDescription = spatialModes[0].description;
        for (std::size_t modeIndex = 0; modeIndex < std::size(spatialModes); ++modeIndex) {
            if (modeIndex > 0u) ContinueRawLabControlRow(spatialModes[modeIndex].label);
            if (BareTextButton(
                    spatialModes[modeIndex].label,
                    selectedRegion->spatialMode == spatialModes[modeIndex].mode)) {
                selectedRegion->spatialMode = spatialModes[modeIndex].mode;
                changed = true;
            }
            LabTooltip(spatialModes[modeIndex].description);
            if (selectedRegion->spatialMode == spatialModes[modeIndex].mode) {
                selectedSpatialDescription = spatialModes[modeIndex].description;
            }
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextDisabled("%s", selectedSpatialDescription);
        ImGui::PopTextWrapPos();
        if (selectedRegion->spatialMode ==
                Stack::RawRecipe::RawColorWarpSpatialMode::EdgeAwareReach ||
            selectedRegion->spatialMode ==
                Stack::RawRecipe::RawColorWarpSpatialMode::AssistedRegion) {
            changed |= BareSliderFloat(
                "Reach", "RawLabColorWarpSpatialReach",
                &selectedRegion->reachPixels, 0.0f, 256.0f, "%.0f px");
            LabTooltip(
                "Maximum image-space distance that Reach or Assisted may grow "
                "outward from directly qualified pixels. The amber dashed "
                "viewport guide represents this distance.");
            changed |= BareSliderFloat(
                "Support", "RawLabColorWarpSpatialSupport",
                &selectedRegion->spatialSupport, 0.0f, 1.0f, "%.2f");
            LabTooltip(
                "Strength contributed by pixels admitted through spatial "
                "support rather than direct color-and-EV qualification. Zero "
                "disables the added spatial contribution; one uses it fully.");
        }
        changed |= BareSliderFloat(
            "Edge stop", "RawLabColorWarpEdgeStop",
            &selectedRegion->edgeStop, 0.0f, 1.0f, "%.2f");
        LabTooltip(
            "How strongly source-image color and luminance edges resist spatial "
            "growth. Higher values stop sooner at boundaries; lower values "
            "permit reach across larger changes.");
        changed |= BareSliderFloat(
            "Boundary", "RawLabColorWarpBoundaryFeather",
            &selectedRegion->featherPixels, 0.0f, 256.0f, "%.0f px");
        LabTooltip(
            "Width of the final image-space fade after spatial membership is "
            "resolved. This is separate from color-distance softness and EV "
            "feathering. The violet viewport guide represents this width.");
        const struct {
            const char* label;
            Stack::RawRecipe::RawColorWarpFeatherDirection direction;
            const char* description;
        } directions[] = {
            { "In", Stack::RawRecipe::RawColorWarpFeatherDirection::Inward,
              "Fade inward from the accepted region boundary, reducing effect near its interior edge." },
            { "Centered", Stack::RawRecipe::RawColorWarpFeatherDirection::Centered,
              "Split the boundary fade across both sides of the accepted region edge." },
            { "Out", Stack::RawRecipe::RawColorWarpFeatherDirection::Outward,
              "Preserve the accepted interior and fade outward beyond its boundary." }
        };
        for (std::size_t directionIndex = 0; directionIndex < std::size(directions); ++directionIndex) {
            if (directionIndex > 0u) ContinueRawLabControlRow(directions[directionIndex].label);
            if (BareTextButton(
                    directions[directionIndex].label,
                    selectedRegion->featherDirection == directions[directionIndex].direction)) {
                selectedRegion->featherDirection = directions[directionIndex].direction;
                changed = true;
            }
            LabTooltip(directions[directionIndex].description);
        }
    }


    if(changed)++args.state->localRevision;
    return changed;
}
}
