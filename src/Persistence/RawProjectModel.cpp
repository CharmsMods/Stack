#include "Persistence/RawProjectModel.h"

#include "Raw/MultiFrameDenoise/Contracts.h"

#include "NodeMath/CompoundDefinition.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Project {
namespace {

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

json UnknownFields(const json& value, std::initializer_list<const char*> knownKeys) {
    if (!value.is_object()) return json::object();
    json unknown = value;
    for (const char* key : knownKeys) unknown.erase(key);
    return unknown;
}

json ObjectWithUnknown(const json& unknown) {
    return unknown.is_object() ? unknown : json::object();
}

json SerializeAsset(const EmbeddedAssetRecord& asset) {
    json value = ObjectWithUnknown(asset.unknownFields);
    value["assetId"] = asset.assetId;
    value["sha256"] = asset.sha256;
    value["byteLength"] = asset.byteLength;
    value["originalFileName"] = asset.originalFileName;
    value["originalExtension"] = asset.originalExtension;
    value["inputFamily"] = MultiFrameInputFamilyName(asset.inputFamily);
    value["captureMetadataSummary"] = asset.captureMetadataSummary;
    value["informationalOriginPath"] = asset.informationalOriginPath;
    return value;
}

json SerializeFrame(const SourceSetFrame& frame) {
    json value = ObjectWithUnknown(frame.unknownFields);
    value["frameId"] = frame.frameId;
    value["assetId"] = frame.assetId;
    value["enabled"] = frame.enabled;
    value["userLabel"] = frame.userLabel;
    value["metadataOverrides"] = frame.metadataOverrides;
    return value;
}

json SerializeSourceSet(const MultiFrameSourceSet& sourceSet) {
    json value = ObjectWithUnknown(sourceSet.unknownFields);
    value["sourceSetId"] = sourceSet.sourceSetId;
    value["name"] = sourceSet.name;
    value["inputFamily"] = MultiFrameInputFamilyName(sourceSet.inputFamily);
    value["frames"] = json::array();
    for (const SourceSetFrame& frame : sourceSet.frames) {
        value["frames"].push_back(SerializeFrame(frame));
    }
    value["referenceFrameId"] = sourceSet.referenceFrameId.empty()
        ? json(nullptr)
        : json(sourceSet.referenceFrameId);
    value["operationIntent"] = MultiFrameOperationIntentName(sourceSet.operationIntent);
    value["operationSchemaVersion"] = sourceSet.operationSchemaVersion;
    value["settings"] = sourceSet.settings;
    value["graphBindingNodeId"] = sourceSet.graphBindingNodeId;
    return value;
}

bool ReadUnsigned64(const json& value, const char* key, std::uint64_t& result) {
    const auto iterator = value.find(key);
    if (iterator == value.end() || !iterator->is_number_unsigned()) return false;
    result = iterator->get<std::uint64_t>();
    return true;
}

bool DeserializeAsset(const json& value, EmbeddedAssetRecord& asset, std::string& error) {
    if (!value.is_object()) {
        error = "Embedded asset record is not an object.";
        return false;
    }
    asset.assetId = value.value("assetId", std::string());
    asset.sha256 = Lower(value.value("sha256", std::string()));
    if (!ReadUnsigned64(value, "byteLength", asset.byteLength)) {
        error = "Embedded asset byteLength is missing or is not an unsigned 64-bit value.";
        return false;
    }
    asset.originalFileName = value.value("originalFileName", std::string());
    asset.originalExtension = value.value("originalExtension", std::string());
    if (!ParseMultiFrameInputFamily(
            value.value("inputFamily", std::string()), asset.inputFamily)) {
        error = "Embedded asset inputFamily is invalid.";
        return false;
    }
    asset.captureMetadataSummary = value.value("captureMetadataSummary", json::object());
    asset.informationalOriginPath = value.value("informationalOriginPath", std::string());
    asset.unknownFields = UnknownFields(value, {
        "assetId", "sha256", "byteLength", "originalFileName", "originalExtension",
        "inputFamily", "captureMetadataSummary", "informationalOriginPath"
    });
    return true;
}

bool DeserializeFrame(const json& value, SourceSetFrame& frame, std::string& error) {
    if (!value.is_object()) {
        error = "Source-set frame is not an object.";
        return false;
    }
    frame.frameId = value.value("frameId", std::string());
    frame.assetId = value.value("assetId", std::string());
    frame.enabled = value.value("enabled", true);
    frame.userLabel = value.value("userLabel", std::string());
    frame.metadataOverrides = value.value("metadataOverrides", json::object());
    frame.unknownFields = UnknownFields(value, {
        "frameId", "assetId", "enabled", "userLabel", "metadataOverrides"
    });
    return true;
}

bool DeserializeSourceSet(const json& value, MultiFrameSourceSet& sourceSet, std::string& error) {
    if (!value.is_object()) {
        error = "Source set is not an object.";
        return false;
    }
    sourceSet.sourceSetId = value.value("sourceSetId", std::string());
    sourceSet.name = value.value("name", std::string());
    if (!ParseMultiFrameInputFamily(
            value.value("inputFamily", std::string()), sourceSet.inputFamily)) {
        error = "Source-set inputFamily is invalid.";
        return false;
    }
    sourceSet.frames.clear();
    const auto frames = value.find("frames");
    if (frames == value.end() || !frames->is_array()) {
        error = "Source-set frames are missing or invalid.";
        return false;
    }
    sourceSet.frames.reserve(frames->size());
    for (const json& frameValue : *frames) {
        SourceSetFrame frame;
        if (!DeserializeFrame(frameValue, frame, error)) return false;
        sourceSet.frames.push_back(std::move(frame));
    }
    const auto reference = value.find("referenceFrameId");
    sourceSet.referenceFrameId =
        reference != value.end() && reference->is_string()
        ? reference->get<std::string>()
        : std::string();
    if (!ParseMultiFrameOperationIntent(
            value.value("operationIntent", std::string()), sourceSet.operationIntent)) {
        error = "Source-set operationIntent is invalid.";
        return false;
    }
    sourceSet.operationSchemaVersion = value.value(
        "operationSchemaVersion", kMultiFrameOperationSchemaVersion);
    sourceSet.settings = value.value("settings", json::object());
    sourceSet.graphBindingNodeId = value.value("graphBindingNodeId", std::string());
    sourceSet.unknownFields = UnknownFields(value, {
        "sourceSetId", "name", "inputFamily", "frames", "referenceFrameId",
        "operationIntent", "operationSchemaVersion", "settings", "graphBindingNodeId"
    });
    return true;
}

} // namespace

const char* ProjectStorageKindName(ProjectStorageKind value) {
    switch (value) {
        case ProjectStorageKind::DirectoryBundle: return "directory-bundle";
        case ProjectStorageKind::PortableFile: return "portable-file";
    }
    return "directory-bundle";
}

const char* MultiFrameInputFamilyName(MultiFrameInputFamily value) {
    switch (value) {
        case MultiFrameInputFamily::Raw: return "raw";
        case MultiFrameInputFamily::Raster: return "raster";
    }
    return "raw";
}

const char* MultiFrameOperationIntentName(MultiFrameOperationIntent value) {
    switch (value) {
        case MultiFrameOperationIntent::Mfsr: return "mfsr";
        case MultiFrameOperationIntent::RawBurstDenoise: return "raw-burst-denoise";
    }
    return "mfsr";
}

const char* MfdInputDomainName(MfdInputDomain value) {
    switch (value) {
        case MfdInputDomain::MosaicCfa: return "mosaic-cfa";
        case MfdInputDomain::LinearRgbUnsupported: return "linear-rgb-unsupported";
    }
    return "mosaic-cfa";
}

const char* MfdResultStateName(MfdResultState value) {
    switch (value) {
        case MfdResultState::Unavailable: return "unavailable";
        case MfdResultState::Rendering: return "rendering";
        case MfdResultState::Ready: return "ready";
        case MfdResultState::Stale: return "stale";
        case MfdResultState::Failed: return "failed";
        case MfdResultState::Canceled: return "canceled";
    }
    return "unavailable";
}

json MakeDefaultMfdOperationSettings() {
    const Raw::Mfd::Parameters parameters;
    return {
        { "schemaVersion", kMfdOperationSchemaVersion },
        { "inputDomain", "mosaic-cfa" },
        { "algorithmId", Raw::Mfd::kAlgorithmId },
        { "algorithmVersion", Raw::Mfd::kAlgorithmVersion },
        { "parameters", Raw::Mfd::SerializeParameters(parameters) },
        { "sharedPostMfdRecipe", json::object() },
        { "viewTransformPlacement", "internal" },
        { "experimentalAlignmentMode", "full" },
        { "experimentalMemoryBudgetGiB", 0.0 },
        { "processingImplemented", false }
    };
}

bool ParseProjectStorageKind(const std::string& value, ProjectStorageKind& result) {
    const std::string normalized = Lower(value);
    if (normalized == "directory-bundle" || normalized == "directorybundle") {
        result = ProjectStorageKind::DirectoryBundle;
        return true;
    }
    if (normalized == "portable-file" || normalized == "portablefile") {
        result = ProjectStorageKind::PortableFile;
        return true;
    }
    return false;
}

bool ParseMultiFrameInputFamily(const std::string& value, MultiFrameInputFamily& result) {
    const std::string normalized = Lower(value);
    if (normalized == "raw") {
        result = MultiFrameInputFamily::Raw;
        return true;
    }
    if (normalized == "raster") {
        result = MultiFrameInputFamily::Raster;
        return true;
    }
    return false;
}

bool ParseMultiFrameOperationIntent(const std::string& value, MultiFrameOperationIntent& result) {
    const std::string normalized = Lower(value);
    if (normalized == "mfsr") {
        result = MultiFrameOperationIntent::Mfsr;
        return true;
    }
    if (normalized == "raw-burst-denoise" || normalized == "burst-denoise") {
        result = MultiFrameOperationIntent::RawBurstDenoise;
        return true;
    }
    return false;
}

bool ParseMfdInputDomain(const std::string& value, MfdInputDomain& result) {
    const std::string normalized = Lower(value);
    if (normalized == "mosaic-cfa" || normalized == "mosaiccfa") {
        result = MfdInputDomain::MosaicCfa;
        return true;
    }
    if (normalized == "linear-rgb-unsupported" || normalized == "linearrgb") {
        result = MfdInputDomain::LinearRgbUnsupported;
        return true;
    }
    return false;
}

RawCaptureCompatibilitySummary BuildRawCaptureCompatibilitySummary(
    const Raw::RawMetadata& metadata) {
    RawCaptureCompatibilitySummary summary;
    summary.pixelLayout = Raw::RawPixelLayoutName(metadata.pixelLayout);
    summary.cfaPattern = Raw::CfaPatternName(metadata.cfaPattern);
    summary.cameraMake = metadata.cameraMake;
    summary.cameraModel = metadata.cameraModel;
    summary.uniqueCameraModel = metadata.dngUniqueCameraModel;
    summary.rawWidth = metadata.rawWidth;
    summary.rawHeight = metadata.rawHeight;
    summary.visibleWidth = metadata.visibleWidth;
    summary.visibleHeight = metadata.visibleHeight;
    summary.leftMargin = metadata.leftMargin;
    summary.topMargin = metadata.topMargin;
    summary.bitDepth = metadata.bitDepth;
    summary.orientation = metadata.orientation;
    summary.exposureTimeSeconds = metadata.exposureTimeSeconds;
    summary.isoSpeed = metadata.isoSpeed;
    summary.captureTimestamp = metadata.captureTimestamp;
    if (metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
        summary.inputDomain = MfdInputDomain::LinearRgbUnsupported;
        summary.rejectionReason =
            "This image no longer contains a supported sensor mosaic. "
            "Linear RAW multi-frame denoise is planned for a future workflow.";
    } else if (metadata.pixelLayout != Raw::RawPixelLayout::MosaicBayer) {
        summary.rejectionReason = "The RAW pixel layout is unsupported for MFD.";
    } else if (metadata.cfaPattern == Raw::CfaPattern::Unknown) {
        summary.rejectionReason =
            "MFD currently requires a supported 2x2 Bayer CFA pattern.";
    } else if (metadata.rawWidth <= 0 || metadata.rawHeight <= 0 ||
               metadata.visibleWidth <= 0 || metadata.visibleHeight <= 0) {
        summary.rejectionReason = "The RAW sensor geometry is incomplete.";
    } else {
        summary.inputDomain = MfdInputDomain::MosaicCfa;
        summary.supported = true;
    }
    return summary;
}

json SerializeRawCaptureCompatibilitySummary(
    const RawCaptureCompatibilitySummary& summary) {
    return {
        { "schemaVersion", 1 },
        { "inputDomain", MfdInputDomainName(summary.inputDomain) },
        { "pixelLayout", summary.pixelLayout },
        { "cfaPattern", summary.cfaPattern },
        { "cameraMake", summary.cameraMake },
        { "cameraModel", summary.cameraModel },
        { "uniqueCameraModel", summary.uniqueCameraModel },
        { "rawWidth", summary.rawWidth },
        { "rawHeight", summary.rawHeight },
        { "visibleWidth", summary.visibleWidth },
        { "visibleHeight", summary.visibleHeight },
        { "leftMargin", summary.leftMargin },
        { "topMargin", summary.topMargin },
        { "bitDepth", summary.bitDepth },
        { "orientation", summary.orientation },
        { "exposureTimeSeconds", summary.exposureTimeSeconds },
        { "isoSpeed", summary.isoSpeed },
        { "captureTimestamp", summary.captureTimestamp },
        { "supported", summary.supported },
        { "rejectionReason", summary.rejectionReason }
    };
}

bool DeserializeRawCaptureCompatibilitySummary(
    const json& value,
    RawCaptureCompatibilitySummary& summary,
    std::string* errorMessage) {
    if (!value.is_object()) {
        if (errorMessage) *errorMessage = "RAW capture compatibility summary is not an object.";
        return false;
    }
    RawCaptureCompatibilitySummary decoded;
    if (!ParseMfdInputDomain(
            value.value("inputDomain", std::string("mosaic-cfa")),
            decoded.inputDomain)) {
        if (errorMessage) *errorMessage = "RAW capture input domain is invalid.";
        return false;
    }
    decoded.pixelLayout = value.value("pixelLayout", std::string());
    decoded.cfaPattern = value.value("cfaPattern", std::string());
    decoded.cameraMake = value.value("cameraMake", std::string());
    decoded.cameraModel = value.value("cameraModel", std::string());
    decoded.uniqueCameraModel = value.value("uniqueCameraModel", std::string());
    decoded.rawWidth = value.value("rawWidth", 0);
    decoded.rawHeight = value.value("rawHeight", 0);
    decoded.visibleWidth = value.value("visibleWidth", 0);
    decoded.visibleHeight = value.value("visibleHeight", 0);
    decoded.leftMargin = value.value("leftMargin", 0);
    decoded.topMargin = value.value("topMargin", 0);
    decoded.bitDepth = value.value("bitDepth", 0);
    decoded.orientation = value.value("orientation", 0);
    decoded.exposureTimeSeconds = value.value("exposureTimeSeconds", 0.0);
    decoded.isoSpeed = value.value("isoSpeed", 0.0);
    decoded.captureTimestamp = value.value("captureTimestamp", std::int64_t { 0 });
    decoded.supported = value.value("supported", false);
    decoded.rejectionReason = value.value("rejectionReason", std::string());
    summary = std::move(decoded);
    if (errorMessage) errorMessage->clear();
    return true;
}

bool AreMfdCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason) {
    const auto fail = [&](const std::string& message) {
        if (reason) *reason = message;
        return false;
    };
    if (!reference.supported || !candidate.supported) {
        return fail("Every MFD frame must be a supported mosaiced RAW capture.");
    }
    if (reference.inputDomain != MfdInputDomain::MosaicCfa ||
        candidate.inputDomain != MfdInputDomain::MosaicCfa) {
        return fail("MFD currently accepts still-mosaiced CFA RAW captures only.");
    }
    const std::string referenceCamera = !reference.uniqueCameraModel.empty()
        ? reference.uniqueCameraModel
        : Lower(reference.cameraMake + "\n" + reference.cameraModel);
    const std::string candidateCamera = !candidate.uniqueCameraModel.empty()
        ? candidate.uniqueCameraModel
        : Lower(candidate.cameraMake + "\n" + candidate.cameraModel);
    if (Lower(referenceCamera) != Lower(candidateCamera)) {
        return fail("The selected frames were captured by different camera models.");
    }
    if (reference.cfaPattern != candidate.cfaPattern) {
        return fail("The selected frames use different CFA patterns.");
    }
    if (reference.bitDepth != candidate.bitDepth) {
        return fail("The selected frames use different RAW sample bit depths.");
    }
    if (reference.orientation != candidate.orientation) {
        return fail("The selected frames use different sensor orientation metadata.");
    }
    if (reference.rawWidth != candidate.rawWidth ||
        reference.rawHeight != candidate.rawHeight ||
        reference.visibleWidth != candidate.visibleWidth ||
        reference.visibleHeight != candidate.visibleHeight ||
        reference.leftMargin != candidate.leftMargin ||
        reference.topMargin != candidate.topMargin) {
        return fail("The selected frames use different sensor or active-image geometry.");
    }
    if (reason) reason->clear();
    return true;
}

std::string GenerateStableUuid() {
    return Stack::NodeMath::GenerateCanonicalUuid();
}

std::string MakeAssetId(const std::string& sha256, std::uint64_t byteLength) {
    return "sha256:" + Lower(sha256) + ":" + std::to_string(byteLength);
}

json SerializeRawProjectSnapshot(const RawProjectSnapshot& snapshot) {
    json value = ObjectWithUnknown(snapshot.unknownFields);
    value["schemaVersion"] = snapshot.schemaVersion;
    json metadata = ObjectWithUnknown(snapshot.metadataUnknownFields);
    metadata["projectKind"] = "raw";
    metadata["rawProjectModel"] = kRawProjectModelSourceSets;
    metadata["projectId"] = snapshot.projectId;
    metadata["projectName"] = snapshot.projectName;
    value["metadata"] = std::move(metadata);
    value["embeddedAssets"] = json::array();
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        value["embeddedAssets"].push_back(SerializeAsset(asset));
    }
    value["sourceSets"] = json::array();
    for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
        value["sourceSets"].push_back(SerializeSourceSet(sourceSet));
    }
    value["pipelineData"] = snapshot.pipelineData;
    value["rawWorkspaceData"] = snapshot.rawWorkspaceData;
    json uiState = ObjectWithUnknown(snapshot.uiStateUnknownFields);
    uiState["activeSourceSetId"] = snapshot.activeSourceSetId;
    uiState["activeFrameId"] = snapshot.activeFrameId;
    value["uiState"] = std::move(uiState);
    value["mfdInputRevision"] = snapshot.mfdInputRevision;
    value["postRecipeRevision"] = snapshot.postRecipeRevision;
    value["dirtyRevision"] = snapshot.dirtyRevision;
    value["persistedStorageRevision"] = snapshot.persistedStorageRevision;
    return value;
}

bool DeserializeRawProjectSnapshot(
    const json& value,
    RawProjectSnapshot& snapshot,
    std::string* errorMessage) {
    std::string error;
    if (!value.is_object()) {
        error = "RAW project manifest is not an object.";
    } else if (value.value("schemaVersion", 0u) != kRawProjectSourceSetSchemaVersion) {
        error = "RAW project manifest is not schema version 3.";
    } else {
        const json metadata = value.value("metadata", json::object());
        if (metadata.value("projectKind", std::string()) != "raw" ||
            metadata.value("rawProjectModel", std::string()) != kRawProjectModelSourceSets) {
            error = "Project metadata is not a source-set RAW project.";
        } else {
            RawProjectSnapshot decoded;
            decoded.schemaVersion = kRawProjectSourceSetSchemaVersion;
            decoded.projectId = metadata.value("projectId", std::string());
            decoded.projectName = metadata.value("projectName", std::string());
            decoded.metadataUnknownFields = UnknownFields(metadata, {
                "projectKind", "rawProjectModel", "projectId", "projectName"
            });
            const json assets = value.value("embeddedAssets", json());
            const json sourceSets = value.value("sourceSets", json());
            if (!assets.is_array() || !sourceSets.is_array()) {
                error = "RAW project assets or source sets are missing.";
            } else {
                decoded.embeddedAssets.reserve(assets.size());
                for (const json& assetValue : assets) {
                    EmbeddedAssetRecord asset;
                    if (!DeserializeAsset(assetValue, asset, error)) break;
                    decoded.embeddedAssets.push_back(std::move(asset));
                }
                if (error.empty()) {
                    decoded.sourceSets.reserve(sourceSets.size());
                    for (const json& setValue : sourceSets) {
                        MultiFrameSourceSet sourceSet;
                        if (!DeserializeSourceSet(setValue, sourceSet, error)) break;
                        decoded.sourceSets.push_back(std::move(sourceSet));
                    }
                }
                if (error.empty()) {
                    decoded.pipelineData = value.value("pipelineData", json::object());
                    decoded.rawWorkspaceData = value.value("rawWorkspaceData", json::object());
                    const json uiState = value.value("uiState", json::object());
                    decoded.activeSourceSetId = uiState.value(
                        "activeSourceSetId", std::string());
                    decoded.activeFrameId = uiState.value(
                        "activeFrameId", std::string());
                    decoded.uiStateUnknownFields = UnknownFields(
                        uiState, { "activeSourceSetId", "activeFrameId" });
                    decoded.mfdInputRevision = value.value(
                        "mfdInputRevision", std::uint64_t { 0 });
                    decoded.postRecipeRevision = value.value(
                        "postRecipeRevision", std::uint64_t { 0 });
                    if (!ReadUnsigned64(value, "dirtyRevision", decoded.dirtyRevision) ||
                        !ReadUnsigned64(
                            value, "persistedStorageRevision", decoded.persistedStorageRevision)) {
                        error = "RAW project revision fields are missing or invalid.";
                    } else {
                        decoded.unknownFields = UnknownFields(value, {
                            "schemaVersion", "metadata", "embeddedAssets", "sourceSets",
                            "pipelineData", "rawWorkspaceData", "uiState", "dirtyRevision",
                            "persistedStorageRevision", "mfdInputRevision",
                            "postRecipeRevision", "_store"
                        });
                        const ModelValidationResult validation = ValidateRawProjectSnapshot(decoded);
                        if (!validation.valid) {
                            error = validation.errors.empty()
                                ? "RAW project model validation failed."
                                : validation.errors.front();
                        } else {
                            snapshot = std::move(decoded);
                        }
                    }
                }
            }
        }
    }
    if (!error.empty()) {
        if (errorMessage) *errorMessage = error;
        return false;
    }
    if (errorMessage) errorMessage->clear();
    return true;
}

ModelValidationResult ValidateRawProjectSnapshot(const RawProjectSnapshot& snapshot) {
    ModelValidationResult result;
    const auto fail = [&](const std::string& message) {
        result.valid = false;
        result.errors.push_back(message);
    };
    if (snapshot.schemaVersion != kRawProjectSourceSetSchemaVersion) {
        fail("RAW project snapshot schemaVersion must be 3.");
    }
    if (snapshot.projectId.empty()) fail("RAW project projectId is required.");
    if (snapshot.projectName.empty()) fail("RAW project projectName is required.");

    std::unordered_map<std::string, const EmbeddedAssetRecord*> assets;
    std::unordered_set<std::string> hashesAndSizes;
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        if (asset.assetId.empty() || asset.sha256.empty()) {
            fail("Embedded assets require assetId and SHA-256.");
            continue;
        }
        if (asset.sha256.size() != 64u ||
            !std::all_of(asset.sha256.begin(), asset.sha256.end(), [](unsigned char character) {
                return std::isxdigit(character) != 0;
            })) {
            fail("Embedded asset SHA-256 must contain 64 hex characters.");
        }
        if (!asset.captureMetadataSummary.is_object()) {
            fail("Embedded asset capture metadata summary must be an object.");
        }
        if (!assets.emplace(asset.assetId, &asset).second) {
            fail("Embedded asset IDs must be unique: " + asset.assetId);
        }
        const std::string contentKey = Lower(asset.sha256) + ":" + std::to_string(asset.byteLength);
        if (!hashesAndSizes.insert(contentKey).second) {
            fail("Embedded asset manifest contains duplicate content: " + asset.assetId);
        }
        if (asset.assetId != MakeAssetId(asset.sha256, asset.byteLength)) {
            fail("Embedded asset ID does not match its SHA-256 and byte length: " + asset.assetId);
        }
    }

    std::unordered_set<std::string> setIds;
    std::unordered_set<std::string> graphBindings;
    for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
        if (sourceSet.sourceSetId.empty() || !setIds.insert(sourceSet.sourceSetId).second) {
            fail("Source-set IDs must be present and unique.");
        }
        if (sourceSet.name.empty()) result.warnings.push_back("A source set has no display name.");
        if (sourceSet.operationSchemaVersion == 0u) {
            fail("Source-set operationSchemaVersion must not be zero.");
        }
        const bool settingsAreObject = sourceSet.settings.is_object();
        if (!settingsAreObject) {
            fail("Source-set reserved settings must be an object.");
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
            sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
            fail("Burst Denoise source sets must contain RAW frames.");
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
            settingsAreObject &&
            sourceSet.operationSchemaVersion >= kMfdMosaicPlaceholderSchemaVersion) {
            const auto inputDomain = sourceSet.settings.find("inputDomain");
            if (inputDomain == sourceSet.settings.end() ||
                !inputDomain->is_string() ||
                inputDomain->get<std::string>() != "mosaic-cfa") {
                fail("MFD operation schema 2 or newer requires the mosaic-cfa input domain.");
            }
            if (sourceSet.operationSchemaVersion >= kMfdOperationSchemaVersion) {
                const auto settingsSchema = sourceSet.settings.find("schemaVersion");
                if (settingsSchema == sourceSet.settings.end() ||
                    !settingsSchema->is_number_unsigned() ||
                    settingsSchema->get<std::uint32_t>() != kMfdOperationSchemaVersion) {
                    fail("MFD operation settings schema does not match the source-set operation schema.");
                }
                const auto algorithmId = sourceSet.settings.find("algorithmId");
                const auto algorithmVersion = sourceSet.settings.find("algorithmVersion");
                if (algorithmId == sourceSet.settings.end() ||
                    !algorithmId->is_string() ||
                    algorithmId->get<std::string>() != Raw::Mfd::kAlgorithmId ||
                    algorithmVersion == sourceSet.settings.end() ||
                    !algorithmVersion->is_number_unsigned() ||
                    algorithmVersion->get<std::uint32_t>() != Raw::Mfd::kAlgorithmVersion) {
                    fail("MFD operation schema 3 requires the pinned RA-CFA V1 algorithm identity.");
                }
                Raw::Mfd::Parameters parameters;
                std::string parameterError;
                const auto parameterValue = sourceSet.settings.find("parameters");
                if (parameterValue == sourceSet.settings.end() ||
                    !Raw::Mfd::DeserializeParameters(
                        *parameterValue, parameters, &parameterError)) {
                    fail("MFD RA-CFA V1 parameters are invalid: " + parameterError);
                }
                const auto processingImplemented =
                    sourceSet.settings.find("processingImplemented");
                if (processingImplemented == sourceSet.settings.end() ||
                    !processingImplemented->is_boolean() ||
                    processingImplemented->get<bool>()) {
                    fail("MFD operation schema 3 does not persist an authoritative processed graph output.");
                }
                const auto memoryBudget =
                    sourceSet.settings.find("experimentalMemoryBudgetGiB");
                if (memoryBudget != sourceSet.settings.end()) {
                    if (!memoryBudget->is_number()) {
                        fail("MFD experimental memory budget must be numeric.");
                    } else {
                        const double value = memoryBudget->get<double>();
                        if (!std::isfinite(value) || value < 0.0 ||
                            value > 256.0) {
                            fail("MFD experimental memory budget must be 0 (automatic) or at most 256 GiB.");
                        }
                    }
                }
                const auto alignmentMode =
                    sourceSet.settings.find("experimentalAlignmentMode");
                if (alignmentMode != sourceSet.settings.end()) {
                    const std::string value = alignmentMode->is_string()
                        ? alignmentMode->get<std::string>()
                        : std::string();
                    if (value != "full" && value != "translation-only" &&
                        value != "identity") {
                        fail("MFD experimental alignment mode must be full, translation-only, or identity.");
                    }
                }
            }
            RawCaptureCompatibilitySummary reference;
            bool haveReference = false;
            for (const SourceSetFrame& frame : sourceSet.frames) {
                const EmbeddedAssetRecord* asset = FindEmbeddedAsset(
                    snapshot, frame.assetId);
                RawCaptureCompatibilitySummary candidate;
                std::string summaryError;
                if (!asset || !DeserializeRawCaptureCompatibilitySummary(
                        asset->captureMetadataSummary,
                        candidate,
                        &summaryError)) {
                    fail("MFD frames require a typed RAW compatibility summary.");
                    continue;
                }
                if (!candidate.supported ||
                    candidate.inputDomain != MfdInputDomain::MosaicCfa) {
                    fail("MFD frames must retain a supported mosaiced-CFA classification.");
                    continue;
                }
                if (haveReference) {
                    std::string reason;
                    if (!AreMfdCapturesStructurallyCompatible(
                            reference, candidate, &reason)) {
                        fail(reason);
                    }
                } else {
                    reference = candidate;
                    haveReference = true;
                }
            }
        }
        if (!sourceSet.graphBindingNodeId.empty() &&
            !graphBindings.insert(sourceSet.graphBindingNodeId).second) {
            fail("Each source set must bind to a unique managed graph node.");
        }

        std::unordered_set<std::string> frameIds;
        std::unordered_set<std::string> assetIds;
        for (const SourceSetFrame& frame : sourceSet.frames) {
            if (frame.frameId.empty() || !frameIds.insert(frame.frameId).second) {
                fail("Frame IDs must be present and unique within a source set.");
            }
            if (!assetIds.insert(frame.assetId).second) {
                fail("An embedded asset may appear only once within a source set: " + frame.assetId);
            }
            const auto asset = assets.find(frame.assetId);
            if (asset == assets.end()) {
                fail("Source-set frame references an unknown embedded asset: " + frame.assetId);
            } else if (asset->second->inputFamily != sourceSet.inputFamily) {
                fail("Source-set frames must use one homogeneous input family.");
            }
            if (!frame.metadataOverrides.is_object()) {
                fail("Source-set frame metadata overrides must be an object.");
            }
        }
        if (!sourceSet.referenceFrameId.empty() &&
            frameIds.find(sourceSet.referenceFrameId) == frameIds.end()) {
            fail("Source-set reference frame is not a member of the set.");
        }
        if (sourceSet.frames.size() < 2u) {
            result.warnings.push_back("Source set " + sourceSet.sourceSetId + " is a valid draft with fewer than two frames.");
        }
    }
    if (!snapshot.activeSourceSetId.empty() &&
        setIds.find(snapshot.activeSourceSetId) == setIds.end()) {
        fail("The active source-set ID does not identify a source set in this project.");
    }
    if (!snapshot.activeFrameId.empty()) {
        bool activeFrameFound = false;
        for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
            if (!snapshot.activeSourceSetId.empty() &&
                sourceSet.sourceSetId != snapshot.activeSourceSetId) {
                continue;
            }
            activeFrameFound = activeFrameFound || std::any_of(
                sourceSet.frames.begin(), sourceSet.frames.end(),
                [&](const SourceSetFrame& frame) {
                    return frame.frameId == snapshot.activeFrameId;
                });
        }
        if (!activeFrameFound) {
            fail("The active frame ID does not identify a frame in this project.");
        }
    }
    return result;
}

MultiFrameSetStatus EvaluateSourceSetStatus(
    const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& sourceSet,
    std::string* reason) {
    if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
        sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
        if (reason) *reason = "Burst Denoise accepts RAW source sets only.";
        return MultiFrameSetStatus::Incompatible;
    }
    RawCaptureCompatibilitySummary reference;
    bool haveReference = false;
    std::unordered_set<std::string> assets;
    for (const SourceSetFrame& frame : sourceSet.frames) {
        const EmbeddedAssetRecord* asset = FindEmbeddedAsset(snapshot, frame.assetId);
        if (!asset || asset->inputFamily != sourceSet.inputFamily ||
            !assets.insert(frame.assetId).second) {
            if (reason) *reason = "The source set contains missing, duplicate, or incompatible assets.";
            return MultiFrameSetStatus::Incompatible;
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
            sourceSet.operationSchemaVersion >= kMfdMosaicPlaceholderSchemaVersion) {
            RawCaptureCompatibilitySummary candidate;
            std::string compatibilityReason;
            if (!DeserializeRawCaptureCompatibilitySummary(
                    asset->captureMetadataSummary,
                    candidate,
                    &compatibilityReason) ||
                !candidate.supported ||
                candidate.inputDomain != MfdInputDomain::MosaicCfa) {
                if (reason) {
                    *reason = compatibilityReason.empty()
                        ? "The MFD burst contains a non-mosaic or unsupported RAW frame."
                        : compatibilityReason;
                }
                return MultiFrameSetStatus::Incompatible;
            }
            if (haveReference &&
                !AreMfdCapturesStructurallyCompatible(
                    reference, candidate, &compatibilityReason)) {
                if (reason) *reason = compatibilityReason;
                return MultiFrameSetStatus::Incompatible;
            }
            if (!haveReference) {
                reference = candidate;
                haveReference = true;
            }
        }
    }
    const std::size_t enabledFrameCount = static_cast<std::size_t>(std::count_if(
        sourceSet.frames.begin(), sourceSet.frames.end(),
        [](const SourceSetFrame& frame) { return frame.enabled; }));
    if (enabledFrameCount < 2u) {
        if (reason) *reason = "Draft: enable at least two frames for future processing.";
        return MultiFrameSetStatus::Draft;
    }
    if (reason) {
        *reason =
            sourceSet.operationIntent ==
                    MultiFrameOperationIntent::RawBurstDenoise &&
                sourceSet.operationSchemaVersion >= kMfdOperationSchemaVersion
            ? "Ready for experimental Bayer processing and RAW development publication."
            : "Processing is not implemented yet.";
    }
    return MultiFrameSetStatus::ReadyForFutureProcessing;
}

const EmbeddedAssetRecord* FindEmbeddedAsset(
    const RawProjectSnapshot& snapshot,
    const std::string& assetId) {
    const auto found = std::find_if(
        snapshot.embeddedAssets.begin(), snapshot.embeddedAssets.end(),
        [&](const EmbeddedAssetRecord& asset) { return asset.assetId == assetId; });
    return found == snapshot.embeddedAssets.end() ? nullptr : &*found;
}

EmbeddedAssetRecord* FindEmbeddedAsset(
    RawProjectSnapshot& snapshot,
    const std::string& assetId) {
    return const_cast<EmbeddedAssetRecord*>(FindEmbeddedAsset(
        static_cast<const RawProjectSnapshot&>(snapshot), assetId));
}

const MultiFrameSourceSet* FindSourceSet(
    const RawProjectSnapshot& snapshot,
    const std::string& sourceSetId) {
    const auto found = std::find_if(
        snapshot.sourceSets.begin(), snapshot.sourceSets.end(),
        [&](const MultiFrameSourceSet& sourceSet) {
            return sourceSet.sourceSetId == sourceSetId;
        });
    return found == snapshot.sourceSets.end() ? nullptr : &*found;
}

MultiFrameSourceSet* FindSourceSet(
    RawProjectSnapshot& snapshot,
    const std::string& sourceSetId) {
    return const_cast<MultiFrameSourceSet*>(FindSourceSet(
        static_cast<const RawProjectSnapshot&>(snapshot), sourceSetId));
}

} // namespace Stack::Project
