#include "Raw/Detail/DetailContrast.h"
#include <algorithm>
#include <cmath>

namespace Stack::RawRecipe {
namespace {
float Bound(float x,float fallback,float lo,float hi){return std::isfinite(x)?std::clamp(x,lo,hi):fallback;}
float Number(const nlohmann::json& j,const char* key,float fallback){const auto i=j.find(key);return i!=j.end()&&i->is_number()?i->get<float>():fallback;}
}
DetailContrast SanitizeDetailContrast(DetailContrast d) {
    d.maximumScale=Bound(d.maximumScale,256,16,1024);
    d.edgeProtection=Bound(d.edgeProtection,.75f,0,1);
    d.targetEv=Bound(d.targetEv,0,-16,16);
    d.targetHalfWidthEv=Bound(d.targetHalfWidthEv,2,.125f,12);
    d.targetFeatherEv=Bound(d.targetFeatherEv,2,.125f,12);
    for(auto& g:d.scaleGains) g=Bound(g,1,0,3);
    for(auto& g:d.evScaleResidual) g=Bound(g,0,-3,3);
    return d;
}
DetailContrast ReadDetailContrast(const nlohmann::json& j) {
    DetailContrast d;if(!j.is_object())return d;
    if(j.contains("enabled")&&j["enabled"].is_boolean())d.enabled=j["enabled"].get<bool>();
    if(j.contains("targetEnabled")&&j["targetEnabled"].is_boolean())d.targetEnabled=j["targetEnabled"].get<bool>();
    d.maximumScale=Number(j,"maximumScale",256);d.edgeProtection=Number(j,"edgeProtection",.75f);
    d.targetEv=Number(j,"targetEv",0);d.targetHalfWidthEv=Number(j,"targetHalfWidthEv",2);d.targetFeatherEv=Number(j,"targetFeatherEv",2);
    const auto read=[&](const char* key,auto& values){
        if(j.contains(key)&&j[key].is_array())for(std::size_t i=0;i<values.size()&&i<j[key].size();++i)
            if(j[key][i].is_number())values[i]=j[key][i].template get<float>();
    };
    read("scaleGains",d.scaleGains);read("evScaleResidual",d.evScaleResidual);
    return SanitizeDetailContrast(d);
}
nlohmann::json SerializeDetailContrast(const DetailContrast& input) {
    const auto d=SanitizeDetailContrast(input);
    return {{"enabled",d.enabled},{"maximumScale",d.maximumScale},{"edgeProtection",d.edgeProtection},
        {"targetEnabled",d.targetEnabled},{"targetEv",d.targetEv},{"targetHalfWidthEv",d.targetHalfWidthEv},
        {"targetFeatherEv",d.targetFeatherEv},{"scaleGains",d.scaleGains},{"evScaleResidual",d.evScaleResidual}};
}
bool IsDetailContrastActive(const DetailContrast& d) {
    return d.enabled&&(std::any_of(d.scaleGains.begin(),d.scaleGains.end(),[](float g){return std::abs(g-1)>1e-6f;})||
        std::any_of(d.evScaleResidual.begin(),d.evScaleResidual.end(),[](float g){return std::abs(g)>1e-6f;}));
}
float DetailBandScale(const DetailContrast& d,int band){return 2*std::pow(d.maximumScale/2,float(std::clamp(band,0,kDetailBands-1))/(kDetailBands-1));}
float EvaluateDetailGain(const DetailContrast& d,int band,float ev) {
    if(!d.enabled)return 1;
    band=std::clamp(band,0,kDetailBands-1);
    const float index=std::clamp((ev-kDetailMinEv)/(kDetailMaxEv-kDetailMinEv)*(kDetailEvSamples-1),0.f,float(kDetailEvSamples-1));
    const int low=int(index),high=std::min(low+1,kDetailEvSamples-1);const float f=index-low;
    const float residual=d.evScaleResidual[low*kDetailBands+band]*(1-f)+d.evScaleResidual[high*kDetailBands+band]*f;
    float weight=1;
    if(d.targetEnabled){float u=std::clamp((std::abs(ev-d.targetEv)-d.targetHalfWidthEv)/d.targetFeatherEv,0.f,1.f);weight=1-u*u*(3-2*u);}
    return 1+(std::clamp(d.scaleGains[band]+residual,0.f,3.f)-1)*weight;
}
std::array<float,kDetailBands*kDetailEvSamples> BuildDetailGainMap(const DetailContrast& d) {
    std::array<float,kDetailBands*kDetailEvSamples> map;
    for(int y=0;y<kDetailEvSamples;++y)for(int x=0;x<kDetailBands;++x)
        map[y*kDetailBands+x]=d.scaleGains[x]+d.evScaleResidual[y*kDetailBands+x];
    return map;
}
void AdjustDetailMacro(DetailContrast& d,int first,int last,float delta) {
    for(int i=std::max(0,first);i<=std::min(kDetailBands-1,last);++i)d.scaleGains[i]=std::clamp(d.scaleGains[i]+delta,0.f,3.f);
}
}
