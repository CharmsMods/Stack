// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Animation/ControlMotion.h"
#include "Notifications/NotificationPresenter.h"
#include "imgui_internal.h"

namespace Stack::UiMotion {
// Read the existing last item only. Never submits a second hit target, changes
// LastItemData, edits a value or dispatches an application operation.
inline Animation::ControlInput LastItemInput(bool selected=false) {
    const bool mouseActive=ImGui::GetCurrentContext()->ActiveIdSource==ImGuiInputSource_Mouse;
    Animation::ControlInput input;
    input.identity=ImGui::GetItemID();
    input.frame=static_cast<std::uint64_t>(ImGui::GetFrameCount());
    input.deltaSeconds=ImGui::GetIO().DeltaTime;
    input.hovered=ImGui::IsItemHovered();
    input.held=ImGui::IsItemActive() && (!mouseActive ||
        ImGui::IsItemHovered(ImGuiHoveredFlags_NoNavOverride));
    input.selected=selected;
    input.focused=ImGui::IsItemFocused();
    input.enabled=(ImGui::GetCurrentContext()->LastItemData.ItemFlags & ImGuiItemFlags_Disabled)==0;
    input.visible=ImGui::IsItemVisible();
    input.reducedMotion=Stack::Notifications::SystemReducedMotion();
    return input;
}
} // namespace Stack::UiMotion
