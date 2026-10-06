#include "Memory.h"
#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace Raw::Bracketing::Sr {
namespace {
constexpr std::uint64_t MiB=1024ull*1024,GiB=1024*MiB;
std::uint64_t Add(std::uint64_t a,std::uint64_t b) {
    return a>std::numeric_limits<std::uint64_t>::max()-b?
        std::numeric_limits<std::uint64_t>::max():a+b;
}
std::uint64_t Multiply(std::uint64_t a,std::uint64_t b) {
    return b&&a>std::numeric_limits<std::uint64_t>::max()/b?
        std::numeric_limits<std::uint64_t>::max():a*b;
}
std::string GiBText(std::uint64_t bytes) {
    std::ostringstream text;text<<std::fixed<<std::setprecision(2)<<double(bytes)/GiB;
    return text.str()+" GiB";
}
}
MemoryPlan PlanMemory(const MemoryRequirements& r,std::uint64_t requestedBudget,
    bool automatic,const Mfd::PhysicalMemorySnapshot& memory) {
    MemoryPlan plan;plan.budgetBytes=requestedBudget;
    if(automatic&&memory.valid) {
        auto available=memory.availablePhysicalBytes;
        if(memory.commitLimitKnown)available=std::min(available,memory.availableCommitBytes);
        // Reserve some of the memory currently free, rather than reserving a
        // fixed 2 GiB again after the OS and other apps already consumed RAM.
        const auto reserve=std::min(available/2,std::clamp(available/8,512*MiB,2*GiB));
        plan.budgetBytes=Add(r.residentBytes,available-reserve);
    }
    plan.additionalBudgetBytes=plan.budgetBytes>r.residentBytes?plan.budgetBytes-r.residentBytes:0;
    const auto fixed=Add(Add(r.residentBytes,r.outputBytes),r.workingBytes);
    const auto peak=[&](unsigned side,std::size_t cache) {
        return Add(Add(fixed,Multiply(r.sourceTileBytes,cache)),
            Multiply(std::uint64_t(side)*side,r.bytesPerOutputTilePixel));
    };
    const auto desiredCache=std::max<std::size_t>(4,std::min<std::size_t>(r.preferredCacheTiles,
        256*MiB/std::max<std::uint64_t>(1,r.sourceTileBytes)));
    plan.peakBytes=peak(32,4);
    for(unsigned side=256;side>=32;side/=2) {
        if(peak(side,4)>plan.budgetBytes)continue;
        const auto room=plan.budgetBytes-peak(side,0);
        plan.tileSide=side;
        plan.cacheTiles=std::min<std::size_t>(desiredCache,room/std::max<std::uint64_t>(1,r.sourceTileBytes));
        plan.peakBytes=peak(side,plan.cacheTiles);plan.fits=true;break;
    }
    plan.additionalBytes=plan.peakBytes>r.residentBytes?plan.peakBytes-r.residentBytes:0;
    if(plan.fits) {
        plan.message="Super-resolution memory: estimated peak "+GiBText(plan.peakBytes)+
            ", additional allocation "+GiBText(plan.additionalBytes)+
            ", additional budget "+GiBText(plan.additionalBudgetBytes)+
            ", tile "+std::to_string(plan.tileSide)+" px.";
    } else {
        plan.message="Super-resolution needs approximately "+GiBText(plan.additionalBytes)+
            " more memory; "+GiBText(plan.additionalBudgetBytes)+
            (automatic&&memory.valid?" is available after the working reserve.":" remains in the processing budget.")+
            " Free memory and retry, or use 1x or Standard.";
    }
    return plan;
}
}
