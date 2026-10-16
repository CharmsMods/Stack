#include "NoiseLayer.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include "Raw/Curves/CurveAuthoring.h"

namespace {
void Read(const json& j,const char* key,float& value){
    const auto found=j.find(key);if(found!=j.end() && found->is_number()){
        const double number=found->get<double>();if(std::isfinite(number) && std::abs(number)<=std::numeric_limits<float>::max())value=float(number);
    }
}
void Read(const json& j,const char* key,int& value){
    const auto found=j.find(key);if(found!=j.end() && found->is_number()){
        const double number=found->get<double>();if(std::isfinite(number))value=int(std::clamp(number,double(std::numeric_limits<int>::min()),double(std::numeric_limits<int>::max())));
    }
}
void Read(const json& j,const char* key,bool& value){const auto found=j.find(key);if(found!=j.end() && found->is_boolean())value=found->get<bool>();}
}
json NoiseLayer::Serialize() const{
    json j{{"type","Noise"},{"strength",m_Strength},{"noiseType",m_Type},{"blendMode",m_BlendMode},
        {"satStrength",m_SatStrength},{"satImpact",m_SatImpact},{"scale",m_Scale},{"opacity",m_Opacity},
        {"blurriness",m_Blurriness},{"seed",m_Seed},{"paramA",m_ParamA},{"paramB",m_ParamB},{"paramC",m_ParamC}};
    auto p=m_Film;p.Sanitize();
    j["filmGrainVersion"]=2;
    j["grainResponseCurve"]=Stack::RawRecipe::CurvePointsJson(p.responseCurve);
    j["grainDensityDeviation"]=p.densityDeviation;j["grainRadiusMicrons"]=p.radiusMicrons;
    j["grainRadiusVariation"]=p.radiusVariation;j["grainMaximumRadiusFactor"]=p.maximumRadiusFactor;
    j["grainFilmWidthMm"]=p.filmWidthMm;j["grainOpticalBlurMicrons"]=p.opticalBlurMicrons;j["grainCoverage"]=p.coverage;
    j["grainColorAmount"]=p.colorAmount;j["grainChannelCorrelation"]=p.channelCorrelation;
    // Keep each channel scalar visible to the ordinary graph parameter sockets.
    for(int i=0;i<3;++i){j[std::string("grainRadius")+"RGB"[i]]=p.channelSize[i];
        j[std::string("grainStrength")+"RGB"[i]]=p.channelStrength[i];}
    j["grainShadows"]=p.shadows;j["grainMidtones"]=p.midtones;j["grainHighlights"]=p.highlights;
    j["grainTonalPivot"]=p.tonalPivot;j["grainExposureFloor"]=p.exposureFloor;j["grainMix"]=p.mix;
    j["grainEncoding"]=p.encoding;j["grainOutput"]=p.output;j["grainSamples"]=p.samples;
    j["grainSeed"]=p.seed;j["grainFrame"]=p.frame;j["grainCompensateMean"]=p.compensateMean;
    return j;
}
void NoiseLayer::Deserialize(const json& j){
    if(!j.is_object())return;
    // Missing mode means an old Noise payload, even when this runtime instance
    // was initially constructed with the new film-grain defaults.
    m_Type=j.contains("noiseType") ? m_Type : 1;
    Read(j,"noiseType",m_Type);m_Type=std::clamp(m_Type,0,13);
    Read(j,"strength",m_Strength);Read(j,"noiseStrength",m_Strength);
    Read(j,"blendMode",m_BlendMode);Read(j,"noiseBlendMode",m_BlendMode);
    Read(j,"satStrength",m_SatStrength);Read(j,"noiseSaturation",m_SatStrength);Read(j,"satImpact",m_SatImpact);
    Read(j,"scale",m_Scale);Read(j,"noiseScale",m_Scale);Read(j,"opacity",m_Opacity);Read(j,"noiseOpacity",m_Opacity);
    Read(j,"blurriness",m_Blurriness);Read(j,"noiseBlurriness",m_Blurriness);Read(j,"seed",m_Seed);
    Read(j,"paramA",m_ParamA);Read(j,"paramB",m_ParamB);Read(j,"paramC",m_ParamC);
    m_Strength=std::clamp(m_Strength,0.f,150.f);m_BlendMode=std::clamp(m_BlendMode,0,5);
    m_SatStrength=std::clamp(m_SatStrength,0.f,4.f);m_SatImpact=std::clamp(m_SatImpact,-100.f,100.f);
    m_Scale=std::clamp(m_Scale,1.f,20.f);m_Opacity=std::clamp(m_Opacity,0.f,1.f);
    m_Blurriness=std::clamp(m_Blurriness,0.f,100.f);m_Seed=std::clamp(m_Seed,0.f,1.f);
    m_ParamA=std::clamp(m_ParamA,0.f,1.f);m_ParamB=std::clamp(m_ParamB,0.f,1.f);m_ParamC=std::clamp(m_ParamC,0.f,1.f);
    auto& p=m_Film;
    Read(j,"grainDensityDeviation",p.densityDeviation);Read(j,"grainRadiusMicrons",p.radiusMicrons);
    Read(j,"grainRadiusVariation",p.radiusVariation);Read(j,"grainMaximumRadiusFactor",p.maximumRadiusFactor);
    Read(j,"grainFilmWidthMm",p.filmWidthMm);Read(j,"grainOpticalBlurMicrons",p.opticalBlurMicrons);Read(j,"grainCoverage",p.coverage);
    Read(j,"grainColorAmount",p.colorAmount);Read(j,"grainChannelCorrelation",p.channelCorrelation);
    for(int i=0;i<3;++i){const auto radius=std::string("grainRadius")+"RGB"[i],strength=std::string("grainStrength")+"RGB"[i];
        Read(j,radius.c_str(),p.channelSize[i]);Read(j,strength.c_str(),p.channelStrength[i]);}
    Read(j,"grainShadows",p.shadows);Read(j,"grainMidtones",p.midtones);Read(j,"grainHighlights",p.highlights);
    Read(j,"grainTonalPivot",p.tonalPivot);Read(j,"grainExposureFloor",p.exposureFloor);Read(j,"grainMix",p.mix);
    Read(j,"grainEncoding",p.encoding);Read(j,"grainOutput",p.output);Read(j,"grainSamples",p.samples);
    Read(j,"grainSeed",p.seed);Read(j,"grainFrame",p.frame);Read(j,"grainCompensateMean",p.compensateMean);
    p.responseCurve=Stack::Renderer::FilmGrain::Parameters{}.responseCurve;
    if(const auto curve=j.find("grainResponseCurve");curve!=j.end())
        p.responseCurve=Stack::RawRecipe::ReadCurvePoints(*curve,0);
    p.Sanitize();
}
