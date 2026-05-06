#pragma once

#include <imgui.h>

namespace ImGuiExtras {
    float AnimateTowards(float current, float target, float deltaTime, float speed = 18.0f);
    float EaseOutCubic(float value);
    void DrawSpinner(const char* label, float radius, int thickness, ImU32 color);
    void RenderBusyOverlay(const char* message);
}
