#include "Editor/EditorModule.h"

#include <array>

bool EditorModule::RenderRawWorkspaceGalleryFileMenu(bool includeFilmstripSort) {
    const bool hasWorkspace = !m_RawWorkspace.workspaceRoot.empty();
    bool workspaceChanged = false;
    if (ImGui::MenuItem("Add Source Folder...")) {
        OpenRawWorkspaceFolderDialog();
        workspaceChanged = true;
    }
    if (ImGui::MenuItem("Rescan", nullptr, false, hasWorkspace)) {
        RescanRawWorkspace();
    }
    if (includeFilmstripSort) {
        ImGui::Separator();
        const std::string workspaceKey = GetRawWorkspaceGalleryScopeKey();
        const auto organization =
            m_RawWorkspaceManualGroupings.find(workspaceKey);
        const auto currentSort =
            organization != m_RawWorkspaceManualGroupings.end()
                ? organization->second.sortMode
                : Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    TimelineAll;
        if (ImGui::BeginMenu(
                "Sort Filmstrip",
                (hasWorkspace && CanEditRawWorkspaceFilmstripOrganization()) ||
                    (m_PermanentGalleryWorkspace && !GetRawWorkspaceSourceFolders().empty()))) {
            if (m_PermanentGalleryWorkspace && ImGui::MenuItem("All Folders", nullptr,
                    IsRawWorkspaceAllFolders() && !IsRawWorkspaceFolderBrowserOpen(),
                    !GetRawWorkspaceSourceFolders().empty())) {
                m_RawWorkspaceGalleryContentMode = Stack::RawWorkspace::GalleryContentMode::Gallery;
                SetRawWorkspaceAllFolders(true);
            }
            constexpr std::array sortModes {
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::TimelineAll,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::TimelineByFolder,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::Manual,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    FileNameAscending,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    FileNameDescending,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    ModifiedNewestFirst,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    ModifiedOldestFirst,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    FolderThenNameAscending,
                Stack::RawWorkspace::RawGalleryFilmstripSortMode::
                    FolderThenNameDescending
            };
            for (const auto mode : sortModes) {
                if (ImGui::MenuItem(
                        Stack::RawWorkspace::
                            RawGalleryFilmstripSortModeLabel(mode),
                        nullptr,
                        !IsRawWorkspaceAllFolders() && currentSort == mode)) {
                    m_RawGallerySourceFoldersOpen = false;
                    SetRawWorkspaceFilmstripSortMode(mode);
                }
            }
            ImGui::EndMenu();
        }
    }
    return workspaceChanged;
}
