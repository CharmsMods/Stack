#pragma once
#include "Kernel.h"
#include "../ProcessingInternal.h"

namespace Raw::Bracketing::Sr {
// The HDR owner chooses each exposure's broad contribution. Combine original
// spatial moments with those same shares before evaluating subpixel support.
// One capture in each of several exposure groups can collectively resolve colour.
inline void ResolveAdaptiveGroups(const std::vector<std::vector<PixelAccumulator>>& groups,
    std::size_t p,float confidence,std::array<Pixel,4>& color) {
    PixelAccumulator pooled;pooled.kernel=groups.front()[p].kernel;pooled.broadKernel=groups.front()[p].broadKernel;
    for(unsigned c=0;c<3;++c) {
        if(!color[c].valid||color[c].fallback)return;
        for(std::size_t g=0;g<groups.size();++g) {
            const auto& v=groups[g][p];
            if(color[c].actual[g]<=0||v.broad[c].weight<=0)continue;
            const float k=float(color[c].actual[g])/v.broad[c].weight;
            auto add=[k](Moment& dst,const Moment& src) {
                dst.value+=k*src.value;dst.weight+=k*src.weight;
                dst.noise+=k*k*src.noise;dst.frameWeightSquared+=k*k*src.frameWeightSquared;
            };
            add(pooled.narrow[c],v.narrow[c]);add(pooled.broad[c],v.broad[c]);
            pooled.detailBroadWeight[c]+=k*v.detailBroadWeight[c];
            pooled.crossNoise[c]+=k*k*v.crossNoise[c];pooled.crossFrameWeight[c]+=k*k*v.crossFrameWeight[c];
            pooled.crossUncertainty[c]+=k*k*v.crossUncertainty[c];
            pooled.broadUncertainty[c]+=k*k*v.broadUncertainty[c];
            pooled.narrowUncertainty[c]+=k*k*v.narrowUncertainty[c];
        }
    }
    const auto channels=ResolveChannels(pooled,confidence);
    const float alpha=ResolveAlpha(pooled,confidence);
    for(unsigned c=0;c<3;++c) {
        if(!channels[c].valid)continue;
        auto& o=color[c];o.value=channels[c].value;o.support=channels[c].support;
        o.measurementVariance=channels[c].variance;o.uncertaintyVariance=channels[c].uncertainty;
        o.variance=o.measurementVariance+o.uncertaintyVariance;
        if(alpha<=0)continue;
        for(std::size_t g=0;g<groups.size();++g)if(o.actual[g]>0) {
            const auto& v=groups[g][p];
            o.actual[g]*=1-alpha+alpha*v.narrow[c].weight/(v.broad[c].weight*pooled.narrow[c].weight);
        }
    }
}
}
