#pragma once
#include "LocalMotion.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Mfd {
// Numerical agreement, not a scene-confidence or motion-acceptance threshold.
inline bool SameMappingDerivative(double a,double b) {
    return std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=
        64*std::numeric_limits<double>::epsilon()*std::max({1.,std::abs(a),std::abs(b)});
}
inline bool FiniteMappingJacobian(const RawMappingJacobian& j) {
    return std::all_of(j.linear.begin(),j.linear.end(),[](double v){return std::isfinite(v);});
}
inline bool SameMappingJacobian(const RawMappingJacobian& a,const RawMappingJacobian& b) {
    for(unsigned i=0;i<4;++i)if(!SameMappingDerivative(a.linear[i],b.linear[i]))return false;
    return true;
}
} // namespace Raw::Mfd
