#include "LibraryManager.h"
#include "Persistence/ProjectIndex.h"
#include "TagManager.h"

#include "App/AppPaths.h"
#include "Async/TaskSystem.h"
#include "Library/Internal/LibraryStorageHelpers.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace StackFormat = StackBinaryFormat;

struct LibraryScanResult {
    bool success = true;
    std::string errorMessage;
    std::vector<std::shared_ptr<ProjectEntry>> projects;
    std::vector<std::shared_ptr<AssetEntry>> assets;
    std::uintmax_t signature = 0;
    int totalItems = 0;
};

void SortProjectsNewestFirst(std::vector<std::shared_ptr<ProjectEntry>>& projects) {
    std::sort(projects.begin(), projects.end(), [](const auto& lhs, const auto& rhs) {
        if (!lhs || !rhs) {
            return static_cast<bool>(lhs) > static_cast<bool>(rhs);
        }
        if (lhs->timestamp != rhs->timestamp) {
            if (lhs->timestamp == "Unknown") {
                return false;
            }
            if (rhs->timestamp == "Unknown") {
                return true;
            }
            return lhs->timestamp > rhs->timestamp;
        }
        return lhs->fileName > rhs->fileName;
    });
}

std::filesystem::path GetStartupTracePath() {
    return AppPaths::GetStartupLogPath();
}

bool IsDetailedStartupTraceEnabled() {
    const char* value = std::getenv("STACK_STARTUP_TRACE");
    return value != nullptr && value[0] != '\0' && value[0] != '0';
}

void TraceStartupStep(const std::string& message) {
    if (!IsDetailedStartupTraceEnabled()) {
        return;
    }

    std::ofstream file(GetStartupTracePath(), std::ios::app);
    if (!file.is_open()) {
        return;
    }

    file << message << '\n';
    file.flush();
}

} // namespace

using namespace Stack::Library::StorageHelpers;

std::uintmax_t LibraryManager::BuildLibrarySignature() const {
    if (!std::filesystem::exists(m_LibraryPath) && !std::filesystem::exists(m_AssetsPath)) {
        return 0;
    }

    std::uintmax_t fileCount = 0;
    std::uintmax_t totalSize = 0;
    std::uintmax_t latestWrite = 0;
    std::uintmax_t nameHash = 0;

    const auto accumulateEntry = [&](const std::filesystem::directory_entry& entry) {
        std::error_code ec;
        const std::filesystem::path observedPath = entry.is_directory(ec) && !ec
            ? Stack::Project::WorkingProjectDocumentPath(entry.path())
            : entry.path();
        ec.clear();
        ++fileCount;
        if (std::filesystem::is_regular_file(observedPath, ec)) {
            totalSize += std::filesystem::file_size(observedPath, ec);
        }
        if (ec) ec.clear();

        const auto writeTime = std::filesystem::last_write_time(observedPath, ec);
        if (!ec) {
            const auto stamp = static_cast<std::uintmax_t>(writeTime.time_since_epoch().count());
            if (stamp > latestWrite) latestWrite = stamp;
        }

        nameHash ^= static_cast<std::uintmax_t>(
            std::hash<std::string>{}(observedPath.string()));
    };

    std::error_code ec;
    if (std::filesystem::exists(m_LibraryPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath, ec)) {
            if (ec) break;
            if (!IsSupportedProjectExtension(entry.path())) continue;
            accumulateEntry(entry);
        }
    }

    ec.clear();
    if (std::filesystem::exists(m_AssetsPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
            if (ec) break;
            if (!IsSupportedAssetExtension(entry.path()) && !IsSupportedAssetMetadataExtension(entry.path())) continue;
            accumulateEntry(entry);
        }
    }

    return (fileCount * 1315423911ull) ^ (totalSize * 2654435761ull) ^ latestWrite ^ nameHash;
}

int LibraryManager::GetProjectCount() const {
    int count = 0;
    std::error_code ec;
    if (std::filesystem::exists(m_LibraryPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath, ec)) {
            if (!ec && IsSupportedProjectExtension(entry.path())) count++;
        }
    }
    if (std::filesystem::exists(m_AssetsPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
            if (!ec && IsSupportedAssetMetadataExtension(entry.path())) count++;
        }
    }
    return count;
}

void LibraryManager::RefreshLibrary(
    std::function<void(int current, int total, const std::string& name)> progressCallback,
    bool syncEmbeddedProjectAssets) {
    (void)syncEmbeddedProjectAssets;
    std::lock_guard<std::mutex> lock(m_ProjectsMutex);
    TraceStartupStep("[LibraryManager] RefreshLibrary begin");

    TagManager::Get().SetLibraryPath(m_LibraryPath);
    TagManager::Get().Load();

    for (auto& project : m_Projects) {
        ReleaseProjectTextures(project);
    }
    m_Projects.clear();

    for (auto& asset : m_Assets) {
        ReleaseAssetTextures(asset);
    }
    m_Assets.clear();

    std::error_code ec;
    std::filesystem::create_directories(m_LibraryPath, ec);
    ec.clear();
    std::filesystem::create_directories(m_AssetsPath, ec);

    Stack::Project::ProjectIndex::Get().RebuildDefaultRoots();
    const std::vector<Stack::Project::ProjectRecord> indexedProjects =
        Stack::Project::ProjectIndex::Get().Snapshot();
    int totalItems = static_cast<int>(indexedProjects.size());
    int currentItem = 0;
    TraceStartupStep("[LibraryManager] Refresh counts: " + std::to_string(totalItems));

    for (const Stack::Project::ProjectRecord& record : indexedProjects) {
        ++currentItem;
        TraceStartupStep("[LibraryManager] Indexing project: " + record.absolutePath.string());
        if (progressCallback) {
            progressCallback(currentItem, totalItems, record.displayName);
        }
        auto project = std::make_shared<ProjectEntry>();
        project->projectId = record.projectId;
        project->fileName = record.absolutePath.string();
        project->absolutePath = record.absolutePath;
        project->projectName = record.displayName.empty()
            ? record.absolutePath.stem().string()
            : record.displayName;
        project->timestamp = record.timestamp.empty() ? "Unknown" : record.timestamp;
        project->projectKind = record.projectKind;
        project->thumbnailBytes = record.coverThumbnailBytes;
        project->sourceWidth = record.sourceWidth > 0 ? record.sourceWidth : 4;
        project->sourceHeight = record.sourceHeight > 0 ? record.sourceHeight : 3;
        project->needsAttention = record.needsAttention;
        project->readOnlyRecovery = record.readOnlyRecovery;
        project->errorMessage = record.errorMessage;
        m_Projects.push_back(std::move(project));
    }

    SortProjectsNewestFirst(m_Projects);

    ec.clear();
    if (std::filesystem::exists(m_AssetsPath, ec) && !ec) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath, ec)) {
            if (ec) break;
            std::string filename = entry.path().filename().string();
            if (filename.size() > 5 && filename.substr(filename.size() - 5) == ".hash") {
                try {
                    std::ifstream f(entry.path());
                    if (f.is_open()) {
                        nlohmann::json meta;
                        f >> meta;

                        std::string assetPngName = filename.substr(0, filename.size() - 5);
                        std::filesystem::path pngPath = m_AssetsPath / assetPngName;

                        if (std::filesystem::exists(pngPath)) {
                            auto asset = std::make_shared<AssetEntry>();
                            asset->fileName = assetPngName;
                            asset->displayName = meta.value("displayName", assetPngName);
                            asset->projectFileName = meta.value("projectFileName", "");
                            asset->timestamp = meta.value("timestamp", "Unknown");
                            asset->width = meta.value("width", 0);
                            asset->height = meta.value("height", 0);

                            const auto projectIt = std::find_if(
                                m_Projects.begin(),
                                m_Projects.end(),
                                [&](const std::shared_ptr<ProjectEntry>& p) {
                                    return p && !asset->projectFileName.empty() && p->fileName == asset->projectFileName;
                                });

                            if (projectIt != m_Projects.end() && *projectIt) {
                                asset->projectName = (*projectIt)->projectName;
                                asset->projectKind = (*projectIt)->projectKind;
                            }

                            m_Assets.push_back(asset);
                        }
                    }
                } catch (...) {}
            }
        }
    }

    m_LastLibrarySignature = BuildLibrarySignature();
    m_ProjectThumbnailWarmupCursor = 0;
    m_AssetThumbnailWarmupCursor = 0;
    m_ProjectThumbnailPriority.clear();
    m_AssetThumbnailPriority.clear();
    TraceStartupStep("[LibraryManager] RefreshLibrary complete");
}

LibraryRefreshSnapshot LibraryManager::GetRefreshSnapshot() const {
    std::lock_guard<std::mutex> lock(m_RefreshMutex);
    return m_LibraryRefreshSnapshot;
}

bool LibraryManager::IsRefreshBusy() const {
    return Async::IsBusy(GetRefreshSnapshot().state);
}

void LibraryManager::RequestRefreshLibraryAsync(bool syncEmbeddedProjectAssets) {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_RefreshMutex);
        ++m_LibraryRefreshGeneration;
        generation = m_LibraryRefreshGeneration;
        m_LibraryRefreshSnapshot = {};
        m_LibraryRefreshSnapshot.generation = generation;
        m_LibraryRefreshSnapshot.state = Async::TaskState::Queued;
        m_LibraryRefreshSnapshot.statusText = "Scanning library...";
    }

    const std::filesystem::path libraryPath = m_LibraryPath;
    const std::filesystem::path assetsPath = m_AssetsPath;

    const bool submitted = Async::TaskSystem::Get().Submit(MakeActivityMetadata("Refreshing library", true), [this, generation, libraryPath, assetsPath, syncEmbeddedProjectAssets]() mutable {
        LibraryScanResult result;
        (void)syncEmbeddedProjectAssets;

        auto updateProgress = [&](int current, int total, const std::string& item) {
            std::lock_guard<std::mutex> lock(m_RefreshMutex);
            if (generation != m_LibraryRefreshGeneration) {
                return;
            }
            m_LibraryRefreshSnapshot.state = Async::TaskState::Running;
            m_LibraryRefreshSnapshot.current = current;
            m_LibraryRefreshSnapshot.total = total;
            m_LibraryRefreshSnapshot.currentItem = item;
            m_LibraryRefreshSnapshot.statusText = item.empty()
                ? std::string("Scanning library...")
                : ("Scanning " + item);
        };

        try {
            std::error_code ec;
            std::filesystem::create_directories(libraryPath, ec);
            ec.clear();
            std::filesystem::create_directories(assetsPath, ec);

            Stack::Project::ProjectIndex::Get().RebuildDefaultRoots();
            const std::vector<Stack::Project::ProjectRecord> indexedProjects =
                Stack::Project::ProjectIndex::Get().Snapshot();
            std::vector<std::filesystem::path> assetMetadataFiles;

            ec.clear();
            if (std::filesystem::exists(assetsPath, ec) && !ec) {
                for (const auto& entry : std::filesystem::directory_iterator(assetsPath, ec)) {
                    if (ec) {
                        break;
                    }
                    if (IsSupportedAssetMetadataExtension(entry.path())) {
                        assetMetadataFiles.push_back(entry.path());
                    }
                }
            }

            result.totalItems = static_cast<int>(indexedProjects.size() + assetMetadataFiles.size());
            int currentItem = 0;
            updateProgress(currentItem, result.totalItems, "projects");

            for (const Stack::Project::ProjectRecord& record : indexedProjects) {
                ++currentItem;
                updateProgress(currentItem, result.totalItems, record.displayName);

                auto project = std::make_shared<ProjectEntry>();
                project->projectId = record.projectId;
                project->fileName = record.absolutePath.string();
                project->absolutePath = record.absolutePath;
                project->projectName = record.displayName.empty()
                    ? record.absolutePath.stem().string()
                    : record.displayName;
                project->timestamp = record.timestamp.empty() ? "Unknown" : record.timestamp;
                project->projectKind = record.projectKind;
                project->thumbnailBytes = record.coverThumbnailBytes;
                project->sourceWidth = record.sourceWidth > 0 ? record.sourceWidth : 4;
                project->sourceHeight = record.sourceHeight > 0 ? record.sourceHeight : 3;
                project->needsAttention = record.needsAttention;
                project->readOnlyRecovery = record.readOnlyRecovery;
                project->errorMessage = record.errorMessage;
                result.projects.push_back(project);
            }

            SortProjectsNewestFirst(result.projects);

            for (const std::filesystem::path& path : assetMetadataFiles) {
                ++currentItem;
                updateProgress(currentItem, result.totalItems, path.filename().string());

                try {
                    std::ifstream file(path);
                    if (!file.is_open()) {
                        continue;
                    }

                    nlohmann::json meta;
                    file >> meta;

                    const std::string filename = path.filename().string();
                    const std::string assetPngName = filename.substr(0, filename.size() - 5);
                    const std::filesystem::path pngPath = assetsPath / assetPngName;
                    if (!std::filesystem::exists(pngPath)) {
                        continue;
                    }

                    auto asset = std::make_shared<AssetEntry>();
                    asset->fileName = assetPngName;
                    asset->displayName = meta.value("displayName", assetPngName);
                    asset->projectFileName = meta.value("projectFileName", "");
                    asset->timestamp = meta.value("timestamp", "Unknown");
                    asset->width = meta.value("width", 0);
                    asset->height = meta.value("height", 0);

                    const auto projectIt = std::find_if(
                        result.projects.begin(),
                        result.projects.end(),
                        [&](const std::shared_ptr<ProjectEntry>& project) {
                            return project && !asset->projectFileName.empty() && project->fileName == asset->projectFileName;
                        });
                    if (projectIt != result.projects.end() && *projectIt) {
                        asset->projectName = (*projectIt)->projectName;
                        asset->projectKind = (*projectIt)->projectKind;
                    }

                    result.assets.push_back(asset);
                } catch (...) {
                    continue;
                }
            }

            result.signature = BuildLibrarySignature();
        } catch (const std::exception& error) {
            result.success = false;
            result.errorMessage = error.what();
        } catch (...) {
            result.success = false;
            result.errorMessage = "Failed to scan library.";
        }

        Async::TaskSystem::Get().PostToMain([this, generation, result = std::move(result)]() mutable {
            {
                std::lock_guard<std::mutex> lock(m_RefreshMutex);
                if (generation != m_LibraryRefreshGeneration) {
                    return;
                }
                m_LibraryRefreshSnapshot.state = Async::TaskState::Applying;
                m_LibraryRefreshSnapshot.statusText = "Applying library scan...";
            }

            if (!result.success) {
                std::lock_guard<std::mutex> lock(m_RefreshMutex);
                m_LibraryRefreshSnapshot.state = Async::TaskState::Failed;
                m_LibraryRefreshSnapshot.statusText = result.errorMessage.empty()
                    ? "Failed to scan library."
                    : result.errorMessage;
                Stack::Notifications::NoticeSpec notice;
                notice.title = "Library refresh failed";
                notice.message = "The previous Library view is still available.";
                notice.details = m_LibraryRefreshSnapshot.statusText;
                notice.severity = Stack::Notifications::Severity::Error;
                notice.dedupeKey = "library-refresh";
                Stack::Notifications::ActionSpec retry;
                retry.label = "Retry";
                retry.resolveOnSuccess = false;
                retry.canInvoke = [this] { return !IsRefreshBusy(); };
                retry.invoke = [this] {
                    RequestRefreshLibraryAsync();
                    return Stack::Notifications::ActionResult::Success();
                };
                notice.actions.push_back(std::move(retry));
                m_RefreshProblem = m_Notifier.Post(std::move(notice));
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_ProjectsMutex);
                for (auto& project : m_Projects) {
                    ReleaseProjectTextures(project);
                }
                for (auto& asset : m_Assets) {
                    ReleaseAssetTextures(asset);
                }
                m_Projects = std::move(result.projects);
                m_Assets = std::move(result.assets);
                m_LastLibrarySignature = result.signature;
                m_ProjectThumbnailWarmupCursor = 0;
                m_AssetThumbnailWarmupCursor = 0;
                m_ProjectThumbnailPriority.clear();
                m_AssetThumbnailPriority.clear();
            }

            std::lock_guard<std::mutex> lock(m_RefreshMutex);
            m_LibraryRefreshSnapshot.state = Async::TaskState::Idle;
            m_LibraryRefreshSnapshot.current = result.totalItems;
            m_LibraryRefreshSnapshot.total = result.totalItems;
            m_LibraryRefreshSnapshot.projectCount = static_cast<int>(m_Projects.size());
            m_LibraryRefreshSnapshot.assetCount = static_cast<int>(m_Assets.size());
            m_LibraryRefreshSnapshot.currentItem.clear();
            m_LibraryRefreshSnapshot.statusText = "Library ready.";
            if (m_RefreshProblem) {
                m_Notifier.Resolve(m_RefreshProblem);
                m_RefreshProblem = 0;
            }
        });
    });
    if (!submitted) {
        std::lock_guard<std::mutex> lock(m_RefreshMutex);
        if (generation != m_LibraryRefreshGeneration) return;
        m_LibraryRefreshSnapshot.state = Async::TaskState::Failed;
        m_LibraryRefreshSnapshot.statusText = "The Library refresh could not be started.";
        m_RefreshProblem = m_Notifier.Error(m_LibraryRefreshSnapshot.statusText);
    }
}

void LibraryManager::CancelLibraryRefreshRequests() {
    {
        std::lock_guard<std::mutex> lock(m_RefreshMutex);
        ++m_LibraryRefreshGeneration;
        m_LibraryRefreshSnapshot.state = Async::TaskState::Idle;
        m_LibraryRefreshSnapshot.currentItem.clear();
        m_LibraryRefreshSnapshot.statusText.clear();
    }

    {
        std::lock_guard<std::mutex> lock(m_SignatureMutex);
        ++m_LibrarySignatureGeneration;
        m_LibrarySignatureTaskState = Async::TaskState::Idle;
    }
}

LibraryAutoRefreshStats LibraryManager::TickAutoRefresh() {
    LibraryAutoRefreshStats stats;
    const auto startedAt = std::chrono::steady_clock::now();
    ProcessDeferredDeletions();
    if (IsRefreshBusy() ||
        Async::IsBusy(m_ProjectLoadTaskState) ||
        Async::IsBusy(m_SaveTaskState) ||
        Async::IsBusy(m_ImportTaskState) ||
        Async::IsBusy(m_ExportTaskState)) {
        stats.skippedForBusyWork = true;
        stats.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
        return stats;
    }

    const auto now = std::chrono::steady_clock::now();
    if (m_LastAutoRefreshSignatureCheck.time_since_epoch().count() != 0 &&
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastAutoRefreshSignatureCheck).count() < 1500) {
        stats.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
        return stats;
    }
    m_LastAutoRefreshSignatureCheck = now;

    if (HasPendingThumbnailWarmup()) {
        stats.skippedForWarmup = true;
        stats.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
        return stats;
    }

    {
        std::lock_guard<std::mutex> signatureLock(m_SignatureMutex);
        if (Async::IsBusy(m_LibrarySignatureTaskState)) {
            stats.signatureBusy = true;
            stats.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
            return stats;
        }
    }

    RequestLibrarySignatureAsync();
    stats.requestedSignature = true;
    stats.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - startedAt).count();
    return stats;
}

void LibraryManager::RequestLibrarySignatureAsync() {
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(m_SignatureMutex);
        if (Async::IsBusy(m_LibrarySignatureTaskState)) {
            return;
        }
        ++m_LibrarySignatureGeneration;
        generation = m_LibrarySignatureGeneration;
        m_LibrarySignatureTaskState = Async::TaskState::Queued;
    }

    Async::TaskSystem::Get().Submit(MakeActivityMetadata("Checking library", true), [this, generation]() {
        {
            std::lock_guard<std::mutex> lock(m_SignatureMutex);
            if (generation != m_LibrarySignatureGeneration) {
                return;
            }
            m_LibrarySignatureTaskState = Async::TaskState::Running;
        }

        const std::uintmax_t latestSignature = BuildLibrarySignature();

        Async::TaskSystem::Get().PostToMain([this, generation, latestSignature]() {
            bool shouldRefresh = false;
            {
                std::lock_guard<std::mutex> lock(m_SignatureMutex);
                if (generation != m_LibrarySignatureGeneration) {
                    return;
                }
                m_LibrarySignatureTaskState = Async::TaskState::Idle;
                shouldRefresh = latestSignature != m_LastLibrarySignature;
            }

            if (shouldRefresh) {
                RequestRefreshLibraryAsync();
            }
        });
    });
}
