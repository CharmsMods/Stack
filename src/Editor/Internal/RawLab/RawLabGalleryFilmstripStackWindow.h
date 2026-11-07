#pragma once

#include "Raw/RawGallerySimilarity.h"

#include <imgui_internal.h>
#include <algorithm>
#include <string>

namespace Stack::Editor::RawLabInternal {

// Stack cards extend above the filmstrip's child bounds. Keep their native
// input and drawing in one transparent, owner-scoped window, as originals do.
template <typename DrawCards>
void RenderGalleryFilmstripStackWindow(const std::string& identity,
    const ImVec2& base, float width, float tileHeight, float gap,
    std::size_t count, float drawerTop, float expansion, const ImRect& clip,
    const DrawCards& drawCards) {
    const auto layout = RawWorkspace::ComputeRawGalleryFilmstripStackLayout(
        count, tileHeight, gap, base.y, drawerTop, expansion);
    const float left = std::max(base.x, clip.Min.x);
    const float right = std::min(base.x + width, clip.Max.x);
    constexpr float headroom = 14.0f;
    if (right <= left || layout.bottom <= layout.top) return;
    const auto owner = ImGui::GetCurrentWindow()->ID;
    const auto key = ImHashData(identity.data(), identity.size(), owner);
    const auto name = "GalleryProjectStack###GalleryProjectStack_" + std::to_string(key);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowPos({left, layout.top - headroom}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({right - left, layout.bottom - layout.top + headroom}, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(name.c_str(), nullptr, flags)) {
        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        drawCards(layout);
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace Stack::Editor::RawLabInternal
