#include "ColorWarpMath.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
namespace {
constexpr float pi=3.14159265358979323846f;
float Finite(float x,float fallback) { return std::isfinite(x)?x:fallback; }
float Smooth(float x) { x=std::clamp(x,0.f,1.f); return x*x*(3.f-2.f*x); }
void SanitizeRange(RawColorWarpRange& r,float lo,float hi) {
    r.low=std::clamp(Finite(r.low,lo),lo,hi);
    r.high=std::clamp(Finite(r.high,hi),r.low,hi);
    r.featherLow=std::clamp(Finite(r.featherLow,0),0.f,hi-lo);
    r.featherHigh=std::clamp(Finite(r.featherHigh,0),0.f,hi-lo);
}
const std::array<std::array<float,3>,8> familyRgb{{
    {1,0,0},{1,.21404114f,0},{1,1,0},{0,1,0},
    {0,1,1},{0,0,1},{.21404114f,0,1},{1,0,1}}};
}
float ColorWarpHue(float a,float b) { return std::atan2(b,a)*180.f/pi; }
float ColorWarpHueDelta(float degrees) {
    return degrees-360.f*std::floor((degrees+180.f)/360.f);
}
std::array<float,2> ColorWarpSelectionCoordinate(const RawColorWarpCoordinate& input) {
    const float scale=1.f/std::max(std::abs(input.lightness),.000001f);
    return {input.a*scale,input.b*scale};
}
std::array<float,2> ColorWarpDestination(const RawColorWarpPin& pin) {
    if(pin.hueShift==0 && pin.saturation==0)return {pin.sourceA+pin.tintA,pin.sourceB+pin.tintB};
    const float angle=(ColorWarpHue(pin.sourceA,pin.sourceB)+pin.hueShift)*pi/180.f;
    const float chroma=std::hypot(pin.sourceA,pin.sourceB)*std::max(0.f,1.f+pin.saturation);
    return {chroma*std::cos(angle)+pin.tintA,chroma*std::sin(angle)+pin.tintB};
}
float ColorWarpRangeWeight(const RawColorWarpRange& r,float x) {
    if(!r.enabled)return 1.f;
    if(x<r.low)return r.featherLow>0?Smooth((x-r.low+r.featherLow)/r.featherLow):0.f;
    if(x>r.high)return r.featherHigh>0?Smooth((r.high+r.featherHigh-x)/r.featherHigh):0.f;
    return 1.f;
}
bool ColorWarpHasAdjustment(const RawColorWarpPin& p) {
    return p.hueShift!=0 || p.saturation!=0 || p.brightness!=0 || p.tintA!=0 || p.tintB!=0;
}
bool ColorWarpParticipates(const RawColorWarpPin& p) {
    return p.enabled && p.strength>0 && (p.protectColor || ColorWarpHasAdjustment(p));
}
float EvaluateColorWarpPinColorWeight(const RawColorWarpPin& pin,const RawColorWarpCoordinate& input) {
    const auto c=ColorWarpSelectionCoordinate(input);
    const float chroma=std::hypot(c[0],c[1]);
    // A continuous neutral guard prevents undefined hue from selecting gray.
    const float neutral=pin.sampleRadius>0 ? 1.f : Smooth((chroma-.02f)/.08f);
    const float hue=ColorWarpHueDelta(ColorWarpHue(c[0],c[1])-ColorWarpHue(pin.sourceA,pin.sourceB));
    // Test adjacent turns as well so feathered ranges remain continuous at red.
    const float hueWeight=std::max({ColorWarpRangeWeight(pin.hueRange,hue),
        ColorWarpRangeWeight(pin.hueRange,hue-360.f),ColorWarpRangeWeight(pin.hueRange,hue+360.f)});
    const float radial=pin.sampleRadius>0 ? 1.f-Smooth((std::hypot(c[0]-pin.sourceA,c[1]-pin.sourceB)/pin.sampleRadius-.25f)/.75f) : 1.f;
    float membership=radial*neutral;
    if(!pin.population.empty()) {
        membership=0;
        for(const auto& p:pin.population) {
            const float dc=std::hypot(c[0]-p.a,c[1]-p.b)/(pin.sampleRadius>0?pin.sampleRadius:std::max(.04f,std::hypot(p.a,p.b)*.4f));
            const float de=(input.sceneEv-p.sceneEv)/std::max(.1f,pin.brightnessSoftness);
            membership=std::max(membership,1.f-Smooth((std::sqrt(dc*dc+de*de)-.25f)/.75f));
        }
    }
    const float shadow=Smooth((input.sceneEv+16.f)/4.f);
    return membership*shadow*hueWeight*ColorWarpRangeWeight(pin.saturationRange,chroma);
}
float EvaluateColorWarpPinLightnessWeight(const RawColorWarpPin& pin,float ev) {
    return ColorWarpRangeWeight(pin.exposureRange,ev);
}
bool SetColorWarpSample(RawColorWarpPin& pin,const RawColorWarpCoordinate& input) {
    const auto c=ColorWarpSelectionCoordinate(input);
    const float chroma=std::hypot(c[0],c[1]);
    if(!std::isfinite(chroma) || input.lightness<.01f)return false;
    pin.sourceA=c[0];pin.sourceB=c[1];pin.family=-1;
    pin.hueRange={-7.5f,7.5f,7.5f,7.5f,true};
    pin.saturationRange={chroma*.75f,chroma*1.25f,chroma*.25f,chroma*.25f,true};
    pin.sampleRadius=std::max(.04f,chroma*.4f);
    pin.hueRange.enabled=pin.saturationRange.enabled=false;
    pin.smoothSelection=.65f;
    pin.exposureRange.enabled=false;
    pin.population={{c[0],c[1],input.sceneEv,1.f}};
    return true;
}
const char* ColorWarpFamilyName(int family) {
    static constexpr const char* names[]{"Red","Orange","Yellow","Green","Aqua","Blue","Purple","Magenta"};
    return names[std::clamp(family,0,7)];
}
RawColorWarpPin MakeColorWarpFamily(int family) {
    family=std::clamp(family,0,7);
    RawColorWarpPin p;
    p.family=family;p.id="family-"+std::to_string(family);p.name=ColorWarpFamilyName(family);
    const auto c=ColorWarpSelectionCoordinate(WorkingRgbToColorWarpCoordinate(familyRgb[family],Raw::RawWorkingSpace::LinearSrgbD65));
    p.sourceA=c[0];p.sourceB=c[1];
    const auto prev=ColorWarpSelectionCoordinate(WorkingRgbToColorWarpCoordinate(familyRgb[(family+7)%8],Raw::RawWorkingSpace::LinearSrgbD65));
    const auto next=ColorWarpSelectionCoordinate(WorkingRgbToColorWarpCoordinate(familyRgb[(family+1)%8],Raw::RawWorkingSpace::LinearSrgbD65));
    p.hueRange={0,0,std::abs(ColorWarpHueDelta(ColorWarpHue(c[0],c[1])-ColorWarpHue(prev[0],prev[1]))),
        std::abs(ColorWarpHueDelta(ColorWarpHue(next[0],next[1])-ColorWarpHue(c[0],c[1]))),true};
    p.smoothSelection=.65f;
    return p;
}
std::size_t ColorWarpSampleCount(const RawColorWarpRecipe& recipe) {
    return std::count_if(recipe.pins.begin(),recipe.pins.end(),[](const auto& p){return p.family<0;});
}
void SanitizeColorWarpPin(RawColorWarpPin& p) {
    p.family=std::clamp(p.family,-1,7);
    p.brightnessSoftness=std::clamp(Finite(p.brightnessSoftness,.75f),.1f,8.f);
    p.tintA=std::clamp(Finite(p.tintA,0),-1.f,1.f);
    p.tintB=std::clamp(Finite(p.tintB,0),-1.f,1.f);
    if(p.population.size()>kMaxRawColorWarpPopulation)p.population.resize(kMaxRawColorWarpPopulation);
    for(auto& point:p.population) {
        point.a=std::clamp(Finite(point.a,0),-100.f,100.f);
        point.b=std::clamp(Finite(point.b,0),-100.f,100.f);
        point.sceneEv=std::clamp(Finite(point.sceneEv,0),-32.f,32.f);
        point.mass=std::clamp(Finite(point.mass,1),0.f,1.f);
    }
    p.sourceA=std::clamp(Finite(p.sourceA,.3f),-100.f,100.f);
    p.sourceB=std::clamp(Finite(p.sourceB,0),-100.f,100.f);
    SanitizeRange(p.hueRange,-180,180);SanitizeRange(p.saturationRange,0,100);
    SanitizeRange(p.exposureRange,-32,32);
    p.hueShift=std::clamp(Finite(p.hueShift,0),-180.f,180.f);
    p.sampleRadius=std::clamp(Finite(p.sampleRadius,0),0.f,2.f);
    p.smoothSelection=std::clamp(Finite(p.smoothSelection,0),0.f,1.f);
    p.saturation=std::clamp(Finite(p.saturation,0),-1.f,5.f);
    p.brightness=std::clamp(Finite(p.brightness,0),-1.f,1.f);
    p.strength=std::clamp(Finite(p.strength,1),0.f,2.f);
}
}
