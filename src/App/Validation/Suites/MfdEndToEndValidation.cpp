#include "App/Validation/ValidationSuites.h"

#include "Editor/EditorRenderWorker.h"
#include "Persistence/RawProjectModel.h"
#include "Raw/MultiFrame/GraphProcessor.h"
#include "Raw/MultiFrameDenoise/Evaluation.h"
#include "Raw/MultiFrameDenoise/Inspection.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/MultiFrameDenoise/Processor.h"
#include "Renderer/GLLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

constexpr int kWidth = 256;
constexpr int kHeight = 256;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD end-to-end validation failed: "
                  << message << std::endl;
    }
    return condition;
}

std::filesystem::path MakeTemporaryDirectory() {
    static std::atomic<std::uint64_t> counter { 0u };
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("stack-mfd-end-to-end-" + std::to_string(stamp) + "-" +
            std::to_string(counter.fetch_add(1u)));
}

double Scene(double x, double y, int site) {
    std::uint32_t hash = 0x9e3779b9u;
    hash ^= static_cast<std::uint32_t>(static_cast<std::int32_t>(
        std::llround(x))) * 0x85ebca6bu;
    hash ^= static_cast<std::uint32_t>(static_cast<std::int32_t>(
        std::llround(y))) * 0xc2b2ae35u;
    hash ^= static_cast<std::uint32_t>(site + 1) * 0x27d4eb2fu;
    hash ^= hash >> 16u;
    hash *= 0x7feb352du;
    hash ^= hash >> 15u;
    hash *= 0x846ca68bu;
    hash ^= hash >> 16u;
    const double broadband =
        static_cast<double>(hash & 0xffffu) / 65535.0 - 0.5;
    return std::clamp(
        0.42 + 0.006 * static_cast<double>(site) +
        0.095 * std::sin(0.071 * x + 0.043 * y) +
        0.080 * std::cos(0.037 * x - 0.083 * y) +
        0.045 * std::sin(0.129 * x + 0.113 * y) +
        0.025 * std::sin(0.317 * x + 0.271 * y) +
        0.018 * std::cos(0.239 * x - 0.349 * y) +
        0.18 * broadband,
        0.08,
        0.82);
}

int SiteAt(int x, int y) {
    if ((y & 1) == 0) return (x & 1) == 0 ? 0 : 1;
    return (x & 1) == 0 ? 2 : 3;
}

Raw::RawImageData MakeSyntheticRaw(
    char identity,
    double translationX,
    double translationY,
    std::uint32_t seed,
    int orientation = 1,
    double sceneScale = 1.0,
    double noiseStandardDeviation = 0.0090) {
    Raw::RawImageData raw;
    raw.metadata.sourceContentSha256 = std::string(64u, identity);
    raw.metadata.sourceByteSize =
        static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(std::uint16_t);
    raw.metadata.sourcePath = std::string(1u, identity) + ".dng";
    raw.metadata.cameraMake = "Stack Validation Camera";
    raw.metadata.cameraModel = "Synthetic Bayer 1";
    raw.metadata.dngUniqueCameraModel = "Stack Synthetic Bayer 1";
    raw.metadata.rawWidth = kWidth;
    raw.metadata.rawHeight = kHeight;
    raw.metadata.visibleWidth = kWidth;
    raw.metadata.visibleHeight = kHeight;
    raw.metadata.orientation = orientation;
    raw.metadata.bitDepth = 12;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.mosaiced = true;
    raw.metadata.isDng = true;
    raw.metadata.blackLevel = 64.0f;
    raw.metadata.perChannelBlack.fill(64.0f);
    raw.metadata.whiteLevel = 4160.0f;
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = {
        { 0.00040, 0.000080 },
        { 0.00035, 0.000070 },
        { 0.00045, 0.000090 }
    };
    raw.rawBuffer.resize(static_cast<std::size_t>(kWidth) * kHeight);
    std::mt19937 generator(seed);
    std::normal_distribution<double> noise(
        0.0, noiseStandardDeviation);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const double normalized = std::clamp(
                sceneScale * Scene(
                    static_cast<double>(x) - translationX,
                    static_cast<double>(y) - translationY,
                    SiteAt(x, y)) + noise(generator),
                0.0,
                0.90);
            raw.rawBuffer[static_cast<std::size_t>(y) * kWidth + x] =
                static_cast<std::uint16_t>(std::lround(
                    64.0 + 4096.0 * normalized));
        }
    }
    return raw;
}

Raw::RawImageData MakeLowSignalRawWithoutNoiseProfile(
    char identity,
    std::uint16_t codeValue) {
    Raw::RawImageData raw =
        MakeSyntheticRaw(identity, 0.0, 0.0, 1u);
    raw.metadata.hasDngNoiseProfile = false;
    raw.metadata.dngNoiseProfile.clear();
    std::fill(raw.rawBuffer.begin(), raw.rawBuffer.end(), codeValue);
    return raw;
}

Raw::Mfd::MfdProcessingFrameInput Input(
    const char* path,
    char identity) {
    Raw::Mfd::MfdProcessingFrameInput input;
    input.stableFrameId = std::string("synthetic-") + identity;
    input.sourcePath = std::filesystem::path(path);
    input.expectedSourceSha256 = std::string(64u, identity);
    input.expectedSourceByteLength =
        static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(std::uint16_t);
    input.expectedVisibleExtent = { kWidth, kHeight };
    return input;
}

std::string SyntheticSha256(std::size_t ordinal) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string value(64u, '0');
    value[62] = kHex[(ordinal >> 4u) & 0x0fu];
    value[63] = kHex[ordinal & 0x0fu];
    return value;
}

Raw::Mfd::MfdProcessingFrameInput Input(
    const std::string& path,
    std::size_t ordinal,
    const std::string& sha256) {
    Raw::Mfd::MfdProcessingFrameInput input;
    input.stableFrameId = "synthetic-shared-" + std::to_string(ordinal);
    input.sourcePath = std::filesystem::path(path);
    input.expectedSourceSha256 = sha256;
    input.expectedSourceByteLength =
        static_cast<std::uint64_t>(kWidth) * kHeight * sizeof(std::uint16_t);
    input.expectedVisibleExtent = { kWidth, kHeight };
    return input;
}

Raw::Mfd::MfdProcessingServices Services(
    std::unordered_map<std::string, Raw::RawImageData> frames) {
    Raw::Mfd::MfdProcessingServices services;
    services.loadRawFrame = [frames = std::move(frames)](
        const std::filesystem::path& path,
        Raw::RawImageData& output,
        const std::function<bool()>& shouldCancel,
        std::string& error) {
        if (shouldCancel && shouldCancel()) {
            error = "Synthetic load canceled.";
            return false;
        }
        const auto found = frames.find(path.generic_u8string());
        if (found == frames.end()) {
            error = "Synthetic frame not found.";
            return false;
        }
        output = found->second;
        error.clear();
        return true;
    };
    return services;
}

Raw::Mfd::MfdProcessingRequest BaselineRequest(
    const std::filesystem::path& working) {
    Raw::Mfd::MfdProcessingRequest request;
    request.fusionBackend = Raw::Mfd::MfdFusionBackend::LegacyRaCfaV1;
    request.frames = { Input("reference.dng", 'a'), Input("alternate.dng", 'b') };
    request.referenceFrameIndex = 0u;
    request.workingDirectory = working;
    request.memoryBudgetBytes = 512ull * 1024ull * 1024ull;
    request.workerCount = 2u;
    request.parameters.fusion.outputTileRawPixels = 64u;
    request.parameters.registration.finestPatchPlanePixels = 16u;
    request.parameters.registration.finestStridePlanePixels = 16u;
    request.parameters.registration.coarsePatchLevelPixels = 8u;
    request.parameters.registration.minimumStructuredSamples = 128u;
    request.parameters.registration.positionalConfidenceScaleRawPixels = 0.50;
    return request;
}

bool ValidateDenoisedCandidate() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);
    const Raw::RawImageData reference = MakeSyntheticRaw('a', 0.0, 0.0, 11u);
    const Raw::RawImageData alternate = MakeSyntheticRaw('b', 8.0, -8.0, 29u);
    auto request = BaselineRequest(temporary / "working");
    std::vector<Raw::Mfd::MfdProcessingProgress> progressUpdates;
    request.reportProgress = [&](const Raw::Mfd::MfdProcessingProgress& progress) {
        progressUpdates.push_back(progress);
    };
    const auto result = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", reference },
            { "alternate.dng", alternate }
        }));
    if (result.status != Raw::Mfd::MfdProcessingStatus::DenoisedCandidate) {
        std::cerr << Raw::Mfd::SerializeMfdProcessingResult(result).dump(2)
                  << std::endl;
    }
    bool ok = true;
    ok &= Check(
        result.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate,
        "the compatible translated burst did not produce a denoised candidate: " +
            result.message);
    ok &= Check(result.published.result &&
            result.diagnostics.acceptedAlternateCount == 1u &&
            result.diagnostics.contributingPixelCount > 1000u &&
            result.referenceNormalizedMosaic.size() ==
                static_cast<std::size_t>(kWidth) * kHeight,
        "the published candidate lacks accepted alternate contributions");
    ok &= Check(
        result.outputCfaPattern == Raw::CfaPattern::RGGB &&
            result.referenceMetadata.cameraModel ==
                reference.metadata.cameraModel &&
            result.referenceMetadata.orientation ==
                reference.metadata.orientation &&
            result.referenceMetadata.hasDngNoiseProfile ==
                reference.metadata.hasDngNoiseProfile,
        "the atomic Bayer result did not retain the reference CFA/color/placement metadata required by downstream RAW development");
    bool monotonicProgress = true;
    bool sawReference = false;
    bool sawPyramid = false;
    bool sawAlternate = false;
    bool sawRegistration = false;
    bool sawFusionUnits = false;
    bool sawFinalizing = false;
    double priorProgress = 0.0;
    for (const Raw::Mfd::MfdProcessingProgress& progress : progressUpdates) {
        monotonicProgress = monotonicProgress &&
            progress.overallFraction + 1.0e-12 >= priorProgress;
        priorProgress = std::max(priorProgress, progress.overallFraction);
        sawReference = sawReference || progress.stage ==
            Raw::Mfd::MfdProcessingStage::PreparingReference;
        sawPyramid = sawPyramid || progress.stage ==
            Raw::Mfd::MfdProcessingStage::BuildingReferencePyramid;
        sawAlternate = sawAlternate || progress.stage ==
            Raw::Mfd::MfdProcessingStage::PreparingAlternate;
        sawRegistration = sawRegistration || progress.stage ==
            Raw::Mfd::MfdProcessingStage::RegisteringAlternate;
        sawFusionUnits = sawFusionUnits ||
            (progress.stage == Raw::Mfd::MfdProcessingStage::FusingTiles &&
             progress.totalUnits > 0u &&
             progress.completedUnits <= progress.totalUnits);
        sawFinalizing = sawFinalizing || progress.stage ==
            Raw::Mfd::MfdProcessingStage::Finalizing;
    }
    ok &= Check(
        !progressUpdates.empty() && monotonicProgress && sawReference &&
            sawPyramid && sawAlternate && sawRegistration && sawFusionUnits &&
            sawFinalizing && priorProgress >= 0.95,
        "processor progress did not report a monotonic, real-stage path through fusion");
    if (result.published.result) {
        std::vector<float> truth(
            static_cast<std::size_t>(kWidth) * kHeight);
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                truth[static_cast<std::size_t>(y) * kWidth + x] =
                    static_cast<float>(Scene(x, y, SiteAt(x, y)));
            }
        }
        double referenceSquaredError = 0.0;
        double outputSquaredError = 0.0;
        std::uint64_t count = 0u;
        for (int y = 24; y < kHeight - 24; ++y) {
            for (int x = 24; x < kWidth - 24; ++x) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * kWidth + x;
                const double truthValue = truth[index];
                const double referenceValue =
                    (static_cast<double>(reference.rawBuffer[index]) - 64.0) /
                    4096.0;
                const double outputValue =
                    result.published.result->normalizedMosaic[index];
                referenceSquaredError +=
                    (referenceValue - truthValue) *
                    (referenceValue - truthValue);
                outputSquaredError +=
                    (outputValue - truthValue) * (outputValue - truthValue);
                ++count;
            }
        }
        ok &= Check(count > 0u && outputSquaredError < referenceSquaredError,
            "the synthetic fused mosaic did not reduce error versus the noisy reference");

        Raw::Mfd::MfdInspectionInputView inspectionInput;
        inspectionInput.sampleId = "synthetic-end-to-end";
        inspectionInput.cfaPattern = Raw::CfaPattern::RGGB;
        inspectionInput.extent = { kWidth, kHeight };
        inspectionInput.referenceNormalizedMosaic =
            &result.referenceNormalizedMosaic;
        inspectionInput.outputNormalizedMosaic =
            &result.published.result->normalizedMosaic;
        inspectionInput.fusionDiagnostics =
            &result.published.result->diagnostics;
        Raw::Mfd::MfdInspectionSummary inspection;
        std::string inspectionError;
        ok &= Check(Raw::Mfd::WriteMfdInspectionPacketView(
                inspectionInput,
                temporary / "inspection",
                inspection,
                &inspectionError),
            "the end-to-end inspection packet failed: " + inspectionError);
        ok &= Check(inspection.valid && inspection.writtenFiles.size() == 7u &&
                inspection.contributingPixelCount ==
                    result.diagnostics.contributingPixelCount,
            "inspection artifacts lost the processor contribution diagnostics");
        if (!inspection.writtenFiles.empty()) {
            std::ifstream png(temporary / "inspection" /
                "output-preview.png", std::ios::binary);
            std::array<unsigned char, 8> signature {};
            png.read(
                reinterpret_cast<char*>(signature.data()),
                static_cast<std::streamsize>(signature.size()));
            const std::array<unsigned char, 8> expected {
                0x89u, 'P', 'N', 'G', 0x0du, 0x0au, 0x1au, 0x0au };
            ok &= Check(signature == expected,
                "the viewable MFD preview is not a PNG file");
        }

        Raw::Mfd::MfdEvaluationInput evaluationInput;
        evaluationInput.sampleId = "synthetic-end-to-end";
        evaluationInput.cfaPattern = Raw::CfaPattern::RGGB;
        evaluationInput.extent = { kWidth, kHeight };
        evaluationInput.noiseFreeReferenceMosaic = std::move(truth);
        evaluationInput.noisyReferenceMosaic =
            result.referenceNormalizedMosaic;
        evaluationInput.outputMosaic =
            result.published.result->normalizedMosaic;
        evaluationInput.fusionDiagnostics =
            result.published.result->diagnostics;
        Raw::Mfd::MfdEvaluationResult evaluation;
        std::string evaluationError;
        ok &= Check(Raw::Mfd::EvaluateMfdOutput(
                evaluationInput, evaluation, &evaluationError),
            "the processor result could not enter the evaluation harness: " +
                evaluationError);
        ok &= Check(evaluation.valid &&
                evaluation.raw.rmseImprovementRatio > 1.0,
            "the integrated evaluation did not measure the known RAW improvement");
    }
    std::filesystem::remove_all(temporary, filesystemError);
    return ok;
}

bool ValidateLowSignalNoiseFallback() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);
    const Raw::RawImageData reference =
        MakeLowSignalRawWithoutNoiseProfile('a', 96u);
    const Raw::RawImageData alternate =
        MakeLowSignalRawWithoutNoiseProfile('b', 96u);
    auto request = BaselineRequest(temporary / "low-signal-noise-fallback");
    request.alignmentMode = Raw::Mfd::MfdAlignmentMode::Identity;

    const auto result = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", reference },
            { "alternate.dng", alternate }
        }));

    bool ok = true;
    ok &= Check(
        result.diagnostics.frames.size() == 2u &&
            result.diagnostics.frames[0].noiseQuality ==
                Raw::Mfd::NoiseModelQuality::GenericLowConfidence &&
            result.diagnostics.frames[1].attempted &&
            result.diagnostics.frames[1].noiseQuality ==
                Raw::Mfd::NoiseModelQuality::GenericLowConfidence,
        "a valid low-signal burst without NoiseProfile stopped before attempting its alternate");
    ok &= Check(
        result.diagnostics.compatibleAlternateCount == 1u &&
            result.diagnostics.acceptedAlternateCount == 1u &&
            result.diagnostics.contributingPixelCount > 0u &&
            result.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate,
        "the conservative generic noise fallback did not permit a spatially safe low-signal alternate to contribute: " +
            result.message);

    std::filesystem::remove_all(temporary, filesystemError);
    return ok;
}

bool ValidateReferenceOnlyAndBudgetGuard() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);
    const Raw::RawImageData reference = MakeSyntheticRaw('a', 0.0, 0.0, 3u);
    const Raw::RawImageData incompatible =
        MakeSyntheticRaw('c', 0.0, 0.0, 5u, 8);
    auto request = BaselineRequest(temporary / "reference-only");
    request.frames[1] = Input("incompatible.dng", 'c');
    const auto fallback = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", reference },
            { "incompatible.dng", incompatible }
        }));
    bool ok = true;
    ok &= Check(
        fallback.status == Raw::Mfd::MfdProcessingStatus::ReferenceOnly &&
            fallback.published.result &&
            fallback.diagnostics.contributingPixelCount == 0u,
        "an incompatible alternate did not publish an explicit reference-only result");
    if (fallback.published.result) {
        for (std::size_t index = 0u; index < reference.rawBuffer.size(); ++index) {
            const float expected = static_cast<float>(
                (static_cast<double>(reference.rawBuffer[index]) - 64.0) /
                4096.0);
            if (fallback.published.result->normalizedMosaic[index] != expected) {
                ok &= Check(false,
                    "reference-only publication changed a normalized reference sample");
                break;
            }
        }
    }

    auto sharedBurstRequest = request;
    sharedBurstRequest.workingDirectory = temporary / "shared-reference-only";
    sharedBurstRequest.fusionBackend =
        Raw::Mfd::MfdFusionBackend::SharedBurstV1;
    const auto sharedBurstFallback = Raw::Mfd::ProcessMfdBurst(
        sharedBurstRequest,
        Services({
            { "reference.dng", reference },
            { "incompatible.dng", incompatible }
        }));
    ok &= Check(
        sharedBurstFallback.status ==
                Raw::Mfd::MfdProcessingStatus::ReferenceOnly &&
            sharedBurstFallback.published.result &&
            sharedBurstFallback.fusionBackend ==
                Raw::Mfd::MfdFusionBackend::SharedBurstV1 &&
            sharedBurstFallback.diagnostics.acceptedAlternateCount == 0u,
        "Shared Burst left no publication when every alternate was rejected");

    request.workingDirectory = temporary / "budget-guard";
    request.memoryBudgetBytes = 1u;
    const auto guarded = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", reference },
            { "incompatible.dng", incompatible }
        }));
    ok &= Check(
        guarded.status == Raw::Mfd::MfdProcessingStatus::Failed &&
            !guarded.published.result &&
            guarded.message.find("memory budget") != std::string::npos,
        "an impossible memory budget did not fail before output allocation");
    std::filesystem::remove_all(temporary, filesystemError);
    return ok;
}

bool ValidateAlignmentBypassModes() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);
    const Raw::RawImageData reference =
        MakeSyntheticRaw('a', 0.0, 0.0, 101u);
    const Raw::RawImageData alternate =
        MakeSyntheticRaw('b', 0.0, 0.0, 103u);

    auto identityRequest = BaselineRequest(temporary / "identity");
    identityRequest.alignmentMode = Raw::Mfd::MfdAlignmentMode::Identity;
    bool identityBuiltPyramid = false;
    identityRequest.reportProgress = [&](const auto& progress) {
        identityBuiltPyramid = identityBuiltPyramid ||
            progress.stage ==
                Raw::Mfd::MfdProcessingStage::BuildingReferencePyramid;
    };
    const auto identity = Raw::Mfd::ProcessMfdBurst(
        identityRequest,
        Services({
            { "reference.dng", reference },
            { "alternate.dng", alternate }
        }));

    auto translationRequest = BaselineRequest(temporary / "translation");
    translationRequest.alignmentMode =
        Raw::Mfd::MfdAlignmentMode::TranslationOnly;
    const auto translation = Raw::Mfd::ProcessMfdBurst(
        translationRequest,
        Services({
            { "reference.dng", reference },
            { "alternate.dng", alternate }
        }));

    bool ok = true;
    ok &= Check(
        identity.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
            identity.published.result &&
            identity.diagnostics.alignmentMode ==
                Raw::Mfd::MfdAlignmentMode::Identity &&
            identity.diagnostics.frames.size() == 2u &&
            identity.diagnostics.frames[1].globalAlignment ==
                Raw::Mfd::GlobalFrameAlignmentChoice::Identity &&
            !identity.diagnostics.frames[1].globalRegistrationPerformed &&
            !identity.diagnostics.frames[1].localRegistrationPerformed &&
            !identityBuiltPyramid,
        "identity-coordinate processing did not bypass pyramids and registration while producing a safe candidate");
    ok &= Check(
        translation.status ==
                Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
            translation.published.result &&
            translation.diagnostics.alignmentMode ==
                Raw::Mfd::MfdAlignmentMode::TranslationOnly &&
            translation.diagnostics.frames.size() == 2u &&
            translation.diagnostics.frames[1].globalAlignment ==
                Raw::Mfd::GlobalFrameAlignmentChoice::Translation &&
            translation.diagnostics.frames[1].globalRegistrationPerformed &&
            !translation.diagnostics.frames[1].localRegistrationPerformed,
        "translation-only processing did not retain global translation while bypassing local registration");
    if (identity.published.result && translation.published.result) {
        ok &= Check(
            identity.published.result->fusedResultCacheKey !=
                translation.published.result->fusedResultCacheKey,
            "alignment mode did not participate in the fused-result cache identity");
    }
    Raw::Mfd::MfdAlignmentMode parsed = Raw::Mfd::MfdAlignmentMode::Full;
    ok &= Check(
        Raw::Mfd::ParseMfdAlignmentMode("identity", parsed) &&
            parsed == Raw::Mfd::MfdAlignmentMode::Identity &&
            !Raw::Mfd::ParseMfdAlignmentMode("unsafe-magic", parsed),
        "alignment-mode persistence IDs were not parsed strictly");

    std::filesystem::remove_all(temporary, filesystemError);
    return ok;
}

bool ValidateAmbiguousLocalMotionUsesGlobalFallback() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);

    auto request = BaselineRequest(temporary / "ambiguous-local-motion");
    request.fusionBackend = Raw::Mfd::MfdFusionBackend::SharedBurstV1;
    request.parameters.registration.uniquenessTransitionMin = 1.0e30;
    request.parameters.registration.uniquenessTransitionMax = 1.0e31;
    request.parameters.registration.flatSafePhotometricCostLimit = 1.0e-12;
    const auto result = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", MakeSyntheticRaw('a', 0.0, 0.0, 211u) },
            { "alternate.dng", MakeSyntheticRaw('b', 0.0, 0.0, 223u) }
        }));

    const bool ok = Check(
        result.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
            result.published.result &&
            result.diagnostics.acceptedAlternateCount == 1u &&
            result.diagnostics.frames.size() == 2u &&
            result.diagnostics.frames[1].acceptedForFusion &&
            result.diagnostics.frames[1].globalRegistrationPerformed &&
            !result.diagnostics.frames[1].localRegistrationPerformed &&
            result.diagnostics.frames[1].message.find(
                "accepted global warp") != std::string::npos,
        "ambiguous local motion rejected a globally aligned Shared Burst frame instead of using reliability-gated fallback");
    if (!ok) {
        std::cerr << Raw::Mfd::SerializeMfdProcessingResult(result).dump(2)
                  << std::endl;
    }
    std::filesystem::remove_all(temporary, filesystemError);
    return ok;
}

bool ValidateCancellationBeforePublication() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    auto request = BaselineRequest(temporary / "canceled");
    request.shouldCancel = [] { return true; };
    const auto canceled = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", MakeSyntheticRaw('a', 0.0, 0.0, 1u) },
            { "alternate.dng", MakeSyntheticRaw('b', 0.0, 0.0, 2u) }
        }));
    std::error_code filesystemError;
    std::filesystem::remove_all(temporary, filesystemError);
    return Check(
        canceled.status == Raw::Mfd::MfdProcessingStatus::Canceled &&
            !canceled.published.result,
        "cancellation published a partial MFD result");
}

bool ValidateCancellationDuringRegistration() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    auto request = BaselineRequest(temporary / "registration-canceled");
    std::atomic<bool> cancel { false };
    request.shouldCancel = [&] { return cancel.load(); };
    request.reportProgress = [&](const Raw::Mfd::MfdProcessingProgress& progress) {
        if (progress.stage ==
                Raw::Mfd::MfdProcessingStage::RegisteringAlternate &&
            progress.stageFraction >= 0.4) {
            cancel.store(true);
        }
    };
    const auto canceled = Raw::Mfd::ProcessMfdBurst(
        request,
        Services({
            { "reference.dng", MakeSyntheticRaw('a', 0.0, 0.0, 41u) },
            { "alternate.dng", MakeSyntheticRaw('b', 8.0, -8.0, 43u) }
        }));
    std::error_code filesystemError;
    std::filesystem::remove_all(temporary, filesystemError);
    return Check(
        cancel.load() &&
            canceled.status == Raw::Mfd::MfdProcessingStatus::Canceled &&
            !canceled.published.result,
        "cancellation from a registration progress checkpoint published a result");
}

bool ValidateProcessingMemoryPolicy() {
    constexpr std::uint64_t gib = Raw::Mfd::kMemoryPolicyGibibyte;
    Raw::Mfd::PhysicalMemorySnapshot physical;
    physical.totalPhysicalBytes = 16ull * gib;
    physical.availablePhysicalBytes = 6ull * gib;
    physical.valid = true;

    const Raw::Mfd::MfdProcessingMemoryBudgetDecision automatic =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(0.0, physical);
    const Raw::Mfd::MfdProcessingMemoryBudgetDecision manual =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(3.0, physical);
    const Raw::Mfd::MfdProcessingMemoryBudgetDecision constrained =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(12.0, physical);
    const Raw::Mfd::MfdProcessingMemoryBudgetDecision invalid =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(-1.0, physical);
    Raw::Mfd::PhysicalMemorySnapshot pressuredPhysical = physical;
    pressuredPhysical.availablePhysicalBytes = 256ull * 1024ull * 1024ull;
    const Raw::Mfd::MfdProcessingMemoryBudgetDecision pressuredAutomatic =
        Raw::Mfd::ResolveMfdProcessingMemoryBudget(0.0, pressuredPhysical);

    bool ok = true;
    ok &= Check(
        automatic.valid && automatic.automatic &&
            automatic.reserveBytes == 2ull * gib &&
            automatic.budgetBytes == 4ull * gib,
        "automatic memory policy did not preserve a 2 GiB operating reserve");
    ok &= Check(
        manual.valid && !manual.automatic &&
            !manual.constrainedToSafeCeiling &&
            manual.budgetBytes == 3ull * gib,
        "a safe manual memory budget was not honored exactly");
    ok &= Check(
        constrained.valid && constrained.constrainedToSafeCeiling &&
            constrained.budgetBytes == automatic.budgetBytes,
        "an unsafe manual memory request exceeded the live safe ceiling");
    ok &= Check(!invalid.valid,
        "a negative manual memory budget was accepted");
    ok &= Check(
        pressuredAutomatic.valid && pressuredAutomatic.automatic &&
            pressuredAutomatic.budgetBytes > 0u &&
            pressuredAutomatic.budgetBytes < 512ull * 1024ull * 1024ull,
        "automatic advisory memory planning still hard-refused a pressured system");
    return ok;
}

bool ValidateSharedBurstProductionPath() {
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);

    constexpr std::size_t captureCount = 10u;
    std::unordered_map<std::string, Raw::RawImageData> frames;
    Raw::Mfd::MfdProcessingRequest first =
        BaselineRequest(temporary / "shared-workers-1");
    first.fusionBackend = Raw::Mfd::MfdFusionBackend::SharedBurstV1;
    first.alignmentMode = Raw::Mfd::MfdAlignmentMode::Identity;
    first.memoryBudgetBytes = 4ull * 1024ull * 1024ull * 1024ull;
    first.workerCount = 1u;
    first.parameters.fusion.outputTileRawPixels = 32u;
    first.frames.clear();
    for (std::size_t index = 0u; index < captureCount; ++index) {
        const char identity = static_cast<char>('a' + index);
        const std::string path = "shared-" +
            std::string(1u, identity) + ".dng";
        const std::string sha256 = SyntheticSha256(index);
        first.frames.push_back(Input(path, index, sha256));
        Raw::RawImageData raw = MakeSyntheticRaw(
            identity, 0.0, 0.0,
            static_cast<std::uint32_t>(100u + index));
        raw.metadata.sourceContentSha256 = sha256;
        frames.emplace(path, std::move(raw));
    }
    const Raw::Mfd::MfdProcessingServices services = Services(frames);
    const Raw::Mfd::MfdProcessingResult oneWorker =
        Raw::Mfd::ProcessMfdBurst(first, services);

    Raw::Mfd::MfdProcessingRequest parallel = first;
    parallel.workingDirectory = temporary / "shared-workers-4";
    parallel.workerCount = 4u;
    parallel.parameters.fusion.outputTileRawPixels = 64u;
    const Raw::Mfd::MfdProcessingResult fourWorkers =
        Raw::Mfd::ProcessMfdBurst(parallel, services);

    bool ok = true;
    if (oneWorker.status !=
            Raw::Mfd::MfdProcessingStatus::DenoisedCandidate ||
        !oneWorker.published.result) {
        std::cerr << Raw::Mfd::SerializeMfdProcessingResult(oneWorker).dump(2)
                  << std::endl;
    }
    ok &= Check(
        oneWorker.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
            oneWorker.published.result &&
            oneWorker.fusionBackend ==
                Raw::Mfd::MfdFusionBackend::SharedBurstV1 &&
            oneWorker.diagnostics.exposureGroupedCaptureCount == captureCount &&
            oneWorker.diagnostics.acceptedAlternateCount == captureCount - 1u &&
            oneWorker.diagnostics.meanEffectiveCaptureCount > 1.0 &&
            oneWorker.diagnostics.predictedIndependentNoiseReduction > 1.0 &&
            !oneWorker.diagnostics.independentNoiseReductionClaimQualified,
        "Shared Burst did not publish truthful grouped/effective diagnostics");
    ok &= Check(
        fourWorkers.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
            fourWorkers.published.result && oneWorker.published.result &&
            fourWorkers.published.result->normalizedMosaic ==
                oneWorker.published.result->normalizedMosaic,
        "Shared Burst output changed across worker counts or tile layouts");

    Raw::Mfd::MfdProcessingRequest grouped = first;
    grouped.workingDirectory = temporary / "shared-exposure-group";
    grouped.frames.resize(3u);
    auto groupedFrames = frames;
    groupedFrames["shared-a.dng"] =
        MakeSyntheticRaw('a', 0.0, 0.0, 149u, 1, 1.0, 0.001);
    groupedFrames["shared-a.dng"].metadata.sourceContentSha256 =
        SyntheticSha256(0u);
    groupedFrames["shared-b.dng"] =
        MakeSyntheticRaw('b', 0.0, 0.0, 150u, 1, 1.0, 0.001);
    groupedFrames["shared-b.dng"].metadata.sourceContentSha256 =
        SyntheticSha256(1u);
    groupedFrames["shared-c.dng"] =
        MakeSyntheticRaw('c', 0.0, 0.0, 151u, 1, 0.6, 0.001);
    groupedFrames["shared-c.dng"].metadata.sourceContentSha256 =
        SyntheticSha256(2u);
    const Raw::Mfd::MfdProcessingResult groupedResult =
        Raw::Mfd::ProcessMfdBurst(grouped, Services(groupedFrames));
    if (groupedResult.status !=
            Raw::Mfd::MfdProcessingStatus::DenoisedCandidate ||
        !groupedResult.published.result) {
        std::cerr << Raw::Mfd::SerializeMfdProcessingResult(groupedResult).dump(2)
                  << std::endl;
    }
    const bool groupingOk =
        groupedResult.status ==
                Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
            groupedResult.published.result &&
            groupedResult.diagnostics.exposureGroupedCaptureCount == 2u &&
            groupedResult.diagnostics.exposureExcludedCaptureCount == 1u &&
            !groupedResult.diagnostics.frames[2].exposureGrouped;
    if (!groupingOk) {
        std::cerr << Raw::Mfd::SerializeMfdProcessingResult(groupedResult).dump(2)
                  << std::endl;
    }
    ok &= Check(groupingOk,
        "the 0.5 EV Burst group did not retain a compatible pair and exclude the bracket exposure");

    Raw::Mfd::MfdProcessingRequest canceled = first;
    canceled.workingDirectory = temporary / "shared-canceled-30";
    canceled.frames.clear();
    std::unordered_map<std::string, Raw::RawImageData> cancellationFrames;
    for (std::size_t index = 0u;
         index < Raw::Mfd::kSharedBurstMaximumEnabledCaptures; ++index) {
        const char identity = static_cast<char>('A' + index);
        const std::string path = "cancel-" +
            std::to_string(index) + ".dng";
        const std::string sha256 = SyntheticSha256(index + 64u);
        canceled.frames.push_back(Input(path, index, sha256));
        Raw::RawImageData raw = MakeSyntheticRaw(
            identity, 0.0, 0.0,
            static_cast<std::uint32_t>(300u + index));
        raw.metadata.sourceContentSha256 = sha256;
        cancellationFrames.emplace(path, std::move(raw));
    }
    std::atomic<bool> cancelAtFusion { false };
    canceled.shouldCancel = [&] { return cancelAtFusion.load(); };
    canceled.reportProgress = [&](const Raw::Mfd::MfdProcessingProgress& progress) {
        if (progress.stage == Raw::Mfd::MfdProcessingStage::FusingTiles) {
            cancelAtFusion.store(true);
        }
    };
    const Raw::Mfd::MfdProcessingResult canceledResult =
        Raw::Mfd::ProcessMfdBurst(canceled, Services(cancellationFrames));
    if (canceledResult.status != Raw::Mfd::MfdProcessingStatus::Canceled) {
        std::cerr << Raw::Mfd::SerializeMfdProcessingResult(canceledResult).dump(2)
                  << std::endl;
    }
    ok &= Check(
        cancelAtFusion.load() &&
            canceledResult.status == Raw::Mfd::MfdProcessingStatus::Canceled &&
            !canceledResult.published.result,
        "30-frame Shared Burst cancellation published a partial result");

    Raw::Mfd::MfdProcessingRequest tooMany = canceled;
    tooMany.shouldCancel = {};
    tooMany.reportProgress = {};
    tooMany.frames.push_back(tooMany.frames.back());
    const Raw::Mfd::MfdProcessingResult tooManyResult =
        Raw::Mfd::ProcessMfdBurst(tooMany, Services(cancellationFrames));
    ok &= Check(
        tooManyResult.status == Raw::Mfd::MfdProcessingStatus::Failed &&
            !tooManyResult.published.result,
        "a 31-frame enabled Shared Burst request bypassed the processing limit");

    std::filesystem::remove_all(temporary, filesystemError);
    return ok;
}

bool ValidateMeasurementGraphDerivedChain() {
    using namespace Stack::Project;
    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);

    RawProjectSnapshot snapshot;
    snapshot.projectId = GenerateStableUuid();
    snapshot.projectName = "Derived measurement chain";
    MultiFrameSourceSet sourceSet;
    sourceSet.sourceSetId = GenerateStableUuid();
    sourceSet.name = "Two repeated bracket levels";
    sourceSet.inputFamily = MultiFrameInputFamily::Raw;
    sourceSet.operationIntent = MultiFrameOperationIntent::RawCaptureSet;

    std::unordered_map<std::string, Raw::RawImageData> decoded;
    std::unordered_map<std::string, std::filesystem::path> paths;
    for (std::size_t index = 0u; index < 4u; ++index) {
        const char identity = static_cast<char>('a' + index);
        const bool longExposure = index >= 2u;
        Raw::RawImageData raw = MakeSyntheticRaw(
            identity, 0.0, 0.0,
            static_cast<std::uint32_t>(900u + index),
            1,
            longExposure ? 1.0 : 0.25,
            0.006);
        raw.metadata.hasExposureTime = true;
        raw.metadata.exposureTimeSeconds = longExposure ? 1.0f / 25.0f : 1.0f / 100.0f;
        raw.metadata.hasIsoSpeed = true;
        raw.metadata.isoSpeed = 100.0f;
        raw.metadata.hasApertureFNumber = true;
        raw.metadata.apertureFNumber = 4.0f;

        EmbeddedAssetRecord asset;
        asset.sha256 = raw.metadata.sourceContentSha256;
        asset.byteLength = raw.metadata.sourceByteSize;
        asset.assetId = MakeAssetId(asset.sha256, asset.byteLength);
        asset.originalFilename = "chain-" + std::to_string(index) + ".dng";
        asset.inputFamily = MultiFrameInputFamily::Raw;
        asset.captureMetadataSummary = SerializeRawCaptureCompatibilitySummary(
            BuildRawCaptureCompatibilitySummary(raw.metadata));
        snapshot.embeddedAssets.push_back(asset);

        SourceSetFrame frame;
        frame.frameId = GenerateStableUuid();
        frame.assetId = asset.assetId;
        frame.userLabel = asset.originalFilename;
        const std::filesystem::path path =
            std::filesystem::path("synthetic-chain") / asset.originalFilename;
        paths[frame.frameId] = path;
        decoded[path.generic_string()] = std::move(raw);
        sourceSet.frames.push_back(std::move(frame));
    }
    sourceSet.referenceFrameId = sourceSet.frames.front().frameId;
    snapshot.activeSourceSetId = sourceSet.sourceSetId;
    snapshot.sourceSets.push_back(sourceSet);
    snapshot.multiFrameGraph = BuildManualMultiFrameGraph(
        snapshot, snapshot.sourceSets.front());

    std::vector<std::string> sourceNodeIds;
    for (const MultiFrameGraphNode& node : snapshot.multiFrameGraph.nodes) {
        if (node.kind == MultiFrameGraphNodeKind::CaptureSubset &&
            node.frameIds.size() == 1u) {
            sourceNodeIds.push_back(node.nodeId);
        }
    }
    const auto addProcessor = [&](MultiFrameGraphNodeKind kind,
                                  const char* title,
                                  double x,
                                  double y) {
        MultiFrameGraphNode node;
        node.nodeId = GenerateStableUuid();
        node.kind = kind;
        node.title = title;
        node.positionX = x;
        node.positionY = y;
        node.settings = {
            { "evidencePolicy", "disjoint-original-evidence-v1" }
        };
        const std::string nodeId = node.nodeId;
        snapshot.multiFrameGraph.nodes.push_back(std::move(node));
        return nodeId;
    };
    const auto addGraphLink = [&](const std::string& fromNodeId,
                                  const std::string& toNodeId,
                                  std::uint32_t order) {
        const MultiFrameGraphNode* from = FindMultiFrameGraphNode(
            snapshot.multiFrameGraph, fromNodeId);
        if (!from) return;
        MultiFrameGraphLink link;
        link.linkId = GenerateStableUuid();
        link.fromNodeId = fromNodeId;
        link.fromPortId = from->kind == MultiFrameGraphNodeKind::CaptureSubset
            ? "measurements"
            : "estimate";
        link.toNodeId = toNodeId;
        link.toPortId = "measurements";
        link.resourceType = from->kind == MultiFrameGraphNodeKind::CaptureSubset
            ? MultiFrameGraphResourceType::RawMeasurementSet
            : MultiFrameGraphResourceType::RawMeasurement;
        link.variadicOrder = order;
        snapshot.multiFrameGraph.links.push_back(std::move(link));
    };
    if (sourceNodeIds.size() == 4u) {
        const std::string shortBurstId = addProcessor(
            MultiFrameGraphNodeKind::BurstDenoise, "Short Burst", 380.0, 80.0);
        const std::string longBurstId = addProcessor(
            MultiFrameGraphNodeKind::BurstDenoise, "Long Burst", 380.0, 360.0);
        const std::string hdrId = addProcessor(
            MultiFrameGraphNodeKind::HdrMerge, "HDR Merge", 650.0, 220.0);
        addGraphLink(sourceNodeIds[0], shortBurstId, 0u);
        addGraphLink(sourceNodeIds[1], shortBurstId, 1u);
        addGraphLink(sourceNodeIds[2], longBurstId, 0u);
        addGraphLink(sourceNodeIds[3], longBurstId, 1u);
        addGraphLink(shortBurstId, hdrId, 0u);
        addGraphLink(longBurstId, hdrId, 1u);
        addGraphLink(hdrId, snapshot.multiFrameGraph.outputNodeId, 0u);
        snapshot.multiFrameGraph.userEdited = true;
    }

    Raw::Mfd::Parameters burstParameters;
    burstParameters.fusion.outputTileRawPixels = 64u;
    burstParameters.registration.finestPatchPlanePixels = 16u;
    burstParameters.registration.finestStridePlanePixels = 16u;
    burstParameters.registration.coarsePatchLevelPixels = 8u;
    burstParameters.registration.minimumStructuredSamples = 128u;
    for (MultiFrameGraphNode& node : snapshot.multiFrameGraph.nodes) {
        if (node.kind == MultiFrameGraphNodeKind::BurstDenoise)
            node.settings["parameters"] = Raw::Mfd::SerializeParameters(
                burstParameters);
    }

    Raw::MultiFrame::GraphProcessingRequest request;
    request.snapshot = snapshot;
    request.materializedSourcePathsByFrameId = paths;
    request.workingDirectory = temporary / "graph";
    request.memoryBudgetBytes = 1u;
    request.enforceMemoryBudget = false;
    request.workerCount = 2u;
    request.loadRawFrame = [decoded = std::move(decoded)](
        const std::filesystem::path& path,
        Raw::RawImageData& output,
        const std::function<bool()>& shouldCancel,
        std::string& error) {
        if (shouldCancel && shouldCancel()) {
            error = "Synthetic graph load canceled.";
            return false;
        }
        const auto found = decoded.find(path.generic_string());
        if (found == decoded.end()) {
            error = "Synthetic graph frame not found.";
            return false;
        }
        output = found->second;
        error.clear();
        return true;
    };
    const auto result = Raw::MultiFrame::ProcessMultiFrameGraph(request);
    const bool completed = result.status ==
            Raw::MultiFrame::GraphProcessingStatus::Completed &&
        result.outputRawData && result.outputMeasurement &&
        result.outputHdrResult &&
        !result.outputHdrCacheDirectory.empty() &&
        result.outputRawData->normalizedMosaicBuffer &&
        result.outputRawData->hdrSidecars &&
        result.outputRawData->hdrSidecars->mergeConfidence &&
        result.outputRawData->hdrSidecars->mergeConfidence->size() ==
            result.outputRawData->normalizedMosaicBuffer->size() &&
        result.outputHdrResult->diagnostics.cacheKey ==
            result.outputRawData->metadata.sourceContentSha256 &&
        result.outputHdrResult->virtualAnchorMosaic.data() ==
            result.outputRawData->normalizedMosaicBuffer->data() &&
        result.outputRawData->multiFrameMeasurementSidecars &&
        result.outputRawData->multiFrameMeasurementSidecars->variance &&
        result.outputRawData->multiFrameMeasurementSidecars->variance->size() ==
            result.outputRawData->normalizedMosaicBuffer->size() &&
        result.outputMeasurement->evidence.contributions.size() == 4u &&
        std::count_if(
            result.processedNodes.begin(), result.processedNodes.end(),
            [](const Raw::MultiFrame::GraphProcessedNode& node) {
                return node.adapter ==
                    Raw::MultiFrame::GraphExecutionAdapter::SharedBurstV1;
            }) == 2 &&
        std::count_if(
            result.processedNodes.begin(), result.processedNodes.end(),
            [](const Raw::MultiFrame::GraphProcessedNode& node) {
                return node.adapter == Raw::MultiFrame::GraphExecutionAdapter::HdrV4;
            }) == 1;
    if (!completed) {
        std::cerr << "Derived graph result: " << result.message << std::endl;
    }
    std::filesystem::remove_all(temporary, filesystemError);
    return Check(completed,
        "two disjoint Burst results did not chain through HDR with variance evidence");
}

} // namespace

bool ValidateMfdEndToEndProcessor() {
    bool ok = true;
    ok &= ValidateDenoisedCandidate();
    ok &= ValidateLowSignalNoiseFallback();
    ok &= ValidateAlignmentBypassModes();
    ok &= ValidateAmbiguousLocalMotionUsesGlobalFallback();
    ok &= ValidateReferenceOnlyAndBudgetGuard();
    ok &= ValidateCancellationBeforePublication();
    ok &= ValidateCancellationDuringRegistration();
    ok &= ValidateProcessingMemoryPolicy();
    if (ok) {
        std::cout
            << "MFD end-to-end processor validation passed: translated and low-signal Bayer bursts reached safe processing, while incompatible, low-memory, and canceled paths remained safe."
            << std::endl;
    }
    return ok;
}

bool ValidateSharedBurstEndToEndProcessor() {
    const bool ok = ValidateSharedBurstProductionPath() &&
        ValidateMeasurementGraphDerivedChain();
    if (ok) {
        std::cout
            << "Shared Burst end-to-end validation passed: 10-frame production fusion was deterministic, exposure grouping excluded bracket data, and 30-frame cancellation remained atomic."
            << std::endl;
    }
    return ok;
}

bool ValidateSharedBurstGpuFusion() {
    bool ok = true;
    if (!glfwInit())
        return Check(false,
            "GLFW could not initialize the Shared Burst GPU validation context");
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(
        16, 16, "Stack Shared Burst GPU Validation", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return Check(false,
            "OpenGL 4.3 is unavailable for Shared Burst GPU validation");
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return Check(false,
            "OpenGL functions could not be loaded for Shared Burst GPU validation");
    }

    const std::filesystem::path temporary = MakeTemporaryDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(temporary, filesystemError);
    constexpr std::size_t captureCount = 4u;
    std::unordered_map<std::string, Raw::RawImageData> frames;
    Raw::Mfd::MfdProcessingRequest base =
        BaselineRequest(temporary / "gpu-base");
    base.fusionBackend = Raw::Mfd::MfdFusionBackend::SharedBurstV1;
    base.alignmentMode = Raw::Mfd::MfdAlignmentMode::Identity;
    base.memoryBudgetBytes = 2ull * 1024ull * 1024ull * 1024ull;
    base.workerCount = 2u;
    base.parameters.fusion.outputTileRawPixels = 64u;
    base.frames.clear();
    for (std::size_t index = 0u; index < captureCount; ++index) {
        const char identity = static_cast<char>('k' + index);
        const std::string path = "gpu-shared-" +
            std::to_string(index) + ".dng";
        const std::string sha256 = SyntheticSha256(index + 128u);
        base.frames.push_back(Input(path, index, sha256));
        // Identical sensor evidence keeps the parity fixture far from every
        // categorical gate boundary while exercising all CFA sites.
        Raw::RawImageData raw = MakeSyntheticRaw(
            identity, 0.0, 0.0, 909u, 1, 1.0, 0.003);
        raw.metadata.sourceContentSha256 = sha256;
        frames.emplace(path, std::move(raw));
    }

    Raw::Mfd::MfdProcessingRequest cpuRequest = base;
    cpuRequest.workingDirectory = temporary / "cpu";
    cpuRequest.preferGpuFusion = false;
    Raw::Mfd::MfdProcessingResult cpu = Raw::Mfd::ProcessMfdBurst(
        cpuRequest, Services(frames));

    Raw::Mfd::MfdProcessingRequest gpuRequest = base;
    gpuRequest.workingDirectory = temporary / "gpu";
    Raw::Mfd::MfdProcessingServices gpuServices = Services(frames);
    gpuServices.executeOpenGlTask = [](
        Raw::OpenGlTask task,
        std::string& error) {
        return task ? task(error) : false;
    };
    Raw::Mfd::MfdProcessingResult gpu = Raw::Mfd::ProcessMfdBurst(
        gpuRequest, gpuServices);

    const bool published = cpu.published.result && gpu.published.result &&
        cpu.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate &&
        gpu.status == Raw::Mfd::MfdProcessingStatus::DenoisedCandidate;
    ok &= Check(published,
        "CPU/GPU Shared Burst parity inputs did not both publish: " +
            gpu.message + " " + gpu.diagnostics.gpuFallbackReason);
    if (published) {
        ok &= Check(
            gpu.diagnostics.executionBackend == "opengl-4.3-compute-fp32" &&
            gpu.diagnostics.gpuDispatchedTileCount > 0u,
            "Shared Burst silently fell back instead of executing GPU compute: " +
                gpu.diagnostics.gpuFallbackReason);
        const auto& cpuMosaic = cpu.published.result->normalizedMosaic;
        const auto& gpuMosaic = gpu.published.result->normalizedMosaic;
        const auto& cpuDiagnostics = cpu.published.result->diagnostics;
        const auto& gpuDiagnostics = gpu.published.result->diagnostics;
        ok &= Check(cpuMosaic.size() == gpuMosaic.size() &&
                cpuDiagnostics.size() == gpuDiagnostics.size(),
            "Shared Burst GPU changed the published result shape");
        for (std::size_t pixel = 0u;
             ok && pixel < cpuMosaic.size(); ++pixel) {
            const double mosaicTolerance = 2.0e-5 + 5.0e-4 *
                std::max(std::abs(cpuMosaic[pixel]),
                         std::abs(gpuMosaic[pixel]));
            const auto& expected = cpuDiagnostics[pixel];
            const auto& actual = gpuDiagnostics[pixel];
            const double varianceTolerance = 2.0e-8 + 3.0e-3 *
                std::max(std::abs(expected.outputVarianceComparisonDomain),
                         std::abs(actual.outputVarianceComparisonDomain));
            const bool categorical =
                expected.decisionReason == actual.decisionReason &&
                expected.dominantRejectionReason ==
                    actual.dominantRejectionReason &&
                expected.exactReferenceCopy == actual.exactReferenceCopy &&
                expected.eligibleAlternateCount ==
                    actual.eligibleAlternateCount &&
                expected.contributingAlternateCount ==
                    actual.contributingAlternateCount &&
                expected.rejectedAlternateCount ==
                    actual.rejectedAlternateCount &&
                expected.rawSupportCount == actual.rawSupportCount &&
                expected.ownerSourceIndex == actual.ownerSourceIndex;
            if (std::abs(cpuMosaic[pixel] - gpuMosaic[pixel]) >
                    mosaicTolerance ||
                std::abs(expected.outputVarianceComparisonDomain -
                    actual.outputVarianceComparisonDomain) >
                    varianceTolerance || !categorical) {
                ok &= Check(false,
                    "Shared Burst GPU parity diverged at pixel " +
                        std::to_string(pixel));
            }
        }
    }

    // Exercise the first registration-specific GPU slice independently from
    // GPU fusion.  The accelerated FP32 cost surface must lead to the same
    // CPU-verified local-motion decisions and final CPU fusion result.
    std::unordered_map<std::string, Raw::RawImageData> registrationFrames {
        { "reference.dng", MakeSyntheticRaw('a', 0.0, 0.0, 11u) },
        { "alternate.dng", MakeSyntheticRaw('b', 8.0, -8.0, 29u) }
    };
    Raw::Mfd::MfdProcessingRequest registrationBase =
        BaselineRequest(temporary / "registration-base");
    registrationBase.preferGpuFusion = false;
    registrationBase.alignmentMode = Raw::Mfd::MfdAlignmentMode::Full;
    Raw::Mfd::MfdProcessingRequest registrationCpuRequest = registrationBase;
    registrationCpuRequest.workingDirectory =
        temporary / "registration-cpu";
    registrationCpuRequest.preferGpuRegistration = false;
    const Raw::Mfd::MfdProcessingResult registrationCpu =
        Raw::Mfd::ProcessMfdBurst(
            registrationCpuRequest, Services(registrationFrames));

    Raw::Mfd::MfdProcessingRequest registrationGpuRequest = registrationBase;
    registrationGpuRequest.workingDirectory =
        temporary / "registration-gpu";
    Raw::Mfd::MfdProcessingServices registrationGpuServices =
        Services(registrationFrames);
    registrationGpuServices.executeOpenGlTask = [](
        Raw::OpenGlTask task,
        std::string& error) {
        return task ? task(error) : false;
    };
    const Raw::Mfd::MfdProcessingResult registrationGpu =
        Raw::Mfd::ProcessMfdBurst(
            registrationGpuRequest, registrationGpuServices);
    const bool registrationPublished =
        registrationCpu.published.result &&
        registrationGpu.published.result;
    ok &= Check(
        registrationPublished &&
            registrationGpu.diagnostics.registrationBackend ==
                "opengl-4.3-discrete-search-fp32+cpu-refinement-fp64" &&
            registrationGpu.diagnostics.registrationGpuDispatchCount > 0u &&
            registrationGpu.diagnostics.
                registrationGpuScoredCandidateCount > 0u,
        "full-alignment local registration did not execute its GPU score path: " +
            registrationGpu.message + " " +
            registrationGpu.diagnostics.registrationGpuFallbackReason);
    if (registrationPublished) {
        const auto& expected =
            registrationCpu.published.result->normalizedMosaic;
        const auto& actual =
            registrationGpu.published.result->normalizedMosaic;
        ok &= Check(expected.size() == actual.size(),
            "GPU local registration changed the published mosaic shape");
        for (std::size_t pixel = 0u;
             ok && pixel < expected.size();
             ++pixel) {
            if (std::abs(expected[pixel] - actual[pixel]) > 1.0e-7) {
                ok &= Check(false,
                    "GPU local-registration parity diverged at pixel " +
                        std::to_string(pixel));
            }
        }
        ok &= Check(
            registrationCpu.diagnostics.frames.size() ==
                    registrationGpu.diagnostics.frames.size() &&
                registrationCpu.diagnostics.frames[1].structuredMotionNodes ==
                    registrationGpu.diagnostics.frames[1].structuredMotionNodes &&
                registrationCpu.diagnostics.frames[1].flatSafeMotionNodes ==
                    registrationGpu.diagnostics.frames[1].flatSafeMotionNodes &&
                registrationCpu.diagnostics.frames[1].rejectedMotionNodes ==
                    registrationGpu.diagnostics.frames[1].rejectedMotionNodes,
            "GPU scoring changed CPU-authoritative local-motion categories");
    }

    Raw::Mfd::MfdProcessingRequest registrationFallbackRequest =
        registrationBase;
    registrationFallbackRequest.workingDirectory =
        temporary / "registration-fallback";
    Raw::Mfd::MfdProcessingServices registrationFallbackServices =
        Services(registrationFrames);
    registrationFallbackServices.executeOpenGlTask = [](
        Raw::OpenGlTask,
        std::string& error) {
        error = "forced registration validation failure";
        return false;
    };
    const Raw::Mfd::MfdProcessingResult registrationFallback =
        Raw::Mfd::ProcessMfdBurst(
            registrationFallbackRequest, registrationFallbackServices);
    ok &= Check(
        registrationFallback.published.result &&
            registrationFallback.diagnostics.registrationBackend ==
                "cpu-reference" &&
            registrationFallback.diagnostics.registrationGpuFallbackReason ==
                "forced registration validation failure" &&
            registrationCpu.published.result &&
            registrationFallback.published.result->normalizedMosaic ==
                registrationCpu.published.result->normalizedMosaic,
        "a local-registration GPU failure did not recover through the exact CPU path");

    const std::array<Raw::CfaPattern, 3> additionalPatterns {
        Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GBRG,
        Raw::CfaPattern::GRBG
    };
    for (const Raw::CfaPattern pattern : additionalPatterns) {
        auto patternFrames = frames;
        for (auto& [path, raw] : patternFrames) {
            (void)path;
            raw.metadata.cfaPattern = pattern;
        }
        Raw::Mfd::MfdProcessingRequest patternCpuRequest = base;
        patternCpuRequest.workingDirectory = temporary /
            ("cpu-cfa-" + std::to_string(static_cast<int>(pattern)));
        patternCpuRequest.preferGpuFusion = false;
        const Raw::Mfd::MfdProcessingResult patternCpu =
            Raw::Mfd::ProcessMfdBurst(
                patternCpuRequest, Services(patternFrames));
        Raw::Mfd::MfdProcessingRequest patternGpuRequest = base;
        patternGpuRequest.workingDirectory = temporary /
            ("gpu-cfa-" + std::to_string(static_cast<int>(pattern)));
        Raw::Mfd::MfdProcessingServices patternGpuServices =
            Services(patternFrames);
        patternGpuServices.executeOpenGlTask = [](
            Raw::OpenGlTask task,
            std::string& error) {
            return task ? task(error) : false;
        };
        const Raw::Mfd::MfdProcessingResult patternGpu =
            Raw::Mfd::ProcessMfdBurst(
                patternGpuRequest, patternGpuServices);
        const bool patternPublished = patternCpu.published.result &&
            patternGpu.published.result &&
            patternGpu.diagnostics.executionBackend ==
                "opengl-4.3-compute-fp32";
        ok &= Check(patternPublished,
            "Shared Burst GPU did not publish for CFA pattern " +
                std::to_string(static_cast<int>(pattern)) + ": " +
                patternGpu.diagnostics.gpuFallbackReason);
        if (!patternPublished) continue;
        const auto& expectedMosaic =
            patternCpu.published.result->normalizedMosaic;
        const auto& actualMosaic =
            patternGpu.published.result->normalizedMosaic;
        const auto& expectedDiagnostics =
            patternCpu.published.result->diagnostics;
        const auto& actualDiagnostics =
            patternGpu.published.result->diagnostics;
        for (std::size_t pixel = 0u;
             ok && pixel < expectedMosaic.size(); ++pixel) {
            const double tolerance = 2.0e-5 + 5.0e-4 *
                std::max(std::abs(expectedMosaic[pixel]),
                         std::abs(actualMosaic[pixel]));
            const bool categorical =
                expectedDiagnostics[pixel].decisionReason ==
                    actualDiagnostics[pixel].decisionReason &&
                expectedDiagnostics[pixel].exactReferenceCopy ==
                    actualDiagnostics[pixel].exactReferenceCopy &&
                expectedDiagnostics[pixel].contributingAlternateCount ==
                    actualDiagnostics[pixel].contributingAlternateCount &&
                expectedDiagnostics[pixel].ownerSourceIndex ==
                    actualDiagnostics[pixel].ownerSourceIndex;
            if (std::abs(expectedMosaic[pixel] - actualMosaic[pixel]) >
                    tolerance || !categorical) {
                ok &= Check(false,
                    "Shared Burst GPU CFA parity diverged at pixel " +
                        std::to_string(pixel));
            }
        }
    }

    Raw::Mfd::MfdProcessingRequest fallbackRequest = base;
    fallbackRequest.workingDirectory = temporary / "fallback";
    Raw::Mfd::MfdProcessingServices fallbackServices = Services(frames);
    fallbackServices.executeOpenGlTask = [](
        Raw::OpenGlTask,
        std::string& error) {
        error = "forced validation failure";
        return false;
    };
    const Raw::Mfd::MfdProcessingResult fallback =
        Raw::Mfd::ProcessMfdBurst(fallbackRequest, fallbackServices);
    ok &= Check(fallback.published.result &&
            fallback.diagnostics.executionBackend == "cpu-reference" &&
            fallback.diagnostics.gpuFallbackReason ==
                "forced validation failure" &&
            cpu.published.result &&
            fallback.published.result->normalizedMosaic ==
                cpu.published.result->normalizedMosaic,
        "a Shared Burst GPU failure did not recover through the CPU reference path");

    Raw::Mfd::MfdProcessingRequest cancelRequest = base;
    cancelRequest.workingDirectory = temporary / "cancel";
    std::atomic<bool> gpuTaskActive { false };
    std::atomic<std::uint32_t> gpuCancelChecks { 0u };
    cancelRequest.shouldCancel = [&]() {
        return gpuTaskActive.load(std::memory_order_relaxed) &&
            gpuCancelChecks.fetch_add(1u, std::memory_order_relaxed) >= 6u;
    };
    Raw::Mfd::MfdProcessingServices cancelServices = Services(frames);
    cancelServices.executeOpenGlTask = [&gpuTaskActive](
        Raw::OpenGlTask task,
        std::string& error) {
        gpuTaskActive.store(true, std::memory_order_relaxed);
        return task ? task(error) : false;
    };
    const Raw::Mfd::MfdProcessingResult canceled =
        Raw::Mfd::ProcessMfdBurst(cancelRequest, cancelServices);
    ok &= Check(canceled.status ==
            Raw::Mfd::MfdProcessingStatus::Canceled &&
            !canceled.published.result,
        "tile-boundary GPU cancellation exposed a partial Shared Burst result");

    EditorRenderWorker worker;
    ok &= Check(worker.Initialize(window),
        "the production render worker could not initialize for Shared Burst GPU");
    if (ok) {
        Raw::Mfd::MfdProcessingRequest workerRequest = base;
        workerRequest.workingDirectory = temporary / "render-worker";
        Raw::Mfd::MfdProcessingServices workerServices = Services(frames);
        workerServices.executeOpenGlTask = [&worker](
            Raw::OpenGlTask task,
            std::string& error) {
            return worker.ExecuteOpenGlTaskBlocking(
                std::move(task), error);
        };
        const Raw::Mfd::MfdProcessingResult workerResult =
            Raw::Mfd::ProcessMfdBurst(workerRequest, workerServices);
        ok &= Check(workerResult.published.result &&
                workerResult.diagnostics.executionBackend ==
                    "opengl-4.3-compute-fp32",
            "the render-worker handoff did not execute Shared Burst GPU: " +
                workerResult.diagnostics.gpuFallbackReason);
    }
    worker.Shutdown();

    std::filesystem::remove_all(temporary, filesystemError);
    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[shared-burst-v1] OpenGL compute parity and fallback "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

} // namespace Stack::Validation
