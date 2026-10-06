#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include "Persistence/ProjectStore.h"
#include "ThirdParty/json.hpp"

namespace StackBinaryFormat {

using json = nlohmann::json;

inline constexpr const char* kEditorProjectKind = "editor";
inline constexpr const char* kRawProjectKind = "raw";
inline constexpr const char* kRenderProjectKind = "render";
inline constexpr const char* kCompositeProjectKind = "composite";

enum class FileKind : std::uint16_t {
    Project = 1,
    LibraryBundle = 2,
    NodePreset = 3
};

struct ProjectLoadOptions {
    bool includeThumbnail = true;
    bool includeSourceImage = true;
    bool includePipelineData = true;
    bool includeNodeBrowserThumbnails = true;
    bool includeRawWorkspaceData = true;
    bool verifyChecksum = true;
};

struct ProjectMetadata {
    std::string projectKind = kEditorProjectKind;
    std::string projectName;
    std::string timestamp;
    int sourceWidth = 0;
    int sourceHeight = 0;
};

struct NodeBrowserThumbnailEntry {
    std::string previewKey;
    std::string previewSeedHash;
    std::uint32_t previewRecipeVersion = 0;
    std::vector<unsigned char> pngBytes;
};

struct NodePresetBoundarySocket {
    std::string nodeTitle;
    std::string socketLabel;
    std::string direction;
    std::string type;
};

struct NodePresetMetadata {
    std::string id;
    std::string displayName;
    std::string timestamp;
    std::string savedWithVersion;
    std::uint32_t presetVersion = 1;
    std::uint32_t nodeCount = 0;
    std::uint32_t inputCount = 0;
    std::uint32_t outputCount = 0;
};

struct NodePresetDocument {
    NodePresetMetadata metadata;
    std::vector<unsigned char> thumbnailBytes;
    json graphPayload = json();
    std::vector<NodePresetBoundarySocket> boundarySockets;
};

struct NodePresetLoadOptions {
    bool includeThumbnail = true;
    bool includeGraphPayload = true;
    bool includeBoundarySockets = true;
    bool verifyChecksum = true;
};

struct ProjectDocument {
    std::string projectId;
    std::filesystem::path adoptedFrom;
    ProjectMetadata metadata;
    std::vector<unsigned char> thumbnailBytes;
    std::vector<unsigned char> sourceImageBytes;
    json pipelineData = json();
    std::vector<NodeBrowserThumbnailEntry> nodeBrowserThumbnailEntries;
    json rawWorkspaceData = json();
    // Current projects of every workflow use the same lazy managed-asset
    // store. sourceImageBytes is an in-memory capture surface, not a second
    // authoritative copy in project.stack.
    Stack::Project::ProjectStoreHandle projectStore;
    std::shared_ptr<Stack::Project::RawProjectSnapshot> rawProjectSnapshot;
};

struct BundledProjectDocument {
    std::string fileName;
    ProjectDocument project;
};

struct AssetDocument {
    std::string fileName;
    std::string displayName;
    std::string timestamp;
    std::string projectFileName;
    std::string projectName;
    int width = 0;
    int height = 0;
    std::vector<unsigned char> imageBytes;
};

struct LibraryBundleDocument {
    std::string bundleName = "Modular Studio Library";
    std::string timestamp;
    std::vector<BundledProjectDocument> projects;
    std::vector<AssetDocument> assets;
};

// Missing-store recovery must claim a new destination instead of overwriting
// a replacement project created after the save check.
struct ProjectWriteResult {
    Stack::Project::ProjectStoreHandle store;
    Stack::Project::RawProjectSnapshot snapshot;
    Stack::Project::ProjectStoreCommitResult commit;

    explicit operator bool() const { return store && static_cast<bool>(commit); }
};

// A supplied baseline belongs to the captured document. Zero claims a new
// store; a nonzero value must match storage before publishing any changes.
ProjectWriteResult WriteProjectFileWithResult(
    const std::filesystem::path& path,
    const ProjectDocument& document,
    bool requireNewStore = false,
    std::optional<std::uint64_t> expectedStorageRevision = std::nullopt);
bool WriteProjectFile(
    const std::filesystem::path& path,
    const ProjectDocument& document,
    bool requireNewStore = false);
bool ReadProjectFile(const std::filesystem::path& path, ProjectDocument& document, const ProjectLoadOptions& options = {});

// Rewrites in-document image payloads to managed asset references while the
// caller's store transaction is active. This is shared by every project
// workflow so Graph, RAW, burst, and HDR commits obey the same asset rules.
bool ExternalizeManagedProjectAssets(
    const Stack::Project::ProjectStoreHandle& store,
    const Stack::Project::ProjectStoreTransaction& transaction,
    Stack::Project::RawProjectSnapshot& snapshot);

bool WriteNodePresetFile(const std::filesystem::path& path, const NodePresetDocument& document);
bool ReadNodePresetFile(const std::filesystem::path& path, NodePresetDocument& document, const NodePresetLoadOptions& options = {});

bool WriteLibraryBundle(const std::filesystem::path& path, const LibraryBundleDocument& document);
bool ReadLibraryBundle(const std::filesystem::path& path, LibraryBundleDocument& document);

bool AreProjectsIdentical(const ProjectDocument& a, const ProjectDocument& b);

} // namespace StackBinaryFormat
