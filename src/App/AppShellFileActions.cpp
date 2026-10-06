#include "AppShell.h"

#include "Async/TaskSystem.h"
#include "Utils/FileDialogs.h"

#include <GLFW/glfw3.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace {

constexpr int RootTabEditor = 1;

} // namespace

void AppShell::RequestMainWindowClose(const char* source) {
    if (!m_Editor) {
        if (m_Window) glfwSetWindowShouldClose(m_Window, GLFW_TRUE);
        return;
    }
    if (m_Window) glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
    const std::string deferredSource=source?source:"close";
    if(m_Editor->RequestAutoBracketForeground("close Stack",[this,deferredSource]{RequestMainWindowClose(deferredSource.c_str());})) {
        // The foreground prompt can be canceled. Recheck earlier tabs if
        // closing resumes instead of retaining old discard approvals.
        m_CloseApprovedWorkspaces.clear();
        if(m_Window)glfwSetWindowShouldClose(m_Window,GLFW_FALSE);
        return;
    }
    if (m_CloseRequested || m_MainWindowCloseSavePending) {
        return;
    }
    if (m_FileActionSavePending || m_ProjectLoadSavePending || m_RawWorkspaceSwitchSavePending ||
        m_Editor->IsWorkspaceTransitionPending() ||
        ActiveProjectLoadPhase() != LibraryToEditorProjectLoadPhase::None) {
        m_ContinueMainWindowCloseSource = deferredSource;
        return;
    }
    if (!m_Editor->FinishWorkspaceInteraction()) {
        m_CloseApprovedWorkspaces.clear();
        m_PendingMainWindowCloseSource.clear();
        m_ContinueMainWindowCloseSource.clear();
        PostNotification(UiNotificationSeverity::Warning,
            "Finish the current edit before closing Stack.", "close-workspace-interaction-busy");
        return;
    }

    if(m_Editor->IsBracketingPresentationActive())m_Editor->CancelBracketingPresentation();
    const std::string closeSource =
        (source && source[0] != '\0') ? source : "unknown";
    const EditorModule::ProjectFileCommandContext context =
        m_Editor->GetProjectFileCommandContext();
    if (context.busy && !context.canSave) {
        // Finish an in-flight load/import before taking its final save snapshot.
        m_ContinueMainWindowCloseSource = closeSource;
        return;
    }
    if (m_Editor->NeedsWorkspaceSaveBeforeTransition() || context.busy) {
        m_PendingMainWindowCloseSource = closeSource;
        m_MainWindowCloseSavePending = true;
        auto* editor = m_Editor;
        const auto scope = editor->GetNotifier();
        editor->RequestSaveWorkspaceBeforeClose([this, editor, scope, closeSource](bool success) {
            m_MainWindowCloseSavePending = false;
            m_PendingMainWindowCloseSource.clear();
            if (success) ContinueMainWindowClose(closeSource);
            else {
                m_CloseApprovedWorkspaces.clear();
                m_ContinueMainWindowCloseSource.clear();
                if (scope.Valid()) ReportSaveFailure(*editor, "The save failed, so Stack stayed open.",
                    [this, closeSource] { RequestMainWindowClose(closeSource.c_str()); });
            }
        });
        return;
    }

    for (const auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_ActiveProjectWorkspace) continue;
        const auto other = workspace->editor->GetProjectFileCommandContext();
        if (other.busy || workspace->editor->NeedsWorkspaceSaveBeforeTransition()) {
            if (ActivateProjectWorkspace(workspace->id)) {
                RequestMainWindowClose(closeSource.c_str());
            } else {
                m_CloseApprovedWorkspaces.clear();
                m_PendingMainWindowCloseSource.clear();
                m_ContinueMainWindowCloseSource.clear();
                PostNotification(UiNotificationSeverity::Warning,
                    "Finish the current edit before closing Stack.", "close-workspace-switch-busy");
            }
            return;
        }
    }
    BeginMainWindowClose(closeSource);
}

void AppShell::BeginMainWindowClose(const std::string& source) {
    if (m_CloseRequested) {
        return;
    }

    m_CloseRequested = true;
    m_ClosingPresentedFrames = 0;
    m_CloseRequestedAt = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
    m_CloseSource = source.empty() ? "unknown" : source;
    for (const auto& workspace : m_ProjectWorkspaces) m_NotificationStore->InvalidateOwner(workspace->id);
    m_NotificationStore->InvalidateOwner(m_AppNotifier.GetOwner().id);
    m_NotificationStore->InvalidateOwner(m_Library.GetNotifier().GetOwner().id);
    m_NotificationStore->InvalidateOwner(m_QueueRenderer.GetNotifier().GetOwner().id);
    if (m_Window) {
        glfwSetWindowShouldClose(m_Window, GLFW_FALSE);
    }
    TraceMainWindowState("close-request", m_CloseSource.c_str());
    TraceShutdownPhase("close-request");
    CancelWorkForMainWindowClose();
}

void AppShell::RequestFileMenuSave() {
    const EditorModule::ProjectFileCommandContext context =
        m_Editor->GetProjectFileCommandContext();
    if (!context.canSave) {
        PostNotification(
            UiNotificationSeverity::Warning,
            context.busyReason.empty()
                ? "There is no project available to save in the current state."
                : context.busyReason,
            "file-menu-save-unavailable");
        return;
    }

    if (m_Editor->HasUnsavedBracketingDraft()) {
        m_Editor->RequestSaveWorkspaceBeforeClose({});
        return;
    }
    const std::string projectName = m_Editor->GetCurrentProjectName().empty()
        ? "Untitled Project"
        : m_Editor->GetCurrentProjectName();
    m_Editor->RequestSaveCurrentProject(projectName);
}

void AppShell::RequestFileMenuSaveAs() {
    const EditorModule::ProjectFileCommandContext context =
        m_Editor->GetProjectFileCommandContext();
    if (!context.canSaveAs) {
        PostNotification(
            UiNotificationSeverity::Warning,
            context.busyReason.empty()
                ? "There is no project available for Save As."
                : context.busyReason,
            "file-menu-save-as-unavailable");
        return;
    }

    std::filesystem::path destination;
    if (context.storageKind ==
        Stack::Project::ProjectStorageKind::DirectoryBundle) {
        const std::string projectName = m_Editor->GetCurrentProjectName().empty()
            ? "RAW Project"
            : m_Editor->GetCurrentProjectName();
        const std::string selected = FileDialogs::SaveProjectBundleDialog(
            "Save Working Project As",
            projectName.c_str());
        if (selected.empty()) {
            return;
        }
        destination = selected;
    } else {
        std::string defaultName;
        if (!context.projectPath.empty() &&
            context.projectPath.extension() == ".stack") {
            defaultName = context.projectPath.filename().string();
        }
        if (defaultName.empty()) {
            defaultName = (m_Editor->GetCurrentProjectName().empty()
                ? std::string("Untitled Project")
                : m_Editor->GetCurrentProjectName()) + ".stack";
        }
        const std::string selected = FileDialogs::SaveProjectFileDialog(
            "Save Project As",
            defaultName.c_str());
        if (selected.empty()) {
            return;
        }
        destination = selected;
    }

    // Native save dialogs run their own modal message loop. Defer the project
    // mutation until the following ImGui frame so the save cannot be dropped
    // while the menu/dialog frame is still unwinding.
    m_PendingFileMenuSaveAsDestination = std::move(destination);
    m_PendingFileMenuSaveAsQueuedFrame = ImGui::GetFrameCount();
}

void AppShell::DispatchPendingFileMenuSaveAs() {
    if (m_PendingFileMenuSaveAsDestination.empty() ||
        m_PendingFileMenuSaveAsQueuedFrame < 0 ||
        ImGui::GetFrameCount() <= m_PendingFileMenuSaveAsQueuedFrame) {
        return;
    }

    std::filesystem::path destination =
        std::move(m_PendingFileMenuSaveAsDestination);
    m_PendingFileMenuSaveAsDestination.clear();
    m_PendingFileMenuSaveAsQueuedFrame = -1;
    m_Editor->RequestSaveProjectAs(destination);
}

void AppShell::ClearPendingFileAction() {
    m_PendingFileAction = PendingFileAction::None;
    m_PendingFileProjectPath.clear();
    m_FileActionSavePending = false;
    m_FileActionSaveFailed = false;
}

void AppShell::QueueFileAction(
    PendingFileAction action,
    std::filesystem::path projectPath) {
    if (m_Editor->IsAutoBracketWorkspace()) {
        if (action == PendingFileAction::CloseCurrent || action == PendingFileAction::CloseWorkspace)
            CloseAutoBracketWorkspace(m_ActiveProjectWorkspace);
        return;
    }
    if(m_Editor->RequestAutoBracketForeground("change projects",[this,action,projectPath]{QueueFileAction(action,projectPath);}))return;
    if (action == PendingFileAction::None) {
        return;
    }
    if (m_FileActionSavePending || m_PendingFileAction != PendingFileAction::None) return;
    if (!m_Editor->FinishWorkspaceInteraction()) {
        PostNotification(UiNotificationSeverity::Warning,
            "Finish the current edit before changing projects.", "file-workspace-interaction-busy");
        return;
    }

    const EditorModule::ProjectFileCommandContext context =
        m_Editor->GetProjectFileCommandContext();
    const bool canJoinSave = context.busy && context.canSave;
    if ((action == PendingFileAction::OpenProject && !context.canOpen && !canJoinSave) ||
        (action == PendingFileAction::NewEditorProject &&
         !context.canCreateEditorProject && !canJoinSave) ||
        (action == PendingFileAction::CloseCurrent && !context.canClose &&
         !m_Editor->HasUnsavedBracketingDraft() && !canJoinSave) ||
        (action == PendingFileAction::CloseWorkspace && context.busy && !canJoinSave)) {
        PostNotification(
            UiNotificationSeverity::Warning,
            context.busyReason.empty()
                ? "That project action is unavailable right now."
                : context.busyReason,
            "file-menu-project-action-unavailable");
        return;
    }

    if (action == PendingFileAction::OpenProject) {
        std::error_code pathError;
        const bool isDirectory = std::filesystem::is_directory(
            projectPath,
            pathError);
        std::string extension = projectPath.extension().string();
        std::transform(
            extension.begin(),
            extension.end(),
            extension.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        const bool supported = !pathError &&
            std::filesystem::exists(projectPath, pathError) &&
            !pathError &&
            ((isDirectory && Stack::Project::IsDirectoryProjectBundle(projectPath)) ||
             (!isDirectory &&
               (Stack::Project::IsPortableV3Project(projectPath) ||
                extension == ".stack" ||
                (projectPath.filename() == "project.stack" &&
                Stack::Project::IsDirectoryProjectBundle(
                    projectPath.parent_path())))));
        if (!supported) {
            PostNotification(
                UiNotificationSeverity::Error,
                "Choose a Stack project document, working project folder, or packed .stack file.",
                "file-menu-open-invalid-project");
            return;
        }
    }

    // Gallery owns browsing state for the lifetime of the application.
    // File commands that create a document need their own project owner too.
    if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId) {
        if (action == PendingFileAction::CloseCurrent ||
            action == PendingFileAction::CloseWorkspace) return;
        if (!ActivateProjectWorkspace(CreateProjectWorkspace())) return;
    }
    m_PendingFileAction = action;
    m_PendingFileProjectPath = std::move(projectPath);
    m_FileActionSaveFailed = false;
    if (m_Editor->NeedsWorkspaceSaveBeforeTransition() || canJoinSave) {
        m_FileActionSavePending = true;
        auto* editor = m_Editor;
        const auto scope = editor->GetNotifier();
        const auto workspaceId = m_ActiveProjectWorkspace;
        editor->RequestSaveWorkspaceBeforeClose([this, editor, scope, workspaceId, action, projectPath = m_PendingFileProjectPath](bool success) {
            m_FileActionSavePending = false;
            if (!scope.Valid()) { ClearPendingFileAction(); return; }
            if (!success) {
                ClearPendingFileAction();
                ReportSaveFailure(*editor, "The save failed. The requested project action did not run.",
                    [this, editor, scope, workspaceId, action, projectPath] {
                        if (!scope.Valid()) return;
                        m_PendingFileAction = action; m_PendingFileProjectPath = projectPath;
                        if (!ExecutePendingFileAction(false,editor,workspaceId)) {
                            ClearPendingFileAction(); scope.Error("The project action could not finish. Your work is still open.");
                        }
                    });
            } else if (!ExecutePendingFileAction(false,editor,workspaceId)) {
                ClearPendingFileAction(); scope.Error("The project action could not finish. Your work is still open.");
            }
        });
        return;
    }
    if (!ExecutePendingFileAction(false)) {
        ClearPendingFileAction();
        PostNotification(UiNotificationSeverity::Warning,
            "The project action could not finish. Your workspace is still open.", "file-workspace-transition-failed");
    }
}

bool AppShell::ExecutePendingFileAction(bool discardCurrent, EditorModule* owner, std::uint64_t workspaceId) {
    if (!owner) owner = m_Editor;
    if (!workspaceId) workspaceId = m_ActiveProjectWorkspace;
    const PendingFileAction action = m_PendingFileAction;
    bool success = false;
    switch (action) {
        case PendingFileAction::NewEditorProject:
            if (owner->GetProjectSessionKind() ==
                EditorModule::ProjectSessionKind::Empty) {
                success = true;
            } else {
                success = owner->CloseCurrentProject(discardCurrent);
            }
            if (success) {
                if (owner == m_Editor) RequestTabSwitch(RootTabEditor);
                else if (auto* workspace = FindProjectWorkspace(workspaceId)) workspace->rootTab = RootTabEditor;
            }
            break;
        case PendingFileAction::OpenProject:
            success = owner->RequestOpenProjectFromPath(
                m_PendingFileProjectPath,
                true);
            break;
        case PendingFileAction::CloseCurrent:
            success = owner->CloseCurrentProject(discardCurrent);
            if (success && workspaceId != m_GalleryWorkspaceId) m_PendingWorkspaceRetirement = workspaceId;
            break;
        case PendingFileAction::CloseWorkspace:
            if (owner->HasUnsavedBracketingDraft() && !discardCurrent) return false;
            success = owner->GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty ||
                owner->CloseCurrentProject(discardCurrent);
            if (success && workspaceId != m_GalleryWorkspaceId) m_PendingWorkspaceRetirement = workspaceId;
            break;
        case PendingFileAction::None:
            break;
    }

    if (success) {
        ClearPendingFileAction();
    }
    return success;
}

void AppShell::CancelWorkForMainWindowClose() {
    for (auto& workspace : m_ProjectWorkspaces) workspace->loadTransition.Reset();
    for (auto& workspace : m_RetiredProjectWorkspaces) workspace->loadTransition.Reset();
    Stack::RawGalleryInspection::Request cancelInspection;
    cancelInspection.cancel = true;
    m_QueueRenderer.RequestInspection(std::move(cancelInspection), {});
    for (auto& workspace : m_ProjectWorkspaces)
        if (workspace->editor->IsAutoBracketWorkspace()) workspace->editor->CancelAutoBracketingForWorkspaceClose(false);
    ReleaseLockedScrubCursor(false);
    ResetBackgroundImageDecodeState();
    for (auto& workspace : m_ProjectWorkspaces) workspace->editor->RequestWorkerShutdownForAppClose();
    for (auto& workspace : m_RetiredProjectWorkspaces) workspace->editor->RequestWorkerShutdownForAppClose();
    m_SettingsPopupOpen = false;
    m_SettingsPopupOpenedAt = 0.0;
    m_ShowEditorSavePrompt = false;
    m_ShowEditorNamePrompt = false;
    m_NotificationPresenter = {};
    m_DetachedPreviewOpeningTopMostHeld = false;
    m_DetachedPreviewOpeningWindow = nullptr;
    m_DetachedPreviewOpeningReleaseAttempts = 0;
    for (auto& workspace : m_ProjectWorkspaces) workspace->editor->CloseDetachedPreviewFullscreen();
    LibraryManager::Get().CancelProjectPreviewRequests();
    LibraryManager::Get().CancelAssetPreviewRequests();
    LibraryManager::Get().CancelLibraryRefreshRequests();
    Async::TaskSystem::Get().RequestStopDiscardQueued();
}
