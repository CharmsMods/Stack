#pragma once
#include "Raw/Bracketing/TemporalGpu.h"
#include "Raw/Bracketing/BurstSample.h"
#include <cmath>
#include <stdexcept>

namespace Stack::Validation {
// Actual GPU preparation/combining: the origin initializes the first pass;
// accepted radiance from all enabled captures determines the final noise pilot.
inline void ValidateBracketingTemporalPilot(const Raw::OpenGlTaskExecutor& execute,
    const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    using namespace Raw::Mfd;
    const auto require=[](bool ok,const std::string& message) {
        if(!ok)throw std::runtime_error("Temporal noise pilot: "+message);
    };
    PreparedDataset data;data.directory=directory;data.width=data.height=16;data.sources.resize(3);
    DirectoryNormalizedTileCache cache(directory);PreparedRawTile origin;std::string error;
    ProcessingRequest request;request.executeOpenGlTask=execute;
    request.memoryBudgetBytes=64ull*1024*1024;request.recipe.groups.resize(1);
    request.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
    for(unsigned i=0;i<3;++i) {
        auto& source=data.sources[i];source.id="pilot-"+std::to_string(i);
        source.enabled=i!=0; // The fixed origin remains a guide, not a contributor.
        auto& frame=source.frame;frame.cacheKey=frame.sourceContentSha256=std::string(64,char('a'+i));
        frame.activeExtent={16,16};frame.sensorActiveArea={0,0,16,16};frame.activeCfaPattern=Raw::CfaPattern::RGGB;
        frame.tileRawPixels=16;frame.tileColumns=frame.tileRows=1;frame.calibration.usableSpanByCfaSite.fill(4095);
        source.noise.preparedFrameCacheKey=frame.cacheKey;source.noise.quality=NoiseModelQuality::TrustedMetadata;
        for(auto& profile:source.noise.sites) {
            profile.shotScale=i==1?.01:.001;profile.offsetVariance=i==1?.001:.002;
            profile.quantizationIncluded=true;
        }
        PreparedRawTile input;input.extent=frame.activeExtent;
        input.normalizedMosaic.assign(256,i==1?.2f:.6f);input.comparisonGain.assign(256,1.f);
        input.sampleFlags.assign(256,0);
        require(cache.Write(frame.cacheKey,input,&error),error);if(i==0)origin=input;
    }
    TemporalGpu gpu(request);require(gpu.Ready(error),error);TemporalGpu::Operation operation(gpu);
    for(unsigned flags:{0u,1u,2u,4u,6u,8u})for(float value:{.1f,.9f}) {
        origin.normalizedMosaic.assign(256,value);origin.sampleFlags.assign(256,std::uint8_t(flags));
        std::vector<std::vector<Observation>> output;
        require(gpu.Combine(request,data,origin,false,output,error),error);
        const double pilot=(flags&6u)?0:double(value);
        const double a=.01*pilot+.001,b=.001*pilot+.002;
        const double refined=(double(.2f)/a+double(.6f)/b)/(1/a+1/b);
        const double finalA=.01*refined+.001,finalB=.001*refined+.002;
        const double expected=(double(.2f)/finalA+double(.6f)/finalB)/(1/finalA+1/finalB);
        const double variance=1/(1/finalA+1/finalB);
        require(output.size()==1&&output[0].size()==256,"unexpected output shape");
        for(const auto& sample:output[0])
            require(sample.finite&&!sample.clipped&&sample.support>0&&
                std::abs(sample.value-expected)<1e-12&&std::abs(sample.measurementVariance-variance)<1e-12&&
                sample.uncertaintyVariance==0,"origin flags="+std::to_string(flags)+
                    ", value="+std::to_string(value)+": shared radiance refinement or final variance differs");
    }
    // The long exposure is clipped at one, while the short captures measure
    // four in reference units. Their shot noise must be evaluated at four.
    origin.normalizedMosaic.assign(256,1.f);origin.sampleFlags.assign(256,1u);
    data.sources[0].enabled=true;data.sources[1].scale=8;data.sources[2].scale=16;
    for(unsigned i=1;i<3;++i) {
        PreparedRawTile input=origin;input.normalizedMosaic.assign(256,i==1?.5f:.25f);
        input.sampleFlags.assign(256,0);require(cache.Write(data.sources[i].frame.cacheKey,input,&error),error);
    }
    for(unsigned active:{2u,3u}) {
        data.sources[2].enabled=active==3;
        std::vector<std::vector<Observation>> output;
        require(gpu.Combine(request,data,origin,false,output,error),error);
        const double a=8*.01*4+64*.001,b=16*.001*4+256*.002;
        const double variance=active==2?a:1/(1/a+1/b);
        for(const auto& sample:output[0])
            require(sample.support>0&&std::abs(sample.value-4)<1e-12&&
                std::abs(sample.measurementVariance-variance)<1e-12,
                "clipped origin capped the recovered highlight's noise pilot");
    }
    // Equal exposures around the taper must retain their arithmetic mean.
    // Using each noisy sample's own headroom systematically darkens it.
    data.sources[0].enabled=false;data.sources[1].scale=data.sources[2].scale=1;
    data.sources[2].enabled=true;
    for(unsigned i=1;i<3;++i) {
        for(auto& profile:data.sources[i].noise.sites) {profile.shotScale=0;profile.offsetVariance=1e-4;}
        PreparedRawTile input=origin;input.normalizedMosaic.assign(256,i==1?.954f:.986f);
        input.sampleFlags.assign(256,0);require(cache.Write(data.sources[i].frame.cacheKey,input,&error),error);
    }
    std::vector<std::vector<Observation>> output;
    require(gpu.Combine(request,data,origin,false,output,error),error);
    for(const auto& sample:output[0])
        require(std::abs(sample.value-(double(.954f)+double(.986f))*.5)<1e-12&&
            std::abs(sample.measurementVariance-5e-5)<1e-12&&std::abs(sample.support-2)<1e-12,
            "stochastic headroom changed the mean or support of an equal-exposure pair");
    // A single contributor has no independently refined pilot. Preserve its
    // scalar sampling contract, including observed rather than predicted taper.
    data.sources[2].enabled=false;origin.normalizedMosaic.assign(256,.4f);origin.sampleFlags.assign(256,0);
    for(auto& profile:data.sources[1].noise.sites) {profile.shotScale=.01;profile.offsetVariance=.001;}
    require(gpu.Combine(request,data,origin,false,output,error),error);
    PreparedTileSampler sampler(directory);
    const auto scalar=ReadBurstSample(data,data.sources[1],sampler,0,0,double(.4f),error);
    require(error.empty(),error);
    for(const auto& sample:output[0])
        require(std::abs(sample.value-scalar.value)<1e-12&&
            std::abs(sample.measurementVariance-scalar.measurementVariance)<1e-12&&
            std::abs(sample.headroom-scalar.headroom)<1e-12&&sample.support==1,
            "single-contributor GPU sampling no longer matches the scalar contract");
    // One bounded large-set control: two groups separated by twelve stops,
    // each with fifteen contributors. Disabled guide/outlier inputs cannot
    // bias the shared pilot. Equal detail preferences must reconstruct the
    // ordinary mean and variance, including at the tile's truncated stencils.
    PreparedDataset large;large.directory=directory/"large-gap";
    large.width=large.height=16;large.sources.resize(32);
    DirectoryNormalizedTileCache largeCache(large.directory);PreparedRawTile largeOrigin;
    auto largeRequest=request;largeRequest.recipe.groups.resize(2);
    constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<large.sources.size();++i) {
        auto& source=large.sources[i];source.id="large-pilot-"+std::to_string(i);
        source.group=i/16;source.enabled=i!=0&&i!=31;source.scale=i<16?1:4096;
        source.detailPreference=.5;
        auto& frame=source.frame;
        frame.cacheKey=frame.sourceContentSha256=std::string(62,'a')+hex[i/16]+hex[i%16];
        frame.activeExtent={16,16};frame.sensorActiveArea={0,0,16,16};frame.activeCfaPattern=Raw::CfaPattern::RGGB;
        frame.tileRawPixels=16;frame.tileColumns=frame.tileRows=1;frame.calibration.usableSpanByCfaSite.fill(4095);
        source.noise.preparedFrameCacheKey=frame.cacheKey;source.noise.quality=NoiseModelQuality::TrustedMetadata;
        for(auto& profile:source.noise.sites) {
            profile.shotScale=.01;profile.offsetVariance=.001;profile.quantizationIncluded=true;
        }
        PreparedRawTile input;input.extent=frame.activeExtent;
        input.normalizedMosaic.assign(256,i==0?.125f:i==31?.9f:float(.5/source.scale));
        input.comparisonGain.assign(256,1.f);input.sampleFlags.assign(256,0);
        require(largeCache.Write(frame.cacheKey,input,&error),error);if(i==0)largeOrigin=input;
    }
    require(gpu.Combine(largeRequest,large,largeOrigin,true,output,error),error);
    require(output.size()==2,"large-set groups were not retained");
    for(unsigned g=0;g<2;++g) {
        const double gain=g?4096:1,expectedVariance=(gain*.01*.5+gain*gain*.001)/15;
        for(const auto& sample:output[g])
            require(sample.finite&&!sample.clipped&&std::abs(sample.value-.5)<1e-12&&
                std::abs(sample.measurementVariance-expectedVariance)<1e-10*std::max(1.,expectedVariance)&&
                std::abs(sample.support-15)<1e-10&&sample.uncertaintyVariance==0,
                "32-source, twelve-stop detail fusion lost group independence, noise scaling or disabled-source exclusion");
    }
    require(operation.Finish(error),error);
}
}
