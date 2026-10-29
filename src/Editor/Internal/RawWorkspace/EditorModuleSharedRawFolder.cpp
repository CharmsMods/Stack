#include "Editor/EditorModule.h"

void EditorModule::SetRawWorkspaceFolder(const std::filesystem::path& root) {
    m_RawGallerySourceFoldersOpen = false;
    if(m_PermanentGalleryWorkspace && m_GalleryNavigationMotion)
        m_GalleryNavigationMotion->Folders(false,m_GalleryBrowseAnchor);
    if (!m_RawWorkspace.allSourceFolders && root.lexically_normal() == m_RawWorkspace.workspaceRoot.lexically_normal()) return;
    if (root.empty()) ClearRawWorkspace();
    else RequestOpenRawWorkspace(root);
}

void EditorModule::SetWorkspaceAppStatePersistenceEnabled(bool enabled) {
    if (m_WorkspaceAppStatePersistenceEnabled == enabled) return;
    ResetRawWorkspaceAppStatePersistState();
    m_WorkspaceAppStatePersistenceEnabled = enabled;
    if (enabled) SaveRawWorkspaceAppState();
}
