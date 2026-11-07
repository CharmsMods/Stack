#pragma once
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <cstdint>

namespace Stack::Tools {
struct Switcher {
    std::string document, owner;
    std::uint64_t revision=0, sourceRevision=0;
    ImVec2 openedWorkSize{};
    int count = 0;
    bool held = false, mouseOpen = false, wasDown = false, suppressed = false;
    bool discardMouse = false, cursorCaptured = false;
    float progress = 0.f;
    int hovered = -1;
    ImVec2 controlsMin{}, controlsMax{}, pointer{}, restoreCursor{};
    ImVec2 listMin{}, listMax{};
    float rowHeight = 32.f, listPosition = 0.f;
    int firstRow = 0, visibleRows = 1;
    double lastPointerX = 0, lastPointerY = 0;
    bool Open() const { return held || mouseOpen; }
    bool Visible() const { return Open() || progress > 0.f; }
    float Amount() const { return progress * progress * (3.f - 2.f * progress); }
    bool HasChoices() const { return count > 1; }
    void Begin(bool keyboard) { if (!HasChoices()) return; held = keyboard; mouseOpen = !keyboard; pointer = {}; hovered = -1; }
    void Close() { held = mouseOpen = false; discardMouse = true; }
    void Tick(float seconds, bool reducedMotion = false) {
        progress = reducedMotion ? (Open() ? 1.f : 0.f)
            : std::clamp(progress + (Open() ? 1.f : -1.f) * std::clamp(seconds, 0.f, .1f) / .22f, 0.f, 1.f);
    }
};
}
