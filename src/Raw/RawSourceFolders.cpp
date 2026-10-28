#include "Raw/RawSourceFolders.h"
#include "Persistence/ManagedAssetPaths.h"
#include "Persistence/ProjectStore.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sys/stat.h>
#endif

namespace Stack::RawWorkspace {
namespace {

std::filesystem::path NormalizeFolder(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    return (error ? path : absolute).lexically_normal();
}

std::string QualifiedSourceKey(const std::string& root, const std::string& source) {
    return "@folder:" + std::to_string(root.size()) + ":" + root + ":" + source;
}

std::string QualifiedProjectKey(const std::string& path, const std::string& nativeId) {
    return "@project:" + std::to_string(path.size()) + ":" + path + ":" + nativeId;
}

std::string FileIdentityKey(const std::filesystem::path& path) {
    // Alias deduplication is a browse decision, not permission for a write.
    // Fall back to the canonical path when a filesystem cannot supply IDs.
#if defined(_WIN32)
    const HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        BY_HANDLE_FILE_INFORMATION info{};
        const bool found = GetFileInformationByHandle(file, &info) != FALSE;
        CloseHandle(file);
        if (found && (info.nFileIndexHigh || info.nFileIndexLow))
            return "file:" + std::to_string(info.dwVolumeSerialNumber) + ":" +
            std::to_string((static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) |
                info.nFileIndexLow);
    }
#else
    struct stat info{};
    if (::stat(path.c_str(), &info) == 0 && info.st_ino != 0)
        return "file:" + std::to_string(static_cast<std::uint64_t>(info.st_dev)) +
            ":" + std::to_string(static_cast<std::uint64_t>(info.st_ino));
#endif
    return "path:" + SourceFolderPathKey(path);
}

bool IsWithin(const std::string& path, const std::string& root) {
    return path == root || (!root.empty() && path.size() > root.size() &&
        path.compare(0, root.size(), root) == 0 &&
        (root.back() == '/' || path[root.size()] == '/'));
}

void MergeMemberships(SourceRecord& target, const SourceRecord& incoming) {
    for (const auto& membership : incoming.sourceSetProjectMemberships) {
        const auto key = SourceFolderPathKey(membership.projectPath);
        const bool exists = std::any_of(target.sourceSetProjectMemberships.begin(),
            target.sourceSetProjectMemberships.end(), [&](const auto& existing) {
                return existing.sourceSetId == membership.sourceSetId &&
                    SourceFolderPathKey(existing.projectPath) == key;
            });
        if (!exists) target.sourceSetProjectMemberships.push_back(membership);
    }
}

} // namespace

std::string SourceFolderPathKey(const std::filesystem::path& path) {
    if (path.empty()) return {};
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    auto normalized = error ? NormalizeFolder(path) : canonical.lexically_normal();
#if defined(_WIN32)
    auto key = normalized.generic_wstring();
    // Use Windows' invariant Unicode mapping, not the process C locale. Paths
    // can contain non-ASCII case variants on ordinary public installations.
    if (key.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        const int length = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
            key.data(), static_cast<int>(key.size()), nullptr, 0, nullptr, nullptr, 0);
        if (length > 0) {
            std::wstring folded(static_cast<std::size_t>(length), L'\0');
            if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                    key.data(), static_cast<int>(key.size()), folded.data(), length,
                    nullptr, nullptr, 0) == length) key = std::move(folded);
        }
    }
    return std::filesystem::path(key).generic_u8string();
#else
    return normalized.generic_u8string();
#endif
}

std::vector<std::filesystem::path> NormalizeSourceFolders(
    const std::vector<std::filesystem::path>& folders) {
    std::vector<std::filesystem::path> result;
    std::unordered_set<std::string> seen;
    for (const auto& folder : folders) {
        if (folder.empty()) continue;
        const auto normalized = NormalizeFolder(folder);
        if (seen.insert(SourceFolderPathKey(normalized)).second)
            result.push_back(normalized);
    }
    return result;
}

std::filesystem::path ResolveSourceWorkspaceRoot(
    const SourceRecord& source, const std::filesystem::path& fallback) {
    return source.sourceWorkspaceRoot.empty() ? fallback : source.sourceWorkspaceRoot;
}

SourceRecord NativeSourceRecord(const SourceRecord& source) {
    auto native = source;
    if (!native.sourceRelativePathKey.empty()) {
        native.relativePathKey = native.sourceRelativePathKey;
        native.parentFolderKey = native.relativePath.has_parent_path()
            ? native.relativePath.parent_path().generic_u8string() : std::string();
        for (auto& membership : native.sourceSetProjectMemberships) {
            if (!membership.nativeProjectId.empty() ||
                membership.projectId.rfind("@project:", 0) == 0) {
                membership.projectId = membership.nativeProjectId;
                membership.nativeProjectId.clear();
            }
        }
        native.sourceRelativePathKey.clear();
    }
    return native;
}

std::filesystem::path ResolveManagedProjectSourceFolder(
    const std::filesystem::path& projectPath) try {
    if (projectPath.empty()) return {};
    const auto store = Stack::Project::ResolveProjectStoreRoot(projectPath);
    if (!Stack::Project::IsDirectoryProjectBundle(store)) return {};
    std::error_code error;
    const auto canonicalStore = std::filesystem::weakly_canonical(store, error);
    if (error) return {};
    const auto assets = Stack::Project::ClosetAssetRoot(canonicalStore);
    if (assets.empty()) return {};
    const auto closet = assets.parent_path();
    // ClosetAssetRoot establishes the marker's identity. Its current schema
    // and the Projects subtree additionally establish this managed owner.
    std::ifstream input(closet / "workspace.json", std::ios::binary);
    const auto marker = nlohmann::json::parse(input, nullptr, false);
    if (!marker.is_object() || !marker.contains("schema") ||
        !marker["schema"].is_string() || marker["schema"] != "stack.rawWorkspace.closet" ||
        !marker.contains("schemaVersion") ||
        !marker["schemaVersion"].is_number_integer() ||
        marker["schemaVersion"].get<int>() != 1) return {};
    const auto projectsKey = SourceFolderPathKey(closet / "Projects");
    const auto storeKey = SourceFolderPathKey(canonicalStore);
    if (storeKey == projectsKey || !IsWithin(storeKey, projectsKey)) return {};
    return closet.parent_path();
} catch (...) {
    // A malformed marker or an inaccessible ancestor establishes no owner.
    return {};
}

ScanResult AggregateSourceFolders(
    const std::vector<std::filesystem::path>& folders,
    RawPathPredicate isRawPath,
    ScanProgressCallback progress,
    CancellationPredicate shouldCancel) {
    ScanResult aggregate;
    const auto roots = NormalizeSourceFolders(folders);
    const auto cancelled = [&] { return shouldCancel && shouldCancel(); };
    const auto cancelResult = [&] {
        ScanResult result;
        result.errorMessage = "Source folder scan canceled.";
        result.progress.stage = ScanProgress::Stage::Failed;
        result.progress.statusText = result.errorMessage;
        return result;
    };
    std::vector<std::string> managedRoots;
    for (const auto& root : roots)
        managedRoots.push_back(SourceFolderPathKey(BuildManagedLayout(root).dataDirectory));
    const auto acceptsSource = [&](const std::filesystem::path& path) {
        const auto pathKey = SourceFolderPathKey(path);
        for (const auto& managed : managedRoots)
            if (IsWithin(pathKey, managed)) return false;
        return isRawPath ? isRawPath(path) : DefaultRawPathPredicate(path);
    };
    std::unordered_map<std::string, std::size_t> sourceIdentities;
    std::unordered_set<std::string> projectPaths;
    std::size_t scanned = 0;
    for (const auto& root : roots) {
        if (cancelled()) return cancelResult();
        const auto rootKey = SourceFolderPathKey(root);
        const auto layout = BuildManagedLayout(root);
        std::vector<SourceRecord> cached;
        LoadCatalogSnapshot(layout, cached);
        std::unordered_map<std::string, const SourceRecord*> reusable;
        for (const auto& source : cached) reusable.emplace(source.relativePathKey, &source);
        const auto reuseIdentity = [&](SourceRecord& source) {
            const auto found = reusable.find(source.relativePathKey);
            if (found == reusable.end()) return false;
            const auto& previous = *found->second;
            if (previous.fileSizeBytes != source.fileSizeBytes ||
                previous.modifiedTimeTicks != source.modifiedTimeTicks ||
                previous.fingerprint.empty() || previous.sourceIdentityAlgorithmVersion !=
                    kSourceIdentityAlgorithmVersion) return false;
            source.fingerprint = previous.fingerprint;
            source.sourceIdentityAlgorithmVersion = previous.sourceIdentityAlgorithmVersion;
            source.captureTimestamp = previous.captureTimestamp;
            source.captureMetadataChecked = previous.captureMetadataChecked;
            return true;
        };
        const auto report = [&](const ScanProgress& folderProgress) {
            if (!progress) return;
            auto combined = folderProgress;
            combined.discoveredRawCount += static_cast<int>(aggregate.sources.size());
            combined.statusText = "Folder " + std::to_string(scanned + 1) + " of " +
                std::to_string(roots.size()) + ": " + folderProgress.statusText;
            progress(combined);
        };
        auto result = ScanWorkspace(root, acceptsSource, report, shouldCancel,
            [](const ManagedLayout& owner, SourceRecord& source, ScanProgress& state) {
                ClassifyThumbnailMetadata(owner, source);
                ++state.cachedPreviewsChecked;
            }, reuseIdentity);
        ++scanned;
        if (cancelled()) return cancelResult();
        if (result.success && !DiscoverWorkspaceProjects(result.layout,
                result.sources, result.sourceSetProjects, shouldCancel)) {
            result.success = false;
            result.errorMessage = "Could not discover saved projects.";
        }
        if (cancelled()) return cancelResult();
        if (!result.success) {
            if (!aggregate.errorMessage.empty()) aggregate.errorMessage += "\n";
            aggregate.errorMessage += root.u8string() + ": " + result.errorMessage;
            continue;
        }
        aggregate.success = true;
        SourceFolderCard card;
        card.root = result.layout.workspaceRoot;
        card.countKnown = true;
        card.sourceCount = result.sources.size();
        for (std::size_t index = 0; index < std::min<std::size_t>(4, result.sources.size()); ++index) {
            auto preview = result.sources[index];
            preview.sourceWorkspaceRoot = result.layout.workspaceRoot;
            card.previews.push_back(std::move(preview));
        }
        aggregate.sourceFolderCards.push_back(std::move(card));
        std::unordered_map<std::string, std::string> displayKeys;
        for (auto& source : result.sources) {
            const auto localKey = source.relativePathKey;
            const auto identity = FileIdentityKey(source.absolutePath);
            const auto existing = sourceIdentities.find(identity);
            if (existing != sourceIdentities.end()) {
                auto& first = aggregate.sources[existing->second];
                displayKeys.emplace(localKey, first.relativePathKey);
                MergeMemberships(first, source);
                continue;
            }
            source.sourceWorkspaceRoot = result.layout.workspaceRoot;
            source.sourceRelativePathKey = localKey;
            source.relativePathKey = QualifiedSourceKey(rootKey, localKey);
            displayKeys.emplace(localKey, source.relativePathKey);
            // Folder grouping remains a presentation-only absolute scope.
            // Join as paths so drive roots and roots with a trailing separator
            // use the same key as the navigator's reconstructed folder tree.
            source.parentFolderKey = (source.parentFolderKey.empty()
                ? result.layout.workspaceRoot
                : result.layout.workspaceRoot / std::filesystem::u8path(source.parentFolderKey))
                .generic_u8string();
            sourceIdentities.emplace(identity, aggregate.sources.size());
            aggregate.sources.push_back(std::move(source));
        }
        for (auto& project : result.sourceSetProjects) {
            if (!projectPaths.insert(SourceFolderPathKey(project.absolutePath)).second) continue;
            project.sourceWorkspaceRoot = ResolveManagedProjectSourceFolder(project.absolutePath);
            if (project.sourceWorkspaceRoot.empty()) project.sourceWorkspaceRoot = result.layout.workspaceRoot;
            for (auto& key : project.sourceKeys) {
                const auto display = displayKeys.find(key);
                if (display != displayKeys.end()) key = display->second;
            }
            const auto reference = displayKeys.find(project.referenceSourceKey);
            if (reference != displayKeys.end()) project.referenceSourceKey = reference->second;
            aggregate.sourceSetProjects.push_back(std::move(project));
        }
        aggregate.progress.directoriesVisited += result.progress.directoriesVisited;
        aggregate.progress.filesVisited += result.progress.filesVisited;
        aggregate.progress.managedDirectoriesSkipped += result.progress.managedDirectoriesSkipped;
        aggregate.progress.cachedPreviewsChecked += result.progress.cachedPreviewsChecked;
        aggregate.progress.reusedSourceIdentityCount += result.progress.reusedSourceIdentityCount;
        aggregate.progress.hashedRawCount += result.progress.hashedRawCount;
        aggregate.progress.verifiedRawBytes += result.progress.verifiedRawBytes;
    }
    // A copied Closet may legitimately retain authored document/source-set
    // IDs. The aggregate's maps, textures and widgets need a path-qualified
    // identity, but no document or native catalog is rewritten for browsing.
    std::unordered_map<std::string, std::string> projectDisplayIds;
    for (auto& project : aggregate.sourceSetProjects) {
        const auto pathKey = SourceFolderPathKey(project.absolutePath);
        project.nativeProjectId = project.projectId;
        project.projectId = QualifiedProjectKey(pathKey, project.nativeProjectId);
        projectDisplayIds.emplace(pathKey, project.projectId);
    }
    for (auto& source : aggregate.sources) {
        for (auto& membership : source.sourceSetProjectMemberships) {
            membership.nativeProjectId = membership.projectId;
            const auto pathKey = SourceFolderPathKey(membership.projectPath);
            const auto display = projectDisplayIds.find(pathKey);
            membership.projectId = display == projectDisplayIds.end()
                ? QualifiedProjectKey(pathKey.empty()
                    ? SourceFolderPathKey(source.sourceWorkspaceRoot) : pathKey,
                    membership.nativeProjectId)
                : display->second;
        }
    }
    aggregate.progress.discoveredRawCount = static_cast<int>(aggregate.sources.size());
    aggregate.progress.stage = aggregate.success ? ScanProgress::Stage::Complete : ScanProgress::Stage::Failed;
    aggregate.progress.statusText = aggregate.success
        ? (aggregate.errorMessage.empty() ? "All folders ready." : "Some source folders are unavailable.")
        : "No source folders could be scanned.";
    if (aggregate.errorMessage.empty() && !aggregate.success)
        aggregate.errorMessage = aggregate.progress.statusText;
    if (progress) progress(aggregate.progress);
    return aggregate;
}

} // namespace Stack::RawWorkspace
