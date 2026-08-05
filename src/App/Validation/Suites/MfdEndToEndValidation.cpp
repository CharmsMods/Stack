#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Evaluation.h"
#include "Raw/MultiFrameDenoise/Inspection.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Raw/MultiFrameDenoise/Processor.h"

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
    int orientation = 1) {
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
    std::normal_distribution<double> noise(0.0, 0.0090);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const double normalized = std::clamp(
                Scene(
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
    return ok;
}

} // namespace

bool ValidateMfdEndToEndProcessor() {
    bool ok = true;
    ok &= ValidateDenoisedCandidate();
    ok &= ValidateLowSignalNoiseFallback();
    ok &= ValidateAlignmentBypassModes();
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

} // namespace Stack::Validation
