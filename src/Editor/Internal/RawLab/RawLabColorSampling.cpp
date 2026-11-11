#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Raw/ColorWarpMath.h"
#include "Editor/NodeEditors/PanelRoller.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
namespace {
ImVec4 SuggestedSwatch(const RawRecipe::RawColorWarpPin& pin,Raw::RawWorkingSpace space) {
    RawRecipe::RawColorWarpCoordinate c;c.lightness=.7f;c.a=pin.sourceA*.7f;c.b=pin.sourceB*.7f;
    auto rgb=ColorDiscDisplayRgb(RawRecipe::ColorWarpCoordinateToWorkingRgb(c,space),space);
    for(auto& x:rgb){x=std::max(0.f,x);x=x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f;}
    return {rgb[0],rgb[1],rgb[2],1};
}
}
void DrawColorWarpSamplingActions(RawLabColorSurfaceArgs& args) {
    auto& recipe=args.colorWarp;
    const float dot=std::max(18.f,ImGui::GetFontSize()*1.15f);
    ImGui::BeginDisabled(RawRecipe::ColorWarpSampleCount(recipe)>=RawRecipe::kMaxRawColorWarpSamples);
    if(BareTextButton("Sample image",args.ui.colorWarpSamplingArmed)) {
        args.ui.colorWarpSamplingArmed=!args.ui.colorWarpSamplingArmed;
        args.ui.colorWarpAttachRegionPinId.clear();
        args.ui.colorWarpResamplePinId.clear();
        args.ui.colorWarpInspectionActive=false;
    }
    ImGui::EndDisabled();
    LabTooltip("Click or draw a circle on the image, then add the sampled color. It initially applies throughout the image.");
    if(args.ui.colorWarpPendingCircleActive && args.ui.colorWarpAreaOwner==args.areaOwner) {
        ImGui::SameLine();
        const bool ready=!args.ui.colorWarpPendingCircleRefining &&
            (args.ui.colorWarpPendingCircleAppend || !args.ui.colorWarpAttachRegionPinId.empty() || !args.ui.colorWarpAreaAnalysisResult.proposals.empty());
        ImGui::BeginDisabled(!ready);
        if(!args.ui.colorWarpAttachRegionPinId.empty() || args.ui.colorWarpPendingCircleAppend) {
            if(BareTextButton("Apply region"))args.ui.colorWarpPendingCircleCommitRequested=true;
        } else {
            ImGui::TextUnformatted("Suggested colors - choose to add");
            ImGui::BeginDisabled(args.ui.colorWarpResamplePinId.empty() && RawRecipe::ColorWarpSampleCount(recipe)>=RawRecipe::kMaxRawColorWarpSamples);
            for(int i=0;i<int(args.ui.colorWarpAreaAnalysisResult.proposals.size());++i) {
                const auto& proposal=args.ui.colorWarpAreaAnalysisResult.proposals[i];
                RawRecipe::RawColorWarpPin suggested;suggested.sourceA=proposal.sourceA;suggested.sourceB=proposal.sourceB;
                ImGui::PushID(i);
                if(ImGui::ColorButton("Accept suggested color",SuggestedSwatch(suggested,args.workingSpace),ImGuiColorEditFlags_NoTooltip,{dot,dot})) {
                    args.ui.colorWarpAcceptedProposal=i;args.ui.colorWarpPendingCircleCommitRequested=true;
                }
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Add this sampled color (%zu pixels). No adjustment is applied initially.",proposal.supportingPixelCount);
                ImGui::PopID();ImGui::SameLine();
            }
            ImGui::EndDisabled();ImGui::NewLine();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(BareTextButton("Cancel")){args.ui.ResetColorWarpAreaRequest();args.ui.colorWarpResamplePinId.clear();}
    }
    if(!args.ui.colorWarpAreaStatus.empty())ImGui::TextWrapped("%s",args.ui.colorWarpAreaStatus.c_str());
    if(RawRecipe::ColorWarpSampleCount(recipe)>=RawRecipe::kMaxRawColorWarpSamples)
        ImGui::TextDisabled("8 / 8 custom selections. Delete one to add another.");
}
bool CommitColorWarpSampling(RawLabColorSurfaceArgs& args) {
    auto& ui=args.ui;auto& recipe=args.colorWarp;
    if(!args.areaEditingAllowed || !args.areaOwner.Valid())return false;
    bool changed=false;
    const auto inputKey=args.scope.graphInputFingerprint?args.scope.graphInputFingerprint:args.cloudInputFingerprint;
    if(args.scope.valid && ui.colorWarpSampleInputFingerprint && ui.colorWarpSampleInputFingerprint!=inputKey) {
        ui.ResetColorWarpAreaRequest();return false;
    }
    for(auto& region:recipe.regions)if(region.id==ui.colorWarpInspectedRegionId) {
        if(std::isfinite(ui.colorWarpRegionReachWheelDelta) && ui.colorWarpRegionReachWheelDelta!=0) {
            region.reachPixels=std::clamp(region.reachPixels+ui.colorWarpRegionReachWheelDelta,0.f,512.f);changed=true;
        }
        if(std::isfinite(ui.colorWarpRegionFeatherWheelDelta) && ui.colorWarpRegionFeatherWheelDelta!=0) {
            region.featherPixels=std::clamp(region.featherPixels+ui.colorWarpRegionFeatherWheelDelta,0.f,512.f);changed=true;
        }
    }
    ui.colorWarpRegionReachWheelDelta=ui.colorWarpRegionFeatherWheelDelta=0;
    if(ui.colorWarpAreaOwner!=args.areaOwner || !ui.colorWarpPendingCircleCommitRequested ||
        !ui.colorWarpPendingCircleActive || ui.colorWarpPendingCircleRefining)return changed;
    auto region=std::find_if(recipe.regions.begin(),recipe.regions.end(),[&](const auto& r){return r.id==ui.colorWarpPendingReplaceRegionId;});
    if(ui.colorWarpPendingCircleAppend) {
        if(region!=recipe.regions.end() && region->circles.size()<RawRecipe::kMaxRawColorWarpSampleCircles) {
            auto circle=ui.colorWarpPendingCircle;circle.id="sample-"+std::to_string(region->circles.size()+1);
            region->circles.push_back(std::move(circle));region->spatialMode=RawRecipe::RawColorWarpSpatialMode::AssistedRegion;
            ui.colorWarpInspectionActive=true;ui.colorWarpInspectedRegionId=region->id;changed=true;
        }
        ui.ResetColorWarpAreaRequest();return changed;
    }
    // Resizing/repositioning a retained sample changes its spatial reference,
    // never discards its adjustments or silently samples its altered output.
    if(region!=recipe.regions.end()) {
        auto circle=std::find_if(region->circles.begin(),region->circles.end(),[&](const auto& c){return c.id==ui.colorWarpPendingReplaceCircleId;});
        if(circle!=region->circles.end()) {
            const auto id=circle->id;*circle=ui.colorWarpPendingCircle;circle->id=id;
        }
        ui.ResetColorWarpAreaRequest();return true;
    }
    // A newly revised graph must supply its current incoming image before a
    // suggested color can be accepted. Adjustment-only revisions can reuse it.
    if(!args.scope.valid && ui.colorWarpAttachRegionPinId.empty())return changed;
    if(ui.colorWarpAttachRegionPinId.empty() && (ui.colorWarpAcceptedProposal<0 ||
        ui.colorWarpAcceptedProposal>=int(ui.colorWarpAreaAnalysisResult.proposals.size())))return changed;
    if(!ui.colorWarpResamplePinId.empty()) {
        const auto found=std::find_if(recipe.pins.begin(),recipe.pins.end(),[&](const auto& p){return p.id==ui.colorWarpResamplePinId;});
        if(found!=recipe.pins.end() && found->family<0) {
            const auto& proposal=ui.colorWarpAreaAnalysisResult.proposals[ui.colorWarpAcceptedProposal];
            RawRecipe::RawColorWarpCoordinate input;
            input.lightness=proposal.sourceLightness;input.a=proposal.sourceA;input.b=proposal.sourceB;
            changed=RawRecipe::SetColorWarpSample(*found,input);
            if(changed)found->population=proposal.population;
        }
        ui.colorWarpResamplePinId.clear();ui.colorWarpSamplingArmed=false;
        ui.ResetColorWarpAreaRequest();return changed;
    }
    recipe.regions.erase(std::remove_if(recipe.regions.begin(),recipe.regions.end(),[&](const auto& r) {
        return std::none_of(recipe.pins.begin(),recipe.pins.end(),[&](const auto& p){return p.regionId==r.id;});
    }),recipe.regions.end());
    if(recipe.regions.size()>=RawRecipe::kMaxRawColorWarpRegions) {
        ui.ResetColorWarpAreaRequest();ui.colorWarpAreaStatus="Remove a sampled selection before adding another.";return changed;
    }
    RawRecipe::RawColorWarpRegion newRegion;
    newRegion.id="region-"+std::to_string(recipe.regions.size()+1);
    while(std::any_of(recipe.regions.begin(),recipe.regions.end(),[&](const auto& r){return r.id==newRegion.id;}))newRegion.id+="-copy";
    newRegion.spatialMode=RawRecipe::RawColorWarpSpatialMode::AllMatches;
    newRegion.circles={ui.colorWarpPendingCircle};newRegion.circles.front().id="sample-1";
    if(!ui.colorWarpAttachRegionPinId.empty()) {
        const auto attached=std::find_if(recipe.pins.begin(),recipe.pins.end(),[&](const auto& p){return p.id==ui.colorWarpAttachRegionPinId;});
        if(attached!=recipe.pins.end()) {
            attached->regionId=newRegion.id;newRegion.spatialMode=RawRecipe::RawColorWarpSpatialMode::Connected;
            ui.colorWarpInspectedRegionId=newRegion.id;ui.colorWarpInspectionActive=true;
            recipe.regions.push_back(std::move(newRegion));changed=true;
        }
        ui.colorWarpAttachRegionPinId.clear();ui.colorWarpSamplingArmed=false;
        ui.ResetColorWarpAreaRequest();return changed;
    }
    const auto before=recipe.pins.size();
    const auto accepted=ui.colorWarpAcceptedProposal;
    const auto proposal=ui.colorWarpAreaAnalysisResult.proposals[accepted];
    if(RawRecipe::ColorWarpSampleCount(recipe)<RawRecipe::kMaxRawColorWarpSamples) {

        RawRecipe::RawColorWarpPin pin;
        RawRecipe::RawColorWarpCoordinate coordinate;
        coordinate.lightness=proposal.sourceLightness;coordinate.a=proposal.sourceA;coordinate.b=proposal.sourceB;
        if(!RawRecipe::SetColorWarpSample(pin,coordinate))return changed;
        pin.population=proposal.population;
        pin.id="sampled-"+std::to_string(RawRecipe::ColorWarpSampleCount(recipe)+1);
        while(std::any_of(recipe.pins.begin(),recipe.pins.end(),[&](const auto& p){return p.id==pin.id;}))pin.id+="-copy";
        pin.name="Sampled color "+std::to_string(RawRecipe::ColorWarpSampleCount(recipe)+1);pin.regionId=newRegion.id;
        recipe.pins.push_back(std::move(pin));
    }
    const bool added=recipe.pins.size()>before;
    if(added) {
        recipe.regions.push_back(std::move(newRegion));ui.selectedColorWarpPin=int(before);
        if(args.panelControls)args.panelControls->selectedFamily=-1;
        ui.colorWarpInspectedRegionId=recipe.pins[before].regionId;ui.colorWarpInspectionActive=false;
        ui.colorWarpSamplingArmed=false;
    }
    if(added)ui.colorWarpAreaAnalysisResult.proposals.erase(ui.colorWarpAreaAnalysisResult.proposals.begin()+accepted);
    ui.colorWarpAcceptedProposal=-1;ui.colorWarpPendingCircleCommitRequested=false;
    if(ui.colorWarpAreaAnalysisResult.proposals.empty())ui.ResetColorWarpAreaRequest();
    else if(RawRecipe::ColorWarpSampleCount(recipe)>=RawRecipe::kMaxRawColorWarpSamples)
        ui.colorWarpAreaStatus="Eight custom selections are in use. Delete one before accepting another suggestion.";
    return changed || added;
}
}
