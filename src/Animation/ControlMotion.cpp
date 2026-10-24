// SPDX-License-Identifier: GPL-3.0-only
#include "ControlMotion.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace Stack::Animation {
void ControlMotion::Reset() {
    m_Hover.Reset(0.f); m_Press.Reset(0.f); m_Selection.Reset(0.f); m_Focus.Reset(0.f);
    m_Snapshot = {}; m_Seen = false; m_Identity = m_Frame = 0;
}
ControlSnapshot ControlMotion::Update(const ControlInput& input, const ControlStyle& style) {
    if (!input.visible) { Reset(); return m_Snapshot; }
    if (m_Seen && m_Identity == input.identity && m_Frame == input.frame) return m_Snapshot;
    const bool continuous = m_Seen && m_Identity == input.identity &&
        m_Frame != std::numeric_limits<std::uint64_t>::max() && m_Frame + 1 == input.frame;
    if (!continuous) {
        Reset();
        m_Selection.Reset(input.selected ? 1.f : 0.f);
    }
    m_Seen = true; m_Identity = input.identity; m_Frame = input.frame;
    const double maximum = std::isfinite(style.maximumDelta) ? std::clamp(style.maximumDelta,0.0,.25) : .05;
    const double dt = std::isfinite(input.deltaSeconds) ? std::clamp(input.deltaSeconds,0.0,maximum) : 0.0;
    m_Hover.Retarget(input.enabled && input.hovered ? 1.f : 0.f,
        MotionSpec::Exponential(style.hoverRate,style.settleEpsilon));
    if (input.enabled && input.held) m_Press.Snap(1.f);
    else m_Press.Retarget(0.f,MotionSpec::Exponential(style.releaseRate,style.settleEpsilon));
    m_Selection.Retarget(input.selected ? 1.f : 0.f,
        MotionSpec::Exponential(style.selectionRate,style.settleEpsilon));
    m_Focus.Retarget(input.enabled && input.focused ? 1.f : 0.f,
        MotionSpec::Exponential(style.focusRate,style.settleEpsilon));
    m_Hover.Advance(dt,input.reducedMotion); m_Press.Advance(dt,input.reducedMotion);
    m_Selection.Advance(dt,input.reducedMotion); m_Focus.Advance(dt,input.reducedMotion);
    m_Snapshot = {m_Hover.Value(),m_Press.Value(),m_Selection.Value(),m_Focus.Value()};
    return m_Snapshot;
}
} // namespace Stack::Animation
