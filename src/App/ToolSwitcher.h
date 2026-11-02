#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace Stack::Tools {
struct Layout { ImVec2 center; float radius = 120.f, inner = 38.f; };
struct Switcher {
    int count = 0;
    bool held = false, mouseOpen = false, wasDown = false, suppressed = false;
    bool discardMouse = false, cursorCaptured = false;
    float progress = 0.f, fadeRemaining = 0.f;
    int hovered = -1;
    ImVec2 controlsMin{}, controlsMax{}, pointer{}, restoreCursor{};
    ImVec2 graphCenter{};
    float graphZoom = .65f;
    double lastPointerX = 0, lastPointerY = 0;
    bool Open() const { return held || mouseOpen; }
    bool Visible() const { return Open() || progress > 0.f; }
    float Amount() const { return progress * progress * (3.f - 2.f * progress); }
    bool HasChoices() const { return count > 1; }
    void Begin(bool keyboard) { if (!HasChoices()) return; held = keyboard; mouseOpen = !keyboard; pointer = {}; hovered = -1; }
    void Close() { held = mouseOpen = false; hovered = -1; discardMouse = true; }
    void Tick(float seconds, bool reducedMotion = false) {
        progress = reducedMotion ? (Open() ? 1.f : 0.f)
            : std::clamp(progress + (Open() ? 1.f : -1.f) * std::clamp(seconds, 0.f, .1f) / .18f, 0.f, 1.f);
    }
    Layout Geometry(float scale) const {
        const auto* viewport=ImGui::GetMainViewport();
        const float radius = std::max(1.f,std::min({viewport->WorkSize.x*.42f,viewport->WorkSize.y*.42f,240.f*scale}));
        return {ImVec2(std::clamp((controlsMin.x+controlsMax.x)*.5f,viewport->WorkPos.x+radius,
                viewport->WorkPos.x+viewport->WorkSize.x-radius),
            std::clamp((controlsMin.y+controlsMax.y)*.5f,viewport->WorkPos.y+radius*.7f,
                std::max(viewport->WorkPos.y+radius*.7f,viewport->WorkPos.y+viewport->WorkSize.y-radius*.7f-28.f))),radius,radius*.31f};
    }
    int Hit(ImVec2 delta, const Layout& layout) const {
        if (!HasChoices()) return -1;
        constexpr float pi = 3.14159265359f;
        if (std::hypot(delta.x, delta.y) < layout.inner) return -1;
        float angle = std::atan2(delta.y, delta.x) + pi * .5f + pi / count;
        if (angle < 0) angle += 2.f * pi;
        return static_cast<int>(angle / (2.f * pi / count)) % count;
    }
};
}
