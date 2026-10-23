#pragma once
#include "ProcessingInternal.h"
#include <algorithm>
#include <limits>

namespace Raw::Bracketing {
inline constexpr const char* kLocalDetailRevision="local-temporal-dct-v1";
inline std::uint64_t LocalDetailPatchCacheBytes(std::uint64_t memoryBudgetBytes) {
    return std::min<std::uint64_t>(32ull*1024*1024,memoryBudgetBytes/32);
}
// Nearby patches revisit every capture. Keep a four-tile interpolation
// footprint per capture when admitted, plus the reference gradient halo.
// Both the actual sampler and worker admission use this same bounded plan.
inline std::size_t LocalDetailSamplerTiles(std::size_t captures,unsigned tilePixels,
    std::uint64_t memoryBudgetBytes=std::numeric_limits<std::uint64_t>::max()) {
    constexpr std::uint64_t maximumCacheBytes=256ull*1024*1024;
    const long double tileBytes=static_cast<long double>(tilePixels)*tilePixels*9+1024;
    const auto cacheBytes=std::min(maximumCacheBytes,memoryBudgetBytes/8);
    const auto available=std::max<std::size_t>(8,static_cast<std::size_t>(cacheBytes/tileBytes));
    const auto desired=captures>(std::numeric_limits<std::size_t>::max()-4)/4
        ?std::numeric_limits<std::size_t>::max():captures*4+4;
    return std::min(available,std::max<std::size_t>(8,desired));
}
// Includes original-sample patches, transforms, evidence, noise footprints,
// overlap bounds and the bounded prepared-tile readers for one worker.
inline std::uint64_t LocalDetailWorkingBytes(std::size_t captures,unsigned tilePixels,
    std::uint64_t memoryBudgetBytes=std::numeric_limits<std::uint64_t>::max()) {
    const long double frames=captures;
    // Read loads the next immutable tile before evicting an old cache entry.
    // Include that transient tile and conservative map/key allocation overhead.
    const long double cacheBytes=(static_cast<long double>(LocalDetailSamplerTiles(
        captures,tilePixels,memoryBudgetBytes))+1)*
        (static_cast<long double>(tilePixels)*tilePixels*9+1024);
    const long double bytes=frames*640*1024+frames*frames*sizeof(double)+
        static_cast<long double>(tilePixels)*tilePixels*32+40ull*1024*1024+cacheBytes+
        LocalDetailPatchCacheBytes(memoryBudgetBytes);
    return bytes>=std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():std::uint64_t(bytes);
}
bool CombineLocalDetail(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,
    std::vector<std::vector<Observation>>&,std::string&,LocalDetailStats* stats=nullptr);
}
