#pragma once

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace Stack::ViewportNavigation {

enum class PanConstraint { CoverViewport, KeepImageReachable, BoundedSurround, Unrestricted };

// References let existing per-panel state remain independent, including RAW's
// persisted navigation fields. No renderer or ImGui context is required.
struct State {
    float& zoom;
    float& target;
    float& panX;
    float& panY;
    bool& animating;
    bool& panning;
    ImVec2& focusScreen;
    ImVec2& focusUv;
};

struct Input {
    ImVec2 mouse;
    ImVec2 delta;
    float wheel = 0.0f;
    float seconds = 0.0f;
    bool hovered = false;
    bool wheelAllowed = true;
    bool middleClicked = false;
    bool middleDown = false;
    bool reset = false;
    float minimumZoom = 1.0f;
    float maximumZoom = 12.0f;
    PanConstraint panConstraint = PanConstraint::CoverViewport;
    // Optional full-window limits for deliberate dragging, never zoom anchoring.
    ImVec2 coverageMinimum{}, coverageMaximum{};
};

inline void ClampPan(State state, ImVec2 size, ImVec2 origin,
                     ImVec2 minimum, ImVec2 maximum,
                     PanConstraint constraint = PanConstraint::CoverViewport) {
    // Surround navigation limits deliberate dragging below. Cursor-anchored
    // zoom must not snap back to a coverage boundary on each animation frame.
    if (constraint == PanConstraint::Unrestricted || constraint == PanConstraint::BoundedSurround) return;
    if (constraint == PanConstraint::KeepImageReachable) {
        // Allow edges through the center and small photos anywhere in the view.
        // Keep a 64-unit strip, or half a tiny photo, reachable at the boundary.
        const float marginX = std::min({64.f, size.x * .5f, (maximum.x - minimum.x) * .5f});
        const float marginY = std::min({64.f, size.y * .5f, (maximum.y - minimum.y) * .5f});
        state.panX = std::clamp(state.panX, minimum.x + marginX - size.x - origin.x,
            maximum.x - marginX - origin.x);
        state.panY = std::clamp(state.panY, minimum.y + marginY - size.y - origin.y,
            maximum.y - marginY - origin.y);
        return;
    }
    state.panX = size.x <= maximum.x-minimum.x ? 0.f :
        std::clamp(state.panX, maximum.x-size.x-origin.x, minimum.x-origin.x);
    state.panY = size.y <= maximum.y-minimum.y ? 0.f :
        std::clamp(state.panY, maximum.y-size.y-origin.y, minimum.y-origin.y);
}

template<class Origin>
void Update(State state, const Input& input, ImVec2 baseSize,
            ImVec2 minimum, ImVec2 maximum, Origin originFor) {
    auto sizeFor = [&] { return ImVec2(baseSize.x * state.zoom, baseSize.y * state.zoom); };
    auto size = sizeFor();
    auto origin = originFor(size);
    ClampPan(state, size, origin, minimum, maximum, input.panConstraint);
    if (input.hovered && input.reset) {
        state.zoom = state.target = 1.0f;
        state.panX = state.panY = 0.0f;
        state.animating = state.panning = false;
        return;
    }
    if (input.hovered && input.wheelAllowed && !input.middleDown &&
        std::abs(input.wheel) > 0.0001f) {
        const float base = state.animating ? state.target : state.zoom;
        const float target = std::clamp(base * std::pow(1.18f, input.wheel),
            input.minimumZoom, input.maximumZoom);
        if (std::abs(target - base) > 0.0001f) {
            state.target = target;
            state.focusScreen = input.mouse;
            // RAW's generated surround is part of the navigable plane. Keep
            // an outside cursor's actual coordinate instead of jumping to an
            // image edge before the first animated zoom step.
            const bool outsideFocus = input.panConstraint == PanConstraint::Unrestricted ||
                input.panConstraint == PanConstraint::BoundedSurround;
            const float minimumSize = outsideFocus ? .0001f : 1.f;
            state.focusUv = ImVec2(
                (input.mouse.x - origin.x - state.panX) / std::max(minimumSize, size.x),
                (input.mouse.y - origin.y - state.panY) / std::max(minimumSize, size.y));
            if (!outsideFocus) {
                state.focusUv.x = std::clamp(state.focusUv.x, 0.f, 1.f);
                state.focusUv.y = std::clamp(state.focusUv.y, 0.f, 1.f);
            }
            state.animating = true;
        }
    }
    if (input.hovered && input.middleClicked) {
        state.target = state.zoom;
        state.animating = false;
        state.panning = true;
    }
    if (state.animating) {
        state.zoom += (state.target - state.zoom) *
            (1.0f - std::exp(-18.0f * std::max(0.0f, input.seconds)));
        if (std::abs(state.target - state.zoom) < 0.0005f) {
            state.zoom = state.target;
            state.animating = false;
        }
        size = sizeFor();
        origin = originFor(size);
        state.panX = state.focusScreen.x - state.focusUv.x * size.x - origin.x;
        state.panY = state.focusScreen.y - state.focusUv.y * size.y - origin.y;
    }
    if (!input.middleDown) state.panning = false;
    else if (state.panning) {
        const auto dragAxis = [](float pan, float delta, float length, float origin,
                                 float low, float high) {
            if (high <= low || length < high-low) return pan+delta;
            const float allowedLow = high-length-origin;
            const float allowedHigh = low-origin;
            // A cursor-anchored zoom may leave this interval. Do not snap back:
            // allow motion toward coverage but never increase exposed space.
            return std::clamp(pan+delta, std::min(pan, allowedLow), std::max(pan, allowedHigh));
        };
        if (input.panConstraint == PanConstraint::BoundedSurround) {
            const auto boundedDrag = [](float pan, float delta, float length, float origin,
                                         float low, float high) {
                const float padding = std::max(0.f, high-low);
                const float allowedLow = low-padding-length-origin;
                const float allowedHigh = high+padding-origin;
                // Keep at most one viewport of empty space beyond the photo.
                // Zoom can start outside this interval; allow gradual motion
                // back, but no further outward dragging and no position snap.
                return std::clamp(pan+delta, std::min(pan, allowedLow), std::max(pan, allowedHigh));
            };
            state.panX = boundedDrag(state.panX, input.delta.x, size.x, origin.x, minimum.x, maximum.x);
            state.panY = boundedDrag(state.panY, input.delta.y, size.y, origin.y, minimum.y, maximum.y);
        } else if (input.panConstraint != PanConstraint::CoverViewport) {
            state.panX += input.delta.x;
            state.panY += input.delta.y;
        } else {
            state.panX = dragAxis(state.panX, input.delta.x, size.x, origin.x,
                input.coverageMinimum.x, input.coverageMaximum.x);
            state.panY = dragAxis(state.panY, input.delta.y, size.y, origin.y,
                input.coverageMinimum.y, input.coverageMaximum.y);
        }
    }
    ClampPan(state, size, origin, minimum, maximum, input.panConstraint);
}

} // namespace Stack::ViewportNavigation
