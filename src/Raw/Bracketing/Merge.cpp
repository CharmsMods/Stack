#include "ProcessingInternal.h"
#include "CaptureCorrespondence.h"
#include "PreparedTileSampler.h"
#include "PreparedRegion.h"
#include "TemporalGpu.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Bracketing {
Observation SampleAlignedCapture(const PreparedSource& source,Mfd::CfaLayout& layout,PreparedTileSampler& sampler,
    double referenceX,double referenceY,double referencePilot,
    const Mfd::RawSignalGradient& sceneGradient,std::string& error) {
    Observation result;const auto site=layout.SiteAt(static_cast<std::int64_t>(referenceX),static_cast<std::int64_t>(referenceY));
    const auto correspondence=EvaluateCaptureCorrespondence(source,{referenceX,referenceY});
    const auto mapped=correspondence.sourceRaw;
    const auto covariance=correspondence.covarianceRaw;
    const double confidence=correspondence.confidence;
    const auto plane=layout.RawToPlane(mapped,site);const auto extent=layout.PlaneExtent(site,source.frame.activeExtent);
    if(!std::isfinite(plane.x)||!std::isfinite(plane.y)) return result;
    const auto nearestX=static_cast<std::int64_t>(std::llround(plane.x)),nearestY=static_cast<std::int64_t>(std::llround(plane.y));
    Mfd::SameCfaTapInput nearest;bool hasNearest=false;
    if(nearestX>=0&&nearestY>=0&&static_cast<std::uint64_t>(nearestX)<extent.width&&static_cast<std::uint64_t>(nearestY)<extent.height) {
        const auto raw=layout.PlanePixelToRaw({nearestX,nearestY,site});
        hasNearest=sampler.ReadSample(source.frame,static_cast<std::uint64_t>(raw.x),static_cast<std::uint64_t>(raw.y),nearest,&error);
        if(hasNearest&&
            std::isfinite(nearest.normalizedSample)&&std::isfinite(nearest.comparisonGain)&&nearest.comparisonGain>0&&
            !Mfd::HasSampleFlag(nearest.sampleFlags,Mfd::PreparedSampleFlag::Defective)&&
            !Mfd::HasSampleFlag(nearest.sampleFlags,Mfd::PreparedSampleFlag::DecoderRepaired)) {
            const auto normalized=nearest.normalizedSample,gain=nearest.comparisonGain;const auto flags=nearest.sampleFlags;
            result.finite=true;result.fallback=normalized*gain*source.scale;result.exposure=1/source.scale;
            result.clipped=Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
            result.fallbackClipped=result.clipped;
        }
    }
    result.localRejected=confidence<=0;
    Mfd::SameCfaScalarParameters parameters;parameters.exposureScale=source.scale;
    parameters.exposureScaleVariance=source.scaleVariance;
    parameters.referenceComparisonPilot=std::max(0.0,referencePilot);
    parameters.warpCovariance=covariance;
    Mfd::SameCfaSampleResult sampled;CaptureFallback fallback;
    if(!sampler.SampleSameCfa(source.frame,source.noise,mapped,site,parameters,sampled,fallback,&error)) {
        if(fallback.finite) {
            result.fallback=fallback.value;result.finite=true;result.exposure=1/source.scale;
            result.clipped=result.fallbackClipped=fallback.clipped;
        }
        return result;
    }
    // A rejected match still needs a geometrically consistent display
    // fallback. Nearest same-CFA taps jump independently at diagonal edges,
    // creating false color. Keep the normal unclipped interpolation without
    // claiming that its alignment passed or adding any capture support.
    result.fallback=sampled.value;result.finite=true;result.clipped=false;result.fallbackClipped=false;
    if(confidence<=0)return result;
    double headroom=1;
    if(hasNearest) {
        headroom=std::clamp((.995-nearest.normalizedSample)/.045,0.0,1.0);headroom=headroom*headroom*(3-2*headroom);
    }
    result.value=sampled.value;
    // Confidence moderates scene-dependent registration risk, not sensor
    // noise. An uncertain location on a flat surface remains useful evidence.
    double registrationRisk=sampled.registrationVariance;
    if(std::isfinite(sceneGradient.dxPerRawPixel)) {
        if(!Mfd::RegistrationUncertaintyVariance(sceneGradient,covariance,
            registrationRisk,nullptr)) registrationRisk=sampled.registrationVariance;
    }
    result.measurementVariance=std::max(1e-12,sampled.interpolationVariance);
    result.uncertaintyVariance=sampled.residualModelVariance+sampled.exposureScaleVariance+
        registrationRisk/std::max(kUncertainCaptureReliability,confidence);
    result.variance=result.measurementVariance+result.uncertaintyVariance;
    result.headroom=headroom;result.support=1;result.clipped=false;return result;
}

static bool EvaluateTemporalRegion(const ProcessingRequest& request,const PreparedDataset& data,
    std::vector<std::vector<Observation>>& groups,const Mfd::PreparedRawTile& origin,bool detailFusion,std::string& error) {
    if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
    if(!data.combiningGpu) {
        error="GPU capture combining is required, but no compute backend was initialized.";
        return false;
    }
    return data.combiningGpu->Combine(request,data,origin,detailFusion,groups,error);
}

bool ReadTemporalGroups(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    std::vector<std::vector<Observation>>& groups,Mfd::PreparedRawTile& origin,std::string& error) {
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    const auto& frame=data.sources[data.origin].frame;
    if(Mfd::ReadPreparedTile(frame,cache,tx,ty,origin,&error)!=Mfd::TileCacheReadStatus::Hit) return false;
    const bool detailFusion=std::any_of(data.sources.begin(),data.sources.end(),
        [](const auto& source){return source.enabled&&source.detailPreference<.999;});
    if(!detailFusion) return EvaluateTemporalRegion(request,data,groups,origin,false,error);
    const auto left=origin.originX>=4?origin.originX-4:0,top=origin.originY>=4?origin.originY-4:0;
    const auto right=std::min<std::uint64_t>(data.width,origin.originX+origin.extent.width+4);
    const auto bottom=std::min<std::uint64_t>(data.height,origin.originY+origin.extent.height+4);
    PreparedTileSampler sampler(data.directory);Mfd::PreparedRawTile expanded;
    if(!ReadPreparedRegion(frame,sampler,left,top,static_cast<unsigned>(right-left),
        static_cast<unsigned>(bottom-top),expanded,error)) return false;
    std::vector<std::vector<Observation>> full;
    if(!EvaluateTemporalRegion(request,data,full,expanded,true,error)) return false;
    groups.resize(full.size());
    for(auto& group:groups)group.resize(origin.extent.width*origin.extent.height);
    for(std::size_t g=0;g<groups.size();++g) for(unsigned y=0;y<origin.extent.height;++y)
        for(unsigned x=0;x<origin.extent.width;++x)
            groups[g][y*origin.extent.width+x]=full[g][(y+origin.originY-top)*expanded.extent.width+x+origin.originX-left];
    return true;
}
Pixel BlendWithWeights(const std::vector<Observation>& input,const double* requested,std::size_t requestedCount,
    const BracketingRecipe& recipe,
    const std::array<double,64>* colorHeadroom) {
    Pixel p;double total=0;
    if(input.size()!=recipe.groups.size()||input.size()>p.actual.size()||
       (requested&&requestedCount!=input.size())||(!requested&&requestedCount!=0))return p;
    for(std::size_t i=0;i<input.size();++i)
        if(recipe.groups[i].enabled) p.localRejected|=input[i].localRejected;
    for(std::size_t i=0;i<input.size();++i) if(recipe.groups[i].enabled&&input[i].support>0) {
        // BlendColor owns the shared safety mask, including its coordinated
        // near-saturation fallback. Reapplying individual tapers here would
        // split that fallback into different exposure choices per channel.
        const auto headroom=colorHeadroom?(*colorHeadroom)[i]:input[i].headroom;
        p.actual[i]=(requested?requested[i]:1/std::max(1e-12,input[i].variance))*headroom;
        total+=p.actual[i];
    }
    if(total<=1e-20&&requested) {
        p=BlendWithWeights(input,nullptr,0,recipe,colorHeadroom);p.fallback=true;
        if(p.fallbackReason==MeasurementFallbackReason::None)
            p.fallbackReason=MeasurementFallbackReason::AutomaticValidInput;
        return p;
    }
    // A valid sample immediately below saturation is still preferable to clipped data.
    if(total<=1e-20) for(std::size_t i=0;i<input.size();++i) if(recipe.groups[i].enabled&&input[i].support>0) {
        p.actual[i]=1/std::max(1e-12,input[i].variance);total+=p.actual[i];p.fallback=true;
    }
    if(total>0) {
        double effective=0;
        for(std::size_t i=0;i<input.size();++i) {
            const double w=p.actual[i]/=total;
            // Rejected colors can have no finite measurement. A zero weight
            // must exclude them, not turn 0 * NaN into a contaminated result.
            if(w<=0)continue;
            p.value+=w*input[i].value;p.variance+=w*w*input[i].variance;
            p.measurementVariance+=w*w*(input[i].measurementVariance>=0?input[i].measurementVariance:input[i].variance);
            p.uncertaintyVariance+=w*w*input[i].uncertaintyVariance;
            if(input[i].support>0) effective+=w*w/input[i].support;
            if(w>0&&input[i].fixedReference&&p.localRejected)
                p.fallbackReason=MeasurementFallbackReason::FixedReference;
        }
        p.support=effective>0?1/effective:0;p.valid=true;return p;
    }
    std::size_t selected=input.size();double exposure=std::numeric_limits<double>::infinity();
    for(std::size_t i=0;i<input.size();++i) if(recipe.groups[i].enabled&&input[i].finite&&input[i].exposure<exposure) {
        selected=i;exposure=input[i].exposure;
    }
    if(selected<input.size()) {
        p.value=input[selected].fallback;p.clipped=input[selected].fallbackClipped||input[selected].clipped;p.actual[selected]=1;
        p.fallbackReason=MeasurementFallbackReason::ShortestExposure;
    } else p.fallbackReason=MeasurementFallbackReason::NoFiniteMeasurement;
    p.fallback=true;return p;
}
Pixel Blend(const std::vector<Observation>& input,const std::vector<double>& requested,const BracketingRecipe& recipe,
    const std::array<double,64>* colorHeadroom) {
    return BlendWithWeights(input,requested.empty()?nullptr:requested.data(),requested.size(),recipe,colorHeadroom);
}
LuminanceTransform BuildLuminanceTransform(const RawMetadata& metadata) {
    LuminanceTransform transform;
    Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(metadata.cfaPattern,layout);
    for(unsigned i=0;i<4;++i) {
        const auto site=layout.SiteAt(i%2,i/2);
        transform.channels[i]=site==Mfd::CfaSite::Red?0:site==Mfd::CfaSite::Blue?2:1;
    }
    const auto& wb=metadata.cameraWhiteBalance;
    transform.redGain=wb[0]/std::max(1e-6f,wb[1]);
    transform.blueGain=wb[2]/std::max(1e-6f,wb[1]);
    transform.cameraToSrgb=metadata.cameraToSrgb;
    return transform;
}
double Luminance(const LuminanceTransform& transform,const std::array<double,4>& cell) {
    std::array<double,3> rgb{};
    for(unsigned i=0;i<4;++i) {
        const unsigned c=transform.channels[i];
        rgb[c]+=cell[i]*(c==1?.5:1);
    }
    rgb[0]*=transform.redGain;rgb[2]*=transform.blueGain;
    std::array<double,3> working{};
    for(unsigned c=0;c<3;++c) for(unsigned k=0;k<3;++k) working[c]+=transform.cameraToSrgb[c*3+k]*rgb[k];
    return .2126*working[0]+.7152*working[1]+.0722*working[2];
}
double Luminance(const RawMetadata& metadata,const std::array<double,4>& cell) {
    return Luminance(BuildLuminanceTransform(metadata),cell);
}
} // namespace Raw::Bracketing
