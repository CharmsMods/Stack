#include "Raw/MultiFrameDenoise/Corpus.h"

#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <set>
#include <unordered_set>

namespace Raw::Mfd {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isxdigit(character) != 0;
        });
}

std::string HashJson(const nlohmann::json& value) {
    const std::string bytes = value.dump();
    const std::vector<std::uint8_t> content(bytes.begin(), bytes.end());
    return Stack::RawEvidence::ComputeSourceIdentity(content).sha256;
}

bool ParseLayer(const std::string& value, MfdCorpusLayer& layer) {
    if (value == "synthetic") {
        layer = MfdCorpusLayer::Synthetic;
        return true;
    }
    if (value == "controlled-camera") {
        layer = MfdCorpusLayer::ControlledCamera;
        return true;
    }
    if (value == "uncontrolled-real") {
        layer = MfdCorpusLayer::UncontrolledReal;
        return true;
    }
    return false;
}

bool ValidLayer(MfdCorpusLayer layer) {
    return layer == MfdCorpusLayer::Synthetic ||
        layer == MfdCorpusLayer::ControlledCamera ||
        layer == MfdCorpusLayer::UncontrolledReal;
}

std::filesystem::path ResolvePath(
    const std::filesystem::path& baseDirectory,
    const std::string& value) {
    std::filesystem::path path = std::filesystem::u8path(value);
    if (path.is_relative()) path = baseDirectory / path;
    return path.lexically_normal();
}

std::string CameraModeIdentity(
    const Stack::Project::RawCaptureCompatibilitySummary& summary) {
    return HashJson({
        { "contract", "ra-cfa-camera-mode-v1" },
        { "cameraMake", summary.cameraMake },
        { "cameraModel", summary.cameraModel },
        { "uniqueCameraModel", summary.uniqueCameraModel },
        { "pixelLayout", summary.pixelLayout },
        { "cfaPattern", summary.cfaPattern },
        { "rawWidth", summary.rawWidth },
        { "rawHeight", summary.rawHeight },
        { "visibleWidth", summary.visibleWidth },
        { "visibleHeight", summary.visibleHeight },
        { "leftMargin", summary.leftMargin },
        { "topMargin", summary.topMargin },
        { "bitDepth", summary.bitDepth },
        { "orientation", summary.orientation }
    });
}

std::string SourceSetIdentity(
    const MfdLockedCorpusEntry& entry) {
    nlohmann::json frames = nlohmann::json::array();
    for (const MfdLockedCorpusFrame& frame : entry.frames) {
        frames.push_back({
            { "sha256", frame.identity.sha256 },
            { "byteLength", frame.identity.byteLength }
        });
    }
    nlohmann::json groundTruth = nullptr;
    if (entry.groundTruthIdentity.valid) {
        groundTruth = {
            { "sha256", entry.groundTruthIdentity.sha256 },
            { "byteLength", entry.groundTruthIdentity.byteLength }
        };
    }
    return HashJson({
        { "contract", "ra-cfa-corpus-source-content-set-v1" },
        { "referenceFrameIndex", entry.referenceFrameIndex },
        { "orderedFrames", std::move(frames) },
        { "referenceMomentGroundTruth", std::move(groundTruth) }
    });
}

nlohmann::json SerializeFileIdentity(const MfdCorpusFileIdentity& identity) {
    return {
        { "valid", identity.valid },
        { "sha256", identity.sha256 },
        { "byteLength", identity.byteLength }
    };
}

bool ValidateFileIdentity(const MfdCorpusFileIdentity& identity) {
    return identity.valid && identity.byteLength > 0u &&
        LooksLikeSha256(identity.sha256);
}

} // namespace

bool DeserializeMfdCorpusIntakeSpec(
    const nlohmann::json& value,
    const std::filesystem::path& baseDirectory,
    MfdCorpusIntakeSpec& spec,
    std::string* error) {
    spec = {};
    if (!value.is_object() ||
        value.value("contractId", std::string()) != kCorpusIntakeContractId ||
        value.value("contractVersion", 0u) != kCorpusIntakeContractVersion) {
        return Fail(error, "The MFD corpus intake contract ID or version is invalid.");
    }
    const auto entries = value.find("entries");
    if (entries == value.end() || !entries->is_array() || entries->empty()) {
        return Fail(error, "The MFD corpus intake definition has no entries.");
    }
    std::set<std::string> sampleIds;
    for (const nlohmann::json& entryValue : *entries) {
        if (!entryValue.is_object()) {
            return Fail(error, "An MFD corpus intake entry is not an object.");
        }
        MfdCorpusSampleSpec entry;
        entry.sampleId = entryValue.value("sampleId", std::string());
        if (entry.sampleId.empty() || !sampleIds.insert(entry.sampleId).second) {
            return Fail(error, "MFD corpus sample IDs must be present and unique.");
        }
        if (!ParseLayer(entryValue.value("layer", std::string()), entry.layer)) {
            return Fail(error, "An MFD corpus entry has an invalid layer.");
        }
        entry.cameraModeLabel =
            entryValue.value("cameraModeLabel", std::string());
        const auto frameValues = entryValue.find("frames");
        if (frameValues == entryValue.end() || !frameValues->is_array() ||
            frameValues->size() < 2u) {
            return Fail(error, "Every MFD corpus entry requires at least two frames.");
        }
        for (const nlohmann::json& frameValue : *frameValues) {
            if (!frameValue.is_string() || frameValue.get<std::string>().empty()) {
                return Fail(error, "MFD corpus frame paths must be nonempty strings.");
            }
            entry.framePaths.push_back(
                ResolvePath(baseDirectory, frameValue.get<std::string>()));
        }
        const auto reference = entryValue.find("referenceFrameIndex");
        if (reference == entryValue.end() || !reference->is_number_unsigned()) {
            return Fail(error, "Every MFD corpus entry requires an unsigned referenceFrameIndex.");
        }
        entry.referenceFrameIndex = reference->get<std::uint64_t>();
        if (entry.referenceFrameIndex >= entry.framePaths.size()) {
            return Fail(error, "An MFD corpus referenceFrameIndex is outside its frame list.");
        }
        const auto groundTruth = entryValue.find("referenceMomentGroundTruth");
        if (groundTruth != entryValue.end() && !groundTruth->is_null()) {
            if (!groundTruth->is_string() ||
                groundTruth->get<std::string>().empty()) {
                return Fail(error, "MFD reference-moment ground truth must be a path or null.");
            }
            entry.referenceMomentGroundTruthPath = ResolvePath(
                baseDirectory, groundTruth->get<std::string>());
        }
        spec.entries.push_back(std::move(entry));
    }
    if (error) error->clear();
    return true;
}

nlohmann::json SerializeMfdCorpusIntakeTemplate() {
    return {
        { "contractId", kCorpusIntakeContractId },
        { "contractVersion", kCorpusIntakeContractVersion },
        { "entries", nlohmann::json::array({
            {
                { "sampleId", "controlled-camera-a-static-2" },
                { "layer", "controlled-camera" },
                { "cameraModeLabel", "Camera A / full resolution / ISO 800" },
                { "referenceFrameIndex", 0u },
                { "frames", nlohmann::json::array({
                    "bursts/camera-a-static/frame-01.dng",
                    "bursts/camera-a-static/frame-02.dng"
                }) },
                { "referenceMomentGroundTruth",
                    "ground-truth/camera-a-static-reference.pfm" }
            },
            {
                { "sampleId", "uncontrolled-camera-b-motion-2" },
                { "layer", "uncontrolled-real" },
                { "cameraModeLabel", "Camera B / full resolution / ISO 1600" },
                { "referenceFrameIndex", 0u },
                { "frames", nlohmann::json::array({
                    "bursts/camera-b-motion/frame-01.raw",
                    "bursts/camera-b-motion/frame-02.raw"
                }) },
                { "referenceMomentGroundTruth", nullptr }
            }
        }) }
    };
}

MfdCorpusIntakeServices MakeFilesystemMfdCorpusIntakeServices() {
    MfdCorpusIntakeServices services;
    services.identifyFile = [](
        const std::filesystem::path& path,
        MfdCorpusFileIdentity& output,
        std::string& error) {
        output = {};
        const Stack::RawEvidence::SourceIdentity identity =
            Stack::RawEvidence::ComputeSourceIdentity(path);
        if (!identity.valid) {
            error = "Could not hash " + path.string() + ": " + identity.reason;
            return false;
        }
        output.valid = true;
        output.sha256 = identity.sha256;
        output.byteLength = identity.byteSize;
        error.clear();
        return true;
    };
    services.inspectRawFrame = [identify = services.identifyFile](
        const std::filesystem::path& path,
        MfdInspectedCorpusFrame& output,
        std::string& error) {
        output = {};
        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(path, filesystemError) ||
            filesystemError) {
            error = "The corpus frame is not a readable regular file: " +
                path.string();
            return false;
        }
        const std::uint64_t sizeBefore =
            std::filesystem::file_size(path, filesystemError);
        if (filesystemError) {
            error = "Could not inspect the corpus frame size: " + path.string();
            return false;
        }
        const auto writeTimeBefore =
            std::filesystem::last_write_time(path, filesystemError);
        if (filesystemError) {
            error = "Could not inspect the corpus frame timestamp: " + path.string();
            return false;
        }
        Raw::RawMetadata metadata;
        if (!Raw::RawLoader::LoadMetadata(path.string(), metadata)) {
            error = metadata.error.empty()
                ? "The RAW header could not be inspected: " + path.string()
                : path.string() + ": " + metadata.error;
            return false;
        }
        if (!identify(path, output.identity, error)) return false;
        const std::uint64_t sizeAfter =
            std::filesystem::file_size(path, filesystemError);
        if (filesystemError) {
            error = "Could not recheck the corpus frame size: " + path.string();
            return false;
        }
        const auto writeTimeAfter =
            std::filesystem::last_write_time(path, filesystemError);
        if (filesystemError || sizeBefore != sizeAfter ||
            sizeAfter != output.identity.byteLength ||
            writeTimeBefore != writeTimeAfter) {
            error = "The corpus frame changed while it was being locked: " +
                path.string();
            return false;
        }
        output.compatibility =
            Stack::Project::BuildRawCaptureCompatibilitySummary(metadata);
        if (!output.compatibility.supported) {
            error = path.filename().string() + ": " +
                output.compatibility.rejectionReason;
            return false;
        }
        error.clear();
        return true;
    };
    return services;
}

bool LockMfdCorpusIntake(
    const MfdCorpusIntakeSpec& spec,
    const MfdCorpusIntakeServices& services,
    MfdCorpusIntakeReport& report,
    std::string* error) {
    report = {};
    if (spec.contractVersion != kCorpusIntakeContractVersion ||
        spec.contractId != kCorpusIntakeContractId || spec.entries.empty() ||
        !services.inspectRawFrame || !services.identifyFile) {
        return Fail(error, "The MFD corpus intake request is invalid.");
    }
    std::set<std::string> sampleIds;
    std::vector<MfdCorpusEntryEvidence> evidenceEntries;
    for (const MfdCorpusSampleSpec& source : spec.entries) {
        MfdLockedCorpusEntry entry;
        entry.evidence.sampleId = source.sampleId;
        entry.evidence.layer = source.layer;
        entry.cameraModeLabel = source.cameraModeLabel;
        entry.referenceFrameIndex = source.referenceFrameIndex;
        const auto reject = [&](const std::string& message) {
            entry.valid = false;
            entry.message = message;
            report.errors.push_back(source.sampleId + ": " + message);
        };
        if (source.sampleId.empty() || !sampleIds.insert(source.sampleId).second) {
            reject("The sample ID is empty or duplicated.");
            report.entries.push_back(std::move(entry));
            continue;
        }
        if (!ValidLayer(source.layer)) {
            reject("The corpus layer is invalid.");
            report.entries.push_back(std::move(entry));
            continue;
        }
        if (source.framePaths.size() < 2u ||
            source.framePaths.size() >
                static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) ||
            source.referenceFrameIndex >= source.framePaths.size()) {
            reject("The frame list or reference selection is invalid.");
            report.entries.push_back(std::move(entry));
            continue;
        }
        std::unordered_set<std::string> sourceIdentities;
        bool entryValid = true;
        Stack::Project::RawCaptureCompatibilitySummary referenceCompatibility;
        bool haveCompatibility = false;
        for (std::size_t index = 0u; index < source.framePaths.size(); ++index) {
            MfdInspectedCorpusFrame inspected;
            std::string inspectError;
            if (!services.inspectRawFrame(
                    source.framePaths[index], inspected, inspectError) ||
                !ValidateFileIdentity(inspected.identity) ||
                !inspected.compatibility.supported) {
                reject(inspectError.empty()
                    ? "A frame did not produce a valid locked RAW identity."
                    : inspectError);
                entryValid = false;
                break;
            }
            const std::string duplicateKey = inspected.identity.sha256 + ":" +
                std::to_string(inspected.identity.byteLength);
            if (!sourceIdentities.insert(duplicateKey).second) {
                reject("The same source content appears more than once in the burst.");
                entryValid = false;
                break;
            }
            if (haveCompatibility) {
                std::string compatibilityError;
                if (!Stack::Project::AreMfdCapturesStructurallyCompatible(
                        referenceCompatibility,
                        inspected.compatibility,
                        &compatibilityError)) {
                    reject(compatibilityError);
                    entryValid = false;
                    break;
                }
            } else {
                referenceCompatibility = inspected.compatibility;
                haveCompatibility = true;
            }
            MfdLockedCorpusFrame frame;
            frame.informationalSourcePath = source.framePaths[index];
            frame.identity = inspected.identity;
            frame.compatibility = inspected.compatibility;
            frame.referenceFrame = index == source.referenceFrameIndex;
            entry.frames.push_back(std::move(frame));
        }
        if (!entryValid) {
            report.entries.push_back(std::move(entry));
            continue;
        }
        const bool groundTruthRequired =
            source.layer != MfdCorpusLayer::UncontrolledReal;
        if (source.referenceMomentGroundTruthPath.empty()) {
            if (groundTruthRequired) {
                reject("Synthetic and controlled samples require reference-moment ground truth.");
                report.entries.push_back(std::move(entry));
                continue;
            }
        } else {
            std::string identityError;
            if (!services.identifyFile(
                    source.referenceMomentGroundTruthPath,
                    entry.groundTruthIdentity,
                    identityError) ||
                !ValidateFileIdentity(entry.groundTruthIdentity)) {
                reject(identityError.empty()
                    ? "Reference-moment ground truth could not be locked."
                    : identityError);
                report.entries.push_back(std::move(entry));
                continue;
            }
            entry.informationalGroundTruthPath =
                source.referenceMomentGroundTruthPath;
        }
        entry.evidence.sourceContentSetSha256 = SourceSetIdentity(entry);
        entry.evidence.cameraModeId = source.layer == MfdCorpusLayer::Synthetic
            ? "synthetic"
            : CameraModeIdentity(referenceCompatibility);
        entry.evidence.frameCount =
            static_cast<std::uint32_t>(source.framePaths.size());
        entry.evidence.hasReferenceMomentGroundTruth =
            entry.groundTruthIdentity.valid;
        entry.evidence.metricsComplete = false;
        entry.evidence.visualReviewComplete = false;
        entry.valid = LooksLikeSha256(entry.evidence.sourceContentSetSha256) &&
            !entry.evidence.cameraModeId.empty();
        entry.message = entry.valid
            ? "The ordered burst and ground-truth identity are locked."
            : "The corpus entry identity could not be generated.";
        if (!entry.valid) report.errors.push_back(source.sampleId + ": " + entry.message);
        if (entry.valid) evidenceEntries.push_back(entry.evidence);
        report.entries.push_back(std::move(entry));
    }
    if (!report.errors.empty() || evidenceEntries.size() != spec.entries.size()) {
        report.valid = true;
        report.manifestLocked = false;
        report.message =
            "The MFD corpus was not locked because one or more entries failed validation.";
        return Fail(error, report.message);
    }
    std::string manifestError;
    if (!ComputeMfdCorpusManifestSha256(
            evidenceEntries, report.manifestSha256, &manifestError)) {
        report.valid = true;
        report.message = manifestError;
        return Fail(error, manifestError);
    }
    report.valid = true;
    report.manifestLocked = true;
    report.message =
        "The MFD source corpus is content-addressed and locked for evaluation.";
    if (error) error->clear();
    return true;
}

nlohmann::json SerializeMfdCorpusIntakeReport(
    const MfdCorpusIntakeReport& report) {
    nlohmann::json entries = nlohmann::json::array();
    std::vector<MfdCorpusEntryEvidence> evidenceEntries;
    for (const MfdLockedCorpusEntry& entry : report.entries) {
        nlohmann::json frames = nlohmann::json::array();
        for (const MfdLockedCorpusFrame& frame : entry.frames) {
            frames.push_back({
                { "informationalSourcePath",
                    frame.informationalSourcePath.generic_u8string() },
                { "identity", SerializeFileIdentity(frame.identity) },
                { "referenceFrame", frame.referenceFrame },
                { "compatibility",
                    Stack::Project::SerializeRawCaptureCompatibilitySummary(
                        frame.compatibility) }
            });
        }
        entries.push_back({
            { "valid", entry.valid },
            { "message", entry.message },
            { "sampleId", entry.evidence.sampleId },
            { "sourceContentSetSha256",
                entry.evidence.sourceContentSetSha256 },
            { "layer", MfdCorpusLayerName(entry.evidence.layer) },
            { "cameraModeId", entry.evidence.cameraModeId },
            { "cameraModeLabel", entry.cameraModeLabel },
            { "frameCount", entry.evidence.frameCount },
            { "referenceFrameIndex", entry.referenceFrameIndex },
            { "frames", std::move(frames) },
            { "informationalGroundTruthPath",
                entry.informationalGroundTruthPath.empty()
                    ? nlohmann::json(nullptr)
                    : nlohmann::json(
                        entry.informationalGroundTruthPath.generic_u8string()) },
            { "groundTruthIdentity",
                SerializeFileIdentity(entry.groundTruthIdentity) },
            { "hasReferenceMomentGroundTruth",
                entry.evidence.hasReferenceMomentGroundTruth },
            { "metricsComplete", entry.evidence.metricsComplete },
            { "visualReviewComplete", entry.evidence.visualReviewComplete }
        });
        if (entry.valid) evidenceEntries.push_back(entry.evidence);
    }
    return {
        { "reportContractId", kLockedCorpusReportContractId },
        { "intakeContractId", kCorpusIntakeContractId },
        { "contractVersion", kCorpusIntakeContractVersion },
        { "valid", report.valid },
        { "manifestLocked", report.manifestLocked },
        { "message", report.message },
        { "manifestSha256", report.manifestSha256 },
        { "errors", report.errors },
        { "entries", std::move(entries) },
        { "releaseManifest", evidenceEntries.empty()
            ? nlohmann::json(nullptr)
            : SerializeMfdCorpusManifest(evidenceEntries) }
    };
}

} // namespace Raw::Mfd
