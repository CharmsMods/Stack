#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Corpus.h"
#include "Raw/MultiFrameDenoise/Evaluation.h"
#include "Raw/MultiFrameDenoise/Inspection.h"
#include "Raw/MultiFrameDenoise/Processor.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

namespace Stack::Validation {
namespace {

bool Finish(std::string* output, const std::string& message, bool result) {
    if (output) *output = message;
    return result;
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
    return true;
}

std::filesystem::path UniqueSibling(
    const std::filesystem::path& target,
    const char* label) {
    static std::atomic<std::uint64_t> counter { 0u };
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    std::filesystem::path result = target;
    result += std::string(".") + label + "-" + std::to_string(stamp) +
        "-" + std::to_string(counter.fetch_add(1u));
    return result;
}

bool WritePfm(
    const std::filesystem::path& path,
    const Raw::Mfd::PublishedFusionResult& result,
    std::string& error) {
    if (result.extent.width >
            static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ||
        result.extent.height >
            static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        error = "The normalized mosaic is too large for the PFM interchange file.";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "The temporary normalized Bayer PFM could not be opened.";
        return false;
    }
    output << "Pf\n" << result.extent.width << ' ' << result.extent.height
           << "\n-1.0\n";
    for (std::uint64_t row = result.extent.height; row > 0u; --row) {
        const std::size_t offset = static_cast<std::size_t>(
            (row - 1u) * result.extent.width);
        output.write(
            reinterpret_cast<const char*>(
                result.normalizedMosaic.data() + offset),
            static_cast<std::streamsize>(
                result.extent.width * sizeof(float)));
    }
    output.flush();
    if (!output) {
        error = "The temporary normalized Bayer PFM was incomplete.";
        return false;
    }
    return true;
}

bool WriteJson(
    const std::filesystem::path& path,
    const nlohmann::json& value,
    std::string& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = "The temporary MFD diagnostics file could not be opened.";
        return false;
    }
    output << value.dump(2) << '\n';
    output.flush();
    if (!output) {
        error = "The temporary MFD diagnostics file was incomplete.";
        return false;
    }
    return true;
}

Raw::CfaPattern ParseCfaPattern(const std::string& value) {
    if (value == "RGGB") return Raw::CfaPattern::RGGB;
    if (value == "BGGR") return Raw::CfaPattern::BGGR;
    if (value == "GBRG") return Raw::CfaPattern::GBRG;
    if (value == "GRBG") return Raw::CfaPattern::GRBG;
    return Raw::CfaPattern::Unknown;
}

std::uint32_t ByteSwap32(std::uint32_t value) {
    return ((value & 0x000000ffu) << 24u) |
        ((value & 0x0000ff00u) << 8u) |
        ((value & 0x00ff0000u) >> 8u) |
        ((value & 0xff000000u) >> 24u);
}

bool ReadMonochromePfm(
    const std::filesystem::path& path,
    Raw::Mfd::PixelExtent expectedExtent,
    std::vector<float>& samples,
    std::string& error) {
    samples.clear();
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    std::uint64_t width = 0u;
    std::uint64_t height = 0u;
    double scale = 0.0;
    if (!input || !(input >> magic >> width >> height >> scale) ||
        magic != "Pf" || width != expectedExtent.width ||
        height != expectedExtent.height || !std::isfinite(scale) ||
        scale == 0.0 || width == 0u || height == 0u ||
        width > std::numeric_limits<std::size_t>::max() / height) {
        error =
            "Reference-moment ground truth must be a matching monochrome float32 PFM.";
        return false;
    }
    const int separator = input.get();
    if (separator == '\r' && input.peek() == '\n') input.get();
    if (separator != '\n' && separator != '\r') {
        error = "The reference-moment PFM header terminator is invalid.";
        return false;
    }
    const std::size_t count = static_cast<std::size_t>(width * height);
    std::vector<float> fileOrder(count);
    input.read(
        reinterpret_cast<char*>(fileOrder.data()),
        static_cast<std::streamsize>(count * sizeof(float)));
    if (!input) {
        error = "The reference-moment PFM payload is incomplete.";
        return false;
    }
    const bool bigEndianPayload = scale > 0.0;
    const double scaleMagnitude = std::abs(scale);
    samples.resize(count);
    for (std::uint64_t fileRow = 0u; fileRow < height; ++fileRow) {
        const std::uint64_t outputRow = height - 1u - fileRow;
        for (std::uint64_t x = 0u; x < width; ++x) {
            float value = fileOrder[static_cast<std::size_t>(
                fileRow * width + x)];
            if (bigEndianPayload) {
                std::uint32_t bits = 0u;
                std::memcpy(&bits, &value, sizeof(bits));
                bits = ByteSwap32(bits);
                std::memcpy(&value, &bits, sizeof(value));
            }
            value = static_cast<float>(value * scaleMagnitude);
            if (!std::isfinite(value)) {
                samples.clear();
                error = "The reference-moment PFM contains a non-finite sample.";
                return false;
            }
            samples[static_cast<std::size_t>(outputRow * width + x)] = value;
        }
    }
    return true;
}

bool PublishDirectory(
    const std::filesystem::path& staging,
    const std::filesystem::path& output,
    std::string& error) {
    std::error_code filesystemError;
    if (!std::filesystem::exists(staging, filesystemError) || filesystemError) {
        error = "The staged MFD result directory is unavailable.";
        return false;
    }
    std::filesystem::path previous;
    const bool hadPrevious =
        std::filesystem::exists(output, filesystemError) && !filesystemError;
    if (hadPrevious) {
        previous = UniqueSibling(output, "previous");
        std::filesystem::rename(output, previous, filesystemError);
        if (filesystemError) {
            error = "The prior MFD output could not be retained before publication.";
            return false;
        }
    }
    std::filesystem::rename(staging, output, filesystemError);
    if (filesystemError) {
        if (hadPrevious) {
            std::error_code restoreError;
            std::filesystem::rename(previous, output, restoreError);
        }
        error = "The MFD result directory could not be published atomically.";
        return false;
    }
    return true;
}

} // namespace

bool RunMfdCorpusEntry(
    const std::string& definitionPath,
    const std::string& sampleId,
    const std::string& outputDirectory,
    std::uint64_t memoryBudgetBytes,
    std::uint32_t workerCount,
    const std::string& backendId,
    const std::string& alignmentModeId,
    std::string* errorMessage) {
    const std::filesystem::path definition =
        std::filesystem::u8path(definitionPath).lexically_normal();
    const std::filesystem::path output =
        std::filesystem::u8path(outputDirectory).lexically_normal();
    if (!definition.is_absolute() || !output.is_absolute() ||
        sampleId.empty() || memoryBudgetBytes == 0u || workerCount == 0u ||
        definition == output) {
        return Finish(
            errorMessage,
            "Definition and output must be distinct absolute paths, with a sample ID and nonzero resource limits.",
            false);
    }

    nlohmann::json definitionJson;
    std::string error;
    if (!ReadJson(definition, definitionJson, error)) {
        return Finish(errorMessage, error, false);
    }
    Raw::Mfd::MfdCorpusIntakeSpec spec;
    if (!Raw::Mfd::DeserializeMfdCorpusIntakeSpec(
            definitionJson, definition.parent_path(), spec, &error)) {
        return Finish(errorMessage, error, false);
    }
    Raw::Mfd::MfdCorpusIntakeReport locked;
    if (!Raw::Mfd::LockMfdCorpusIntake(
            spec,
            Raw::Mfd::MakeFilesystemMfdCorpusIntakeServices(),
            locked,
            &error)) {
        return Finish(
            errorMessage,
            error + " The corpus definition was not processed.",
            false);
    }
    const auto found = std::find_if(
        locked.entries.begin(),
        locked.entries.end(),
        [&sampleId](const Raw::Mfd::MfdLockedCorpusEntry& entry) {
            return entry.evidence.sampleId == sampleId;
        });
    if (found == locked.entries.end() || !found->valid) {
        return Finish(
            errorMessage,
            "The requested sample is not a valid entry in the locked corpus.",
            false);
    }

    const std::filesystem::path staging = UniqueSibling(output, "staging");
    std::error_code filesystemError;
    std::filesystem::create_directories(staging, filesystemError);
    if (filesystemError) {
        return Finish(
            errorMessage,
            "The staged MFD output directory could not be created.",
            false);
    }

    Raw::Mfd::MfdProcessingRequest request;
    if (backendId == "shared-burst") {
        request.fusionBackend = Raw::Mfd::MfdFusionBackend::SharedBurstV1;
    } else if (backendId == "legacy-ra-cfa") {
        request.fusionBackend = Raw::Mfd::MfdFusionBackend::LegacyRaCfaV1;
    } else {
        std::filesystem::remove_all(staging, filesystemError);
        return Finish(
            errorMessage,
            "The MFD corpus backend must be shared-burst or legacy-ra-cfa.",
            false);
    }
    if (!Raw::Mfd::ParseMfdAlignmentMode(
            alignmentModeId, request.alignmentMode)) {
        std::filesystem::remove_all(staging, filesystemError);
        return Finish(
            errorMessage,
            "The MFD corpus alignment mode must be full, translation-only, or identity.",
            false);
    }
    request.referenceFrameIndex = found->referenceFrameIndex;
    request.workingDirectory = staging / "working-cache";
    request.memoryBudgetBytes = memoryBudgetBytes;
    request.workerCount = workerCount;
    request.frames.reserve(found->frames.size());
    for (std::size_t index = 0u; index < found->frames.size(); ++index) {
        const Raw::Mfd::MfdLockedCorpusFrame& lockedFrame =
            found->frames[index];
        Raw::Mfd::MfdProcessingFrameInput frame;
        frame.stableFrameId = lockedFrame.identity.sha256 + "#" +
            std::to_string(index);
        frame.sourcePath = lockedFrame.informationalSourcePath;
        frame.expectedSourceSha256 = lockedFrame.identity.sha256;
        frame.expectedSourceByteLength = lockedFrame.identity.byteLength;
        frame.expectedVisibleExtent = {
            static_cast<std::uint64_t>(
                lockedFrame.compatibility.visibleWidth),
            static_cast<std::uint64_t>(
                lockedFrame.compatibility.visibleHeight)
        };
        request.frames.push_back(std::move(frame));
    }

    const Raw::Mfd::MfdProcessingResult processing =
        Raw::Mfd::ProcessMfdBurst(
            request, Raw::Mfd::MakeFilesystemMfdProcessingServices());
    if (!processing.published.result ||
        processing.status == Raw::Mfd::MfdProcessingStatus::Failed ||
        processing.status == Raw::Mfd::MfdProcessingStatus::Canceled) {
        std::filesystem::remove_all(staging, filesystemError);
        std::string failure = processing.message.empty()
            ? "The MFD processor did not publish a complete result."
            : processing.message;
        for (const Raw::Mfd::MfdProcessingFrameDiagnostic& frame :
             processing.diagnostics.frames) {
            failure += "\n- " + frame.stableFrameId + ": " +
                Raw::Mfd::DecisionReasonName(frame.decisionReason) +
                " - " + frame.message;
        }
        return Finish(errorMessage, failure, false);
    }

    const Raw::CfaPattern cfaPattern = ParseCfaPattern(
        found->frames[found->referenceFrameIndex].compatibility.cfaPattern);
    Raw::Mfd::MfdInspectionInput inspectionInput;
    inspectionInput.sampleId = sampleId;
    inspectionInput.cfaPattern = cfaPattern;
    inspectionInput.extent = processing.published.result->extent;
    inspectionInput.referenceNormalizedMosaic =
        processing.referenceNormalizedMosaic;
    inspectionInput.outputNormalizedMosaic =
        processing.published.result->normalizedMosaic;
    inspectionInput.fusionDiagnostics =
        processing.published.result->diagnostics;
    Raw::Mfd::MfdInspectionSummary inspection;
    if (!Raw::Mfd::WriteMfdInspectionPacket(
            inspectionInput,
            staging / "inspection",
            inspection,
            &error)) {
        std::filesystem::remove_all(staging, filesystemError);
        return Finish(errorMessage, error, false);
    }

    nlohmann::json diagnostics =
        Raw::Mfd::SerializeMfdProcessingResult(processing);
    diagnostics["corpusManifestSha256"] = locked.manifestSha256;
    diagnostics["sampleId"] = sampleId;
    diagnostics["parameters"] = Raw::Mfd::SerializeParameters(
        request.parameters);
    diagnostics["inspection"] =
        Raw::Mfd::SerializeMfdInspectionSummary(inspection);
    diagnostics["evaluation"] = {
        { "available", false },
        { "message",
            "No normalized reference-moment ground truth was declared for this sample." }
    };
    if (!found->informationalGroundTruthPath.empty()) {
        std::vector<float> groundTruth;
        std::string evaluationError;
        if (ReadMonochromePfm(
                found->informationalGroundTruthPath,
                processing.published.result->extent,
                groundTruth,
                evaluationError)) {
            Raw::Mfd::MfdEvaluationInput evaluationInput;
            evaluationInput.sampleId = sampleId;
            evaluationInput.cfaPattern = cfaPattern;
            evaluationInput.extent = processing.published.result->extent;
            evaluationInput.noiseFreeReferenceMosaic = std::move(groundTruth);
            evaluationInput.noisyReferenceMosaic =
                processing.referenceNormalizedMosaic;
            evaluationInput.outputMosaic =
                processing.published.result->normalizedMosaic;
            evaluationInput.fusionDiagnostics =
                processing.published.result->diagnostics;
            Raw::Mfd::MfdEvaluationResult evaluation;
            if (Raw::Mfd::EvaluateMfdOutput(
                    evaluationInput,
                    evaluation,
                    &evaluationError)) {
                std::vector<std::filesystem::path> evaluationFiles;
                if (!Raw::Mfd::WriteMfdVisualDiagnostics(
                        evaluationInput,
                        evaluation,
                        staging / "evaluation",
                        evaluationFiles,
                        &evaluationError)) {
                    diagnostics["evaluation"] = {
                        { "available", false },
                        { "message", evaluationError }
                    };
                } else {
                    diagnostics["evaluation"] =
                        Raw::Mfd::SerializeMfdEvaluationResult(evaluation);
                }
            } else {
                diagnostics["evaluation"] = {
                    { "available", false },
                    { "message", evaluationError }
                };
            }
        } else {
            diagnostics["evaluation"] = {
                { "available", false },
                { "message", evaluationError }
            };
        }
    }
    diagnostics["artifact"] = {
        { "filename", "normalized-bayer.pfm" },
        { "sampleType", "float32" },
        { "rowOrder", "bottom-to-top-pfm" },
        { "cfaPattern", found->frames[found->referenceFrameIndex]
                .compatibility.cfaPattern },
        { "inspectionDirectory", "inspection" },
        { "evaluationDirectory",
            found->informationalGroundTruthPath.empty()
                ? nullptr
                : nlohmann::json("evaluation") }
    };
    if (!WritePfm(
            staging / "normalized-bayer.pfm",
            *processing.published.result,
            error) ||
        !WriteJson(staging / "diagnostics.json", diagnostics, error)) {
        std::filesystem::remove_all(staging, filesystemError);
        return Finish(errorMessage, error, false);
    }

    const std::filesystem::path working = staging / "working-cache";
    if (working.parent_path() == staging) {
        std::filesystem::remove_all(working, filesystemError);
        if (filesystemError) {
            std::error_code cleanupError;
            std::filesystem::remove_all(staging, cleanupError);
            return Finish(
                errorMessage,
                "The disposable MFD working cache could not be removed before publication.",
                false);
        }
    }
    if (!output.parent_path().empty()) {
        std::filesystem::create_directories(
            output.parent_path(), filesystemError);
        if (filesystemError) {
            std::filesystem::remove_all(staging, filesystemError);
            return Finish(
                errorMessage,
                "The final MFD output parent directory could not be created.",
                false);
        }
    }
    if (!PublishDirectory(staging, output, error)) {
        std::filesystem::remove_all(staging, filesystemError);
        return Finish(errorMessage, error, false);
    }
    return Finish(errorMessage, std::string(), true);
}

} // namespace Stack::Validation
