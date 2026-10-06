#pragma once

#include <imgui.h>

namespace Stack::UiActivity {

// Keep operation guards without dimming a panel during work. Ordinary
// unavailable controls retain their disabled appearance. Pair with EndDisabled.
inline void BeginDisabledForWork(bool busy, bool unavailable = false) {
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha,
        busy && !unavailable ? 1.0f : ImGui::GetStyle().DisabledAlpha);
    ImGui::BeginDisabled(busy || unavailable);
    ImGui::PopStyleVar();
}

} // namespace Stack::UiActivity
