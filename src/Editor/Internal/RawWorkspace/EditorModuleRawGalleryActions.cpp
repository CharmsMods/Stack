#include "Editor/EditorModule.h"

#include "App/PlatformHelpers.h"
#include "App/WorkspacePresentation.h"
#include "Library/LibraryManager.h"
#include "Persistence/ProjectIndex.h"
#include "Project/ProjectPath.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/RawLoader.h"

#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>

namespace {
using Action = Stack::RawGalleryActions::Action;
using Kind = Stack::RawGalleryActions::ItemKind;

bool SameRoot(const std::filesystem::path& a, const std::filesystem::path& b) {
    return (a.empty() && b.empty()) || Stack::Project::SameProjectPath(a, b);
}

const Stack::RawWorkspace::SourceRecord* FindGallerySource(
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources,
    const std::string& key,
    const std::filesystem::path& expectedPath = {}) {
    const auto found = std::find_if(sources.begin(), sources.end(), [&](const auto& source) {
        return source.relativePathKey == key && (expectedPath.empty() ||
            Stack::Project::SameProjectPath(source.absolutePath, expectedPath));
    });
    return found == sources.end() ? nullptr : &*found;
}
}

Stack::RawGalleryActions::Context EditorModule::CaptureRawGalleryActionContext() {
    Stack::RawGalleryActions::Context context;
    context.workspaceRoot = m_RawWorkspace.workspaceRoot;
    context.catalogGeneration = m_RawWorkspaceScanGeneration;
    context.openAvailable = static_cast<bool>(m_RawGalleryOpenSelectionHandler);
    context.queueAvailable = static_cast<bool>(m_RawGalleryQueueRequestHandler);
    RefreshRawEditAttributeClipboardFromSystem();
    context.hasEditClipboard = m_RawEditAttributeClipboard.has_value();
    context.foregroundBusy = GetProjectFileCommandContext().busy || IsBracketingPresentationActive();
    context.previewOnly = Stack::Workspace::IsPreview();
    const auto* activeSource = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
    std::unordered_map<std::string, const Stack::RawWorkspace::SourceRecord*> sourcesByKey;
    std::unordered_map<std::string, const Stack::RawWorkspace::SourceRecord*> sourcesByFingerprint;
    if (m_RawWorkspace.selectedSourceKeys.size() > 1u || !m_RawWorkspaceLabSelectedProjectPaths.empty()) {
        sourcesByKey.reserve(m_RawWorkspace.sources.size());
        if (!m_RawWorkspaceLabSelectedProjectPaths.empty())
            sourcesByFingerprint.reserve(m_RawWorkspace.sources.size());
        for (const auto& source : m_RawWorkspace.sources) {
            sourcesByKey.emplace(source.relativePathKey, &source);
            if (!m_RawWorkspaceLabSelectedProjectPaths.empty() && !source.fingerprint.empty())
                sourcesByFingerprint.emplace(source.fingerprint, &source);
        }
    }
    std::unordered_set<std::wstring> captureKeys;
    const auto addCapture = [&](const std::filesystem::path& path) {
        if (path.empty() || !Raw::RawLoader::IsRawPath(path.u8string())) return;
        auto key = path.lexically_normal().generic_wstring();
#if defined(_WIN32)
        std::transform(key.begin(), key.end(), key.begin(),
            [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
#endif
        if (captureKeys.insert(std::move(key)).second) context.bracketSources.push_back(path);
    };
    for (const auto& key : m_RawWorkspace.selectedSourceKeys) {
        const auto found = sourcesByKey.find(key);
        const auto* source = sourcesByKey.empty() ? FindGallerySource(m_RawWorkspace.sources, key)
            : found == sourcesByKey.end() ? nullptr : found->second;
        if (!source || source->absolutePath.empty()) continue;
        Stack::RawGalleryActions::Item item;
        item.path = source->absolutePath;
        item.sourceKey = key;
        item.canCopyEdits = (!IsMultiFrameRawProjectActive() && IsRawWorkspaceProjectActive() &&
                m_Project->rawSourceKey == key && activeSource &&
                Stack::Project::SameProjectPath(activeSource->absolutePath, source->absolutePath)) ||
            ((source->project.status == Stack::RawWorkspace::ProjectStatus::Existing ||
              source->project.status == Stack::RawWorkspace::ProjectStatus::Embedded) &&
             !source->project.absolutePath.empty());
        if (source->thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Ready ||
            source->thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Valid)
            item.thumbnailPath = source->thumbnail.absolutePath;
        if (key == m_RawWorkspace.selectedSourceKey) context.focusedIndex = context.items.size();
        context.items.push_back(std::move(item));
        addCapture(source->absolutePath);
    }
    for (const auto& path : m_RawWorkspaceLabSelectedProjectPaths) {
        if (path.empty()) continue;
        Stack::RawGalleryActions::Item item;
        item.kind = Kind::Project;
        item.path = path;
        item.protectedProject = Stack::Project::SameProjectPath(path, m_Project->storePath) ||
            (m_ProjectRemovalGuard && m_ProjectRemovalGuard(path));
        Stack::Project::ProjectRecord record;
        if (Stack::Project::ProjectIndex::Get().FindByPath(path, record)) {
            item.canCopyEdits = record.hasRawWorkspaceRecipe || record.multiFrameProject;
            item.canVersion = record.format == Stack::Project::IndexedProjectFormat::CurrentBundle &&
                record.storageKind == Stack::Project::ProjectStorageKind::DirectoryBundle &&
                !record.readOnlyRecovery && !record.needsAttention;
            for (const auto& indexedSource : record.sources) {
                std::filesystem::path original = indexedSource.originalPath;
                if (!indexedSource.fingerprint.empty()) {
                    const auto source = sourcesByFingerprint.find(indexedSource.fingerprint);
                    if (source != sourcesByFingerprint.end()) original = source->second->absolutePath;
                }
                addCapture(original);
            }
        }
        if (Stack::Project::SameProjectPath(path, m_RawWorkspaceLabFocusedProjectPath))
            context.focusedIndex = context.items.size();
        context.items.push_back(std::move(item));
    }
    if (context.focusedIndex >= context.items.size() && !context.items.empty()) context.focusedIndex = 0;
    return context;
}

void EditorModule::RefreshRawGalleryAfterFileMove(const std::filesystem::path& root) {
    Stack::Project::ProjectIndex::Get().RebuildDefaultRoots();
    LibraryManager::Get().RequestRefreshLibraryAsync();
    // A deferred operation may finish after folder navigation. Refresh the
    // shared index, but never clear the new folder's selection or scan owner.
    if (!SameRoot(root, m_RawWorkspace.workspaceRoot)) return;
    m_RawWorkspace.selectedSourceKey.clear();
    m_RawWorkspace.selectedSourceKeys.clear();
    m_RawWorkspaceLabSelectedProjectPaths.clear();
    m_RawWorkspaceLabFocusedProjectPath.clear();
    m_RawWorkspaceLabFocusedProjectName.clear();
    RescanRawWorkspace();
    InvalidateRawWorkspaceGalleryPresentation();
}

bool EditorModule::ExecuteRawGalleryAction(Action action,
    const Stack::RawGalleryActions::Context& captured) {
    auto context = captured;
    if (!SameRoot(context.workspaceRoot, m_RawWorkspace.workspaceRoot) ||
        context.catalogGeneration != m_RawWorkspaceScanGeneration) {
        PostNotification(UiNotificationSeverity::Warning,
            "The Gallery folder changed. Select the items again.", "raw-gallery-action-stale");
        return false;
    }
    // Recheck open sessions at execution, including queued native input.
    for (auto& item : context.items) {
        if (item.kind == Kind::Project)
            item.protectedProject = Stack::Project::SameProjectPath(item.path, m_Project->storePath) ||
                (m_ProjectRemovalGuard && m_ProjectRemovalGuard(item.path));
    }
    const auto availability = Stack::RawGalleryActions::GetAvailability(context, action);
    if (!availability.enabled) {
        PostNotification(UiNotificationSeverity::Warning, availability.reason, "raw-gallery-action-unavailable");
        return false;
    }
    switch (action) {
    case Action::Open:
    case Action::CreateCaptureSet: {
        RawGalleryOpenSelection selection;
        for (const auto& item : context.items) selection.items.push_back({item.path, item.kind == Kind::Project});
        selection.bracketSources = context.bracketSources;
        if (action == Action::CreateCaptureSet) {
            for (const auto& path : selection.bracketSources) {
                std::error_code error;
                if (!std::filesystem::is_regular_file(path, error) || error) {
                    PostNotification(UiNotificationSeverity::Error,
                        "One selected original capture is unavailable: " + path.filename().u8string(),
                        "gallery-bracket-source-unavailable");
                    return false;
                }
            }
        }
        m_RawGalleryOpenSelectionHandler(std::move(selection), action == Action::CreateCaptureSet);
        return true;
    }
    case Action::AddToQueue: {
        Stack::RawGalleryQueue::Request request;
        for (const auto& item : context.items) {
            if (item.kind == Kind::Project) request.projectPaths.push_back(item.path);
            else request.sources.push_back({item.path, item.thumbnailPath});
        }
        const auto result = m_RawGalleryQueueRequestHandler(request);
        Stack::Notifications::NoticeSpec notice;
        notice.message = result.added ? "Added " + std::to_string(result.added) +
            (result.added == 1u ? " item to Queue." : " items to Queue.") : "No new items were added to Queue.";
        if (result.alreadyPresent) notice.message += " " + std::to_string(result.alreadyPresent) + " already in Queue.";
        if (result.skipped) notice.message += " " + std::to_string(result.skipped) + " skipped.";
        notice.severity = result.skipped ? UiNotificationSeverity::Warning :
            result.added ? UiNotificationSeverity::Success : UiNotificationSeverity::Info;
        notice.outcome = result.skipped ? (result.added ? Stack::Notifications::Outcome::Partial :
            Stack::Notifications::Outcome::Failure) : Stack::Notifications::Outcome::Success;
        notice.dedupeKey = "raw-gallery-add-to-queue";
        for (const auto& error : result.errors) notice.details += error + "\n";
        GetNotifier().Post(std::move(notice));
        return result.added > 0u || result.alreadyPresent > 0u;
    }
    case Action::CopyEdits: {
        const auto& item = context.items[context.focusedIndex];
        std::string error;
        const auto* source = item.kind == Kind::Source ?
            FindGallerySource(m_RawWorkspace.sources, item.sourceKey, item.path) : nullptr;
        const bool copied = item.kind == Kind::Project ? CopyRawEditAttributesFromProjectPath(item.path, {}, &error) :
            source && Stack::Project::SameProjectPath(source->absolutePath, item.path) &&
                CopyRawEditAttributesFromGallerySource(*source, &error);
        if (!copied) PostNotification(UiNotificationSeverity::Error,
            error.empty() ? "The focused item has no copyable RAW edit." : error, "raw-edit-attributes-copy-failed");
        return copied;
    }
    case Action::PasteEdits: {
        std::vector<Stack::RawWorkspace::SourceRecord> sources;
        std::vector<Stack::RawWorkspace::SourceSetProjectCatalogEntry> projects;
        for (const auto& item : context.items) {
            if (item.kind == Kind::Source) {
                if (const auto* source = FindGallerySource(m_RawWorkspace.sources, item.sourceKey, item.path))
                    sources.push_back(*source);
            } else {
                const auto found = std::find_if(m_RawWorkspace.sourceSetProjects.begin(), m_RawWorkspace.sourceSetProjects.end(),
                    [&](const auto& project) { return Stack::Project::SameProjectPath(project.absolutePath, item.path); });
                if (found != m_RawWorkspace.sourceSetProjects.end()) projects.push_back(*found);
            }
        }
        RequestPasteRawEditAttributesForGalleryTargets(std::move(sources), std::move(projects));
        return true;
    }
    case Action::ShowInExplorer: {
        std::string error;
        const bool revealed = PlatformHelpers::RevealPathInExplorer(context.items.front().path, &error);
        if (!revealed) PostNotification(UiNotificationSeverity::Error,
            error.empty() ? "The selected item could not be shown in Explorer." : error, "raw-gallery-reveal");
        return revealed;
    }
    case Action::CopyPath: {
        std::string text;
        for (const auto& item : context.items) {
            if (!text.empty()) text += "\r\n";
            text += item.path.u8string();
        }
        ImGui::SetClipboardText(text.c_str());
        PostNotification(UiNotificationSeverity::Success,
            context.items.size() == 1u ? "Copied the selected path." : "Copied the selected paths.", "raw-gallery-copy-path");
        return true;
    }
    case Action::NewSavedVersion:
        RequestCreateProjectVersion(context.items.front().path);
        return true;
    case Action::RevertProjects:
        m_RawWorkspacePendingRevertProjectPaths.clear();
        for (const auto& item : context.items) m_RawWorkspacePendingRevertProjectPaths.push_back(item.path);
        m_RawWorkspaceRevertPopupRequested = true;
        RenderRawWorkspaceGalleryRevertPopup();
        return true;
    case Action::TrashProjects: {
        const auto document = GetProjectDocumentId();
        auto remove = [this, context, document] {
            if (GetProjectDocumentId() != document || !SameRoot(context.workspaceRoot, m_RawWorkspace.workspaceRoot)) {
                PostNotification(UiNotificationSeverity::Warning,
                    "The Gallery folder changed before the projects could be moved. Try again.", "raw-gallery-action-stale");
                return;
            }
            std::vector<std::filesystem::path> paths;
            bool protectedProject = false;
            for (const auto& item : context.items) {
                if (Stack::Project::SameProjectPath(item.path, m_Project->storePath) ||
                    (m_ProjectRemovalGuard && m_ProjectRemovalGuard(item.path))) protectedProject = true;
                else paths.push_back(item.path);
            }
            const auto result = Stack::RawGalleryFileActions::DeleteProjectsToTrash(context.workspaceRoot, paths);
            if (result.moved) RefreshRawGalleryAfterFileMove(context.workspaceRoot);
            Stack::Notifications::NoticeSpec notice;
            notice.severity = !result.errors.empty() || protectedProject ? UiNotificationSeverity::Warning :
                result.moved ? UiNotificationSeverity::Success : UiNotificationSeverity::Error;
            notice.outcome = !result.errors.empty() || protectedProject ? Stack::Notifications::Outcome::Partial :
                result.moved ? Stack::Notifications::Outcome::Success : Stack::Notifications::Outcome::Failure;
            notice.message = result.moved ? "Moved " + std::to_string(result.moved) +
                (result.moved == 1u ? " project to Stack Trash." : " projects to Stack Trash.") :
                "No projects were moved to Stack Trash.";
            if (protectedProject) notice.message += " Open projects were kept.";
            notice.dedupeKey = "raw-gallery-delete-project";
            for (const auto& error : result.errors) notice.details += error + "\n";
            GetNotifier().Post(std::move(notice));
        };
        if (!RequestAutoBracketForeground("remove these projects", remove)) remove();
        return true;
    }
    }
    return false;
}

void EditorModule::RenderRawGalleryContextActions(const std::filesystem::path& focusedPath) {
    auto context = CaptureRawGalleryActionContext();
    for (std::size_t i = 0; i < context.items.size(); ++i)
        if (Stack::Project::SameProjectPath(context.items[i].path, focusedPath)) context.focusedIndex = i;
    constexpr std::array actions {Action::Open, Action::CreateCaptureSet, Action::AddToQueue,
        Action::CopyEdits, Action::PasteEdits, Action::NewSavedVersion, Action::ShowInExplorer,
        Action::CopyPath, Action::TrashProjects, Action::RevertProjects};
    for (const auto action : actions) {
        if (action == Action::CopyEdits || action == Action::ShowInExplorer || action == Action::TrashProjects)
            ImGui::Separator();
        if ((action == Action::TrashProjects || action == Action::RevertProjects || action == Action::NewSavedVersion) &&
            std::none_of(context.items.begin(), context.items.end(), [](const auto& item) { return item.kind == Kind::Project; }))
            continue;
        const auto available = Stack::RawGalleryActions::GetAvailability(context, action);
        if (ImGui::MenuItem(Stack::RawGalleryActions::Label(action), nullptr, false, available.enabled))
            ExecuteRawGalleryAction(action, context);
        if (!available.enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", available.reason.c_str());
    }
}

void EditorModule::HandleRawGalleryActionShortcuts() {
    if (m_RawGalleryShortcutFrame == ImGui::GetFrameCount()) return;
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsAnyItemActive() ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) return;
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) return;
    std::optional<Action> action;
    if (!io.KeyCtrl && !io.KeyAlt && !io.KeyShift &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
        action = Action::TrashProjects;
    else if (io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_C, false))
        action = Action::CopyEdits;
    else if (io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_V, false))
        action = Action::PasteEdits;
    if (action) {
        m_RawGalleryShortcutFrame = ImGui::GetFrameCount();
        ExecuteRawGalleryAction(*action, CaptureRawGalleryActionContext());
    }
}
