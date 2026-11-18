#include "Renderer/RawDevelopmentStageCachePolicy.h"

#include <array>
#include "Raw/ColorWarpMath.h"
#include "Raw/RawColorWarpSelection.h"
#include "Raw/PreparedColorWarp.h"
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

std::array<float,3> ReferenceHslCurve(const ColorWarpHslCoefficients& coefficients,float hue) {
    for(int i=0;i<8;++i) {
        const auto& a=coefficients[i];const auto& b=coefficients[(i+1)%8];
        const auto positive=[](float h){return h-360.f*std::floor(h/360.f);};
        const float span=positive(b[0]-a[0]),distance=positive(hue-a[0]);
        if(distance<=span) {
            float t=distance/std::max(span,.000001f);t=t*t*(3.f-2.f*t);
            float delta=ColorWarpHueDelta(b[1]-a[1]);if(delta==-180.f)delta=180.f;
            return {a[1]+t*delta,a[2]+t*(b[2]-a[2]),a[3]+t*(b[3]-a[3])};
        }
    }
    return {};
}

std::array<float,3> ReferenceHslSelections(const RawColorWarpRecipe& recipe,
    const RawColorWarpCoordinate& input,std::array<float,3> global,std::array<float,2> uv) {
    auto result=recipe.limitHslToSelections?std::array<float,3>{}:global;
    for(const auto& selection:recipe.hslSelections) {
        const float weight=HslSelectionWeight(selection,input,uv);if(weight<=0)continue;
        const auto own=selection.own[0]||selection.own[1]||selection.own[2]?
            ReferenceHslCurve(HslOwnCoefficients(selection),ColorWarpHue(input.a,input.b)):std::array<float,3>{};
        for(int channel=0;channel<3;++channel) {
            const float target=selection.own[channel]?own[channel]:global[channel]*selection.influence;
            result[channel]+=weight*(target-result[channel]);
        }
    }
    return result;
}

void PreparedEvaluationParity() {
    RawColorWarpRecipe recipe;
    recipe.version=6;recipe.pins[0].hueShift=35;recipe.pins[1].saturation=.7f;recipe.pins[5].brightness=-.25f;
    HslSelection first;first.id="first";first.allColors=true;first.own={true,false,true};
    first.areaEnabled=true;first.areaSoftness=.04f;
    first.outline={{.1f,.1f},{.9f,.1f},{.9f,.4f},{.4f,.4f},{.4f,.9f},{.1f,.9f}};
    first.bands={{-5,2,.5f}};first.excludedBands={{-.2f,.2f,.1f}};
    for(int i=0;i<8;++i)first.values[i]={i%2?-180.f:0.f,.1f*i,.04f*i};
    auto second=first;second.id="second";second.own={false,true,false};second.areaSoftness=0;
    second.influence=.6f;second.bands.clear();second.excludedBands.clear();
    // A following global selection must still restore the composed result.
    HslSelection restore;restore.id="restore";restore.allColors=true;restore.influence=1;
    restore.areaEnabled=true;restore.outline={{.6f,.6f},{.8f,.6f},{.8f,.8f},{.6f,.8f}};
    recipe.hslSelections={first,second,restore};
    RawColorWarpPin disabled;disabled.enabled=false;recipe.pins.push_back(disabled);
    RawColorWarpPin move;move.sourceA=.2f;move.sourceB=.1f;move.moveColors=true;move.hueShift=15;
    move.saturation=.2f;move.brightness=.1f;move.hueRange.enabled=false;move.regionId="region";
    recipe.pins.push_back(move);
    auto protect=move;protect.moveColors=false;protect.protectColor=true;protect.strength=.3f;
    recipe.pins.push_back(protect);
    RawColorWarpRegion region;region.id="region";region.spatialSupport=.7f;recipe.regions.push_back(region);
    std::vector<RawColorWarpMaskSample> masks(recipe.pins.size());
    masks[9]={.45f,.2f,.75f,false};masks[10]={.6f,.1f,.9f,true};
    const std::array<std::array<float,3>,8> samples{{{0,0,0},{.18f,.18f,.18f},{.6f,.1f,.04f},
        {.05f,.6f,.2f},{.1f,.2f,.8f},{4,2,1},{-.01f,.2f,.1f},{1e-8f,2e-8f,1e-8f}}};
    const std::array<std::array<float,2>,6> positions{{{.02f,.02f},{.1f,.1f},{.3f,.3f},{.7f,.3f},{.3f,.7f},{.7f,.7f}}};
    for(int version:{4,5,6})for(bool limited:{false,true})for(bool blend:{false,true}) {
        recipe.version=version;recipe.limitHslToSelections=limited;
        recipe.combine=blend?RawColorWarpCombine::Blend:RawColorWarpCombine::Accumulate;
        const auto prepared=PrepareColorWarpEvaluation(recipe);
        for(auto space:{Raw::RawWorkingSpace::LinearSrgbD65,Raw::RawWorkingSpace::LinearRec2020D65})
            for(const auto& rgb:samples)for(const auto& uv:positions) {
                const auto coordinate=WorkingRgbToColorWarpCoordinate(rgb,space);
                const auto global=ReferenceHslCurve(prepared.globalHsl,ColorWarpHue(coordinate.a,coordinate.b));
                Require(ResolveHslSelections(recipe,coordinate,global,uv,&prepared.selections)==
                    ReferenceHslSelections(recipe,coordinate,global,uv),"Prepared HSL selection composition changed");
                for(bool masked:{false,true}) {
                    const auto* fields=masked?&masks:nullptr;
                    Require(ApplyPreparedColorWarp(prepared,rgb,space,fields,uv)==
                        ApplyPreparedColorWarp(recipe,rgb,space,fields,uv),"Prepared Color Warp changed pixels or mask indexing");
                }
            }
    }
    const auto coefficients=PrepareColorWarpHsl(recipe);
    const auto own=HslOwnCoefficients(first);
    for(const auto& anchor:coefficients)for(float offset:{-360.f,-.0001f,0.f,.0001f,360.f}) {
        const float hue=anchor[0]+offset;const auto segment=FindColorWarpHslSegment(coefficients,hue);
        Require(EvaluateColorWarpHsl(coefficients,segment)==ReferenceHslCurve(coefficients,hue) &&
            EvaluateColorWarpHsl(own,segment)==ReferenceHslCurve(own,hue),"Shared hue segment changed seam or half-turn interpolation");
    }
    const auto prepared=PrepareColorWarpEvaluation(recipe);
    const auto captured=ApplyPreparedColorWarp(prepared,samples[2],Raw::RawWorkingSpace::LinearSrgbD65,nullptr,positions[2]);
    recipe.pins.clear();recipe.hslSelections.clear();recipe.enabled=false;
    Require(ApplyPreparedColorWarp(prepared,samples[2],Raw::RawWorkingSpace::LinearSrgbD65,nullptr,positions[2])==captured,
        "Prepared batch observed a later edit to its source recipe");
}

void HslStageIdentity() {
    RawDevelopmentRecipe recipe;recipe.colorWarp.version=6;recipe.colorWarp.pins[0].hueShift=25;
    HslSelection selection;selection.id="selection";selection.bands={{-2,2,.5f}};
    selection.excludedBands={{0,1,.2f}};selection.areaEnabled=true;
    selection.outline={{.1f,.1f},{.8f,.1f},{.8f,.8f},{.1f,.8f}};
    selection.own={true,true,true};selection.values[0]={30,.2f,.1f};
    recipe.colorWarp.hslSelections={selection};
    const auto before=BuildStageFingerprints(recipe,1024);
    const auto changed=[&](const auto& edit) {
        auto next=recipe;edit(next.colorWarp);const auto after=BuildStageFingerprints(next,1024);
        SameEarlyStages(before,after);
        Require(before.postColorWarp!=after.postColorWarp && before.postViewTransform!=after.postViewTransform &&
            before.postOutputCrop!=after.postOutputCrop,"HSL edit reused stale Color Warp output");
    };
    changed([](auto& c){c.limitHslToSelections=true;});
    changed([](auto& c){c.hslSelections[0].enabled=false;});
    changed([](auto& c){c.hslSelections[0].hue+=10;});
    changed([](auto& c){c.hslSelections[0].hueWidth+=1;});
    changed([](auto& c){c.hslSelections[0].hueSoftness+=1;});
    changed([](auto& c){c.hslSelections[0].allColors=true;});
    changed([](auto& c){c.hslSelections[0].areaEnabled=false;});
    changed([](auto& c){c.hslSelections[0].areaSoftness+=.02f;});
    changed([](auto& c){c.hslSelections[0].influence=.3f;});
    changed([](auto& c){c.hslSelections[0].bands[0].low-=1;});
    changed([](auto& c){c.hslSelections[0].bands[0].high+=1;});
    changed([](auto& c){c.hslSelections[0].bands[0].softness+=1;});
    changed([](auto& c){c.hslSelections[0].bands.push_back({4,5,.1f});});
    changed([](auto& c){c.hslSelections[0].excludedBands[0].low-=1;});
    changed([](auto& c){c.hslSelections[0].excludedBands[0].high+=1;});
    changed([](auto& c){c.hslSelections[0].excludedBands[0].softness+=1;});
    changed([](auto& c){c.hslSelections[0].excludedBands.clear();});
    changed([](auto& c){c.hslSelections[0].outline[0][0]+=.03f;});
    changed([](auto& c){c.hslSelections[0].outline[0][1]+=.03f;});
    changed([](auto& c){c.hslSelections[0].outline.pop_back();});
    for(int channel=0;channel<3;++channel) {
        changed([&](auto& c){c.hslSelections[0].own[channel]=false;});
        changed([&](auto& c){c.hslSelections[0].values[7][channel]+=.1f;});
    }
    changed([](auto& c){c.hslSelections.clear();});
    auto named=recipe;named.colorWarp.hslSelections[0].id="renamed-id";named.colorWarp.hslSelections[0].name="Renamed";
    Require(BuildStageFingerprints(named,1024).postColorWarp==before.postColorWarp,
        "HSL display metadata invalidated pixel caches");
    auto second=selection;second.influence=.2f;recipe.colorWarp.hslSelections.push_back(second);
    const auto ordered=BuildStageFingerprints(recipe,1024);
    std::swap(recipe.colorWarp.hslSelections[0],recipe.colorWarp.hslSelections[1]);
    Require(BuildStageFingerprints(recipe,1024).postColorWarp!=ordered.postColorWarp,
        "HSL selection compositing order was absent from the cache key");
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
    auto& pin=recipe.colorWarp.pins.front();pin.hueShift=pin.saturation=pin.brightness=0;
    Require(ColorWarpDestination(pin)==std::array<float,2>{pin.sourceA,pin.sourceB},"Neutral Move changed destination");
    pin.regionId.clear();pin.hueRange.enabled=false;pin.saturationRange.enabled=false;
    RawColorWarpCoordinate input;input.lightness=.7f;input.a=.15f;input.b=.1f;input.sceneEv=0;
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
    soft.brightness=.4f;softRecipe.pins={soft};
    std::vector<RawColorWarpMaskSample> masks{{.75f,0,1,true}};
    const std::array<float,3> hdr{4.f,2.f,1.f};
    const auto bright=ApplyPreparedColorWarp(softRecipe,hdr,Raw::RawWorkingSpace::LinearSrgbD65,&masks);
    Require(bright[0]>4 && std::abs(bright[0]/hdr[0]-bright[1]/hdr[1])<1e-5f,"Exposure clipped HDR or altered color ratios");
    PolicyTransitions();
    UnaffectedRecipes();
    PreparedEvaluationParity();
    HslStageIdentity();
    std::cout << "Color Warp cache policy and prepared evaluation: stage boundaries, ordered HSL, masks and snapshot ownership passed.\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
