#include "ProcessingInternal.h"
#include "PresentationEvidence.h"
#include "PreparedTileSampler.h"
#include "BurstNoise.h"
#include "Raw/RawTechnicalEvidence.h"
#include <array>

namespace Raw::Bracketing {
namespace {
bool NeedsEstimate(const PreparedSource& source) {
    return source.noise.quality>=Mfd::NoiseModelQuality::EstimatedBurst;
}
bool SameSensitivity(const PreparedSource& a,const PreparedSource& b) {
    return a.metadata.hasIsoSpeed&&b.metadata.hasIsoSpeed&&a.metadata.isoSpeed>0&&b.metadata.isoSpeed>0&&
        std::abs(std::log2(a.metadata.isoSpeed/b.metadata.isoSpeed))<.01;
}
bool ReadNearest(PreparedTileSampler& sampler,const PreparedSource& source,
    const Mfd::CfaLayout& layout,Mfd::RawCoordinate coordinate,Mfd::CfaSite site,
    Mfd::SameCfaTapInput& sample,std::string& error) {
    const auto mapped=source.alignment.Map(coordinate);
    const auto plane=layout.RawToPlane(mapped,site);
    if(!std::isfinite(plane.x)||!std::isfinite(plane.y)) return false;
    const auto raw=layout.PlanePixelToRaw({std::llround(plane.x),std::llround(plane.y),site});
    if(raw.x<0||raw.y<0) return false;
    if(!sampler.ReadSample(source.frame,static_cast<std::uint64_t>(raw.x),
        static_cast<std::uint64_t>(raw.y),sample,&error)) return false;
    return sample.sampleFlags==0&&std::isfinite(sample.normalizedSample)&&sample.normalizedSample<.9&&
        std::isfinite(sample.comparisonGain)&&sample.comparisonGain>0;
}
}

bool RefineBurstNoise(const ProcessingRequest& request,PreparedDataset& data,
    std::vector<std::string>& diagnostics,std::string& error) {
    Mfd::CfaLayout layout;
    if(!Mfd::CfaLayout::TryCreate(data.sources[data.origin].frame.activeCfaPattern,layout)) return false;
    std::vector<bool> analyzed(data.sources.size(),false);
    std::string presentedCapture;
    for(std::size_t anchorIndex=0;anchorIndex<data.sources.size();++anchorIndex) {
        if(analyzed[anchorIndex]) continue;
        const auto& anchor=data.sources[anchorIndex];
        ObserveNoise(request,anchor);
        presentedCapture=anchor.id;
        if(request.reportPresentation) {
            ProcessingProgress progress;progress.stage=ProcessingStage::Noise;progress.captureId=anchor.id;
            progress.completed=anchorIndex;progress.total=data.sources.size();
            progress.detail=Mfd::NoiseModelQualityName(anchor.noise.quality);progress.hasNoiseEstimate=true;
            for(unsigned b=0;b<16;++b)for(const auto& profile:anchor.noise.sites)
                progress.noiseVariance[b]+=(profile.shotScale*b/15.+profile.offsetVariance+profile.quantizationVariance)*.25;
            request.reportPresentation(progress);
        }

        std::vector<std::size_t> members;
        for(std::size_t i=0;i<data.sources.size();++i)
            if((data.sources[i].enabled||i==data.origin)&&SameSensitivity(anchor,data.sources[i])) {
                members.push_back(i);analyzed[i]=true;
            }
        if(members.size()<2) continue;
        bool needed=false;
        for(auto i:members) needed|=NeedsEstimate(data.sources[i]);
        if(!needed) continue;
        // Noise belongs to the sensor sensitivity, not the UI exposure group.
        // Pool shutter brackets at the same ISO too, and calibrate the fixed
        // origin even when it has been excluded as an output contributor.
        std::stable_sort(members.begin(),members.end(),[&](auto a,auto b) {
            return std::abs(std::log(data.sources[a].scale/anchor.scale))<
                std::abs(std::log(data.sources[b].scale/anchor.scale));
        });
        std::array<std::vector<BurstNoiseBlock>,4> blocks;
        std::size_t pairs=0;
        // Limit analysis cost independently of burst length. Original captures
        // still all participate in the merge, and duplicate hashes were rejected
        // before reaching this stage.
        for(std::size_t member=0;member<members.size()&&pairs<2;++member) {
            if(members[member]==anchorIndex) continue;
            const auto& alternate=data.sources[members[member]];
            if(!SameSensitivity(anchor,alternate)) continue;
            ++pairs;
            PreparedTileSampler sampler(data.directory,8);
            const unsigned stride=std::max(32u,static_cast<unsigned>(
                std::sqrt(static_cast<double>(data.width)*data.height/384))/2*2);
            for(unsigned y=4;y+36<data.height;y+=stride) for(unsigned x=4;x+36<data.width;x+=stride) {
                if(request.shouldCancel&&request.shouldCancel()) {error="Canceled";return false;}
                for(unsigned siteIndex=0;siteIndex<4;++siteIndex) {
                    const auto site=static_cast<Mfd::CfaSite>(siteIndex);
                    const auto offset=layout.OffsetFor(site);
                    std::vector<double> details;details.reserve(64);double signal=0;
                    for(unsigned by=0;by<8;++by) for(unsigned bx=0;bx<8;++bx) {
                        double difference=0,shot=0,dark=0;bool valid=true;
                        for(unsigned tap=0;tap<4;++tap) {
                            const Mfd::RawCoordinate coordinate={double(x+offset.x+bx*4+2*(tap%2)),
                                double(y+offset.y+by*4+2*(tap/2))};
                            Mfd::SameCfaTapInput a,b;
                            if(!ReadNearest(sampler,anchor,layout,coordinate,site,a,error)||
                               !ReadNearest(sampler,alternate,layout,coordinate,site,b,error)) {valid=false;break;}
                            const double ga=a.comparisonGain*anchor.scale,gb=b.comparisonGain*alternate.scale;
                            const double coefficient=(tap==0||tap==3)?.5:-.5;
                            difference+=coefficient*(ga*a.normalizedSample-gb*b.normalizedSample);
                            dark+=.25*(ga*ga+gb*gb);
                            shot+=.25*(ga*ga*std::max(0.,a.normalizedSample)+gb*gb*std::max(0.,b.normalizedSample));
                        }
                        if(!error.empty()) return false;
                        if(valid&&dark>0) {details.push_back(difference/std::sqrt(dark));signal+=shot/dark;}
                    }
                    BurstNoiseBlock block;
                    if(!details.empty()&&MeasureBurstNoiseBlock(details,signal/details.size(),block))
                        blocks[siteIndex].push_back(block);
                }
            }
        }
        if(!pairs) continue;
        std::array<Mfd::SiteNoiseProfile,4> profiles;
        std::array<BurstNoiseFitReport,4> fits;
        bool resolved=true;
        for(unsigned site=0;site<4;++site)
            resolved&=FitBurstNoise(blocks[site],anchor.noise.sites[site],profiles[site],&fits[site]);
        if(!resolved) {
            diagnostics.push_back(anchor.id+": temporal noise calibration had too few usable patches; retained the existing noise model.");
            continue;
        }
        for(auto i:members) {
            auto& source=data.sources[i];
            if(!NeedsEstimate(source)||!SameSensitivity(anchor,source)) continue;
            source.noise.sites=profiles;source.noise.quality=Mfd::NoiseModelQuality::EstimatedBurst;
            source.noise.diagnostics.quality=source.noise.quality;
            source.noise.diagnostics.source="temporal-burst-haar-v1";
            source.noise.diagnostics.sourceRecordId="bracketing-temporal-pairs";
            source.noise.diagnostics.correlatedNoiseUnmodeled=true;
            source.noise.diagnostics.warnings.clear();
            source.noise.diagnostics.warnings.push_back("Temporal noise calibration does not measure correlated or fixed-pattern noise. Narrow signal ranges retain the prior shot/read ratio.");
            nlohmann::json identity={{"frame",source.frame.cacheKey},{"method","temporal-burst-haar-v1"}};
            for(unsigned site=0;site<4;++site) {
                auto& diagnostic=source.noise.diagnostics.sites[site];
                diagnostic.acceptedBlockCount=blocks[site].size();diagnostic.candidateBlockCount=blocks[site].size();diagnostic.valid=true;
                diagnostic.signalMinimum=fits[site].minimum;diagnostic.signalMaximum=fits[site].maximum;
                diagnostic.signalCoverage=fits[site].maximum-fits[site].minimum;
                diagnostic.populatedSignalBins=fits[site].bins;
                diagnostic.fitRootMeanSquareError=fits[site].rootMeanSquareError;
                diagnostic.reason=fits[site].limitedRange
                    ?"temporal differences; limited signal range calibrates the prior shot/read ratio"
                    :"temporal differences; Poisson-Gaussian coefficients fitted across signal bins";
                identity["profiles"].push_back({profiles[site].shotScale,profiles[site].offsetVariance,
                    profiles[site].quantizationVariance});
            }
            const auto serialized=identity.dump();
            source.noise.identitySha256=Stack::RawEvidence::ComputeSourceIdentity(
                std::vector<std::uint8_t>(serialized.begin(),serialized.end())).sha256;
            if(request.reportPresentation) {
                std::vector<EvidencePoint> points;
                const auto step=std::max<std::size_t>(1,(blocks[0].size()+127)/128);
                for(std::size_t p=0;p<blocks[0].size();p+=step)
                    points.push_back({float(blocks[0][p].signal),float(blocks[0][p].variance)});
                ObserveNoise(request,source,points);
                presentedCapture=source.id;
            }
            diagnostics.push_back(source.id+": noise measured from independent burst differences ("+
                std::to_string(blocks[0].size())+" patches per color, fixed-pattern noise remains unmodeled).");
        }
    }
    ReportPresentation(request,ProcessingStage::Noise,data.sources.size(),data.sources.size(),presentedCapture);
    return true;
}
}
