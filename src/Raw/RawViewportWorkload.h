#pragma once

#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawViewportStageRequirements.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include <array>
#include <vector>

namespace Raw {
using ViewportStage = Stack::Renderer::RawDevelopmentCache::Stage;
constexpr std::size_t kViewportStageCount = 8;
inline constexpr std::uint64_t kViewportTimingVersion = 5;
using ViewportStageCosts = std::array<double, kViewportStageCount>;

namespace ViewportModules {
using Recipe = Stack::RawRecipe::RawDevelopmentRecipe;
using namespace Stack::Renderer::RawDevelopmentCache;
inline std::size_t RawBaseCost(const Recipe& input) {
    std::size_t base = 1;
    HashTypedValue(base, input.rawRecipeVersion);
    HashTypedValue(base, input.technical.processingVersion);
    HashTypedValue(base, input.technical.demosaicMethod);
    HashTypedValue(base, input.technical.workingSpace);
    const auto& mosaic = input.technical.mosaicDenoise;
    HashTypedValue(base, mosaic.enabled);
    HashTypedValue(base, mosaic.mode);
    HashTypedValue(base, mosaic.radius);
    HashTypedValue(base, mosaic.iterations);
    HashTypedValue(base, mosaic.hotPixelSuppression);
    HashTypedValue(base, input.cropRotation.rotationDegrees);
    return base;
}
inline std::size_t NeutralPlacementCost(const Recipe& input) {
    std::size_t denoise = 1;
    HashTypedValue(denoise, Stack::RawRecipe::IsRgbDenoiseActive(input.rgbDenoise));
    HashTypedValue(denoise, input.rgbDenoise.method);
    HashTypedValue(denoise, input.rgbDenoise.mapping);
    HashTypedValue(denoise, input.rgbDenoise.modelSha256);
    HashTypedValue(denoise, input.rgbDenoise.maximumStructureSize);
    HashTypedValue(denoise, input.rgbDenoise.lumaMap.points.size());
    HashTypedValue(denoise, input.rgbDenoise.chromaMap.points.size());
    return denoise;
}
inline std::size_t RawPlacementCost(const Recipe& input) {
    if (Stack::RawRecipe::IsColorCalibrationActive(input.colorCalibration)) return 3;
    return std::abs(input.preToneExposureEv) > 0.0001f ? 2 : 1;
}
inline std::size_t PostLocalRangeCost(const Recipe& input) {
    std::size_t local = 1;
    HashTypedValue(local, Stack::RawRecipe::IsLocalRangeEnabled(input));
    HashTypedValue(local, input.localRange.points.size());
    HashTypedValue(local, input.localRange.targetZones.size());
    HashTypedValue(local, input.localRange.regionMaskEnabled);
    HashTypedValue(local, input.localRange.colorMaskEnabled);
    HashTypedValue(local, input.localRange.areas.size());
    for (const auto& area : input.localRange.areas) {
        HashTypedValue(local, area.enabled);
        HashTypedValue(local, area.points.size());
        HashTypedValue(local, area.strokes.size());
        for (const auto& stroke : area.strokes) {
            HashTypedValue(local, stroke.path.size());
            HashTypedValue(local, stroke.radius);
            HashTypedValue(local, stroke.followEdges);
        }
    }
    for (const auto& zone : input.localRange.targetZones) {
        HashTypedValue(local, zone.enabled);
        HashTypedValue(local, zone.scope);
        HashTypedValue(local, zone.seeds.size());
    }
    return local;
}
inline std::size_t PostFinishToneCost(const Recipe& input) {
    const auto& tone = input.finishTone.layerJson;
    std::size_t toneKey = 1;
    for (const char* field : {"type", "mode", "domain", "samplingBasis", "targetingMode", "autoCalibratePending",
        "localBaselineEnabled", "localBaselineRadius", "foundationAdaptiveAssist", "targetScope", "scopedMaskAction"})
        if (tone.is_object() && tone.contains(field)) HashTypedJson(toneKey, tone[field]);
    for (const char* field : {"points", "preparedPoints"})
        if (tone.is_object() && tone.contains(field)) HashTypedValue(toneKey, tone[field].size());
    if (tone.is_object() && tone.contains("pointCurves") && tone["pointCurves"].is_object())
        for (const auto& channel : tone["pointCurves"]) {
            if (channel.is_object() && channel.contains("points")) HashTypedValue(toneKey,channel["points"].size());
            if (channel.is_object() && channel.contains("interpolation")) HashTypedJson(toneKey,channel["interpolation"]);
        }
    return toneKey;
}
inline std::size_t PostColorWarpCost(const Recipe& input) {
    std::size_t color = 1;
    HashTypedValue(color, Stack::RawRecipe::IsDetailContrastActive(input.detailContrast));
    HashTypedValue(color, input.detailContrast.maximumScale);
    HashTypedValue(color, Stack::RawRecipe::IsColorWarpEnabled(input.colorWarp));
    HashTypedValue(color, input.colorWarp.version);
    HashTypedValue(color, input.colorWarp.pins.size());
    for (const auto& region : input.colorWarp.regions) {
        HashTypedValue(color, region.spatialMode);
        HashTypedValue(color, region.reachPixels);
        HashTypedValue(color, region.featherPixels);
        HashTypedValue(color, region.circles.size());
    }
    return color;
}
inline std::size_t PostViewTransformCost(const Recipe& input) {
    std::size_t view = 1;
    HashTypedValue(view, Stack::RawRecipe::IsViewTransformEnabled(input));
    const auto viewJson = input.viewTransform.layerJson.is_object() ? input.viewTransform.layerJson : nlohmann::json::object();
    HashTypedValue(view, viewJson.value("preserveHue", true));
    HashTypedValue(view, viewJson.value("encodeSrgbOutput", input.technical.encodeSrgbOutput));
    return view;
}
inline std::size_t PostOutputCropCost(const Recipe& input) {
    std::size_t crop = 1;
    HashTypedValue(crop,input.cropRotation.cropEnabled);
    if (input.cropRotation.cropEnabled) {
        HashTypedValue(crop,input.cropRotation.cropWidth);
        HashTypedValue(crop,input.cropRotation.cropHeight);
    }
    return crop;
}

inline bool RawBaseProbe(Recipe& r) {
    if (r.technical.mosaicDenoise.enabled) return false;
    r.technical.mosaicDenoise.enabled = true;
    r.technical.mosaicDenoise.lumaStrength = 0.25f;
    return true;
}
inline bool DenoiseProbe(Recipe& r) {
    if (Stack::RawRecipe::IsRgbDenoiseActive(r.rgbDenoise) ||
        r.rgbDenoise.method != Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1) return false;
    r.rgbDenoise.enabled = true;
    r.rgbDenoise.lumaMap.baseMultiplier = 0.25f;
    return true;
}
inline bool ExposureProbe(Recipe& r) {
    if (std::abs(r.preToneExposureEv) >= 0.0001f) return false;
    r.preToneExposureEv = 0.25f;
    return true;
}
inline bool LocalProbe(Recipe& r) {
    if (Stack::RawRecipe::IsLocalRangeEnabled(r)) return false;
    r.localRange.enabled = true;
    r.localRange.strength = 1.0f;
    if (r.localRange.points.empty()) r.localRange.points = Stack::RawRecipe::DefaultLocalRangeRecipe().points;
    if (!r.localRange.points.empty()) r.localRange.points[r.localRange.points.size()/2].deltaEv = 0.25f;
    return true;
}
inline bool ColorProbe(Recipe& r) {
    if (Stack::RawRecipe::IsColorWarpEnabled(r.colorWarp)) return false;
    r.colorWarp.enabled = true;
    r.colorWarp.strength = 1.0f;
    Stack::RawRecipe::RawColorWarpPin pin;
    pin.id = "viewport-probe"; pin.sourceA = 0.03f; pin.targetA = 0.04f;
    r.colorWarp.pins = {pin};
    return true;
}
inline bool ViewProbe(Recipe& r) {
    if (Stack::RawRecipe::IsViewTransformEnabled(r)) return false;
    r.viewTransform.layerJson["enabled"] = true;
    r.viewTransform.layerJson["exposure"] = 0.25f;
    return true;
}
}

enum class ViewportPreparation { StageAndTileCancellation, CompletedInputOnly };
struct ViewportModuleDescriptor {
    ViewportStage stage;
    const char* name;
    std::size_t (*costIdentity)(const ViewportModules::Recipe&);
    bool (*unusedProbe)(ViewportModules::Recipe&);
    ViewportPreparation (*preparation)(const ViewportModules::Recipe&);
    const char* reconfiguredLayerType = nullptr;
    ViewportStageRegion (*regionRequirement)(const ViewportModules::Recipe&, int, int, int, int) = nullptr;
};
inline ViewportPreparation OrdinaryViewportPreparation(const ViewportModules::Recipe&) {
    return ViewportPreparation::StageAndTileCancellation;
}
inline ViewportPreparation DenoiseViewportPreparation(const ViewportModules::Recipe& r) {
    return r.rgbDenoise.method == Stack::RawRecipe::RawRgbDenoiseMethod::ClassicalMultiscaleV1
        ? ViewportPreparation::StageAndTileCancellation : ViewportPreparation::CompletedInputOnly;
}
// Each timed stage registers its cost identity, region requirements, probe and
// cancellation contract together. A null region requirement is pointwise.
inline constexpr std::array<ViewportModuleDescriptor, kViewportStageCount> kViewportModules {{
    {ViewportStage::RawBase, "Transform and RAW denoise", ViewportModules::RawBaseCost, ViewportModules::RawBaseProbe, OrdinaryViewportPreparation, nullptr, ViewportModules::RawBaseRegion},
    {ViewportStage::NeutralPlacement, "Denoise", ViewportModules::NeutralPlacementCost, ViewportModules::DenoiseProbe, DenoiseViewportPreparation, nullptr, ViewportModules::DenoiseRegion},
    {ViewportStage::RawPlacement, "EV", ViewportModules::RawPlacementCost, ViewportModules::ExposureProbe, OrdinaryViewportPreparation},
    {ViewportStage::PostLocalRange, "Local EV", ViewportModules::PostLocalRangeCost, ViewportModules::LocalProbe, OrdinaryViewportPreparation, nullptr, ViewportModules::LocalRegion},
    {ViewportStage::PostFinishTone, "Tone curve", ViewportModules::PostFinishToneCost, nullptr, OrdinaryViewportPreparation, "ToneCurve", ViewportModules::ToneRegion},
    {ViewportStage::PostColorWarp, "Color and detail", ViewportModules::PostColorWarpCost, ViewportModules::ColorProbe, OrdinaryViewportPreparation, nullptr, ViewportModules::ColorRegion},
    {ViewportStage::PostViewTransform, "View transform", ViewportModules::PostViewTransformCost, ViewportModules::ViewProbe, OrdinaryViewportPreparation, "ViewTransform"},
    {ViewportStage::PostOutputCrop, "Transform", ViewportModules::PostOutputCropCost, nullptr, OrdinaryViewportPreparation},
}};

inline std::array<std::size_t,kViewportStageCount> ViewportWorkloadKeys(const ViewportModules::Recipe& recipe) {
    std::array<std::size_t,kViewportStageCount> keys {};
    for (const auto& module : kViewportModules) keys[static_cast<std::size_t>(module.stage)] = module.costIdentity(recipe);
    return keys;
}
inline std::array<std::size_t,kViewportStageCount> ViewportRefinementKeys(std::array<std::size_t,kViewportStageCount> keys) {
    for (auto& key : keys) Stack::Renderer::RawDevelopmentCache::HashTypedValue(key,std::string("native-refinement"));
    return keys;
}
inline ViewportStage ChangedViewportStage(
    const Stack::RawRecipe::RawDevelopmentRecipe& before,
    const Stack::RawRecipe::RawDevelopmentRecipe& after) {
    using namespace Stack::Renderer::RawDevelopmentCache;
    const auto a = BuildStageFingerprints(before, 0);
    const auto b = BuildStageFingerprints(after, 0);
    for (const auto& descriptor : kViewportModules)
        if (a.For(descriptor.stage) != b.For(descriptor.stage)) return descriptor.stage;
    return ViewportStage::PostOutputCrop;
}

struct ViewportProbe {
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    ViewportStage stage = ViewportStage::RawBase;
    bool hypothetical = false;
};

inline std::vector<ViewportProbe> BuildViewportProbes(const ViewportModules::Recipe& current, ViewportStage selected) {
    std::vector<ViewportProbe> probes {{current, ViewportStage::RawBase, false}};
    for (const auto& module : kViewportModules) {
        if (module.preparation(current) != ViewportPreparation::StageAndTileCancellation) continue;
        auto recipe = current;
        if (module.unusedProbe && module.unusedProbe(recipe)) probes.push_back({std::move(recipe),module.stage,true});
        else if (module.reconfiguredLayerType) probes.push_back({current,module.stage,false});
    }
    std::stable_sort(probes.begin()+1,probes.end(),[selected](const auto& a,const auto& b) {
        return a.stage == selected && b.stage != selected;
    });
    return probes;
}
}
