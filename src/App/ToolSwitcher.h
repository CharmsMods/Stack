#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace Stack::Tools {
struct Layout { ImVec2 center; float radius = 120.f, inner = 38.f; };
struct Switcher {
    static constexpr int Count = 6;
    bool held = false, mouseOpen = false, wasDown = false, suppressed = false;
    bool discardMouse = false, cursorCaptured = false;
    float progress = 0.f, fadeRemaining = 0.f;
    int hovered = -1;
    ImVec2 controlsMin{}, controlsMax{}, pointer{}, restoreCursor{};
    double lastPointerX = 0, lastPointerY = 0;
    bool Open() const { return held || mouseOpen; }
    bool Visible() const { return Open() || progress > 0.f; }
    float Amount() const { return progress * progress * (3.f - 2.f * progress); }
    void Begin(bool keyboard) { held = keyboard; mouseOpen = !keyboard; pointer = {}; hovered = -1; }
    void Close() { held = mouseOpen = false; hovered = -1; discardMouse = true; }
    void Tick(float seconds, bool reducedMotion = false) {
        progress = reducedMotion ? (Open() ? 1.f : 0.f)
            : std::clamp(progress + (Open() ? 1.f : -1.f) * std::clamp(seconds, 0.f, .1f) / .18f, 0.f, 1.f);
    }
    Layout Geometry(float scale) const {
        const float width = std::max(1.f, controlsMax.x - controlsMin.x);
        const float height = std::max(1.f, controlsMax.y - controlsMin.y);
        const float radius = std::max(30.f, std::min({width * .47f, height * .43f, 165.f * scale}));
        return {ImVec2((controlsMin.x + controlsMax.x) * .5f,
            (controlsMin.y + controlsMax.y) * .5f), radius, radius * .31f};
    }
    int Hit(ImVec2 delta, const Layout& layout) const {
        constexpr float pi = 3.14159265359f;
        if (std::hypot(delta.x, delta.y) < layout.inner) return -1;
        float angle = std::atan2(delta.y, delta.x) + pi * .5f + pi / Count;
        if (angle < 0) angle += 2.f * pi;
        return static_cast<int>(angle / (2.f * pi / Count)) % Count;
    }
};
}
