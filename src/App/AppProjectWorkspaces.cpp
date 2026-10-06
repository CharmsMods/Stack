#include "AppShell.h"
#include "Async/TaskSystem.h"
#include "Project/ProjectPath.h"
#include "Library/LibraryManager.h"
#include "WorkspaceInputScope.h"
#include "imgui_internal.h"

#include <algorithm>
#include <utility>

void AppShell::InitializeProjectWorkspaces() {
    auto workspace = std::make_unique<ProjectWorkspace>(m_NextProjectWorkspace++);
    workspace->rootTab = static_cast<int>(Stack::Workspace::Id::Raw);
    BindWorkspaceNotifications(*workspace);
    workspace->editor->Initialize(m_Window, m_Appearance.get());
    workspace->editor->SetPermanentGalleryWorkspace(true);
    workspace->editor->EnterRawWorkspaceRootTab();
    m_SharedRawWorkspaceRoot = workspace->editor->GetRawWorkspaceState().workspaceRoot;
    m_ActiveProjectWorkspace = workspace->id;
    m_GalleryWorkspaceId = workspace->id;
    m_CurrentTabId = workspace->rootTab;
    m_RequestedTab = -1;
    m_Editor = workspace->editor.get();
    ConfigureWorkspaceEditor(*m_Editor);
    m_ProjectWorkspaces.push_back(std::move(workspace));
}

ProjectWorkspace* AppShell::FindProjectWorkspace(std::uint64_t id) const {
    for (const auto& workspace : m_ProjectWorkspaces)
        if (workspace->id == id) return workspace.get();
    return nullptr;
}

AppShell::LibraryToEditorProjectLoadPhase AppShell::ActiveProjectLoadPhase() const {
    const auto* workspace = FindProjectWorkspace(m_ActiveProjectWorkspace);
    return workspace ? workspace->loadTransition.phase : LibraryToEditorProjectLoadPhase::None;
}

std::uint64_t AppShell::CreateProjectWorkspace() {
    auto workspace = std::make_unique<ProjectWorkspace>(m_NextProjectWorkspace++);
    BindWorkspaceNotifications(*workspace);
    workspace->editor->Initialize(m_Window, m_Appearance.get(), false);
    workspace->editor->SetPermanentGalleryWorkspace(false);
    workspace->editor->EnterRawWorkspaceRootTab();
    ConfigureWorkspaceEditor(*workspace->editor);
    workspace->editor->SetRawWorkspaceFolder(m_SharedRawWorkspaceRoot);
    const auto id = workspace->id;
    m_ProjectWorkspaces.push_back(std::move(workspace));
    return id;
}

void AppShell::ConfigureWorkspaceEditor(EditorModule& editor) {
    auto* owner = &editor;
    editor.SetProjectRemovalGuard([this](const std::filesystem::path& path) {
        const auto usesPath = [&](const auto& workspace) {
            return Stack::Project::SameProjectPath(path,
                Stack::Project::ResolveProjectStoreRoot(workspace->editor->GetCurrentProjectFileName()));
        };
        return std::any_of(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(), usesPath) ||
            std::any_of(m_RetiredProjectWorkspaces.begin(), m_RetiredProjectWorkspaces.end(), usesPath);
    });
    editor.SetGalleryNavigationHandler([this, owner] {
        if (owner == m_Editor)
            QueueWorkspaceTabAction(WorkspaceTabAction::Select, m_GalleryWorkspaceId);
    });
    editor.SetRawWorkspaceFolderChangedHandler([this](const std::filesystem::path& root) {
        m_SharedRawWorkspaceRoot = root;
    });
    editor.SetRawGalleryQueueRequestHandler(
        [this](const Stack::RawGalleryQueue::Request& request) {
            return m_Queue.Model().AddGalleryRequest(request);
        });
    editor.SetRawGalleryOpenSelectionHandler(
        [this, owner](EditorModule::RawGalleryOpenSelection selection,
                      bool bracket) {
            if (owner != m_Editor || !m_GalleryTabOpenQueue.empty() ||
                m_PendingGalleryTabOpen) return;
            if (bracket) {
                if (selection.bracketSources.size() < 2u) return;
                GalleryTabOpen target;
                target.kind = GalleryTabOpen::Kind::Bracket;
                target.bracketSources = std::move(selection.bracketSources);
                m_GalleryTabOpenQueue.push_back(std::move(target));
            } else {
                for (const auto& item : selection.items) {
                    GalleryTabOpen target;
                    target.kind = item.project
                        ? GalleryTabOpen::Kind::Project
                        : GalleryTabOpen::Kind::Source;
                    target.path = item.path;
                    m_GalleryTabOpenQueue.push_back(std::move(target));
                }
            }
        });
    // Capture the owning editor, not the active tab pointer. An inspection
    // can finish after the user switches to a different project.
    editor.SetRawGalleryInspectionRequestHandler(
        [this, owner](const Stack::RawGalleryInspection::Request& request) {
            const auto inspect = [this, owner, request] {
                // A foreground request may have waited for bracketing. A
                // hidden tab must not replace or cancel the current tab's
                // inspection in the shared Queue renderer.
                if (owner != m_Editor && !request.cancel) return;
                std::string error;
                if (m_QueueRenderer.RequestInspection(request,
                        [owner, lease = owner->ProjectTasks().Retain()](Stack::RawGalleryInspection::Result result) {
                            owner->CompleteRawGalleryInspection(std::move(result));
                        }, &error, owner->GetNotifier())) return;
                Stack::RawGalleryInspection::Result result;
                result.requestId = request.requestId;
                result.error = error.empty()
                    ? "The Gallery inspection could not be started." : std::move(error);
                owner->CompleteRawGalleryInspection(std::move(result));
            };
            if (request.cancel || !owner->RequestAutoBracketForeground("inspect this image", inspect))
                inspect();
        });
}

bool AppShell::CanSwitchProjectWorkspace() const {
    return m_Editor && !m_NotificationPresenter.BlocksInput() && !m_CloseRequested && !m_PendingWorkspaceRetirement &&
        !m_MainWindowCloseSavePending && !m_FileActionSavePending &&
        m_ContinueMainWindowCloseSource.empty() &&
        !m_ProjectLoadSavePending && !m_RawWorkspaceSwitchSavePending &&
        m_PendingFileAction == PendingFileAction::None &&
        m_PendingFileMenuSaveAsDestination.empty() &&
        !m_ShowEditorSavePrompt && !m_ShowEditorNamePrompt &&
        !m_ShowRawWorkspaceSwitchPrompt && !m_ShowUnnamedEditorClosePrompt &&
        !m_ShowFileDispositionPrompt && !m_ShowOpenProjectPrompt &&
        !m_RootTabBodyFadeActive && !m_WorkspaceSwitcher.Visible() &&
        !m_ToolSwitcher.Visible() && !ImGui::GetTopMostPopupModal();
}

void AppShell::QueueWorkspaceTabAction(WorkspaceTabAction action, std::uint64_t id) {
    if (!CanSwitchProjectWorkspace()) return;
    if (action == WorkspaceTabAction::Close && id == m_GalleryWorkspaceId) return;
    m_WorkspaceTabAction = action;
    m_WorkspaceTabActionId = id;
}

bool AppShell::ActivateProjectWorkspace(std::uint64_t id, bool preserveNavigation) {
    if (id == m_ActiveProjectWorkspace) {
        if (id == m_GalleryWorkspaceId) {
            m_Editor->OpenRawWorkspaceGalleryWorkspace();
            RequestTabSwitch(static_cast<int>(Stack::Workspace::Id::Raw));
        }
        return true;
    }
    const auto next = std::find_if(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(),
        [id](const auto& workspace) { return workspace->id == id; });
    if (next == m_ProjectWorkspaces.end() || !m_Editor->FinishWorkspaceInteraction()) return false;
    if (m_ActiveProjectWorkspace != m_GalleryWorkspaceId)
        m_NavigationRail.lastEditingWorkspace = m_ActiveProjectWorkspace;
    if (m_ToolSwitcher.Open() || m_ToolSwitcher.cursorCaptured) CancelToolSwitcher();
    CancelProjectPillPreview();
    for (auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_ActiveProjectWorkspace) workspace->rootTab = m_CurrentTabId;
    }
    m_Editor->EndWorkspacePreview();
    m_Editor->SetWorkspaceDetachedWindowsVisible(false);
    m_Editor->SetLibraryWindowHovered(false);
    ReleaseLockedScrubCursor(false);
    Stack::Workspace::CloseOrdinaryPopups();
    ImGui::ClearActiveID();
    m_Library.DismissPreviewsForProjectLoad();
    m_SettingsPopupOpen = false;
    m_SettingsPopupOpenedAt = 0;
    m_LibraryWindowOpen = false;
    m_ActiveSyncLayerId.clear();
    m_Editor = (*next)->editor.get();
    m_ActiveProjectWorkspace = id;
    m_CurrentTabId = (*next)->rootTab;
    if (id == m_GalleryWorkspaceId) {
        (*next)->rawCatalogRefreshPending = true;
        m_CurrentTabId = static_cast<int>(Stack::Workspace::Id::Raw);
        m_Editor->EnterRawWorkspaceRootTab();
        m_Editor->OpenRawWorkspaceGalleryWorkspace();
    }
    m_Editor->SetWorkspaceDetachedWindowsVisible(true);
    if (!preserveNavigation) {
        m_RequestedTab = -1;
        m_RootTabBodyFadeActive = false;
        m_RootTabBodyFadeQueuedTab = -1;
        m_WorkspaceFinishPending = false;
        m_WorkspaceSwitcher.progress = 0;
        CancelToolSwitcher();
        const bool altWasDown = m_ToolSwitcher.wasDown;
        m_ToolSwitcher = {};
        m_ToolSwitcher.wasDown = altWasDown;
        m_ToolSwitcher.suppressed = altWasDown;
    }
    m_ScrollActiveProjectTabIntoView = true;
    UpdateWorkspacePreferencesOwner();
    return true;
}

void AppShell::TickProjectWorkspaces() {
    SynchronizeRawWorkspaceFolder();
    const auto openPendingGalleryTab = [this]() {
        if (!m_PendingGalleryTabOpen) return;
        auto* workspace = FindProjectWorkspace(m_PendingGalleryTabOpen->workspaceId);
        if (!workspace) { m_PendingGalleryTabOpen.reset(); return; }
        auto* editor = workspace->editor.get();
        const GalleryTabOpen& target = *m_PendingGalleryTabOpen;
        if (target.kind == GalleryTabOpen::Kind::Bracket) {
            editor->BeginBracketingDraft(true);
            editor->AddBracketingDraftFiles(target.bracketSources);
            editor->OpenBracketingTool();
            editor->RequestOpenRawLabTab();
        } else if (target.kind == GalleryTabOpen::Kind::Project) {
            // The queue releases this file before its editor reads a snapshot.
            // Other bracket jobs and tabs continue while this one cancels.
            const auto writing = [&](const auto& other) {
                return other->editor->IsAutoBracketProjectBusy(target.path);
            };
            if (std::any_of(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(), writing) ||
                std::any_of(m_RetiredProjectWorkspaces.begin(), m_RetiredProjectWorkspaces.end(), writing)) return;
            if (!editor->RequestOpenRawWorkspaceProjectFromGallery(target.path))
                PostUiNotification(editor->GetNotifier(), UiNotificationSeverity::Error,
                    "Could not open " + target.path.filename().string() + ".",
                    "gallery-project-open");
        } else {
            const auto& sources = editor->GetRawWorkspaceState().sources;
            const auto found = std::find_if(sources.begin(), sources.end(),
                [&](const auto& source) {
                    return source.absolutePath.lexically_normal() ==
                        target.path.lexically_normal();
                });
            if (found == sources.end() && editor->IsRawWorkspaceScanBusy())
                return;
            if (found == sources.end()) {
                PostUiNotification(editor->GetNotifier(), UiNotificationSeverity::Error,
                    "Could not find " + target.path.filename().string() +
                        " in the Gallery folder.",
                    "gallery-source-open");
            } else {
                editor->SelectRawWorkspaceSourceForGallery(
                    found->relativePathKey, false, false, true);
                editor->RequestOpenRawLabTab();
            }
        }
        m_PendingGalleryTabOpen.reset();
    };
    openPendingGalleryTab();
    if (!m_PendingGalleryTabOpen && !m_GalleryTabOpenQueue.empty() &&
        m_WorkspaceTabAction == WorkspaceTabAction::None &&
        CanSwitchProjectWorkspace()) {
        m_WorkspaceTabAction = WorkspaceTabAction::Create;
    }
    // Mutate the tab collection only between frames of workspace UI. No
    // button callback can delete an editor while its RenderUI is on the stack.
    if (m_PendingWorkspaceRetirement) FinishProjectWorkspaceRetirement();
    if (m_WorkspaceTabAction != WorkspaceTabAction::None && CanSwitchProjectWorkspace()) {
        const auto action = std::exchange(m_WorkspaceTabAction, WorkspaceTabAction::None);
        const auto id = std::exchange(m_WorkspaceTabActionId, 0);
        if (action == WorkspaceTabAction::Close && CloseAutoBracketWorkspace(id)) return;
        if (action == WorkspaceTabAction::Close && id != m_ActiveProjectWorkspace) {
            const auto closing = std::find_if(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(),
                [id](const auto& workspace) { return workspace->id == id; });
            if (closing != m_ProjectWorkspaces.end()) {
                auto& editor = *(*closing)->editor;
                if (!editor.GetProjectFileCommandContext().busy &&
                    !editor.NeedsWorkspaceSaveBeforeTransition() &&
                    !editor.HasUnsavedBracketingDraft() &&
                    (editor.GetProjectSessionKind() == EditorModule::ProjectSessionKind::Empty ||
                     editor.CloseCurrentProject(false))) {
                    m_PendingWorkspaceRetirement = id;
                    return;
                }
            }
        }
        const bool leavingCurrent = action == WorkspaceTabAction::Create || id != m_ActiveProjectWorkspace;
        if (leavingCurrent && !m_Editor->FinishWorkspaceInteraction()) return;
        if (action == WorkspaceTabAction::Create && !m_GalleryTabOpenQueue.empty()) {
            if (m_Editor->FinishWorkspaceInteraction()) {
                const auto createdId = CreateProjectWorkspace();
                if (ActivateProjectWorkspace(createdId)) {
                    m_PendingGalleryTabOpen =
                        std::move(m_GalleryTabOpenQueue.front());
                    m_GalleryTabOpenQueue.pop_front();
                    m_PendingGalleryTabOpen->workspaceId = createdId;
                    openPendingGalleryTab();
                }
            }
        } else if (action == WorkspaceTabAction::Create) {
            ActivateProjectWorkspace(CreateProjectWorkspace());
        } else if (ActivateProjectWorkspace(id) && action == WorkspaceTabAction::Close) {
            QueueFileAction(PendingFileAction::CloseWorkspace);
        }
    }
    if (m_PendingWorkspaceRetirement) FinishProjectWorkspaceRetirement();
    for (auto& workspace : m_ProjectWorkspaces) {
        const bool foreground = workspace->id == m_ActiveProjectWorkspace;
        workspace->editor->PumpNonRenderingWork(foreground ? 2.5 : 0.5, foreground);
        if (!foreground) {
            // Allow already-running work to finish without starting another
            // automatic batch in every inactive tab.
            workspace->editor->TickBracketing(false);
            workspace->editor->UpdateBracketingPresentation(false);
        }
    }
    TickAutoBracketWorkspaces();
    // A retired editor waits only for its own tasks, external callbacks and
    // render clients. Another project's work cannot keep it alive indefinitely.
    {
        m_RetiredProjectWorkspaces.erase(std::remove_if(
            m_RetiredProjectWorkspaces.begin(), m_RetiredProjectWorkspaces.end(), [&](auto& workspace) {
            if (workspace->editor->AutoBracketWorkActive()) return false;
            if (!workspace->editor->IsWorkerShutdownReadyForAppClose()) return false;
            workspace->editor->Shutdown();
            return true;
        }), m_RetiredProjectWorkspaces.end());
    }
    if (!m_ContinueMainWindowCloseSource.empty() && !ImGui::GetTopMostPopupModal()) {
        const auto source = std::exchange(m_ContinueMainWindowCloseSource, {});
        RequestMainWindowClose(source.c_str());
    }
}

void AppShell::RetireCurrentProjectWorkspace() {
    if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId) return;
    m_PendingWorkspaceRetirement = m_ActiveProjectWorkspace;
}

void AppShell::FinishProjectWorkspaceRetirement() {
    const auto retiringId = m_PendingWorkspaceRetirement;
    const auto current = std::find_if(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(),
        [retiringId](const auto& workspace) { return workspace->id == retiringId; });
    if (current == m_ProjectWorkspaces.end() || retiringId == m_GalleryWorkspaceId) {
        m_PendingWorkspaceRetirement = 0;
        return;
    }
    // Do not remove the active editor until it can safely relinquish input.
    if ((*current)->editor->IsWorkspaceTransitionPending() || !(*current)->editor->FinishWorkspaceInteraction()) return;
    m_PendingWorkspaceRetirement = 0;
    const auto index = static_cast<std::size_t>(current - m_ProjectWorkspaces.begin());
    const bool wasActive = retiringId == m_ActiveProjectWorkspace;
    m_WorkspaceCompositor.ForgetProjectPreview(retiringId);
    if (m_NavigationRail.previewProject == retiringId) CancelProjectPillPreview();
    auto retiring = std::move(*current);
    m_NotificationStore->InvalidateOwner(retiring->id);
    auto* retiringEditor = retiring->editor.get();
    retiring->loadTransition.Reset();
    retiringEditor->SetWorkspaceAppStatePersistenceEnabled(false);
    retiringEditor->SetRawWorkspaceFolderChangedHandler({});
    m_ProjectWorkspaces.erase(current);
    retiring->editor->SetWorkspaceDetachedWindowsVisible(false);
    if (!retiring->editor->IsAutoBracketWorkspace()) retiring->editor->SetDocumentPersistenceEnabled(false);
    m_RetiredProjectWorkspaces.push_back(std::move(retiring));
    if (wasActive) ActivateProjectWorkspace(m_ProjectWorkspaces[std::min(index, m_ProjectWorkspaces.size() - 1)]->id);
    UpdateWorkspacePreferencesOwner();
    retiringEditor->RequestWorkerShutdownForAppClose();
}

void AppShell::ContinueMainWindowClose(const std::string& source) {
    m_CloseApprovedWorkspaces.push_back(m_ActiveProjectWorkspace);
    m_ContinueMainWindowCloseSource = source;
    Stack::Workspace::CloseOrdinaryPopups();
}

bool AppShell::AllWorkspaceWorkersReadyForClose() const {
    const auto ready = [](const auto& workspace) {
        return workspace->editor->IsWorkerShutdownReadyForAppClose();
    };
    return std::all_of(m_ProjectWorkspaces.begin(), m_ProjectWorkspaces.end(), ready) &&
        std::all_of(m_RetiredProjectWorkspaces.begin(), m_RetiredProjectWorkspaces.end(), ready);
}
