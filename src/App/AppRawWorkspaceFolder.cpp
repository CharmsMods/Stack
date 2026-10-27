#include "AppShell.h"
#include "Persistence/ProjectCatalogChanges.h"

void AppShell::SynchronizeRawWorkspaceFolder() {
    const auto catalogRevision = Stack::Project::ProjectCatalogRevision();
    // Catalog invalidation is shared. Browse roots and project origins are not.
    for (auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_GalleryWorkspaceId &&
            workspace->observedProjectCatalogRevision != catalogRevision) {
            workspace->observedProjectCatalogRevision = catalogRevision;
            workspace->rawCatalogRefreshPending = true;
        }
    }
    UpdateWorkspacePreferencesOwner();
}

void AppShell::UpdateWorkspacePreferencesOwner() {
    if (m_UiPerformanceCapture) {
        for (auto& workspace : m_ProjectWorkspaces)
            workspace->editor->SetWorkspaceAppStatePersistenceEnabled(false);
        return;
    }
    // Hidden project editors must never replace the source registry or Gallery
    // startup scope with their private editing context.
    const auto* gallery = FindProjectWorkspace(m_GalleryWorkspaceId);
    EditorModule* writer = gallery ? gallery->editor.get() : nullptr;
    for (auto& workspace : m_ProjectWorkspaces)
        if (workspace->editor.get() != writer)
            workspace->editor->SetWorkspaceAppStatePersistenceEnabled(false);
    if (writer) writer->SetWorkspaceAppStatePersistenceEnabled(true);
}
