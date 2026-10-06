#include "AppShell.h"
#include "AppHeaderStyle.h"
#include "Library/LibraryManager.h"
#include "Utils/FileDialogs.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <utility>

namespace {
constexpr auto AppOwner = std::numeric_limits<std::uint64_t>::max() - 1;
constexpr auto LibraryOwner = AppOwner - 1;
constexpr auto QueueOwner = AppOwner - 2;
}

void AppShell::BindWorkspaceNotifications(ProjectWorkspace& workspace) {
    workspace.editor->SetNotificationScope(m_NotificationStore->ForOwner(workspace.id, "Untitled"));
}

void AppShell::UpdateNotifications() {
    if (!m_AppNotifier.Valid()) {
        m_AppNotifier = m_NotificationStore->ForOwner(AppOwner, "Stack", Stack::Notifications::OwnerKind::Application);
        const auto library = m_NotificationStore->ForOwner(LibraryOwner, "Library", Stack::Notifications::OwnerKind::Library);
        LibraryManager::Get().SetNotificationScope(library);
        m_Library.SetNotificationScope(library);
        m_QueueRenderer.SetNotificationScope(m_NotificationStore->ForOwner(QueueOwner, "Queue", Stack::Notifications::OwnerKind::Queue));
        m_SettingsPopupState.notifier = m_AppNotifier;
    }
    for (const auto& workspace : m_ProjectWorkspaces) {
        const auto name = workspace->editor->GetCurrentProjectName();
        m_NotificationStore->SetOwnerLabel(workspace->id, workspace->id == m_GalleryWorkspaceId ? "Gallery" : name.empty() ? "Untitled" : name);
        workspace->editor->UpdateNotificationDecisions(workspace->id == m_ActiveProjectWorkspace);
    }
    for (auto it = m_SaveRecoveryOwners.begin(); it != m_SaveRecoveryOwners.end();) {
        auto* workspace = FindProjectWorkspace(it->first);
        const auto& captured = it->second;
        if (!workspace || workspace->editor->GetProjectDocumentId() != captured.document ||
            workspace->editor->GetProjectSession().files != captured.files.lock() ||
            workspace->editor->GetProjectSession().files->load.generation != captured.loadGeneration) {
            if (workspace) workspace->editor->GetNotifier().InvalidateOperation(captured.operation);
            it = m_SaveRecoveryOwners.erase(it);
        } else ++it;
    }
    m_Library.UpdateNotificationDecisions();
    AppSettingsPopup::UpdateNotifications(m_UpdateManager.get(), m_SettingsPopupState);
    const auto snapshot = CollectActivity();
    std::unordered_map<std::string, bool> live;
    const auto records = m_NotificationStore->Snapshot();
    std::vector<Stack::UiActivity::Entry> grouped;
    for (const auto& entry : snapshot.entries) {
        const auto existing = std::find_if(grouped.begin(),grouped.end(),[&](const auto& group) {
            return entry.operationId && group.ownerId == entry.ownerId && group.operationId == entry.operationId;
        });
        if (existing == grouped.end()) grouped.push_back(entry);
        else {
            if (existing->label != entry.label) existing->details.push_back(entry.label);
            for (const auto& detail : entry.details)
                if (std::find(existing->details.begin(),existing->details.end(),detail) == existing->details.end()) existing->details.push_back(detail);
            if (entry.progress) existing->progress = entry.progress;
        }
    }
    for (const auto& entry : grouped) {
        const std::string key = std::to_string(entry.ownerId) + ":" +
            (entry.operationId ? "operation:" + std::to_string(entry.operationId) : "collector:" + entry.key);
        // Owned activities report their own result. Collector records are only
        // a view of execution and are removed without inventing completion.
        if (std::any_of(records.begin(), records.end(), [&](const auto& record) {
            return record.owner.id == entry.ownerId && record.state == Stack::Notifications::RecordState::Running &&
                (entry.operationId ? record.operationId == entry.operationId : record.content.title == entry.label) &&
                record.content.dedupeKey != "collected-activity";
        })) continue;
        live[key] = true;
        auto found = m_LiveActivities.find(key);
        if (found == m_LiveActivities.end()) {
            const auto notifier = entry.ownerId == m_AppNotifier.GetOwner().id ? m_AppNotifier :
                entry.ownerId == LibraryManager::Get().GetNotifier().GetOwner().id ? LibraryManager::Get().GetNotifier() :
                entry.ownerId == m_QueueRenderer.GetNotifier().GetOwner().id ? m_QueueRenderer.GetNotifier() :
                FindProjectWorkspace(entry.ownerId) ? FindProjectWorkspace(entry.ownerId)->editor->GetNotifier() : Stack::Notifications::Notifier{};
            if (!notifier.Valid()) continue;
            Stack::Notifications::NoticeSpec spec;
            spec.title = entry.label; spec.operationId = entry.operationId;
            spec.dedupeKey = "collected-activity"; spec.preview = false; spec.maintenance = entry.maintenance;
            found = m_LiveActivities.emplace(key, LiveActivity{notifier, notifier.BeginActivity(std::move(spec))}).first;
        }
        std::string details;
        for (const auto& detail : entry.details) { if (!details.empty()) details += "\n"; details += detail; }
        found->second.notifier.UpdateActivity(found->second.handle, entry.label, entry.progress, std::move(details));
    }
    for (auto it = m_LiveActivities.begin(); it != m_LiveActivities.end();) {
        if (live.find(it->first) == live.end()) {
            it->second.notifier.ForgetActivity(it->second.handle);
            it = m_LiveActivities.erase(it);
        } else ++it;
    }
    m_NotificationPresenter.BeginFrame(*m_NotificationStore);
}

Stack::Notifications::PresentationContext AppShell::NotificationContext() {
    Stack::Notifications::PresentationContext context;
    const auto* viewport = ImGui::GetMainViewport();
    context.currentOwner = m_ActiveProjectWorkspace;
    context.workspacePosition = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + Stack::Header::CaptionHeight);
    context.workspaceSize = ImVec2(viewport->WorkSize.x, std::max(1.f, viewport->WorkSize.y - Stack::Header::CaptionHeight));
    context.reducedMotion = Stack::Notifications::SystemReducedMotion();
    context.keepFixed = [this](ImDrawList* draw) { m_WorkspaceCompositor.KeepFixed(draw); };
    context.viewProject = [this](std::uint64_t owner) {
        if (FindProjectWorkspace(owner)) ActivateProjectWorkspace(owner);
    };
    return context;
}

void AppShell::PostNotification(UiNotificationSeverity severity, const std::string& message, const std::string& key) {
    PostUiNotification(m_Editor ? m_Editor->GetNotifier() : m_AppNotifier, severity, message, key);
}

void AppShell::RenderNotifications() {
    const auto context = NotificationContext();
    m_NotificationPresenter.RenderActivity(*m_NotificationStore, context);
    m_NotificationPresenter.RenderDialog(*m_NotificationStore, context);
}

void AppShell::ReportSaveFailure(EditorModule& owner, const std::string& message,
    std::function<void()> continuation) {
    using namespace Stack::Notifications;
    const auto scope = owner.GetNotifier();
    const auto document = owner.GetProjectDocumentId();
    const auto generation = owner.GetProjectSession().files->load.generation;
    auto* editor = &owner;
    const auto current = [scope, editor, document, generation] {
        return scope.Valid() && editor->GetProjectDocumentId() == document &&
            editor->GetProjectSession().files->load.generation == generation;
    };
    NoticeSpec spec;
    spec.title = "Project was not saved"; spec.message = "Your work is still open.";
    spec.details = message; spec.severity = Severity::Error; spec.outcome = Outcome::Failure;
    spec.foreground = scope.GetOwner().id == m_ActiveProjectWorkspace;
    const auto previous = m_SaveRecoveryOwners.find(scope.GetOwner().id);
    if (previous != m_SaveRecoveryOwners.end()) scope.InvalidateOperation(previous->second.operation);
    spec.operationId = scope.NewOperation();
    m_SaveRecoveryOwners[scope.GetOwner().id] = {document,owner.GetProjectSession().files,generation,spec.operationId};
    auto event = std::make_shared<EventId>(0);
    ActionSpec keep;
    keep.label = "Keep open"; keep.safeCancel = true;
    keep.invoke = [] { return ActionResult::Success(); };
    spec.actions.push_back(std::move(keep));
    ActionSpec retry;
    retry.label = "Retry save"; retry.canInvoke = current;
    retry.invoke = [this, scope, editor, current, event, continuation = std::move(continuation)] {
        if (!current()) return ActionResult::Failure("This project changed. Save it from its tab.");
        const bool accepted = editor->RequestSaveWorkspaceBeforeClose([this, scope, current, event, continuation](bool success) {
            if (!current()) { scope.FinishAction(*event,1,ActionResult::Failure("The project changed while saving.")); return; }
            scope.FinishAction(*event,1,success ? ActionResult::Success() :
                ActionResult::Failure("The save failed. Your work is still open. Try again or choose Keep open."));
            if (success && continuation) continuation();
        });
        return accepted ? ActionResult::Pending() : ActionResult::Failure("The save could not start. Try again.");
    };
    spec.actions.push_back(std::move(retry));
    *event = scope.RequestDecision(std::move(spec));
}

void AppShell::RenderFileCommandPrompts() {
    DispatchPendingFileMenuSaveAs();
    if (m_ShowOpenProjectPrompt) {
        m_ShowOpenProjectPrompt = false;
        std::filesystem::path selected =
            FileDialogs::OpenStackProjectFileDialog("Open Project");
        if (!selected.empty()) {
            std::string fileName = selected.filename().string();
            std::transform(
                fileName.begin(),
                fileName.end(),
                fileName.begin(),
                [](unsigned char value) {
                    return static_cast<char>(std::tolower(value));
                });
            if (fileName == "project.stack") {
                selected = selected.parent_path();
            }
            QueueFileAction(PendingFileAction::OpenProject, std::move(selected));
        }
    }
    m_ShowFileDispositionPrompt = false;
}

void AppShell::RenderEditorSavePrompts() {
    const bool newProject = m_Editor->ConsumeProjectNamingPromptRequest();
    if (newProject || m_ShowEditorNamePrompt) {
        m_ShowEditorNamePrompt = false;
        const auto scope = m_Editor->GetNotifier();
        m_Editor->RequestSaveWorkspaceBeforeClose([scope](bool success) {
            if (!success) scope.Error("Could not create the project on disk. Your work is still open.");
        });
    }
    if (m_ShowEditorSavePrompt) {
        m_ShowEditorSavePrompt = false;
        const auto target = std::exchange(m_PendingProjectLoadFileName, {});
        if (!target.empty()) BeginLibraryToEditorProjectLoad(target);
    }
    if (m_ShowRawWorkspaceSwitchPrompt && !m_RawWorkspaceSwitchSavePending) {
        m_ShowRawWorkspaceSwitchPrompt = false;
        m_RawWorkspaceSwitchSavePending = true;
        auto* owner = m_Editor;
        const auto scope = owner->GetNotifier();
        owner->RequestSaveWorkspaceBeforeClose([this, owner, scope](bool success) {
            m_RawWorkspaceSwitchSavePending = false;
            if (!scope.Valid()) return;
            if (success) owner->CloseEditorProjectAndActivateRawWorkspace();
            else scope.Error("Could not save the project. It will stay open.");
        });
    }
    if (m_ShowUnnamedEditorClosePrompt) {
        m_ShowUnnamedEditorClosePrompt = false;
        RequestMainWindowClose(m_PendingMainWindowCloseSource.c_str());
    }
}
