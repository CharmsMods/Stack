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
    // User-started first processing, imports and saves get the next admission
    // slot. An already running automatic bracket retains its own owner and
    // continues at background CPU/IO priority, with foreground GPU batches
    // selected before its next batch. Ordinary edits do not stop that work.
    const auto foregroundWork = [](const auto& workspace) {
        const auto& editor = *workspace->editor;
        return !editor.IsAutoBracketWorkspace() &&
            (editor.IsUserStartedProcessingBusy() || editor.IsGraphDropImportBusy() ||
             editor.IsSourceLoadBusy() || editor.IsRawWorkspaceProjectLoadBusy() || editor.IsExportBusy() ||
             editor.IsProjectFileSaveBusy() || editor.IsRawWorkspaceProjectSaveBusy());
    };
    const bool blocked = m_CloseRequested || !m_ContinueMainWindowCloseSource.empty() ||
        m_MainWindowCloseSavePending || !m_StartupRevealVisual.AllowsInput();
    const bool foregroundBusy = std::any_of(m_ProjectWorkspaces.begin(),m_ProjectWorkspaces.end(),foregroundWork) ||
        std::any_of(m_RetiredProjectWorkspaces.begin(),m_RetiredProjectWorkspaces.end(),foregroundWork);
    auto* gallery = FindProjectWorkspace(m_GalleryWorkspaceId);
    if (!gallery) return;
    const auto& galleryRoot = gallery->editor->GetRawWorkspaceState().workspaceRoot;
    bool workerExists = false;
    const auto publishCatalogChanges = [this](EditorModule& worker) {
        if (!worker.ConsumeRawWorkspaceCatalogChanged()) return;
        const auto& root = worker.GetRawWorkspaceState().workspaceRoot;
        for (auto& workspace : m_ProjectWorkspaces)
            if (workspace->editor.get() != &worker && !workspace->editor->IsAutoBracketWorkspace() &&
                (workspace->editor->IsRawWorkspaceAllFolders() ||
                 workspace->editor->GetRawWorkspaceState().workspaceRoot == root ||
                 Stack::Project::SameProjectPath(workspace->editor->GetRawWorkspaceState().workspaceRoot, root)))
                workspace->rawCatalogRefreshPending = true;
    };
    for (auto& workspace : m_ProjectWorkspaces) {
        if (!workspace->editor->IsAutoBracketWorkspace()) {
            publishCatalogChanges(*workspace->editor);
            continue;
        }
        const auto& workerRoot=workspace->editor->GetRawWorkspaceState().workspaceRoot;
        workerExists |= workerRoot==galleryRoot || Stack::Project::SameProjectPath(workerRoot,galleryRoot);
        workspace->editor->SetAutoBracketProtectedProjectIds(protectedProjects,protectedPaths);
        workspace->editor->TickAutoBracketing(blocked || foregroundBusy);
        publishCatalogChanges(*workspace->editor);
    }
    for (auto& workspace : m_RetiredProjectWorkspaces) {
        if (!workspace->editor->IsAutoBracketWorkspace()) {
            publishCatalogChanges(*workspace->editor);
            continue;
        }
        const auto& workerRoot=workspace->editor->GetRawWorkspaceState().workspaceRoot;
        workerExists |= workerRoot==galleryRoot || Stack::Project::SameProjectPath(workerRoot,galleryRoot);
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
    // Aggregate browsing must not launch a mixed-folder automatic batch under
    // the remembered anchor root. Existing root-owned workers ticked above.
    if (scheduler.IsRawWorkspaceAllFolders() || scheduler.IsRawWorkspaceFolderBrowserOpen()) return;
    scheduler.SetAutoBracketProtectedProjectIds(protectedProjects,protectedPaths);
    scheduler.TickAutoBracketing(blocked || foregroundBusy);
    if (blocked || foregroundBusy || !scheduler.ConsumeAutoBracketWorkspaceRequest()) return;

    auto workspace = std::make_unique<ProjectWorkspace>(m_NextProjectWorkspace++);
    workspace->sourceWorkspaceRoot = galleryRoot;
    workspace->editor->Initialize(m_Window, m_Appearance.get(), false);
    ConfigureWorkspaceEditor(*workspace->editor);
    workspace->editor->SetRawWorkspaceFolder(galleryRoot);
    if (scheduler.TransferAutoBracketingTo(*workspace->editor)) {
        // Starting automatic work does not change the user's selection.
        workspace->editor->SetWorkspaceDetachedWindowsVisible(false);
        m_ProjectWorkspaces.push_back(std::move(workspace));
    } else {
        workspace->editor->RequestWorkerShutdownForAppClose();
        m_RetiredProjectWorkspaces.push_back(std::move(workspace));
    }
}
