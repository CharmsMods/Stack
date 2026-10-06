#include "Raw/Bracketing/Panorama/Compatibility.h"
#include "Raw/Bracketing/Recipe.h"
#include "Raw/RawRecipeCompatibility.h"
#include "Persistence/RawProjectModel.h"

#include "Raw/MultiFrameDenoise/Contracts.h"
#include "Raw/MultiFrameDenoise/SharedBurst.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include "Raw/RawDevelopmentRecipe.h"

#include "NodeMath/CompoundDefinition.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Project {
namespace {

constexpr const char* kUnifiedProjectKind = "stack-project";
constexpr const char* kUnifiedProjectModel = "unified-document";

bool HasExactFields(
    const json& value,
    std::initializer_list<const char*> fields) {
    if (!value.is_object() || value.size() != fields.size()) return false;
    return std::all_of(
        fields.begin(),
        fields.end(),
        [&](const char* field) { return value.contains(field); });
}

bool HasAllFields(
    const json& value,
    std::initializer_list<const char*> fields) {
    return value.is_object() && std::all_of(
        fields.begin(),
        fields.end(),
        [&](const char* field) { return value.contains(field); });
}

bool HasOnlyFields(
    const json& value,
    std::initializer_list<const char*> fields) {
    if (!value.is_object()) return false;
    for (auto item = value.begin(); item != value.end(); ++item) {
        if (std::find_if(
                fields.begin(), fields.end(),
                [&](const char* field) { return item.key() == field; }) ==
            fields.end()) {
            return false;
        }
    }
    return true;
}

std::string FirstJsonDifference(
    const json& expected,
    const json& actual,
    const std::string& path = "project") {
    if (expected.dump() == actual.dump()) return {};
    if (expected.type() != actual.type()) {
        return path;
    }
    if (expected.is_object()) {
        for (const auto& [key, child] : expected.items()) {
            const auto found = actual.find(key);
            if (found == actual.end()) return path + "." + key;
            const std::string difference = FirstJsonDifference(
                child, *found, path + "." + key);
            if (!difference.empty()) return difference;
        }
        for (const auto& [key, child] : actual.items()) {
            (void)child;
            if (!expected.contains(key)) return path + "." + key;
        }
        return {};
    }
    if (expected.is_array()) {
        if (expected.size() != actual.size()) return path;
        for (std::size_t index = 0; index < expected.size(); ++index) {
            const std::string difference = FirstJsonDifference(
                expected[index], actual[index],
                path + "[" + std::to_string(index) + "]");
            if (!difference.empty()) return difference;
        }
        return {};
    }
    return expected == actual ? std::string() : path;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool ValidateCurrentRecipeVersions(
    const json& value,
    const std::string& path,
    std::string& error) {
    if (value.is_object()) {
        const auto recipeVersion = value.find("rawRecipeVersion");
        if (recipeVersion != value.end()) {
            if (!Stack::RawRecipe::IsCanonicalRawRecipeDocument(value)) {
                error = path + " is not a canonical supported RAW recipe.";
                return false;
            }
        }
        for (const auto& [key, child] : value.items()) {
            if (!ValidateCurrentRecipeVersions(
                    child, path + "." + key, error)) {
                return false;
            }
        }
    } else if (value.is_array()) {
        for (std::size_t index = 0; index < value.size(); ++index) {
            if (!ValidateCurrentRecipeVersions(
                    value[index],
                    path + "[" + std::to_string(index) + "]",
                    error)) {
                return false;
            }
        }
    }
    return true;
}

json CanonicalRawWorkspaceData(const json& source) {
    if (!source.is_object() || source.empty()) {
        return json::object();
    }
    static constexpr const char* kCurrentFields[] = {
        "schema",
        "schemaVersion",
        "rawProjectModel",
        "projectId",
        "activeSourceSetId",
        "activeFrameId",
        "rawWorkspaceMode",
        "rawSourceRef",
        "rawRecipe",
        "managedRawSection",
        "customRawSection",
        "readOnlyReason",
        "managedAssetId",
        "originalSourcePath",
        "originalFileFingerprint",
        "repairRequired",
        "repairedCopy"
    };
    json result = json::object();
    for (const char* field : kCurrentFields) {
        const auto value = source.find(field);
        if (value != source.end()) {
            result[field] = *value;
        }
    }
    return result;
}

json SerializeAsset(const EmbeddedAssetRecord& asset) {
    json value = json::object();
    value["assetId"] = asset.assetId;
    value["sha256"] = asset.sha256;
    value["byteLength"] = asset.byteLength;
    value["displayName"] = asset.displayName;
    value["projectAssetPath"] = asset.projectAssetPath;
    value["originalSourcePath"] = asset.originalSourcePath;
    value["workspaceRelativeSourcePath"] = asset.workspaceRelativeSourcePath;
    value["originalFileFingerprint"] = asset.originalFileFingerprint;
    value["originalFilename"] = asset.originalFilename;
    value["managedRole"] = asset.managedRole;
    value["inputFamily"] = MultiFrameInputFamilyName(asset.inputFamily);
    value["captureMetadataSummary"] = asset.captureMetadataSummary;
    return value;
}

json SerializeLifecycle(const ProjectLifecycleMetadata& lifecycle) {
    json value = json::object();
    value["creationOrigin"] = ProjectCreationOriginName(lifecycle.creationOrigin);
    value["cleanupWhenUntouched"] = lifecycle.cleanupWhenUntouched;
    value["explicitlyRetained"] = lifecycle.explicitlyRetained;
    value["untouchedStateFingerprint"] = lifecycle.untouchedStateFingerprint;
    value["initialAssetIds"] = lifecycle.initialAssetIds;
    value["autoCreatedAtDirtyRevision"] = lifecycle.autoCreatedAtDirtyRevision;
    return value;
}

bool DeserializeLifecycle(
    const json& value,
    ProjectLifecycleMetadata& lifecycle,
    std::string& error) {
    if (!value.is_object()) {
        error = "Project lifecycle metadata is not an object.";
        return false;
    }
    if (!ParseProjectCreationOrigin(
            value.value("creationOrigin", std::string("manual")),
            lifecycle.creationOrigin)) {
        error = "Project lifecycle creationOrigin is invalid.";
        return false;
    }
    lifecycle.cleanupWhenUntouched = value.value("cleanupWhenUntouched", false);
    lifecycle.explicitlyRetained = value.value("explicitlyRetained", false);
    lifecycle.untouchedStateFingerprint = value.value(
        "untouchedStateFingerprint", std::string());
    lifecycle.initialAssetIds.clear();
    const json initialAssets = value.value("initialAssetIds", json::array());
    if (!initialAssets.is_array()) {
        error = "Project lifecycle initialAssetIds is not an array.";
        return false;
    }
    for (const json& assetId : initialAssets) {
        if (!assetId.is_string()) {
            error = "Project lifecycle initialAssetIds contains a non-string value.";
            return false;
        }
        lifecycle.initialAssetIds.push_back(assetId.get<std::string>());
    }
    lifecycle.autoCreatedAtDirtyRevision = value.value(
        "autoCreatedAtDirtyRevision", std::uint64_t { 0 });
    return true;
}

json SerializeFrame(const SourceSetFrame& frame) {
    json value = json::object();
    value["frameId"] = frame.frameId;
    value["assetId"] = frame.assetId;
    value["enabled"] = frame.enabled;
    value["userLabel"] = frame.userLabel;
    value["metadataOverrides"] = frame.metadataOverrides;
    return value;
}

json SerializeSourceSet(const MultiFrameSourceSet& sourceSet) {
    json value = json::object();
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
    asset.displayName = value.value("displayName", std::string());
    asset.projectAssetPath = value.value("projectAssetPath", std::string());
    asset.originalSourcePath = value.value("originalSourcePath", std::string());
    asset.workspaceRelativeSourcePath = value.value("workspaceRelativeSourcePath", std::string());
    asset.originalFileFingerprint = value.value(
        "originalFileFingerprint", std::string());
    asset.originalFilename = value.value("originalFilename", std::string());
    asset.managedRole = value.value("managedRole", std::string());
    if (!ParseMultiFrameInputFamily(
            value.value("inputFamily", std::string()), asset.inputFamily)) {
        error = "Embedded asset inputFamily is invalid.";
        return false;
    }
    asset.captureMetadataSummary = value.value("captureMetadataSummary", json::object());
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

const char* ProjectCreationOriginName(ProjectCreationOrigin value) {
    switch (value) {
        case ProjectCreationOrigin::Manual: return "manual";
        case ProjectCreationOrigin::AutoFromViewer: return "auto-from-viewer";
        case ProjectCreationOrigin::MultiSelection: return "multi-selection";
        case ProjectCreationOrigin::ImportedPackedProject: return "imported-packed-project";
    }
    return "manual";
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
        case MultiFrameOperationIntent::RawCaptureSet: return "raw-capture-set";
        case MultiFrameOperationIntent::RawBurstDenoise: return "raw-burst-denoise";
        case MultiFrameOperationIntent::RawBurstHdr: return "raw-burst-hdr";
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
    const Raw::Mfd::SharedBurstSettings sharedBurstSettings;
    return {
        { "schemaVersion", kMfdOperationSchemaVersion },
        { "inputDomain", "mosaic-cfa" },
        { "algorithmId", Raw::Mfd::kSharedBurstAlgorithmId },
        { "algorithmVersion", Raw::Mfd::kSharedBurstAlgorithmVersion },
        { "parameters", Raw::Mfd::SerializeParameters(parameters) },
        { "sharedBurstSettings", {
            { "schemaVersion", sharedBurstSettings.schemaVersion },
            { "algorithmId", sharedBurstSettings.algorithmId },
            { "algorithmVersion", sharedBurstSettings.algorithmVersion },
            { "profile", sharedBurstSettings.profile },
            { "exposureGroupToleranceEv",
                sharedBurstSettings.exposureGroupToleranceEv },
            { "huberThreshold", sharedBurstSettings.huberThreshold },
            { "maximumHuberIterations",
                sharedBurstSettings.maximumHuberIterations },
            { "absoluteHuberTolerance",
                sharedBurstSettings.absoluteHuberTolerance },
            { "relativeHuberTolerance",
                sharedBurstSettings.relativeHuberTolerance }
        } },
        { "frameTrust", json::object() },
        { "sharedPreMfdRecipe", json::object() },
        { "sharedPostMfdRecipe", json::object() },
        { "viewTransformPlacement", "internal" },
        { "experimentalAlignmentMode", "full" },
        { "experimentalMemoryBudgetGiB", 0.0 },
        { "processingImplemented", false }
    };
}

json MakeDefaultHdrOperationSettings() {
    const Raw::Hdr::Parameters parameters;
    return {
        { "schemaVersion", kHdrOperationSchemaVersion },
        { "inputDomain", "mosaic-cfa" },
        { "algorithmId", Raw::Hdr::kAlgorithmId },
        { "algorithmVersion", Raw::Hdr::kAlgorithmVersion },
        { "parameters", Raw::Hdr::SerializeParameters(parameters) },
        { "automaticGeometricReference", true },
        { "automaticRadiometricAnchor", true },
        { "radiometricAnchorFrameId", nullptr },
        { "sharedPostHdrRecipe", json::object() },
        { "viewTransformPlacement", "internal" },
        { "result", json::object() },
        { "processingImplemented", false }
    };
}

bool ParseProjectStorageKind(const std::string& value, ProjectStorageKind& result) {
    if (value == "directory-bundle") {
        result = ProjectStorageKind::DirectoryBundle;
        return true;
    }
    if (value == "portable-file") {
        result = ProjectStorageKind::PortableFile;
        return true;
    }
    return false;
}

bool ParseProjectCreationOrigin(
    const std::string& value,
    ProjectCreationOrigin& result) {
    if (value == "manual") {
        result = ProjectCreationOrigin::Manual;
        return true;
    }
    if (value == "auto-from-viewer") {
        result = ProjectCreationOrigin::AutoFromViewer;
        return true;
    }
    if (value == "multi-selection") {
        result = ProjectCreationOrigin::MultiSelection;
        return true;
    }
    if (value == "imported-packed-project") {
        result = ProjectCreationOrigin::ImportedPackedProject;
        return true;
    }
    return false;
}

bool ParseMultiFrameInputFamily(const std::string& value, MultiFrameInputFamily& result) {
    if (value == "raw") {
        result = MultiFrameInputFamily::Raw;
        return true;
    }
    if (value == "raster") {
        result = MultiFrameInputFamily::Raster;
        return true;
    }
    return false;
}

bool ParseMultiFrameOperationIntent(const std::string& value, MultiFrameOperationIntent& result) {
    if (value == "mfsr") {
        result = MultiFrameOperationIntent::Mfsr;
        return true;
    }
    if (value == "raw-capture-set") {
        result = MultiFrameOperationIntent::RawCaptureSet;
        return true;
    }
    if (value == "raw-burst-denoise") {
        result = MultiFrameOperationIntent::RawBurstDenoise;
        return true;
    }
    if (value == "raw-burst-hdr") {
        result = MultiFrameOperationIntent::RawBurstHdr;
        return true;
    }
    return false;
}

bool ParseMfdInputDomain(const std::string& value, MfdInputDomain& result) {
    if (value == "mosaic-cfa") {
        result = MfdInputDomain::MosaicCfa;
        return true;
    }
    if (value == "linear-rgb-unsupported") {
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
    summary.apertureFNumber = metadata.apertureFNumber;
    summary.focalLengthMm = metadata.focalLengthMm;
    summary.focusDistanceMeters = metadata.focusDistanceMeters;
    summary.lensModel = metadata.lensModel;
    summary.hasDngNoiseProfile = metadata.hasDngNoiseProfile;
    summary.dngNoiseProfilePlaneCount = static_cast<int>(metadata.dngNoiseProfile.size());
    summary.hasDngLinearResponseLimit = metadata.hasDngLinearResponseLimit;
    summary.dngLinearResponseLimit = metadata.dngLinearResponseLimit;
    summary.dngUnsupportedOpcodeCount = metadata.dngUnsupportedOpcodeCount;
    if (metadata.hasExposureTime && metadata.hasApertureFNumber && metadata.hasIsoSpeed) {
        summary.exposureConfidenceProvenance = "shutter-aperture-iso";
    } else if (metadata.hasExposureTime) {
        summary.exposureConfidenceProvenance = "shutter-only";
    } else {
        summary.exposureConfidenceProvenance = "image-fit-only";
    }
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
        { "schemaVersion", 2 },
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
        { "apertureFNumber", summary.apertureFNumber },
        { "focalLengthMm", summary.focalLengthMm },
        { "focusDistanceMeters", summary.focusDistanceMeters },
        { "lensModel", summary.lensModel },
        { "hasDngNoiseProfile", summary.hasDngNoiseProfile },
        { "dngNoiseProfilePlaneCount", summary.dngNoiseProfilePlaneCount },
        { "hasDngLinearResponseLimit", summary.hasDngLinearResponseLimit },
        { "dngLinearResponseLimit", summary.dngLinearResponseLimit },
        { "dngUnsupportedOpcodeCount", summary.dngUnsupportedOpcodeCount },
        { "exposureConfidenceProvenance", summary.exposureConfidenceProvenance },
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
    decoded.apertureFNumber = value.value("apertureFNumber", 0.0);
    decoded.focalLengthMm = value.value("focalLengthMm", 0.0);
    decoded.focusDistanceMeters = value.value("focusDistanceMeters", 0.0);
    decoded.lensModel = value.value("lensModel", std::string());
    decoded.hasDngNoiseProfile = value.value("hasDngNoiseProfile", false);
    decoded.dngNoiseProfilePlaneCount = value.value("dngNoiseProfilePlaneCount", 0);
    decoded.hasDngLinearResponseLimit = value.value("hasDngLinearResponseLimit", false);
    decoded.dngLinearResponseLimit = value.value("dngLinearResponseLimit", 1.0);
    decoded.dngUnsupportedOpcodeCount = value.value("dngUnsupportedOpcodeCount", 0);
    decoded.exposureConfidenceProvenance = value.value(
        "exposureConfidenceProvenance", std::string());
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

bool AreHdrCapturesStructurallyCompatible(
    const RawCaptureCompatibilitySummary& reference,
    const RawCaptureCompatibilitySummary& candidate,
    std::string* reason,
    std::vector<std::string>* warnings) {
    if (!AreMfdCapturesStructurallyCompatible(reference, candidate, reason)) return false;
    const auto incompatible = [&](const std::string& message) {
        if (reason) *reason = message;
        return false;
    };
    if (reference.focalLengthMm > 0.0 && candidate.focalLengthMm > 0.0 &&
        std::abs(reference.focalLengthMm - candidate.focalLengthMm) > 0.1) {
        return incompatible("HDR brackets must use the same focal length.");
    }
    if (!reference.lensModel.empty() && !candidate.lensModel.empty() &&
        Lower(reference.lensModel) != Lower(candidate.lensModel)) {
        return incompatible("HDR brackets must use the same lens.");
    }
    if (warnings) {
        if (reference.apertureFNumber <= 0.0 || candidate.apertureFNumber <= 0.0)
            warnings->push_back("Aperture metadata is missing; exposure matching may need per-frame offsets.");
        if (reference.focalLengthMm <= 0.0 || candidate.focalLengthMm <= 0.0)
            warnings->push_back("Focal-length metadata is missing; lens geometry could not be fully verified.");
        if (reference.focusDistanceMeters <= 0.0 || candidate.focusDistanceMeters <= 0.0) {
            warnings->push_back("Focus-distance metadata is missing; focus compatibility could not be verified.");
        } else {
            const double scale = std::max(
                reference.focusDistanceMeters,
                candidate.focusDistanceMeters);
            if (std::abs(reference.focusDistanceMeters - candidate.focusDistanceMeters) >
                std::max(0.01, scale * 0.02)) {
                warnings->push_back(
                    "Focus distances differ. Bracketing will preserve the original sample "
                    "coordinates without correcting focus breathing or sharpness changes.");
            }
        }
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
    json value = json::object();
    value["schemaVersion"] = snapshot.schemaVersion;
    json metadata = json::object();
    metadata["projectKind"] = kUnifiedProjectKind;
    metadata["projectModel"] = kUnifiedProjectModel;
    metadata["projectId"] = snapshot.projectId;
    metadata["projectName"] = snapshot.projectName;
    metadata["projectKindHint"] = snapshot.projectKindHint;
    metadata["documentModel"] = "unified-project";
    metadata["timestamp"] = snapshot.timestamp;
    metadata["sourceWidth"] = snapshot.sourceWidth;
    metadata["sourceHeight"] = snapshot.sourceHeight;
    metadata["adoptedFrom"] = snapshot.adoptedFrom;
    metadata["sourceAssetId"] = snapshot.sourceAssetId;
    value["metadata"] = std::move(metadata);
    value["lifecycle"] = SerializeLifecycle(snapshot.lifecycle);
    value["embeddedAssets"] = json::array();
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        value["embeddedAssets"].push_back(SerializeAsset(asset));
    }
    value["sourceSets"] = json::array();
    for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
        value["sourceSets"].push_back(SerializeSourceSet(sourceSet));
    }
    value["multiFrameGraph"] = SerializeMultiFrameGraph(snapshot.multiFrameGraph);
    value["pipelineData"] = snapshot.pipelineData;
    value["rawWorkspaceData"] = CanonicalRawWorkspaceData(
        snapshot.rawWorkspaceData);
    json uiState = json::object();
    uiState["activeSourceSetId"] = snapshot.activeSourceSetId;
    uiState["activeFrameId"] = snapshot.activeFrameId;
    uiState["nodeBrowserThumbnails"] = snapshot.nodeBrowserThumbnails;
    value["uiState"] = std::move(uiState);
    value["mfdInputRevision"] = snapshot.mfdInputRevision;
    value["hdrInputRevision"] = snapshot.hdrInputRevision;
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
    } else if (const std::uint32_t schemaVersion = value.value("schemaVersion", 0u);
               schemaVersion != kRawProjectSourceSetSchemaVersion) {
        error = "This project uses an obsolete Stack project schema and is not supported.";
    } else {
        const json metadata = value.value("metadata", json::object());
        if (metadata.value("projectKind", std::string()) != kUnifiedProjectKind ||
            metadata.value("projectModel", std::string()) != kUnifiedProjectModel) {
            error = "Project metadata does not use the current unified Stack document model.";
        } else {
            RawProjectSnapshot decoded;
            decoded.schemaVersion = kRawProjectSourceSetSchemaVersion;
            decoded.projectId = metadata.value("projectId", std::string());
            decoded.projectName = metadata.value("projectName", std::string());
            decoded.projectKindHint = metadata.value(
                "projectKindHint", std::string("raw"));
            decoded.timestamp = metadata.value("timestamp", std::string("Unknown"));
            decoded.sourceWidth = metadata.value("sourceWidth", 1);
            decoded.sourceHeight = metadata.value("sourceHeight", 1);
            decoded.adoptedFrom = metadata.value("adoptedFrom", std::string());
            decoded.sourceAssetId = metadata.value("sourceAssetId", std::string());
            if (!DeserializeLifecycle(
                    value.value("lifecycle", json()),
                    decoded.lifecycle,
                    error)) {
                if (error.empty()) {
                    error = "Project lifecycle metadata is invalid.";
                }
            }
            const json assets = value.value("embeddedAssets", json());
            const json sourceSets = value.value("sourceSets", json());
            if (error.empty() && (!assets.is_array() || !sourceSets.is_array())) {
                if (!assets.is_array() && !sourceSets.is_array()) {
                    error = "The project manifest is incomplete: embeddedAssets and sourceSets must both be arrays.";
                } else if (!assets.is_array()) {
                    error = "The project manifest is incomplete: embeddedAssets must be an array.";
                } else {
                    error = "The project manifest is incomplete: sourceSets must be an array.";
                }
            } else if (error.empty()) {
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
                    if (!DeserializeMultiFrameGraph(
                            value.value("multiFrameGraph", json()),
                            decoded.multiFrameGraph,
                            &error)) {
                        if (error.empty()) {
                            error = "RAW project MultiFrame graph is invalid.";
                        }
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
                    decoded.nodeBrowserThumbnails = uiState.value(
                        "nodeBrowserThumbnails", json::array());
                    decoded.mfdInputRevision = value.value(
                        "mfdInputRevision", std::uint64_t { 0 });
                    decoded.hdrInputRevision = value.value(
                        "hdrInputRevision", std::uint64_t { 0 });
                    decoded.postRecipeRevision = value.value(
                        "postRecipeRevision", std::uint64_t { 0 });
                    if (!ReadUnsigned64(value, "dirtyRevision", decoded.dirtyRevision) ||
                        !ReadUnsigned64(
                            value, "persistedStorageRevision", decoded.persistedStorageRevision)) {
                        error = "RAW project revision fields are missing or invalid.";
                    } else {
                        const ModelValidationResult validation = ValidateRawProjectSnapshot(decoded);
                        if (!validation.valid) {
                            error = validation.errors.empty()
                                ? "RAW project model validation failed."
                                : validation.errors.front();
                        } else {
                            const json canonical =
                                SerializeRawProjectSnapshot(decoded);
                            json authoredValue = value;
                            authoredValue.erase("_store");
                            if (canonical.dump() != authoredValue.dump()) {
                                error = "Project manifest is not canonical for the current Stack project schema at " +
                                    FirstJsonDifference(
                                        canonical, authoredValue) + ".";
                            } else {
                                snapshot = std::move(decoded);
                            }
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
        fail("RAW project snapshot schemaVersion must match the current schema.");
    }
    if (snapshot.projectId.empty()) fail("RAW project projectId is required.");
    if (snapshot.projectName.empty()) fail("RAW project projectName is required.");
    if (snapshot.projectKindHint.empty()) {
        fail("Project kind hint must not be empty.");
    }
    if (snapshot.sourceWidth <= 0 || snapshot.sourceHeight <= 0) {
        fail("RAW project source dimensions must be positive.");
    }
    if (!snapshot.nodeBrowserThumbnails.is_array()) {
        fail("RAW project node browser thumbnails must be an array.");
    }
    if (snapshot.lifecycle.cleanupWhenUntouched &&
        snapshot.lifecycle.creationOrigin != ProjectCreationOrigin::AutoFromViewer) {
        fail("Only projects automatically created from viewing may opt into untouched cleanup.");
    }
    std::string recipeVersionError;
    if (!ValidateCurrentRecipeVersions(
            snapshot.pipelineData,
            "pipelineData",
            recipeVersionError) ||
        !ValidateCurrentRecipeVersions(
            snapshot.rawWorkspaceData,
            "rawWorkspaceData",
            recipeVersionError)) {
        fail(recipeVersionError);
    }
    if (snapshot.rawWorkspaceData.is_object() &&
        !snapshot.rawWorkspaceData.empty() &&
        (snapshot.rawWorkspaceData.contains("schema") ||
         snapshot.rawWorkspaceData.contains("rawProjectModel"))) {
        if (snapshot.rawWorkspaceData.value("schema", std::string()) !=
                "stack.rawWorkspace.project" ||
            snapshot.rawWorkspaceData.value("schemaVersion", 0u) !=
                kRawWorkspaceProjectSchemaVersion ||
            snapshot.rawWorkspaceData.value("rawProjectModel", std::string()) !=
                kRawProjectModelSourceSets) {
            fail("RAW workspace data does not use the current schema.");
        }
        if (snapshot.rawWorkspaceData.contains("rawRecipe")) {
            if (!Stack::RawRecipe::IsCanonicalWorkspaceSourceRecipeDocument(snapshot.rawWorkspaceData.at("rawRecipe")))
                fail("A RAW workspace must store source preparation and presentation separately from its creative graph operations.");
            const std::string managedAssetId =
                snapshot.rawWorkspaceData.value(
                    "managedAssetId", std::string());
            if (managedAssetId.empty() ||
                FindEmbeddedAsset(snapshot, managedAssetId) == nullptr) {
                fail("A current single-image RAW recipe must reference its managed source asset.");
            }
        }
    }

    const MultiFrameGraphValidationResult graphValidation =
        ValidateMultiFrameGraph(snapshot.multiFrameGraph, snapshot, false);
    if (!graphValidation.valid) {
        for (const std::string& graphError : graphValidation.errors) {
            fail(graphError);
        }
    }
    result.warnings.insert(
        result.warnings.end(),
        graphValidation.warnings.begin(),
        graphValidation.warnings.end());

    std::unordered_map<std::string, const EmbeddedAssetRecord*> assets;
    std::unordered_set<std::string> hashesAndSizes;
    for (const EmbeddedAssetRecord& asset : snapshot.embeddedAssets) {
        if (asset.assetId.empty() || asset.sha256.empty()) {
            fail("Embedded assets require assetId and SHA-256.");
            continue;
        }
        if (asset.displayName.empty() || asset.originalFilename.empty() ||
            asset.originalFileFingerprint.empty()) {
            fail("Embedded assets require current identity and display fields.");
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
        if (!asset.projectAssetPath.empty()) {
            const std::filesystem::path managedPath(asset.projectAssetPath);
            if (managedPath.is_absolute() || managedPath.empty() ||
                std::find(managedPath.begin(), managedPath.end(), "..") != managedPath.end()) {
                fail("Managed project asset paths must be safe project-relative paths.");
            }
        }
        if (!asset.workspaceRelativeSourcePath.empty()) {
            const std::filesystem::path relative(asset.workspaceRelativeSourcePath);
            if (relative.has_root_path() ||
                std::find(relative.begin(), relative.end(), "..") != relative.end())
                fail("Workspace source provenance must be a safe relative path.");
        }
    }
    if (!snapshot.sourceAssetId.empty() &&
        assets.find(snapshot.sourceAssetId) == assets.end()) {
        fail("RAW project sourceAssetId must reference a managed asset.");
    }

    std::unordered_set<std::string> initialAssetIds;
    for (const std::string& assetId : snapshot.lifecycle.initialAssetIds) {
        if (assetId.empty() || !initialAssetIds.insert(assetId).second) {
            fail("Project lifecycle initial asset IDs must be non-empty and unique.");
        } else if (assets.find(assetId) == assets.end()) {
            fail("Project lifecycle references an initial asset that is not managed by the project.");
        }
    }

    std::unordered_set<std::string> setIds;
    std::unordered_set<std::string> graphBindings;
    for (const MultiFrameSourceSet& sourceSet : snapshot.sourceSets) {
        if (sourceSet.sourceSetId.empty() || !setIds.insert(sourceSet.sourceSetId).second) {
            fail("Source-set IDs must be present and unique.");
        }
        if (sourceSet.name.empty()) result.warnings.push_back("A source set has no display name.");
        const std::uint32_t expectedOperationSchema =
            sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise
            ? kMfdOperationSchemaVersion
            : sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr
                ? kHdrOperationSchemaVersion
                : kMultiFrameOperationSchemaVersion;
        if (sourceSet.operationSchemaVersion != expectedOperationSchema) {
            fail("Source-set operationSchemaVersion does not match the current operation schema.");
        }
        if ((sourceSet.operationIntent ==
                 MultiFrameOperationIntent::RawBurstDenoise ||
             sourceSet.operationIntent ==
                 MultiFrameOperationIntent::RawBurstHdr) &&
            snapshot.multiFrameGraph.nodes.empty() && !sourceSet.settings.contains("bracketing")) {
            fail("Current MFD and HDR source sets require an authored MultiFrame graph.");
        }
        const bool settingsAreObject = sourceSet.settings.is_object();
        if (!settingsAreObject) {
            fail("Source-set reserved settings must be an object.");
        } else if (!ValidateCurrentRecipeVersions(
                       sourceSet.settings,
                       "sourceSets.settings",
                       recipeVersionError)) {
            fail(recipeVersionError);
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet &&
            sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
            fail("RAW capture sets must contain RAW frames.");
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
            sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
            fail("Burst Denoise source sets must contain RAW frames.");
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
            sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
            fail("Burst HDR source sets must contain RAW frames.");
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet &&
            settingsAreObject) {
            if (!HasExactFields(sourceSet.settings, {
                    "schemaVersion", "inputDomain", "processingNode" }) ||
                sourceSet.operationSchemaVersion != kMultiFrameOperationSchemaVersion ||
                sourceSet.settings.value("schemaVersion", 0u) !=
                    kMultiFrameOperationSchemaVersion ||
                sourceSet.settings.value("inputDomain", std::string()) !=
                    "mosaic-cfa") {
                fail("RAW capture sets require the neutral mosaic-cfa dataset contract.");
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
                    fail("RAW capture-set frames require a typed compatibility summary.");
                    continue;
                }
                if (!candidate.supported ||
                    candidate.inputDomain != MfdInputDomain::MosaicCfa) {
                    fail("RAW capture-set frames must remain supported mosaiced-CFA captures.");
                    continue;
                }
                if (haveReference) {
                    std::string reason;
                    auto comparison = candidate;
                    comparison.orientation = reference.orientation;
                    if (!AreMfdCapturesStructurallyCompatible(
                            reference, comparison, &reason)) {
                        fail(reason);
                    }
                } else {
                    reference = candidate;
                    haveReference = true;
                }
            }
        }
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise &&
            settingsAreObject) {
            if (!HasAllFields(sourceSet.settings, {
                    "schemaVersion", "inputDomain", "algorithmId",
                    "algorithmVersion", "parameters", "sharedBurstSettings",
                    "frameTrust", "sharedPreMfdRecipe", "sharedPostMfdRecipe",
                    "viewTransformPlacement", "experimentalAlignmentMode",
                    "experimentalMemoryBudgetGiB", "processingImplemented" }) ||
                !HasOnlyFields(sourceSet.settings, {
                    "schemaVersion", "inputDomain", "algorithmId",
                    "algorithmVersion", "parameters", "sharedBurstSettings",
                    "frameTrust", "sharedPreMfdRecipe", "sharedPostMfdRecipe",
                    "viewTransformPlacement", "experimentalAlignmentMode",
                    "experimentalMemoryBudgetGiB", "processingImplemented",
                    "experimentalProcessingAvailable",
                    "graphViewTransformNodeUuid", "graphViewTransformSettings",
                    "result" })) {
                fail("MFD settings do not match the current operation schema exactly.");
            }
            const auto inputDomain = sourceSet.settings.find("inputDomain");
            if (inputDomain == sourceSet.settings.end() ||
                !inputDomain->is_string() ||
                inputDomain->get<std::string>() != "mosaic-cfa") {
                fail("MFD requires the mosaic-cfa input domain.");
            }
            {
                const auto settingsSchema = sourceSet.settings.find("schemaVersion");
                if (settingsSchema == sourceSet.settings.end() ||
                    !settingsSchema->is_number_unsigned() ||
                    settingsSchema->get<std::uint32_t>() != sourceSet.operationSchemaVersion) {
                    fail("MFD operation settings schema does not match the source-set operation schema.");
                }
                const auto algorithmId = sourceSet.settings.find("algorithmId");
                const auto algorithmVersion = sourceSet.settings.find("algorithmVersion");
                if (algorithmId == sourceSet.settings.end() ||
                    !algorithmId->is_string() ||
                    algorithmId->get<std::string>() !=
                        Raw::Mfd::kSharedBurstAlgorithmId ||
                    algorithmVersion == sourceSet.settings.end() ||
                    !algorithmVersion->is_number_unsigned() ||
                    algorithmVersion->get<std::uint32_t>() !=
                        Raw::Mfd::kSharedBurstAlgorithmVersion) {
                    fail("MFD requires the current Shared Burst algorithm.");
                }
                Raw::Mfd::Parameters parameters;
                std::string parameterError;
                const auto parameterValue = sourceSet.settings.find("parameters");
                if (parameterValue == sourceSet.settings.end() ||
                    !Raw::Mfd::DeserializeParameters(
                        *parameterValue, parameters, &parameterError)) {
                    fail("MFD preparation parameters are invalid: " + parameterError);
                }
                {
                    const auto burstValue =
                        sourceSet.settings.find("sharedBurstSettings");
                    if (burstValue == sourceSet.settings.end() ||
                        !burstValue->is_object() ||
                        !HasExactFields(*burstValue, {
                            "schemaVersion", "algorithmId", "algorithmVersion",
                            "profile", "exposureGroupToleranceEv",
                            "huberThreshold", "maximumHuberIterations",
                            "absoluteHuberTolerance", "relativeHuberTolerance" }) ||
                        burstValue->value("schemaVersion", 0u) !=
                            Raw::Mfd::kSharedBurstSettingsSchemaVersion ||
                        burstValue->value("algorithmId", std::string()) !=
                            Raw::Mfd::kSharedBurstAlgorithmId ||
                        burstValue->value("algorithmVersion", 0u) !=
                            Raw::Mfd::kSharedBurstAlgorithmVersion ||
                        burstValue->value("profile", std::string()) !=
                            "static-maximum") {
                        fail("Shared Burst V1 settings identity is invalid.");
                    } else {
                        const double exposureTolerance = burstValue->value(
                            "exposureGroupToleranceEv", -1.0);
                        const double huberThreshold = burstValue->value(
                            "huberThreshold", -1.0);
                        const std::uint32_t maximumIterations =
                            burstValue->value("maximumHuberIterations", 0u);
                        const double absoluteTolerance = burstValue->value(
                            "absoluteHuberTolerance", -1.0);
                        const double relativeTolerance = burstValue->value(
                            "relativeHuberTolerance", -1.0);
                        if (!std::isfinite(exposureTolerance) ||
                            exposureTolerance <= 0.0 ||
                            exposureTolerance > 4.0 ||
                            !std::isfinite(huberThreshold) ||
                            huberThreshold <= 0.0 ||
                            maximumIterations == 0u ||
                            maximumIterations > 12u ||
                            std::abs(huberThreshold - 1.345) > 1.0e-12 ||
                            !std::isfinite(absoluteTolerance) ||
                            absoluteTolerance < 0.0 ||
                            !std::isfinite(relativeTolerance) ||
                            relativeTolerance < 0.0) {
                            fail("Shared Burst V1 settings are outside supported bounds.");
                        }
                    }
                    const auto trust = sourceSet.settings.find("frameTrust");
                    if (trust == sourceSet.settings.end() ||
                        !trust->is_object()) {
                        fail("Shared Burst V1 requires a frame-trust object.");
                    } else {
                        for (const auto& [frameId, value] : trust->items()) {
                            if (!value.is_number()) {
                                fail("Shared Burst frame trust must be numeric.");
                                continue;
                            }
                            const double attenuation = value.get<double>();
                            if (!std::isfinite(attenuation) ||
                                attenuation < 0.0 || attenuation > 1.0) {
                                fail("Shared Burst frame trust must remain in [0,1].");
                            }
                        }
                    }
                }
                const auto processingImplemented =
                    sourceSet.settings.find("processingImplemented");
                if (processingImplemented == sourceSet.settings.end() ||
                    !processingImplemented->is_boolean() ||
                    processingImplemented->get<bool>()) {
                    fail("MFD settings must not persist a processed graph output.");
                }
                {
                    const auto preRecipe = sourceSet.settings.find("sharedPreMfdRecipe");
                    if (preRecipe == sourceSet.settings.end() ||
                        !preRecipe->is_object()) {
                        fail("MFD requires shared pre-MFD settings.");
                    }
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
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr &&
            settingsAreObject) {
            if (!HasAllFields(sourceSet.settings, {
                    "schemaVersion", "inputDomain", "algorithmId",
                    "algorithmVersion", "parameters",
                    "automaticGeometricReference", "automaticRadiometricAnchor",
                    "radiometricAnchorFrameId", "sharedPostHdrRecipe",
                    "viewTransformPlacement", "result",
                    "processingImplemented" }) ||
                !HasOnlyFields(sourceSet.settings, {
                    "schemaVersion", "inputDomain", "algorithmId",
                    "algorithmVersion", "parameters",
                    "automaticGeometricReference", "automaticRadiometricAnchor",
                    "radiometricAnchorFrameId", "sharedPostHdrRecipe",
                    "viewTransformPlacement", "result",
                    "processingImplemented", "graphViewTransformNodeUuid",
                    "graphViewTransformSettings", "bracketing", "bracketingDraft", "bracketingSelection", "bracketingDraftSelection",
                    "identicalCaptureExclusions", "bracketingResult", "autoBracket" })) {
                fail("HDR settings do not match the current operation schema exactly.");
            }
            if((sourceSet.settings.contains("bracketingResult")||sourceSet.settings.contains("autoBracket"))&&
                !sourceSet.settings.contains("bracketing"))
                fail("Saved bracket results and automatic processing require a bracket recipe.");
            // A derived result can be missing or invalid without invalidating its
            // originals. BracketingResultStore verifies its descriptor and bytes
            // when the result is opened; the capture model remains recoverable.
            if (sourceSet.operationSchemaVersion != kHdrOperationSchemaVersion ||
                sourceSet.settings.value("schemaVersion", 0u) != kHdrOperationSchemaVersion) {
                fail("HDR operation settings schema does not match HDR schema version 1.");
            }
            unsigned expectedAlgorithmVersion = Raw::Hdr::kAlgorithmVersion;
            if (const auto bracketSettings=sourceSet.settings.find("bracketing");bracketSettings!=sourceSet.settings.end()) {
                expectedAlgorithmVersion=1;
                if(bracketSettings->is_object()) expectedAlgorithmVersion=static_cast<unsigned>(bracketSettings->value("version",1));
            }
            if (sourceSet.settings.value("inputDomain", std::string()) != "mosaic-cfa" ||
                sourceSet.settings.value("algorithmId", std::string()) != (sourceSet.settings.contains("bracketing") ? "stack-bracketing" : Raw::Hdr::kAlgorithmId) ||
                sourceSet.settings.value("algorithmVersion", 0u) != expectedAlgorithmVersion) {
                fail("HDR operation requires the pinned tripod-cfa-hdr algorithm identity.");
            }
            if (sourceSet.settings.contains("bracketing")) {
                Raw::Bracketing::BracketingRecipe bracket;
                std::string bracketError;
                if (!Raw::Bracketing::Deserialize(sourceSet.settings["bracketing"], bracket, bracketError)) fail(bracketError);
                else {
                    std::unordered_set<std::string> assigned;
                    for (const auto& group : bracket.groups) for (const auto& frame : group.frames) assigned.insert(frame.id);
                    for (const auto& frame : sourceSet.frames) assigned.erase(frame.frameId);
                    if (!assigned.empty()) fail("A bracket group refers to a missing capture.");
                    if (!snapshot.multiFrameGraph.nodes.empty()) fail("A bracket recipe cannot also contain an editable legacy multi-frame graph.");
                }
                const auto exclusions =
                    sourceSet.settings.find("identicalCaptureExclusions");
                if(sourceSet.settings.contains("bracketingDraft")) {
                    Raw::Bracketing::BracketingRecipe draft;
                    if(!Raw::Bracketing::Deserialize(sourceSet.settings["bracketingDraft"],draft,bracketError,true))fail(bracketError);
                    else for(const auto& group:draft.groups)for(const auto& frame:group.frames)
                        if(std::none_of(sourceSet.frames.begin(),sourceSet.frames.end(),[&](const auto& f){return f.frameId==frame.id;}))
                            fail("A pending bracket draft refers to a missing capture.");
                }
                for(const auto* key:{"bracketingSelection","bracketingDraftSelection"})if(sourceSet.settings.contains(key)) {
                    const auto& selection=sourceSet.settings[key];
                    if(!selection.is_array())fail("Bracket selection must be a capture ID array.");
                    else for(const auto& id:selection)if(!id.is_string()||
                        std::none_of(sourceSet.frames.begin(),sourceSet.frames.end(),[&](const auto& f){return id==f.frameId;}))
                        fail("Bracket selection refers to a missing capture.");
                }
                if (exclusions != sourceSet.settings.end()) {
                    if (!exclusions->is_array()) {
                        fail("Bracket identical-capture exclusions must be an array.");
                    } else {
                        for (const auto& exclusion : *exclusions) {
                            if (!exclusion.is_object() ||
                                !exclusion.contains("excluded") ||
                                !exclusion["excluded"].is_string() ||
                                !exclusion.contains("kept") ||
                                !exclusion["kept"].is_string() ||
                                !exclusion.contains("assetId") ||
                                !exclusion["assetId"].is_string()) {
                                fail("A bracket identical-capture exclusion is invalid.");
                                break;
                            }
                        }
                    }
                }
            }
            const auto postRecipe = sourceSet.settings.find("sharedPostHdrRecipe");
            if (postRecipe == sourceSet.settings.end() || !postRecipe->is_object()) {
                fail("HDR operation schema 1 requires a shared post-HDR recipe.");
            }
            const auto parameters = sourceSet.settings.find("parameters");
            Raw::Hdr::Parameters hdrParameters;
            std::string parameterError;
            if (parameters == sourceSet.settings.end() ||
                !Raw::Hdr::DeserializeParameters(*parameters, hdrParameters, &parameterError)) {
                fail("HDR operation schema 1 requires parameters.");
            }
            if (!sourceSet.settings.contains("bracketing") && snapshot.multiFrameGraph.nodes.empty() &&
                sourceSet.frames.size() > Raw::Hdr::kMaximumFrameCount) {
                fail("HDR source sets support at most twenty frames.");
            }
            RawCaptureCompatibilitySummary reference;
            bool haveReference = false;
            for (const SourceSetFrame& frame : sourceSet.frames) {
                const EmbeddedAssetRecord* asset = FindEmbeddedAsset(snapshot, frame.assetId);
                RawCaptureCompatibilitySummary candidate;
                std::string summaryError;
                if (!asset || !DeserializeRawCaptureCompatibilitySummary(
                        asset->captureMetadataSummary, candidate, &summaryError)) {
                    fail("HDR frames require a typed RAW compatibility summary.");
                    continue;
                }
                if (!candidate.supported || candidate.inputDomain != MfdInputDomain::MosaicCfa) {
                    fail("HDR frames must retain a supported mosaiced-CFA classification.");
                    continue;
                }
                if (sourceSet.settings.contains("bracketing") &&
                    !Raw::Bracketing::UsesStoredFrame(sourceSet.settings["bracketing"], frame.frameId)) continue;
                if (sourceSet.settings.contains("bracketing"))
                    candidate.orientation = Raw::Bracketing::ResolveStoredOrientation(
                        sourceSet.settings["bracketing"], candidate.orientation);
                if (haveReference) {
                    std::string reason;
                    std::vector<std::string> compatibilityWarnings;
                    if (!(Raw::Bracketing::Panorama::IsPanorama(sourceSet)
                            ? Raw::Bracketing::Panorama::Compatible(reference, candidate, &reason)
                            : AreHdrCapturesStructurallyCompatible(reference, candidate, &reason, &compatibilityWarnings))) {
                        fail(reason);
                    }
                    result.warnings.insert(result.warnings.end(),
                        compatibilityWarnings.begin(), compatibilityWarnings.end());
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
    if ((sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet ||
         sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
         sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr) &&
        sourceSet.inputFamily != MultiFrameInputFamily::Raw) {
        if (reason) *reason = "RAW burst operations accept RAW source sets only.";
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
        if (sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet ||
            sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
            sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr) {
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
                        ? "The RAW burst contains a non-mosaic or unsupported RAW frame."
                        : compatibilityReason;
                }
                return MultiFrameSetStatus::Incompatible;
            }
            if (sourceSet.settings.contains("bracketing") &&
                !Raw::Bracketing::UsesStoredFrame(sourceSet.settings["bracketing"], frame.frameId)) continue;
            if (sourceSet.settings.contains("bracketing"))
                candidate.orientation = Raw::Bracketing::ResolveStoredOrientation(
                    sourceSet.settings["bracketing"], candidate.orientation);
            auto comparison = candidate;
            if (haveReference && sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet)
                comparison.orientation = reference.orientation;
            const bool compatible = !haveReference ||
                (sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr
                    ? (Raw::Bracketing::Panorama::IsPanorama(sourceSet)
                        ? Raw::Bracketing::Panorama::Compatible(reference, comparison, &compatibilityReason)
                        : AreHdrCapturesStructurallyCompatible(reference, comparison, &compatibilityReason, nullptr))
                    : AreMfdCapturesStructurallyCompatible(
                        reference, comparison, &compatibilityReason));
            if (!compatible) {
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
    if (sourceSet.operationIntent == MultiFrameOperationIntent::RawCaptureSet) {
        if (enabledFrameCount == 0u) {
            if (reason) *reason = "Draft: include at least one capture.";
            return MultiFrameSetStatus::Draft;
        }
        if (reason) {
            *reason = "Capture set is ready for a processing node.";
        }
        return MultiFrameSetStatus::ReadyForFutureProcessing;
    }
    if (enabledFrameCount < (sourceSet.settings.contains("bracketing") ? 1u : 2u)) {
        if (reason) *reason = "Draft: enable at least two frames for processing.";
        return MultiFrameSetStatus::Draft;
    }
    if (snapshot.multiFrameGraph.nodes.empty() &&
        sourceSet.operationIntent ==
            MultiFrameOperationIntent::RawBurstDenoise &&
        enabledFrameCount > Raw::Mfd::kSharedBurstMaximumEnabledCaptures) {
        if (reason) {
            *reason = "Disable captures until no more than 30 are enabled for Shared Burst.";
        }
        return MultiFrameSetStatus::Draft;
    }
    if (reason) {
        *reason =
                sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstDenoise ||
                sourceSet.operationIntent == MultiFrameOperationIntent::RawBurstHdr
            ? "Ready for Bayer-domain processing and RAW development publication."
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

ProjectDocumentKind ClassifyProjectDocument(
    const RawProjectSnapshot& snapshot) {
    // Source sets are the authoritative workflow boundary.
    return snapshot.sourceSets.empty()
        ? ProjectDocumentKind::SingleImage
        : ProjectDocumentKind::MultiFrame;
}

bool IsMultiFrameProjectDocument(const RawProjectSnapshot& snapshot) {
    return ClassifyProjectDocument(snapshot) ==
        ProjectDocumentKind::MultiFrame;
}

MultiFrameSourceSet* FindSourceSet(
    RawProjectSnapshot& snapshot,
    const std::string& sourceSetId) {
    return const_cast<MultiFrameSourceSet*>(FindSourceSet(
        static_cast<const RawProjectSnapshot&>(snapshot), sourceSetId));
}

} // namespace Stack::Project
