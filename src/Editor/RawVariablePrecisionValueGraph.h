#pragma once

#include <algorithm>
#include <cmath>

namespace Stack::Editor::VariablePrecisionValueGraph {

struct Configuration { float minimum = -16.0f; float maximum = 16.0f; float minimumSpan = 0.20f; };
struct State { float value = 0.0f; float depth = 0.0f; float position = 0.5f; float anchor = 0.5f; };

inline float Range(const Configuration& c) { return std::max(0.000001f, c.maximum - c.minimum); }
inline float SmoothDepth(float depth) {
    const float t = std::clamp(depth, 0.0f, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}
inline float Span(const Configuration& c, float depth) {
    const float range = Range(c);
    const float minimumSpan = std::clamp(c.minimumSpan, 0.000001f, range);
    return range * std::pow(minimumSpan / range, SmoothDepth(depth));
}
inline float LocalMinimum(const Configuration& c, float depth, float anchor) {
    return c.minimum + std::clamp(anchor, 0.0f, 1.0f) * (Range(c) - Span(c, depth));
}
inline float ValueAt(const Configuration& c, float depth, float anchor, float position) {
    return LocalMinimum(c, depth, anchor) + std::clamp(position, 0.0f, 1.0f) * Span(c, depth);
}
inline float PositionOf(const Configuration& c, float value, float depth, float anchor) {
    return std::clamp((value - LocalMinimum(c, depth, anchor)) / Span(c, depth), 0.0f, 1.0f);
}
inline State FromValue(const Configuration& c, float value, float depth = 0.0f) {
    State state;
    state.value = std::clamp(value, c.minimum, c.maximum);
    state.depth = std::clamp(depth, 0.0f, 1.0f);
    state.anchor = std::clamp((state.value - c.minimum) / Range(c), 0.0f, 1.0f);
    state.position = PositionOf(c, state.value, state.depth, state.anchor);
    return state;
}
inline void ApplyVertical(const Configuration& c, State& state, float normalizedDelta) {
    const float oldDepth = state.depth;
    const float newDepth = std::clamp(oldDepth + normalizedDelta, 0.0f, 1.0f);
    if (newDepth == oldDepth) return;
    const float newSpan = Span(c, newDepth);
    if (newDepth > oldDepth) {
        const float denominator = Range(c) - newSpan;
        if (std::abs(denominator) > 0.000001f) {
            state.anchor = std::clamp(
                (state.value - c.minimum - state.position * newSpan) / denominator,
                0.0f, 1.0f);
        }
    } else {
        state.position = PositionOf(c, state.value, newDepth, state.anchor);
    }
    state.depth = newDepth;
}
inline void ApplyHorizontal(const Configuration& c, State& state, float normalizedDelta) {
    state.position = std::clamp(state.position + normalizedDelta, 0.0f, 1.0f);
    state.value = ValueAt(c, state.depth, state.anchor, state.position);
}
inline void ApplyDelta(const Configuration& c, State& state, float dx, float dy) {
    ApplyVertical(c, state, dy * 0.5f);
    ApplyHorizontal(c, state, dx);
    ApplyVertical(c, state, dy * 0.5f);
}

} // namespace Stack::Editor::VariablePrecisionValueGraph
