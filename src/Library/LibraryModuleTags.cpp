#include "LibraryModule.h"

#include "App/settings/AppearanceTheme.h"
#include "TagManager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace {

ImVec4 BlendColor(const ImVec4& from, const ImVec4& to, float t) {
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        from.x + (to.x - from.x) * clamped,
        from.y + (to.y - from.y) * clamped,
        from.z + (to.z - from.z) * clamped,
        from.w + (to.w - from.w) * clamped);
}

} // namespace

void LibraryModule::RenderTagsDrawer(
    StackAppearance::AppearanceManager* appearance,
    bool wallpaperSurfaces,
    const StackAppearance::RuntimeSurfacePalette& surfacePalette,
    float dt) {
    (void)appearance;
    const ImVec2 gridPos = ImGui::GetWindowPos();
    const ImVec2 gridSize = ImGui::GetWindowSize();
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const float mainViewportLeft = ImGui::GetMainViewport()->Pos.x;
    const auto allTags = TagManager::Get().GetAllKnownTags();

    constexpr float panelWidth = 242.0f;
    const float desiredHeight = 142.0f +
        static_cast<float>(std::min<std::size_t>(allTags.size(), 8u)) * 27.0f;
    const float panelHeight = std::clamp(
        desiredHeight,
        176.0f,
        std::max(176.0f, std::min(390.0f, gridSize.y - 32.0f)));
    const ImVec2 panelOrigin(
        gridPos.x + 10.0f,
        gridPos.y + (gridSize.y - panelHeight) * 0.5f);

    const bool pointerOnEdge =
        mousePos.x >= mainViewportLeft && mousePos.x <= gridPos.x + 15.0f &&
        mousePos.y >= gridPos.y && mousePos.y <= gridPos.y + gridSize.y;
    const bool pointerOverPanel = m_FilterPanelExpanded &&
        mousePos.x >= panelOrigin.x - 10.0f &&
        mousePos.x <= panelOrigin.x + panelWidth + 12.0f &&
        mousePos.y >= panelOrigin.y - 10.0f &&
        mousePos.y <= panelOrigin.y + panelHeight + 10.0f;
    bool hoveringTagsPanel = pointerOnEdge || pointerOverPanel;

    if (ImGui::IsDragDropActive()) {
        hoveringTagsPanel = true;
    }

    if (hoveringTagsPanel) {
        m_FilterPanelHoverGrace = 0.28f;
    } else {
        m_FilterPanelHoverGrace = std::max(0.0f, m_FilterPanelHoverGrace - dt);
    }
    m_FilterPanelExpanded = hoveringTagsPanel || m_FilterPanelHoverGrace > 0.0f;

    const float targetWidth = m_FilterPanelExpanded ? panelWidth : 0.0f;
    m_FilterPanelWidthAnim += (targetWidth - m_FilterPanelWidthAnim) *
        std::min(1.0f, dt * 13.0f);
    if (std::abs(m_FilterPanelWidthAnim - targetWidth) < 0.1f) {
        m_FilterPanelWidthAnim = targetWidth;
    }

    if (m_FilterPanelWidthAnim <= 0.1f) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 panelMin = panelOrigin;
    const ImVec2 panelMax(
        panelOrigin.x + m_FilterPanelWidthAnim,
        panelOrigin.y + panelHeight);
    ImVec4 colBgOpaqueVec = surfacePalette.drawerSurface;
    if (wallpaperSurfaces) {
        colBgOpaqueVec.w = std::max(colBgOpaqueVec.w, 0.92f);
    } else {
        colBgOpaqueVec = ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        colBgOpaqueVec.w = 0.96f;
    }
    const float reveal = std::clamp(m_FilterPanelWidthAnim / panelWidth, 0.0f, 1.0f);
    for (int layer = 3; layer >= 1; --layer) {
        const float spread = static_cast<float>(layer) * 5.0f;
        drawList->AddRectFilled(
            ImVec2(panelMin.x - spread, panelMin.y - spread),
            ImVec2(panelMax.x + spread, panelMax.y + spread),
            IM_COL32(0, 0, 0, static_cast<int>(7.0f * reveal)),
            14.0f + spread);
    }
    colBgOpaqueVec.w *= reveal;
    drawList->AddRectFilled(
        panelMin,
        panelMax,
        ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec),
        12.0f);

    const float contentWidth = m_FilterPanelWidthAnim - 28.0f;
    if (contentWidth <= 120.0f) {
        return;
    }

    ImGui::SetCursorScreenPos(ImVec2(panelOrigin.x + 14.0f, panelOrigin.y + 13.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(7.0f, 5.0f));
    ImGui::BeginChild(
        "LibraryTagsDrawer",
        ImVec2(contentWidth, panelHeight - 26.0f),
        false,
        ImGuiWindowFlags_None);

    ImGui::TextUnformatted("Filters");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));

    auto& currentSelection = m_ShowAssets ? m_SelectedAssets : m_SelectedProjects;
    const bool hasSelectionForTagging = !currentSelection.empty();
    auto applyTagToSelection = [&]() {
        std::string tagText = m_AddTagBuffer;
        auto firstNonSpace = std::find_if_not(tagText.begin(), tagText.end(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        });
        auto lastNonSpace = std::find_if_not(tagText.rbegin(), tagText.rend(), [](unsigned char ch) {
            return std::isspace(ch) != 0;
        }).base();
        if (firstNonSpace >= lastNonSpace || currentSelection.empty()) {
            return false;
        }

        tagText = std::string(firstNonSpace, lastNonSpace);
        for (const auto& fileName : currentSelection) {
            TagManager::Get().AddTag(fileName, tagText);
        }
        m_AddTagBuffer[0] = '\0';
        return true;
    };

    bool noTagFilter = m_FilterNoTag;
    if (ImGui::Checkbox("Untagged only", &noTagFilter)) {
        m_FilterNoTag = noTagFilter;
        if (m_FilterNoTag) {
            m_ActiveTagFilters.clear();
        }
    }

    if (!m_FilterNoTag) {
        for (const auto& tag : allTags) {
            bool active = m_ActiveTagFilters.count(tag) > 0;
            if (ImGui::Checkbox(tag.c_str(), &active)) {
                if (active) {
                    m_ActiveTagFilters.insert(tag);
                } else {
                    m_ActiveTagFilters.erase(tag);
                }
            }
        }
    }

    if (!m_ActiveTagFilters.empty() || m_FilterNoTag) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ImGui::SmallButton("Clear Filters")) {
            m_ActiveTagFilters.clear();
            m_FilterNoTag = false;
        }
        ImGui::PopStyleColor(3);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    const bool tagReady = hasSelectionForTagging && (m_AddTagBuffer[0] != '\0');
    if (tagReady) {
        if (wallpaperSurfaces) {
            const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, BlendColor(surfacePalette.controlSurface, accent, 0.24f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, BlendColor(surfacePalette.controlSurfaceHovered, accent, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, BlendColor(surfacePalette.controlSurfaceActive, accent, 0.36f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(64, 150, 84, 92));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(74, 168, 94, 104));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(82, 184, 102, 116));
        }
    }
    ImGui::BeginDisabled(!hasSelectionForTagging);
    ImGui::SetNextItemWidth(-1.0f);
    const bool submittedTag = ImGui::InputTextWithHint(
        "##addtag",
        hasSelectionForTagging ? "Tag selected..." : "Select items to tag",
        m_AddTagBuffer,
        sizeof(m_AddTagBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::EndDisabled();
    if (tagReady) {
        ImGui::PopStyleColor(3);
    }
    if (submittedTag) {
        applyTagToSelection();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}
