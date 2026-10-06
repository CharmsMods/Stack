#include "AppShell.h"
#include "Utils/FileDialogs.h"
#include <algorithm>

void AppShell::RenderStackMenu() {
    auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(300.0f, 0.0f),
        ImVec2(440.0f, viewport->Size.y * 0.85f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 9.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    if (ImGui::BeginPopup("GlobalFileMenu")) {
        const EditorModule::ProjectFileCommandContext context =
            m_Editor->GetProjectFileCommandContext();
        const auto drawMenuIcon = [](unsigned int texture, bool enabled = true) {
            if (texture == 0) {
                return;
            }
            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const float iconSize = std::min(15.0f, itemMax.y - itemMin.y - 5.0f);
            const ImVec2 iconMin(
                itemMin.x + 6.0f,
                itemMin.y + (itemMax.y - itemMin.y - iconSize) * 0.5f);
            const ImVec2 iconMax(iconMin.x + iconSize, iconMin.y + iconSize);
            ImVec4 tint = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            if (!enabled) {
                tint = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            }
            ImGui::GetWindowDrawList()->AddImage(
                (ImTextureID)(intptr_t)texture,
                iconMin,
                iconMax,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                ImGui::ColorConvertFloat4ToU32(tint));
        };
        const auto fileMenuItem = [&drawMenuIcon](
                                      const char* label,
                                      const char* shortcut,
                                      unsigned int texture,
                                      bool enabled = true) {
            // Dear ImGui menu items own the text/shortcut/arrow columns, while
            // the icon is drawn over the row. Reserve a full, scale-friendly
            // icon column so the label never crosses the glyph.
            const std::string paddedLabel = std::string("        ") + label;
            const bool activated = ImGui::MenuItem(
                paddedLabel.c_str(), shortcut, false, enabled);
            drawMenuIcon(texture, enabled);
            return activated;
        };
        const auto menuSeparator = []() {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
        };
        if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId &&
            (m_CurrentTabId == 3 || m_CurrentTabId == 5)) {
            m_Editor->RenderRawWorkspaceGalleryFileMenu();
            menuSeparator();
        }
        if (fileMenuItem(
                "New Editor Project",
                "Ctrl+N",
                m_FileNewTexture,
                context.canCreateEditorProject)) {
            QueueFileAction(PendingFileAction::NewEditorProject);
        }
        if (fileMenuItem(
                "Open Project...",
                "Ctrl+O",
                m_FileOpenProjectTexture,
                context.canOpen)) {
            m_ShowOpenProjectPrompt = true;
        }

        menuSeparator();
        if (fileMenuItem(
                "Save Project",
                "Ctrl+S",
                m_FileSaveTexture,
                context.canSave)) {
            RequestFileMenuSave();
        }
        if (fileMenuItem(
                "Save As...",
                "Ctrl+Shift+S",
                m_FileSaveTexture,
                context.canSaveAs)) {
            RequestFileMenuSaveAs();
        }

        if (fileMenuItem(
                "Pack Project...",
                nullptr,
                m_FileSaveTexture,
                context.canSaveAs)) {
            const std::string defaultName =
                (m_Editor->GetCurrentProjectName().empty()
                    ? std::string("Untitled Project")
                    : m_Editor->GetCurrentProjectName()) + ".stack";
            const std::filesystem::path destination =
                FileDialogs::SaveProjectFileDialog(
                    "Pack Project for Sharing",
                    defaultName.c_str());
                if (!destination.empty()) {
                    std::string error;
                    if (!m_Editor->PackCurrentProject(destination, &error)) {
                        m_Editor->ShowUiNotification(
                            UiNotificationSeverity::Error,
                            error.empty()
                                ? "Project packing failed."
                                : error,
                            "file-menu-pack-project");
                    } else {
                        m_Editor->ShowUiNotification(
                            UiNotificationSeverity::Success,
                            "Packed project created. The working project remains active.",
                            "file-menu-pack-project");
                    }
                }
        }

        menuSeparator();
        const char* closeLabel = context.sessionKind ==
            EditorModule::ProjectSessionKind::RawPreview
            ? "Close Preview"
            : "Close Project";
        if (fileMenuItem(
                closeLabel,
                "Ctrl+W",
                m_FileExitProgramTexture,
                context.canClose)) {
            QueueFileAction(PendingFileAction::CloseCurrent);
        }
        menuSeparator();
        if (fileMenuItem(
                "Exit Stack",
                nullptr,
                m_FileExitProgramTexture)) {
            RequestMainWindowClose("file-menu-exit");
        }
        if (context.busy && !context.busyReason.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("%s", context.busyReason.c_str());
        }
        if ((m_CurrentTabId == 3 || m_CurrentTabId == 5) && !m_Editor->IsAutoBracketWorkspace()) {
            if (m_Editor->IsRawWorkspaceInfoAvailable() && !m_Editor->IsRawWorkspaceGalleryWorkspaceOpen() &&
                ImGui::MenuItem("Image information", nullptr, m_Editor->IsRawWorkspaceInfoOpen()))
                m_Editor->ToggleRawWorkspaceInfo();
            if (m_Editor->IsRawWorkspaceLockedByEditorProject() &&
                ImGui::MenuItem("Switch to RAW editing", nullptr, false, !m_RawWorkspaceSwitchSavePending)) {
                if (m_Editor->NeedsWorkspaceSaveBeforeTransition()) m_ShowRawWorkspaceSwitchPrompt = true;
                else m_Editor->CloseEditorProjectAndActivateRawWorkspace();
            }
        }
        menuSeparator();
        if (ImGui::MenuItem("Settings")) {
            m_SettingsPopupOpen = true;
            m_SettingsPopupOpenedAt = ImGui::GetTime();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(5);
}
