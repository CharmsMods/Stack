#pragma once

#include "Raw/RawDevelopmentRecipe.h"

#include <algorithm>
#include <vector>

namespace Stack::RawRecipe {

// A sampled curve keeps the authored boundary/segment selection rules while
// preparing handle sanitization and monotone tangents once for the whole table.
// Evaluation uses the same segment arithmetic as EvaluateRawBezierCurve.
class PreparedRawBezierCurve {
public:
    PreparedRawBezierCurve() = default;
    explicit PreparedRawBezierCurve(const std::vector<RawBezierCurvePoint>& points)
        : points_(points), segments_(BuildRawBezierSegments(points)) {}

    bool Empty() const { return points_.empty(); }

    float Evaluate(float x) const {
        if (points_.empty()) return std::clamp(x, 0.0f, 1.0f);
        if (points_.size() == 1u) return std::clamp(points_.front().y, 0.0f, 1.0f);
        x = std::clamp(x, 0.0f, 1.0f);
        if (x <= points_.front().x) return std::clamp(points_.front().y, 0.0f, 1.0f);
        if (x >= points_.back().x) return std::clamp(points_.back().y, 0.0f, 1.0f);
        std::size_t index = 0;
        while (index + 1u < points_.size() && x > points_[index + 1u].x) ++index;
        return EvaluateRawBezierSegment(segments_[std::min(index, segments_.size() - 1u)], x);
    }

private:
    std::vector<RawBezierCurvePoint> points_;
    std::vector<RawBezierSegment> segments_;
};

} // namespace Stack::RawRecipe
