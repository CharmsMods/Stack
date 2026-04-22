#pragma once

#include <imgui.h>

namespace RenderDockLayout {

ImGuiID GetDockSpaceId();
void ApplyDefaultLayout(ImGuiID dockSpaceId, const ImVec2& dockSpaceSize);

} // namespace RenderDockLayout
