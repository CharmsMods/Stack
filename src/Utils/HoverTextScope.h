#pragma once
#include <imgui_internal.h>

namespace ImGuiExtras {
// Suppress hover text produced by a host, including shared controls. Explicit
// context menus and click-opened popups retain their normal lifecycle.
class HoverTextScope {
public:
    explicit HoverTextScope(bool enabled = true) : enabled_(enabled),
        previous_(GImGui->TooltipPreviousWindow), count_(previous_ ? previous_->BeginCount : 0) {}
    ~HoverTextScope() {
        auto* tooltip = GImGui->TooltipPreviousWindow;
        if (enabled_ && tooltip && (tooltip != previous_ || tooltip->BeginCount != count_))
            ImGui::SetWindowHiddenAndSkipItemsForCurrentFrame(tooltip);
    }
private:
    bool enabled_;
    ImGuiWindow* previous_;
    int count_;
};
}
