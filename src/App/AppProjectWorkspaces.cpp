#include "AppShell.h"
#include "Async/TaskSystem.h"
#include "Project/ProjectPath.h"
#include "Library/LibraryManager.h"
#include "WorkspaceInputScope.h"
#include "imgui_internal.h"
#include "App/UiFrameBudget.h"

#include <algorithm>
#include <utility>

void AppShell::InitializeProjectWorkspaces() {
    auto workspace = std::make_unique<ProjectWorkspace>(m_NextProjectWorkspace++);
    workspace->rootTab = static_cast<int>(Stack::Workspace::Id::Raw);
    BindWorkspaceNotifications(*workspace);
    workspace->editor->SetPermanentGalleryWorkspace(true);
    workspace->editor->Initialize(m_Window, m_Appearance.get());
    workspace->editor->EnterRawWorkspaceRootTab();
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
    // An unscoped new document has no arbitrary first-folder owner in All photos.
    if (m_ActiveProjectWorkspace == m_GalleryWorkspaceId)
        return CreateProjectWorkspace(m_Editor && !m_Editor->IsRawWorkspaceAllFolders() &&
            !m_Editor->IsRawWorkspaceFolderBrowserOpen()
            ? m_Editor->GetRawWorkspaceState().workspaceRoot : std::filesystem::path{});
    const auto* owner = FindProjectWorkspace(m_ActiveProjectWorkspace);
    return CreateProjectWorkspace(owner ? owner->sourceWorkspaceRoot : std::filesystem::path{});
}

std::uint64_t AppShell::CreateProjectWorkspace(const std::filesystem::path& sourceWorkspaceRoot) {
    auto workspace = std::make_unique<ProjectWorkspace>(m_NextProjectWorkspace++);
    workspace->sourceWorkspaceRoot = sourceWorkspaceRoot;
    BindWorkspaceNotifications(*workspace);
    workspace->editor->Initialize(m_Window, m_Appearance.get(), false, true);
    workspace->editor->SetPermanentGalleryWorkspace(false);
    workspace->editor->EnterRawWorkspaceRootTab();
    ConfigureWorkspaceEditor(*workspace->editor);
    workspace->editor->SetRawWorkspaceFolder(sourceWorkspaceRoot);
    const auto id = workspace->id;
    m_ProjectWorkspaces.push_back(std::move(workspace));
    return id;
}

void AppShell::ConfigureWorkspaceEditor(EditorModule& editor) {
    if (m_DiagnosticQueueCurrent) editor.SetDocumentPersistenceEnabled(false);
    auto* owner = &editor;
    const auto workspace=std::find_if(m_ProjectWorkspaces.begin(),m_ProjectWorkspaces.end(),
        [&](const auto& item){return item->editor.get()==owner;});
    // During construction the new workspace has not entered the collection yet.
    editor.SetGalleryNavigationTransition(m_GalleryNavigationMotion,
        workspace==m_ProjectWorkspaces.end()?m_NextProjectWorkspace-1:(*workspace)->id);
    // New tabs inherit current presentation defaults, not a stale disk copy.
    // This value-only snapshot contains no browse scope or project state.
    if (const auto* gallery = FindProjectWorkspace(m_GalleryWorkspaceId);
        gallery && gallery->editor.get() != owner)
        editor.ApplyRawWorkspacePresentationPreferences(
            gallery->editor->GetRawWorkspacePresentationPreferences());
    editor.SetRawWorkspacePresentationPreferencesChangedHandler(
        [this, owner](const EditorModule::RawWorkspacePresentationPreferences& preferences) {
            if (m_UiPerformanceCapture || owner != m_Editor || owner->IsAutoBracketWorkspace()) return;
            const auto* gallery = FindProjectWorkspace(m_GalleryWorkspaceId);
            if (!gallery || gallery->editor.get() == owner) return;
            // Gallery remains the sole writer. Hidden editors cannot replace
            // global defaults or leak their source-folder context into them.
            gallery->editor->ApplyRawWorkspacePresentationPreferences(preferences, true);
        });
    editor.SetProjectVersionHandler([this](const std::filesystem::path& path) {
        for (const auto& workspace : m_ProjectWorkspaces) {
            if (Stack::Project::SameProjectPath(path,
                    Stack::Project::ResolveProjectStoreRoot(workspace->editor->GetCurrentProjectFileName()))) {
                workspace->editor->RequestCurrentProjectVersion();
                return true;
            }
        }
        return false;
    });
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
    editor.SetRawGalleryQueueRequestHandler(
        [this](const Stack::RawGalleryQueue::Request& request) {
            return m_Queue.Model().AddGalleryRequest(request);
        });
    editor.SetProjectQueueRequestHandler(
        [this](const Stack::Queue::ProjectRequest& request) {
            return m_Queue.Model().AddProjectSnapshot(request);
        });
    editor.SetRawGalleryOpenSelectionHandler(
        [this, owner](EditorModule::RawGalleryOpenSelection selection,
                      bool bracket) {
            if (owner != m_Editor) return;
            const auto intent=m_GalleryNavigationMotion->NewIntent();
            if(m_PendingGalleryTabOpen) {
                m_BackgroundGalleryTabOpens.push_back(std::move(*m_PendingGalleryTabOpen));
                m_PendingGalleryTabOpen.reset();
            }
            if (bracket) {
                if (selection.bracketSources.size() < 2u) return;
                GalleryTabOpen target;
                target.kind = GalleryTabOpen::Kind::Bracket;
                target.navigationGeneration=intent;
                // Brackets spanning folders use the first selected input's owner.
                // Resolve now: Gallery may navigate while this open is queued.
                target.sourceWorkspaceRoot = owner->ResolveRawWorkspaceSourceFolder(selection.bracketSources.front());
                target.bracketSources = std::move(selection.bracketSources);
                m_GalleryTabOpenQueue.push_back(std::move(target));
            } else {
                for (const auto& item : selection.items) {
                    GalleryTabOpen target;
                    target.kind = item.project
                        ? GalleryTabOpen::Kind::Project
                        : GalleryTabOpen::Kind::Source;
                    target.path = item.path;
                    target.transitionOrigin=item.transitionOrigin.Valid()?item.transitionOrigin:
                        owner->CaptureGalleryTransitionOrigin(item.path);
                    target.navigationGeneration=intent;
                    target.sourceWorkspaceRoot = item.sourceWorkspaceRoot.empty()
                        ? owner->ResolveRawWorkspaceSourceFolder(item.path)
                        : item.sourceWorkspaceRoot;
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
    m_GalleryNavigationMotion->NewIntent();
    if (action == WorkspaceTabAction::Close && id == m_GalleryWorkspaceId) return;
    // Explicit pill navigation supersedes a root destination waiting on a load,
    // including selecting the already-active project.
    m_NavigationRail.requestedDestination = -1;
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
    std::filesystem::path gallerySourceRoot;
    if (id == m_GalleryWorkspaceId) {
        if (const auto* outgoing = FindProjectWorkspace(m_ActiveProjectWorkspace))
            gallerySourceRoot = outgoing->sourceWorkspaceRoot;
        else {
            // Closing the active pill retires it before selecting its neighbor.
            for (const auto& outgoing : m_RetiredProjectWorkspaces)
                if (outgoing->id == m_ActiveProjectWorkspace) {
                    gallerySourceRoot = outgoing->sourceWorkspaceRoot;
                    break;
                }
        }
    }
    if(id==m_GalleryWorkspaceId && m_GalleryNavigationMotion->Owns(m_ActiveProjectWorkspace)) {
        auto* gallery=FindProjectWorkspace(m_GalleryWorkspaceId);
        Stack::Renderer::GalleryTransitionOrigin destination;
        if(const auto* outgoing=FindProjectWorkspace(m_ActiveProjectWorkspace);gallery && outgoing)
            destination=gallery->editor->CaptureGalleryTransitionOrigin(std::filesystem::u8path(outgoing->galleryOrigin.subject));
        m_GalleryNavigationMotion->ReturnImage(std::move(destination));
    } else if(m_ActiveProjectWorkspace==m_GalleryWorkspaceId && id!=m_GalleryWorkspaceId &&
        !(*next)->galleryOrigin.subject.empty()) {
        auto origin=m_Editor->CaptureGalleryTransitionOrigin(std::filesystem::u8path((*next)->galleryOrigin.subject));
        if(origin.Valid()) {
            const auto& rail=m_NavigationRail;
            m_GalleryNavigationMotion->BeginImage(std::move(origin),id,
                {rail.bodyPosition,{rail.bodyPosition.x+rail.bodySize.x,rail.bodyPosition.y+rail.bodySize.y}});
        } else m_GalleryNavigationMotion->CancelImage();
    } else if(id!=m_GalleryWorkspaceId && !m_GalleryNavigationMotion->Owns(id) &&
        m_GalleryNavigationMotion->HasImage())m_GalleryNavigationMotion->CancelImage();
    m_NavigationRail.requestedDestination = -1;
    if (m_ActiveProjectWorkspace != m_GalleryWorkspaceId)
        m_NavigationRail.lastEditingWorkspace = m_ActiveProjectWorkspace;
    if (m_ToolSwitcher.Open() || m_ToolSwitcher.cursorCaptured) CancelToolSwitcher();
    CancelProjectPillPreview();
    for (auto& workspace : m_ProjectWorkspaces) {
        if (workspace->id == m_ActiveProjectWorkspace) {
            // App destinations must not become a project's editing return point.
            const bool appSurface = m_CurrentTabId == static_cast<int>(Stack::Workspace::Id::Queue) ||
                m_CurrentTabId == static_cast<int>(Stack::Workspace::Id::Library);
            workspace->rootTab = appSurface && workspace->id != m_GalleryWorkspaceId &&
                !workspace->editor->IsAutoBracketWorkspace()
                ? static_cast<int>(Stack::Workspace::Id::Graph) : m_CurrentTabId;
        }
    }
    if(m_PendingGalleryTabOpen && m_PendingGalleryTabOpen->workspaceId==m_ActiveProjectWorkspace) {
        m_BackgroundGalleryTabOpens.push_back(std::move(*m_PendingGalleryTabOpen));
        m_PendingGalleryTabOpen.reset();
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
    if (id != m_GalleryWorkspaceId && !m_Editor->IsViewMode())
        m_Editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph);
    if (id == m_GalleryWorkspaceId) {
        // Centralize this for rail, direct, notification and retirement switches.
        // Only Gallery navigates; the outgoing editor keeps its document and jobs.
        if (!gallerySourceRoot.empty()) m_Editor->SetRawWorkspaceFolder(gallerySourceRoot);
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
    m_GalleryNavigationMotion->Tick(ImGui::GetTime(),ImGui::GetFrameCount(),Stack::Notifications::SystemReducedMotion());
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
            if (!editor->RequestOpenRawWorkspaceProjectFromGallery(target.path)) {
                if(workspace->id==m_ActiveProjectWorkspace && m_GalleryNavigationMotion->Waiting(workspace->id))
                    QueueWorkspaceTabAction(WorkspaceTabAction::Select,m_GalleryWorkspaceId);
                PostUiNotification(editor->GetNotifier(), UiNotificationSeverity::Error,
                    "Could not open " + target.path.filename().string() + ".",
                    "gallery-project-open");
            }
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
                if(workspace->id==m_ActiveProjectWorkspace && m_GalleryNavigationMotion->Waiting(workspace->id))
                    QueueWorkspaceTabAction(WorkspaceTabAction::Select,m_GalleryWorkspaceId);
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
    if(!m_PendingGalleryTabOpen && !m_BackgroundGalleryTabOpens.empty()) {
        m_PendingGalleryTabOpen=std::move(m_BackgroundGalleryTabOpens.front());
        m_BackgroundGalleryTabOpens.pop_front();
        openPendingGalleryTab();
        if(m_PendingGalleryTabOpen) {
            m_BackgroundGalleryTabOpens.push_back(std::move(*m_PendingGalleryTabOpen));
            m_PendingGalleryTabOpen.reset();
        }
    }
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
                const auto createdId = CreateProjectWorkspace(m_GalleryTabOpenQueue.front().sourceWorkspaceRoot);
                const bool foreground=m_GalleryTabOpenQueue.front().navigationGeneration==m_GalleryNavigationMotion->Intent();
                if (!foreground || ActivateProjectWorkspace(createdId)) {
                    m_PendingGalleryTabOpen =
                        std::move(m_GalleryTabOpenQueue.front());
                    m_GalleryTabOpenQueue.pop_front();
                    m_PendingGalleryTabOpen->workspaceId = createdId;
                    if(auto* created=FindProjectWorkspace(createdId)) {
                        created->galleryOrigin=m_PendingGalleryTabOpen->transitionOrigin;
                        created->galleryOrigin.thumbnail.reset();
                    }
                    if(foreground && m_PendingGalleryTabOpen->transitionOrigin.Valid()) {
                        const auto& rail=m_NavigationRail;
                        m_GalleryNavigationMotion->BeginImage(std::move(m_PendingGalleryTabOpen->transitionOrigin),
                            createdId,{rail.bodyPosition,{rail.bodyPosition.x+rail.bodySize.x,rail.bodyPosition.y+rail.bodySize.y}});
                    }
                    openPendingGalleryTab();
                }
            }
        } else if (action == WorkspaceTabAction::Create) {
            ActivateProjectWorkspace(CreateProjectWorkspace());
        } else if (ActivateProjectWorkspace(id)) {
            if (action == WorkspaceTabAction::Close) {
                QueueFileAction(PendingFileAction::CloseWorkspace);
            } else if (action == WorkspaceTabAction::Select && id != m_GalleryWorkspaceId &&
                !m_Editor->IsAutoBracketWorkspace() && m_Editor->FinishWorkspaceInteraction()) {
                // Selecting even the already-active pill means returning to its
                // editor. Its project-owned View/Graph mode remains untouched.
                RequestTabSwitch(static_cast<int>(Stack::Workspace::Id::Graph));
            }
        }
    }
    if (m_PendingWorkspaceRetirement) FinishProjectWorkspaceRetirement();
    for (auto& workspace : m_ProjectWorkspaces) {
        const bool foreground = workspace->id == m_ActiveProjectWorkspace;
        workspace->editor->PumpNonRenderingWork(foreground ?
            (m_GalleryNavigationMotion->Waiting(workspace->id) ? Stack::App::MainThreadCompletionBudgetMs(0) : 2.5) : .5, foreground);
        if(foreground && m_GalleryNavigationMotion->Waiting(workspace->id) &&
            workspace->editor->GalleryLoadFailed())
            QueueWorkspaceTabAction(WorkspaceTabAction::Select,m_GalleryWorkspaceId);
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
    if (wasActive && !m_UiPerformanceCapture && !(*current)->editor->IsAutoBracketWorkspace()) {
        if (const auto* gallery = FindProjectWorkspace(m_GalleryWorkspaceId))
            gallery->editor->ApplyRawWorkspacePresentationPreferences(
                (*current)->editor->GetRawWorkspacePresentationPreferences(), true);
    }
    m_GalleryNavigationMotion->Retire(retiringId);
    m_WorkspaceCompositor.ForgetProjectPreview(retiringId);
    if (m_NavigationRail.previewProject == retiringId) CancelProjectPillPreview();
    auto retiring = std::move(*current);
    m_NotificationStore->InvalidateOwner(retiring->id);
    auto* retiringEditor = retiring->editor.get();
    retiring->loadTransition.Reset();
    retiringEditor->SetRawWorkspacePresentationPreferencesChangedHandler({});
    retiringEditor->SetWorkspaceAppStatePersistenceEnabled(false);
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
