#pragma once
#include "imgui.h"

namespace Stack::StartupReveal {
enum class Phase { Disabled, Throw, Catch, PaintPier, PaintOcean, Hold, RevealInterface, Complete };
struct Visual {
    Phase phase = Phase::Disabled;
    float sceneSeconds = 0;
    float backdropAmount = 0;
    float interfaceOpacity = 1;
    float captionButtonOpacity = 1;
    bool pegVisible = false;
    bool IsActive() const;
    bool HasTransparentBackdrop() const { return false; }
    bool ShouldRenderInterface() const;
    bool AllowsInput() const;
};
class Controller {
public:
    void Enable();
    void Disable();
    void Start(double nowSeconds);
    void RequestSkip() { m_Skip = true; }
    Visual Update(double nowSeconds, const ImVec2& viewportSize,
        bool ready = true, bool reducedMotion = false, bool inputReleased = true);
    bool IsEnabled() const { return m_Enabled; }
    bool IsActive() const { return m_Visual.IsActive(); }
private:
    bool m_Enabled = false, m_Skip = false;
    double m_StartedAt = -1, m_FadeAt = -1;
    float m_FadeDuration = .4f;
    Visual m_Visual;
};
} // namespace Stack::StartupReveal
