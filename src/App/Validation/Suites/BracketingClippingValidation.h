#pragma once

#include "Raw/Bracketing/BurstSample.h"
#include "Raw/Bracketing/HdrColorFusion.h"
#include "Raw/Bracketing/TemporalGpu.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace Stack::Validation {

// Fixed, small sensor cases exercise the actual prepared-tile and GPU paths.
// Saturated samples hold a physically consistent lower bound. They are not
// arbitrary missing flags attached to otherwise low-valued measurements.
inline void ValidateBracketingClipping(const Raw::OpenGlTaskExecutor& execute,
    const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    using namespace Raw::Mfd;
    const auto require=[](bool ok,const std::string& message) {
        if(!ok)throw std::runtime_error("Bracket clipping reconstruction: "+message);
    };
    constexpr unsigned side=32,center=10;
    constexpr double gain=.125,noise=1e-5;
    ProcessingRequest request;request.executeOpenGlTask=execute;
    request.memoryBudgetBytes=64ull*1024*1024;
    request.recipe.groups.resize(2);
    request.recipe.alignmentMode=AlignmentMode::AutomaticGlobal;
    std::string error;
    TemporalGpu gpu(request);require(gpu.Ready(error),error);
    TemporalGpu::Operation operation(gpu);
    double maximumValueError=0,maximumVarianceError=0,maximumSceneError=0;
    double worstPairVarianceRatio=0,exactCenter=0,fractionalCenter=0;

    for(unsigned scenario=0;scenario<9;++scenario) {
        const bool fractional=scenario!=0;
        const bool expectedSupported=scenario<2||scenario==7;
        const double dx=fractional?.5:0,dy=fractional?.3:0;
        const double base=scenario==0?.96:scenario==7?.30:.85;
        const double slopeX=scenario==0?.06:scenario==7?.01:.08;
        const double slopeY=scenario==0?.004:.006;
        const auto signal=[&](double x,double y) {
            if(scenario==3)return 1.1;
            if(scenario==5)return x<6?.2:x<7?.8:1.1;
            return base+slopeX*(x-5)+slopeY*(y-5);
        };
        PreparedDataset data;data.directory=directory/std::to_string(scenario);
        data.width=data.height=side;data.sources.resize(2);
        DirectoryNormalizedTileCache cache(data.directory);
        PreparedRawTile origin,sourceTile;
        for(unsigned i=0;i<2;++i) {
            auto& source=data.sources[i];source.id="clipping-"+std::to_string(i);
            source.group=i;source.enabled=i==1;source.scale=i?gain:1;
            auto& frame=source.frame;frame.cacheKey=frame.sourceContentSha256=std::string(64,char('a'+i));
            frame.activeExtent={side,side};frame.sensorActiveArea={0,0,side,side};
            frame.activeCfaPattern=Raw::CfaPattern::RGGB;
            frame.tileRawPixels=side;frame.tileColumns=frame.tileRows=1;
            frame.calibration.usableSpanByCfaSite.fill(4095);
            source.noise.preparedFrameCacheKey=frame.cacheKey;
            source.noise.quality=NoiseModelQuality::TrustedMetadata;
            for(auto& profile:source.noise.sites) {
                profile.offsetVariance=noise;profile.quantizationIncluded=true;
            }
            if(i) {
                source.alignmentDiagnostic.model=CaptureAlignment::Model::Translation;
                source.alignment.translationRaw={dx,dy};
                // An unavailable local match must stay rejected even if its
                // underlying sensor neighborhood admits a good plane fit.
                source.alignmentDiagnostic.localApplied=scenario==6;
            }
            PreparedRawTile input;input.extent=frame.activeExtent;
            input.normalizedMosaic.resize(side*side);
            input.comparisonGain.assign(side*side,1.f);input.sampleFlags.assign(side*side,0);
            for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
                const auto p=y*side+x;
                const double value=signal(double(x/2)+(i?0:dx*.5),double(y/2)+(i?0:dy*.5));
                input.normalizedMosaic[p]=float(i?std::min(1.,value):value*gain);
                if(i&&value>=1)input.sampleFlags[p]=std::uint8_t(PreparedSampleFlag::Saturated);
            }
            if(i&&scenario==2) {
                // Every positively weighted central bilinear sample is
                // required; a lost central color cannot be extrapolated.
                for(unsigned y=center;y<center+2;++y)for(unsigned x=center+2;x<center+4;++x) {
                    input.normalizedMosaic[y*side+x]=1.f;
                    input.sampleFlags[y*side+x]=std::uint8_t(PreparedSampleFlag::Saturated);
                }
            }
            if(i&&scenario==4) {
                for(unsigned y=center;y<center+2;++y)for(unsigned x=center-2;x<center;++x)
                    input.sampleFlags[y*side+x]=std::uint8_t(PreparedSampleFlag::Defective);
            }
            if(i&&scenario==8) {
                // A nominally low-noise exposure with inconsistent surviving
                // measurements must not turn its fitted plane into evidence.
                for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {
                    const auto p=y*side+x;
                    if(!input.sampleFlags[p])input.normalizedMosaic[p]+=
                        ((x/2+y/2)&1)?.03f:-.03f;
                }
            }
            require(cache.Write(frame.cacheKey,input,&error),error);
            if(i)sourceTile=std::move(input);else origin=std::move(input);
        }

        std::vector<std::vector<Observation>> output;
        require(gpu.Combine(request,data,origin,false,output,error),error);
        require(output.size()==2&&output[1].size()==side*side,"unexpected output shape");
        PreparedTileSampler sampler(data.directory);
        for(unsigned site=0;site<4;++site) {
            const unsigned x=center+site%2,y=center+site/2,p=y*side+x;
            const auto scalar=ReadBurstSample(data,data.sources[1],sampler,x,y,
                origin.normalizedMosaic[p],error,nullptr,true);
            require(error.empty(),error);
            const auto& accelerated=output[1][p];
            require((scalar.support>0)==expectedSupported&&
                (accelerated.support>0)==expectedSupported,
                "scenario "+std::to_string(scenario)+" changed measurement eligibility");
            require(scalar.localRejected==accelerated.localRejected,
                "CPU/GPU geometry rejection differs");
            const double expected=signal(5+dx*.5,5+dy*.5)*gain;
            if(!expectedSupported) {
                if(scenario==6)require(scalar.finite&&accelerated.finite&&scalar.localRejected&&
                    !scalar.fallbackClipped&&!accelerated.fallbackClipped&&
                    std::abs(scalar.fallback-expected)<2e-8&&
                    std::abs(scalar.fallback-accelerated.fallback)<1e-11,
                    "rejected geometry lost the consistent zero-support display fallback");
                continue;
            }
            maximumValueError=std::max(maximumValueError,std::abs(scalar.value-accelerated.value));
            maximumVarianceError=std::max(maximumVarianceError,
                std::abs(scalar.measurementVariance-accelerated.measurementVariance));
            maximumSceneError=std::max(maximumSceneError,std::abs(accelerated.value-expected));
            require(std::abs(scalar.value-expected)<2e-8&&
                std::abs(accelerated.value-expected)<2e-8,
                "partial support biased an affine scene or its color");
            require(std::abs(scalar.value-accelerated.value)<1e-11&&
                std::abs(scalar.measurementVariance-accelerated.measurementVariance)<1e-12&&
                std::abs(scalar.uncertaintyVariance-accelerated.uncertaintyVariance)<1e-11&&
                std::abs(scalar.headroom-accelerated.headroom)<1e-10,
                "CPU/GPU reconstructed value or uncertainty differs");
            require(scalar.measurementVariance>0&&scalar.uncertaintyVariance>0&&
                scalar.support==1&&accelerated.support==1,
                "spatial samples claimed independent captures or lost uncertainty");
            if(scenario==0) {
                exactCenter=accelerated.value;
                require(std::abs(scalar.value-double(sourceTile.normalizedMosaic[p])*gain)<1e-14&&
                    std::abs(scalar.measurementVariance-noise*gain*gain)<1e-14,
                    "derivative-only clipping smoothed the valid sensor value");
            }
            if(scenario==1)fractionalCenter=accelerated.value;
        }
        if(scenario<2) {
            // Enable the short capture, then use production color coordination.
            // Recovered long-exposure measurements should lower noise while
            // every color continues to describe the same neutral ramp.
            data.sources[0].enabled=true;
            require(gpu.Combine(request,data,origin,false,output,error),error);
            ColorObservations samples;GatherColorCell(output,center*side+center,side,samples);
            const auto merged=BlendColor(samples,{},request.recipe);
            const double expected=signal(5+dx*.5,5+dy*.5)*gain;
            for(const auto& pixel:merged) {
                worstPairVarianceRatio=std::max(worstPairVarianceRatio,pixel.measurementVariance/noise);
                require(pixel.valid&&pixel.actual[1]>0&&std::abs(pixel.value-expected)<2e-8,
                    "HDR color coordination discarded recovered support or introduced false color");
                require(pixel.measurementVariance<noise*.25,
                    "retained long-exposure support did not improve noise over the short capture");
            }
        }
    }
    require(operation.Finish(error),error);
    std::cout<<"Native clipping: exact center="<<exactCenter<<", fractional center="<<fractionalCenter
        <<", maximum scene error="<<maximumSceneError<<", CPU/GPU value error="<<maximumValueError
        <<", variance error="<<maximumVarianceError
        <<", worst recovered-pair/short-only variance="<<worstPairVarianceRatio<<'\n';
}

} // namespace Stack::Validation
