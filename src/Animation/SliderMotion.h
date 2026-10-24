// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ControlMotion.h"
#include <algorithm>
#include <cmath>

namespace Stack::Animation {
struct SliderSnapshot {
    ControlSnapshot control;
    // Always the authoritative value mapped to the supplied endpoints. This is
    // never an animated replacement for the widget's value or a commit request.
    float position = 0.f;
    bool validRange = false;
};
class SliderMotion {
public:
    SliderSnapshot Update(const ControlInput& input, double value, double first, double last,
        const ControlStyle& style = {}) {
        SliderSnapshot next;
        next.control = m_Control.Update(input,style);
        const double span = last-first;
        next.validRange = std::isfinite(value) && std::isfinite(first) &&
            std::isfinite(last) && std::isfinite(span) && span != 0.;
        if (next.validRange) {
            // Normalize after clamping to avoid overflow far outside the range.
            const double bounded=std::clamp(value,std::min(first,last),std::max(first,last));
            next.position=static_cast<float>(std::clamp((bounded-first)/span,0.,1.));
        }
        m_Snapshot=next;
        return next;
    }
    const SliderSnapshot& Snapshot() const { return m_Snapshot; }
    void Reset() { m_Control.Reset(); m_Snapshot={}; }
private:
    ControlMotion m_Control;
    SliderSnapshot m_Snapshot;
};
} // namespace Stack::Animation
