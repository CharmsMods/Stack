#pragma once
#include "Raw/Bracketing/SuperResolution/Preparation.h"
#include <cmath>
#include <stdexcept>

namespace Stack::Validation {
inline void ValidateSuperResolutionNoisePreparation(const std::filesystem::path& directory) {
    using namespace Raw::Bracketing;
    auto data=std::make_shared<PreparedDataset>();
    data->directory=directory;data->width=data->height=64;data->sources.resize(1);
    auto& source=data->sources[0];source.scale=3;
    auto& frame=source.frame;frame.activeExtent={64,64};frame.sensorActiveArea={0,0,64,64};
    frame.tileRawPixels=64;frame.tileColumns=frame.tileRows=1;
    frame.calibration.usableSpanByCfaSite.fill(4095);
    Raw::Mfd::PreparedRawTile input;input.extent=frame.activeExtent;
    input.normalizedMosaic.assign(64*64,.2f);input.comparisonGain.assign(64*64,2.f);
    input.sampleFlags.assign(64*64,0);
    Raw::Mfd::DirectoryNormalizedTileCache cache(directory);
    BracketingAnalysis analysis;analysis.prepared=data;analysis.guideEv.assign(32*32,0.f);
    Sr::Tile target;target.originX=target.originY=8;target.width=target.height=8;target.step=.5f;
    // Included quantization, separately specified quantization, no quantization,
    // and a quantization-limited calibrated profile. Different CFA coefficients
    // and two phases check that preparation preserves the physical site's model.
    for(unsigned phase=0;phase<2;++phase)for(unsigned test=0;test<4;++test) {
        frame.cacheKey=std::string(64,char('1'+phase*4+test));
        frame.activeCfaPattern=phase?Raw::CfaPattern::BGGR:Raw::CfaPattern::RGGB;
        source.noise.preparedFrameCacheKey=frame.cacheKey;
        source.noise.quality=Raw::Mfd::NoiseModelQuality::CalibratedCamera;
        for(unsigned site=0;site<4;++site) {
            auto& profile=source.noise.sites[site];profile={};
            profile.shotScale=test==3?0:1e-4*(site+1);
            profile.offsetVariance=(test==3?1e-8:1e-7)*(site+1);
            profile.quantizationVariance=test==2?0:1e-8*(site+1);
            profile.quantizationIncluded=test!=1;
        }
        std::string error;
        if(!cache.Write(frame.cacheKey,input,&error))throw std::runtime_error(error);
        PreparedTileSampler sampler(directory);Sr::FrameRegion region;
        if(!Sr::BuildFrameRegion(analysis,source,target,sampler,region,{},error))
            throw std::runtime_error(error);
        Raw::Mfd::CfaLayout layout;Raw::Mfd::CfaLayout::TryCreate(frame.activeCfaPattern,layout);
        for(int y=0;y<region.height;++y)for(int x=0;x<region.width;++x) {
            const auto site=std::size_t(layout.SiteAt(region.left+x,region.top+y));
            auto profile=source.noise.sites[site];
            if(!profile.quantizationIncluded)profile.offsetVariance+=profile.quantizationVariance;
            profile.quantizationIncluded=true;
            Raw::Mfd::GainPropagatedVariance expected;
            if(!Raw::Mfd::PropagateKnownGainVariance(profile,6,.18,4095,expected,&error))
                throw std::runtime_error(error);
            const auto& sample=region.samples[std::size_t(y)*region.width+x];
            if(sample.x!=float(double(input.normalizedMosaic[0])*6)||
               sample.y!=float(expected.shotCoefficient)||sample.z!=float(expected.offsetVariance)||sample.w!=1)
                throw std::runtime_error("SR sensor preparation changed the sample or counted quantization incorrectly");
        }
        for(const auto& mapping:region.mapping)if(mapping.w!=0||mapping.z!=1)
            throw std::runtime_error("SR sensor noise was mixed into registration/model uncertainty");
    }
}
}
