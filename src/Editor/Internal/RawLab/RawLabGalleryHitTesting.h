#pragma once

#include <imgui_internal.h>

#include <algorithm>

namespace Stack::Editor::RawLabInternal {

// Reserve the whole thumbnail for layout, but register only its exposed strip
// for input. A covered card must not claim hover or clicks before the front card.
inline bool GalleryThumbnailButton(const char* id, const ImVec2& tileSize,
    float exposedHeight, ImGuiButtonFlags flags) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID itemId = window->GetID(id);
    const float height = std::clamp(exposedHeight, 0.0f, tileSize.y);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImRect hit(minimum, ImVec2(minimum.x + tileSize.x, minimum.y + height));
    ImGui::ItemSize(tileSize);
    const bool visible = ImGui::ItemAdd(hit, itemId, nullptr,
        (flags & ImGuiButtonFlags_EnableNav) ? ImGuiItemFlags_None : ImGuiItemFlags_NoNav);
    if (!visible || height <= 0.0f) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(hit, itemId, &hovered, &held, flags);
    ImGui::RenderNavCursor(hit, itemId);
    return pressed;
}

}
