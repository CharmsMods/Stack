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
    ImVec2 gridPos = ImGui::GetWindowPos();
    ImVec2 gridSize = ImGui::GetWindowSize();
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    const float mainViewportLeft = ImGui::GetMainViewport()->Pos.x;

    bool hoveringTagsPanel = false;
    if (!m_FilterPanelExpanded) {
        if (mousePos.x >= mainViewportLeft && mousePos.x <= gridPos.x + 15.0f &&
            mousePos.y >= gridPos.y && mousePos.y <= gridPos.y + gridSize.y) {
            hoveringTagsPanel = true;
        }
    } else {
        if (mousePos.x >= mainViewportLeft && mousePos.x <= gridPos.x + m_FilterPanelWidthAnim + 15.0f &&
            mousePos.y >= gridPos.y && mousePos.y <= gridPos.y + gridSize.y) {
            hoveringTagsPanel = true;
        }
    }

    if (ImGui::IsDragDropActive()) {
        hoveringTagsPanel = true;
    }

    m_FilterPanelExpanded = hoveringTagsPanel;

    const float tagsPanelTargetWidth = m_FilterPanelExpanded ? 220.0f : 0.0f;
    m_FilterPanelWidthAnim += (tagsPanelTargetWidth - m_FilterPanelWidthAnim) * dt * 10.0f;
    if (std::abs(m_FilterPanelWidthAnim - tagsPanelTargetWidth) < 0.1f) {
        m_FilterPanelWidthAnim = tagsPanelTargetWidth;
    }

    if (m_FilterPanelWidthAnim <= 0.1f) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 panelMin = gridPos;

    const float gradientWidth = std::min(60.0f, m_FilterPanelWidthAnim);
    const float solidWidth = m_FilterPanelWidthAnim - gradientWidth;
    ImVec4 colBgOpaqueVec = surfacePalette.drawerSurface;
    ImU32 colBgOpaque = 0;
    ImU32 colBgTrans = 0;
    if (wallpaperSurfaces) {
        colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);
        colBgTrans = ImGui::ColorConvertFloat4ToU32(surfacePalette.drawerSurfaceTransparent);
    } else {
        ImVec4 workspaceColor = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
        const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
        const bool isLightBg = luminance >= 0.5f;
        colBgOpaqueVec = workspaceColor;
        colBgOpaqueVec.w = isLightBg ? 0.95f : 0.93f;
        colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);
        ImVec4 colBgTransVec = workspaceColor;
        colBgTransVec.w = 0.0f;
        colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);
    }
    const ImU32 colTitleText = ImGui::GetColorU32(ImGui::GetStyleColorVec4(ImGuiCol_Text));

    if (solidWidth > 0.0f) {
        drawList->AddRectFilled(panelMin, ImVec2(gridPos.x + solidWidth, gridPos.y + gridSize.y), colBgOpaque);
    }
    drawList->AddRectFilledMultiColor(
        ImVec2(gridPos.x + solidWidth, gridPos.y),
        ImVec2(gridPos.x + m_FilterPanelWidthAnim, gridPos.y + gridSize.y),
        colBgOpaque,
        colBgTrans,
        colBgTrans,
        colBgOpaque);

    const float contentWidth = m_FilterPanelWidthAnim - 40.0f;
    if (contentWidth <= 1.0f) {
        return;
    }

    ImGui::SetCursorScreenPos(ImVec2(gridPos.x + 16.0f, gridPos.y + 24.0f));
    if (wallpaperSurfaces) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    }
    ImGui::BeginChild("LibraryTagsDrawer", ImVec2(contentWidth, gridSize.y - 48.0f), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
    ImGui::TextUnformatted("LIBRARY FILTERS");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

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

    auto allTags = TagManager::Get().GetAllKnownTags();

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
        if (ImGui::SmallButton("Clear Filters")) {
            m_ActiveTagFilters.clear();
            m_FilterNoTag = false;
        }
    }

    ImGui::Spacing();
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
    ImGui::SetNextItemWidth(-14.0f);
    const bool submittedTag = ImGui::InputTextWithHint(
        "##addtag",
        "New tag...",
        m_AddTagBuffer,
        sizeof(m_AddTagBuffer),
        ImGuiInputTextFlags_EnterReturnsTrue);
    if (tagReady) {
        ImGui::PopStyleColor(3);
    }
    if (submittedTag) {
        applyTagToSelection();
    }

    ImGui::Dummy(ImVec2(0.0f, 16.0f));
    ImGui::SeparatorText("Theme");
    if (appearance != nullptr) {
        const std::string activePresetId = appearance->GetActivePresetId();
        const StackAppearance::ThemeDefinition* activePreset = appearance->GetActivePreset();
        const std::string currentPresetName = activePreset ? activePreset->displayName : "Custom";
        ImGui::SetNextItemWidth(-14.0f);
        if (ImGui::BeginCombo("##LibraryThemePresetCombo", currentPresetName.c_str())) {
            for (const auto& preset : appearance->GetFactoryThemes()) {
                const bool selected = activePresetId == preset.id;
                if (ImGui::Selectable(preset.displayName.c_str(), selected)) {
                    appearance->SelectPresetById(preset.id);
                    appearance->ApplyCurrentTheme(ImGui::GetIO(), ImGui::GetStyle());
                }
            }
            ImGui::EndCombo();
        }
    }

    ImGui::EndChild();
    if (wallpaperSurfaces) {
        ImGui::PopStyleColor();
    }
}
