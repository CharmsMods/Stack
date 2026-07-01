#include "LibraryManager.h"
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
        ++fileCount;
        totalSize += std::filesystem::file_size(entry.path(), ec);
        if (ec) ec.clear();

        const auto writeTime = std::filesystem::last_write_time(entry.path(), ec);
        if (!ec) {
            const auto stamp = static_cast<std::uintmax_t>(writeTime.time_since_epoch().count());
            if (stamp > latestWrite) latestWrite = stamp;
        }

        nameHash ^= static_cast<std::uintmax_t>(std::hash<std::string>{}(entry.path().string()));
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

    if (!std::filesystem::exists(m_LibraryPath)) {
        std::filesystem::create_directories(m_LibraryPath);
    }
    if (!std::filesystem::exists(m_AssetsPath)) {
        std::filesystem::create_directories(m_AssetsPath);
    }

    int totalItems = GetProjectCount();
    int currentItem = 0;
    TraceStartupStep("[LibraryManager] Refresh counts: " + std::to_string(totalItems));

    std::vector<std::string> activeAssetFiles;

    for (const auto& entry : std::filesystem::directory_iterator(m_LibraryPath)) {
        if (!IsSupportedProjectExtension(entry.path())) continue;

        currentItem++;
        TraceStartupStep("[LibraryManager] Loading project: " + entry.path().filename().string());
        if (progressCallback) progressCallback(currentItem, totalItems, entry.path().filename().string());

        StackFormat::ProjectLoadOptions options;
        options.includeThumbnail = true;
        options.includeSourceImage = syncEmbeddedProjectAssets;
        options.includePipelineData = syncEmbeddedProjectAssets;
        options.verifyChecksum = syncEmbeddedProjectAssets;

        StackFormat::ProjectDocument document;
        if (!LoadProjectDocument(entry.path().filename().string(), document, options)) {
            std::cerr << "Failed to parse project: " << entry.path() << std::endl;
            continue;
        }

        auto project = std::make_shared<ProjectEntry>();
        project->fileName = entry.path().filename().string();
        project->projectName = document.metadata.projectName;
        project->timestamp = document.metadata.timestamp;
        project->projectKind = document.metadata.projectKind;
        project->thumbnailBytes = std::move(document.thumbnailBytes);
        project->sourceWidth = document.metadata.sourceWidth;
        project->sourceHeight = document.metadata.sourceHeight;

        m_Projects.push_back(project);

        if (syncEmbeddedProjectAssets) {
            // Full asset sync is intentionally opt-in; startup only needs metadata and thumbnails.
            std::vector<std::string> projectAssets = SyncProjectAssets(project->fileName, document);
            activeAssetFiles.insert(activeAssetFiles.end(), projectAssets.begin(), projectAssets.end());
        }
    }

    if (syncEmbeddedProjectAssets) {
        CleanupOrphanedAssets(activeAssetFiles);
    }

    // Populate m_Assets directly from the synced .hash files
    if (std::filesystem::exists(m_AssetsPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(m_AssetsPath)) {
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

    Async::TaskSystem::Get().Submit([this, generation, libraryPath, assetsPath, syncEmbeddedProjectAssets]() mutable {
        LibraryScanResult result;
        std::vector<std::string> activeAssetFiles;

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

            std::vector<std::filesystem::path> projectFiles;
            std::vector<std::filesystem::path> assetMetadataFiles;

            ec.clear();
            if (std::filesystem::exists(libraryPath, ec) && !ec) {
                for (const auto& entry : std::filesystem::directory_iterator(libraryPath, ec)) {
                    if (ec) {
                        break;
                    }
                    if (IsSupportedProjectExtension(entry.path())) {
                        projectFiles.push_back(entry.path());
                    }
                }
            }

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

            result.totalItems = static_cast<int>(projectFiles.size() + assetMetadataFiles.size());
            int currentItem = 0;
            updateProgress(currentItem, result.totalItems, "projects");

            for (const std::filesystem::path& path : projectFiles) {
                ++currentItem;
                updateProgress(currentItem, result.totalItems, path.filename().string());

                StackFormat::ProjectLoadOptions options;
                options.includeThumbnail = true;
                options.includeSourceImage = syncEmbeddedProjectAssets;
                options.includePipelineData = syncEmbeddedProjectAssets;
                options.verifyChecksum = syncEmbeddedProjectAssets;

                StackFormat::ProjectDocument document;
                if (!LoadProjectDocument(path.filename().string(), document, options)) {
                    continue;
                }

                auto project = std::make_shared<ProjectEntry>();
                project->fileName = path.filename().string();
                project->projectName = document.metadata.projectName;
                project->timestamp = document.metadata.timestamp;
                project->projectKind = document.metadata.projectKind;
                project->thumbnailBytes = std::move(document.thumbnailBytes);
                project->sourceWidth = document.metadata.sourceWidth;
                project->sourceHeight = document.metadata.sourceHeight;
                result.projects.push_back(project);

                if (syncEmbeddedProjectAssets) {
                    std::vector<std::string> projectAssets = SyncProjectAssets(project->fileName, document);
                    activeAssetFiles.insert(activeAssetFiles.end(), projectAssets.begin(), projectAssets.end());
                }
            }

            if (syncEmbeddedProjectAssets) {
                CleanupOrphanedAssets(activeAssetFiles);

                assetMetadataFiles.clear();
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
                result.totalItems = static_cast<int>(projectFiles.size() + assetMetadataFiles.size());
            }

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
        });
    });
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

    Async::TaskSystem::Get().Submit([this, generation]() {
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
