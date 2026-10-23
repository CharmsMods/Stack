#pragma once
#include <cstdint>
#include <mutex>

namespace Raw::Bracketing {
struct LocalDetailStats {
    std::uint64_t attempted=0,centerRejected=0,sampleRejected=0,expanded=0,accepted=0;
};
// One update per completed tile. Times are sums of overlapping worker
// intervals, not additional pipeline wall time or isolated GPU kernel time.
struct GroupProcessingMetrics {
    mutable std::mutex mutex;
    double temporal=0,detail=0,cache=0;
    std::uint64_t cachedTiles=0,computedTiles=0;
    LocalDetailStats patches;
    void Add(double temporalSeconds,double detailSeconds,double cacheSeconds,bool hit,const LocalDetailStats& p) {
        std::lock_guard<std::mutex> lock(mutex);
        temporal+=temporalSeconds;detail+=detailSeconds;cache+=cacheSeconds;
        hit?++cachedTiles:++computedTiles;
        patches.attempted+=p.attempted;patches.centerRejected+=p.centerRejected;
        patches.sampleRejected+=p.sampleRejected;patches.expanded+=p.expanded;patches.accepted+=p.accepted;
    }
};
}
