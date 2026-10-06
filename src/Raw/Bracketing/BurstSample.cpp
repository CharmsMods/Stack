#include "BurstSample.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
Observation ReadBurstSample(const PreparedDataset& data,const PreparedSource& source,
    PreparedTileSampler& sampler,unsigned x,unsigned y,double pilot,std::string& error,
    const Mfd::RawSignalGradient* sceneGradient) {
    Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
    const bool reference=&source==&data.sources[data.origin];
    if(!reference&&(source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||source.alignmentDiagnostic.localApplied))
        return SampleAlignedCapture(source,layout,sampler,x,y,pilot,
            sceneGradient?*sceneGradient:Mfd::RawSignalGradient{std::numeric_limits<double>::quiet_NaN(),0},error);
    Observation sample;Mfd::SameCfaTapInput tap;
    if(!sampler.ReadSample(source.frame,x,y,tap,&error))return sample;
    const double gain=tap.comparisonGain*source.scale;
    sample.value=sample.fallback=tap.normalizedSample*gain;sample.exposure=1/source.scale;
    sample.finite=std::isfinite(sample.value)&&std::isfinite(gain)&&gain>0&&
        !Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Defective)&&
        !Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::DecoderRepaired);
    sample.clipped=Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Saturated)||
        Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
    sample.fallbackClipped=sample.clipped;
    if(!sample.finite||sample.clipped)return sample;
    const auto site=std::size_t(layout.SiteAt(x,y));const auto& profile=source.noise.sites[site];
    Mfd::GainPropagatedVariance variance;
    if(!Mfd::PropagateKnownGainVariance(profile,gain,std::max(0.,pilot),
        source.frame.calibration.usableSpanByCfaSite[site],variance,&error))return {};
    sample.measurementVariance=std::max(1e-12,variance.variance);
    sample.uncertaintyVariance=Mfd::ResidualModelVariance(profile,pilot)+
        pilot*pilot*source.scaleVariance/(source.scale*source.scale);
    sample.variance=sample.measurementVariance+sample.uncertaintyVariance;
    sample.headroom=std::clamp((.995-tap.normalizedSample)/.045,0.,1.);
    sample.headroom*=sample.headroom*(3-2*sample.headroom);
    sample.support=1;sample.fixedReference=reference;return sample;
}
}
