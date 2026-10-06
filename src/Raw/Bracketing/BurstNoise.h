#pragma once
#include "Raw/MultiFrameDenoise/NoiseModel.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
// Each observation is a temporal difference of two 2x2 same-color Haar
// details, divided by the square root of its known gain-squared sum. A
// spatially linear scene cancels, and a constant black offset cancels too.
struct BurstNoiseBlock { double signal=0,variance=0; };
struct BurstNoiseFitReport {
    double minimum=0,maximum=0,rootMeanSquareError=0;
    std::size_t bins=0;bool limitedRange=true;
};

inline double MedianNoiseValue(std::vector<double> values) {
    if(values.empty()) return 0;
    const auto middle=values.begin()+values.size()/2;
    std::nth_element(values.begin(),middle,values.end());
    const double upper=*middle;
    return values.size()%2?upper:.5*(upper+*std::max_element(values.begin(),middle));
}

inline bool MeasureBurstNoiseBlock(const std::vector<double>& details,double signal,
    BurstNoiseBlock& block) {
    if(details.size()<32||!std::isfinite(signal)) return false;
    const double center=MedianNoiseValue(details);
    auto deviations=details;
    for(auto& value:deviations) value=std::abs(value-center);
    const double sigma=MedianNoiseValue(std::move(deviations))/.6744897501960817;
    // Small-sample MAD correction, negligible for large blocks.
    block={std::max(0.0,signal),sigma*sigma*(1+3./details.size())};
    return std::isfinite(block.variance)&&block.variance>0;
}

inline bool FitBurstNoise(std::vector<BurstNoiseBlock> blocks,
    const Mfd::SiteNoiseProfile& previous,Mfd::SiteNoiseProfile& profile,BurstNoiseFitReport* report=nullptr) {
    if(report) *report={};
    if(blocks.size()<24) return false;
    std::sort(blocks.begin(),blocks.end(),[](const auto& a,const auto& b){return a.signal<b.signal;});
    std::vector<Mfd::NoiseCalibrationObservation> observations;
    const std::size_t perBin=std::max<std::size_t>(12,blocks.size()/8);
    for(std::size_t begin=0;begin<blocks.size();begin+=perBin) {
        std::vector<double> signals,variances;
        for(std::size_t i=begin;i<std::min(blocks.size(),begin+perBin);++i) {
            signals.push_back(blocks[i].signal);variances.push_back(blocks[i].variance);
        }
        if(signals.size()<8) continue;
        // A median across spatially distributed blocks resists minority moving
        // edges without selecting the smallest random noise fluctuations.
        observations.push_back({MedianNoiseValue(signals),MedianNoiseValue(variances),double(signals.size())});
    }
    if(observations.size()<2) return false;
    profile=previous;
    const double span=observations.back().meanSignal-observations.front().meanSignal;
    Mfd::NoiseFitDiagnostics fit;
    if(span>.02&&Mfd::FitPoissonGaussianProfile(observations,
        previous.quantizationVariance,profile,fit,nullptr)) {
        profile.quantizationVariance=previous.quantizationVariance;
        if(report) report->limitedRange=false;
    } else {
        // A narrow signal range cannot identify two independent coefficients.
        // Preserve the prior's shot/read ratio but calibrate its total noise
        // to the measured range. Merely fitting a nonnegative intercept would
        // leave an excessive generic shot coefficient entirely uncorrected.
        std::vector<double> ratios;
        for(const auto& o:observations) {
            const double modeled=previous.shotScale*o.meanSignal+previous.offsetVariance;
            if(modeled>0) ratios.push_back(o.variance/modeled);
        }
        if(ratios.empty()) return false;
        const double factor=MedianNoiseValue(ratios);
        profile.shotScale=previous.shotScale*factor;
        profile.offsetVariance=std::max(previous.quantizationVariance,previous.offsetVariance*factor);
    }
    profile.quantizationIncluded=true;
    if(report) {
        report->minimum=observations.front().meanSignal;report->maximum=observations.back().meanSignal;
        report->bins=observations.size();double error=0;
        for(const auto& observation:observations) {
            const double residual=observation.variance-profile.shotScale*observation.meanSignal-profile.offsetVariance;
            error+=residual*residual;
        }
        report->rootMeanSquareError=std::sqrt(error/observations.size());
    }
    return Mfd::ValidateSiteNoiseProfile(profile,nullptr);
}
}
