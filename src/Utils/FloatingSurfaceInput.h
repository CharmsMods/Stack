#pragma once

#include <imgui.h>

namespace ImGuiExtras {

// Per-surface input ownership. Delay a single press until it becomes a click
// or drag, so either button may be pressed first when moving a floating tool.
struct FloatingSurfaceInput {
    enum class Phase { Idle, Pending, Editing, Moving, ReleaseClick, Drain };
    Phase phase = Phase::Idle;
    int button = 0;
    ImVec2 press{};
    bool doubleClick = false;
    bool moving = false;
    bool OwnsPointer() const { return phase != Phase::Idle; }
};

// Routes input only while drawing the tool. Restores the physical button state
// before other windows (including the image viewport) are processed.
class FloatingSurfaceInputScope {
public:
    // A host may route before Begin() so its placement uses current input.
    // In that case supply the host's background capture ID explicitly.
    FloatingSurfaceInputScope(FloatingSurfaceInput& state, bool canStart,
        ImGuiID backgroundCapture = 0);
    ~FloatingSurfaceInputScope();
    FloatingSurfaceInputScope(const FloatingSurfaceInputScope&) = delete;
    FloatingSurfaceInputScope& operator=(const FloatingSurfaceInputScope&) = delete;
private:
    struct Button { bool down, clicked, released, twice; float duration; unsigned short count; } saved[2];
    ImVec2 mouse, delta;
};

} // namespace ImGuiExtras
