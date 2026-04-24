#pragma once

#include <imgui.h>

namespace ImGuiExtras {
    void DrawSpinner(const char* label, float radius, int thickness, ImU32 color);
    void RenderBusyOverlay(const char* message);
}
