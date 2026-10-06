#include "Editor/EditorModule.h"

void EditorModule::SetRawWorkspaceFolder(const std::filesystem::path& root) {
    if (root.lexically_normal() == m_RawWorkspace.workspaceRoot.lexically_normal()) return;
    if (root.empty()) ClearRawWorkspace();
    else RequestOpenRawWorkspace(root);
}

void EditorModule::SetWorkspaceAppStatePersistenceEnabled(bool enabled) {
    if (m_WorkspaceAppStatePersistenceEnabled == enabled) return;
    ResetRawWorkspaceAppStatePersistState();
    m_WorkspaceAppStatePersistenceEnabled = enabled;
    if (enabled) SaveRawWorkspaceAppState();
}
