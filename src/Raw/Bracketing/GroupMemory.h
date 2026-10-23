#pragma once
#include "DetailFusion.h"
#include "LocalDetail.h"

namespace Raw::Bracketing {
inline std::uint64_t GroupWorkerBytes(std::size_t groups,unsigned tilePixels,std::size_t captures,
    std::uint64_t memoryBudgetBytes=std::numeric_limits<std::uint64_t>::max()) {
    // Include the temporal detail halo, band accumulators, a source observation
    // buffer, output/canonicalization copies, and the two live tile samplers.
    const std::uint64_t side=static_cast<std::uint64_t>(tilePixels)+8;
    return side*side*(groups*(4*sizeof(Observation)+sizeof(DetailFusionAccumulator))+224)+
        std::max<std::uint64_t>(64ull*1024*1024,LocalDetailWorkingBytes(captures,tilePixels,memoryBudgetBytes));
}

inline std::uint64_t GroupOutputReserveBytes(std::uint64_t pixels) {
    return pixels*36+64ull*1024*1024;
}

inline bool GroupWorkerFits(std::uint64_t budget,std::uint64_t reserve,std::uint64_t worker) {
    return budget>reserve&&budget-reserve>=worker;
}
}
