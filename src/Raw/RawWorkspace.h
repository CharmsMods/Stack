#pragma once

#include "ThirdParty/json.hpp"
#include "Persistence/StackBinaryFormat.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawGallerySimilarity.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::RawWorkspace {

using PersistenceCommitPredicate = std::function<bool()>;

constexpr const char* kThumbnailsFolderName = "Stack RAW Thumbnails";
constexpr const char* kProjectsFolderName = "Stack RAW Projects";
constexpr const char* kCatalogFolderName = "Stack RAW Catalog";
constexpr const char* kCatalogFileName = "catalog.json";
constexpr const char* kRatingsFileName = "ratings.json";
constexpr int kFastNeutralThumbnailMaxDimension = 320;
constexpr int kNeutralThumbnailMaxDimension = 512;
constexpr int kSourceIdentityAlgorithmVersion = 1;

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

enum class GalleryContentMode {
    Gallery,
    Projects,
    Bracket
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
    UnifiedLayers,
    ManagedDecomposed,
    CustomGraph
};

struct ManagedLayout {
    std::filesystem::path workspaceRoot;
    std::filesystem::path dataDirectory;
    std::filesystem::path manifestPath;
    std::filesystem::path galleryPath;
    std::filesystem::path projectCoversDirectory;
    std::filesystem::path sourceTrashDirectory;
    std::filesystem::path projectTrashDirectory;
    std::filesystem::path thumbnailsDirectory;
    std::filesystem::path transientThumbnailsDirectory;
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
    int thumbnailVersion = 2;
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
    RawGallerySimilarityDescriptor similarityDescriptor;
    std::string errorMessage;
};

struct ProjectInfo {
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    ProjectStatus status = ProjectStatus::Unknown;
    RawProjectMode mode = RawProjectMode::UnifiedLayers;
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

struct SourceSetProjectMembership {
    std::string projectId;
    std::string projectName;
    std::filesystem::path projectPath;
    std::string sourceSetId;
    std::string sourceSetName;
    bool projectIsMultiFrame = false;
    std::filesystem::path projectCoverThumbnailCachePath;
};

struct SourceSetProjectCatalogEntry {
    std::string projectId;
    std::string projectName;
    std::filesystem::path absolutePath;
    std::filesystem::path relativePath;
    Stack::Project::ProjectStorageKind storageKind =
        Stack::Project::ProjectStorageKind::DirectoryBundle;
    ProjectStatus status = ProjectStatus::Unknown;
    bool readOnlyRecovery = false;
    bool dirty = false;
    bool conflict = false;
    bool multiFrameProject = false;
    bool bracketingProject = false;
    std::uint64_t sourceSetCount = 0;
    std::uint64_t totalFrameCount = 0;
    std::uint64_t rawSetCount = 0;
    std::uint64_t rasterSetCount = 0;
    std::string referenceSourceKey;
    std::vector<unsigned char> coverThumbnailBytes;
    std::filesystem::path coverThumbnailCachePath;
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
    std::int64_t modifiedUnixSeconds = 0;
    std::int64_t captureTimestamp = 0;
    bool captureMetadataChecked = false;
    std::string fingerprint;
    int sourceIdentityAlgorithmVersion = 0;
    ThumbnailInfo thumbnail;
    // Fast, disposable first-pass image used only while the persistent
    // high-quality thumbnail is being built. It is never written to the
    // workspace catalog.
    ThumbnailInfo transientThumbnail;
    ProjectInfo project;
    std::vector<SourceSetProjectMembership> sourceSetProjectMemberships;
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
    RawProjectMode mode = RawProjectMode::UnifiedLayers;
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
    std::int64_t modifiedUnixSeconds = 0;
    std::int64_t captureTimestamp = 0;
    bool captureMetadataChecked = false;
    std::string fingerprint;
    CatalogThumbnailRecord thumbnail;
    CatalogProjectRecord project;
    std::vector<SourceSetProjectMembership> sourceSetProjectMemberships;
};

struct ScanProgress {
    enum class Stage {
        Preparing,
        Scanning,
        CheckingSavedPreviews,
        DiscoveringProjects,
        ApplyingCatalog,
        Complete,
        Failed
    };

    Stage stage = Stage::Preparing;
    int directoriesVisited = 0;
    int filesVisited = 0;
    int managedDirectoriesSkipped = 0;
    int discoveredRawCount = 0;
    int cachedPreviewsChecked = 0;
    int reusedSourceIdentityCount = 0;
    int hashedRawCount = 0;
    std::uint64_t verifiedRawBytes = 0;
    std::string currentItem;
    std::string statusText;
};

struct ScanResult {
    bool success = false;
    std::string errorMessage;
    ManagedLayout layout;
    std::vector<SourceRecord> sources;
    std::vector<SourceSetProjectCatalogEntry> sourceSetProjects;
    ScanProgress progress;
};

struct WorkspaceState {
    std::filesystem::path workspaceRoot;
    std::vector<std::filesystem::path> recentWorkspaceRoots;
    std::vector<SourceRecord> sources;
    std::string selectedSourceKey;
    std::vector<std::string> selectedSourceKeys;
    std::vector<SourceSetProjectCatalogEntry> sourceSetProjects;
};

struct ThumbnailProgress {
    int total = 0;
    int valid = 0;
    int queued = 0;
    int completed = 0;
    int failed = 0;
    int quickAvailable = 0;
    int standardAvailable = 0;
    bool intakeComplete = false;
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
    bool multiSelected = false;
    std::uint64_t sourceSetProjectMembershipCount = 0;
    bool representsProject = false;
    bool projectIsMultiFrame = false;
    std::string projectId;
    std::string projectName;
    std::filesystem::path projectPath;
    std::filesystem::path projectCoverThumbnailCachePath;
    std::uint64_t savedProjectCount = 0;
};

struct GalleryFolderGroup {
    std::string folderKey;
    std::string label;
    std::vector<GallerySourceView> sources;
};

struct GalleryProjectView {
    std::size_t projectIndex = 0;
    std::string projectId;
    std::string projectName;
    std::filesystem::path projectPath;
    std::filesystem::path coverThumbnailCachePath;
    std::string referenceSourceKey;
    std::uint64_t frameCount = 0;
    ProjectStatus status = ProjectStatus::Unknown;
    bool multiFrameProject = false;
    bool bracketingProject = false;
};

struct GalleryPresentation {
    std::vector<GalleryFolderGroup> groups;
    std::vector<GalleryProjectView> projects;
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
    RawProjectMode mode = RawProjectMode::UnifiedLayers;
    ProjectStatus projectStatus = ProjectStatus::Unknown;
    std::string statusText;
    std::string graphTooltip;
    std::string readOnlyMessage;
};

struct AppState {
    std::filesystem::path lastWorkspaceRoot;
    std::vector<std::filesystem::path> recentWorkspaceRoots;
    float controlsPanelWidth = 0.0f;
    float rawLabToolRailWidth = 0.0f;
    float rawLabLowerShelfHeight = 0.0f;
    float rawLabFilmstripHeight = 0.0f;
    float rawLabGalleryThumbnailScale = 1.0f;
    bool rawLabLowerShelfOpen = false;
    bool rawLabToolRailOnRight = false;
    int rawViewportTargetFps = 30;
    bool smoothRawViewportUpdates = true;
    int rawViewportFadeBelowFps = 30;
    int rawLabActiveTool = 0;
    int rawLabActivePointCurve = 0;
    bool rawLabColorWarpLiveCloud = false;
    int rawLabLastGalleryHost = 1;
    int rawLabGalleryDisplayMode = 0;
    int rawLabGalleryContentMode = 0;
    std::unordered_map<std::string, RawGalleryManualGrouping>
        rawLabManualGroupings;
};

using RawPathPredicate = std::function<bool(const std::filesystem::path&)>;
using ScanProgressCallback = std::function<void(const ScanProgress&)>;
using ScanSourceReadyCallback =
    std::function<void(const ManagedLayout&, SourceRecord&, ScanProgress&)>;
using SourceIdentityReuseCallback = std::function<bool(SourceRecord&)>;
using CancellationPredicate = std::function<bool()>;

ManagedLayout BuildManagedLayout(const std::filesystem::path& workspaceRoot);
bool IsManagedFolderName(const std::string& folderName);
bool EnsureManagedFolders(const std::filesystem::path& workspaceRoot, std::string* outError = nullptr);
bool LoadGalleryState(const ManagedLayout& layout, RawGalleryManualGrouping& grouping, std::string* error = nullptr);
bool SaveGalleryState(const ManagedLayout& layout, const RawGalleryManualGrouping& grouping, std::string* error = nullptr);
bool DefaultRawPathPredicate(const std::filesystem::path& path);
ScanResult ScanWorkspace(
    const std::filesystem::path& workspaceRoot,
    RawPathPredicate isRawPath = {},
    ScanProgressCallback progressCallback = {},
    CancellationPredicate shouldCancel = {},
    ScanSourceReadyCallback sourceReadyCallback = {},
    SourceIdentityReuseCallback reuseSourceIdentity = {});

bool SelectSourceByKey(WorkspaceState& state, const std::string& sourceKey);
void AddRecentWorkspace(WorkspaceState& state, const std::filesystem::path& workspaceRoot, std::size_t maxRecent = 8);
ThumbnailSignature BuildThumbnailSignature(const SourceRecord& source, int maxDimension = kNeutralThumbnailMaxDimension);
ThumbnailInfo BuildThumbnailInfo(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension = kNeutralThumbnailMaxDimension);
ThumbnailInfo BuildTransientThumbnailInfo(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension = kFastNeutralThumbnailMaxDimension);
bool ThumbnailSignatureMatches(const ThumbnailSignature& expected, const nlohmann::json& actual);
ThumbnailStatus ClassifyThumbnail(
    const ManagedLayout& layout,
    SourceRecord& source,
    int maxDimension = kNeutralThumbnailMaxDimension);
ThumbnailStatus ClassifyThumbnailMetadata(
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
ThumbnailGenerationResult GenerateFastNeutralThumbnail(
    const ManagedLayout& layout,
    const SourceRecord& source,
    int maxDimension = kFastNeutralThumbnailMaxDimension,
    CancellationPredicate shouldCancel = {});
bool RemoveTransientThumbnailCache(
    const ManagedLayout& layout,
    std::string* outError = nullptr);
GalleryPresentation BuildGalleryPresentation(const WorkspaceState& state);
void FilterGalleryProjectCards(GalleryPresentation& presentation, GalleryContentMode mode);
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
bool DiscoverSourceSetProjects(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& sources,
    std::vector<SourceSetProjectCatalogEntry>& projects,
    CancellationPredicate shouldCancel = {});
nlohmann::json BuildRawSourceRefJson(const SourceRecord& source, bool linkedRaw = true);
nlohmann::json BuildRawProjectData(
    const SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& downstreamGraph,
    RawProjectMode mode = RawProjectMode::UnifiedLayers,
    bool linkedRaw = true);
bool ApplyRawWorkspaceDataToProjectDocument(
    const SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& downstreamGraph,
    StackBinaryFormat::ProjectDocument& document,
    RawProjectMode mode = RawProjectMode::UnifiedLayers,
    bool linkedRaw = true);
bool ReadProjectInfoFromDocument(
    const StackBinaryFormat::ProjectDocument& document,
    ProjectInfo& outInfo,
    Stack::RawRecipe::RawDevelopmentRecipe* outRecipe = nullptr);

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
bool LoadCatalogSnapshot(
    const ManagedLayout& layout,
    std::vector<SourceRecord>& outSources,
    std::string* outSelectedSourceKey = nullptr,
    std::string* outError = nullptr,
    std::vector<SourceSetProjectCatalogEntry>* outProjects = nullptr);
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
