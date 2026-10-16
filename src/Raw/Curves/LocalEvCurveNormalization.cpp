#include "Raw/Curves/LocalEvCurveNormalization.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Stack::RawRecipe {

float LocalEvCurveOffset(const std::vector<RawBezierCurvePoint>& points, float normalization) {
    if (points.size() < 2 || !std::isfinite(normalization) || normalization <= 0.0f)
        return 0.0f;
    // Three-point Gaussian quadrature integrates cubic y(t) times quadratic
    // x'(t) exactly, including manual handles and unevenly spaced anchors.
    constexpr std::array<double, 3> times{.1127016653792583, .5, .8872983346207417};
    constexpr std::array<double, 3> weights{5.0/18.0, 4.0/9.0, 5.0/18.0};
    double integral = 0.0;
    for (const auto& segment : BuildRawBezierSegments(points)) {
        for (std::size_t i = 0; i < times.size(); ++i) {
            const double t = times[i], u = 1.0 - t;
            const double y = u*u*u*segment.left.y + 3*u*u*t*segment.leftHandleY
                + 3*u*t*t*segment.rightHandleY + t*t*t*segment.right.y;
            const double dx = 3*(u*u*(segment.leftHandleX-segment.left.x)
                + 2*u*t*(segment.rightHandleX-segment.leftHandleX)
                + t*t*(segment.right.x-segment.rightHandleX));
            integral += weights[i] * y * dx;
        }
    }
    // The supported domain is 0..1; extend the endpoint values across tails.
    integral += points.front().x * points.front().y
        + (1.0 - points.back().x) * points.back().y;
    return float((integral - .5) * std::clamp(normalization, 0.0f, 1.0f));
}

float LocalEvGlobalOffsetEv(const RawLocalRangeRecipe& recipe) {
    if (!std::isfinite(recipe.curveNormalization) || recipe.curveNormalization <= 0.0f) return 0.0f;
    return 8.0f * LocalEvCurveOffset(RawLocalRangeBezierPoints(recipe), recipe.curveNormalization);
}

} // namespace Stack::RawRecipe
