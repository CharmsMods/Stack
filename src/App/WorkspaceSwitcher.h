#pragma once

#include <array>
#include <algorithm>
#include <cmath>

namespace Stack::Workspace {
// Values remain compatible with AppShell's existing programmatic navigation.
enum class Id : int { Library = 0, Graph = 1, Raw = 5, Bracketing = 6, Queue = 7 };
struct Destination { Id id; const char* label; float x, y; };
inline constexpr std::array<Destination, 4> Destinations{{
    {Id::Library, "LIBRARY", 0, -1}, {Id::Graph, "GRAPH", 1, 0},
    {Id::Queue, "QUEUE", 0, 1}, {Id::Raw, "RAW", -1, 0}
}};
struct VisualConfig {
    float ringRadius = 111, travelRadius = 100, deadRadius = 42;
    float labelRadius = 176, beadRadius = 10.5f;
    float transitionSeconds = .26f, recessedScale = .855f, curvature = .064f;
    float pointerSensitivity = .25f, previewFadeSeconds = .18f;
    float sectorHysteresis = .065f;
};
struct HoldInput {
    bool leftAlt=false, rightAlt=false, leftControl=false;
    bool tab=false, f4=false, escape=false, focused=true;
    bool Alt() const { return leftAlt || rightAlt; }
    bool AltGr() const { return rightAlt && leftControl; }
    bool SystemChord() const { return Alt() && (tab || f4); }
};
enum class HoldAction { None, Open, Commit, Cancel, CancelWithoutFocus };
inline HoldAction ResolveHoldAction(const HoldInput& input, bool held, bool inhibitOpen) {
    if(held) {
        if(!input.focused || input.SystemChord() || input.AltGr()) return HoldAction::CancelWithoutFocus;
        if(input.escape) return HoldAction::Cancel;
        if(!input.Alt()) return HoldAction::Commit;
    } else if(input.focused && input.Alt() && !input.AltGr() && !input.SystemChord() && !inhibitOpen) {
        return HoldAction::Open;
    }
    return HoldAction::None;
}
class Switcher {
public:
    VisualConfig config;
    void Begin(int active) { original = preview = active; selected = -1; x = y = 0; held = true; }
    void Move(float dx, float dy) {
        x += dx * config.pointerSensitivity / scale; y += dy * config.pointerSensitivity / scale;
        const float r = std::hypot(x, y);
        if (r > config.travelRadius) { x *= config.travelRadius / r; y *= config.travelRadius / r; }
        if (std::hypot(x, y) < config.deadRadius) { selected = -1; preview = original; return; }
        const float angle = std::atan2(y, x);
        auto distance = [&](int i) {
            float a = angle - std::atan2(Destinations[i].y, Destinations[i].x);
            return std::abs(std::atan2(std::sin(a), std::cos(a)));
        };
        int next = 0;
        for (int i = 1; i < static_cast<int>(Destinations.size()); ++i) if (distance(i) < distance(next)) next = i;
        if (selected >= 0 && distance(selected) <= distance(next) + config.sectorHysteresis) next = selected;
        selected = next; preview = static_cast<int>(Destinations[next].id);
    }
    int End(bool commit) { held = false; if (!commit) preview = original; selected = -1; return preview; }
    void Tick(float seconds) {
        const float step = std::max(0.f, seconds) / config.transitionSeconds;
        progress = std::clamp(progress + (held ? step : -step), 0.f, 1.f);
    }
    float Amount() const { return progress * progress * (3.f - 2.f * progress); }
    bool Visible() const { return held || progress > 0.f; }
    int original = 0, preview = 0, selected = -1;
    float x = 0, y = 0, scale = 1, progress = 0;
    bool held = false;
};
}
