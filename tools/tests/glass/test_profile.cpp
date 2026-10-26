#include "Renderer/Glass/glass_profile.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace stack::glass;

namespace {
int checks = 0, failures = 0;
void expect(bool pass, const char* message) {
    ++checks;
    if (!pass) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
bool near(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }
bool near(Vec2 a, Vec2 b, float tolerance) { return length(a - b) <= tolerance; }

void testSupportAndJoins() {
    for (const float peak : {0.13f, 0.5f, 0.86f}) {
        const EdgeFoldProfile p{17.0f, 31.0f, peak, EdgeProfileKind::Fold};
        expect(p.valid(), "normal profile valid");
        for (const float d : {-1000.0f, -17.0f, 0.0f, 1000.0f}) {
            const auto s = sampleEdgeFold(d, p);
            expect(s.valid && s.displacementPx == 0 && s.dDisplacementDDepth == 0 &&
                       s.d2DisplacementDDepth2 == 0, "exact compact support and clear center");
        }
        const auto maximum = sampleEdgeFold(-p.widthPx * peak, p);
        expect(near(maximum.displacementPx, p.reachPx, 1.0e-5), "peak equals requested reach");
        expect(std::abs(maximum.dDisplacementDDepth) < 1.0e-8 &&
                   std::abs(maximum.d2DisplacementDDepth2) < 2.0e-5, "peak is a C2 join");
        for (const float join : {0.0f, -p.widthPx * peak, -p.widthPx}) {
            const float epsilon = 1.0e-5f;
            const auto a = sampleEdgeFold(join - epsilon, p), b = sampleEdgeFold(join + epsilon, p);
            expect(near(a.displacementPx, b.displacementPx, 1.0e-5), "displacement continuous at join");
            expect(near(a.dDisplacementDDepth, b.dDisplacementDDepth, 1.0e-7), "slope continuous at join");
            expect(near(a.d2DisplacementDDepth2, b.d2DisplacementDDepth2, 0.002), "curvature continuous at join");
        }
        for (int i = 0; i <= 200; ++i) {
            const auto s = sampleEdgeFold(-p.widthPx * i / 200.0f, p);
            expect(s.valid && std::isfinite(s.displacementPx) &&
                       std::isfinite(s.dDisplacementDDepth) && std::isfinite(s.d2DisplacementDDepth2),
                   "entire profile finite");
            expect(s.displacementPx >= 0 && s.displacementPx <= p.reachPx + 1.0e-5f,
                   "reach bounded without clamping the warp");
        }
    }
}

void testIndependentIntegralAndDifferentials() {
    // Every quintic transition has mean 1/2; therefore a two-ramp fold has area
    // reach*width/2 regardless of where the peak sits. Numerical integration of
    // sampled output checks that invariant without repeating the polynomial.
    for (const float peak : {0.2f, 0.47f, 0.8f}) {
        const EdgeFoldProfile p{20.0f, 13.0f, peak, EdgeProfileKind::Fold};
        constexpr int n = 4096;
        double area = 0, slopeIntegral = 0;
        for (int i = 0; i < n; ++i) {
            const float depth = p.widthPx * (i + 0.5f) / n;
            const auto s = sampleEdgeFold(-depth, p);
            area += s.displacementPx * p.widthPx / n;
            slopeIntegral += s.dDisplacementDDepth * p.widthPx / n;
        }
        expect(near(area, 0.5 * p.reachPx * p.widthPx, 1.0e-4), "independent fold-area invariant");
        expect(near(slopeIntegral, 0.0, 2.0e-5), "integral of fold slope vanishes");
        for (int i = 1; i < 100; ++i) {
            const float depth = p.widthPx * i / 100.0f, h = 0.002f;
            const auto s = sampleEdgeFold(-depth, p);
            const float loDepth = depth - h, hiDepth = depth + h;
            const auto lo = sampleEdgeFold(-loDepth, p), hi = sampleEdgeFold(-hiDepth, p);
            const double actualStep = static_cast<double>(hiDepth) - loDepth;
            const double numericalSlope = (hi.displacementPx - lo.displacementPx) / actualStep;
            const double numericalCurvature = (hi.dDisplacementDDepth - lo.dDisplacementDDepth) / actualStep;
            expect(near(s.dDisplacementDDepth, numericalSlope, 0.0015), "analytic slope matches numerical differentiation");
            // A C2 join has a continuous second derivative but its derivative
            // need not be continuous; a centered stencil crossing that join
            // converges linearly. Continuity there is tested separately above.
            const double joinDistance = std::abs(depth - p.widthPx * p.peakFraction);
            if (joinDistance > h)
                expect(near(s.d2DisplacementDDepth2, numericalCurvature, 0.0015), "analytic curvature matches numerical differentiation");
        }
    }
}

void testMonotoneProfile() {
    const EdgeProfile p{18, 29, 0.4f, EdgeProfileKind::Monotone};
    expect(p.valid() && EdgeProfile{}.kind == EdgeProfileKind::Monotone,
           "monotone edge profile is the explicit default");
    for (const float d : {0.0f, 0.001f, 1000.0f}) {
        const auto s = sampleEdgeProfile(d, p);
        expect(s.valid && s.displacementPx == p.reachPx && s.dDisplacementDDepth == 0 &&
                   s.d2DisplacementDDepth2 == 0, "monotone boundary extends constantly through exterior AA");
    }
    for (const float d : {-18.0f, -18.01f, -1000.0f}) {
        const auto s = sampleEdgeProfile(d, p);
        expect(s.valid && s.displacementPx == 0 && s.dDisplacementDDepth == 0 &&
                   s.d2DisplacementDDepth2 == 0, "monotone clear interior is exactly unchanged");
    }
    const auto middle = sampleEdgeProfile(-p.widthPx * 0.5f, p);
    expect(near(middle.displacementPx, p.reachPx * 0.5, 1.0e-6), "monotone half-depth is half-reach");
    expect(middle.d2DisplacementDDepth2 == 0, "monotone midpoint is inflection");
    expect(near(foldNormalSamplingStretch(middle), minimumFoldNormalSamplingStretch(p), 1.0e-6),
           "monotone maximum sampling stretch occurs at midpoint");
    double area = 0, integratedSlope = 0;
    float previous = p.reachPx;
    constexpr int n = 2048;
    for (int i = 0; i < n; ++i) {
        const float depth = p.widthPx * (i + 0.5f) / n;
        const auto s = sampleEdgeProfile(-depth, p);
        expect(s.displacementPx <= previous && s.displacementPx >= 0 &&
                   s.dDisplacementDDepth <= 0, "entire monotone branch remains decreasing and bounded");
        previous = s.displacementPx;
        area += s.displacementPx * p.widthPx / n;
        integratedSlope += s.dDisplacementDDepth * p.widthPx / n;
    }
    expect(near(area, p.widthPx * p.reachPx * 0.5, 1.0e-4), "independent monotone area invariant");
    expect(near(integratedSlope, -p.reachPx, 1.0e-4), "monotone integrated slope equals boundary-to-interior change");
    for (int i = 1; i < 100; ++i) {
        const float depth = p.widthPx * i / 100.0f, h = 0.003f;
        const float lo = depth - h, hi = depth + h;
        const auto a = sampleEdgeProfile(-lo, p), b = sampleEdgeProfile(-hi, p);
        const auto s = sampleEdgeProfile(-depth, p);
        const double step = static_cast<double>(hi) - lo;
        expect(near((b.displacementPx - a.displacementPx) / step, s.dDisplacementDDepth, 0.001),
               "monotone derivative agrees with sampled displacement");
        expect(near((b.dDisplacementDDepth - a.dDisplacementDDepth) / step, s.d2DisplacementDDepth2, 0.001),
               "monotone curvature agrees with sampled derivative");
        EdgeProfile unusedPeak = p;
        unusedPeak.peakFraction = 0.8f;
        expect(sampleEdgeProfile(-depth, unusedPeak).displacementPx == s.displacementPx,
               "monotone shape is independent of unused fold peak control");
    }
}

void testRegularizedNeck() {
    const EdgeProfile p{12, 20, 0.5f, EdgeProfileKind::Monotone};
    const auto field = [](float x) {
        return smoothUnion({x, {1, 0}}, {-x, {-1, 0}}, 8);
    };
    const auto center = sampleProfileWarp({0, 0}, field(0), p);
    expect(center.valid && near(center.samplePositionPx, {0, 0}, 0), "stationary neck has zero warp");
    float previousDistance = std::numeric_limits<float>::max();
    for (const float x : {0.01f, 0.001f, 0.0001f, 0.00001f}) {
        const auto positive = sampleProfileWarp({x, 0}, field(x), p);
        const auto negative = sampleProfileWarp({-x, 0}, field(-x), p);
        const float distance = length(positive.samplePositionPx);
        expect(positive.valid && negative.valid && finite(positive.samplePositionPx), "near-neck mapping stays finite");
        expect(near(positive.samplePositionPx, -negative.samplePositionPx, 1.0e-4f), "neck damping remains symmetric");
        expect(distance < previousDistance, "warped neck position continuously approaches center");
        expect(distance <= x + p.reachPx * length(field(x).gradient) / 0.04f + 1.0e-5f,
               "regularized neck obeys finite Lipschitz bound");
        previousDistance = distance;
    }
    const auto regularized = sampleProfileWarp({0.0001f, 0}, field(0.0001f), p);
    const auto raw = sampleProfileWarp({0.0001f, 0}, field(0.0001f), p, 1, 0);
    expect(length(regularized.samplePositionPx) < 0.01f * length(raw.samplePositionPx),
           "default regularization removes normalized-gradient direction jump");
    const auto unitGradient = sampleProfileWarp({0, 0}, {-1, {1, 0}}, p);
    const auto profile = sampleEdgeProfile(-1, p);
    const double expected = profile.displacementPx / std::sqrt(1.0 + 0.04 * 0.04);
    expect(near(unitGradient.displacementPx, expected, 2.0e-6), "unit-gradient reach uses documented small regularization factor");
    expect(profile.displacementPx - unitGradient.displacementPx < profile.displacementPx * 0.00081,
           "ordinary edge reach changes by less than 0.081 percent");
}

void testCircularProfile() {
    for (const float epsilon : {0.08f, 0.15696f, 0.5f, 1.0f}) {
        const EdgeProfile p{38.4f, 55.13f, 0.4f, EdgeProfileKind::Circular, epsilon};
        expect(p.valid(), "finite-slope circular family valid");
        const auto outer = sampleEdgeProfile(0, p), exterior = sampleEdgeProfile(0.01f, p);
        const auto clear = sampleEdgeProfile(-p.widthPx, p);
        expect(outer.displacementPx == p.reachPx && exterior.displacementPx == p.reachPx &&
                   outer.dDisplacementDDepth == 0, "circular exterior AA extension stays constant");
        expect(clear.displacementPx == 0 && clear.dDisplacementDDepth == 0,
               "circular clear interior reaches exact identity with zero slope");
        const auto insideEdge = sampleEdgeProfile(-p.widthPx * 1.0e-7f, p);
        const double boundarySlope = -p.reachPx * (1.0 + epsilon) / (p.widthPx * epsilon);
        expect(near(insideEdge.dDisplacementDDepth, boundarySlope, 0.002),
               "circular one-sided outer slope is finite and independently bounded");
        expect(near(foldNormalSamplingStretch(insideEdge), minimumFoldNormalSamplingStretch(p), 0.002),
               "circular minimum stretch is its one-sided boundary infimum");
        const auto insideClear = sampleEdgeProfile(-p.widthPx * (1.0f - 1.0e-5f), p);
        expect(insideClear.displacementPx < 1.0e-7 && std::abs(insideClear.dDisplacementDDepth) < 0.0001,
               "circular curve joins the clear region continuously through first derivative");
        expect(near(insideClear.d2DisplacementDDepth2,
                    p.reachPx * (1.0 + epsilon) / (p.widthPx * p.widthPx), 1.0e-5),
               "circular C1 join does not falsely claim vanishing second derivative");
        for (int i = 1; i < 100; ++i) {
            const float depth = p.widthPx * i / 100.0f, h = 0.0015f;
            const auto s = sampleEdgeProfile(-depth, p);
            const double u = static_cast<double>(depth) / p.widthPx;
            // Independent implicit circular-section equation, rather than the
            // rationalized displacement implementation being tested.
            const double recoveredS = 1.0 - (1.0 - epsilon) * s.displacementPx / p.reachPx;
            const double implicit = recoveredS * recoveredS +
                (1.0 - epsilon * epsilon) * (1.0 - u) * (1.0 - u);
            expect(near(implicit, 1, 3.0e-7), "circular output satisfies independent implicit section equation");
            if (epsilon == 1)
                expect(near(s.displacementPx, p.reachPx * (1.0 - u) * (1.0 - u), 4.0e-6),
                       "regularization one gives exact independent quadratic limit");
            const float loDepth = depth - h, hiDepth = depth + h;
            const auto lo = sampleEdgeProfile(-loDepth, p), hi = sampleEdgeProfile(-hiDepth, p);
            const double step = static_cast<double>(hiDepth) - loDepth;
            expect(near((hi.displacementPx - lo.displacementPx) / step, s.dDisplacementDDepth, 0.002),
                   "circular analytic slope matches numerical differentiation");
            expect(near((hi.dDisplacementDDepth - lo.dDisplacementDDepth) / step,
                        s.d2DisplacementDDepth2, 0.001),
                   "circular analytic curvature matches numerical differentiation");
            EdgeProfile scaled = p; scaled.widthPx *= 3; scaled.reachPx *= 3;
            const auto threeX = sampleEdgeProfile(-depth * 3, scaled);
            expect(near(threeX.displacementPx, 3.0 * s.displacementPx, 3.0e-5) &&
                       near(threeX.dDisplacementDDepth, s.dDisplacementDDepth, 1.0e-5) &&
                       near(threeX.d2DisplacementDDepth2, s.d2DisplacementDDepth2 / 3, 1.0e-5),
                   "circular family obeys physical-pixel DPI covariance");
        }
        // Integrating a circular section gives an arcsine area. This tests the
        // profile over the whole interval using an independently derived result.
        const double root = std::sqrt(1.0 - epsilon * epsilon);
        const double expectedArea = epsilon == 1 ? p.reachPx * p.widthPx / 3.0 :
            p.reachPx * p.widthPx / (1.0 - epsilon) *
            (1.0 - 0.5 * (epsilon + std::asin(root) / root));
        double area = 0;
        constexpr int n = 8192;
        for (int i = 0; i < n; ++i)
            area += sampleEdgeProfile(-p.widthPx * (i + 0.5f) / n, p).displacementPx * p.widthPx / n;
        expect(near(area, expectedArea, 0.0002), "circular integral matches independent section area");
    }
}

void testWarpAndJacobian() {
    const EdgeFoldProfile p{16.0f, 27.0f, 0.4f, EdgeProfileKind::Fold};
    const Vec2 center{0, 0}, halfSize{80, 50};
    for (const Vec2 point : {Vec2{0, 0}, Vec2{10, 0}, Vec2{0, 15}}) {
        const auto s = sampleProfileWarp(point, sampleRoundedBox(point, center, halfSize, 20), p);
        expect(s.valid && near(s.samplePositionPx, point, 0.0f), "clear center exactly identity");
    }
    const Vec2 edgePoint{78, 0};
    const auto f = sampleRoundedBox(edgePoint, center, halfSize, 20);
    const auto warp = sampleProfileWarp(edgePoint, f, p);
    expect(warp.valid && warp.samplePositionPx.x < edgePoint.x && warp.samplePositionPx.y == edgePoint.y,
           "edge source moves inward along analytic normal");
    expect(near(sampleProfileWarp(edgePoint, f, p, 0).samplePositionPx, edgePoint, 0),
           "zero activation exactly preserves source mapping");
    expect(near(sampleProfileWarp(edgePoint, f, p, 0.5f).samplePositionPx,
                (edgePoint + warp.samplePositionPx) * 0.5f, 1.0e-5f), "activation changes displacement continuously");

    const auto fixedNormalMap = [&p](float x) {
        // Vertical edge at x=0, inside x<0; independent mapped-position result.
        return sampleProfileWarp({x, 0}, {x, {1, 0}}, p).samplePositionPx.x;
    };
    float measuredMinimum = 1.0f;
    int foldedSamples = 0;
    for (int i = 1; i < 300; ++i) {
        const float x = -p.widthPx * i / 300.0f, h = 0.002f;
        const auto s = sampleEdgeFold(x, p);
        const float jacobian = foldNormalSamplingStretch(s);
        const float fd = (fixedNormalMap(x + h) - fixedNormalMap(x - h)) / (2 * h);
        expect(near(jacobian, fd, 0.004), "texture Jacobian sign and magnitude from independent mapped positions");
        measuredMinimum = std::min(measuredMinimum, jacobian);
        if (jacobian < 0) ++foldedSamples;
    }
    expect(foldedSamples > 20, "strong profile contains intentional localized image inversion");
    expect(near(measuredMinimum, minimumFoldNormalSamplingStretch(p), 0.002), "analytic maximum fold stretch bound");
    const EdgeFoldProfile noFold{16.0f, 0.1f, 0.4f, EdgeProfileKind::Fold};
    expect(minimumFoldNormalSamplingStretch(noFold) > 0, "weak profile can remain monotonic");
    expect(near(requiredProfilePaddingPx(p, 3), 30, 0), "source capture includes reach plus filter support");
}

void testDpiAndLocalPrecision() {
    const EdgeFoldProfile a{13, 19, 0.31f, EdgeProfileKind::Fold},
                          b{39, 57, 0.31f, EdgeProfileKind::Fold};
    for (int i = 0; i <= 100; ++i) {
        const float d = -13.0f * i / 100.0f;
        const auto x = sampleEdgeFold(d, a), y = sampleEdgeFold(d * 3, b);
        expect(near(y.displacementPx, x.displacementPx * 3, 3.0e-5), "DPI covariance of displacement");
        expect(near(y.dDisplacementDDepth, x.dDisplacementDDepth, 5.0e-6), "DPI invariant texture stretch");
        expect(near(y.d2DisplacementDDepth2, x.d2DisplacementDDepth2 / 3, 1.0e-5), "curvature rescales with DPI");
    }
    const OpticalLocalOrigin origin{1.0e9, -1.0e9};
    Vec2 local;
    expect(toOpticalLocal(origin.xPx + 12.125, origin.yPx - 4.375, origin, local) &&
               near(local, {12.125f, -4.375f}, 0), "subtract double origin before float preserves subpixel coordinates");
    const Vec2 prematurelyRounded{static_cast<float>(origin.xPx + 12.125),
                                 static_cast<float>(origin.yPx - 4.375)};
    expect(prematurelyRounded.x == static_cast<float>(origin.xPx), "fixture demonstrates information lost by global float");
    const auto shiftedShape = sampleRoundedBox(local, {0, 0}, {20, 10}, 5);
    const auto originShape = sampleRoundedBox({12.125f, -4.375f}, {0, 0}, {20, 10}, 5);
    expect(shiftedShape.distance == originShape.distance && near(shiftedShape.gradient, originShape.gradient, 0),
           "shape and normal invariant under distant desktop translation");
}

void testAnalyticFields() {
    // Same analytic primitives are used in the GLSL handoff. Differentiate the
    // SDF itself away from its known medial-axis/corner-join nondifferentiabilities.
    const auto shape = [](Vec2 p) { return sampleRoundedBox(p, {0, 0}, {43, 28}, 13); };
    const auto group = [&shape](Vec2 p) {
        return smoothUnion(shape(p - Vec2{-32, 0}), shape(p - Vec2{32, 0}), 14);
    };
    for (int iy = -9; iy <= 9; ++iy) for (int ix = -13; ix <= 13; ++ix) {
        const Vec2 p{ix * 4.73f + 0.613f, iy * 3.97f + 0.327f};
        const auto g = group(p);
        const float h = 0.01f;
        const Vec2 fd{(group(p + Vec2{h, 0}).distance - group(p - Vec2{h, 0}).distance) / (2 * h),
                      (group(p + Vec2{0, h}).distance - group(p - Vec2{0, h}).distance) / (2 * h)};
        expect(length(g.gradient) <= 1.00001f, "compact smooth union retains gradient magnitude");
        expect(near(g.gradient, fd, 0.0015f), "rounded-box union analytic gradient matches independent differences");
    }
    const auto stationary = smoothUnion({-2, {1, 0}}, {-2, {-1, 0}}, 8);
    expect(length(stationary.gradient) == 0, "symmetric smooth union has zero gradient, not invented unit normal");
    const auto warp = sampleProfileWarp({0, 0}, stationary, {12, 20, 0.5f});
    expect(warp.valid && near(warp.samplePositionPx, {0, 0}, 0), "stationary union point has explicit zero-warp policy");
}

void testValidation() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    for (const EdgeFoldProfile p : {EdgeFoldProfile{0, 1, 0.5f}, {1, -1, 0.5f}, {1, 1, 0},
                                   {1, 1, 1}, {nan, 1, 0.5f}, {1, inf, 0.5f}, {1, 1, nan},
                                   {1.0e-30f, 1.0e30f, 0.5f}}) {
        expect(!p.valid() && !sampleEdgeFold(-0.5f, p).valid, "invalid or overflowing profile rejected");
    }
    expect(!sampleEdgeFold(nan, {}).valid, "NaN distance rejected");
    auto circular = EdgeProfile{}; circular.kind = EdgeProfileKind::Circular; circular.circularRegularization = 0;
    expect(!circular.valid() && !sampleEdgeProfile(-1, circular).valid, "singular unregularized circular profile rejected");
    circular.circularRegularization = nan;
    expect(!circular.valid(), "nonfinite circular regularization rejected");
    expect(std::isnan(foldNormalSamplingStretch({})), "invalid sample does not masquerade as unit Jacobian");
    expect(std::isnan(foldNormalSamplingStretch(sampleEdgeFold(-1, {}), nan)), "invalid Jacobian activation rejected");
    expect(sampleEdgeFold(-3, {12, 0, 0.5f}).displacementPx == 0, "zero reach is a valid identity profile");
    expect(!sampleProfileWarp({0, 0}, {-1, {1, 0}}, {}, -0.1f).valid, "negative activation rejected");
    expect(!sampleProfileWarp({0, 0}, {-1, {1, 0}}, {}, 1.1f).valid, "activation above one rejected");
    expect(!sampleProfileWarp({0, 0}, {-1, {nan, 0}}, {}).valid, "invalid gradient rejected");
    expect(!sampleProfileWarp({0, 0}, {-1, {1, 0}}, {}, 1, -1).valid, "negative gradient regularization rejected");
    Vec2 sentinel{17, 23};
    expect(!toOpticalLocal(std::numeric_limits<double>::infinity(), 0, {}, sentinel) &&
               near(sentinel, {17, 23}, 0), "invalid local conversion preserves caller output");
    expect(!toOpticalLocal(std::numeric_limits<double>::max(), 0, {}, sentinel), "local float overflow rejected");
    expect(std::isnan(requiredProfilePaddingPx({}, -1)), "negative filter support rejected");
}
}

int main() {
    testSupportAndJoins(); testIndependentIntegralAndDifferentials(); testWarpAndJacobian();
    testMonotoneProfile(); testRegularizedNeck(); testCircularProfile(); testDpiAndLocalPrecision(); testAnalyticFields(); testValidation();
    std::printf("Authored profile checks: %d, failures: %d\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
