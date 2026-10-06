#pragma once

#include "imgui.h"

namespace Stack::StartupReveal {

enum class Phase {
    Disabled,
    FormDisk,
    ExpandDisk,
    HoldSurface,
    RevealInterface,
    RevealCaptionButtons,
    Complete,
};

struct Visual {
    Phase phase = Phase::Disabled;
    float diskOpacity = 0.0f;
    float diskRadius = 0.0f;
    float diskFeather = 0.0f;
    float interfaceOpacity = 1.0f;
    float captionButtonOpacity = 1.0f;

    bool IsActive() const;
    bool HasTransparentBackdrop() const;
    bool ShouldRenderInterface() const;
    bool AllowsInput() const;
};

class Controller {
public:
    void Enable();
    void Disable();
    void Start(double nowSeconds);
    Visual Update(double nowSeconds, const ImVec2& viewportSize);

    bool IsEnabled() const;
    bool IsActive() const;

private:
    bool m_Enabled = false;
    double m_StartedAt = -1.0;
    Visual m_Visual;
};

void Render(const Visual& visual, const ImVec2& viewportPos, const ImVec2& viewportSize,
    const ImVec4& surfaceColor, ImDrawList* drawList);

} // namespace Stack::StartupReveal
