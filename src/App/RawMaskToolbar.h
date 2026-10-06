#pragma once

#include "Editor/RawOperationMaskTarget.h"
#include "imgui.h"

namespace Stack::Navigation {

struct RawMaskToolbarState {
    bool enabled = false;
    float revealAmount = 0.f;
    double visibleUntil = 0.0;
    float visibleHeight = 0.f;
    ImVec2 position{};
    ImVec2 maskButtonPosition{};
    ImVec2 panelTogglePosition{};
    ImGuiID popupId = 0;
    bool menuOpen = false;
    bool menuDrawn = false;
    std::uint64_t targetWorkspace = 0;
    std::optional<Stack::Editor::RawOperationMaskTarget> target;
    std::string commandError;
};

} // namespace Stack::Navigation
