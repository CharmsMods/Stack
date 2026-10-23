#pragma once
#include "PreparedTileSampler.h"

namespace Raw::Bracketing {
inline bool ReadPreparedRegion(const Mfd::PreparedRawFrame& frame,PreparedTileSampler& sampler,
    std::uint64_t left,std::uint64_t top,unsigned width,unsigned height,
    Mfd::PreparedRawTile& region,std::string& error) {
    return sampler.ReadRegion(frame,left,top,width,height,region,&error);
}
}
