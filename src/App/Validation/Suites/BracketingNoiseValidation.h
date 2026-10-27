#pragma once
#include "Raw/Bracketing/BurstNoise.h"
#include "Raw/Bracketing/ProcessingInternal.h"
#include "Raw/Bracketing/PhotometricReliability.h"
#include "Raw/Bracketing/ExposureGraph.h"
#include <random>
#include <stdexcept>
#include <limits>
#include <iostream>

namespace Stack::Validation {
inline void ValidateBracketingTemporalNoiseSignal(const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    using namespace Raw::Mfd;
    constexpr unsigned side=640;
    constexpr double shot=.001,read=1e-6;
    bool signalUnbiased=true;
    for(bool dark:{true,false}) {
        PreparedDataset data;data.width=data.height=side;data.sources.resize(3);
        data.directory=directory/(dark?"broad-dark":"positive-control");
        DirectoryNormalizedTileCache cache(data.directory);
        ProcessingRequest request;request.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
        request.recipe.groups.resize(1);
        for(unsigned f=0;f<3;++f) {
            auto& source=data.sources[f];source.id="temporal-noise-"+std::to_string(f);
            source.exposureCalibrated=true;
            auto& frame=source.frame;frame.cacheKey=frame.sourceContentSha256=std::string(64,char('a'+f));
            frame.activeCfaPattern=Raw::CfaPattern::RGGB;frame.activeExtent={side,side};frame.sensorActiveArea={0,0,side,side};
            frame.tileRawPixels=64;frame.tileColumns=frame.tileRows=side/64;
            frame.calibration.usableSpanByCfaSite.fill(65535);
            auto& model=source.noise;model.preparedFrameCacheKey=frame.cacheKey;model.identitySha256=std::string(64,'d');
            model.quality=NoiseModelQuality::EstimatedBurst;model.diagnostics.quality=model.quality;
            model.diagnostics.resolved=true;model.diagnostics.referenceOnly=false;
            for(auto& profile:model.sites) {
                profile.shotScale=shot;profile.offsetVariance=read;
                profile.quantizationVariance=1./(12.*65535*65535);profile.quantizationIncluded=true;
            }
            // Compatible estimated priors are eligible for temporal fitting;
            // generic dark-only priors deliberately are not. Both scenes have
            // broad signal coverage, with known dark patches in the first.
            if(!ValidateNoiseModel(model))throw std::runtime_error("Invalid temporal-noise fixture prior");
            std::mt19937 random(4517+f);std::normal_distribution<double> normal;
            for(unsigned ty=0;ty<side/64;++ty)for(unsigned tx=0;tx<side/64;++tx) {
                PreparedRawTile tile;tile.tileX=tx;tile.tileY=ty;tile.originX=tx*64;tile.originY=ty*64;tile.extent={64,64};
                tile.normalizedMosaic.resize(4096);tile.comparisonGain.assign(4096,1);tile.sampleFlags.assign(4096,0);
                for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x) {
                    const int band=std::max(0,(int(tile.originX+x)-4)/32);
                    const double signal=dark?(band<9?0:.04*(band-8)):.10+.015*band;
                    const float sample=float(signal+std::sqrt(shot*signal+read)*normal(random));
                    if(!dark&&sample<=0)throw std::runtime_error("Positive temporal-noise control contains a nonpositive sample");
                    tile.normalizedMosaic[y*64+x]=sample;
                }
                std::string error;if(!cache.Write(frame.cacheKey,tile,&error))throw std::runtime_error(error);
            }
        }
        std::string error;std::vector<std::string> diagnostics;
        if(!RefineBurstNoise(request,data,diagnostics,error))throw std::runtime_error(error);
        for(const auto& source:data.sources) {
            const auto& model=source.noise;
            if(!ValidateNoiseModel(model)||model.diagnostics.sourceRecordId!="bracketing-temporal-pairs")
                throw std::runtime_error("Eligible temporal-noise fit did not publish a valid model");
            for(unsigned site=0;site<4;++site) {
                const auto& fit=model.diagnostics.sites[site];const auto& profile=model.sites[site];
                if(!fit.valid||fit.signalCoverage<.1||!std::isfinite(fit.signalMinimum)||fit.signalMinimum<0)
                    throw std::runtime_error("Temporal-noise fit lost finite broad signal evidence");
                if(dark)signalUnbiased&=fit.signalMinimum<std::sqrt(read)*.1;
                else for(double signal:{.1,.4}) {
                    const double expected=shot*signal+read,actual=profile.shotScale*signal+profile.offsetVariance;
                    if(std::abs(actual/expected-1)>.2)
                        throw std::runtime_error("Positive temporal-noise control changed its measured variance");
                }
            }
        }
    }
    // Test the observable predictor rather than requiring a well-conditioned
    // read intercept from every noisy broad-range fit. No fit weights change.
    if(!signalUnbiased)throw std::runtime_error("Zero-signal temporal patches acquired a rectified noise-dependent predictor");
    std::cout<<"Temporal noise: signed dark-patch signal, eligible priors and positive broad-scene control passed.\n";
}

inline void ValidateBracketingNoiseResolutionFailures() {
    using namespace Raw::Mfd;
    PreparedRawFrame frame;
    frame.cacheKey=frame.sourceContentSha256=std::string(64,'b');
    frame.sensorActiveArea={0,0,64,64};frame.activeExtent={64,64};
    frame.activeCfaPattern=Raw::CfaPattern::RGGB;
    frame.tileRawPixels=64;frame.tileColumns=frame.tileRows=1;
    frame.calibration.maximumBlackByCfaSite.fill(64);
    frame.calibration.whiteLevelByCfaSite.fill(4159);
    frame.calibration.usableSpanByCfaSite.fill(4095);
    PreparedRawTile tile;tile.extent=frame.activeExtent;
    tile.normalizedMosaic.assign(4096,.02f);
    tile.comparisonGain.assign(4096,1.f);tile.sampleFlags.assign(4096,0);
    MemoryNormalizedTileCache cache;std::string error;
    if(!cache.Write(frame.cacheKey,tile,&error))throw std::runtime_error(error);
    NoiseResolutionOptions options;options.enableGenericLowConfidence=true;
    for(auto& site:options.genericLowConfidenceSites) {
        site.shotScale=1e-4;site.offsetVariance=1e-7;
    }
    struct Canceled {};
    // Entry, tile, and both block rows must honor cancellation. A rejected
    // estimate may fall back, but an exception must unwind without doing so.
    for(unsigned stop:{1u,2u,3u,4u}) {
        unsigned calls=0;
        options.burstEstimate.checkCancellation=[&] {if(++calls==stop)throw Canceled{};};
        NoiseModel model;model.identitySha256="unchanged";bool canceled=false;
        try {EstimateNoiseModelFromPreparedFrame(frame,cache,options.burstEstimate,model,&error);}
        catch(const Canceled&) {canceled=true;}
        if(!canceled||calls!=stop||model.identitySha256!="unchanged")
            throw std::runtime_error("Canceled noise estimation published a partial result or missed a cancellation boundary");
        calls=0;canceled=false;
        try {(void)ResolveNoiseModel({},frame,&cache,options);}
        catch(const Canceled&) {canceled=true;}
        if(!canceled||calls!=stop)
            throw std::runtime_error("Noise resolution swallowed cancellation and selected a fallback");
    }
    options.burstEstimate.checkCancellation={};
    Raw::RawMetadata metadata;
    metadata.isoSpeed=std::numeric_limits<float>::infinity();
    metadata.exposureTimeSeconds=metadata.apertureFNumber=std::numeric_limits<float>::quiet_NaN();
    metadata.hasDngNoiseProfile=true;
    metadata.dngNoiseProfile={{std::numeric_limits<double>::quiet_NaN(),1e-7}};
    const auto fallback=ResolveNoiseModel(metadata,frame,&cache,options);
    if(!fallback.resolved||fallback.model.quality!=NoiseModelQuality::GenericLowConfidence||
       !ValidateNoiseModel(fallback.model))
        throw std::runtime_error("Malformed noise metadata prevented a valid generic fallback");
    metadata.dngNoiseProfile={{1e-4,1e-7}};
    options.burstEstimate.checkCancellation=[] {throw Canceled{};};
    // Valid embedded calibration must not run the image-estimation fallback.
    NoiseResolutionResult trusted;
    try {trusted=ResolveNoiseModel(metadata,frame,&cache,options);}
    catch(const Canceled&) {throw std::runtime_error("Valid DNG calibration unnecessarily invoked image estimation");}
    if(!trusted.resolved||trusted.model.quality!=NoiseModelQuality::TrustedMetadata)
        throw std::runtime_error("Valid DNG noise calibration lost precedence");
}

inline void ValidateBracketingNoiseEvidence() {
    ValidateBracketingNoiseResolutionFailures();
    using namespace Raw::Bracketing;
    std::mt19937 random(4157);std::normal_distribution<double> normal;
    std::vector<BurstNoiseBlock> blocks;
    for(unsigned b=0;b<512;++b) {
        const double signal=.005+.5*b/511;
        const double variance=.00008*signal+.000012;
        std::vector<double> details;
        for(unsigned i=0;i<64;++i) details.push_back(normal(random)*std::sqrt(variance));
        BurstNoiseBlock block;
        if(!MeasureBurstNoiseBlock(details,signal,block)) throw std::runtime_error("Burst noise block failed");
        if(b%19==0) block.variance*=20; // minority moving/structured patches
        blocks.push_back(block);
    }
    Raw::Mfd::SiteNoiseProfile previous,profile;previous.quantizationIncluded=true;
    previous.shotScale=.0001;previous.offsetVariance=1e-7;
    if(!FitBurstNoise(blocks,previous,profile)||std::abs(profile.offsetVariance/.000012-1)>.2||
        std::abs(profile.shotScale/.00008-1)>.25)
        throw std::runtime_error("Temporal noise fit did not recover the Poisson-Gaussian profile");
    std::vector<BurstNoiseBlock> darkBlocks(128,{.003,2e-7});
    previous.shotScale=.0004;previous.offsetVariance=4e-6;
    if(!FitBurstNoise(darkBlocks,previous,profile)||
        std::abs((profile.shotScale*.003+profile.offsetVariance)/2e-7-1)>.05)
        throw std::runtime_error("Dark-burst calibration retained an excessive generic noise level");
    // Noise energy at the expected level does not count as scene structure.
    if(NoiseCorrectedMismatch(0,49,49)>1e-12||NoiseCorrectedMismatch(245,1225,49)<20)
        throw std::runtime_error("Noise-corrected mismatch confused noise and coherent displacement");
    Raw::Mfd::LocalMotionGrid motion;motion.valid=true;motion.width=motion.height=2;
    motion.referenceRawExtent=motion.sourceRawExtent={64,64};
    motion.spacingRawX=motion.spacingRawY=63;motion.nodes.resize(4);motion.structuredCount=4;
    for(auto& node:motion.nodes) {node.state=Raw::Mfd::MotionNodeState::Structured;
        node.confidence=1;node.covarianceRaw={.0025,0,.0025};}
    Raw::Mfd::ReliabilityBuildRequest request;request.rawExtent={64,64};request.motionGrid=&motion;
    Raw::Mfd::CfaLayout::TryCreate(Raw::CfaPattern::RGGB,request.layout);
    request.noiseQuality=Raw::Mfd::NoiseModelQuality::GenericLowConfidence;
    std::vector<double> samples(64*64);
    for(auto& sample:samples) sample=normal(random)*.005;
    bool displaced=false,missingReference=false;
    request.residualEvaluator=[&](Raw::Mfd::RawCoordinate raw,Raw::Mfd::CfaSite site,
        const Raw::Mfd::LocalMotionFieldSample&,Raw::Mfd::ReliabilityResidualSample& sample) {
        sample.valid=true;sample.hardValid=!missingReference;
        sample.referenceValue=.02;sample.alternateValue=.02+samples[static_cast<unsigned>(raw.y)*64+static_cast<unsigned>(raw.x)];
        sample.referenceGateVariance=sample.alternateGateVariance=.0000125;
        if(displaced&&site==Raw::Mfd::CfaSite::Red&&raw.x>=16&&raw.x<48&&raw.y>=16&&raw.y<48)
            sample.alternateValue+=.05;
        return true;
    };
    Raw::Mfd::ReliabilityMap map;std::string error;
    const auto patch=Raw::Mfd::ReliabilityRejectMask(Raw::Mfd::ReliabilityRejectBit::PatchRejected);
    if(!BuildColorReliability(request,map,error)) throw std::runtime_error(error);
    unsigned rejected=0;for(const auto& cell:map.cells) rejected+=(cell.rejectionBits&patch)!=0;
    if(rejected>map.cells.size()/100) throw std::runtime_error("Noise-only patches lost their temporal support");
    displaced=true;
    if(!BuildColorReliability(request,map,error)||!(map.cells[16*32+16].rejectionBits&patch))
        throw std::runtime_error("Generic noise confidence bypassed a single-color displaced boundary");
    displaced=false;missingReference=true;
    if(!BuildColorReliability(request,map,error)) throw std::runtime_error(error);
    for(const auto& cell:map.cells) if(cell.rejectionBits&patch)
        throw std::runtime_error("Missing reference evidence was mistaken for a photometric contradiction");
    std::vector<double> ev={0,0,0},uncertainty;
    const std::vector<ExposureEdge> connected={{0,1,1,.0001},{1,2,1.02,.0001},{0,2,2.01,.0001}};
    if(!SolveExposureGraph(connected,{true,false,false},ev,uncertainty)||
        std::abs(ev[1]-.9966666667)>1e-6||std::abs(ev[2]-2.0133333333)>1e-6||uncertainty[1]<=0)
        throw std::runtime_error("Exposure graph did not balance independent overlap constraints");
    ev={0,0,0};
    if(SolveExposureGraph({{0,1,1,.0001}},{true,false,false},ev,uncertainty))
        throw std::runtime_error("Exposure graph accepted a disconnected HDR input");
}
}
