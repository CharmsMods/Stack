#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Corpus.h"
#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Finish(std::string* output, const std::string& message, bool result) {
    if (output) *output = message;
    return result;
}

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD corpus intake validation failed: "
                  << message << std::endl;
    }
    return condition;
}

bool WriteJsonRecoverably(
    const std::filesystem::path& path,
    const nlohmann::json& value,
    std::string& error) {
    if (path.empty()) {
        error = "The JSON output path is empty.";
        return false;
    }
    std::error_code filesystemError;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystemError);
        if (filesystemError) {
            error = "The JSON output directory could not be created.";
            return false;
        }
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::filesystem::remove(temporary, filesystemError);
    filesystemError.clear();
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "The temporary JSON output could not be opened.";
            return false;
        }
        output << value.dump(2) << '\n';
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, filesystemError);
            error = "The temporary JSON output was incomplete.";
            return false;
        }
    }
    std::filesystem::path previous = path;
    previous += ".previous";
    const bool hadPrevious =
        std::filesystem::exists(path, filesystemError) && !filesystemError;
    if (hadPrevious) {
        std::filesystem::remove(previous, filesystemError);
        filesystemError.clear();
        std::filesystem::rename(path, previous, filesystemError);
        if (filesystemError) {
            std::filesystem::remove(temporary, filesystemError);
            error = "The prior JSON output could not be retained.";
            return false;
        }
    }
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        if (hadPrevious) {
            std::error_code restoreError;
            std::filesystem::rename(previous, path, restoreError);
        }
        std::filesystem::remove(temporary, filesystemError);
        error = "The JSON output could not be published atomically.";
        return false;
    }
    error.clear();
    return true;
}

bool ReadJson(
    const std::filesystem::path& path,
    nlohmann::json& value,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "The corpus definition could not be opened.";
        return false;
    }
    try {
        input >> value;
    } catch (const std::exception& exception) {
        error = std::string("The corpus definition is invalid JSON: ") +
            exception.what();
        return false;
    }
    error.clear();
    return true;
}

Raw::Mfd::MfdCorpusFileIdentity IdentityForText(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    const Stack::RawEvidence::SourceIdentity identity =
        Stack::RawEvidence::ComputeSourceIdentity(bytes);
    return { identity.valid, identity.sha256, identity.byteSize };
}

Raw::RawMetadata CompatibleMetadata(const std::filesystem::path& path) {
    const std::string name = path.filename().string();
    Raw::RawMetadata metadata;
    metadata.cameraMake = "Test Camera Company";
    metadata.cameraModel = name.find("cam-b") != std::string::npos
        ? "Camera B"
        : "Camera A";
    metadata.dngUniqueCameraModel = metadata.cameraModel;
    metadata.rawWidth = name.find("incompatible") != std::string::npos
        ? 4032
        : 4000;
    metadata.rawHeight = 3000;
    metadata.visibleWidth = metadata.rawWidth;
    metadata.visibleHeight = metadata.rawHeight;
    metadata.bitDepth = 14;
    metadata.orientation = name.find("orientation-8") != std::string::npos
        ? 8
        : 1;
    metadata.cfaPattern = Raw::CfaPattern::RGGB;
    metadata.pixelLayout = name.find("linear-rgb") != std::string::npos
        ? Raw::RawPixelLayout::LinearRgb
        : Raw::RawPixelLayout::MosaicBayer;
    metadata.mosaiced =
        metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer;
    return metadata;
}

Raw::Mfd::MfdCorpusIntakeServices FakeServices() {
    Raw::Mfd::MfdCorpusIntakeServices services;
    services.identifyFile = [](
        const std::filesystem::path& path,
        Raw::Mfd::MfdCorpusFileIdentity& identity,
        std::string& error) {
        const std::string name = path.filename().string();
        if (name.find("missing") != std::string::npos) {
            error = "The requested test file is missing.";
            return false;
        }
        const std::string content = name.find("same-content") != std::string::npos
            ? "duplicate-content"
            : path.generic_u8string();
        identity = IdentityForText(content);
        error.clear();
        return true;
    };
    services.inspectRawFrame = [identify = services.identifyFile](
        const std::filesystem::path& path,
        Raw::Mfd::MfdInspectedCorpusFrame& frame,
        std::string& error) {
        frame = {};
        if (!identify(path, frame.identity, error)) return false;
        frame.compatibility =
            Stack::Project::BuildRawCaptureCompatibilitySummary(
                CompatibleMetadata(path));
        if (!frame.compatibility.supported) {
            error = frame.compatibility.rejectionReason;
            return false;
        }
        error.clear();
        return true;
    };
    return services;
}

nlohmann::json ValidDefinition() {
    return {
        { "contractId", Raw::Mfd::kCorpusIntakeContractId },
        { "contractVersion", Raw::Mfd::kCorpusIntakeContractVersion },
        { "entries", nlohmann::json::array({
            {
                { "sampleId", "controlled-a-2" },
                { "layer", "controlled-camera" },
                { "cameraModeLabel", "Camera A test mode" },
                { "referenceFrameIndex", 0u },
                { "frames", nlohmann::json::array({
                    "camera-a/frame-01.dng",
                    "camera-a/frame-02.dng"
                }) },
                { "referenceMomentGroundTruth", "truth/a.pfm" }
            },
            {
                { "sampleId", "real-b-2" },
                { "layer", "uncontrolled-real" },
                { "cameraModeLabel", "Camera B test mode" },
                { "referenceFrameIndex", 1u },
                { "frames", nlohmann::json::array({
                    "camera-b/cam-b-frame-01.dng",
                    "camera-b/cam-b-frame-02.dng"
                }) },
                { "referenceMomentGroundTruth", nullptr }
            }
        }) }
    };
}

bool ParseAndLock(
    const nlohmann::json& definition,
    Raw::Mfd::MfdCorpusIntakeReport& report,
    std::string& error) {
    Raw::Mfd::MfdCorpusIntakeSpec spec;
    if (!Raw::Mfd::DeserializeMfdCorpusIntakeSpec(
            definition,
            std::filesystem::path("C:/mfd-corpus"),
            spec,
            &error)) {
        return false;
    }
    return Raw::Mfd::LockMfdCorpusIntake(
        spec, FakeServices(), report, &error);
}

} // namespace

bool WriteMfdCorpusIntakeTemplate(
    const std::string& outputPath,
    std::string* errorMessage) {
    const std::filesystem::path output =
        std::filesystem::u8path(outputPath);
    if (!output.is_absolute()) {
        return Finish(
            errorMessage,
            "The MFD corpus template output path must be absolute.",
            false);
    }
    std::string error;
    if (!WriteJsonRecoverably(
            output.lexically_normal(),
            Raw::Mfd::SerializeMfdCorpusIntakeTemplate(),
            error)) {
        return Finish(errorMessage, error, false);
    }
    return Finish(errorMessage, std::string(), true);
}

bool LockMfdCorpusDefinition(
    const std::string& definitionPath,
    const std::string& outputPath,
    std::string* errorMessage) {
    const std::filesystem::path definition =
        std::filesystem::u8path(definitionPath).lexically_normal();
    const std::filesystem::path output =
        std::filesystem::u8path(outputPath).lexically_normal();
    if (definition.empty() || output.empty() || !definition.is_absolute() ||
        !output.is_absolute() || definition == output) {
        return Finish(
            errorMessage,
            "Definition and output must be different absolute paths.",
            false);
    }
    nlohmann::json value;
    std::string error;
    if (!ReadJson(definition, value, error)) {
        return Finish(errorMessage, error, false);
    }
    Raw::Mfd::MfdCorpusIntakeSpec spec;
    if (!Raw::Mfd::DeserializeMfdCorpusIntakeSpec(
            value, definition.parent_path(), spec, &error)) {
        return Finish(errorMessage, error, false);
    }
    Raw::Mfd::MfdCorpusIntakeReport report;
    const bool locked = Raw::Mfd::LockMfdCorpusIntake(
        spec,
        Raw::Mfd::MakeFilesystemMfdCorpusIntakeServices(),
        report,
        &error);
    std::string writeError;
    if (!WriteJsonRecoverably(
            output,
            Raw::Mfd::SerializeMfdCorpusIntakeReport(report),
            writeError)) {
        return Finish(errorMessage, writeError, false);
    }
    if (!locked) {
        return Finish(
            errorMessage,
            error + " Inspect the written report for entry-level errors.",
            false);
    }
    return Finish(errorMessage, std::string(), true);
}

bool ValidateMfdCorpusIntake() {
    bool ok = true;
    std::string error;
    Raw::Mfd::MfdCorpusIntakeReport baseline;
    ok &= Check(ParseAndLock(ValidDefinition(), baseline, error),
        "a valid controlled/uncontrolled definition did not lock: " + error);
    ok &= Check(baseline.valid && baseline.manifestLocked &&
            baseline.manifestSha256.size() == 64u &&
            baseline.entries.size() == 2u &&
            baseline.entries[0].evidence.hasReferenceMomentGroundTruth &&
            !baseline.entries[1].evidence.hasReferenceMomentGroundTruth &&
            baseline.entries[0].evidence.cameraModeId !=
                baseline.entries[1].evidence.cameraModeId,
        "the locked manifest lost ground-truth or derived camera-mode identity");
    const nlohmann::json serialized =
        Raw::Mfd::SerializeMfdCorpusIntakeReport(baseline);
    ok &= Check(serialized.value("reportContractId", "") ==
            Raw::Mfd::kLockedCorpusReportContractId &&
            serialized.value("manifestLocked", false) &&
            serialized["releaseManifest"].is_object(),
        "the locked report lost its contract or release manifest");

    nlohmann::json labelChanged = ValidDefinition();
    labelChanged["entries"][0]["cameraModeLabel"] = "A renamed label";
    Raw::Mfd::MfdCorpusIntakeReport relabeled;
    error.clear();
    ok &= Check(ParseAndLock(labelChanged, relabeled, error) &&
            relabeled.manifestSha256 == baseline.manifestSha256,
        "an informational camera-mode label changed corpus identity");

    nlohmann::json frameReordered = ValidDefinition();
    std::swap(
        frameReordered["entries"][0]["frames"][0],
        frameReordered["entries"][0]["frames"][1]);
    Raw::Mfd::MfdCorpusIntakeReport reordered;
    error.clear();
    ok &= Check(ParseAndLock(frameReordered, reordered, error) &&
            reordered.manifestSha256 != baseline.manifestSha256,
        "ordered frame identity did not invalidate the locked manifest");

    nlohmann::json referenceChanged = ValidDefinition();
    referenceChanged["entries"][0]["referenceFrameIndex"] = 1u;
    Raw::Mfd::MfdCorpusIntakeReport changedReference;
    error.clear();
    ok &= Check(ParseAndLock(referenceChanged, changedReference, error) &&
            changedReference.manifestSha256 != baseline.manifestSha256,
        "reference selection did not invalidate the locked manifest");

    nlohmann::json noTruth = ValidDefinition();
    noTruth["entries"][0]["referenceMomentGroundTruth"] = nullptr;
    Raw::Mfd::MfdCorpusIntakeReport rejected;
    error.clear();
    ok &= Check(!ParseAndLock(noTruth, rejected, error) &&
            rejected.valid && !rejected.manifestLocked &&
            !rejected.errors.empty(),
        "a controlled sample without ground truth was locked");

    nlohmann::json incompatible = ValidDefinition();
    incompatible["entries"][0]["frames"][1] =
        "camera-a/incompatible-frame.dng";
    error.clear();
    ok &= Check(!ParseAndLock(incompatible, rejected, error) &&
            !rejected.manifestLocked,
        "an incompatible sensor geometry was locked");

    nlohmann::json orientationMismatch = ValidDefinition();
    orientationMismatch["entries"][0]["frames"][1] =
        "camera-a/orientation-8-frame.dng";
    error.clear();
    ok &= Check(!ParseAndLock(orientationMismatch, rejected, error) &&
            !rejected.manifestLocked,
        "inconsistent sensor orientation metadata was locked");

    nlohmann::json duplicate = ValidDefinition();
    duplicate["entries"][0]["frames"] = nlohmann::json::array({
        "camera-a/same-content-a.dng",
        "camera-a/same-content-b.dng"
    });
    error.clear();
    ok &= Check(!ParseAndLock(duplicate, rejected, error) &&
            !rejected.manifestLocked,
        "duplicate source content was locked within one burst");

    nlohmann::json linearRgb = ValidDefinition();
    linearRgb["entries"][0]["frames"][0] =
        "camera-a/linear-rgb-frame.dng";
    error.clear();
    ok &= Check(!ParseAndLock(linearRgb, rejected, error) &&
            !rejected.manifestLocked,
        "a demosaiced Linear RGB RAW crossed the mosaic-only boundary");

    nlohmann::json malformed = ValidDefinition();
    malformed["contractVersion"] = 99u;
    Raw::Mfd::MfdCorpusIntakeSpec malformedSpec;
    error.clear();
    ok &= Check(!Raw::Mfd::DeserializeMfdCorpusIntakeSpec(
            malformed,
            std::filesystem::path("C:/mfd-corpus"),
            malformedSpec,
            &error),
        "an unknown corpus intake contract version was accepted");

    if (ok) {
        std::cout
            << "MFD corpus intake validation passed; real RAW files can now "
            << "be compatibility-checked and content-locked without enabling output."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
