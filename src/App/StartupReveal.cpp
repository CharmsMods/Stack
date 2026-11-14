#include "StartupReveal.h"
#include <algorithm>

namespace Stack::StartupReveal {
bool Visual::IsActive() const { return phase != Phase::Disabled && phase != Phase::Complete; }
bool Visual::ShouldRenderInterface() const {
    return true; // Prepare the real interface underneath the scene before the handoff.
}
bool Visual::AllowsInput() const { return !IsActive(); }
void Controller::Enable() {
    m_Enabled = true; m_Skip = false; m_StartedAt = m_FadeAt = -1;
    m_Visual = {}; m_Visual.phase = Phase::Throw;
    m_Visual.interfaceOpacity = m_Visual.captionButtonOpacity = 0;
}
void Controller::Disable() { m_Enabled = false; m_Visual = {}; }
void Controller::Start(double now) { if (m_Enabled) m_StartedAt = now; }
Visual Controller::Update(double now, const ImVec2&, bool ready, bool reduced, bool released) {
    if (!m_Enabled || m_Visual.phase == Phase::Complete) return m_Visual;
    const double t = m_StartedAt < 0 ? 0.0 : std::max(0.0, now-m_StartedAt);
    m_Visual.sceneSeconds = reduced ? 2.9f : static_cast<float>(std::max(0.0,t-.4));
    const float intro=reduced?1.f:std::clamp(static_cast<float>(t/.4),0.f,1.f);
    m_Visual.backdropAmount=intro*intro*(3-2*intro);
    m_Visual.pegVisible = m_Visual.sceneSeconds >= .85f;
    if (m_FadeAt < 0 && ready && m_StartedAt >= 0 && (m_Visual.sceneSeconds >= 2.9f || m_Skip || reduced)) {
        m_FadeAt = now; m_FadeDuration = m_Skip || reduced ? .18f : .9f;
    }
    if (m_FadeAt >= 0) {
        const float a = std::clamp(static_cast<float>((now-m_FadeAt)/m_FadeDuration),0.f,1.f);
        m_Visual.phase = a >= 1 && released ? Phase::Complete : Phase::RevealInterface;
        m_Visual.interfaceOpacity = a*a*(3-2*a);
        m_Visual.captionButtonOpacity = m_Visual.interfaceOpacity;
    } else {
        const float scene=m_Visual.sceneSeconds;
        m_Visual.phase = scene < .85f ? Phase::Throw : scene < 1.3f ? Phase::Catch :
            scene < 2.f ? Phase::PaintPier : scene < 2.9f ? Phase::PaintOcean : Phase::Hold;
        m_Visual.interfaceOpacity = m_Visual.captionButtonOpacity = 0;
    }
    return m_Visual;
}
} // namespace Stack::StartupReveal
