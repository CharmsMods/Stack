#include "Editor/EditorModule.h"

#include "App/PlatformHelpers.h"
#include "Utils/FileDialogs.h"
#include "App/WorkspacePresentation.h"
#include "Library/LibraryManager.h"
#include "Persistence/ProjectIndex.h"
#include "Project/ProjectPath.h"
#include "Raw/RawGalleryFileActions.h"
#include "Raw/RawLoader.h"
#include "Raw/RawSourceFolders.h"

#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <map>
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

Stack::RawGalleryActions::Context EditorModule::CaptureRawGalleryActionContext(bool editControlsOnly) {
    Stack::RawGalleryActions::Context context;
    context.workspaceRoot = m_RawWorkspace.workspaceRoot;
    context.catalogGeneration = m_RawWorkspaceScanGeneration;
    context.openAvailable = static_cast<bool>(m_RawGalleryOpenSelectionHandler);
    context.queueAvailable = static_cast<bool>(m_RawGalleryQueueRequestHandler);
    RefreshRawEditAttributeClipboardFromSystem(!editControlsOnly);
    context.hasEditClipboard = m_RawEditAttributeClipboard.has_value();
    context.foregroundBusy = !editControlsOnly &&
        (GetProjectFileCommandContext().busy || IsBracketingPresentationActive());
    context.previewOnly = Stack::Workspace::IsPreview();
    context.aggregateView = m_RawWorkspace.allSourceFolders;
    std::unordered_map<std::string, const Stack::RawWorkspace::SourceRecord*> sourcesByKey;
    std::unordered_map<std::string, const Stack::RawWorkspace::SourceRecord*> sourcesByFingerprint;
    if (!editControlsOnly && (m_RawWorkspace.selectedSourceKeys.size() > 1u || !m_RawWorkspaceLabSelectedProjectPaths.empty())) {
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
        if (editControlsOnly) return;
        if (path.empty() || !Raw::RawLoader::IsRawPath(path.u8string())) return;
        auto key = path.lexically_normal().generic_wstring();
#if defined(_WIN32)
        std::transform(key.begin(), key.end(), key.begin(),
            [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
#endif
        if (captureKeys.insert(std::move(key)).second) context.bracketSources.push_back(path);
    };
    auto sourceBegin = m_RawWorkspace.selectedSourceKeys.begin();
    auto sourceEnd = m_RawWorkspace.selectedSourceKeys.end();
    auto projectBegin = m_RawWorkspaceLabSelectedProjectPaths.begin();
    auto projectEnd = m_RawWorkspaceLabSelectedProjectPaths.end();
    if (editControlsOnly) {
        // These identities come from the same catalog as the selection. Avoid
        // filesystem canonicalization and full selection materialization while
        // drawing controls; execution still captures and validates every item.
        const auto focusedProject = std::find(projectBegin, projectEnd, m_RawWorkspaceLabFocusedProjectPath);
        if (focusedProject != projectEnd) {
            projectBegin = focusedProject;
            projectEnd = projectBegin + 1;
            sourceBegin = sourceEnd;
        } else if (sourceBegin != sourceEnd) {
            const auto focusedSource = std::find(sourceBegin, sourceEnd, m_RawWorkspace.selectedSourceKey);
            if (focusedSource != sourceEnd) sourceBegin = focusedSource;
            sourceEnd = sourceBegin + 1;
            projectBegin = projectEnd;
        } else if (projectBegin != projectEnd) {
            projectEnd = projectBegin + 1;
        }
    }
    for (auto selected = sourceBegin; selected != sourceEnd; ++selected) {
        const auto& key = *selected;
        const auto found = sourcesByKey.find(key);
        const auto* source = sourcesByKey.empty() ? FindGallerySource(m_RawWorkspace.sources, key)
            : found == sourcesByKey.end() ? nullptr : found->second;
        if (!source || source->absolutePath.empty()) continue;
        Stack::RawGalleryActions::Item item;
        item.path = source->absolutePath;
        item.sourceKey = key;
        item.sourceRoot = Stack::RawWorkspace::ResolveSourceWorkspaceRoot(*source, m_RawWorkspace.workspaceRoot);
        // An original remains an unedited input even when a saved project
        // references it. Copying edits belongs to the saved project card.
        item.camera = source->thumbnail.camera;
        if (source->thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Ready ||
            source->thumbnail.status == Stack::RawWorkspace::ThumbnailStatus::Valid)
            item.thumbnailPath = source->thumbnail.absolutePath;
        if (key == m_RawWorkspace.selectedSourceKey) context.focusedIndex = context.items.size();
        context.items.push_back(std::move(item));
        addCapture(source->absolutePath);
    }
    for (auto selected = projectBegin; selected != projectEnd; ++selected) {
        const auto& path = *selected;
        if (path.empty()) continue;
        Stack::RawGalleryActions::Item item;
        item.kind = Kind::Project;
        item.path = path;
        item.protectedProject = !editControlsOnly && (Stack::Project::SameProjectPath(path, m_Project->storePath) ||
            (m_ProjectRemovalGuard && m_ProjectRemovalGuard(path)));
        Stack::Project::ProjectRecord record;
        if (FindRawWorkspaceGalleryProjectRecord(path, record)) {
            item.canCopyEdits = !record.needsAttention && (record.projectKind=="editor" || record.hasRawWorkspaceRecipe || record.multiFrameProject);
            item.canVersion = record.format == Stack::Project::IndexedProjectFormat::CurrentBundle &&
                record.storageKind == Stack::Project::ProjectStorageKind::WorkingFile &&
                !record.readOnlyRecovery && !record.needsAttention;
            if (!editControlsOnly) {
                for (const auto& indexedSource : record.sources) {
                    std::filesystem::path original = indexedSource.originalPath;
                    if (original.empty() && !indexedSource.fingerprint.empty()) {
                        const auto source = sourcesByFingerprint.find(indexedSource.fingerprint);
                        if (source != sourcesByFingerprint.end()) original = source->second->absolutePath;
                    }
                    addCapture(original);
                }
            }
        }
        if (editControlsOnly || Stack::Project::SameProjectPath(path, m_RawWorkspaceLabFocusedProjectPath))
            context.focusedIndex = context.items.size();
        context.items.push_back(std::move(item));
    }
    if (context.focusedIndex >= context.items.size() && !context.items.empty()) context.focusedIndex = 0;
    return context;
}

void EditorModule::RefreshRawGalleryAfterFileMove(const std::filesystem::path& root) {
    // Index discovery touches disk; never run it on the key/input thread.
    ProjectTasks().Submit("Refreshing Gallery after move", [this, root] {
        Stack::Project::ProjectIndex::Get().RebuildDefaultRoots();
        ProjectTasks().PostToMain([this, root] {
            LibraryManager::Get().RequestRefreshLibraryAsync();
            if (!SameRoot(root, m_RawWorkspace.workspaceRoot)) return;
            RequestRawWorkspaceScan(false);
            InvalidateRawWorkspaceGalleryPresentation();
        });
    });
}

bool EditorModule::ExecuteRawGalleryAction(Action action,
    const Stack::RawGalleryActions::Context& captured) {
    auto context = captured;
    if (context.aggregateView != m_RawWorkspace.allSourceFolders ||
        !SameRoot(context.workspaceRoot, m_RawWorkspace.workspaceRoot) ||
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
            else request.sources.push_back({item.path, item.thumbnailPath, item.camera});
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
    case Action::Relink:
        return RequestGalleryProjectRelink(context.items[context.focusedIndex].path);
    case Action::ApplyProject:
    case Action::CopyEdits: {
        const auto& item = context.items[context.focusedIndex];
        std::string error;
        m_GalleryApplyChooseTarget = action == Action::ApplyProject;
        const bool copied = CopyRawEditAttributesFromProjectPath(item.path, {}, &error);
        if (!copied) PostNotification(UiNotificationSeverity::Error,
            error.empty() ? "The focused item has no copyable RAW edit." : error, "raw-edit-attributes-copy-failed");
        if (copied && m_GalleryApplyChooseTarget) {
            m_RawWorkspaceGalleryContentMode = Stack::RawWorkspace::GalleryContentMode::Gallery;
            m_RawWorkspaceLabSelectedProjectPaths.clear();
            InvalidateRawWorkspaceGalleryPresentation(false);
            PostNotification(UiNotificationSeverity::Info,"Select a target original or linked project, then choose Apply to selection.","preset-select-target");
        } else if (!copied) m_GalleryApplyChooseTarget=false;
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
    case Action::MoveToTrash: {
        if (m_RawGalleryDeletionPending) return false;
        const auto document = GetProjectDocumentId();
        auto remove = [this, context, document] {
            if (GetProjectDocumentId() != document || context.aggregateView != m_RawWorkspace.allSourceFolders ||
                context.catalogGeneration != m_RawWorkspaceScanGeneration ||
                !SameRoot(context.workspaceRoot, m_RawWorkspace.workspaceRoot)) {
                PostNotification(UiNotificationSeverity::Warning,
                    "The Gallery folder changed before the items could be moved. Select them again.", "raw-gallery-action-stale");
                return;
            }
            std::vector<std::pair<Kind,std::pair<std::filesystem::path,std::filesystem::path>>> jobs;
            Stack::RawGalleryFileActions::Result result;
            bool protectedProject = false;
            for (const auto& item : context.items) {
                if (item.kind == Kind::Project) {
                    const auto store = Stack::Project::ResolveProjectStoreRoot(item.path);
                    if (Stack::Project::SameProjectPath(store, Stack::Project::ResolveProjectStoreRoot(m_Project->storePath)) ||
                        (m_ProjectRemovalGuard && m_ProjectRemovalGuard(store))) {
                        protectedProject = true;
                        continue;
                    }
                }
                const auto owner = item.sourceRoot.empty() ? ResolveRawWorkspaceSourceFolder(item.path) : item.sourceRoot;
                if (owner.empty()) {
                    result.errors.push_back("Could not identify the source folder for " + item.path.u8string());
                    continue;
                }
                jobs.push_back({item.kind,{owner,item.path}});
            }
            m_RawGalleryDeletionPending = true;
            const auto activity = GetNotifier().BeginActivity("Moving " + std::to_string(jobs.size()) + " items to Closet Trash");
            if (!ProjectTasks().Submit("Moving Gallery items to Trash",[this,context,document,activity,jobs=std::move(jobs),result=std::move(result),protectedProject]() mutable {
                std::map<std::pair<std::filesystem::path,Kind>,std::vector<std::filesystem::path>> batches;
                for(const auto& job:jobs)batches[{job.second.first,job.first}].push_back(job.second.second);
                for(const auto& [batch,paths]:batches) {
                    try {
                        auto moved=batch.second==Kind::Source ?
                            Stack::RawGalleryFileActions::DeleteSourcesToTrash(batch.first,paths) :
                            Stack::RawGalleryFileActions::DeleteProjectsToTrash(batch.first,paths);
                        result.moved+=moved.moved;
                        result.movedSources.insert(result.movedSources.end(),moved.movedSources.begin(),moved.movedSources.end());
                        result.errors.insert(result.errors.end(),moved.errors.begin(),moved.errors.end());
                    } catch(const std::exception& error) {result.errors.push_back(error.what());}
                }
                ProjectTasks().PostToMain([this,context,document,activity,result=std::move(result),protectedProject]() mutable {
            m_RawGalleryDeletionPending = false;
            GetNotifier().CompleteActivity(activity, "Finished moving Gallery items.");
            if (result.moved && GetProjectDocumentId()==document && SameRoot(context.workspaceRoot,m_RawWorkspace.workspaceRoot)) {
                // Reject scan publications captured before the move; the fresh
                // scan starts after the background index refresh completes.
                {
                    std::lock_guard<std::mutex> lock(m_RawWorkspaceScanMutex);
                    m_RawWorkspaceScanSnapshot.generation=++m_RawWorkspaceScanGeneration;
                    m_RawWorkspaceScanSnapshot.state=Async::TaskState::Idle;
                }
                m_RawWorkspaceGalleryLayoutAnimation.PrepareRemoval();
                ++m_RawWorkspaceGalleryGridStacks.navigationRevision;
                std::unordered_set<std::string> movedPaths, movedKeys;
                for (const auto& path : result.movedSources) movedPaths.insert(path.lexically_normal().generic_u8string());
                const auto movedPath = [&](const auto& path) { return movedPaths.count(path.lexically_normal().generic_u8string()) != 0; };
                for (const auto& source : m_RawWorkspace.sources) if (movedPath(source.absolutePath)) movedKeys.insert(source.relativePathKey);
                auto& sources=m_RawWorkspace.sources;
                sources.erase(std::remove_if(sources.begin(),sources.end(),[&](const auto& source) {return movedPath(source.absolutePath);}),sources.end());
                auto& projects=m_RawWorkspace.sourceSetProjects;
                projects.erase(std::remove_if(projects.begin(),projects.end(),[&](const auto& project) {return movedPath(project.absolutePath);}),projects.end());
                auto& keys=m_RawWorkspace.selectedSourceKeys;
                keys.erase(std::remove_if(keys.begin(),keys.end(),[&](const auto& key) {return movedKeys.count(key)!=0;}),keys.end());
                auto& paths=m_RawWorkspaceLabSelectedProjectPaths;
                paths.erase(std::remove_if(paths.begin(),paths.end(),movedPath),paths.end());
                if (movedKeys.count(m_RawWorkspace.selectedSourceKey)) m_RawWorkspace.selectedSourceKey=keys.empty()?std::string{}:keys.front();
                if (movedPath(m_RawWorkspaceLabFocusedProjectPath)) {m_RawWorkspaceLabFocusedProjectPath.clear();m_RawWorkspaceLabFocusedProjectName.clear();}
                InvalidateRawWorkspaceGalleryPresentation();
            }
            if (result.moved && GetProjectDocumentId()==document && SameRoot(context.workspaceRoot,m_RawWorkspace.workspaceRoot)) RefreshRawGalleryAfterFileMove(context.workspaceRoot);
            Stack::Notifications::NoticeSpec notice;
            notice.severity = !result.errors.empty() || protectedProject ? UiNotificationSeverity::Warning :
                result.moved ? UiNotificationSeverity::Success : UiNotificationSeverity::Error;
            notice.outcome = !result.errors.empty() || protectedProject ? Stack::Notifications::Outcome::Partial :
                result.moved ? Stack::Notifications::Outcome::Success : Stack::Notifications::Outcome::Failure;
            notice.message = result.moved ? "Moved " + std::to_string(result.moved) +
                (result.moved == 1u ? " item to Closet Trash." : " items to Closet Trash.") :
                "No items were moved to Closet Trash.";
            if (protectedProject) notice.message += " Open projects were kept.";
            notice.dedupeKey = "raw-gallery-delete-project";
            for (const auto& error : result.errors) notice.details += error + "\n";
            GetNotifier().Post(std::move(notice));
                });
            })) {
                m_RawGalleryDeletionPending = false;
                GetNotifier().CompleteActivity(activity, "Could not schedule Gallery deletion.");
                PostNotification(UiNotificationSeverity::Error,"Could not schedule Gallery deletion.","raw-gallery-delete-project");
            }
        };
        if (!RequestAutoBracketForeground("move these items to Trash", remove)) remove();
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
        Action::ApplyProject, Action::CopyEdits, Action::PasteEdits, Action::NewSavedVersion, Action::ShowInExplorer,
        Action::CopyPath, Action::Relink, Action::MoveToTrash, Action::RevertProjects};
    for (const auto action : actions) {
        if (action == Action::ApplyProject && !UsesGalleryPresetPreviews()) continue;
        if (action == Action::Relink && m_RawWorkspaceGalleryContentMode != Stack::RawWorkspace::GalleryContentMode::Unlinked) continue;
        if (action == Action::CopyEdits && (context.focusedIndex >= context.items.size() ||
                context.items[context.focusedIndex].kind != Kind::Project)) continue;
        if (action == Action::CopyEdits || action == Action::ShowInExplorer || action == Action::MoveToTrash)
            ImGui::Separator();
        if ((action == Action::RevertProjects || action == Action::NewSavedVersion) &&
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
        action = Action::MoveToTrash;
    else if (io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_C, false))
        action = Action::CopyEdits;
    else if (io.KeyCtrl && io.KeyShift && !io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_V, false))
        action = Action::PasteEdits;
    if (action) {
        m_RawGalleryShortcutFrame = ImGui::GetFrameCount();
        ExecuteRawGalleryAction(*action, CaptureRawGalleryActionContext());
    }
}

bool EditorModule::RequestGalleryProjectRelink(const std::filesystem::path& path) {
    const auto folder = FileDialogs::OpenFolderDialog("Locate original photos");
    if (folder.empty()) return false;
    const auto owner = m_Project;
    return ProjectTasks().Submit("Relinking project originals", [this, owner, path, folder] {
        std::size_t matched = 0;
        std::string error;
        const bool ok = Stack::Project::RelinkProjectSources(path, std::filesystem::u8path(folder), matched, error);
        owner->tasks.PostToMain([this, ok, matched, error] {
            PostNotification(ok ? UiNotificationSeverity::Info : UiNotificationSeverity::Error,
                ok ? "Reconnected " + std::to_string(matched) + " original(s)." : error, "gallery-relink");
            if (matched) RefreshUnifiedProjectViewsAfterSave(true);
        });
    });
}
