#pragma once
#include "Raw/MultiFrameDenoise/MotionJacobian.h"

namespace Raw::Bracketing {
// The guide is differentiated in fixed-origin raw coordinates, while motion
// covariance describes source displacement. Keep native covariance untouched:
// this optional C_origin = J^-1 C_source J^-T is for guide substitution only.
inline bool TryGuideCovariance(Mfd::SymmetricRawCovariance source,
    const Mfd::RawMappingJacobian& jacobian,Mfd::SymmetricRawCovariance& origin) {
    if(!jacobian.valid||!Mfd::FiniteMappingJacobian(jacobian))return false;
    const auto normalize=[](Mfd::SymmetricRawCovariance& value) {
        // Avoid indeterminate inf-inf determinants in the scalar validator.
        // This only declines optional guide transport, not the native sample.
        if(!std::isfinite(value.xxRawPixelsSquared*value.yyRawPixelsSquared)||
           !std::isfinite(value.xyRawPixelsSquared*value.xyRawPixelsSquared))return false;
        double ignored=0;
        if(!Mfd::RegistrationUncertaintyVariance({},value,ignored,nullptr))return false;
        // RegistrationUncertaintyVariance validates a copy. Return the same
        // roundoff-normalized PSD covariance rather than exporting a tiny
        // negative diagonal as the GPU's unavailable-guide sentinel.
        value.xxRawPixelsSquared=std::max(0.,value.xxRawPixelsSquared);
        value.yyRawPixelsSquared=std::max(0.,value.yyRawPixelsSquared);
        if(value.xxRawPixelsSquared*value.yyRawPixelsSquared-value.xyRawPixelsSquared*value.xyRawPixelsSquared<0)
            value.xyRawPixelsSquared=std::copysign(std::sqrt(value.xxRawPixelsSquared*value.yyRawPixelsSquared),value.xyRawPixelsSquared);
        return true;
    };
    if(!normalize(source))return false;
    const auto& j=jacobian.linear;
    const double determinant=j[0]*j[3]-j[1]*j[2];
    // Same numerical invertibility convention as InvertAffineModel; this is
    // not an image-information threshold and never rejects a source sample.
    if(!std::isfinite(determinant)||std::abs(determinant)<=1e-12)return false;
    if(j==std::array<double,4>{1,0,0,1}){origin=source;return true;}
    const double a=j[3]/determinant,b=-j[1]/determinant,c=-j[2]/determinant,d=j[0]/determinant;
    Mfd::SymmetricRawCovariance candidate{
        a*a*source.xxRawPixelsSquared+2*a*b*source.xyRawPixelsSquared+b*b*source.yyRawPixelsSquared,
        a*c*source.xxRawPixelsSquared+(a*d+b*c)*source.xyRawPixelsSquared+b*d*source.yyRawPixelsSquared,
        c*c*source.xxRawPixelsSquared+2*c*d*source.xyRawPixelsSquared+d*d*source.yyRawPixelsSquared};
    if(!normalize(candidate))return false;
    origin=candidate;return true;
}
} // namespace Raw::Bracketing
