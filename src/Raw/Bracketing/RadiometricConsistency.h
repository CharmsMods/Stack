#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing {
// Exposure is one scalar shared by the colours. A changing colour balance is
// contradictory calibration evidence, not a reason to white-balance the burst.
inline bool ConsistentExposureRatio(const std::array<double,3>& ev,
    const std::array<double,3>& variance,const std::array<bool,3>& valid,
    double& result,double& resultVariance) {
    unsigned count=0;double weight=0,sum=0;
    for(unsigned a=0;a<3;++a)if(valid[a]) {
        if(!std::isfinite(ev[a])||!std::isfinite(variance[a])||variance[a]<=0)return false;
        for(unsigned b=0;b<a;++b)if(valid[b]&&
            std::abs(ev[a]-ev[b])>std::max(.05,3*std::sqrt(variance[a]+variance[b])))return false;
        const double w=1/variance[a];weight+=w;sum+=w*ev[a];++count;
    }
    if(count<2||!std::isfinite(weight)||!std::isfinite(sum))return false;
    result=sum/weight;resultVariance=1/weight;
    return std::isfinite(result)&&std::isfinite(resultVariance);
}
}
