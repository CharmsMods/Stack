// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ScalarChannel.h"
#include <cstdint>

namespace Stack::Animation {
// Transient presentation only. Input interpretation and actions stay with the
// widget. Own one instance per control, not in a process-global identity map.
struct ControlInput {
    std::uint64_t identity = 0;
    std::uint64_t frame = 0;
    double deltaSeconds = 0.0;
    bool hovered = false, held = false, selected = false, focused = false;
    bool enabled = true, visible = true, reducedMotion = false;
};
struct ControlStyle {
    float hoverRate = 26.f, releaseRate = 30.f, selectionRate = 22.f, focusRate = 26.f;
    float settleEpsilon = .01f;
    double maximumDelta = .05;
};
struct ControlSnapshot {
    float hover = 0.f, press = 0.f, selection = 0.f, focus = 0.f;
    float Elevation() const { return 1.f - .8f * press; }
};
class ControlMotion {
public:
    // Repeated evaluation in one frame returns the same immutable snapshot.
    // Gaps/identity replacement discard stale pressure, not authored state.
    ControlSnapshot Update(const ControlInput& input, const ControlStyle& style = {});
    const ControlSnapshot& Snapshot() const { return m_Snapshot; }
    void Reset();
private:
    ScalarChannel m_Hover, m_Press, m_Selection, m_Focus;
    ControlSnapshot m_Snapshot;
    std::uint64_t m_Identity = 0, m_Frame = 0;
    bool m_Seen = false;
};
} // namespace Stack::Animation
