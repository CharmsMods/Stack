#include "LibraryManager.h"

#include "App/AppPaths.h"
#include "Renderer/GLHelpers.h"

#include <filesystem>
#include <utility>

LibraryManager::LibraryManager() {
    AppPaths::EnsureRuntimeDirectories();
    m_LibraryPath = AppPaths::GetProjectsDirectory();
    m_AssetsPath = AppPaths::GetLibraryDirectory() / "Previews";
    std::error_code ec;
    std::filesystem::create_directories(m_LibraryPath, ec);
    ec.clear();
    std::filesystem::create_directories(m_AssetsPath, ec);
    const AppPaths::DirectoryWriteProbe writeProbe =
        AppPaths::ProbeProjectsDirectoryWritable();
    m_ProjectRootWritable = writeProbe.writable;
    m_ProjectRootWriteError = writeProbe.message;
    if (!m_ProjectRootWritable) {
        PostNotification(
            UiNotificationSeverity::Error,
            m_ProjectRootWriteError,
            "project-root-read-only");
    }
}

LibraryManager::~LibraryManager() {
    for (auto& project : m_Projects) {
        ReleaseProjectTextures(project);
    }
    for (auto& asset : m_Assets) {
        ReleaseAssetTextures(asset);
    }
}

void LibraryManager::ProcessDeferredDeletions() {
    if (m_DeferredTextureDeletions.empty()) return;
    glDeleteTextures(static_cast<GLsizei>(m_DeferredTextureDeletions.size()), m_DeferredTextureDeletions.data());
    m_DeferredTextureDeletions.clear();
}

bool LibraryManager::ConsumeSavedProjectEvent(std::string& outFileName, std::string& outProjectKind) {
    if (m_PendingSavedProjectFileName.empty()) {
        outFileName.clear();
        outProjectKind.clear();
        return false;
    }

    outFileName = m_PendingSavedProjectFileName;
    outProjectKind = m_PendingSavedProjectKind;
    m_PendingSavedProjectFileName.clear();
    m_PendingSavedProjectKind.clear();
    return true;
}

void LibraryManager::SetNotificationScope(Stack::Notifications::Notifier notifier) {
    m_Notifier = std::move(notifier);
    if (!m_ProjectRootWritable) {
        PostNotification(UiNotificationSeverity::Error, m_ProjectRootWriteError, "project-root-read-only");
    }
}

void LibraryManager::CollectActivity(Stack::UiActivity::Snapshot& snapshot) const {
    snapshot.Add(Async::IsBusy(GetExportTaskState()), "Exporting");
    snapshot.Add(Async::IsBusy(GetSaveTaskState()), "Saving");
    snapshot.Add(Async::IsBusy(GetImportTaskState()), "Importing");
    snapshot.Add(Async::IsBusy(GetProjectLoadTaskState()), "Loading project");
    snapshot.Add(IsRefreshBusy(), "Refreshing library");
    std::lock_guard<std::mutex> lock(m_ProjectsMutex);
    auto collect = [&](const auto& entries) {
        for (const auto& entry : entries) {
            if (!entry) continue;
            snapshot.Add(Async::IsBusy(entry->previewTaskState), "Loading preview");
            snapshot.Add(Async::IsBusy(entry->thumbnailDecodeState) ||
                entry->thumbnailDecodeState == Async::TaskState::Ready, "Building previews");
        }
    };
    collect(m_Projects);
    collect(m_Assets);
}

void LibraryManager::QueueSavedProjectEvent(const std::string& fileName, const std::string& projectKind) {
    m_PendingSavedProjectFileName = fileName;
    m_PendingSavedProjectKind = projectKind;
}

void LibraryManager::PostNotification(UiNotificationSeverity severity, std::string message, std::string dedupeKey) {
    if (message.empty()) {
        return;
    }
    PostUiNotification(m_Notifier, severity, std::move(message), std::move(dedupeKey));
}

Async::ActivityMetadata LibraryManager::MakeActivityMetadata(std::string label, bool maintenance) const {
    Async::ActivityMetadata metadata;
    metadata.ownerId = m_Notifier.GetOwner().id;
    metadata.ownerGeneration = m_Notifier.GetOwner().generation;
    metadata.label = std::move(label);
    metadata.maintenance = maintenance;
    const auto inherited = Async::TaskSystem::CurrentActivity();
    if (inherited.ownerId == metadata.ownerId && inherited.ownerGeneration == metadata.ownerGeneration)
        metadata.operationId = inherited.operationId;
    return metadata;
}

void LibraryManager::ReportPreviewProblem(const std::shared_ptr<ProjectEntry>& project) {
    if (!project) return;
    Stack::Notifications::NoticeSpec notice;
    notice.title = "Preview unavailable";
    notice.message = "Could not prepare this project preview.";
    notice.context = project->projectName;
    notice.details = project->previewStatusText;
    notice.severity = Stack::Notifications::Severity::Error;
    notice.dedupeKey = "project-preview-" + project->fileName;
    const std::weak_ptr<ProjectEntry> target = project;
    Stack::Notifications::ActionSpec retry;
    retry.label = "Retry";
    retry.resolveOnSuccess = false;
    retry.canInvoke = [target] {
        const auto project = target.lock();
        return project && !Async::IsBusy(project->previewTaskState);
    };
    retry.invoke = [this, target] {
        const auto project = target.lock();
        if (!project) return Stack::Notifications::ActionResult::Failure("This project is no longer in the Library.");
        RequestProjectPreview(project);
        return Stack::Notifications::ActionResult::Success();
    };
    notice.actions.push_back(std::move(retry));
    const auto key = notice.dedupeKey;
    m_PreviewProblems[key] = m_Notifier.Post(std::move(notice));
}

void LibraryManager::ReportPreviewProblem(const std::shared_ptr<AssetEntry>& asset) {
    if (!asset) return;
    Stack::Notifications::NoticeSpec notice;
    notice.title = "Preview unavailable";
    notice.message = "Could not prepare this image preview.";
    notice.context = asset->displayName;
    notice.details = asset->previewStatusText;
    notice.severity = Stack::Notifications::Severity::Error;
    notice.dedupeKey = "asset-preview-" + asset->fileName;
    const std::weak_ptr<AssetEntry> target = asset;
    Stack::Notifications::ActionSpec retry;
    retry.label = "Retry";
    retry.resolveOnSuccess = false;
    retry.canInvoke = [target] {
        const auto asset = target.lock();
        return asset && !Async::IsBusy(asset->previewTaskState);
    };
    retry.invoke = [this, target] {
        const auto asset = target.lock();
        if (!asset) return Stack::Notifications::ActionResult::Failure("This image is no longer in the Library.");
        RequestAssetPreview(asset);
        return Stack::Notifications::ActionResult::Success();
    };
    notice.actions.push_back(std::move(retry));
    const auto key = notice.dedupeKey;
    m_PreviewProblems[key] = m_Notifier.Post(std::move(notice));
}

void LibraryManager::ResolvePreviewProblem(const std::string& key) {
    const auto found = m_PreviewProblems.find(key);
    if (found == m_PreviewProblems.end()) return;
    m_Notifier.Resolve(found->second);
    m_PreviewProblems.erase(found);
}
