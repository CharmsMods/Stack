#include "AppShell.h"
#include "Persistence/ProjectCatalogChanges.h"

void AppShell::SynchronizeRawWorkspaceFolder() {
    const auto catalogRevision = Stack::Project::ProjectCatalogRevision();
    // Folder navigation is shared; project documents and editing state remain
    // owned by their tabs. RequestOpenRawWorkspace preserves a pinned project.
    for (auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_GalleryWorkspaceId &&
            workspace->observedProjectCatalogRevision != catalogRevision) {
            workspace->observedProjectCatalogRevision = catalogRevision;
            workspace->rawCatalogRefreshPending = true;
        }
        // Automatic queues keep their source folder while Gallery navigates.
        if (workspace->editor->IsAutoBracketWorkspace()) continue;
        if (!workspace->editor->AutoBracketWorkActive())
            workspace->editor->SetRawWorkspaceFolder(m_SharedRawWorkspaceRoot);
    }
    UpdateWorkspacePreferencesOwner();
}

void AppShell::UpdateWorkspacePreferencesOwner() {
    EditorModule* writer = m_Editor && !m_Editor->IsAutoBracketWorkspace() ? m_Editor : nullptr;
    if (!writer) for (auto& workspace : m_ProjectWorkspaces)
        if (!workspace->editor->IsAutoBracketWorkspace()) { writer = workspace->editor.get(); break; }
    for (auto& workspace : m_ProjectWorkspaces)
        if (workspace->editor.get() != writer)
            workspace->editor->SetWorkspaceAppStatePersistenceEnabled(false);
    if (writer) writer->SetWorkspaceAppStatePersistenceEnabled(true);
}
