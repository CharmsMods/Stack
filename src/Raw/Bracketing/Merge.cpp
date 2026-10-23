#include "ProcessingInternal.h"
#include "CaptureCorrespondence.h"
#include "PreparedTileSampler.h"
#include "SceneGradient.h"
#include "DetailFusion.h"
#include "PreparedRegion.h"
#include "TemporalGpu.h"
#include "CaptureFallback.h"
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

static bool EvaluateTemporalRegion(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    std::vector<std::vector<Observation>>& groups,const Mfd::PreparedRawTile& origin,bool detailFusion,std::string& error) {
    if(data.combiningGpu) {
        if(data.combiningGpu->Combine(request,data,origin,detailFusion,groups,error))return true;
        if(!error.empty())return false;
    }
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    const auto count=origin.normalizedMosaic.size();groups.resize(request.recipe.groups.size());
    for(auto& group:groups)group.assign(count,Observation{});
    std::vector<std::vector<double>> weight(groups.size(),std::vector<double>(count));
    std::vector<std::vector<double>> squared(groups.size(),std::vector<double>(count));
    // One transient bit per tile sample tracks the selected fallback's own
    // status. Observation::localRejected aggregates every capture and cannot
    // identify whether the selected fallback itself had rejected geometry.
    std::vector<std::vector<bool>> fallbackRejected(groups.size(),std::vector<bool>(count));
    std::vector<std::vector<DetailFusionAccumulator>> bands;
    if(detailFusion) {
        bands.resize(groups.size());
        for(const auto& source:data.sources)
            if(source.enabled&&source.detailPreference<.999) bands[source.group].resize(count);
    }
    std::vector<Mfd::RawSignalGradient> sceneGradients(count,
        {std::numeric_limits<double>::quiet_NaN(),0});
    if(request.recipe.alignmentMode==AlignmentMode::AutomaticLocal&&
       !BuildSceneGradients(request,data,origin,sceneGradients,error)) return false;
    // Estimate the signal once in the reference exposure domain. Deriving a
    // different variance pilot from every noisy frame makes the sample and its
    // inverse-variance weight correlated. The effect is most visible as bias
    // and contouring when a large, very dark fixed-coordinate burst is lifted.
    std::vector<double> stablePilot(count);
    for(std::size_t p=0;p<count;++p) {
        const auto x=p%origin.extent.width,y=p/origin.extent.width;
        double pilot=0;unsigned used=0;
        for(unsigned dy=0;dy<4;dy+=2) for(unsigned dx=0;dx<4;dx+=2) {
            const auto xx=static_cast<std::int64_t>(((origin.originX+x)/4)*4+x%2+dx)-static_cast<std::int64_t>(origin.originX);
            const auto yy=static_cast<std::int64_t>(((origin.originY+y)/4)*4+y%2+dy)-static_cast<std::int64_t>(origin.originY);
            if(xx<0||yy<0||xx>=static_cast<std::int64_t>(origin.extent.width)||
                yy>=static_cast<std::int64_t>(origin.extent.height)) continue;
            const auto q=static_cast<std::size_t>(yy)*origin.extent.width+xx;
            const auto flags=origin.sampleFlags[q];
            if(Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||
                Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip)||
                Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective)||
                Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::DecoderRepaired)) continue;
            const double value=origin.normalizedMosaic[q]*origin.comparisonGain[q];
            if(std::isfinite(value)) {pilot+=value;++used;}
        }
        const double point=Mfd::HasSampleFlag(origin.sampleFlags[p],Mfd::PreparedSampleFlag::DecoderRepaired)
            ?0.0:origin.normalizedMosaic[p]*origin.comparisonGain[p];
        stablePilot[p]=std::max(0.0,used?pilot/used:(std::isfinite(point)?point:0.0));
    }
    std::vector<Observation> samples;
    for(const auto& source:data.sources) {
        if(!source.enabled) continue;
        if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
        const bool fixedReference=&source==&data.sources[data.origin];
        Mfd::PreparedRawTile sourceTile;
        const auto& tile=fixedReference?origin:sourceTile;
        const bool aligned=source.alignmentDiagnostic.model!=CaptureAlignment::Model::Identity||
            (!fixedReference&&source.alignmentDiagnostic.localApplied);
        if(!aligned&&!fixedReference) {
            if(detailFusion) {
                PreparedTileSampler regionSampler(data.directory);
                if(!ReadPreparedRegion(source.frame,regionSampler,origin.originX,origin.originY,
                    static_cast<unsigned>(origin.extent.width),static_cast<unsigned>(origin.extent.height),sourceTile,error)) return false;
            } else if(Mfd::ReadPreparedTile(source.frame,cache,tx,ty,sourceTile,&error)!=Mfd::TileCacheReadStatus::Hit) return false;
        }
        const auto group=source.group;
        Mfd::CfaLayout layout;Mfd::CfaLayout::TryCreate(source.frame.activeCfaPattern,layout);
        PreparedTileSampler sampler(data.directory);
        const bool useBands=detailFusion&&!bands[group].empty();
        if(useBands)samples.resize(count);
        const auto accumulate=[&](std::size_t p,const Observation& sample) {
            auto& o=groups[group][p];
            o.localRejected|=sample.localRejected;
            if(!sample.finite) return;
            if(PreferCaptureFallback(o.finite,fallbackRejected[group][p],o.exposure,
                sample.localRejected,sample.exposure)) {
                o.fallback=sample.fallback;o.exposure=sample.exposure;o.clipped=sample.clipped;
                o.fallbackClipped=sample.fallbackClipped;
                fallbackRejected[group][p]=sample.localRejected;
            }
            o.finite=true;if(sample.support<=0) return;
            o.fixedReference|=sample.fixedReference;
            const double w=1/sample.variance;
            if(weight[group][p]==0) o.measurementVariance=0;
            o.value+=w*sample.value;o.variance+=w*w*sample.variance;o.headroom+=w*sample.headroom;
            o.measurementVariance+=w*w*sample.measurementVariance;
            o.uncertaintyVariance+=w*w*sample.uncertaintyVariance;
            weight[group][p]+=w;squared[group][p]+=w*w;
        };
        for(std::size_t p=0;p<count;++p) {
            Observation sample;
            if(aligned) {
                sample=SampleAlignedCapture(source,layout,sampler,origin.originX+p%origin.extent.width,
                    origin.originY+p/origin.extent.width,stablePilot[p],sceneGradients[p],error);
                if(!error.empty()) return false;
            }
            else {
                const auto flags=tile.sampleFlags[p];const double normalized=tile.normalizedMosaic[p];const double gain=tile.comparisonGain[p]*source.scale;
                sample.value=normalized*gain;sample.fallback=sample.value;sample.exposure=1/source.scale;
                sample.finite=std::isfinite(sample.value)&&std::isfinite(gain)&&gain>0&&
                    !Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Defective)&&
                    !Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::DecoderRepaired);
                sample.clipped=Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::Saturated)||Mfd::HasSampleFlag(flags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
                sample.fallbackClipped=sample.clipped;
                if(sample.finite&&!sample.clipped) {
                    const auto site=static_cast<std::size_t>(layout.SiteAt(
                        tile.originX+p%tile.extent.width,tile.originY+p/tile.extent.width));
                    const auto& profile=source.noise.sites[site];
                    // Convert the shared comparison-domain pilot back to this
                    // capture's normalized domain before propagating its own
                    // calibrated noise profile through the known gain.
                    Mfd::GainPropagatedVariance variance;
                    if(!Mfd::PropagateKnownGainVariance(profile,gain,stablePilot[p],
                        source.frame.calibration.usableSpanByCfaSite[site],variance,&error)) return false;
                    sample.measurementVariance=std::max(1e-12,variance.variance);
                    sample.uncertaintyVariance=Mfd::ResidualModelVariance(profile,stablePilot[p]);
                    sample.uncertaintyVariance+=stablePilot[p]*stablePilot[p]*source.scaleVariance/(source.scale*source.scale);
                    sample.variance=sample.measurementVariance+sample.uncertaintyVariance;
                    sample.headroom=std::clamp((.995-normalized)/.045,0.0,1.0);sample.headroom=sample.headroom*sample.headroom*(3-2*sample.headroom);sample.support=1;
                }
            }
            sample.fixedReference=fixedReference&&sample.support>0;
            accumulate(p,sample);
            if(useBands) samples[p]=sample;
        }
        if(useBands) for(std::size_t p=0;p<count;++p) {
            const auto coarse=CoarseSample(samples,static_cast<unsigned>(origin.extent.width),
                static_cast<unsigned>(origin.extent.height),static_cast<unsigned>(p%origin.extent.width),
                static_cast<unsigned>(p/origin.extent.width),aligned?2.5:1.);
            bands[group][p].Add(samples[p],coarse,source.detailPreference);
        }
    }
    for(std::size_t g=0;g<groups.size();++g) for(std::size_t p=0;p<count;++p) {
        auto& o=groups[g][p];const double w=weight[g][p];
        if(w>0) {o.value/=w;o.variance/=w*w;o.measurementVariance/=w*w;
            o.uncertaintyVariance/=w*w;o.headroom/=w;o.support=w*w/squared[g][p];o.clipped=false;
            if(detailFusion&&!bands[g].empty()) bands[g][p].Finish(o);
        }
    }
    return true;
}
bool ReadTemporalGroups(const ProcessingRequest& request,const PreparedDataset& data,unsigned tx,unsigned ty,
    std::vector<std::vector<Observation>>& groups,Mfd::PreparedRawTile& origin,std::string& error) {
    Mfd::DirectoryNormalizedTileCache cache(data.directory);
    const auto& frame=data.sources[data.origin].frame;
    if(Mfd::ReadPreparedTile(frame,cache,tx,ty,origin,&error)!=Mfd::TileCacheReadStatus::Hit) return false;
    const bool detailFusion=std::any_of(data.sources.begin(),data.sources.end(),
        [](const auto& source){return source.enabled&&source.detailPreference<.999;});
    if(!detailFusion) return EvaluateTemporalRegion(request,data,tx,ty,groups,origin,false,error);
    const auto left=origin.originX>=4?origin.originX-4:0,top=origin.originY>=4?origin.originY-4:0;
    const auto right=std::min<std::uint64_t>(data.width,origin.originX+origin.extent.width+4);
    const auto bottom=std::min<std::uint64_t>(data.height,origin.originY+origin.extent.height+4);
    PreparedTileSampler sampler(data.directory);Mfd::PreparedRawTile expanded;
    if(!ReadPreparedRegion(frame,sampler,left,top,static_cast<unsigned>(right-left),
        static_cast<unsigned>(bottom-top),expanded,error)) return false;
    std::vector<std::vector<Observation>> full;
    if(!EvaluateTemporalRegion(request,data,tx,ty,full,expanded,true,error)) return false;
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
        const auto headroom=colorHeadroom?std::min(input[i].headroom,(*colorHeadroom)[i]):input[i].headroom;
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
