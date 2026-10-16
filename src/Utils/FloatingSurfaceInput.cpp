#include "Utils/FloatingSurfaceInput.h"
#include <imgui_internal.h>

namespace ImGuiExtras {
FloatingSurfaceInputScope::FloatingSurfaceInputScope(FloatingSurfaceInput& state, bool canStart,
    ImGuiID backgroundCapture) {
    auto& io = ImGui::GetIO();
    mouse = io.MousePos;
    delta = io.MouseDelta;
    for (int i = 0; i < 2; ++i)
        saved[i] = {io.MouseDown[i], io.MouseClicked[i], io.MouseReleased[i],
            io.MouseDoubleClicked[i], io.MouseDownDuration[i], io.MouseClickedCount[i]};
    const auto suppress = [&] {
        for (int i = 0; i < 2; ++i) {
            io.MouseDown[i] = io.MouseClicked[i] = io.MouseReleased[i] = io.MouseDoubleClicked[i] = false;
            io.MouseDownDuration[i] = -1.f;
            io.MouseClickedCount[i] = 0;
        }
    };
    const auto click = [&] {
        // ImGui can capture the physical press for this window's background
        // while the tool is waiting to distinguish editing from a move chord.
        // NoMove still creates that capture. Hand it back before replaying
        // the press, without taking ownership from another control/window.
        const auto capture = backgroundCapture ? backgroundCapture : ImGui::GetCurrentWindow()->MoveId;
        if (ImGui::GetActiveID() == capture)
            ImGui::ClearActiveID();
        suppress();
        io.MousePos = state.press;
        io.MouseDelta = ImVec2(0,0);
        io.MouseDown[state.button] = io.MouseClicked[state.button] = true;
        io.MouseDoubleClicked[state.button] = state.doubleClick;
        io.MouseClickedCount[state.button] = state.doubleClick ? 2 : 1;
        io.MouseDownDuration[state.button] = 0.f;
    };
    const bool anyDown = saved[0].down || saved[1].down;
    const bool bothDown = saved[0].down && saved[1].down;
    state.moving = false;
    using Phase = FloatingSurfaceInput::Phase;
    if (io.AppFocusLost || (state.OwnsPointer() && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
        state.phase = anyDown ? Phase::Drain : Phase::Idle;
        suppress();
        return;
    }
    if (state.phase == Phase::Idle && canStart && (saved[0].clicked || saved[1].clicked)) {
        state.button = saved[0].clicked ? 0 : 1;
        state.press = io.MousePos;
        state.doubleClick = saved[state.button].twice;
        state.phase = Phase::Pending;
    }
    switch (state.phase) {
    case Phase::Pending: {
        suppress();
        if (bothDown) {
            state.phase = Phase::Moving;
            state.moving = true;
        } else if (!saved[state.button].down) {
            click();
            state.phase = Phase::ReleaseClick;
        } else {
            const ImVec2 distance(io.MousePos.x - state.press.x, io.MousePos.y - state.press.y);
            if (distance.x * distance.x + distance.y * distance.y >= io.MouseDragThreshold * io.MouseDragThreshold) {
                click();
                state.phase = Phase::Editing;
            }
        }
        break;
    }
    case Phase::Editing:
        // A started parameter drag keeps ownership. Adding the other button
        // does not turn that edit into movement or invoke a second action.
        io.MouseDown[1-state.button] = io.MouseClicked[1-state.button] =
            io.MouseReleased[1-state.button] = io.MouseDoubleClicked[1-state.button] = false;
        io.MouseClickedCount[1-state.button] = 0;
        if (!saved[state.button].down) state.phase = anyDown ? Phase::Drain : Phase::Idle;
        break;
    case Phase::Moving:
        suppress();
        state.moving = bothDown;
        if (!bothDown) state.phase = anyDown ? Phase::Drain : Phase::Idle;
        break;
    case Phase::ReleaseClick:
        suppress();
        io.MouseReleased[state.button] = true;
        if (saved[0].clicked || saved[1].clicked) {
            // A fast second click may arrive while the first synthetic
            // release is being delivered. Keep it for the next frame.
            state.button = saved[0].clicked ? 0 : 1;
            state.press = mouse;
            state.doubleClick = saved[state.button].twice;
            state.phase = Phase::Pending;
        } else state.phase = anyDown ? Phase::Drain : Phase::Idle;
        break;
    case Phase::Drain:
        suppress();
        if (!anyDown) state.phase = Phase::Idle;
        break;
    case Phase::Idle: break;
    }
}

FloatingSurfaceInputScope::~FloatingSurfaceInputScope() {
    auto& io = ImGui::GetIO();
    io.MousePos = mouse;
    io.MouseDelta = delta;
    for (int i = 0; i < 2; ++i) {
        io.MouseDown[i] = saved[i].down;
        io.MouseClicked[i] = saved[i].clicked;
        io.MouseReleased[i] = saved[i].released;
        io.MouseDoubleClicked[i] = saved[i].twice;
        io.MouseDownDuration[i] = saved[i].duration;
        io.MouseClickedCount[i] = saved[i].count;
    }
}
} // namespace ImGuiExtras
