#include "Editor/EditorModule.h"
#include "App/AppHeaderStyle.h"
#include "App/WorkspacePresentation.h"
#include "RawLabUiSupport.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <utility>

using namespace Stack::Editor::RawLabInternal;

void EditorModule::RenderRawWorkspaceSectionPanel(
    const ImVec2& position, const ImVec2& size, float visibleWidth) {
    m_RawSectionPanelFrame = ImGui::GetFrameCount();
    if (visibleWidth <= 0.5f || size.x <= 0.0f || size.y <= 0.0f) return;
    EnsureRawWorkspaceLoaded();
    LoadResourceTextures();
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
        const float unit = ImGui::GetFontSize() / 13.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f * unit, 16.0f * unit));
        ImGui::BeginChild("SectionContents", size, ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground | (m_PermanentGalleryWorkspace ? 0 :
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse));
        ImGui::BeginDisabled(Stack::Workspace::IsPreview() || visibleWidth < size.x - 0.5f);
        if (m_PermanentGalleryWorkspace) {
            RenderRawGalleryFolderPanel();
        } else {
            if (BareToolIslandButton("Layers", !m_RawWorkspaceLabUi.settingsTabActive))
                m_RawWorkspaceLabUi.settingsTabActive = false;
            ImGui::SameLine(0, 8.f * unit);
            if (BareToolIslandButton("Settings", m_RawWorkspaceLabUi.settingsTabActive))
                m_RawWorkspaceLabUi.settingsTabActive = true;
            ImGui::Spacing();
            const bool settings = m_RawWorkspaceLabUi.settingsTabActive;
            ImGui::PushID(settings ? "Settings" : "Layers");
            if (settings) ImGui::PushID(static_cast<int>(m_RawWorkspaceLabUi.activeTool));
            ImGui::BeginChild("PanelBody", ImVec2(0, 0), ImGuiChildFlags_None,
                ImGuiWindowFlags_NoBackground);
            if (settings) {
                RenderRawWorkspaceToolSettings();
            } else if (IsRawWorkspaceProjectActive() &&
                m_Project->rawMode == Stack::RawWorkspace::RawProjectMode::UnifiedLayers) {
                m_RawLayerPanel.Prune(ImGui::GetFrameCount());
                // This is a permanent section host. The old floating/docked
                // presentation flag does not move it out of the section panel.
                RenderRawLayerPanelContents();
            } else if (IsMultiFrameRawProjectActive()) {
                ImGui::TextWrapped("Layers become available on a developed RAW image. Capture controls remain beside the viewport.");
            } else {
                ImGui::TextWrapped("Open an image from Gallery to work with its layers and masks.");
            }
            ImGui::EndChild();
            if (settings) ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::EndChild();
        ImGui::PopStyleVar();
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
    m_RawWorkspaceLabUi.settingsTabActive = true;
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

void EditorModule::RenderRawGalleryFolderPanel() {
    const std::string workspace = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (workspace != m_RawGalleryFolderWorkspaceKey) {
        m_RawGalleryFolderWorkspaceKey = workspace;
        m_RawGalleryFolderFilter.reset();
        m_RawGalleryFilteredRevision = std::numeric_limits<std::uint64_t>::max();
    }
    ImGui::TextUnformatted("Folders");
    ImGui::Spacing();
    if (BareTextButton("Open folder")) {
        OpenRawWorkspaceFolderDialog();
        return;
    }
    if (m_RawWorkspace.workspaceRoot.empty()) {
        ImGui::TextWrapped("Choose a folder to browse its RAW images.");
        return;
    }
    ImGui::TextWrapped("%s", m_RawWorkspace.workspaceRoot.filename().string().c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", workspace.c_str());
    const auto selectFolder = [&](std::optional<std::string> folder) {
        if (folder == m_RawGalleryFolderFilter) return;
        m_RawGalleryFolderFilter = std::move(folder);
        // The display caches own pointers into their presentations. Invalidate
        // them together before either grid or filmstrip resolves its rows.
        InvalidateRawWorkspaceGalleryPresentation();
        m_RawWorkspaceLabGalleryScrollTargetY = 0.0f;
        m_RawWorkspaceLabGalleryScrollCurrentY = 0.0f;
        m_RawWorkspaceLabFilmstripScrollTargetX = 0.0f;
        m_RawWorkspaceGalleryLayoutAnimation.Reset();
    };
    if (ImGui::Selectable("All folders", !m_RawGalleryFolderFilter)) selectFolder(std::nullopt);
    struct Folder { std::map<std::string, Folder> children; std::string key; } root;
    for (const auto& group : GetRawWorkspaceGalleryPresentation().groups) {
        auto* folder = &root;
        std::filesystem::path path;
        for (const auto& part : std::filesystem::path(group.folderKey)) {
            if (part.empty() || part == ".") continue;
            path /= part;
            folder = &folder->children[part.string()];
            folder->key = path.generic_string();
        }
    }
    const auto renderFolders = [&](const auto& self, const Folder& folder) -> void {
        for (const auto& [label, child] : folder.children) {
            ImGui::PushID(child.key.c_str());
            auto flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (child.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
            if (m_RawGalleryFolderFilter == child.key) flags |= ImGuiTreeNodeFlags_Selected;
            const bool open = ImGui::TreeNodeEx("##Folder", flags, "%s", label.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectFolder(child.key);
            if (open && !child.children.empty()) { self(self, child); ImGui::TreePop(); }
            ImGui::PopID();
        }
    };
    renderFolders(renderFolders, root);
    ImGui::Spacing();
    ImGui::TextDisabled("Show");
    // Retain the Gallery's existing category and automatic-bracket actions.
    RenderRawWorkspaceLabGalleryHeader(false, true);
}

const Stack::RawWorkspace::GalleryPresentation& EditorModule::GetRawWorkspacePanelGalleryPresentation() {
    const std::string workspace = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (workspace != m_RawGalleryFolderWorkspaceKey) {
        m_RawGalleryFolderWorkspaceKey = workspace;
        m_RawGalleryFolderFilter.reset();
        m_RawGalleryFilteredRevision = std::numeric_limits<std::uint64_t>::max();
    }
    const auto& source = GetRawWorkspaceCategoryPresentation();
    if (!m_PermanentGalleryWorkspace || !m_RawGalleryFolderFilter) return source;
    if (m_RawGalleryFilteredRevision == m_RawWorkspaceGalleryRevision &&
        m_RawGalleryFilteredMode == m_RawWorkspaceGalleryContentMode &&
        m_RawGalleryFilteredQueueRevision == m_RawWorkspaceCategoryQueueRevision) return m_RawGalleryFilteredPresentation;
    const std::string folder = *m_RawGalleryFolderFilter;
    const auto contains = [&](const std::string& key) {
        return key == folder || (key.size() > folder.size() &&
            key.compare(0, folder.size(), folder) == 0 && key[folder.size()] == '/');
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
