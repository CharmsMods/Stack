#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/Denoise/RawDenoiseBandSchedule.h"

namespace Raw {
enum class ViewportRegionRequirement { Pointwise, Neighborhood, FullInput };
struct ViewportStageRegion {
    ViewportRegionRequirement requirement = ViewportRegionRequirement::Pointwise;
    int support = 0;
    bool materializeFullInput = false;
};
namespace ViewportModules {
inline ViewportStageRegion RawBaseRegion(const Stack::RawRecipe::RawDevelopmentRecipe& r, int, int, int, int) {
    // Sensor-coordinate CFA kernels retain their full input until tiled upload exists.
    return {ViewportRegionRequirement::Neighborhood,
        (r.technical.demosaicMethod == DemosaicMethod::MalvarHeCutler ? 2 : 1) +
        (r.technical.mosaicDenoise.enabled ? 2*std::clamp(r.technical.mosaicDenoise.radius,1,4) : 0), true};
}
inline ViewportStageRegion DenoiseRegion(const Stack::RawRecipe::RawDevelopmentRecipe& r,
    int width, int height, int sourceWidth, int sourceHeight) {
    if (!Stack::RawRecipe::IsRgbDenoiseActive(r.rgbDenoise)) return {};
    if (r.rgbDenoise.method != Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1)
        return {ViewportRegionRequirement::FullInput,0,true};
    int support=1;
    for (const auto& band : Stack::RawRecipe::BuildRawDenoiseBandSchedule(width,height,sourceWidth,sourceHeight,r.rgbDenoise.maximumStructureSize))
        support+=2*band.renderGap;
    return {ViewportRegionRequirement::Neighborhood,support,true};
}
inline ViewportStageRegion LocalRegion(const Stack::RawRecipe::RawDevelopmentRecipe& r, int, int, int, int) {
    // Local selections retain full-image coordinates and connected neighborhoods.
    return Stack::RawRecipe::IsLocalRangeEnabled(r) || !r.localRange.areas.empty()
        ? ViewportStageRegion{ViewportRegionRequirement::FullInput,0,true} : ViewportStageRegion{};
}
inline ViewportStageRegion ToneRegion(const Stack::RawRecipe::RawDevelopmentRecipe& r, int, int, int, int) {
    const auto& tone=r.finishTone.layerJson;
    if (tone.is_object() && (tone.value("localBaselineEnabled",false) || tone.value("autoCalibratePending",false) ||
        tone.value("targetScope",0)!=0 || tone.value("foundationAdaptiveAssist",false)))
        return {ViewportRegionRequirement::FullInput,0,false};
    return {};
}
inline ViewportStageRegion ColorRegion(const Stack::RawRecipe::RawDevelopmentRecipe& r, int, int, int, int) {
    if (Stack::RawRecipe::IsDetailContrastActive(r.detailContrast))
        return {ViewportRegionRequirement::FullInput,0,true};
    return Stack::RawRecipe::IsColorWarpEnabled(r.colorWarp) && !r.colorWarp.regions.empty()
        ? ViewportStageRegion{ViewportRegionRequirement::FullInput,0,false} : ViewportStageRegion{};
}
}
}
