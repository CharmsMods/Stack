#include "AppShell.h"
#include "Project/ProjectPath.h"
#include "imgui_internal.h"
#include <algorithm>

bool AppShell::CloseAutoBracketWorkspace(std::uint64_t id) {
    const auto found = std::find_if(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(),
        [id](const auto& workspace) { return workspace->id == id; });
    if (found == m_ProjectWorkspaces.end() || !(*found)->editor->IsAutoBracketWorkspace()) return false;
    if (m_PendingWorkspaceRetirement && m_PendingWorkspaceRetirement != id) return true;
    (*found)->editor->CancelAutoBracketingForWorkspaceClose();
    m_PendingWorkspaceRetirement = id;
    return true;
}

void AppShell::TickAutoBracketWorkspaces() {
    std::vector<std::string> protectedProjects;
    std::vector<std::filesystem::path> protectedPaths;
    if (m_PendingGalleryTabOpen && m_PendingGalleryTabOpen->kind == GalleryTabOpen::Kind::Project)
        protectedPaths.push_back(m_PendingGalleryTabOpen->path);
    for (const auto& target : m_GalleryTabOpenQueue)
        if (target.kind == GalleryTabOpen::Kind::Project) protectedPaths.push_back(target.path);
    const auto protectWorkspace = [&](const auto& workspace) {
        if (workspace->editor->IsAutoBracketWorkspace()) return;
        protectedPaths.push_back(workspace->editor->GetCurrentProjectFileName());
        if (workspace->loadTransition.phase != ProjectLoadPhase::None)
            protectedPaths.push_back(workspace->loadTransition.projectFileName);
        const auto& id = workspace->editor->GetProjectDocumentId();
        if (!id.empty()) protectedProjects.push_back(id);
        if (const auto* snapshot = workspace->editor->GetActiveRawProjectSnapshot())
            protectedProjects.push_back(snapshot->projectId);
    };
    for (const auto& workspace : m_ProjectWorkspaces) protectWorkspace(workspace);
    for (const auto& workspace : m_RetiredProjectWorkspaces) protectWorkspace(workspace);
    // Editing, saving, loading or processing another project does not own
    // this worker. Only application shutdown blocks independent queues.
    const bool blocked = m_CloseRequested || !m_ContinueMainWindowCloseSource.empty() ||
        m_MainWindowCloseSavePending || !m_StartupRevealVisual.AllowsInput();
    auto* gallery = FindProjectWorkspace(m_GalleryWorkspaceId);
    if (!gallery) return;
    const auto& galleryRoot = gallery->editor->GetRawWorkspaceState().workspaceRoot;
    bool workerExists = false;
    const auto publishCatalogChanges = [this](EditorModule& worker) {
        if (!worker.ConsumeAutoBracketCatalogChanged()) return;
        const auto& root = worker.GetRawWorkspaceState().workspaceRoot;
        for (auto& workspace : m_ProjectWorkspaces)
            if (!workspace->editor->IsAutoBracketWorkspace() &&
                workspace->editor->GetRawWorkspaceState().workspaceRoot == root)
                workspace->rawCatalogRefreshPending = true;
    };
    for (auto& workspace : m_ProjectWorkspaces) {
        if (!workspace->editor->IsAutoBracketWorkspace()) continue;
        workerExists |= Stack::Project::SameProjectPath(
            workspace->editor->GetRawWorkspaceState().workspaceRoot, galleryRoot);
        workspace->editor->SetAutoBracketProtectedProjectIds(protectedProjects,protectedPaths);
        workspace->editor->TickAutoBracketing(blocked);
        publishCatalogChanges(*workspace->editor);
    }
    for (auto& workspace : m_RetiredProjectWorkspaces) {
        if (!workspace->editor->IsAutoBracketWorkspace()) continue;
        workerExists |= Stack::Project::SameProjectPath(
            workspace->editor->GetRawWorkspaceState().workspaceRoot, galleryRoot);
        // Cancellation must publish its queue state before a replacement
        // scheduler reads that folder's saved progress.
        workspace->editor->TickAutoBracketing(true);
        publishCatalogChanges(*workspace->editor);
    }
    for (auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id != m_ActiveProjectWorkspace ||
            !workspace->rawCatalogRefreshPending || workspace->editor->IsRawWorkspaceScanBusy() ||
            workspace->editor->IsWorkspaceTransitionPending()) continue;
        workspace->rawCatalogRefreshPending = false;
        workspace->editor->RescanRawWorkspace();
    }
    if (workerExists) return;
    auto& scheduler = *gallery->editor;
    scheduler.SetAutoBracketProtectedProjectIds(protectedProjects,protectedPaths);
    scheduler.TickAutoBracketing(blocked);
    if (blocked || !scheduler.ConsumeAutoBracketWorkspaceRequest()) return;

    auto workspace = std::make_unique<ProjectWorkspace>(m_NextProjectWorkspace++);
    workspace->editor->Initialize(m_Window, m_Appearance.get(), false);
    ConfigureWorkspaceEditor(*workspace->editor);
    workspace->editor->SetRawWorkspaceFolder(galleryRoot);
    if (scheduler.TransferAutoBracketingTo(*workspace->editor)) {
        // Starting automatic work does not change the user's selection.
        m_ProjectWorkspaces.push_back(std::move(workspace));
    } else {
        workspace->editor->RequestWorkerShutdownForAppClose();
        m_RetiredProjectWorkspaces.push_back(std::move(workspace));
    }
}
