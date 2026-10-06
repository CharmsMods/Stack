#pragma once

#include "Persistence/MultiFrameGraph.h"
#include "ThirdParty/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace Raw {
struct RawMetadata;
}

namespace Stack::Project {

using json = nlohmann::json;

inline constexpr std::uint32_t kRawProjectSourceSetSchemaVersion = 8;
inline constexpr std::uint32_t kRawWorkspaceProjectSchemaVersion = 5;
inline constexpr std::uint32_t kMultiFrameOperationSchemaVersion = 1;
inline constexpr std::uint32_t kMfdOperationSchemaVersion = 5;
inline constexpr std::uint32_t kHdrOperationSchemaVersion = 1;
inline constexpr const char* kRawProjectModelSourceSets = "source-sets";

enum class ProjectStorageKind {
    DirectoryBundle,
    PortableFile
};

enum class ProjectCreationOrigin {
    Manual,
    AutoFromViewer,
    MultiSelection,
    ImportedPackedProject
};

// Presentation and routing metadata derived from the universal project
// document. It must never select a different persistence implementation.
enum class ProjectDocumentKind {
    SingleImage,
    MultiFrame
};

enum class MultiFrameInputFamily {
    Raw,
    Raster
};

enum class MultiFrameOperationIntent {
    Mfsr,
    RawCaptureSet,
    RawBurstDenoise,
    RawBurstHdr
};

enum class MfdInputDomain {
    MosaicCfa,
    LinearRgbUnsupported
};

enum class MfdResultState {
    Unavailable,
    Rendering,
    Ready,
    Stale,
    Failed,
    Canceled
};

struct RawCaptureCompatibilitySummary {
    MfdInputDomain inputDomain = MfdInputDomain::MosaicCfa;
    std::string pixelLayout;
    std::string cfaPattern;
    std::string cameraMake;
    std::string cameraModel;
    std::string uniqueCameraModel;
    int rawWidth = 0;
    int rawHeight = 0;
    int visibleWidth = 0;
    int visibleHeight = 0;
    int leftMargin = 0;
    int topMargin = 0;
    int bitDepth = 0;
    int orientation = 0;
    double exposureTimeSeconds = 0.0;
    double isoSpeed = 0.0;
    double apertureFNumber = 0.0;
    double focalLengthMm = 0.0;
    double focusDistanceMeters = 0.0;
    std::string lensModel;
    bool hasDngNoiseProfile = false;
    int dngNoiseProfilePlaneCount = 0;
    bool hasDngLinearResponseLimit = false;
    double dngLinearResponseLimit = 1.0;
    int dngUnsupportedOpcodeCount = 0;
    std::string exposureConfidenceProvenance;
    std::int64_t captureTimestamp = 0;
    bool supported = false;
    std::string rejectionReason;
};

enum class MultiFrameSetStatus {
    Draft,
    ReadyForFutureProcessing,
    Incompatible
};

struct EmbeddedAssetRecord {
    std::string assetId;
    std::string sha256;
    std::uint64_t byteLength = 0;
    // Stable project-owned identity and presentation. The managed path is
    // relative to the project folder and is never derived again from an
    // original's current file name.
    std::string displayName;
    std::string projectAssetPath;
    std::string originalSourcePath;
    std::string workspaceRelativeSourcePath;
    std::string originalFileFingerprint;
    std::string originalFilename;
    std::string managedRole;

    MultiFrameInputFamily inputFamily = MultiFrameInputFamily::Raw;
    json captureMetadataSummary = json::object();
};

struct ProjectLifecycleMetadata {
    ProjectCreationOrigin creationOrigin = ProjectCreationOrigin::Manual;
    bool cleanupWhenUntouched = false;
    bool explicitlyRetained = false;
    std::string untouchedStateFingerprint;
    std::vector<std::string> initialAssetIds;
    std::uint64_t autoCreatedAtDirtyRevision = 0;
};

struct SourceSetFrame {
    std::string frameId;
    std::string assetId;
    bool enabled = true;
    std::string userLabel;
    json metadataOverrides = json::object();
};

struct MultiFrameSourceSet {
    std::string sourceSetId;
    std::string name;
    MultiFrameInputFamily inputFamily = MultiFrameInputFamily::Raw;
    std::vector<SourceSetFrame> frames;
    std::string referenceFrameId;
    MultiFrameOperationIntent operationIntent = MultiFrameOperationIntent::Mfsr;
    std::uint32_t operationSchemaVersion = kMultiFrameOperationSchemaVersion;
    json settings = json::object();
    std::string graphBindingNodeId;
};

struct RawProjectSnapshot {
    std::uint32_t schemaVersion = kRawProjectSourceSetSchemaVersion;
    std::string projectId;
    std::string projectName;
    // A searchable hint only. Graph/content remains authoritative for what
    // workflows the project supports.
    std::string projectKindHint = "project";
    std::string timestamp = "Unknown";
    int sourceWidth = 1;
    int sourceHeight = 1;
    std::string adoptedFrom;
    std::string sourceAssetId;
    ProjectLifecycleMetadata lifecycle;
    std::vector<EmbeddedAssetRecord> embeddedAssets;
    std::vector<MultiFrameSourceSet> sourceSets;
    MultiFrameGraphDocument multiFrameGraph;
    json pipelineData = json::object();
    json rawWorkspaceData = json::object();
    std::vector<unsigned char> coverThumbnailBytes;
    std::string activeSourceSetId;
    std::string activeFrameId;
    json nodeBrowserThumbnails = json::array();
    std::uint64_t mfdInputRevision = 0;
    std::uint64_t hdrInputRevision = 0;
    std::uint64_t postRecipeRevision = 0;
    std::uint64_t dirtyRevision = 0;
    std::uint64_t persistedStorageRevision = 0;
};

struct ModelValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

const char* ProjectStorageKindName(ProjectStorageKind value);
const char* ProjectCreationOriginName(ProjectCreationOrigin value);
const char* MultiFrameInputFamilyName(MultiFrameInputFamily value);
const char* MultiFrameOperationIntentName(MultiFrameOperationIntent value);
const char* MfdInputDomainName(MfdInputDomain value);
const char* MfdResultStateName(MfdResultState value);

ProjectDocumentKind ClassifyProjectDocument(const RawProjectSnapshot& snapshot);
bool IsMultiFrameProjectDocument(const RawProjectSnapshot& snapshot);

bool ParseProjectStorageKind(const std::string& value, ProjectStorageKind& result);
bool ParseProjectCreationOrigin(const std::string& value, ProjectCreationOrigin& result);
bool ParseMultiFrameInputFamily(const std::string& value, MultiFrameInputFamily& result);
bool ParseMultiFrameOperationIntent(const std::string& value, MultiFrameOperationIntent& result);
bool ParseMfdInputDomain(const std::string& value, MfdInputDomain& result);

json MakeDefaultMfdOperationSettings();
json MakeDefaultHdrOperationSettings();

RawCaptureCompatibilitySummary BuildRawCaptureCompatibilitySummary(
    const Raw::RawMetadata& metadata);
json SerializeRawCaptureCompatibilitySummary(
    const RawCaptureCompatibilitySummary& summary);
bool DeserializeRawCaptureCompatibilitySummary(
    const json& value,
    RawCaptureCompatibilitySummary& summary,
    std::string* errorMessage = nullptr);
bool AreMfdCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason = nullptr);
bool AreHdrCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason = nullptr,
    std::vector<std::string>* warnings = nullptr);

std::string GenerateStableUuid();
std::string MakeAssetId(const std::string& sha256, std::uint64_t byteLength);

json SerializeRawProjectSnapshot(const RawProjectSnapshot& snapshot);
bool DeserializeRawProjectSnapshot(
    const json& value,
    RawProjectSnapshot& snapshot,
    std::string* errorMessage = nullptr);

ModelValidationResult ValidateRawProjectSnapshot(const RawProjectSnapshot& snapshot);
MultiFrameSetStatus EvaluateSourceSetStatus(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet,
    std::string* reason = nullptr);

const EmbeddedAssetRecord* FindEmbeddedAsset(
    const RawProjectSnapshot& snapshot,
    const std::string& assetId);
EmbeddedAssetRecord* FindEmbeddedAsset(
    RawProjectSnapshot& snapshot,
    const std::string& assetId);
const MultiFrameSourceSet* FindSourceSet(
    const RawProjectSnapshot& snapshot,
    const std::string& sourceSetId);
MultiFrameSourceSet* FindSourceSet(
    RawProjectSnapshot& snapshot,
    const std::string& sourceSetId);

} // namespace Stack::Project
