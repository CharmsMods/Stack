#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Raw::Bracketing {
// Marginal ranks supply a search initializer, never a calibrated exposure.
// Keep black and clipped samples in each sorted distribution: deleting them
// independently changes the ranks across bracket exposures.
struct AlignmentExposureDistribution {
    // Finite samples sorted in ascending order, without intensity censoring.
    std::array<std::vector<double>,3> channels;
    double shotVariance=0,offsetVariance=0,signalFloor=1e-5;
};

inline double EstimateAlignmentExposureSeed(const AlignmentExposureDistribution& reference,
    const AlignmentExposureDistribution& alternate) {
    std::vector<double> ratios;ratios.reserve(27);
    const auto usable=[](double value,const AlignmentExposureDistribution& distribution) {
        const double variance=distribution.offsetVariance+distribution.shotVariance*std::max(0.,value);
        return std::isfinite(value)&&std::isfinite(variance)&&variance>=0&&value<.8&&
            value>std::max(distribution.signalFloor,3*std::sqrt(variance));
    };
    for(unsigned c=0;c<3;++c) {
        const auto& a=reference.channels[c];const auto& b=alternate.channels[c];
        if(a.size()<32||b.size()<32)continue;
        for(unsigned percentile=10;percentile<=90;percentile+=10) {
            const double x=a[(a.size()-1)*percentile/100],y=b[(b.size()-1)*percentile/100];
            if(usable(x,reference)&&usable(y,alternate))ratios.push_back(std::log2(x)-std::log2(y));
        }
    }
    if(ratios.empty())return 1.;
    std::sort(ratios.begin(),ratios.end());
    const auto middle=ratios.size()/2;
    const double ev=ratios.size()%2?ratios[middle]:.5*(ratios[middle-1]+ratios[middle]);
    return std::exp2(std::clamp(ev,-16.,16.));
}
}
