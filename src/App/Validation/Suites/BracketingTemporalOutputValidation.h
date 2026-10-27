#pragma once
#include "Raw/Bracketing/TemporalGpu.h"
#include <cmath>
#include <stdexcept>
#include <tuple>

namespace Stack::Validation {
// Finite metadata can overflow during GPU arithmetic. Such a failed operation
// must not publish NaN measurements or replace the caller's previous groups.
inline void ValidateBracketingTemporalOutput(const Raw::OpenGlTaskExecutor& execute,
    const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    using namespace Raw::Mfd;
    const auto require=[](bool ok,const std::string& message) {
        if(!ok)throw std::runtime_error("Temporal GPU output: "+message);
    };
    const auto fields=[](const Observation& o) {
        return std::tie(o.value,o.variance,o.headroom,o.support,o.fallback,o.exposure,
            o.finite,o.clipped,o.localRejected,o.fixedReference,o.measurementVariance,
            o.uncertaintyVariance,o.fallbackClipped);
    };
    const auto unchanged=[&](const auto& a,const auto& b) {
        if(a.size()!=b.size())return false;
        for(std::size_t g=0;g<a.size();++g) {
            if(a[g].size()!=b[g].size())return false;
            for(std::size_t i=0;i<a[g].size();++i)if(fields(a[g][i])!=fields(b[g][i]))return false;
        }
        return true;
    };
    PreparedDataset data;data.directory=directory;data.width=data.height=16;data.sources.resize(3);
    DirectoryNormalizedTileCache cache(directory);PreparedRawTile origin,last;std::string error;
    ProcessingRequest request;request.executeOpenGlTask=execute;
    request.memoryBudgetBytes=64ull*1024*1024;request.recipe.groups.resize(2);
    request.recipe.alignmentMode=AlignmentMode::FixedCoordinates;
    for(unsigned i=0;i<3;++i) {
        auto& source=data.sources[i];source.id="output-"+std::to_string(i);source.group=i==2?1:0;
        auto& frame=source.frame;frame.cacheKey=frame.sourceContentSha256=std::string(64,char('d'+i));
        frame.activeExtent={16,16};frame.sensorActiveArea={0,0,16,16};frame.activeCfaPattern=Raw::CfaPattern::RGGB;
        frame.tileRawPixels=16;frame.tileColumns=frame.tileRows=1;frame.calibration.usableSpanByCfaSite.fill(4095);
        source.noise.preparedFrameCacheKey=frame.cacheKey;source.noise.quality=NoiseModelQuality::TrustedMetadata;
        for(auto& profile:source.noise.sites) {profile.offsetVariance=1e-4;profile.quantizationIncluded=true;}
        PreparedRawTile input;input.extent=frame.activeExtent;
        input.normalizedMosaic.assign(256,.2f*(i+1));input.comparisonGain.assign(256,1.f);
        input.sampleFlags.assign(256,0);
        require(cache.Write(frame.cacheKey,input,&error),error);
        if(i==0)origin=input;if(i==2)last=input;
    }
    TemporalGpu gpu(request);require(gpu.Ready(error),error);TemporalGpu::Operation operation(gpu);
    std::vector<std::vector<Observation>> output;
    require(gpu.Combine(request,data,origin,false,output,error),error);
    require(output.size()==2&&output[0].size()==256&&output[1].size()==256,"unexpected output shape");
    for(const auto& sample:output[0])
        require(sample.finite&&!sample.clipped&&sample.support==2&&
            std::abs(sample.value-.3)<1e-7&&std::abs(sample.measurementVariance-5e-5)<1e-12,
            "normal two-capture control changed");
    const auto previous=output;
    // A clipped fallback and an entirely unsupported group both deliberately
    // carry the negative measurement sentinel, with no measurement support.
    for(unsigned flags:{1u,2u}) {
        last.sampleFlags.assign(256,std::uint8_t(flags));
        require(cache.Write(data.sources[2].frame.cacheKey,last,&error),error);
        require(gpu.Combine(request,data,origin,false,output,error),error);
        for(const auto& sample:output[1])
            require(sample.finite==(flags==1)&&sample.clipped==(flags==1)&&
                sample.fallbackClipped==(flags==1)&&sample.support==0&&sample.measurementVariance==-1,
                "legitimate clipped/unsupported sentinel rejected or changed");
    }
    last.sampleFlags.assign(256,0);require(cache.Write(data.sources[2].frame.cacheKey,last,&error),error);
    auto& source=data.sources[2];const auto ordinaryNoise=source.noise;source.scale=2;
    for(auto& profile:source.noise.sites)profile.offsetVariance=1e100;
    require(gpu.Combine(request,data,origin,false,output,error),error);
    for(const auto& sample:output[1])
        require(sample.finite&&sample.support==1&&std::isfinite(sample.variance)&&
            std::abs(sample.variance/4e100-1)<1e-12&&std::abs(sample.value-2*double(.2f*3))<1e-12,
            "large but finite arithmetic was rejected or changed");
    source.noise=ordinaryNoise;source.scale=1;
    require(gpu.Combine(request,data,origin,false,output,error),error);
    require(unchanged(output,previous),"ordinary output changed after sentinel/large-finite controls");
    auto canceled=request;canceled.shouldCancel=[] {return true;};
    require(!gpu.Combine(canceled,data,origin,false,output,error)&&error=="Canceled"&&unchanged(output,previous),
        "cancellation replaced previous output");
    error.clear();
    // Exercise the actual metadata resolver: this is a valid finite profile,
    // not a NaN injected into private GPU state. Gain squared overflows it.
    Raw::RawMetadata metadata;metadata.hasDngNoiseProfile=true;metadata.dngNoiseProfile={{0,1e308}};
    const auto resolved=ResolveNoiseModel(metadata,source.frame,nullptr);
    require(resolved.resolved&&ValidateNoiseModel(resolved.model),"finite metadata control did not resolve");
    source.noise=resolved.model;source.scale=2;
    require(!gpu.Combine(request,data,origin,false,output,error),"non-finite arithmetic was published as success");
    require(error.find("observation")!=std::string::npos,"arithmetic failure lacked an output diagnostic");
    require(unchanged(output,previous),"late-group arithmetic failure replaced previous output");
    require(gpu.Workspaces().residentBytes==0,"failed operation retained its GPU tile workspace");
    source.noise=ordinaryNoise;source.scale=1;error.clear();
    require(gpu.Combine(request,data,origin,false,output,error)&&unchanged(output,previous),
        "valid operation did not recover after rejected arithmetic");
    require(operation.Finish(error),error);
}
}
