#include "Processor.h"
#include "ProcessingTiles.h"
#include <algorithm>
#include <limits>
#include <thread>

namespace Raw::Bracketing {
std::uint64_t EstimateBracketingPeakResidentBytes(unsigned width,unsigned height,
    std::size_t sourceCount,std::size_t groupCount,std::uint32_t workerCount,bool localAlignment) {
    const auto saturated=[](long double value) {
        return value>=std::numeric_limits<std::uint64_t>::max()
            ?std::numeric_limits<std::uint64_t>::max():static_cast<std::uint64_t>(value);
    };
    const long double pixels=static_cast<long double>(width)*height;
    const long double output=pixels*(4*sizeof(float)+4*sizeof(std::uint8_t));
    const long double preparedTiles=pixels*18;
    const long double preview=std::min<long double>(pixels/4,640.0L*640)*
        (4*groupCount*sizeof(Preview::Sample)+8*groupCount+32);
    const long double pyramids=localAlignment?pixels*2*(2*sizeof(double)+sizeof(std::uint8_t))*1.34L:0;
    const long double reliability=localAlignment?pixels*.25L*std::max<std::size_t>(1,sourceCount-1)*3:0;
    const long double tileWorkers=static_cast<long double>(std::max<std::uint32_t>(1,workerCount))*
        GroupWorkerBytes(groupCount,512,sourceCount);
    return saturated(output+preparedTiles+pyramids+reliability+preview+tileWorkers+64.0L*1024*1024);
}

std::uint32_t ResolveBracketingWorkerCount(
    std::uint64_t memoryBudgetBytes,std::uint32_t requestedLimit) {
    const std::uint32_t hardware=std::max(1u,std::thread::hardware_concurrency());
    constexpr std::uint64_t perWorkerPlanningBytes=384ull*1024*1024;
    const std::uint64_t memoryWorkers=std::max<std::uint64_t>(1,
        memoryBudgetBytes/perWorkerPlanningBytes);
    const auto memoryLimit=static_cast<std::uint32_t>(std::min<std::uint64_t>(
        hardware,memoryWorkers));
    return requestedLimit?std::max(1u,std::min(requestedLimit,memoryLimit)):memoryLimit;
}
}
