#pragma once

// Original C++17 profile mathematics for the authored optical provider.
// These are CPU reference/configuration helpers, not a runtime image fallback.
// A profile is an artistic sampling field, NOT an assertion about Apple's shader
// or a substitute for the separately supplied closed-volume Snell reference.

#include "glass_shape.hpp"
#include <limits>

namespace stack::glass {

// Subtract a group origin BEFORE narrowing to float. Performing this subtraction
// after a desktop coordinate has already been rounded to float cannot restore
// lost precision. Both the input and the origin are physical framebuffer pixels.
struct OpticalLocalOrigin {
    double xPx = 0.0;
    double yPx = 0.0;
    bool valid() const { return std::isfinite(xPx) && std::isfinite(yPx); }
};

inline bool toOpticalLocal(double framebufferXPx, double framebufferYPx,
                           OpticalLocalOrigin origin, Vec2& result) {
    if (!origin.valid() || !std::isfinite(framebufferXPx) || !std::isfinite(framebufferYPx))
        return false;
    const double x = framebufferXPx - origin.xPx, y = framebufferYPx - origin.yPx;
    constexpr double limit = std::numeric_limits<float>::max();
    if (!std::isfinite(x) || !std::isfinite(y) || std::abs(x) > limit || std::abs(y) > limit)
        return false;
    result = {static_cast<float>(x), static_cast<float>(y)};
    return true;
}

// A material's sampling fold and its lighting normal are INDEPENDENT controls.
// Do not derive the glass surface normal from the derivative of this screen warp:
// that would make a texture caustic turn into an unintended physical ridge.
// All lengths below are physical framebuffer pixels, scaled together for DPI.
enum class EdgeProfileKind { Monotone = 0, Fold = 1, Circular = 2 };

struct EdgeProfile {
    float widthPx = 12.0f;         // extent inward from the silhouette
    float reachPx = 24.0f;         // maximum inward source displacement
    float peakFraction = 0.45f;    // depth of maximum reach, as a fraction of width
    EdgeProfileKind kind = EdgeProfileKind::Monotone;
    float circularRegularization = 0.08f; // finite one-sided outer-edge slope

    bool valid() const {
        if (!std::isfinite(widthPx) || !std::isfinite(reachPx) ||
            !std::isfinite(peakFraction) || widthPx <= 0 || reachPx < 0 ||
            peakFraction <= 0 || peakFraction >= 1 ||
            !std::isfinite(circularRegularization) || circularRegularization <= 0 ||
            circularRegularization > 1 ||
            (kind != EdgeProfileKind::Monotone && kind != EdgeProfileKind::Fold &&
             kind != EdgeProfileKind::Circular)) return false;
        // The tightest ramp determines derivative magnitude. Reject values that
        // would overflow float output; no silent parameter clamping is performed.
        const double ramp = static_cast<double>(widthPx) *
            (kind == EdgeProfileKind::Monotone ? 1.0 :
             std::min(static_cast<double>(peakFraction), 1.0 - peakFraction));
        const double e = circularRegularization;
        const double maxSlope = kind == EdgeProfileKind::Circular ?
            reachPx * (1.0 + e) / (static_cast<double>(widthPx) * e) : 1.875 * reachPx / ramp;
        const double maxCurvature = kind == EdgeProfileKind::Circular ?
            reachPx * (1.0 + e) / (static_cast<double>(widthPx) * widthPx * e * e * e) :
            (10.0 / std::sqrt(3.0)) * reachPx / (ramp * ramp);
        constexpr double limit = std::numeric_limits<float>::max();
        return std::isfinite(maxSlope) && std::isfinite(maxCurvature) &&
            maxSlope <= limit && maxCurvature <= limit;
    }
};

using EdgeFoldProfile = EdgeProfile; // Naming compatibility with the first curve-only draft.

struct EdgeFoldSample {
    float displacementPx = 0.0f;
    float dDisplacementDDepth = 0.0f;    // dimensionless; depth grows inward
    float d2DisplacementDDepth2 = 0.0f;  // inverse pixels
    bool valid = false;
};
using EdgeProfileSample = EdgeFoldSample;

// Quintic transition Q(t) and its first two derivatives. Q is the integral of
// 30*t^2*(1-t)^2 on [0,1]. Keeping these in double makes endpoint cancellation
// benign for the CPU oracle. The GLSL counterpart may use float Horner form.
struct QuinticSample {
    double value = 0.0;
    double derivative = 0.0;
    double secondDerivative = 0.0;
};
inline QuinticSample quinticTransition(double t) {
    if (t <= 0.0) return {};
    if (t >= 1.0) return {1.0, 0.0, 0.0};
    const double oneMinusT = 1.0 - t;
    return {t * t * t * (10.0 + t * (-15.0 + 6.0 * t)),
            30.0 * t * t * oneMinusT * oneMinusT,
            60.0 * t * oneMinusT * (1.0 - 2.0 * t)};
}

// signedDistancePx is negative INSIDE the shape. All curves become exactly zero
// at depth >= width. Monotone/Fold are C2 at that join; Circular is C1 there.
// Monotone: D=reach*(1-Q(depth/width)); maximum at the outer boundary. It extends
// as a CONSTANT through exterior AA samples; apply silhouette coverage separately.
// Fold: compact bell with zero value/first/second derivatives at both endpoints.
// Circular: rationalized, finite-slope circle-drop family; maximum at the outer
// boundary, constant exterior AA extension, finite one-sided interior derivatives.
// The exterior extension has a slope jump at depth=0; do not claim a C1 join there.
// A large reach intentionally permits a local source-image fold in either case.
inline EdgeFoldSample sampleEdgeProfile(float signedDistancePx, const EdgeProfile& p) {
    if (!p.valid() || !std::isfinite(signedDistancePx)) return {};
    if (signedDistancePx <= -p.widthPx || p.reachPx == 0.0f)
        return {0.0f, 0.0f, 0.0f, true};
    if (p.kind == EdgeProfileKind::Circular) {
        if (signedDistancePx >= 0) return {p.reachPx, 0.0f, 0.0f, true};
        const double u = -static_cast<double>(signedDistancePx) / p.widthPx;
        const double e = p.circularRegularization, t = 1.0 - u;
        const double s = std::sqrt(e * e + (1.0 - e * e) * u * (2.0 - u));
        const double amplitude = p.reachPx * (1.0 + e);
        // Rationalization avoids subtracting almost equal values at the clear
        // interior and remains well-defined at e=1 (the quadratic limit).
        return {static_cast<float>(amplitude * t * t / (1.0 + s)),
                static_cast<float>(-amplitude * t / (p.widthPx * s)),
                static_cast<float>(amplitude / (static_cast<double>(p.widthPx) * p.widthPx) *
                                   (1.0 / s + (1.0 - e * e) * t * t / (s * s * s))), true};
    }
    if (p.kind == EdgeProfileKind::Monotone) {
        if (signedDistancePx >= 0) return {p.reachPx, 0.0f, 0.0f, true};
        const QuinticSample q = quinticTransition(-static_cast<double>(signedDistancePx) / p.widthPx);
        return {static_cast<float>(p.reachPx * (1.0 - q.value)),
                static_cast<float>(-p.reachPx * q.derivative / p.widthPx),
                static_cast<float>(-p.reachPx * q.secondDerivative /
                                   (static_cast<double>(p.widthPx) * p.widthPx)), true};
    }
    if (signedDistancePx >= 0.0f) return {0.0f, 0.0f, 0.0f, true};
    const double depth = -static_cast<double>(signedDistancePx);
    const double peak = static_cast<double>(p.widthPx) * p.peakFraction;
    const bool rising = depth <= peak;
    const double rampWidth = rising ? peak : static_cast<double>(p.widthPx) - peak;
    const double t = rising ? depth / rampWidth : (p.widthPx - depth) / rampWidth;
    const QuinticSample q = quinticTransition(t);
    const double slopeSign = rising ? 1.0 : -1.0;
    return {static_cast<float>(p.reachPx * q.value),
            static_cast<float>(p.reachPx * q.derivative * slopeSign / rampWidth),
            static_cast<float>(p.reachPx * q.secondDerivative / (rampWidth * rampWidth)), true};
}

inline EdgeFoldSample sampleEdgeFold(float signedDistancePx, const EdgeFoldProfile& p) {
    return sampleEdgeProfile(signedDistancePx, p);
}

struct ProfileWarpSample {
    Vec2 samplePositionPx{};
    float displacementPx = 0.0f;
    float gradientLength = 0.0f;
    bool valid = false;
};

// Uses the existing analytic field and its ACTUAL derivative. Smooth unions
// should use smoothUnion() from glass_shape.hpp without normalizing its gradient
// during field construction. Only the final sampling direction is normalized.
// Near a stationary smooth-union neck, normalizing g directly is singular. The
// default g/sqrt(dot(g,g)+0.04^2) smoothly damps that warp to zero. This changes a
// unit-gradient edge's reach by only about 0.08%. Set regularization to zero ONLY
// for a diagnostic comparison; at stationary points that mode cannot be smooth.
inline ProfileWarpSample sampleProfileWarp(Vec2 localPixelPx, ShapeSample field,
                                          const EdgeProfile& p, float activation = 1.0f,
                                          float gradientRegularization = 0.04f) {
    if (!finite(localPixelPx) || !finite(field.gradient) || !std::isfinite(field.distance) ||
        !std::isfinite(activation) || activation < 0 || activation > 1 ||
        !std::isfinite(gradientRegularization) || gradientRegularization < 0) return {};
    const EdgeFoldSample fold = sampleEdgeProfile(field.distance, p);
    if (!fold.valid) return {};
    const double gx = field.gradient.x, gy = field.gradient.y;
    const double gradLength = std::hypot(gx, gy);
    if (!std::isfinite(gradLength) || gradLength > std::numeric_limits<float>::max()) return {};
    const float displacement = fold.displacementPx * activation;
    if (gradLength == 0.0 || displacement == 0.0f)
        return {localPixelPx, 0.0f, static_cast<float>(gradLength), true};
    const double divisor = std::hypot(gradLength, static_cast<double>(gradientRegularization));
    const Vec2 normal{static_cast<float>(gx / divisor), static_cast<float>(gy / divisor)};
    const Vec2 sampled = localPixelPx - normal * displacement;
    if (!finite(sampled)) return {};
    return {sampled, static_cast<float>(displacement * gradLength / divisor),
            static_cast<float>(gradLength), true};
}

// Diagnostic for a locally straight true-SDF edge. Without regularization the
// map is p - n*D(-d), and its normal Jacobian eigenvalue is 1 + D'(depth), NOT
// 1 - D'(depth). The supplied regularization scales n by 1/sqrt(1+epsilon^2).
// Negative
// values represent an intentional fold/inversion; zero is a source caustic.
// This is NOT the full 2D Jacobian of a curved or smooth-unioned field. The GPU
// must filter using derivatives of the final sample position (or a full Hessian).
inline float foldNormalSamplingStretch(const EdgeFoldSample& s, float activation = 1.0f,
                                      float gradientRegularization = 0.04f) {
    if (!s.valid || !std::isfinite(s.dDisplacementDDepth) || !std::isfinite(activation) ||
        activation < 0 || activation > 1 || !std::isfinite(gradientRegularization) ||
        gradientRegularization < 0) return std::numeric_limits<float>::quiet_NaN();
    const double normalScale = 1.0 / std::hypot(1.0, static_cast<double>(gradientRegularization));
    return static_cast<float>(1.0 + activation * normalScale * s.dDisplacementDDepth);
}

// Exact lower bound over a straight-edge profile. For Circular it is the infimum
// at the one-sided outer edge; for the C2 families max Q'(t)=1.875. Use to diagnose
// whether a preset deliberately folds, not as a quality score.
inline float minimumFoldNormalSamplingStretch(const EdgeProfile& p, float activation = 1.0f,
                                             float gradientRegularization = 0.04f) {
    if (!p.valid() || !std::isfinite(activation) || activation < 0 || activation > 1 ||
        !std::isfinite(gradientRegularization) || gradientRegularization < 0)
        return std::numeric_limits<float>::quiet_NaN();
    const double fallingWidth = static_cast<double>(p.widthPx) *
        (p.kind == EdgeProfileKind::Monotone ? 1.0 : 1.0 - p.peakFraction);
    const double normalScale = 1.0 / std::hypot(1.0, static_cast<double>(gradientRegularization));
    if (p.kind == EdgeProfileKind::Circular)
        return static_cast<float>(1.0 - activation * normalScale * p.reachPx *
            (1.0 + p.circularRegularization) / (static_cast<double>(p.widthPx) * p.circularRegularization));
    return static_cast<float>(1.0 - activation * normalScale * 1.875 * p.reachPx / fallingWidth);
}

// Conservative source padding for this provider. Inward mapping can still leave
// a cropped source ROI at a curved/merged feature, so the capture contract must
// include this amount in every direction in addition to the reconstruction and
// roughness-filter support. No screen-size multiplier or hidden global offset.
inline float requiredProfilePaddingPx(const EdgeFoldProfile& p, float filterSupportPx) {
    if (!p.valid() || !std::isfinite(filterSupportPx) || filterSupportPx < 0)
        return std::numeric_limits<float>::quiet_NaN();
    const double sum = static_cast<double>(p.reachPx) + filterSupportPx;
    return sum <= std::numeric_limits<float>::max() ? static_cast<float>(sum) :
        std::numeric_limits<float>::quiet_NaN();
}

} // namespace stack::glass
