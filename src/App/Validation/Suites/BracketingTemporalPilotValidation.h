#pragma once
#include "Raw/Bracketing/TemporalGpu.h"
#include <cmath>
#include <stdexcept>

namespace Stack::Validation {
// Actual GPU preparation/combining: unusable origin payloads must not become
// noise evidence for valid alternate captures when the entire pilot is absent.
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
        const double expected=(double(.2f)/a+double(.6f)/b)/(1/a+1/b);
        const double variance=1/(1/a+1/b);
        require(output.size()==1&&output[0].size()==256,"unexpected output shape");
        for(const auto& sample:output[0])
            require(sample.finite&&!sample.clipped&&sample.support>0&&
                std::abs(sample.value-expected)<1e-12&&std::abs(sample.measurementVariance-variance)<1e-12&&
                sample.uncertaintyVariance==0,"origin flags="+std::to_string(flags)+
                    ", value="+std::to_string(value)+": invalid payload entered alternate weights, or a valid/clipped pilot changed");
    }
    require(operation.Finish(error),error);
}
}
