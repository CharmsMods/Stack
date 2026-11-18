#pragma once

#include "MotionJacobian.h"

namespace Raw::Mfd {

inline bool TryMotionCovarianceEigenvalues(
    SymmetricRawCovariance covariance,
    double& low,
    double& high) {
    if (!std::isfinite(covariance.xxRawPixelsSquared) ||
        !std::isfinite(covariance.xyRawPixelsSquared) ||
        !std::isfinite(covariance.yyRawPixelsSquared)) {
        return false;
    }
    const double trace = covariance.xxRawPixelsSquared +
        covariance.yyRawPixelsSquared;
    const double difference = covariance.xxRawPixelsSquared -
        covariance.yyRawPixelsSquared;
    const double radius = std::sqrt(std::max(
        0.0,
        0.25 * difference * difference +
            covariance.xyRawPixelsSquared * covariance.xyRawPixelsSquared));
    low = 0.5 * trace - radius;
    high = 0.5 * trace + radius;
    return std::isfinite(low) && std::isfinite(high);
}

inline bool TryClampMotionCovariance(
    SymmetricRawCovariance input,
    double minimumSigma,
    double maximumSigma,
    SymmetricRawCovariance& result) {
    double low = 0.0;
    double high = 0.0;
    if (!TryMotionCovarianceEigenvalues(input, low, high)) return false;
    const double minimum = minimumSigma * minimumSigma;
    const double maximum = maximumSigma * maximumSigma;
    if (high < -1.0e-10 || low < -std::max(1.0e-10, 1.0e-8 * std::abs(high))) {
        return false;
    }
    const double angle = 0.5 * std::atan2(
        2.0 * input.xyRawPixelsSquared,
        input.xxRawPixelsSquared - input.yyRawPixelsSquared);
    const double clampedHigh = std::max(minimum, std::min(maximum, high));
    const double clampedLow = std::max(minimum, std::min(maximum, low));
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    result.xxRawPixelsSquared =
        clampedHigh * cosine * cosine + clampedLow * sine * sine;
    result.xyRawPixelsSquared =
        (clampedHigh - clampedLow) * cosine * sine;
    result.yyRawPixelsSquared =
        clampedHigh * sine * sine + clampedLow * cosine * cosine;
    return std::isfinite(result.xxRawPixelsSquared) &&
        std::isfinite(result.xyRawPixelsSquared) &&
        std::isfinite(result.yyRawPixelsSquared);
}


// Covariances describe displacement in the output axes of their motion map.
// Compose maps by transporting the first displacement through the second map:
// C_composed = J_second C_first J_second^T + C_second. This does not account
// for correlations between the two fitted maps.
inline bool TryTransportMotionCovariance(SymmetricRawCovariance input,
    const RawMappingJacobian& jacobian, SymmetricRawCovariance& output) {
    const auto valid=[](const SymmetricRawCovariance& c) {
        if(!std::isfinite(c.xxRawPixelsSquared)||!std::isfinite(c.xyRawPixelsSquared)||
           !std::isfinite(c.yyRawPixelsSquared)||c.xxRawPixelsSquared<0||c.yyRawPixelsSquared<0)return false;
        const double scale=std::max({c.xxRawPixelsSquared,std::abs(c.xyRawPixelsSquared),c.yyRawPixelsSquared});
        if(scale==0)return true;
        const double xx=c.xxRawPixelsSquared/scale,xy=c.xyRawPixelsSquared/scale,yy=c.yyRawPixelsSquared/scale;
        return xx*yy-xy*xy>=-64*std::numeric_limits<double>::epsilon()*std::max(xx*yy,xy*xy);
    };
    if(!jacobian.valid||!FiniteMappingJacobian(jacobian)||!valid(input))return false;
    const auto& j=jacobian.linear;
    // In particular, preserve translation-only closure arithmetic exactly.
    if(j==std::array<double,4>{1,0,0,1}){output=input;return true;}
    SymmetricRawCovariance transformed{
        j[0]*j[0]*input.xxRawPixelsSquared+2*j[0]*j[1]*input.xyRawPixelsSquared+j[1]*j[1]*input.yyRawPixelsSquared,
        j[0]*j[2]*input.xxRawPixelsSquared+(j[0]*j[3]+j[1]*j[2])*input.xyRawPixelsSquared+j[1]*j[3]*input.yyRawPixelsSquared,
        j[2]*j[2]*input.xxRawPixelsSquared+2*j[2]*j[3]*input.xyRawPixelsSquared+j[3]*j[3]*input.yyRawPixelsSquared};
    if(!valid(transformed))return false;
    output=transformed;return true;
}

// At a motion-grid knot, each supported side is a possible local linear model.
// Transport through every side and retain a PSD upper envelope instead of an
// average Jacobian, which can cancel opposing slopes. For D=C_next-E, adding
// its positive spectral part gives E_new=E+D_+: both E_new-E and E_new-C_next
// are PSD. The bounded, deterministic fold preserves identical/affine sides
// exactly and avoids inflating unaffected axes for commuting covariances.
//
// This is a first-order local-model envelope, not a calibrated posterior for
// uncertainty spanning whole cells or correlations between fitted links. Do
// not use this envelope to relax a closure gate: closure tests each side.
inline bool TryTransportMotionCovarianceEnvelope(SymmetricRawCovariance input,
    const RawMappingJacobianSet& linearizations,SymmetricRawCovariance& output) {
    if(linearizations.count==0||linearizations.count>linearizations.sides.size())return false;
    SymmetricRawCovariance envelope;
    if(!TryTransportMotionCovariance(input,linearizations.sides[0],envelope))return false;
    for(unsigned i=1;i<linearizations.count;++i) {
        SymmetricRawCovariance next;
        if(!TryTransportMotionCovariance(input,linearizations.sides[i],next))return false;
        const double xx=next.xxRawPixelsSquared-envelope.xxRawPixelsSquared;
        const double xy=next.xyRawPixelsSquared-envelope.xyRawPixelsSquared;
        const double yy=next.yyRawPixelsSquared-envelope.yyRawPixelsSquared;
        const double center=.5*xx+.5*yy;
        const double radius=std::hypot(.5*xx-.5*yy,xy);
        const double low=center-radius,high=center+radius;
        if(!std::isfinite(low)||!std::isfinite(high))return false;
        if(high<=0)continue;
        if(low>=0) {envelope=next;continue;}
        // The positive eigenspace is rank one. D-low*I is its projector
        // multiplied by high-low; no angle or eigenvector branch is needed.
        const double factor=.5*(high/radius);
        envelope.xxRawPixelsSquared+=factor*(xx-low);
        envelope.xyRawPixelsSquared+=factor*xy;
        envelope.yyRawPixelsSquared+=factor*(yy-low);
    }
    // Keep failure transactional, including nonfinite arithmetic or a
    // malformed covariance; the caller decides its evidence rejection.
    SymmetricRawCovariance checked;
    if(!TryTransportMotionCovariance(envelope,{{1,0,0,1},true},checked))return false;
    output=checked;return true;
}

// Closure is measured in the reverse map's output coordinates. At an internal
// knot every supported one-sided linearization must pass: use the largest
// Mahalanobis residual, not an averaged/dilated covariance that could create
// confidence merely because the derivative is ambiguous. Agreeing sides retain
// the exact affine/translation statistic. Unavailable evidence leaves output
// unchanged and lets the caller use its existing invalid-covariance rejection.
inline bool TryMotionClosureMahalanobis(RawCoordinate error,
    SymmetricRawCovariance forward,SymmetricRawCovariance reverse,double floorVariance,
    const std::array<RawMappingJacobian,4>& sides,unsigned count,double& output) {
    if(count==0||count>sides.size()||!std::isfinite(error.x)||!std::isfinite(error.y)||
       !std::isfinite(floorVariance)||floorVariance<0)return false;
    SymmetricRawCovariance checkedReverse;
    if(!TryTransportMotionCovariance(reverse,{{1,0,0,1},true},checkedReverse))return false;
    double largest=0;
    for(unsigned i=0;i<count;++i) {
        SymmetricRawCovariance transported;
        if(!TryTransportMotionCovariance(forward,sides[i],transported))return false;
        // Match the public field derivative's existing numerical-agreement
        // convention and retain its selected-side arithmetic at affine knots.
        if(i>0&&SameMappingJacobian(sides[0],sides[i]))continue;
        const double xx=transported.xxRawPixelsSquared+reverse.xxRawPixelsSquared+floorVariance;
        const double xy=transported.xyRawPixelsSquared+reverse.xyRawPixelsSquared;
        const double yy=transported.yyRawPixelsSquared+reverse.yyRawPixelsSquared+floorVariance;
        // Same invertibility convention and operation order as the former
        // closure gate. No upper clamp after transport: magnification matters.
        const double determinant=xx*yy-xy*xy;
        if(!std::isfinite(determinant)||determinant<=1e-20)return false;
        const double inverseXX=yy/determinant,inverseXY=-xy/determinant,inverseYY=xx/determinant;
        if(!std::isfinite(inverseXX)||!std::isfinite(inverseXY)||!std::isfinite(inverseYY))return false;
        const double xTerm=error.x*error.x*inverseXX,crossTerm=2*error.x*error.y*inverseXY,
            yTerm=error.y*error.y*inverseYY;
        const double value=xTerm+crossTerm+yTerm;
        const double roundoff=64*std::numeric_limits<double>::epsilon()*
            (std::abs(xTerm)+std::abs(crossTerm)+std::abs(yTerm));
        if(!std::isfinite(value)||!std::isfinite(roundoff)||value < -roundoff)return false;
        largest=std::max(largest,value);
    }
    output=largest;return true;
}

} // namespace Raw::Mfd
