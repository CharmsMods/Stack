#include "Editor/EditorModule.h"
#include "App/AppHeaderStyle.h"
#include "App/settings/AppearanceTheme.h"
#include "App/UiPerformanceTrace.h"
#include "App/WorkspacePresentation.h"
#include "App/WorkspaceInputScope.h"
#include "Editor/UI/WindowDrawOpacity.h"
#include "RawLabUiSupport.h"
#include "Library/LibraryControls.h"
#include "Editor/NodeEditors/NodeEditorPanelPresentation.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

using namespace Stack::Editor::RawLabInternal;

void EditorModule::RenderRawWorkspaceSectionPanel(
    const ImVec2& position, const ImVec2& size, float visibleWidth,
    const std::function<void()>& libraryNavigation) {
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::ViewPanel);
    m_RawSectionPanelFrame = ImGui::GetFrameCount();
    if (visibleWidth <= 0.5f || size.x <= 0.0f || size.y <= 0.0f) return;
    EnsureRawWorkspaceLoaded();
    if (!m_PermanentGalleryWorkspace) LoadResourceTextures();
    ImGui::SetNextWindowPos(position);
    ImGui::SetNextWindowSize(ImVec2(std::min(size.x, visibleWidth), size.y));
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    const ImVec4 panelTint = Stack::Header::ResolvePanelColor(
        GetWorkspaceBaseColor(), Stack::Header::ResolveSectionTintOpacity(visibleWidth / size.x));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panelTint);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    char name[80];
    std::snprintf(name, sizeof(name), "##RawWorkspaceSection_%p", static_cast<void*>(this));
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus;
    if (ImGui::Begin(name, nullptr, flags)) {
        const float unit = ImGui::GetFontSize() / (m_PermanentGalleryWorkspace ? 16.f : 13.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2((m_PermanentGalleryWorkspace ? 12.f : 14.f) * unit, 16.0f * unit));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,
            m_PermanentGalleryWorkspace ? 0.f : ImGui::GetStyle().ChildRounding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, m_PermanentGalleryWorkspace ? ImVec2(0, 0) : ImGui::GetStyle().ItemSpacing);
        const bool visible = ImGui::BeginChild("SectionContents", size, ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const float expansion = std::clamp(visibleWidth / size.x, 0.f, 1.f);
        const float contentAlpha = expansion * expansion * (3.f - 2.f * expansion);
        // Suppress transition input without changing enabled/disabled styling.
        // Real disabled controls retain their normal appearance throughout.
        Stack::Workspace::InputScope transitionInput(visibleWidth < size.x - 0.5f, false);
        ImGui::BeginDisabled(Stack::Workspace::IsPreview());
        if (visible && m_PermanentGalleryWorkspace) {
            if (libraryNavigation) libraryNavigation();
            RenderRawGalleryFolderPanel();
        } else if (visible) {
            const bool bracketSource=IsBracketingToolActive();
            const auto tabButton=[&](const char* label,int tab) {
                if (ImGui::GetCursorPosX()>14.f*unit &&
                    ImGui::GetCursorPosX()+ImGui::CalcTextSize(label).x+16.f*unit>size.x-14.f*unit-(m_ViewSectionTab==1 ? 26.f*unit : 0.f))
                    ImGui::NewLine();
                if (BareToolIslandButton(label,m_ViewSectionTab==tab))
                    SetViewSectionTab(tab==1 && m_ViewSectionTab==1 ? 0 : tab);
            };
            tabButton(bracketSource ? "Source" : "Layers",0);
            ImGui::SameLine(0,8.f*unit);tabButton("Controls",1);
            if (!bracketSource) { ImGui::SameLine(0,8.f*unit);tabButton("Settings",2); }
            ImGui::SameLine(0,8.f*unit);tabButton("Assets",3);
            if (m_ViewSectionTab==1) {
                ImGui::SameLine();
                ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),size.x-38.f*unit));
                RenderNodeEditorPanelMenu();
            }
            ImGui::Spacing();
            const bool controls=m_ViewSectionTab==1;
            const bool settings = !bracketSource && m_ViewSectionTab==2;
            ImGui::PushID(m_ViewSectionTab==3 ? "Assets" : controls ? "Controls" : bracketSource ? "Source" : settings ? "Settings" : "Layers");
            if (settings) ImGui::PushID(static_cast<int>(m_RawWorkspaceLabUi.activeTool));
            const bool bodyVisible=ImGui::BeginChild("PanelBody", ImVec2(0, 0), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoBackground | (controls ? ImGuiWindowFlags_NoScrollbar : ImGuiWindowFlags_None));
            {
                if (bodyVisible && m_ViewSectionTab==3) {
                    RenderRawAssetsPanel();
                } else if (bodyVisible && controls) {
                    RenderNodeEditorPanel();
                } else if (bodyVisible && bracketSource) {
                    RenderBracketingSidePanelUI();
                } else if (bodyVisible && settings) {
                    RenderRawWorkspaceToolSettings();
                } else if (bodyVisible && IsRawWorkspaceProjectActive() &&
                    m_Project->rawMode == Stack::RawWorkspace::RawProjectMode::UnifiedLayers) {
                    m_RawLayerPanel.Prune(ImGui::GetFrameCount());
                    // This is a permanent section host. The old floating/docked
                    // presentation flag does not move it out of the section panel.
                    RenderRawLayerPanelContents();
                } else if (bodyVisible && IsMultiFrameRawProjectActive()) {
                    ImGui::TextWrapped("Layers become available on a developed RAW image. Capture controls remain beside the viewport.");
                } else if (bodyVisible) {
                    ImGui::TextWrapped("Open an image from Gallery to work with its layers and masks.");
                }
            }
            auto* body=ImGui::GetCurrentWindow();
            ImGui::EndChild();
            if (controls && bodyVisible)
                Stack::Editor::DrawControlsPanelScrollbar(body,ImGui::GetWindowPos().x+size.x);
            if (settings) ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        // Include custom plots and thumbnails without double-dimming widgets.
        // The panel's separate translucent tint follows expansion above.
        Stack::Editor::ApplyWindowDrawOpacity(ImGui::GetCurrentWindow(),contentAlpha);
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
    }
    // Keep the section above the full-screen workspace without taking focus.
    // This also puts its tint after the viewport's sharp image draw.
    if (ImGui::GetCurrentContext()->OpenPopupStack.empty())
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(5);
}

void EditorModule::RequestRawSettingsPanel() {
    if (Stack::Workspace::IsPreview()) return;
    SetViewSectionTab(2);
    m_RawSettingsPanelRequested = true;
}

bool EditorModule::ConsumeRawSettingsPanelRequest() {
    return std::exchange(m_RawSettingsPanelRequested, false);
}

bool EditorModule::GetRawActiveControlBounds(ImVec2& minimum, ImVec2& maximum) const {
    const int frame = ImGui::GetFrameCount();
    if (m_PermanentGalleryWorkspace || m_RawActiveControlFrame < frame - 1 ||
        m_RawActiveControlMaximum.x <= m_RawActiveControlMinimum.x ||
        m_RawActiveControlMaximum.y <= m_RawActiveControlMinimum.y) return false;
    minimum = m_RawActiveControlMinimum;
    maximum = m_RawActiveControlMaximum;
    return true;
}

bool EditorModule::ConsumeRawToolPickerRequest() {
    const bool requested = m_RawToolPickerRequested;
    m_RawToolPickerRequested = false;
    return requested;
}

const Stack::RawWorkspace::GalleryPresentation& EditorModule::GetRawWorkspacePanelGalleryPresentation() {
    SynchronizeRawGalleryFolderScope();
    const auto& source = GetRawWorkspaceCategoryPresentation();
    if (!m_PermanentGalleryWorkspace || !m_RawGalleryFolderFilter) return source;
    if (m_RawGalleryFilteredRevision == m_RawWorkspaceGalleryRevision &&
        m_RawGalleryFilteredMode == m_RawWorkspaceGalleryContentMode &&
        m_RawGalleryFilteredQueueRevision == m_RawWorkspaceCategoryQueueRevision) return m_RawGalleryFilteredPresentation;
    const std::string folder = *m_RawGalleryFolderFilter;
    const auto contains = [&](const std::string& key) {
        return GalleryFolderTree::ContainsFolderKey(folder, key);
    };
    m_RawGalleryFilteredPresentation = source;
    auto& filtered = m_RawGalleryFilteredPresentation;
    filtered.groups.erase(std::remove_if(filtered.groups.begin(), filtered.groups.end(),
        [&](const auto& group) { return !contains(group.folderKey); }), filtered.groups.end());
    filtered.projects.erase(std::remove_if(filtered.projects.begin(), filtered.projects.end(),
        [&](const auto& project) {
            const auto* reference = FindRawWorkspaceSourceByKey(project.referenceSourceKey);
            return !reference || !contains(reference->parentFolderKey);
        }), filtered.projects.end());
    filtered.totalSources = 0;
    for (const auto& group : filtered.groups) filtered.totalSources += static_cast<int>(group.sources.size());
    m_RawGalleryFilteredRevision = m_RawWorkspaceGalleryRevision;
    m_RawGalleryFilteredMode = m_RawWorkspaceGalleryContentMode;
    m_RawGalleryFilteredQueueRevision = m_RawWorkspaceCategoryQueueRevision;
    return filtered;
}
