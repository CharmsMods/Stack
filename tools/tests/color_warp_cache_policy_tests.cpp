#include "Renderer/RawDevelopmentStageCachePolicy.h"

#include <array>
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
    pin.targetA = 0.1f;
    recipe.colorWarp.pins.push_back(pin);
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
            case 3: recipe.colorWarp.pins.front().targetA = 0; break;
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
    PolicyTransitions();
    UnaffectedRecipes();
    std::cout << "Color Warp cache policy: warm/fresh quality transitions, stage boundaries and inactive reuse passed.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
