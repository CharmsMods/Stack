#include "FilmGrainParameters.h"
#include <algorithm>
#include <cmath>
#include "Raw/RawDevelopmentRecipe.h"

namespace Stack::Renderer::FilmGrain {
namespace {
float Bounded(float value, float fallback, float lo, float hi) {
    return std::isfinite(value) ? std::clamp(value,lo,hi) : fallback;
}
}
void Parameters::Sanitize() {
    const Parameters defaults;
    densityDeviation=Bounded(densityDeviation,defaults.densityDeviation,0.f,.3f);
    radiusMicrons=Bounded(radiusMicrons,defaults.radiusMicrons,.25f,25.f);
    radiusVariation=Bounded(radiusVariation,defaults.radiusVariation,0.f,.8f);
    maximumRadiusFactor=Bounded(maximumRadiusFactor,defaults.maximumRadiusFactor,1.f,4.f);
    filmWidthMm=Bounded(filmWidthMm,defaults.filmWidthMm,8.f,70.f);
    opticalBlurMicrons=Bounded(opticalBlurMicrons,defaults.opticalBlurMicrons,0.f,20.f);
    coverage=Bounded(coverage,defaults.coverage,.05f,.9f);
    colorAmount=Bounded(colorAmount,defaults.colorAmount,0.f,1.f);
    channelCorrelation=Bounded(channelCorrelation,defaults.channelCorrelation,0.f,1.f);
    for (int i=0;i<3;++i) {
        channelSize[i]=Bounded(channelSize[i],1.f,.5f,2.f);
        channelStrength[i]=Bounded(channelStrength[i],1.f,0.f,3.f);
    }
    shadows=Bounded(shadows,defaults.shadows,0.f,3.f);
    midtones=Bounded(midtones,defaults.midtones,0.f,3.f);
    highlights=Bounded(highlights,defaults.highlights,0.f,3.f);
    tonalPivot=Bounded(tonalPivot,defaults.tonalPivot,.01f,.9f);
    exposureFloor=Bounded(exposureFloor,defaults.exposureFloor,0.f,.1f);
    mix=Bounded(mix,defaults.mix,0.f,1.f);
    encoding=std::clamp(encoding,0,2); output=std::clamp(output,0,2);
    // Timeline values use floats: this integer range remains exactly representable.
    samples=std::clamp(samples,4,64); seed=std::clamp(seed,0,16777215); frame=std::clamp(frame,0,1000000);
    for(auto& point:responseCurve) {
        point.x=Bounded(point.x,0.f,0.f,1.f);point.y=Bounded(point.y,1.f/3.f,0.f,1.f);
        for(auto* handle:{&point.incoming,&point.outgoing}) {
            handle->strength=Bounded(handle->strength,1.f,0.f,1.5f);
            handle->offsetX=Bounded(handle->offsetX,0.f,-1.f,1.f);
            handle->offsetY=Bounded(handle->offsetY,0.f,-1.f,1.f);
        }
    }
    const auto ordered=[](const auto& a,const auto& b){return a.x<b.x;};
    if(!std::is_sorted(responseCurve.begin(),responseCurve.end(),ordered))
        std::stable_sort(responseCurve.begin(),responseCurve.end(),ordered);
    responseCurve.erase(std::unique(responseCurve.begin(),responseCurve.end(),
        [](const auto& a,const auto& b){return a.x==b.x;}),responseCurve.end());
    if(responseCurve.size()<2)responseCurve=defaults.responseCurve;
    if(responseCurve.front().x>0.f){auto first=responseCurve.front();first.x=0;responseCurve.insert(responseCurve.begin(),first);}
    if(responseCurve.back().x<1.f){auto last=responseCurve.back();last.x=1;responseCurve.push_back(last);}
}
bool SameResponseCurve(const std::vector<RawRecipe::RawBezierCurvePoint>& a,
    const std::vector<RawRecipe::RawBezierCurvePoint>& b) {
    if(a.size()!=b.size())return false;
    const auto sameHandle=[](const auto& x,const auto& y){return x.strength==y.strength && x.manual==y.manual &&
        x.offsetX==y.offsetX && x.offsetY==y.offsetY;};
    for(size_t i=0;i<a.size();++i)if(a[i].x!=b[i].x || a[i].y!=b[i].y ||
        !sameHandle(a[i].incoming,b[i].incoming) || !sameHandle(a[i].outgoing,b[i].outgoing))return false;
    return true;
}
float EvaluateResponse(const Parameters& p,float t) {
    t=std::clamp(t,0.f,1.f);
    const auto smooth=[](float x){return x*x*(3.f-2.f*x);};
    const float zone=t<p.tonalPivot ? p.shadows+(p.midtones-p.shadows)*smooth(t/p.tonalPivot) :
        p.midtones+(p.highlights-p.midtones)*smooth((t-p.tonalPivot)/(1.f-p.tonalPivot));
    return zone*3.f*RawRecipe::EvaluateRawBezierCurve(p.responseCurve,t);
}
double ClippedRadiusSecondMoment(double mean, double variation, double maximumFactor) {
    if (variation<1e-6) return mean*mean;
    const double variance=std::log1p(variation*variation), sigma=std::sqrt(variance);
    const double mu=std::log(mean)-.5*variance, maximum=mean*maximumFactor;
    const auto normal=[](double x) { return .5*std::erfc(-x/std::sqrt(2.)); };
    return std::exp(2.*mu+2.*variance)*normal((std::log(maximum)-mu-2.*variance)/sigma)+
        maximum*maximum*(1.-normal((std::log(maximum)-mu)/sigma));
}
}
