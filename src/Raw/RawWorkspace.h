#pragma once

#include "ThirdParty/json.hpp"
#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawDevelopmentRecipe.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Stack::RawWorkspace {

using PersistenceCommitPredicate = std::function<bool()>;

constexpr const char* kThumbnailsFolderName = "Stack RAW Thumbnails";
constexpr const char* kProjectsFolderName = "Stack RAW Projects";
constexpr const char* kCatalogFolderName = "Stack RAW Catalog";
constexpr const char* kCatalogFileName = "catalog.json";
constexpr const char* kRatingsFileName = "ratings.json";
constexpr int kNeutralThumbnailMaxDimension = 320;

enum class ThumbnailStatus {
    Unknown,
    Missing,
    Stale,
    Valid,
    Queued,
    Generating,
    Ready,
    Failed
};

enum class GalleryDisplayMode {
    Grid,
    List
};

enum class GalleryPlacementMode {
    RightGallery,
    BottomFilmstrip
};

enum class ProjectStatus {
    Unknown,
    NoProject,
    Existing,
    MissingSource,
    Conflict,
    Embedded,
    Invalid
};

enum class RawProjectMode {
    Unknown,
    RecipeBacked,
    ManagedDecomposed,
    CustomGraph
};

struct ManagedLayout {
    std::filesystem::path workspaceRoot;
    std::filesystem::path thumbnailsDirectory;
    std::filesystem::path projectsDirectory;
    std::filesystem::path catalogDirectory;
    std::filesystem::path catalogPath;
    std::filesystem::path ratingsPath;
};

struct ThumbnailSignature {
    int schemaVersion = 1;
    std::string sourceRelativePath;
    std::uintmax_t sourceFileSizeBytes = 0;
    std::int64_t sourceModifiedTimeTicks = 0;
    std::string sourceFingerprint;
    int rawLoaderAlgorithmVersion = 1;
    int neutralPreviewSettingsVersion = 1;
    int thumbnailVersion = 1;
    int maxDimension = kNeutralThumbnailMaxDimension;
};

struct ThumbnailInfo {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    std::filesystem::path signaturePath;
    std::filesystem::path signatureRelativePath;
    ThumbnailStatus status = ThumbnailStatus::Unknown;
    int width = 0;
    int height = 0;
    std::string errorMessage;
};

struct ProjectInfo {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    ProjectStatus status = ProjectStatus::Unknown;
    RawProjectMode mode = RawProjectMode::RecipeBacked;
    std::string sourceRelativePathKey;
    std::string sourceFingerprint;
    std::uintmax_t sourceFileSizeBytes = 0;
    std::int64_t sourceModifiedTimeTicks = 0;
    std::int64_t projectModifiedTimeTicks = 0;
    bool linkedRaw = true;
    bool embeddedRaw = false;
    bool autosaved = false;
    bool dirty = false;
    std::string readOnlyReason;
    std::string associationReason;
    std::string errorMessage;
};

struct SourceRecord {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    std::string relativePathKey;
    std::string fileName;
    std::string stem;
    std::string extension;
    std::string parentFolderKey;
    std::uintmax_t fileSizeBytes = 0;
    std::int64_t modifiedTimeTicks = 0;
    std::string fingerprint;
    ThumbnailInfo thumbnail;
    ProjectInfo project;
};

struct CatalogThumbnailRecord {
    std::filesystem::path relativePath;
    std::filesystem::path signatureRelativePath;
    ThumbnailStatus status = ThumbnailStatus::Unknown;
    int width = 0;
    int height = 0;
    std::string errorMessage;
};

struct CatalogProjectRecord {
    std::filesystem::path relativePath;
    ProjectStatus status = ProjectStatus::Unknown;
    RawProjectMode mode = RawProjectMode::RecipeBacked;
    std::int64_t projectModifiedTimeTicks = 0;
    bool linkedRaw = true;
    bool embeddedRaw = false;
    std::string readOnlyReason;
    std::string associationReason;
    std::string errorMessage;
};

struct CatalogSourceRecord {
    std::filesystem::path absolutePath;
    std::string relativePathKey;
    std::string fileName;
    std::string stem;
    std::string extension;
    std::string parentFolderKey;
    std::uintmax_t fileSizeBytes = 0;
    std::int64_t modifiedTimeTicks = 0;
    std::string fingerprint;
    CatalogThumbnailRecord thumbnail;
    CatalogProjectRecord project;
};

struct ScanProgress {
    int directoriesVisited = 0;
    int filesVisited = 0;
    int managedDirectoriesSkipped = 0;
    int discoveredRawCount = 0;
    std::string currentItem;
    std::string statusText;
};

struct ScanResult {
    bool success = false;
    std::string errorMessage;
    ManagedLayout layout;
    std::vector<SourceRecord> sources;
    ScanProgress progress;
};

struct WorkspaceState {
    std::filesystem::path workspaceRoot;
    std::vector<std::filesystem::path> recentWorkspaceRoots;
    std::vector<SourceRecord> sources;
    std::string selectedSourceKey;
};

struct ThumbnailProgress {
    int total = 0;
    int valid = 0;
    int queued = 0;
    int completed = 0;
    int failed = 0;
    std::string currentItem;
    std::string statusText;
};

struct ThumbnailGenerationResult {
    bool success = false;
    ThumbnailInfo thumbnail;
    std::string errorMessage;
};

struct GallerySourceView {
    std::size_t sourceIndex = 0;
    std::string relativePathKey;
    std::string fileName;
    std::string folderKey;
    std::uintmax_t fileSizeBytes = 0;
    ThumbnailStatus thumbnailStatus = ThumbnailStatus::Unknown;
    std::filesystem::path thumbnailRelativePath;
    ProjectStatus projectStatus = ProjectStatus::Unknown;
    bool selected = false;
};

struct GalleryFolderGroup {
    std::string folderKey;
    std::string label;
    std::vector<GallerySourceView> sources;
};

struct GalleryPresentation {
    std::vector<GalleryFolderGroup> groups;
    int totalSources = 0;
    int readyThumbnailCount = 0;
    int queuedThumbnailCount = 0;
    int failedThumbnailCount = 0;
    bool hasSelection = false;
    std::string selectedSourceKey;
};

struct RawPanelState {
    bool hasSelection = false;
    bool hasProject = false;
    bool recipeControlsEditable = false;
    bool editCreatesProject = false;
    bool openGraphEnabled = false;
    RawProjectMode mode = RawProjectMode::RecipeBacked;
    ProjectStatus projectStatus = ProjectStatus::Unknown;
    std::string statusText;
    std::string graphTooltip;
    std::string readOnlyMessage;
};

struct AppState {
    std::filesystem::path lastWorkspaceRoot;
    std::string lastSelectedSourceKey;
    std::vector<std::filesystem::path> recentWorkspaceRoots;
    float controlsPanelWidth = 0.0f;
};

using RawPathPredicate = std::function<bool(const std::filesystem::path&)>;
using ScanProgressCallback = std::function<void(const ScanProgress&)>;
using CancellationPredicate = std::function<bool()>;

ManagedLayout BuildManagedLayout(const std::filesystem::path& workspaceRoot);
bool IsManagedFolderName(const std::string& folderName);
bool EnsureManagedFolders(const std::filesystem::path& workspaceRoot, std::string* outError = nullptr);
bool DefaultRawPathPredicate(const std::filesystem::path& path);
ScanResult ScanWorkspace(
    const std::filesystem::path& workspaceRoot,
    RawPathPredicate isRawPath = {},
    ScanProgressCallback progressCallback = {},
    CancellationPredicate shouldCancel = {});

bool SelectSourceByKey(WorkspaceState& state, const std::string& sourceKey);
void AddRecentWorkspace(WorkspaceState& state, const std::filesystem::path& workspaceRoot, std::size_t maxRecent = 8);
ThumbnailSignature BuildThumbnailSignature(const SourceRecord& source, int maxDimension = kNeutralThumbnailMaxDimension);
ThumbnailInfo BuildThumbnailInfo(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension = kNeutralThumbnailMaxDimension);
bool ThumbnailSignatureMatches(const ThumbnailSignature& expected, const nlohmann::json& actual);
ThumbnailStatus ClassifyThumbnail(
    const ManagedLayout& layout,
    SourceRecord& source,
    int maxDimension = kNeutralThumbnailMaxDimension);
bool ClassifyThumbnails(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    int maxDimension = kNeutralThumbnailMaxDimension,
    CancellationPredicate shouldCancel = {});
ThumbnailProgress BuildThumbnailProgress(const std::vector<SourceRecord>& sources);
ThumbnailGenerationResult GenerateNeutralThumbnail(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension = kNeutralThumbnailMaxDimension,
    CancellationPredicate shouldCancel = {});
GalleryPresentation BuildGalleryPresentation(const WorkspaceState& state);
RawPanelState BuildRawPanelState(const SourceRecord* source);
GalleryPlacementMode ResolveExclusiveGalleryPlacement(
    GalleryPlacementMode requested,
    GalleryPlacementMode fallback = GalleryPlacementMode::RightGallery);
const char* GalleryDisplayModeLabel(GalleryDisplayMode mode);
const char* GalleryPlacementModeLabel(GalleryPlacementMode mode);
const char* ProjectStatusLabel(ProjectStatus status);
const char* RawProjectModeLabel(RawProjectMode mode);
const char* ThumbnailStatusLabel(ThumbnailStatus status);
std::string ProjectStatusToString(ProjectStatus status);
ProjectStatus ProjectStatusFromString(const std::string& value);
std::string RawProjectModeToString(RawProjectMode mode);
RawProjectMode RawProjectModeFromString(const std::string& value);
std::string ThumbnailStatusToString(ThumbnailStatus status);
ThumbnailStatus ThumbnailStatusFromString(const std::string& value);

std::filesystem::path BuildProjectRelativePathForSource(const SourceRecord& source);
ProjectInfo BuildExpectedProjectInfo(const ManagedLayout& layout, const SourceRecord& source);
bool DiscoverProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    CancellationPredicate shouldCancel = {});
nlohmann::json BuildRawSourceRefJson(const SourceRecord& source, bool linkedRaw = true);
nlohmann::json BuildRawProjectData(
    const SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& downstreamGraph,
    RawProjectMode mode = RawProjectMode::RecipeBacked,
    bool linkedRaw = true);
bool ApplyRawWorkspaceDataToProjectDocument(
    const SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& downstreamGraph,
    StackBinaryFormat::ProjectDocument& document,
    RawProjectMode mode = RawProjectMode::RecipeBacked,
    bool linkedRaw = true);
bool ReadProjectInfoFromDocument(
    const StackBinaryFormat::ProjectDocument& document,
    ProjectInfo& outInfo,
    Stack::RawRecipe::RawDevelopmentRecipe* outRecipe = nullptr);
bool RelinkProjectDocumentToSource(
    const SourceRecord& source,
    StackBinaryFormat::ProjectDocument& document,
    std::string* outError = nullptr);
bool EmbedRawSourceInProjectDocument(
    const SourceRecord& source,
    StackBinaryFormat::ProjectDocument& document,
    std::string* outError = nullptr);

bool WriteCatalogSkeleton(
    const ManagedLayout& layout,
    const std::vector<SourceRecord>& sources,
    const std::string& selectedSourceKey,
    std::string* outError = nullptr);
bool WriteCatalogSkeleton(
    const ManagedLayout& layout,
    const std::vector<CatalogSourceRecord>& sources,
    const std::string& selectedSourceKey,
    std::string* outError = nullptr);
bool WriteCatalogSkeletonIfCurrent(
    const ManagedLayout& layout,
    const std::vector<SourceRecord>& sources,
    const std::string& selectedSourceKey,
    PersistenceCommitPredicate shouldCommit,
    std::string* outError = nullptr);
bool WriteCatalogSkeletonIfCurrent(
    const ManagedLayout& layout,
    const std::vector<CatalogSourceRecord>& sources,
    const std::string& selectedSourceKey,
    PersistenceCommitPredicate shouldCommit,
    std::string* outError = nullptr);
bool SaveAppState(const std::filesystem::path& path, const AppState& state, std::string* outError = nullptr);
bool SaveAppStateIfCurrent(
    const std::filesystem::path& path,
    const AppState& state,
    PersistenceCommitPredicate shouldCommit,
    std::string* outError = nullptr);
bool LoadAppState(const std::filesystem::path& path, AppState& outState, std::string* outError = nullptr);

nlohmann::json SerializeSourceRecord(const SourceRecord& source);
CatalogSourceRecord BuildCatalogSourceRecord(const SourceRecord& source);
std::vector<CatalogSourceRecord> BuildCatalogSourceRecords(const std::vector<SourceRecord>& sources);

} // namespace Stack::RawWorkspace
