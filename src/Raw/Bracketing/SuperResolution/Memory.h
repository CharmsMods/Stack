#pragma once
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace Raw::Bracketing::Sr {
struct MemoryRequirements {
    std::uint64_t residentBytes=0;
    std::uint64_t outputBytes=0;
    std::uint64_t workingBytes=0;
    std::uint64_t sourceTileBytes=0;
    std::size_t preferredCacheTiles=4;
    std::uint64_t bytesPerOutputTilePixel=0;
};
struct MemoryPlan {
    bool fits=false;
    unsigned tileSide=32;
    std::size_t cacheTiles=4;
    std::uint64_t budgetBytes=0;
    std::uint64_t peakBytes=0;
    std::uint64_t additionalBytes=0;
    std::uint64_t additionalBudgetBytes=0;
    std::string message;
};
// Planning is separate from OS telemetry so pressure, explicit limits and
// reuse of already resident analysis can be tested deterministically.
MemoryPlan PlanMemory(const MemoryRequirements&,std::uint64_t requestedBudget,
    bool automatic,const Mfd::PhysicalMemorySnapshot&);
}
