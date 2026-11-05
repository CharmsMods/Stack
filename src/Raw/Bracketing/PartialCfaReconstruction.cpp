#include "PartialCfaReconstruction.h"
#include "LocalAffineFit.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace Raw::Bracketing {
namespace {
constexpr double kMaximumResidualSquared=16;
constexpr double kMaximumReducedResidual=4;
constexpr double kMaximumCoefficientMagnitude=2;
constexpr double kMaximumVarianceRatio=4;

bool Clipped(const Mfd::SameCfaTapInput& tap) {
    return Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Saturated)||
        Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::ExplicitDecoderClip);
}
bool Bad(const Mfd::SameCfaTapInput& tap) {
    return !std::isfinite(tap.normalizedSample)||!std::isfinite(tap.comparisonGain)||
        tap.comparisonGain<=0||
        Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::Defective)||
        Mfd::HasSampleFlag(tap.sampleFlags,Mfd::PreparedSampleFlag::DecoderRepaired);
}
double Headroom(double sample) {
    const double h=std::clamp((.995-sample)/.045,0.,1.);
    return h*h*(3-2*h);
}
struct FitTap {
    double x=0,y=0,value=0,variance=0,weight=0;
    bool unique=false,clipped=false;
};
}

bool ReconstructPartialCfaFootprint(
    const Mfd::KeysBicubicFootprint& footprint,
    const std::array<Mfd::SameCfaTapInput,Mfd::kSameCfaTapCount>& taps,
    const Mfd::SiteNoiseProfile& noiseProfile,
    double usableCodeSpanDn,
    const Mfd::SameCfaScalarParameters& parameters,
    Mfd::SameCfaSampleResult& result,
    double& headroom) {
    result={};headroom=0;
    if(!std::isfinite(parameters.numericalVarianceFloor)||parameters.numericalVarianceFloor<=0||
        !std::isfinite(footprint.coordinate.x)||!std::isfinite(footprint.coordinate.y))return false;

    std::array<FitTap,Mfd::kSameCfaTapCount> observations{};
    LocalAffineMoments<double> moments;
    bool missing=false,valueComplete=true;
    unsigned validCount=0;
    double centralMass=0,centralVariance=0,headroomSum=0,originalMagnitude=0;
    for(std::size_t i=0;i<taps.size();++i) {
        const auto& coordinate=footprint.taps[i];const auto& tap=taps[i];
        bool unique=true;
        for(std::size_t j=0;j<i;++j)
            if(coordinate.x==footprint.taps[j].x&&coordinate.y==footprint.taps[j].y)unique=false;
        if(!unique)continue;
        if(Bad(tap))return false;
        auto& o=observations[i];o.unique=true;o.clipped=Clipped(tap);
        o.x=double(coordinate.x)-footprint.coordinate.x;
        o.y=double(coordinate.y)-footprint.coordinate.y;
        const double gain=parameters.exposureScale*tap.comparisonGain;
        Mfd::GainPropagatedVariance propagated;
        if(!Mfd::PropagateKnownGainVariance(noiseProfile,gain,parameters.referenceComparisonPilot,
            usableCodeSpanDn,propagated))return false;
        o.value=gain*tap.normalizedSample;o.variance=propagated.variance;
        if(!std::isfinite(o.value))return false;
        const double centralWeight=std::max(0.,1-std::abs(o.x))*std::max(0.,1-std::abs(o.y));
        // The output must lie in an observed local cell. Unlike merely
        // normalizing the surviving cubic lobes, this cannot extrapolate
        // through a lost central measurement.
        if(centralWeight>0&&o.clipped)return false;
        centralMass+=centralWeight;
        centralVariance+=centralWeight*centralWeight*o.variance;
        const double magnitude=std::abs(footprint.coefficients[i]);
        originalMagnitude+=magnitude;
        headroomSum+=magnitude*(o.clipped?0:Headroom(tap.normalizedSample));
        if(o.clipped) {
            missing=true;
            if(footprint.coefficients[i]!=0)valueComplete=false;
            continue;
        }
        // Rational spatial taper keeps the CPU and GLSL double path on the
        // same arithmetic without relying on double-precision transcendentals.
        const double radius=1+.5*(o.x*o.x+o.y*o.y);
        o.weight=1/(radius*radius*std::max(parameters.numericalVarianceFloor,o.variance));
        moments.Add(o.x,o.y,o.value,o.weight);++validCount;
    }
    if(!missing||validCount<6||std::abs(centralMass-1)>1e-12||originalMagnitude<=0)return false;
    LocalAffineFit<double> fit;
    if(!moments.Fit(fit))return false;

    // Saturation makes the surviving sample locations asymmetric. A local
    // plane reproduces an affine signal despite that asymmetry (Kronander
    // et al., https://arxiv.org/abs/1308.4908, section 5.3). Its effective
    // coefficients may be signed, so conditioning alone is insufficient.
    auto reconstructed=footprint;
    reconstructed.coefficients={};reconstructed.derivativeXPlane={};reconstructed.derivativeYPlane={};
    reconstructed.coefficientSum=0;reconstructed.derivativeXSum=0;reconstructed.derivativeYSum=0;
    auto accepted=taps;
    double normalizedResidual=0,weightedResidual=0,weightedNoise=0;
    double valueVariance=0,coefficientMagnitude=0,gradientNoiseRisk=0;
    for(std::size_t i=0;i<observations.size();++i) {
        const auto& o=observations[i];
        if(!o.unique||o.clipped) {
            accepted[i]={};
            if(o.unique) {
                // A clipped value is a lower bound, not a sample for the
                // fit. Contradicting it signals an unseen edge/model failure.
                const double deficit=o.value-(fit.value+fit.slopeX*o.x+fit.slopeY*o.y);
                if(deficit>0&&deficit*deficit>kMaximumResidualSquared*
                    std::max(parameters.numericalVarianceFloor,o.variance))return false;
            }
            continue;
        }
        const auto coefficients=fit.Coefficients(o.x,o.y,o.weight);
        // Derivative-only loss must not smooth or move the original valid
        // value. It changes only the slope estimate and its uncertainty.
        const double valueCoefficient=valueComplete?footprint.coefficients[i]:coefficients[0];
        reconstructed.coefficients[i]=valueCoefficient;
        reconstructed.derivativeXPlane[i]=coefficients[1];
        reconstructed.derivativeYPlane[i]=coefficients[2];
        reconstructed.coefficientSum+=valueCoefficient;
        reconstructed.derivativeXSum+=coefficients[1];
        reconstructed.derivativeYSum+=coefficients[2];
        coefficientMagnitude+=std::abs(valueCoefficient);
        valueVariance+=valueCoefficient*valueCoefficient*o.variance;
        const double residual=o.value-(fit.value+fit.slopeX*o.x+fit.slopeY*o.y);
        const double standardized=residual*residual/
            std::max(parameters.numericalVarianceFloor,o.variance);
        if(!std::isfinite(standardized)||standardized>kMaximumResidualSquared)return false;
        normalizedResidual+=standardized;
        weightedResidual+=o.weight*residual*residual;
        weightedNoise+=o.weight*o.variance;
        double slopeRisk=0;
        if(!Mfd::RegistrationUncertaintyVariance({.5*coefficients[1],.5*coefficients[2]},
            parameters.warpCovariance,slopeRisk))return false;
        gradientNoiseRisk+=o.variance*slopeRisk;
    }
    if(normalizedResidual>kMaximumReducedResidual*(validCount-3)||
        coefficientMagnitude>kMaximumCoefficientMagnitude||
        valueVariance>kMaximumVarianceRatio*std::max(parameters.numericalVarianceFloor,centralVariance))return false;

    Mfd::SameCfaSampleResult evaluated;
    if(!Mfd::EvaluateSameCfaFootprintScalar(reconstructed,accepted,noiseProfile,usableCodeSpanDn,
        parameters,evaluated))return false;
    // Keep reconstruction/model risk distinct from sensor measurement noise.
    // It must survive a caller replacing the native gradient with a shared
    // scene gradient for registration uncertainty.
    const double extraModelVariance=std::max(0.,(weightedResidual-weightedNoise)/moments.weight)+gradientNoiseRisk;
    if(!std::isfinite(extraModelVariance)||extraModelVariance<0)return false;
    evaluated.residualModelVariance+=extraModelVariance;
    evaluated.gateVariance+=extraModelVariance;evaluated.fusionVariance+=extraModelVariance;
    if(!std::isfinite(evaluated.residualModelVariance)||!std::isfinite(evaluated.gateVariance)||
        !std::isfinite(evaluated.fusionVariance))return false;
    evaluated.message="Bracket same-CFA sample reconstructed from measured unclipped support.";
    headroom=headroomSum/originalMagnitude;
    result=std::move(evaluated);
    return true;
}

} // namespace Raw::Bracketing
