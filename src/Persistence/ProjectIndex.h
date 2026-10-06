#pragma once

#include "Persistence/RawProjectModel.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace Stack::Project {

enum class IndexedProjectFormat {
    CurrentBundle,
    PackedPortable,
    NeedsAttention
};

enum class IndexedCoverState {
    Ready,
    Missing,
    Invalid
};

struct IndexedProjectSource {
    std::filesystem::path originalPath;
    std::filesystem::path workspaceRelativePath;
    std::string fingerprint;
    std::string contentSha256;
    std::uint64_t byteLength = 0;
    std::string assetId;
};

struct ProjectRecord {
    std::string projectId;
    std::filesystem::path absolutePath;
    std::string displayName;
    std::string projectKind;
    std::string timestamp;
    int sourceWidth = 0;
    int sourceHeight = 0;
    IndexedProjectFormat format = IndexedProjectFormat::NeedsAttention;
    ProjectStorageKind storageKind = ProjectStorageKind::DirectoryBundle;
    IndexedCoverState coverState = IndexedCoverState::Missing;
    std::vector<unsigned char> coverThumbnailBytes;
    std::vector<IndexedProjectSource> sources;
    std::vector<MultiFrameSourceSet> sourceSets;
    bool hasRawWorkspaceData = false;
    bool hasRawWorkspaceRecipe = false;
    std::string rawWorkspaceMode;
    std::string rawWorkspaceReadOnlyReason;
    std::string rawSourceRelativePathKey;
    std::string rawSourceFingerprint;
    std::uint64_t rawSourceFileSizeBytes = 0;
    std::int64_t rawSourceModifiedTimeTicks = 0;
    bool rawSourceLinked = true;
    bool rawSourceEmbedded = false;
    bool multiFrameProject = false;
    std::uint64_t indexSignatureSize = 0;
    std::int64_t indexSignatureModifiedTimeTicks = 0;
    std::uint64_t editRevision = 0;
    std::uint64_t storageRevision = 0;
    bool readOnlyRecovery = false;
    bool needsAttention = false;
    std::string errorMessage;
};

class ProjectIndex {
public:
    static ProjectIndex& Get();

    // Return this scan's records so another consumer's rebuild cannot replace
    // the results between a caller's scan and its workspace filtering.
    std::vector<ProjectRecord> Rebuild(const std::vector<std::filesystem::path>& roots,
        bool recursive = false);
    void RebuildDefaultRoots();
    std::vector<ProjectRecord> Snapshot() const;
    // Return only records whose project bundle lives directly under (or
    // below) the supplied root. This keeps workspace-scoped consumers from
    // treating the application-wide index as their project population.
    std::vector<ProjectRecord> SnapshotForRoot(
        const std::filesystem::path& root) const;
    bool FindByPath(
        const std::filesystem::path& path,
        ProjectRecord& outRecord) const;

    bool CloneVersion(
        const std::filesystem::path& sourceProject,
        const std::filesystem::path& destinationRoot,
        std::filesystem::path& outProjectPath,
        std::string* errorMessage = nullptr);

    static std::vector<std::filesystem::path> DefaultRoots();
    static std::filesystem::path BuildUniqueProjectPath(
        const std::filesystem::path& root,
        const std::string& displayName,
        const std::string& projectId);

private:
    ProjectIndex() = default;

    mutable std::mutex m_RebuildMutex;
    mutable std::mutex m_Mutex;
    std::vector<ProjectRecord> m_Records;
};

} // namespace Stack::Project
