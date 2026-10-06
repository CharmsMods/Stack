#include "Raw/Tone/SceneTone.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
namespace {
float Finite(float v, float fallback, float lo, float hi) {
    return std::isfinite(v) ? std::clamp(v,lo,hi) : fallback;
}
float Number(const nlohmann::json& j, const char* key, float fallback) {
    const auto i=j.find(key);
    return i!=j.end() && i->is_number() ? i->get<float>() : fallback;
}
float BaseSlope(const SceneTone& t, float e) {
    if(e<=t.points.front().ev) return t.points.front().contrast;
    for(std::size_t i=1;i<t.points.size();++i) {
        const auto& a=t.points[i-1]; const auto& b=t.points[i];
        if(e<=b.ev) return a.contrast+(b.contrast-a.contrast)*(e-a.ev)/(b.ev-a.ev);
    }
    return t.points.back().contrast;
}
double BaseIntegral(const SceneTone& t, double e) {
    // Primitive relative to the first point, with linear extensions.
    double result=0, last=t.points.front().ev;
    if(e<=last) return (e-last)*t.points.front().contrast;
    for(std::size_t i=1;i<t.points.size();++i) {
        const auto& a=t.points[i-1]; const auto& b=t.points[i];
        const double dx=std::min(e,double(b.ev))-last;
        result+=dx*a.contrast+0.5*dx*dx*(b.contrast-a.contrast)/(b.ev-a.ev);
        if(e<=b.ev) return result;
        last=b.ev;
    }
    return result+(e-last)*t.points.back().contrast;
}
double Shape(const SceneTone& t,double e) {
    const double d=std::abs(e-t.pivotEv)-t.coreHalfWidthEv;
    if(d<=0) return 1;
    if(d>=t.transitionEv) return 0;
    const double u=d/t.transitionEv;
    const double k=t.coreHalfWidthEv/t.transitionEv+0.5;
    return 1-u*u*(3-2*u)-t.outerProtection*k*30*u*u*(1-u)*(1-u);
}
double ShapeIntegral(const SceneTone& t,double e) {
    const double d=std::abs(e-t.pivotEv), h=t.coreHalfWidthEv, w=t.transitionEv;
    const double u=std::clamp((d-h)/w,0.0,1.0);
    const double u2=u*u,u3=u2*u,u4=u3*u,u5=u4*u;
    const double ramp=u-u3+0.5*u4;
    const double compensation=10*u3-15*u4+6*u5;
    const double area=std::min(d,h)+w*ramp-t.outerProtection*(h+0.5*w)*compensation;
    return e<t.pivotEv ? -area : area;
}
}

SceneTone SanitizeSceneTone(SceneTone t) {
    t.anchorOffsetEv=Finite(t.anchorOffsetEv,0,-12,12);
    t.contrast=Finite(t.contrast,1,0.05f,4);
    t.pivotEv=Finite(t.pivotEv,0,-12,12);
    t.coreHalfWidthEv=Finite(t.coreHalfWidthEv,1,0.125f,8);
    t.transitionEv=Finite(t.transitionEv,2,0.25f,12);
    t.outerProtection=Finite(t.outerProtection,0,0,1);
    for(auto& p:t.points) { p.ev=Finite(p.ev,0,-24,24); p.contrast=Finite(p.contrast,1,0.05f,8); }
    std::stable_sort(t.points.begin(),t.points.end(),[](auto a,auto b){return a.ev<b.ev;});
    t.points.erase(std::unique(t.points.begin(),t.points.end(),[](auto a,auto b){return b.ev-a.ev<0.05f;}),t.points.end());
    if(t.points.size()>32) t.points.resize(32);
    if(t.points.size()<2) t.points={{-16,1},{16,1}};
    return t;
}
SceneTone ReadSceneTone(const nlohmann::json& j) {
    SceneTone t;
    if(!j.is_object()) return t;
    if(j.contains("enabled")&&j["enabled"].is_boolean()) t.enabled=j["enabled"].get<bool>();
    t.anchorOffsetEv=Number(j,"anchorOffsetEv",0); t.contrast=Number(j,"contrast",1);
    t.pivotEv=Number(j,"pivotEv",0); t.coreHalfWidthEv=Number(j,"coreHalfWidthEv",1);
    t.transitionEv=Number(j,"transitionEv",2); t.outerProtection=Number(j,"outerProtection",0);
    if(j.contains("points")&&j["points"].is_array()) {
        t.points.clear();
        for(const auto& p:j["points"]) {
            if(p.is_object()) t.points.push_back({Number(p,"ev",0),Number(p,"contrast",1)});
            if(t.points.size()>=32) break;
        }
    }
    return SanitizeSceneTone(std::move(t));
}
nlohmann::json SerializeSceneTone(const SceneTone& input) {
    const auto t=SanitizeSceneTone(input);
    nlohmann::json j={{"enabled",t.enabled},{"anchorOffsetEv",t.anchorOffsetEv},{"contrast",t.contrast},
        {"pivotEv",t.pivotEv},{"coreHalfWidthEv",t.coreHalfWidthEv},{"transitionEv",t.transitionEv},{"outerProtection",t.outerProtection}};
    j["points"]=nlohmann::json::array();
    for(const auto& p:t.points) j["points"].push_back({{"ev",p.ev},{"contrast",p.contrast}});
    return j;
}
bool IsSceneToneActive(const SceneTone& t) {
    return t.enabled && (std::abs(t.anchorOffsetEv)>1e-7f || std::abs(t.contrast-1)>1e-7f ||
        std::any_of(t.points.begin(),t.points.end(),[](auto p){return std::abs(p.contrast-1)>1e-7f;}));
}
float EffectiveSceneToneContrast(const SceneTone& t) {
    float minimum=8;
    for(const auto& p:t.points) minimum=std::min(minimum,p.contrast);
    double delta=t.contrast-1;
    const double k=t.outerProtection*(t.coreHalfWidthEv/t.transitionEv+0.5);
    // The transition polynomial has only one internal nontrivial extremum.
    // Use its analytic minimum, not a sample-based positivity test.
    if(delta>0 && k>0.1) {
        const double u=0.5+0.05/k;
        const double minimumShape=Shape(t,t.pivotEv+t.coreHalfWidthEv+u*t.transitionEv);
        if(minimumShape<0) delta=std::min(delta,(minimum-kSceneToneSlopeFloor)/-minimumShape);
    }
    if(delta<0) delta=std::max(delta,double(kSceneToneSlopeFloor-minimum));
    return float(1+delta);
}
float EvaluateSceneToneContrast(const SceneTone& t,float e) {
    if(!t.enabled) return 1;
    return float(BaseSlope(t,e)+(EffectiveSceneToneContrast(t)-1)*Shape(t,e));
}
float EvaluateSceneToneEv(const SceneTone& t,float e) {
    if(!t.enabled) return e;
    return float(t.anchorOffsetEv+BaseIntegral(t,e)-BaseIntegral(t,0)+
        (EffectiveSceneToneContrast(t)-1)*ShapeIntegral(t,e));
}
std::array<float,3> ApplySceneTone(const SceneTone& t,std::array<float,3> rgb,const std::array<float,3>& weights) {
    if(!IsSceneToneActive(t)) return rgb;
    const double y=double(rgb[0])*weights[0]+double(rgb[1])*weights[1]+double(rgb[2])*weights[2];
    // Undefined logarithms pass through. Signed channels with positive Y keep
    // their signs and ratios; no per-channel black clamp is introduced.
    if(!(y>0) || !std::isfinite(y)) return rgb;
    const float e=float(std::log2(y/0.18));
    const double gain=std::exp2(double(EvaluateSceneToneEv(t,e))-e);
    for(auto& c:rgb) c=float(c*gain);
    return rgb;
}
float SetSceneToneOutput(SceneTone& t,std::size_t index,float target) {
    t=SanitizeSceneTone(std::move(t));
    if(index>=t.points.size()||!std::isfinite(target)) return 0;
    const float e=t.points[index].ev;
    if(std::abs(e)<0.025f) {
        t.anchorOffsetEv=Finite(t.anchorOffsetEv+target-EvaluateSceneToneEv(t,e),0,-12,12);
    } else {
        // The base integral is affine in a slope point. Use its exact basis
        // coefficient and re-evaluate after a macro feasibility limit changes.
        SceneTone basis=t; basis.contrast=1;
        const double a=BaseIntegral(basis,e)-BaseIntegral(basis,0);
        basis.points[index].contrast+=1;
        const double weight=BaseIntegral(basis,e)-BaseIntegral(basis,0)-a;
        if(std::abs(weight)>1e-8) for(int iteration=0;iteration<8;++iteration) {
            const float error=target-EvaluateSceneToneEv(t,e);
            if(std::abs(error)<1e-5f) break;
            t.points[index].contrast=Finite(float(t.points[index].contrast+error/weight),1,0.05f,8);
        }
    }
    return EvaluateSceneToneEv(t,e);
}
std::vector<float> BuildSceneToneLut(const SceneTone& input) {
    const auto t=SanitizeSceneTone(input);
    std::vector<float> lut(kSceneToneLutSize);
    for(int i=0;i<kSceneToneLutSize;++i) {
        const float e=kSceneToneLutMinEv+(kSceneToneLutMaxEv-kSceneToneLutMinEv)*i/(kSceneToneLutSize-1);
        lut[i]=EvaluateSceneToneEv(t,e);
    }
    return lut;
}
} // namespace Stack::RawRecipe
