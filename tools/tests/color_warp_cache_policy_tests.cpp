#include "Renderer/RawDevelopmentStageCachePolicy.h"

#include <array>
#include "Raw/ColorWarpMath.h"
#include "Raw/RawColorWarpSelection.h"
#include <iostream>
#include <stdexcept>

namespace {
using namespace Stack::RawRecipe;
using namespace Stack::Renderer::RawDevelopmentCache;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

RawDevelopmentRecipe SpatialRecipe() {
    RawDevelopmentRecipe recipe;
    RawColorWarpRegion region;
    region.id = "region";
    region.spatialMode = RawColorWarpSpatialMode::Cohesive;
    region.circles.emplace_back();
    region.circles.back().id = "sample";
    recipe.colorWarp.regions.push_back(region);
    RawColorWarpPin pin;
    pin.id = "pin";
    pin.regionId = region.id;
    pin.sourceA = .3f;pin.hueShift = 10.f;
    recipe.colorWarp.pins={pin};
    return recipe;
}

void SameEarlyStages(const StageFingerprints& a, const StageFingerprints& b) {
    Require(a.rawBase == b.rawBase && a.neutralPlacement == b.neutralPlacement &&
        a.calibratedNeutral == b.calibratedNeutral && a.rawPlacement == b.rawPlacement &&
        a.postLocalRange == b.postLocalRange && a.postFinishTone == b.postFinishTone,
        "Color Warp mask policy invalidated an earlier reusable stage");
}

void PolicyTransitions() {
    const auto recipe = SpatialRecipe();
    Require(UsesColorWarpSpatialMasks(recipe.colorWarp), "fixture does not use spatial masks");
    const std::array<ColorWarpMaskRenderPolicy, 9> policies{{
        {384, true, false}, {384, false, false}, {1024, false, false},
        {2048, false, false}, {2048, false, true},
        {1024, false, false}, {1024, true, false}, {1024, true, true},
        {384, true, false}}};
    auto warmed = BuildStageFingerprints(recipe, 2048, policies.front());
    for (std::size_t i = 1; i < policies.size(); ++i) {
        const auto fresh = BuildStageFingerprints(recipe, 2048, policies[i]);
        SameEarlyStages(warmed, fresh);
        Require(warmed.postColorWarp != fresh.postColorWarp &&
            warmed.postViewTransform != fresh.postViewTransform &&
            warmed.postOutputCrop != fresh.postOutputCrop,
            "warmed incompatible mask policy could satisfy a fresh request");
        Require(BuildStageFingerprint(recipe, 2048, Stage::PostOutputCrop, policies[i]) ==
            fresh.postOutputCrop, "single-stage helper did not preserve the actual policy");
        warmed = fresh;
        Require(warmed.postOutputCrop == BuildStageFingerprints(recipe, 2048, policies[i]).postOutputCrop,
            "unchanged policy lost warm-cache eligibility");
    }
    const auto clamp = [&](int edge) {
        return BuildStageFingerprints(recipe, 2048, {edge, false, true}).postOutputCrop;
    };
    Require(clamp(-1) == clamp(384) && clamp(9999) == clamp(2048),
        "policy fingerprint disagrees with setter quality clamping");
    Require(BuildStageFingerprints(recipe, 2048).postOutputCrop ==
        BuildStageFingerprints(recipe, 2048, {1024, false, false}).postOutputCrop,
        "default policy no longer describes the default renderer");
}

void UnaffectedRecipes() {
    for (int mode = 0; mode < 5; ++mode) {
        auto recipe = SpatialRecipe();
        switch (mode) {
            case 0: recipe.colorWarp.enabled = false; break;
            case 1: recipe.colorWarp.strength = 0; break;
            case 2: recipe.colorWarp.pins.front().enabled = false; break;
            case 3: recipe.colorWarp.pins.front().hueShift = 0; break;
            case 4: recipe.colorWarp.regions.front().spatialMode = RawColorWarpSpatialMode::AllMatches; break;
        }
        Require(!UsesColorWarpSpatialMasks(recipe.colorWarp), "inactive/non-spatial recipe uses mask policy");
        const auto a = BuildStageFingerprints(recipe, 2048, {384, true, false});
        const auto b = BuildStageFingerprints(recipe, 2048, {2048, false, true});
        SameEarlyStages(a, b);
        Require(a.postColorWarp == b.postColorWarp && a.postViewTransform == b.postViewTransform &&
            a.postOutputCrop == b.postOutputCrop, "irrelevant policy prevented unchanged output reuse");
    }
}
}

int main() try {
    auto recipe=SpatialRecipe();
    const auto before=BuildStageFingerprints(recipe,2048);
    recipe.colorWarp.pins.front().moveColors=true;
    Require(before.postColorWarp!=BuildStageFingerprints(recipe,2048).postColorWarp,"Move reused Turn output");
    auto saved=SerializeColorWarpRecipe(recipe.colorWarp);
    Require(DeserializeColorWarpRecipe(saved).pins.back().moveColors,"Move mode failed save round-trip");
    saved["pins"].back().erase("moveColors");
    Require(!DeserializeColorWarpRecipe(saved).pins.back().moveColors,"Legacy pin did not retain Turn");
    auto& pin=recipe.colorWarp.pins.front();pin.hueShift=pin.saturation=pin.exposureEv=0;
    Require(ColorWarpDestination(pin)==std::array<float,2>{pin.sourceA,pin.sourceB},"Neutral Move changed destination");
    pin.regionId.clear();pin.hueRange.enabled=false;pin.saturationRange.enabled=false;
    RawColorWarpCoordinate input;input.lightness=.7f;input.a=.15f;input.b=.1f;
    auto protector=pin;protector.id="protect";protector.protectColor=true;recipe.colorWarp.pins.push_back(protector);
    Require(ColorWarpPreviewInfluence(recipe.colorWarp,0,input,{})==0,"Preview ignored protection");
    recipe.colorWarp.pins.back().enabled=false;
    Require(ColorWarpPreviewInfluence(recipe.colorWarp,0,input,{})>.99f,"Unadjusted selection could not be previewed");
    RawColorWarpPin soft;
    RawColorWarpCoordinate gray;gray.lightness=.7f;gray.a=.007f;gray.b=0;gray.sceneEv=0;
    Require(SetColorWarpSample(soft,gray),"Near-neutral sampling failed");
    Require(soft.sampleRadius>=.04f && soft.smoothSelection>0,"Sample has no noise tolerance");
    auto grayNoise=gray;grayNoise.b=.002f;
    Require(std::abs(EvaluateColorWarpPinColorWeight(soft,gray)-EvaluateColorWarpPinColorWeight(soft,grayNoise))<.02f,
        "Small neutral color noise fragmented selection");
    RawColorWarpRecipe softRecipe;softRecipe.pins={soft};
    auto softSaved=SerializeColorWarpRecipe(softRecipe);
    Require(DeserializeColorWarpRecipe(softSaved).pins.back().sampleRadius==soft.sampleRadius,"Soft range lost on save");
    softSaved["pins"].back().erase("sampleRadius");
    Require(DeserializeColorWarpRecipe(softSaved).pins.back().sampleRadius==0,"Legacy selection range changed");
    // Small membership hole in otherwise similar material; strong edge at right.
    soft.sampleRadius=0;soft.hueRange={-3,3,1,1,true};soft.saturationRange.enabled=false;
    soft.smoothSelection=1;
    RawColorWarpMaskGuide guide;guide.width=guide.height=5;guide.pixels.assign(25,gray);
    guide.pixels[12].b=.001f;
    for(int y=0;y<5;++y) {guide.pixels[y*5+4].b=.3f;guide.pixels[y*5+4].sceneEv=5;}
    const auto fields=BuildRawColorWarpMaskFields(guide,soft,nullptr);
    Require(EvaluateColorWarpPinColorWeight(soft,guide.pixels[12])==0 && fields.smoothGate[12]>.1f,"Smoothing left a selection pinhole");
    Require(fields.smoothGate[14]<.001f,"Smoothing crossed a strong material edge");
    RawColorWarpMaskSample absolute{fields.smoothGate[12],0,1,true};
    Require(EvaluateColorWarpPinWeight(soft,guide.pixels[12],absolute)>.1f,"CPU reapplied the noisy qualifier");
    soft.exposureEv=2.67f;softRecipe.pins={soft};
    std::vector<RawColorWarpMaskSample> masks{{.75f,0,1,true}};
    const std::array<float,3> hdr{4.f,2.f,1.f};
    const auto bright=ApplyPreparedColorWarp(softRecipe,hdr,Raw::RawWorkingSpace::LinearSrgbD65,&masks);
    Require(bright[0]>4 && std::abs(bright[0]/hdr[0]-bright[1]/hdr[1])<1e-5f,"Exposure clipped HDR or altered color ratios");
    PolicyTransitions();
    UnaffectedRecipes();
    std::cout << "Color Warp cache policy: warm/fresh quality transitions, stage boundaries and inactive reuse passed.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
