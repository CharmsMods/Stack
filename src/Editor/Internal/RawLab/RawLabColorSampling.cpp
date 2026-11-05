#include "Editor/Internal/RawLab/RawLabColorSurface.h"
#include "Raw/ColorWarpMath.h"
#include <algorithm>
#include <cmath>

namespace Stack::Editor::RawLabInternal {
bool CommitColorWarpSampling(RawLabColorSurfaceArgs& args) {
    auto& ui=args.ui;auto& recipe=args.colorWarp;
    if(!args.areaEditingAllowed || !args.areaOwner.Valid())return false;
    bool changed=false;
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
    for(const auto& proposal:ui.colorWarpAreaAnalysisResult.proposals) {
        if(RawRecipe::ColorWarpSampleCount(recipe)>=RawRecipe::kMaxRawColorWarpSamples)break;
        RawRecipe::RawColorWarpPin pin;
        RawRecipe::RawColorWarpCoordinate coordinate;
        coordinate.lightness=proposal.sourceLightness;coordinate.a=proposal.sourceA;coordinate.b=proposal.sourceB;
        if(!RawRecipe::SetColorWarpSample(pin,coordinate))continue;
        pin.id="sampled-"+std::to_string(RawRecipe::ColorWarpSampleCount(recipe)+1);
        while(std::any_of(recipe.pins.begin(),recipe.pins.end(),[&](const auto& p){return p.id==pin.id;}))pin.id+="-copy";
        pin.name="Sampled color "+std::to_string(RawRecipe::ColorWarpSampleCount(recipe)+1);pin.regionId=newRegion.id;
        recipe.pins.push_back(std::move(pin));
    }
    const bool added=recipe.pins.size()>before;
    if(added) {
        recipe.regions.push_back(std::move(newRegion));ui.selectedColorWarpPin=int(before);
        ui.colorWarpInspectedRegionId=recipe.pins[before].regionId;ui.colorWarpInspectionActive=false;
        ui.colorWarpSamplingArmed=false;
    }
    ui.ResetColorWarpAreaRequest();
    if(!added)ui.colorWarpAreaStatus="Choose a distinguishable color; neutral gray has no hue to select.";
    return changed || added;
}
}
