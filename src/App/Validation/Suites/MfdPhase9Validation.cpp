#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Evaluation.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

struct Options {
    std::filesystem::path outputDirectory;
};

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 9 validation failed: " << message << std::endl;
    }
    return condition;
}

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int index = 0; index < argc; ++index) {
        const std::string argument = argv[index] ? argv[index] : "";
        if (argument == "--output") {
            if (index + 1 >= argc) return false;
            options.outputDirectory = argv[++index];
        } else {
            return false;
        }
    }
    return true;
}

std::filesystem::path MakeTemporaryDirectory() {
    static std::atomic<std::uint64_t> counter { 0u };
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("stack-mfd-phase9-" + std::to_string(stamp) + "-" +
            std::to_string(counter.fetch_add(1u)));
}

bool WriteJsonAtomic(
    const std::filesystem::path& path,
    const nlohmann::json& value) {
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) return false;
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << value.dump(2) << '\n';
    output.flush();
    const bool written = static_cast<bool>(output);
    output.close();
    if (!written) {
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    if (std::filesystem::exists(path, filesystemError) && !filesystemError) {
        std::filesystem::remove(path, filesystemError);
    }
    if (filesystemError) {
        std::filesystem::remove(temporary, filesystemError);
        return false;
    }
    std::filesystem::rename(temporary, path, filesystemError);
    return !filesystemError;
}

bool IsMoving(std::uint64_t x, std::uint64_t y) {
    return x >= 22u && x < 38u && y >= 14u && y < 34u;
}

double SceneValue(std::uint64_t x, std::uint64_t y) {
    const double siteOffset = (x & 1u) == 0u && (y & 1u) == 0u
        ? 0.030
        : ((x & 1u) != 0u && (y & 1u) == 0u
            ? 0.000
            : ((x & 1u) == 0u ? 0.006 : -0.020));
    const double checker = ((x / 4u + y / 4u) & 1u) != 0u
        ? 0.025
        : -0.025;
    return std::clamp(
        0.35 + siteOffset + checker +
            0.09 * std::sin(0.21 * static_cast<double>(x)) +
            0.07 * std::cos(0.17 * static_cast<double>(y)),
        0.02,
        0.90);
}

Raw::Mfd::MfdEvaluationInput MakeSyntheticEvaluation() {
    Raw::Mfd::MfdEvaluationInput input;
    input.sampleId = "synthetic-reference-anchored-motion";
    input.cfaPattern = Raw::CfaPattern::RGGB;
    input.extent = { 64u, 48u };
    const std::size_t count = static_cast<std::size_t>(
        input.extent.width * input.extent.height);
    input.noiseFreeReferenceMosaic.resize(count);
    input.noisyReferenceMosaic.resize(count);
    input.outputMosaic.resize(count);
    input.alternateMomentMosaic.resize(count);
    input.flatRegionMask.resize(count, 0u);
    input.detailRegionMask.resize(count, 0u);
    input.movingRegionMask.resize(count, 0u);
    input.reliabilityMap.resize(count, 1.0f);
    input.motionConfidenceMap.resize(count, 0.95f);
    input.fusionDiagnostics.resize(count);
    constexpr double noiseAmplitude = 0.012;
    constexpr double burstReduction = 2.8284271247461903;
    for (std::size_t index = 0u; index < count; ++index) {
        const std::uint64_t x =
            static_cast<std::uint64_t>(index) % input.extent.width;
        const std::uint64_t y =
            static_cast<std::uint64_t>(index) / input.extent.width;
        const double truth = SceneValue(x, y);
        const double noise = noiseAmplitude * std::sin(
            0.754877666 * static_cast<double>(index) + 0.31);
        const bool moving = IsMoving(x, y);
        input.noiseFreeReferenceMosaic[index] = static_cast<float>(truth);
        input.noisyReferenceMosaic[index] = static_cast<float>(truth + noise);
        input.outputMosaic[index] = static_cast<float>(
            truth + (moving ? noise : noise / burstReduction));
        input.alternateMomentMosaic[index] = static_cast<float>(
            std::clamp(truth + (moving ? 0.20 : 0.0), 0.0, 1.0));
        input.flatRegionMask[index] = y < 10u ? 1u : 0u;
        input.detailRegionMask[index] =
            !moving && x >= 2u && x + 2u < input.extent.width &&
                    y >= 2u && y + 2u < input.extent.height
                ? 1u
                : 0u;
        input.movingRegionMask[index] = moving ? 1u : 0u;
        Raw::Mfd::FusionPixelDiagnostics& diagnostic =
            input.fusionDiagnostics[index];
        diagnostic.referenceIncluded = true;
        diagnostic.effectiveSampleCount = moving ? 1.0 : 8.0;
        diagnostic.outputVarianceComparisonDomain = moving
            ? noiseAmplitude * noiseAmplitude * 0.5
            : noiseAmplitude * noiseAmplitude * 0.5 / 8.0;
        if (moving) {
            diagnostic.decisionReason =
                Raw::Mfd::DecisionReason::AllAlternatesRejected;
            diagnostic.exactReferenceCopy = true;
            diagnostic.rejectedAlternateCount = 7u;
            input.reliabilityMap[index] = 0.0f;
            input.motionConfidenceMap[index] = 0.10f;
        } else {
            diagnostic.eligibleAlternateCount = 7u;
            diagnostic.contributingAlternateCount = 7u;
        }
    }
    input.expectedOutputNoiseStandardDeviation =
        noiseAmplitude / std::sqrt(2.0) / burstReduction;
    input.motionCalibrationSamples.reserve(100u);
    for (std::uint32_t index = 0u; index < 100u; ++index) {
        const double mahalanobis = index < 68u
            ? 1.0
            : (index < 95u ? 3.0 : (index < 99u ? 7.0 : 10.0));
        Raw::Mfd::MotionCalibrationSample sample;
        sample.errorRawX = 0.1 * std::sqrt(mahalanobis);
        sample.predictedCovariance = { 0.01, 0.0, 0.01 };
        sample.confidence = 0.95;
        input.motionCalibrationSamples.push_back(sample);
    }
    input.performance.available = true;
    input.performance.stages = {
        { "decode-normalize", 1.0 },
        { "registration", 2.0 },
        { "reliability", 1.0 },
        { "fusion", 1.0 }
    };
    input.performance.peakResidentBytes = 32u * 1024u * 1024u;
    input.performance.peakWorkingBytes = 4u * 1024u * 1024u;
    input.performance.cacheBytesRead = 1024u;
    input.performance.cacheBytesWritten = 2048u;
    input.performance.cancellationLatencyMs = 2.0;
    input.performance.coldCacheWallTimeMs = 5.0;
    input.performance.warmCacheWallTimeMs = 1.0;
    return input;
}

Raw::Mfd::MfdAblationReport BuildSyntheticAblations(
    const Raw::Mfd::MfdEvaluationInput& v1,
    bool& ok) {
    Raw::Mfd::MfdEvaluationInput reference = v1;
    reference.outputMosaic = reference.noisyReferenceMosaic;
    for (auto& diagnostic : reference.fusionDiagnostics) {
        diagnostic = {};
        diagnostic.decisionReason =
            Raw::Mfd::DecisionReason::AllAlternatesRejected;
        diagnostic.exactReferenceCopy = true;
        diagnostic.referenceIncluded = true;
        diagnostic.effectiveSampleCount = 1.0;
    }

    Raw::Mfd::MfdEvaluationInput naive = v1;
    for (std::size_t index = 0u; index < naive.outputMosaic.size(); ++index) {
        if (naive.movingRegionMask[index] != 0u) {
            naive.outputMosaic[index] = static_cast<float>(
                (naive.noisyReferenceMosaic[index] +
                    7.0 * naive.alternateMomentMosaic[index]) /
                8.0);
        }
        auto& diagnostic = naive.fusionDiagnostics[index];
        diagnostic = {};
        diagnostic.referenceIncluded = true;
        diagnostic.eligibleAlternateCount = 7u;
        diagnostic.contributingAlternateCount = 7u;
        diagnostic.effectiveSampleCount = 8.0;
    }

    const std::vector<Raw::Mfd::MfdAblationVariant> variants {
        { "reference-frame", reference },
        { "naive-same-cfa-mean", naive },
        { "ra-cfa-v1", v1 },
        { "without-robust-rejection", naive }
    };
    Raw::Mfd::MfdAblationReport report;
    std::string error;
    ok &= Check(Raw::Mfd::BuildMfdAblationReport(
            variants, report, &error),
        "synthetic ablation report failed: " + error);
    return report;
}

bool ValidateMetrics(
    const Raw::Mfd::MfdEvaluationInput& input,
    Raw::Mfd::MfdEvaluationResult& result) {
    bool ok = true;
    std::string error;
    ok &= Check(Raw::Mfd::EvaluateMfdOutput(input, result, &error),
        "synthetic evaluation failed: " + error);
    ok &= Check(result.valid && result.raw.available &&
            result.raw.rmseImprovementRatio > 1.5 &&
            std::abs(result.raw.meanBias) < 1.0e-3 &&
            std::abs(result.raw.meanEffectiveSampleCount - 8.0) < 1.0e-12 &&
            std::abs(result.raw.measuredToIdealNoiseReductionRatio - 1.0) <
                0.01 &&
            result.raw.byCfaSite.size() == 4u,
        "RAW fidelity/noise metrics did not recognize the known improvement");
    ok &= Check(result.detail.available &&
            result.detail.gradientMagnitudeRetention >= 0.95,
        "same-CFA detail retention rejected the reference-preserving output");
    ok &= Check(result.motion.available &&
            result.motion.staticAcceptancePrecision == 1.0 &&
            result.motion.staticAcceptanceRecall == 1.0 &&
            result.motion.movingFalseMergeRate == 0.0 &&
            result.motion.temporalStateLeakageFraction == 0.0,
        "motion-mask metrics did not identify exact reference anchoring");
    ok &= Check(result.calibration.available &&
            std::abs(result.calibration.coverage68 - 0.68) < 1.0e-12 &&
            std::abs(result.calibration.coverage95 - 0.95) < 1.0e-12 &&
            std::abs(result.calibration.coverage99 - 0.99) < 1.0e-12 &&
            result.calibration.highConfidenceCatastrophicErrorRate == 0.0,
        "known chi-square covariance coverage was measured incorrectly");
    ok &= Check(result.performance.available &&
            result.performance.totalWallTimeMs == 5.0,
        "stage performance traces were not aggregated deterministically");
    const nlohmann::json serialized =
        Raw::Mfd::SerializeMfdEvaluationResult(result);
    ok &= Check(serialized.value("evaluationContractId", "") ==
            Raw::Mfd::kEvaluationContractId &&
            serialized["rawFidelity"].value("available", false),
        "evaluation serialization lost its versioned identity or metrics");

    auto invalid = input;
    invalid.outputMosaic.pop_back();
    Raw::Mfd::MfdEvaluationResult rejected;
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateMfdOutput(invalid, rejected, &error),
        "a truncated output mosaic crossed the evaluation boundary");
    return ok;
}

bool ValidateAblations(
    const Raw::Mfd::MfdAblationReport& report) {
    bool ok = true;
    ok &= Check(report.valid && report.results.size() == 4u &&
            report.stableVariantOrder == std::vector<std::string> {
                "reference-frame",
                "naive-same-cfa-mean",
                "ra-cfa-v1",
                "without-robust-rejection" },
        "ablation variants did not preserve their stable declared order");
    if (report.results.size() == 4u) {
        ok &= Check(report.results[2].raw.rmse < report.results[0].raw.rmse &&
                report.results[2].motion.movingFalseMergeRate <
                    report.results[1].motion.movingFalseMergeRate,
            "RA-CFA did not beat the reference/no-rejection synthetic baselines");
    }
    return ok;
}

std::vector<Raw::Mfd::MfdEvaluationResult> MakeCompleteEvaluationEvidence(
    const Raw::Mfd::MfdEvaluationResult& prototype) {
    std::vector<Raw::Mfd::MfdEvaluationResult> evaluations;
    for (std::uint32_t index = 0u; index < 12u; ++index) {
        auto copy = prototype;
        copy.sampleId = "synthetic-" + std::to_string(index);
        evaluations.push_back(std::move(copy));
    }
    for (std::uint32_t index = 0u; index < 5u; ++index) {
        auto copy = prototype;
        copy.sampleId = "controlled-" + std::to_string(index);
        evaluations.push_back(std::move(copy));
    }
    return evaluations;
}

Raw::Mfd::MfdCorpusEvidence MakeCompleteCorpus() {
    Raw::Mfd::MfdCorpusEvidence corpus;
    corpus.manifestLocked = true;
    corpus.fullFrozenCorpusRerunComplete = true;
    corpus.performanceTracesComplete = true;
    corpus.noHandSelectedExceptions = true;
    constexpr std::array<std::uint32_t, 5> lengths {
        2u, 4u, 8u, 16u, 32u
    };
    for (std::uint32_t index = 0u; index < 12u; ++index) {
        corpus.entries.push_back({
            "synthetic-" + std::to_string(index),
            std::string(64u, static_cast<char>('a' + index % 6u)),
            Raw::Mfd::MfdCorpusLayer::Synthetic,
            "synthetic",
            lengths[index % lengths.size()],
            true,
            true,
            true
        });
    }
    for (std::uint32_t index = 0u; index < 5u; ++index) {
        corpus.entries.push_back({
            "controlled-" + std::to_string(index),
            std::string(64u, static_cast<char>('b' + index % 5u)),
            Raw::Mfd::MfdCorpusLayer::ControlledCamera,
            index < 3u ? "camera-a-mode-1" : "camera-b-mode-2",
            lengths[index % lengths.size()],
            true,
            true,
            true
        });
    }
    for (std::uint32_t index = 0u; index < 10u; ++index) {
        corpus.entries.push_back({
            "real-" + std::to_string(index),
            std::string(64u, static_cast<char>('c' + index % 4u)),
            Raw::Mfd::MfdCorpusLayer::UncontrolledReal,
            index < 5u ? "camera-a-mode-1" : "camera-b-mode-2",
            lengths[index % lengths.size()],
            false,
            true,
            true
        });
    }
    std::string error;
    Raw::Mfd::ComputeMfdCorpusManifestSha256(
        corpus.entries, corpus.manifestSha256, &error);
    return corpus;
}

bool ValidateReleaseGates(
    const Raw::Mfd::MfdEvaluationResult& evaluation,
    Raw::Mfd::MfdParameterFreeze& freeze,
    Raw::Mfd::MfdReleaseGateReport& currentBlocked) {
    bool ok = true;
    const auto completeCorpus = MakeCompleteCorpus();
    const std::string manifestHash = completeCorpus.manifestSha256;
    std::string error;
    ok &= Check(manifestHash.size() == 64u,
        "the complete corpus did not produce a manifest identity");
    auto reorderedEntries = completeCorpus.entries;
    std::reverse(reorderedEntries.begin(), reorderedEntries.end());
    std::string reorderedHash;
    ok &= Check(Raw::Mfd::ComputeMfdCorpusManifestSha256(
            reorderedEntries, reorderedHash, &error) &&
            reorderedHash != manifestHash,
        "corpus entry ordering did not invalidate the frozen manifest");
    auto completionChangedEntries = completeCorpus.entries;
    completionChangedEntries.front().metricsComplete = false;
    completionChangedEntries.front().visualReviewComplete = false;
    std::string completionChangedHash;
    ok &= Check(Raw::Mfd::ComputeMfdCorpusManifestSha256(
            completionChangedEntries, completionChangedHash, &error) &&
            completionChangedHash == manifestHash,
        "post-run completion state incorrectly changed source-corpus identity");
    ok &= Check(Raw::Mfd::FreezeMfdParameterCandidate(
            {},
            "ra-cfa-v1-conservative-candidate",
            manifestHash,
            freeze,
            &error),
        "parameter candidate freeze failed: " + error);
    Raw::Mfd::MfdCorpusEvidence unavailableCorpus;
    unavailableCorpus.manifestSha256 = manifestHash;
    ok &= Check(Raw::Mfd::EvaluateMfdReleaseGates(
            freeze,
            unavailableCorpus,
            { evaluation },
            {},
            currentBlocked,
            &error) &&
            currentBlocked.valid && !currentBlocked.productionReady,
        "the release gate did not remain closed without a real corpus");

    const auto completeEvaluations =
        MakeCompleteEvaluationEvidence(evaluation);
    Raw::Mfd::MfdReleaseGateReport complete;
    ok &= Check(Raw::Mfd::EvaluateMfdReleaseGates(
            freeze,
            completeCorpus,
            completeEvaluations,
            {},
            complete,
            &error) && complete.productionReady &&
            std::all_of(
                complete.gates.begin(),
                complete.gates.end(),
                [](const Raw::Mfd::MfdReleaseGateResult& gate) {
                    return gate.passed;
                }),
        "a complete conforming evidence packet did not pass gate logic: " +
            error);

    auto tampered = freeze;
    tampered.parameters["fusion"]["outputTileRawPixels"] = 256u;
    Raw::Mfd::MfdReleaseGateReport rejected;
    ok &= Check(Raw::Mfd::EvaluateMfdReleaseGates(
            tampered,
            completeCorpus,
            completeEvaluations,
            {},
            rejected,
            &error) && !rejected.productionReady,
        "a parameter payload changed after freezing remained releasable");
    return ok;
}

bool ValidateArtifacts(
    const Raw::Mfd::MfdEvaluationInput& input,
    const Raw::Mfd::MfdEvaluationResult& result,
    const std::filesystem::path& directory) {
    bool ok = true;
    std::vector<std::filesystem::path> files;
    std::string error;
    ok &= Check(Raw::Mfd::WriteMfdVisualDiagnostics(
            input, result, directory, files, &error),
        "visual diagnostic generation failed: " + error);
    ok &= Check(files.size() == 9u,
        "the visual packet is missing reference/output/difference or maps");
    for (const std::filesystem::path& file : files) {
        ok &= Check(std::filesystem::exists(file),
            "a reported visual diagnostic file does not exist");
    }
    const auto pgm = std::find_if(
        files.begin(), files.end(), [](const std::filesystem::path& path) {
            return path.extension() == ".pgm";
        });
    if (pgm != files.end()) {
        std::ifstream inputFile(*pgm, std::ios::binary);
        std::array<char, 2> magic {};
        inputFile.read(magic.data(), 2);
        ok &= Check(magic[0] == 'P' && magic[1] == '5',
            "visual diagnostic is not a portable 16-bit grayscale image");
    }
    return ok;
}

} // namespace

bool ValidateMfdPhase9Evaluation(int argc, char** argv) {
    Options options;
    if (!ParseOptions(argc, argv, options)) {
        std::cerr << "Usage: Stack.exe --validate-mfd-phase9 [--output <directory>]\n";
        return false;
    }
    bool ok = true;
    const Raw::Mfd::MfdEvaluationInput synthetic = MakeSyntheticEvaluation();
    Raw::Mfd::MfdEvaluationResult evaluation;
    ok &= ValidateMetrics(synthetic, evaluation);
    Raw::Mfd::MfdAblationReport ablations =
        BuildSyntheticAblations(synthetic, ok);
    ok &= ValidateAblations(ablations);
    Raw::Mfd::MfdParameterFreeze freeze;
    Raw::Mfd::MfdReleaseGateReport currentBlocked;
    ok &= ValidateReleaseGates(evaluation, freeze, currentBlocked);

    const bool persistent = !options.outputDirectory.empty();
    const std::filesystem::path artifactDirectory = persistent
        ? options.outputDirectory
        : MakeTemporaryDirectory();
    ok &= ValidateArtifacts(synthetic, evaluation, artifactDirectory);
    if (persistent) {
        ok &= Check(WriteJsonAtomic(
                artifactDirectory / "phase9-synthetic-ablation.json",
                Raw::Mfd::SerializeMfdAblationReport(ablations)),
            "the persistent ablation report could not be written");
        ok &= Check(WriteJsonAtomic(
                artifactDirectory / "phase9-current-release-gates.json",
                Raw::Mfd::SerializeMfdReleaseGateReport(currentBlocked)),
            "the persistent release-gate report could not be written");
        ok &= Check(WriteJsonAtomic(
                artifactDirectory / "phase9-parameter-candidate.json",
                {
                    { "valid", freeze.valid },
                    { "parameterSetId", freeze.parameterSetId },
                    { "parameterSha256", freeze.parameterSha256 },
                    { "corpusManifestSha256", freeze.corpusManifestSha256 },
                    { "parameters", freeze.parameters }
                }),
            "the parameter candidate report could not be written");
    } else {
        std::error_code cleanupError;
        std::filesystem::remove_all(artifactDirectory, cleanupError);
    }
    if (ok) {
        std::cout
            << "MFD Phase 9 evaluation harness validation passed; "
            << "production release remains blocked until a locked real corpus passes."
            << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
