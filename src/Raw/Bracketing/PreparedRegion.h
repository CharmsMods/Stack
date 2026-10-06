#pragma once
#include "PreparedTileSampler.h"

namespace Raw::Bracketing {
inline bool ReadPreparedRegion(const Mfd::PreparedRawFrame& frame,PreparedTileSampler& sampler,
    std::uint64_t left,std::uint64_t top,unsigned width,unsigned height,
    Mfd::PreparedRawTile& region,std::string& error) {
    region={};region.originX=left;region.originY=top;region.extent={width,height};
    const auto count=static_cast<std::size_t>(width)*height;
    region.normalizedMosaic.resize(count);region.comparisonGain.resize(count);region.sampleFlags.resize(count);
    for(unsigned y=0;y<height;++y) for(unsigned x=0;x<width;++x) {
        Mfd::SameCfaTapInput tap;
        if(!sampler.ReadSample(frame,left+x,top+y,tap,&error)) return false;
        const auto p=static_cast<std::size_t>(y)*width+x;
        region.normalizedMosaic[p]=static_cast<float>(tap.normalizedSample);
        region.comparisonGain[p]=static_cast<float>(tap.comparisonGain);region.sampleFlags[p]=tap.sampleFlags;
    }
    return true;
}
}
