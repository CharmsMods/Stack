#pragma once
#include "ProcessingInternal.h"
#include <limits>

namespace Raw::Bracketing {
inline constexpr const char* kLocalDetailRevision="local-temporal-dct-v1";
// Includes original-sample patches, transforms, evidence, noise footprints,
// overlap bounds and the bounded prepared-tile readers for one worker.
inline std::uint64_t LocalDetailWorkingBytes(std::size_t captures,unsigned tilePixels) {
    const long double frames=captures;
    const long double bytes=frames*640*1024+frames*frames*sizeof(double)+
        static_cast<long double>(tilePixels)*tilePixels*32+40ull*1024*1024;
    return bytes>=std::numeric_limits<std::uint64_t>::max()?std::numeric_limits<std::uint64_t>::max():std::uint64_t(bytes);
}
bool CombineLocalDetail(const ProcessingRequest&,const PreparedDataset&,unsigned,unsigned,
    std::vector<std::vector<Observation>>&,std::string&);
}
