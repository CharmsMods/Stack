#pragma once
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <vector>

namespace Raw::Bracketing {
// Some integer RAWs expose 12-bit readings in 14-bit units. Express the
// clipping guard in source code steps without estimating white from the
// brightest scene pixel. A flat or coarsely posterized image is inconclusive.
inline unsigned IntegerCodeSpacing(const std::vector<std::uint16_t>& samples) {
    if(samples.empty())return 1;
    const auto first=samples.front();
    unsigned differences=0;
    std::bitset<1024> levels;
    const auto stride=std::max<std::size_t>(1,samples.size()/4096);
    for(std::size_t p=0;p<samples.size();p+=stride) {
        differences|=first^samples[p];
        if(differences&1)return 1;
        levels.set(samples[p]&1023);
    }
    if(levels.count()<32)return 1;
    // Verify the candidate lattice over all sensor samples, including masked
    // margins. A single finer code invalidates the coarser spacing.
    for(const auto sample:samples) {
        differences|=first^sample;
        if(differences&1)return 1;
    }
    return std::min(16u,differences&(~differences+1u));
}
}
