#include "Raw/MultiFrameDenoise/Evaluation.h"

#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
}

std::string HashText(const std::string& value) {
    const std::vector<std::uint8_t> bytes(value.begin(), value.end());
    return Stack::RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

nlohmann::json SerializePerformance(
    const PerformanceMemoryTrace& performance) {
    nlohmann::json stages = nlohmann::json::array();
    for (const PerformanceStageTrace& stage : performance.stages) {
        stages.push_back({
            { "stage", stage.stage },
            { "wallTimeMs", stage.wallTimeMs }
        });
    }
    return {
        { "available", performance.available },
        { "stages", std::move(stages) },
        { "totalWallTimeMs", performance.totalWallTimeMs },
        { "peakResidentBytes", performance.peakResidentBytes },
        { "peakWorkingBytes", performance.peakWorkingBytes },
        { "cacheBytesRead", performance.cacheBytesRead },
        { "cacheBytesWritten", performance.cacheBytesWritten },
        { "cancellationLatencyMs", performance.cancellationLatencyMs },
        { "coldCacheWallTimeMs", performance.coldCacheWallTimeMs },
        { "warmCacheWallTimeMs", performance.warmCacheWallTimeMs }
    };
}

std::string SanitizeStem(const std::string& value) {
    std::string result;
    result.reserve(std::min<std::size_t>(value.size(), 80u));
    for (unsigned char character : value) {
        if (result.size() >= 80u) break;
        if (std::isalnum(character) || character == '-' || character == '_') {
            result.push_back(static_cast<char>(character));
        } else if (!result.empty() && result.back() != '-') {
            result.push_back('-');
        }
    }
    while (!result.empty() && result.back() == '-') result.pop_back();
    return result.empty() ? "mfd-sample" : result;
}

bool ReplaceTemporary(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination,
    std::string* error) {
    std::error_code filesystemError;
    if (std::filesystem::exists(destination, filesystemError) &&
        !filesystemError) {
        std::filesystem::remove(destination, filesystemError);
    }
    if (filesystemError) {
        std::filesystem::remove(temporary, filesystemError);
        return Fail(error, "MFD diagnostic could not replace an old artifact.");
    }
    std::filesystem::rename(temporary, destination, filesystemError);
    if (filesystemError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return Fail(error, "MFD diagnostic artifact publication failed.");
    }
    return true;
}

bool WriteTextAtomic(
    const std::filesystem::path& path,
    const std::string& text,
    std::string* error) {
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return Fail(error, "MFD diagnostic report could not be opened.");
    output << text;
    output.flush();
    const bool written = static_cast<bool>(output);
    output.close();
    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Fail(error, "MFD diagnostic report write was incomplete.");
    }
    return ReplaceTemporary(temporary, path, error);
}

bool WritePgm16Atomic(
    const std::filesystem::path& path,
    PixelExtent extent,
    const std::vector<double>& values,
    double scale,
    std::string* error) {
    if (values.size() != extent.width * extent.height ||
        !std::isfinite(scale) || scale <= 0.0 ||
        extent.width > std::numeric_limits<std::uint32_t>::max() ||
        extent.height > std::numeric_limits<std::uint32_t>::max()) {
        return Fail(error, "MFD diagnostic image contract is invalid.");
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return Fail(error, "MFD diagnostic image could not be opened.");
    output << "P5\n" << extent.width << ' ' << extent.height << "\n65535\n";
    for (double value : values) {
        const double normalized = std::clamp(value / scale, 0.0, 1.0);
        const std::uint16_t encoded = static_cast<std::uint16_t>(
            std::llround(normalized * 65535.0));
        const std::array<char, 2> bytes {
            static_cast<char>((encoded >> 8u) & 0xffu),
            static_cast<char>(encoded & 0xffu)
        };
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    output.flush();
    const bool written = static_cast<bool>(output);
    output.close();
    if (!written) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return Fail(error, "MFD diagnostic image write was incomplete.");
    }
    return ReplaceTemporary(temporary, path, error);
}

bool ValidThresholds(const MfdReleaseThresholds& thresholds) {
    return thresholds.minimumSyntheticEntries > 0u &&
        thresholds.minimumControlledEntries > 0u &&
        thresholds.minimumUncontrolledEntries > 0u &&
        thresholds.minimumDistinctCameraModes > 0u &&
        std::isfinite(thresholds.minimumRmseImprovementRatio) &&
        thresholds.minimumRmseImprovementRatio > 1.0 &&
        std::isfinite(thresholds.maximumAbsoluteBias) &&
        thresholds.maximumAbsoluteBias >= 0.0 &&
        std::isfinite(thresholds.minimumGradientRetention) &&
        thresholds.minimumGradientRetention > 0.0 &&
        std::isfinite(thresholds.maximumMovingFalseMergeRate) &&
        thresholds.maximumMovingFalseMergeRate >= 0.0 &&
        thresholds.maximumMovingFalseMergeRate <= 1.0 &&
        std::isfinite(thresholds.maximumCoverage68AbsoluteError) &&
        thresholds.maximumCoverage68AbsoluteError >= 0.0 &&
        thresholds.maximumCoverage68AbsoluteError <= 1.0 &&
        std::isfinite(thresholds.maximumCoverage95AbsoluteError) &&
        thresholds.maximumCoverage95AbsoluteError >= 0.0 &&
        thresholds.maximumCoverage95AbsoluteError <= 1.0 &&
        std::isfinite(thresholds.maximumCoverage99AbsoluteError) &&
        thresholds.maximumCoverage99AbsoluteError >= 0.0 &&
        thresholds.maximumCoverage99AbsoluteError <= 1.0 &&
        std::isfinite(
            thresholds.maximumHighConfidenceCatastrophicErrorRate) &&
        thresholds.maximumHighConfidenceCatastrophicErrorRate >= 0.0 &&
        thresholds.maximumHighConfidenceCatastrophicErrorRate <= 1.0;
}

void AddGate(
    MfdReleaseGateReport& report,
    const std::string& id,
    bool passed,
    const std::string& message) {
    report.gates.push_back({ id, passed, message });
}

bool ValidCorpusLayer(MfdCorpusLayer layer) {
    switch (layer) {
        case MfdCorpusLayer::Synthetic:
        case MfdCorpusLayer::ControlledCamera:
        case MfdCorpusLayer::UncontrolledReal:
            return true;
    }
    return false;
}

} // namespace

nlohmann::json SerializeMfdEvaluationResult(
    const MfdEvaluationResult& result) {
    nlohmann::json cfa = nlohmann::json::array();
    for (const CfaErrorMetrics& site : result.raw.byCfaSite) {
        cfa.push_back({
            { "site", CfaSiteName(site.site) },
            { "sampleCount", site.sampleCount },
            { "meanBias", site.meanBias },
            { "rmse", site.rmse }
        });
    }
    return {
        { "evaluationContractVersion", kEvaluationContractVersion },
        { "evaluationContractId", kEvaluationContractId },
        { "valid", result.valid },
        { "message", result.message },
        { "sampleId", result.sampleId },
        { "extent", {
            { "width", result.extent.width },
            { "height", result.extent.height }
        } },
        { "rawFidelity", {
            { "available", result.raw.available },
            { "sampleCount", result.raw.sampleCount },
            { "meanBias", result.raw.meanBias },
            { "meanAbsoluteError", result.raw.meanAbsoluteError },
            { "rmse", result.raw.rmse },
            { "psnr", result.raw.psnr },
            { "referenceRmse", result.raw.referenceRmse },
            { "rmseImprovementRatio", result.raw.rmseImprovementRatio },
            { "flatResidualStandardDeviation",
                result.raw.flatResidualStandardDeviation },
            { "referenceFlatResidualStandardDeviation",
                result.raw.referenceFlatResidualStandardDeviation },
            { "measuredFlatNoiseReductionRatio",
                result.raw.measuredFlatNoiseReductionRatio },
            { "expectedFlatStandardDeviation",
                result.raw.expectedFlatStandardDeviation },
            { "observedToExpectedNoiseRatio",
                result.raw.observedToExpectedNoiseRatio },
            { "meanEffectiveSampleCount",
                result.raw.meanEffectiveSampleCount },
            { "idealIndependentNoiseReductionRatio",
                result.raw.idealIndependentNoiseReductionRatio },
            { "measuredToIdealNoiseReductionRatio",
                result.raw.measuredToIdealNoiseReductionRatio },
            { "horizontalSameCfaResidualCorrelation",
                result.raw.horizontalSameCfaResidualCorrelation },
            { "verticalSameCfaResidualCorrelation",
                result.raw.verticalSameCfaResidualCorrelation },
            { "byCfaSite", std::move(cfa) }
        } },
        { "detailRetention", {
            { "available", result.detail.available },
            { "evaluatedPixelCount", result.detail.evaluatedPixelCount },
            { "gradientMagnitudeRetention",
                result.detail.gradientMagnitudeRetention },
            { "highFrequencyContrastRetention",
                result.detail.highFrequencyContrastRetention },
            { "maximumAbsoluteError", result.detail.maximumAbsoluteError }
        } },
        { "motionMask", {
            { "available", result.motion.available },
            { "movingPixelCount", result.motion.movingPixelCount },
            { "staticPixelCount", result.motion.staticPixelCount },
            { "staticAcceptancePrecision",
                result.motion.staticAcceptancePrecision },
            { "staticAcceptanceRecall", result.motion.staticAcceptanceRecall },
            { "movingFalseMergeRate", result.motion.movingFalseMergeRate },
            { "movingRegionRmseToReferenceMoment",
                result.motion.movingRegionRmseToReferenceMoment },
            { "temporalStateLeakageFraction",
                result.motion.temporalStateLeakageFraction }
        } },
        { "calibration", {
            { "available", result.calibration.available },
            { "sampleCount", result.calibration.sampleCount },
            { "coverage68", result.calibration.coverage68 },
            { "coverage95", result.calibration.coverage95 },
            { "coverage99", result.calibration.coverage99 },
            { "coverage68AbsoluteError",
                result.calibration.coverage68AbsoluteError },
            { "coverage95AbsoluteError",
                result.calibration.coverage95AbsoluteError },
            { "coverage99AbsoluteError",
                result.calibration.coverage99AbsoluteError },
            { "medianEndpointErrorRawPixels",
                result.calibration.medianEndpointErrorRawPixels },
            { "percentile95EndpointErrorRawPixels",
                result.calibration.percentile95EndpointErrorRawPixels },
            { "highConfidenceCatastrophicErrorRate",
                result.calibration.highConfidenceCatastrophicErrorRate },
            { "confidenceExpectedCalibrationError",
                result.calibration.confidenceExpectedCalibrationError }
        } },
        { "performance", SerializePerformance(result.performance) }
    };
}

bool WriteMfdVisualDiagnostics(
    const MfdEvaluationInput& input,
    const MfdEvaluationResult& result,
    const std::filesystem::path& outputDirectory,
    std::vector<std::filesystem::path>& writtenFiles,
    std::string* error) {
    writtenFiles.clear();
    if (!result.valid || result.sampleId != input.sampleId ||
        outputDirectory.empty()) {
        return Fail(error, "MFD visual diagnostic request is invalid.");
    }
    std::error_code filesystemError;
    std::filesystem::create_directories(outputDirectory, filesystemError);
    if (filesystemError) {
        return Fail(error, "MFD visual diagnostic directory could not be created.");
    }
    const std::string stem = SanitizeStem(input.sampleId);
    const std::size_t count = input.outputMosaic.size();
    std::vector<double> reference(count);
    std::vector<double> output(count);
    std::vector<double> difference(count);
    std::vector<double> effectiveSamples(count, 1.0);
    std::vector<double> acceptedAlternates(count, 0.0);
    std::vector<double> reliability(count, 0.0);
    std::vector<double> motionConfidence(count, 0.0);
    std::vector<double> movingTruth(count, 0.0);
    double maximumDifference = 0.0;
    double maximumEffectiveSamples = 1.0;
    for (std::size_t index = 0u; index < count; ++index) {
        reference[index] = input.noisyReferenceMosaic[index];
        output[index] = input.outputMosaic[index];
        difference[index] = std::abs(
            static_cast<double>(input.outputMosaic[index]) -
            input.noisyReferenceMosaic[index]);
        maximumDifference = std::max(maximumDifference, difference[index]);
        if (input.fusionDiagnostics.size() == count) {
            effectiveSamples[index] =
                input.fusionDiagnostics[index].effectiveSampleCount;
            acceptedAlternates[index] =
                input.fusionDiagnostics[index].contributingAlternateCount > 0u
                    ? 1.0
                    : 0.0;
            maximumEffectiveSamples = std::max(
                maximumEffectiveSamples, effectiveSamples[index]);
        }
        if (input.reliabilityMap.size() == count) {
            reliability[index] = input.reliabilityMap[index];
        }
        if (input.motionConfidenceMap.size() == count) {
            motionConfidence[index] = input.motionConfidenceMap[index];
        }
        if (input.movingRegionMask.size() == count) {
            movingTruth[index] = input.movingRegionMask[index] != 0u
                ? 1.0
                : 0.0;
        }
    }
    const auto writeImage = [&](
        const std::string& suffix,
        const std::vector<double>& values,
        double scale) {
        const std::filesystem::path path =
            outputDirectory / (stem + "-" + suffix + ".pgm");
        if (!WritePgm16Atomic(path, input.extent, values, scale, error)) {
            return false;
        }
        writtenFiles.push_back(path);
        return true;
    };
    if (!writeImage("reference", reference, 1.0) ||
        !writeImage("output", output, 1.0) ||
        !writeImage(
            "difference",
            difference,
            std::max(maximumDifference, 1.0e-12)) ||
        !writeImage("effective-samples", effectiveSamples,
            maximumEffectiveSamples) ||
        !writeImage("alternate-acceptance", acceptedAlternates, 1.0)) {
        return false;
    }
    if ((!input.reliabilityMap.empty() &&
            !writeImage("reliability", reliability, 1.0)) ||
        (!input.motionConfidenceMap.empty() &&
            !writeImage("motion-confidence", motionConfidence, 1.0)) ||
        (!input.movingRegionMask.empty() &&
            !writeImage("moving-truth", movingTruth, 1.0))) {
        return false;
    }
    const std::filesystem::path reportPath =
        outputDirectory / (stem + "-metrics.json");
    if (!WriteTextAtomic(
            reportPath,
            SerializeMfdEvaluationResult(result).dump(2) + "\n",
            error)) {
        return false;
    }
    writtenFiles.push_back(reportPath);
    return true;
}

bool BuildMfdAblationReport(
    const std::vector<MfdAblationVariant>& variants,
    MfdAblationReport& report,
    std::string* error) {
    report = {};
    if (variants.empty()) {
        return Fail(error, "MFD ablation report requires at least one variant.");
    }
    std::set<std::string> names;
    for (const MfdAblationVariant& variant : variants) {
        if (variant.name.empty() || !names.insert(variant.name).second) {
            return Fail(error, "MFD ablation variant names are empty or duplicated.");
        }
        MfdEvaluationInput input = variant.evaluation;
        input.sampleId = input.sampleId + ":" + variant.name;
        MfdEvaluationResult evaluated;
        std::string evaluationError;
        if (!EvaluateMfdOutput(input, evaluated, &evaluationError)) {
            return Fail(
                error,
                "MFD ablation variant failed: " + variant.name + ": " +
                    evaluationError);
        }
        report.stableVariantOrder.push_back(variant.name);
        report.results.push_back(std::move(evaluated));
    }
    report.valid = true;
    report.message = "MFD ablation variants were evaluated in stable order.";
    return true;
}

nlohmann::json SerializeMfdAblationReport(const MfdAblationReport& report) {
    nlohmann::json results = nlohmann::json::array();
    for (std::size_t index = 0u; index < report.results.size(); ++index) {
        results.push_back({
            { "variant", index < report.stableVariantOrder.size()
                ? report.stableVariantOrder[index]
                : "invalid" },
            { "evaluation", SerializeMfdEvaluationResult(
                report.results[index]) }
        });
    }
    return {
        { "evaluationContractVersion", kEvaluationContractVersion },
        { "evaluationContractId", kEvaluationContractId },
        { "valid", report.valid },
        { "message", report.message },
        { "stableVariantOrder", report.stableVariantOrder },
        { "results", std::move(results) }
    };
}

bool FreezeMfdParameterCandidate(
    const Parameters& parameters,
    const std::string& parameterSetId,
    const std::string& corpusManifestSha256,
    MfdParameterFreeze& freeze,
    std::string* error) {
    freeze = {};
    if (!ValidateParameters(parameters, error) || parameterSetId.empty() ||
        !LooksLikeSha256(corpusManifestSha256)) {
        return Fail(error, "MFD parameter-freeze request is invalid.");
    }
    freeze.parameters = SerializeParameters(parameters);
    freeze.parameterSha256 = HashText(freeze.parameters.dump());
    if (!LooksLikeSha256(freeze.parameterSha256)) {
        freeze = {};
        return Fail(error, "MFD parameter-freeze hash generation failed.");
    }
    freeze.valid = true;
    freeze.parameterSetId = parameterSetId;
    freeze.corpusManifestSha256 = corpusManifestSha256;
    freeze.parameterSchemaVersion = parameters.schemaVersion;
    return true;
}

const char* MfdCorpusLayerName(MfdCorpusLayer layer) {
    switch (layer) {
        case MfdCorpusLayer::Synthetic: return "synthetic";
        case MfdCorpusLayer::ControlledCamera: return "controlled-camera";
        case MfdCorpusLayer::UncontrolledReal: return "uncontrolled-real";
    }
    return "invalid";
}

nlohmann::json SerializeMfdCorpusManifest(
    const std::vector<MfdCorpusEntryEvidence>& entries) {
    nlohmann::json serializedEntries = nlohmann::json::array();
    for (const MfdCorpusEntryEvidence& entry : entries) {
        serializedEntries.push_back({
            { "sampleId", entry.sampleId },
            { "sourceContentSetSha256", entry.sourceContentSetSha256 },
            { "layer", MfdCorpusLayerName(entry.layer) },
            { "cameraModeId", entry.cameraModeId },
            { "frameCount", entry.frameCount },
            { "hasReferenceMomentGroundTruth",
                entry.hasReferenceMomentGroundTruth }
        });
    }
    return {
        { "manifestContractId", "ra-cfa-v1-corpus-manifest-v1" },
        { "orderedEntries", std::move(serializedEntries) }
    };
}

bool ComputeMfdCorpusManifestSha256(
    const std::vector<MfdCorpusEntryEvidence>& entries,
    std::string& manifestSha256,
    std::string* error) {
    manifestSha256.clear();
    if (entries.empty()) {
        return Fail(error, "MFD corpus manifest cannot be empty.");
    }
    std::set<std::string> sampleIds;
    for (const MfdCorpusEntryEvidence& entry : entries) {
        if (entry.sampleId.empty() ||
            !sampleIds.insert(entry.sampleId).second ||
            !LooksLikeSha256(entry.sourceContentSetSha256) ||
            !ValidCorpusLayer(entry.layer) || entry.frameCount < 2u) {
            return Fail(error, "MFD corpus manifest entry is invalid.");
        }
    }
    manifestSha256 = HashText(SerializeMfdCorpusManifest(entries).dump());
    return LooksLikeSha256(manifestSha256) ||
        Fail(error, "MFD corpus manifest hashing failed.");
}

bool EvaluateMfdReleaseGates(
    const MfdParameterFreeze& freeze,
    const MfdCorpusEvidence& corpus,
    const std::vector<MfdEvaluationResult>& evaluations,
    const MfdReleaseThresholds& thresholds,
    MfdReleaseGateReport& report,
    std::string* error) {
    report = {};
    if (!ValidThresholds(thresholds)) {
        return Fail(error, "MFD release thresholds are invalid.");
    }
    Parameters frozenParameters;
    std::string frozenParameterError;
    const bool frozenParametersValid = freeze.parameters.is_object() &&
        DeserializeParameters(
            freeze.parameters, frozenParameters, &frozenParameterError);
    bool freezeHashValid = freeze.valid && frozenParametersValid &&
        !freeze.parameterSetId.empty() &&
        LooksLikeSha256(freeze.parameterSha256) &&
        LooksLikeSha256(freeze.corpusManifestSha256) &&
        freeze.parameterSchemaVersion == kParameterSchemaVersion &&
        freeze.parameters.is_object() &&
        HashText(freeze.parameters.dump()) == freeze.parameterSha256;
    AddGate(report, "parameter-freeze", freezeHashValid,
        freezeHashValid
            ? "The parameter candidate is content-addressed."
            : "No valid content-addressed parameter candidate is frozen.");

    std::string computedManifestSha256;
    std::string manifestError;
    const bool manifestIdentityValid = ComputeMfdCorpusManifestSha256(
        corpus.entries, computedManifestSha256, &manifestError);
    const bool manifestValid = manifestIdentityValid &&
        LooksLikeSha256(corpus.manifestSha256) &&
        computedManifestSha256 == corpus.manifestSha256 &&
        corpus.manifestLocked && freezeHashValid &&
        freeze.corpusManifestSha256 == corpus.manifestSha256;
    AddGate(report, "locked-corpus-manifest", manifestValid,
        manifestValid
            ? "The parameter candidate is bound to the locked corpus."
            : "The corpus is unlocked, missing, or does not match the freeze.");

    std::uint64_t syntheticCount = 0u;
    std::uint64_t controlledCount = 0u;
    std::uint64_t uncontrolledCount = 0u;
    std::set<std::string> cameraModes;
    std::set<std::uint32_t> frameCounts;
    std::set<std::string> sampleIds;
    bool entriesValid = !corpus.entries.empty();
    bool evidenceComplete = !corpus.entries.empty();
    bool visualComplete = !corpus.entries.empty();
    bool groundTruthComplete = !corpus.entries.empty();
    for (const MfdCorpusEntryEvidence& entry : corpus.entries) {
        entriesValid = entriesValid && !entry.sampleId.empty() &&
            sampleIds.insert(entry.sampleId).second &&
            LooksLikeSha256(entry.sourceContentSetSha256) &&
            ValidCorpusLayer(entry.layer) && entry.frameCount >= 2u;
        evidenceComplete = evidenceComplete && entry.metricsComplete;
        visualComplete = visualComplete && entry.visualReviewComplete;
        if (entry.layer != MfdCorpusLayer::UncontrolledReal) {
            groundTruthComplete = groundTruthComplete &&
                entry.hasReferenceMomentGroundTruth;
        }
        frameCounts.insert(entry.frameCount);
        switch (entry.layer) {
            case MfdCorpusLayer::Synthetic:
                ++syntheticCount;
                break;
            case MfdCorpusLayer::ControlledCamera:
                ++controlledCount;
                if (!entry.cameraModeId.empty()) {
                    cameraModes.insert(entry.cameraModeId);
                }
                break;
            case MfdCorpusLayer::UncontrolledReal:
                ++uncontrolledCount;
                if (!entry.cameraModeId.empty()) {
                    cameraModes.insert(entry.cameraModeId);
                }
                break;
        }
    }
    AddGate(report, "corpus-entry-contract", entriesValid,
        entriesValid
            ? "Corpus entries have stable unique identities."
            : "Corpus entries are missing, duplicated, or invalid.");
    const bool layerCoverage =
        syntheticCount >= thresholds.minimumSyntheticEntries &&
        controlledCount >= thresholds.minimumControlledEntries &&
        uncontrolledCount >= thresholds.minimumUncontrolledEntries &&
        cameraModes.size() >= thresholds.minimumDistinctCameraModes;
    AddGate(report, "corpus-layer-coverage", layerCoverage,
        layerCoverage
            ? "Synthetic, controlled, and uncontrolled corpus coverage is present."
            : "The camera-diverse corpus coverage minimums are not met.");
    constexpr std::array<std::uint32_t, 5> kRequiredFrameCounts {
        2u, 4u, 8u, 16u, 32u
    };
    const bool burstCoverage = std::all_of(
        kRequiredFrameCounts.begin(),
        kRequiredFrameCounts.end(),
        [&](std::uint32_t count) {
            return frameCounts.find(count) != frameCounts.end();
        });
    AddGate(report, "burst-length-coverage", burstCoverage,
        burstCoverage
            ? "The corpus covers 2/4/8/16/32-frame bursts."
            : "The corpus does not cover every required burst length.");
    AddGate(report, "reference-moment-ground-truth", groundTruthComplete,
        groundTruthComplete
            ? "Synthetic and controlled samples have reference-moment truth."
            : "Synthetic or controlled samples lack reference-moment truth.");
    AddGate(report, "metrics-and-visual-review",
        evidenceComplete && visualComplete,
        evidenceComplete && visualComplete
            ? "Metrics and visual review are complete for every corpus entry."
            : "Metrics or visual review remain incomplete.");

    bool evaluationsValid = !evaluations.empty();
    std::set<std::string> evaluationIds;
    bool rawAvailable = false;
    bool detailAvailable = false;
    bool motionAvailable = false;
    bool calibrationAvailable = false;
    bool performanceAvailable = false;
    double minimumImprovement = std::numeric_limits<double>::infinity();
    double maximumBias = 0.0;
    double minimumGradient = std::numeric_limits<double>::infinity();
    double maximumFalseMerge = 0.0;
    double maximumCoverage68Error = 0.0;
    double maximumCoverage95Error = 0.0;
    double maximumCoverage99Error = 0.0;
    double maximumCatastrophicRate = 0.0;
    for (const MfdEvaluationResult& evaluation : evaluations) {
        evaluationsValid = evaluationsValid && evaluation.valid &&
            !evaluation.sampleId.empty() &&
            evaluationIds.insert(evaluation.sampleId).second;
        if (evaluation.raw.available) {
            rawAvailable = true;
            minimumImprovement = std::min(
                minimumImprovement, evaluation.raw.rmseImprovementRatio);
            maximumBias = std::max(
                maximumBias, std::abs(evaluation.raw.meanBias));
        }
        if (evaluation.detail.available) {
            detailAvailable = true;
            minimumGradient = std::min(
                minimumGradient,
                evaluation.detail.gradientMagnitudeRetention);
        }
        if (evaluation.motion.available) {
            motionAvailable = true;
            maximumFalseMerge = std::max(
                maximumFalseMerge,
                evaluation.motion.movingFalseMergeRate);
        }
        if (evaluation.calibration.available) {
            calibrationAvailable = true;
            maximumCoverage68Error = std::max(
                maximumCoverage68Error,
                evaluation.calibration.coverage68AbsoluteError);
            maximumCoverage95Error = std::max(
                maximumCoverage95Error,
                evaluation.calibration.coverage95AbsoluteError);
            maximumCoverage99Error = std::max(
                maximumCoverage99Error,
                evaluation.calibration.coverage99AbsoluteError);
            maximumCatastrophicRate = std::max(
                maximumCatastrophicRate,
                evaluation.calibration.highConfidenceCatastrophicErrorRate);
        }
        performanceAvailable = performanceAvailable ||
            evaluation.performance.available;
    }
    AddGate(report, "evaluation-contracts", evaluationsValid,
        evaluationsValid
            ? "Every supplied evaluation record is valid."
            : "Evaluation records are missing or invalid.");
    bool evaluationCoverage = evaluationsValid && !corpus.entries.empty() &&
        syntheticCount + controlledCount > 0u;
    for (const MfdCorpusEntryEvidence& entry : corpus.entries) {
        if (entry.layer == MfdCorpusLayer::UncontrolledReal) continue;
        evaluationCoverage = evaluationCoverage &&
            evaluationIds.find(entry.sampleId) != evaluationIds.end();
    }
    AddGate(report, "ground-truth-evaluation-coverage", evaluationCoverage,
        evaluationCoverage
            ? "Every synthetic and controlled corpus entry was evaluated."
            : "Synthetic or controlled corpus entries lack evaluation results.");
    const bool rawQuality = rawAvailable &&
        minimumImprovement >= thresholds.minimumRmseImprovementRatio &&
        maximumBias <= thresholds.maximumAbsoluteBias;
    AddGate(report, "raw-noise-fidelity", rawQuality,
        rawQuality
            ? "RAW noise reduction and bias guardrails pass."
            : "RAW noise reduction or bias guardrails fail.");
    const bool detailQuality = detailAvailable &&
        minimumGradient >= thresholds.minimumGradientRetention;
    AddGate(report, "detail-retention", detailQuality,
        detailQuality
            ? "Same-CFA detail retention passes."
            : "Detail retention evidence is missing or below threshold.");
    const bool motionQuality = motionAvailable &&
        maximumFalseMerge <= thresholds.maximumMovingFalseMergeRate;
    AddGate(report, "motion-fidelity", motionQuality,
        motionQuality
            ? "Moving-region false merge is within the guardrail."
            : "Motion evidence is missing or the false-merge guardrail fails.");
    const bool calibrationQuality = calibrationAvailable &&
        maximumCoverage68Error <= thresholds.maximumCoverage68AbsoluteError &&
        maximumCoverage95Error <= thresholds.maximumCoverage95AbsoluteError &&
        maximumCoverage99Error <= thresholds.maximumCoverage99AbsoluteError &&
        maximumCatastrophicRate <=
            thresholds.maximumHighConfidenceCatastrophicErrorRate;
    AddGate(report, "confidence-covariance-calibration", calibrationQuality,
        calibrationQuality
            ? "Confidence and covariance calibration guardrails pass."
            : "Calibration evidence is missing or outside guardrails.");
    AddGate(report, "performance-memory-traces",
        corpus.performanceTracesComplete && performanceAvailable,
        corpus.performanceTracesComplete && performanceAvailable
            ? "Performance and memory traces are complete."
            : "Performance or memory traces are incomplete.");
    AddGate(report, "frozen-corpus-rerun",
        corpus.fullFrozenCorpusRerunComplete &&
            corpus.noHandSelectedExceptions,
        corpus.fullFrozenCorpusRerunComplete &&
            corpus.noHandSelectedExceptions
            ? "The frozen corpus rerun passed without hand-selected exceptions."
            : "A full exception-free frozen-corpus rerun has not passed.");

    report.valid = true;
    report.productionReady = std::all_of(
        report.gates.begin(), report.gates.end(),
        [](const MfdReleaseGateResult& gate) { return gate.passed; });
    report.message = report.productionReady
        ? "RA-CFA V1 satisfies the recorded production release gates."
        : "RA-CFA V1 remains blocked by one or more production release gates.";
    return true;
}

nlohmann::json SerializeMfdReleaseGateReport(
    const MfdReleaseGateReport& report) {
    nlohmann::json gates = nlohmann::json::array();
    for (const MfdReleaseGateResult& gate : report.gates) {
        gates.push_back({
            { "id", gate.id },
            { "passed", gate.passed },
            { "message", gate.message }
        });
    }
    return {
        { "releaseGateContractId", kReleaseGateContractId },
        { "evaluationContractVersion", kEvaluationContractVersion },
        { "valid", report.valid },
        { "productionReady", report.productionReady },
        { "message", report.message },
        { "gates", std::move(gates) }
    };
}

} // namespace Raw::Mfd
