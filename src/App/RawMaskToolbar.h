#pragma once
#include "Animation/FrameTween.h"
#include <imgui.h>

namespace Stack::Navigation {
struct RawMaskToolbarState {
    bool enabled = false, menuOpen = false;
    float revealAmount = 0.f, height = 0.f;
    Animation::FrameTween revealMotion;
    double visibleUntil = 0;
    ImVec2 position{}, panelTogglePosition{};
};
}
