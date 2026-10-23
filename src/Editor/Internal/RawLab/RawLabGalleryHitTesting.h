#pragma once

#include "RawLabGalleryLayoutAnimation.h"

#include <imgui_internal.h>

#include <algorithm>

namespace Stack::Editor::RawLabInternal {

// Keep tile decoration and its hit target attached to the moving image while
// reserving the final rectangle for layout. This touches only submitted vertices
// during an active transition; it does no image work or layout invalidation.
class GalleryTileMotion {
public:
    GalleryTileMotion(const GalleryLayoutAnimation& motion, const std::string& key,
        const ImRect& target) : m_Target(target), m_Displayed(target) {
        m_Active = motion.DisplayRect(key, target, m_Displayed);
        if (m_Active) {
            m_Draw = ImGui::GetWindowDrawList();
            m_FirstVertex = m_Draw->VtxBuffer.Size;
        }
    }
    ~GalleryTileMotion() {
        if (!m_Active) return;
        for (int index = m_FirstVertex; index < m_Draw->VtxBuffer.Size; ++index)
            m_Draw->VtxBuffer[index].pos = Map(m_Draw->VtxBuffer[index].pos);
    }
    ImVec2 Map(const ImVec2& point) const {
        if (!m_Active) return point;
        return ImVec2(m_Displayed.Min.x + (point.x - m_Target.Min.x) *
                m_Displayed.GetWidth() / std::max(1.0f, m_Target.GetWidth()),
            m_Displayed.Min.y + (point.y - m_Target.Min.y) *
                m_Displayed.GetHeight() / std::max(1.0f, m_Target.GetHeight()));
    }
    ImRect Map(const ImRect& rect) const { return ImRect(Map(rect.Min), Map(rect.Max)); }
    bool Active() const { return m_Active; }
private:
    ImRect m_Target, m_Displayed;
    ImDrawList* m_Draw = nullptr;
    int m_FirstVertex = 0;
    bool m_Active = false;
};

// Reserve the whole thumbnail for layout, but register only its exposed strip
// for input. A covered card must not claim hover or clicks before the front card.
inline bool GalleryThumbnailButton(const char* id, const ImVec2& tileSize,
    float exposedHeight, ImGuiButtonFlags flags, const GalleryTileMotion* motion = nullptr,
    bool pointerEnabled = true) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    const ImGuiID itemId = window->GetID(id);
    const float height = std::clamp(exposedHeight, 0.0f, tileSize.y);
    const ImVec2 minimum = ImGui::GetCursorScreenPos();
    const ImRect target(minimum, ImVec2(minimum.x + tileSize.x, minimum.y + height));
    const ImRect hit = motion ? motion->Map(target) : target;
    ImGui::ItemSize(tileSize);
    const bool visible = ImGui::ItemAdd(hit, itemId, nullptr,
        ((flags & ImGuiButtonFlags_EnableNav) ? ImGuiItemFlags_None : ImGuiItemFlags_NoNav) |
            (pointerEnabled ? ImGuiItemFlags_None : ImGuiItemFlags_Disabled));
    if (!visible || height <= 0.0f || !pointerEnabled) return false;
    bool hovered = false, held = false;
    const bool pressed = ImGui::ButtonBehavior(hit, itemId, &hovered, &held, flags);
    ImGui::RenderNavCursor(target, itemId);
    return pressed;
}

}
