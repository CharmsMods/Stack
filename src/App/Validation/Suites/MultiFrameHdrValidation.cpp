#include "App/Validation/ValidationSuites.h"

#include "Editor/EditorRenderWorker.h"
#include "Persistence/RawProjectModel.h"
#include "Raw/MultiFrame/PublicationFence.h"
#include "Raw/MultiFrameHdr/Processor.h"
#include "Renderer/GLLoader.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Validation {
namespace {

constexpr int kHdrWidth = 256;
constexpr int kHdrHeight = 256;

bool Check(bool condition, const std::string& message) {
    if (!condition)
        std::cerr << "RAW HDR validation failed: " << message << std::endl;
    return condition;
}

std::filesystem::path TemporaryDirectory(const char* label) {
    static std::atomic<std::uint64_t> counter { 0u };
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        (std::string("stack-hdr-") + label + "-" + std::to_string(stamp) +
         "-" + std::to_string(counter.fetch_add(1u)));
}

struct TemporaryCleanup {
    std::filesystem::path path;
    ~TemporaryCleanup() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

double HdrScene(double x, double y) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    if (ix >= 88 && ix < 144 && iy >= 72 && iy < 136) return 2.6;
    if (ix >= 34 && ix < 62 && iy >= 176 && iy < 212) return -0.00045;
    // Keep the registration texture continuous. A per-pixel hash has no
    // meaningful fractional translation and would turn this into a nearest-
    // integer fixture instead of a subpixel-registration test.
    return 0.095 +
        0.034 * std::sin(0.031 * x + 0.023 * y) +
        0.026 * std::sin(0.047 * x - 0.037 * y) +
        0.018 * std::cos(0.019 * x + 0.053 * y);
}

double WindowBracketScene(double x, double y) {
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    if (ix >= 104 && ix < 136 && iy >= 92 && iy < 124) return 2.6;
    const double texture = 0.08 * std::sin(0.071 * x + 0.043 * y) +
        0.05 * std::cos(0.037 * x - 0.061 * y);
    if (ix < kHdrWidth / 2)
        return 0.020 * (1.0 + texture);
    return 0.56 * (1.0 + texture);
}

Raw::RawImageData MakeHdrRaw(
    char identity,
    Raw::CfaPattern cfa,
    double actualExposure,
    double metadataExposure,
    double translationX = 0.0,
    double translationY = 0.0,
    double iso = 100.0,
    const std::function<double(double, double)>& scene = {}) {
    Raw::RawImageData raw;
    raw.metadata.sourceContentSha256 = std::string(64u, identity);
    raw.metadata.sourceByteSize =
        static_cast<std::uint64_t>(kHdrWidth) * kHdrHeight * sizeof(std::uint16_t);
    raw.metadata.sourcePath = std::string(1u, identity) + ".dng";
    raw.metadata.cameraMake = "Stack Validation Camera";
    raw.metadata.cameraModel = "Synthetic HDR Bayer";
    raw.metadata.dngUniqueCameraModel = "Stack Synthetic HDR Bayer";
    raw.metadata.rawWidth = kHdrWidth;
    raw.metadata.rawHeight = kHdrHeight;
    raw.metadata.visibleWidth = kHdrWidth;
    raw.metadata.visibleHeight = kHdrHeight;
    raw.metadata.orientation = 1;
    raw.metadata.bitDepth = 16;
    raw.metadata.cfaPattern = cfa;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.mosaiced = true;
    raw.metadata.isDng = true;
    raw.metadata.blackLevel = 64.0f;
    raw.metadata.perChannelBlack.fill(64.0f);
    raw.metadata.whiteLevel = 65000.0f;
    raw.metadata.exposureTimeSeconds = static_cast<float>(
        metadataExposure * 16.0 / iso);
    raw.metadata.isoSpeed = static_cast<float>(iso);
    raw.metadata.apertureFNumber = 4.0f;
    raw.metadata.hasExposureTime = true;
    raw.metadata.hasIsoSpeed = true;
    raw.metadata.hasApertureFNumber = true;
    raw.metadata.hasDngNoiseProfile = true;
    raw.metadata.dngNoiseProfile = {
        { 1.0e-8, 1.0e-10 },
        { 1.0e-8, 1.0e-10 },
        { 1.0e-8, 1.0e-10 }
    };
    raw.rawBuffer.resize(static_cast<std::size_t>(kHdrWidth) * kHdrHeight);
    constexpr double span = 65000.0 - 64.0;
    for (int y = 0; y < kHdrHeight; ++y) {
        for (int x = 0; x < kHdrWidth; ++x) {
            const double sampleX = static_cast<double>(x) - translationX;
            const double sampleY = static_cast<double>(y) - translationY;
            const double sceneSignal = scene
                ? scene(sampleX, sampleY)
                : HdrScene(sampleX, sampleY);
            const double normalized = std::clamp(
                sceneSignal * actualExposure,
                -64.0 / span,
                1.0);
            raw.rawBuffer[static_cast<std::size_t>(y) * kHdrWidth + x] =
                static_cast<std::uint16_t>(std::clamp(
                    std::llround(64.0 + span * normalized), 0ll, 65000ll));
        }
    }
    return raw;
}

void AttachValidationGainMap(Raw::RawImageData& raw) {
    Raw::DngGainMapOpcode map;
    map.top = 0;
    map.left = 0;
    map.bottom = kHdrHeight;
    map.right = kHdrWidth;
    map.plane = 0;
    map.planes = 1;
    map.rowPitch = 1;
    map.colPitch = 1;
    map.mapPointsV = 2;
    map.mapPointsH = 2;
    map.mapPlanes = 1;
    map.mapSpacingV = 1.0;
    map.mapSpacingH = 1.0;
    map.mapOriginV = 0.0;
    map.mapOriginH = 0.0;
    map.gains = { 1.0f, 2.0f, 3.0f, 4.25f };
    raw.metadata.dngGainMaps.push_back(std::move(map));
    raw.metadata.dngGainMapCount = 1;
    raw.metadata.dngOpcodeCount[1] = 1;
    raw.metadata.dngAppliedOpcodeCountByList[1] = 1;
}

Raw::Hdr::FrameInput HdrInput(const char* path, char identity) {
    Raw::Hdr::FrameInput input;
    input.stableFrameId = std::string("hdr-") + identity;
    input.sourcePath = path;
    input.expectedSourceSha256 = std::string(64u, identity);
    input.expectedSourceByteLength =
        static_cast<std::uint64_t>(kHdrWidth) * kHdrHeight * sizeof(std::uint16_t);
    return input;
}

std::string IndexedIdentity(std::size_t index) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string identity(64u, '0');
    identity[0] = hex[(index >> 4u) & 0x0fu];
    identity[1] = hex[index & 0x0fu];
    for (std::size_t i = 2u; i < identity.size(); ++i)
        identity[i] = hex[(index * 7u + i * 3u) & 0x0fu];
    return identity;
}

Raw::Hdr::FrameInput IndexedHdrInput(
    const std::string& path,
    std::size_t index) {
    Raw::Hdr::FrameInput input;
    input.stableFrameId = "hdr-" + std::to_string(index);
    input.sourcePath = path;
    input.expectedSourceSha256 = IndexedIdentity(index);
    input.expectedSourceByteLength =
        static_cast<std::uint64_t>(kHdrWidth) * kHdrHeight * sizeof(std::uint16_t);
    return input;
}

Raw::Hdr::Services HdrServices(
    std::unordered_map<std::string, Raw::RawImageData> frames) {
    Raw::Hdr::Services services;
    services.loadRawFrame = [frames = std::move(frames)](
        const std::filesystem::path& path,
        Raw::RawImageData& output,
        const std::function<bool()>& shouldCancel,
        std::string& error) {
        if (shouldCancel && shouldCancel()) {
            error = "Synthetic HDR load canceled.";
            return false;
        }
        const auto found = frames.find(path.generic_u8string());
        if (found == frames.end()) {
            error = "Synthetic HDR frame not found.";
            return false;
        }
        output = found->second;
        error.clear();
        return true;
    };
    return services;
}

Raw::Hdr::Request HdrRequest(
    const std::filesystem::path& directory,
    Raw::Hdr::AlignmentMode alignment = Raw::Hdr::AlignmentMode::Identity) {
    Raw::Hdr::Request request;
    request.frames = {
        HdrInput("short.dng", 'a'),
        HdrInput("anchor.dng", 'b'),
        HdrInput("long.dng", 'c')
    };
    request.geometricReferenceFrameIndex = 0;
    request.radiometricAnchorFrameIndex = 1;
    request.parameters.alignmentMode = alignment;
    request.parameters.tileRawPixels = 64;
    request.workingDirectory = directory;
    request.inputRevision = 7;
    request.memoryBudgetBytes = 512ull * 1024ull * 1024ull;
    return request;
}

void SetNormalizedSample(
    Raw::RawImageData& raw,
    int x,
    int y,
    double normalized);

std::unordered_map<std::string, Raw::RawImageData> ExactBracket(
    Raw::CfaPattern cfa,
    double middleTranslationX = 0.0,
    double middleTranslationY = 0.0,
    double longMetadataExposure = 4.0,
    double longIso = 100.0) {
    std::unordered_map<std::string, Raw::RawImageData> frames;
    frames.emplace("short.dng", MakeHdrRaw('a', cfa, 0.25, 0.25));
    frames.emplace("anchor.dng", MakeHdrRaw(
        'b', cfa, 1.0, 1.0, middleTranslationX, middleTranslationY));
    frames.emplace("long.dng", MakeHdrRaw(
        'c', cfa, 4.0, longMetadataExposure, 0.0, 0.0, longIso));
    return frames;
}

std::unordered_map<std::string, Raw::RawImageData> AllRejectedPixelBracket(
    Raw::CfaPattern cfa) {
    auto frames = ExactBracket(cfa);
    // Saturate the reference sample and every same-CFA interpolation tap in
    // every exposure. The processor must publish an explicitly flagged exact
    // policy fallback here, never a NaN hole that can blank RAW development.
    for (auto& [path, frame] : frames) {
        (void)path;
        for (int y : { 200, 202 }) {
            for (int x : { 200, 202 })
                SetNormalizedSample(frame, x, y, 1.0);
        }
    }
    return frames;
}

std::unordered_map<std::string, Raw::RawImageData> WindowOverlapBracket(
    Raw::CfaPattern cfa,
    bool includeNoiseProfile = true) {
    std::unordered_map<std::string, Raw::RawImageData> frames;
    frames.emplace("short.dng", MakeHdrRaw(
        'a', cfa, 0.02, 0.02, 0.0, 0.0, 100.0, WindowBracketScene));
    frames.emplace("anchor.dng", MakeHdrRaw(
        'b', cfa, 1.0, 1.0, 0.0, 0.0, 100.0, WindowBracketScene));
    frames.emplace("long.dng", MakeHdrRaw(
        'c', cfa, 23.0, 23.0, 0.0, 0.0, 100.0, WindowBracketScene));
    if (!includeNoiseProfile) {
        for (auto& item : frames) {
            item.second.metadata.hasDngNoiseProfile = false;
            item.second.metadata.dngNoiseProfile.clear();
        }
    }
    return frames;
}

std::unordered_map<std::string, Raw::RawImageData> GainMapBracket() {
    std::unordered_map<std::string, Raw::RawImageData> frames;
    Raw::RawImageData shortFrame = MakeHdrRaw(
        'a', Raw::CfaPattern::RGGB, 0.25, 0.25);
    Raw::RawImageData longFrame = MakeHdrRaw(
        'b', Raw::CfaPattern::RGGB, 1.0, 1.0);
    AttachValidationGainMap(shortFrame);
    AttachValidationGainMap(longFrame);
    frames.emplace("short.dng", std::move(shortFrame));
    frames.emplace("anchor.dng", std::move(longFrame));
    return frames;
}

std::unordered_map<std::string, Raw::RawImageData> TwentyFrameBracket(
    Raw::Hdr::Request& request,
    Raw::CfaPattern cfa) {
    request.frames.clear();
    request.frames.reserve(Raw::Hdr::kMaximumFrameCount);
    std::unordered_map<std::string, Raw::RawImageData> frames;
    for (std::size_t i = 0; i < Raw::Hdr::kMaximumFrameCount; ++i) {
        const double exposure = std::exp2(
            0.2 * (static_cast<double>(i) - 10.0));
        const std::string path = "frame-" + std::to_string(i) + ".dng";
        Raw::RawImageData raw = MakeHdrRaw('a', cfa, exposure, exposure);
        raw.metadata.sourceContentSha256 = IndexedIdentity(i);
        raw.metadata.sourcePath = path;
        frames.emplace(path, std::move(raw));
        request.frames.push_back(IndexedHdrInput(path, i));
    }
    request.geometricReferenceFrameIndex = 0;
    request.radiometricAnchorFrameIndex = 10;
    return frames;
}

void SetNormalizedSample(
    Raw::RawImageData& raw,
    int x,
    int y,
    double normalized) {
    constexpr double span = 65000.0 - 64.0;
    raw.rawBuffer[static_cast<std::size_t>(y) * kHdrWidth + x] =
        static_cast<std::uint16_t>(std::clamp(
            std::llround(64.0 + span * normalized), 0ll, 65000ll));
}

std::unordered_map<std::string, Raw::RawImageData> HighlightHoleBracket() {
    std::unordered_map<std::string, Raw::RawImageData> frames;
    Raw::RawImageData shortFrame = MakeHdrRaw(
        'a', Raw::CfaPattern::RGGB, 0.25, 0.25);
    Raw::RawImageData referenceFrame = MakeHdrRaw(
        'b', Raw::CfaPattern::RGGB, 1.0, 1.0);
    for (int y = 104; y <= 105; ++y) {
        for (int x = 110; x <= 111; ++x)
            SetNormalizedSample(referenceFrame, x, y, 0.08);
    }
    // A second disagreement in an ordinary, unclipped neighborhood must
    // retain the geometric reference's temporal appearance.
    SetNormalizedSample(referenceFrame, 40, 40, 0.005);
    frames.emplace("short.dng", std::move(shortFrame));
    frames.emplace("reference.dng", std::move(referenceFrame));
    return frames;
}

std::unordered_map<std::string, Raw::RawImageData> ColorCoherenceBracket() {
    std::unordered_map<std::string, Raw::RawImageData> frames;
    Raw::RawImageData shortFrame = MakeHdrRaw(
        'a', Raw::CfaPattern::RGGB, 0.25, 0.25);
    Raw::RawImageData referenceFrame = MakeHdrRaw(
        'b', Raw::CfaPattern::RGGB, 1.0, 1.0);

    // Make red substantially dimmer than the other three CFA sites throughout
    // the bright patch. A real colored light must not be neutralized; each CFA
    // color therefore has to be judged against its own spatial neighborhood.
    for (int y = 72; y < 136; y += 2) {
        for (int x = 88; x < 144; x += 2) {
            SetNormalizedSample(shortFrame, x, y, 0.70 * 0.25);
            SetNormalizedSample(referenceFrame, x, y, 0.70);
        }
    }

    // One false dark red sample in the long exposure. Its valid value exists
    // in the short exposure and should be selected without interpolation.
    SetNormalizedSample(referenceFrame, 110, 104, 0.08);

    // A genuine tiny dark object appears consistently in both exposures. All
    // four CFA sites must stay dark even though the neighborhood is bright.
    for (int y = 120; y <= 121; ++y) {
        for (int x = 120; x <= 121; ++x) {
            SetNormalizedSample(shortFrame, x, y, 0.08 * 0.25);
            SetNormalizedSample(referenceFrame, x, y, 0.08);
        }
    }
    frames.emplace("short.dng", std::move(shortFrame));
    frames.emplace("reference.dng", std::move(referenceFrame));
    return frames;
}

bool ValidatePublishedShape(const Raw::Hdr::Result& result) {
    const std::size_t pixels = static_cast<std::size_t>(kHdrWidth) * kHdrHeight;
    return Check(result.status == Raw::Hdr::ProcessingStatus::Published,
                 result.message) &&
        Check(result.virtualAnchorMosaic.size() == pixels,
              "virtual-anchor mosaic size changed") &&
        Check(result.varianceProxy.size() == pixels,
              "variance sidecar size changed") &&
        Check(result.mergeConfidence.size() == pixels,
              "confidence sidecar size changed") &&
        Check(result.ownerFrame.size() == pixels && result.flags.size() == pixels,
              "owner/fallback sidecar size changed");
}

bool ValidateRadianceAccuracy(const Raw::Hdr::Result& result) {
    std::vector<double> errorsEv;
    double biasSum = 0.0;
    double expectedSum = 0.0;
    std::size_t count = 0;
    bool preservedOverrange = false;
    bool preservedSigned = false;
    for (int y = 4; y + 4 < kHdrHeight; ++y) {
        for (int x = 4; x + 4 < kHdrWidth; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * kHdrWidth + x;
            const double expected = HdrScene(x, y);
            const double actual = result.virtualAnchorMosaic[index];
            if (!std::isfinite(actual)) continue;
            preservedOverrange = preservedOverrange || actual > 1.5;
            preservedSigned = preservedSigned || actual < 0.0;
            if (expected > 0.01) {
                biasSum += actual - expected;
                expectedSum += expected;
                errorsEv.push_back(std::abs(std::log2(
                    std::max(1.0e-12, actual) / expected)));
                ++count;
            }
        }
    }
    std::sort(errorsEv.begin(), errorsEv.end());
    const double p99 = errorsEv.empty() ? 99.0 : errorsEv[std::min(
        errorsEv.size() - 1u,
        static_cast<std::size_t>(errorsEv.size() * 0.99))];
    const double relativeBias = std::abs(biasSum) / std::max(1.0e-12, expectedSum);
    return Check(count > 1000u, "too few finite HDR validation samples") &&
        Check(relativeBias < 0.002,
              "mean radiance bias exceeds 0.2% (actual " +
                  std::to_string(relativeBias * 100.0) + "%)") &&
        Check(p99 < 0.03,
              "99th-percentile exposure handoff error exceeds 0.03 EV (actual " +
                  std::to_string(p99) + " EV)") &&
        Check(preservedOverrange, "above-white HDR values were clipped") &&
        Check(preservedSigned, "signed shadow samples were clipped");
}

bool ValidateFailurePublicationSafety() {
    using Raw::MultiFrame::ActivePublicationContext;
    using Raw::MultiFrame::EvaluatePublicationFence;
    using Raw::MultiFrame::PublicationAttemptStamp;
    using Raw::MultiFrame::PublicationFenceResult;

    const std::filesystem::path temporary =
        TemporaryDirectory("publication-safety");
    TemporaryCleanup cleanup { temporary };
    const auto bracket = ExactBracket(Raw::CfaPattern::RGGB);

    Raw::Hdr::Request baselineRequest = HdrRequest(temporary / "baseline");
    Raw::Hdr::Result baseline = Raw::Hdr::ProcessBurst(
        baselineRequest, HdrServices(bracket));
    bool ok = ValidatePublishedShape(baseline);
    if (baseline.status != Raw::Hdr::ProcessingStatus::Published) return false;

    auto active = std::make_shared<const Raw::Hdr::Result>(baseline);
    const Raw::Hdr::Result* const originalActive = active.get();
    const auto considerForAdoption = [&active](
        Raw::Hdr::Result candidate,
        const PublicationAttemptStamp& attempt,
        const ActivePublicationContext& context) {
        if (candidate.status != Raw::Hdr::ProcessingStatus::Published)
            return false;
        if (EvaluatePublicationFence(attempt, context) !=
            PublicationFenceResult::Current) {
            return false;
        }
        active = std::make_shared<const Raw::Hdr::Result>(
            std::move(candidate));
        return true;
    };

    bool cancelRequested = false;
    Raw::Hdr::Request canceledRequest = HdrRequest(temporary / "canceled");
    canceledRequest.inputRevision = 8u;
    canceledRequest.reportProgress = [&cancelRequested](
        const Raw::Hdr::Progress& progress) {
        if (progress.stage == Raw::Hdr::ProcessingStage::Fusing)
            cancelRequested = true;
    };
    canceledRequest.shouldCancel = [&cancelRequested]() {
        return cancelRequested;
    };
    Raw::Hdr::Result canceled = Raw::Hdr::ProcessBurst(
        canceledRequest, HdrServices(bracket));
    ok &= Check(
        cancelRequested &&
            canceled.status == Raw::Hdr::ProcessingStatus::Canceled,
        "mid-fusion cancellation produced an adoptable HDR result");
    ok &= Check(
        !considerForAdoption(
            std::move(canceled),
            { 21u, "project-a", 8u },
            { 21u, true, "project-a", 8u, true }) &&
            active.get() == originalActive,
        "a canceled HDR candidate replaced the last valid result");

    Raw::Hdr::Request constrainedRequest = HdrRequest(
        temporary / "memory-budget");
    constrainedRequest.inputRevision = 9u;
    constrainedRequest.memoryBudgetBytes = 1u;
    Raw::Hdr::Result constrained = Raw::Hdr::ProcessBurst(
        constrainedRequest, HdrServices(bracket));
    ok &= Check(
        constrained.status == Raw::Hdr::ProcessingStatus::Failed &&
            constrained.message.find("memory budget") != std::string::npos,
        "the HDR memory guard did not fail before publication");
    ok &= Check(
        !considerForAdoption(
            std::move(constrained),
            { 22u, "project-a", 9u },
            { 22u, true, "project-a", 9u, true }) &&
            active.get() == originalActive,
        "a memory-budget failure replaced the last valid HDR result");

    Raw::Hdr::Request cacheFailureRequest = HdrRequest(
        temporary / "cache-failure");
    cacheFailureRequest.inputRevision = 10u;
    std::error_code filesystemError;
    std::filesystem::create_directories(
        cacheFailureRequest.workingDirectory, filesystemError);
    {
        std::ofstream blockingFile(
            cacheFailureRequest.workingDirectory / "results",
            std::ios::binary | std::ios::trunc);
        blockingFile.put('x');
    }
    Raw::Hdr::Result cacheFailure = Raw::Hdr::ProcessBurst(
        cacheFailureRequest, HdrServices(bracket));
    ok &= Check(
        cacheFailure.status == Raw::Hdr::ProcessingStatus::Failed &&
            cacheFailure.message.find("cache") != std::string::npos,
        "a failed HDR cache publication remained marked published");
    ok &= Check(
        !considerForAdoption(
            std::move(cacheFailure),
            { 23u, "project-a", 10u },
            { 23u, true, "project-a", 10u, true }) &&
            active.get() == originalActive,
        "a cache-publication failure replaced the last valid HDR result");

    Raw::Hdr::Result staleCandidate = baseline;
    ok &= Check(
        !considerForAdoption(
            std::move(staleCandidate),
            { 24u, "project-a", baselineRequest.inputRevision },
            { 24u, true, "project-a",
                baselineRequest.inputRevision + 1u, true }) &&
            active.get() == originalActive,
        "a stale HDR completion replaced the last valid result");

    const std::filesystem::path cachePath =
        baselineRequest.workingDirectory / "results" /
        (baseline.diagnostics.cacheKey + ".hdr-cache");
    {
        std::fstream cache(
            cachePath,
            std::ios::in | std::ios::out | std::ios::binary);
        ok &= Check(static_cast<bool>(cache),
            "the HDR publication-safety fixture could not open its cache");
        if (cache) cache.put('X');
    }
    Raw::Hdr::Result cacheTarget = baseline;
    std::string cacheError;
    const bool corruptRead = Raw::Hdr::ReadResultCache(
        baselineRequest.workingDirectory / "results",
        baseline.diagnostics.cacheKey,
        cacheTarget,
        &cacheError);
    ok &= Check(
        !corruptRead &&
            cacheTarget.status == baseline.status &&
            cacheTarget.diagnostics.cacheKey == baseline.diagnostics.cacheKey &&
            cacheTarget.virtualAnchorMosaic == baseline.virtualAnchorMosaic,
        "a corrupt HDR cache partially replaced its verified decode target");
    ok &= Check(active.get() == originalActive,
        "corrupt-cache handling replaced the active HDR result");
    return ok;
}

} // namespace

bool ValidateHdrContractsAndOverrange() {
    bool ok = true;
    ok &= Check(Raw::Hdr::kAlgorithmVersion == 4u,
        "HDR algorithm version did not advance for neutral display and graph calibration");
    ok &= Check(Raw::Hdr::kProcessorContractVersion == 2u,
        "HDR processor diagnostics contract did not advance");
    ok &= Check(Raw::Hdr::kMaximumFrameCount == 20u,
        "HDR frame-count headroom changed from twenty");
    ok &= Check(std::string(Stack::Project::MultiFrameOperationIntentName(
        Stack::Project::MultiFrameOperationIntent::RawBurstHdr)) == "raw-burst-hdr",
        "stable HDR operation serialization changed");
    Raw::Hdr::Parameters parameters;
    const nlohmann::json serialized = Raw::Hdr::SerializeParameters(parameters);
    Raw::Hdr::Parameters decoded;
    std::string error;
    ok &= Check(Raw::Hdr::DeserializeParameters(serialized, decoded, &error), error);
    ok &= Check(decoded.alignmentMode == Raw::Hdr::AlignmentMode::AutoTranslation,
        "HDR settings round-trip changed alignment mode");
    ok &= Check(Raw::NormalizedMosaicInputContractName(
        Raw::NormalizedMosaicInputContract::HdrVirtualAnchorPreGain) ==
            std::string("hdr-virtual-anchor-pre-gain"),
        "normalized mosaic HDR contract changed");

    const std::filesystem::path temporary = TemporaryDirectory("contracts");
    TemporaryCleanup cleanup { temporary };
    Raw::Hdr::Request request = HdrRequest(temporary);
    Raw::Hdr::Result result = Raw::Hdr::ProcessBurst(
        request, HdrServices(ExactBracket(Raw::CfaPattern::RGGB)));
    ok &= ValidatePublishedShape(result);
    ok &= ValidateRadianceAccuracy(result);
    Raw::Hdr::Result restored;
    ok &= Check(Raw::Hdr::ReadResultCache(
        temporary / "results", result.diagnostics.cacheKey, restored, &error), error);
    ok &= Check(restored.virtualAnchorMosaic == result.virtualAnchorMosaic,
        "verified HDR cache did not restore deterministic pixels");
    ok &= Check(result.diagnostics.colorCoherentRepairPixelCount == 0u,
        "exact static HDR data triggered a color-coherence repair");
    ok &= Check(restored.diagnostics.colorCoherentRepairPixelCount ==
            result.diagnostics.colorCoherentRepairPixelCount,
        "color-coherence diagnostics did not survive cache restoration");
    std::cout << "[hdr-v2] contracts and signed/overrange transport "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

bool ValidateHdrCalibrationAndMixedIso() {
    const std::filesystem::path temporary = TemporaryDirectory("mixed-iso");
    TemporaryCleanup cleanup { temporary };
    Raw::Hdr::Request request = HdrRequest(temporary);
    Raw::Hdr::Result result = Raw::Hdr::ProcessBurst(
        request,
        HdrServices(ExactBracket(
            Raw::CfaPattern::GRBG, 0.0, 0.0, 8.0, 800.0)));
    bool ok = ValidatePublishedShape(result);
    const auto frame = std::find_if(
        result.diagnostics.frames.begin(), result.diagnostics.frames.end(),
        [](const Raw::Hdr::FrameDiagnostic& item) {
            return item.stableFrameId == "hdr-c";
        });
    ok &= Check(frame != result.diagnostics.frames.end(),
        "mixed-ISO frame diagnostics are missing");
    if (frame != result.diagnostics.frames.end()) {
        ok &= Check(frame->lowConfidence,
            "severe metadata/image exposure disagreement was not downweighted");
        ok &= Check(std::abs(std::log2(frame->fittedExposureRelativeToAnchor / 4.0)) < 0.03,
            "image fitting did not remain authoritative over ISO metadata");
    }
    ok &= Check(!result.diagnostics.warnings.empty(),
        "mixed-ISO calibration disagreement did not produce a warning");

    const std::filesystem::path overlapDirectory =
        TemporaryDirectory("adjacent-overlap");
    TemporaryCleanup overlapCleanup { overlapDirectory };
    Raw::Hdr::Request overlapRequest = HdrRequest(overlapDirectory);
    Raw::Hdr::Result overlap = Raw::Hdr::ProcessBurst(
        overlapRequest,
        HdrServices(WindowOverlapBracket(Raw::CfaPattern::GBRG)));
    const bool overlapPublished = ValidatePublishedShape(overlap);
    ok &= overlapPublished;
    if (overlapPublished) {
        const auto longFrame = std::find_if(
            overlap.diagnostics.frames.begin(), overlap.diagnostics.frames.end(),
            [](const Raw::Hdr::FrameDiagnostic& item) {
                return item.stableFrameId == "hdr-c";
            });
        ok &= Check(longFrame != overlap.diagnostics.frames.end(),
            "adjacent-overlap long-frame diagnostics are missing");
        if (longFrame != overlap.diagnostics.frames.end()) {
            ok &= Check(std::abs(std::log2(
                    longFrame->fittedExposureRelativeToAnchor / 23.0)) < 0.15,
                "the long exposure was not recovered through its middle-exposure overlap");
            ok &= Check(longFrame->shadowContribution > 0.80,
                "the verified long exposure did not dominate clean shadow measurements (actual " +
                    std::to_string(longFrame->shadowContribution * 100.0) +
                    "%, scale " +
                    std::to_string(longFrame->fittedExposureRelativeToAnchor) +
                    ", uncertainty " +
                    std::to_string(longFrame->fittedExposureUncertaintyEv) +
                    " EV, noise inflation " +
                    std::to_string(longFrame->noiseVarianceInflation) +
                    (longFrame->lowConfidence ? ", low confidence)" : ")"));
            ok &= Check(longFrame->exposureFitPath.size() >= 2u,
                "the long exposure has no recorded path to the radiometric anchor");
        }
        const auto shortFrame = std::find_if(
            overlap.diagnostics.frames.begin(), overlap.diagnostics.frames.end(),
            [](const Raw::Hdr::FrameDiagnostic& item) {
                return item.stableFrameId == "hdr-a";
            });
        if (shortFrame != overlap.diagnostics.frames.end()) {
            ok &= Check(shortFrame->highlightContribution > 0.80,
                "the short exposure did not retain clipped highlight ownership");
        }
        bool rejectedExtremePair = false;
        bool everyVerifiedEdgeUsesAllSites = true;
        for (const Raw::Hdr::ExposureFitEdgeDiagnostic& edge :
                overlap.diagnostics.exposureFitEdges) {
            if (edge.fromFrameId == "hdr-a" && edge.toFrameId == "hdr-c")
                rejectedExtremePair = !edge.verified;
            if (edge.verified) {
                everyVerifiedEdgeUsesAllSites = everyVerifiedEdgeUsesAllSites &&
                    std::all_of(
                        edge.siteSampleCounts.begin(), edge.siteSampleCounts.end(),
                        [](std::uint64_t count) { return count > 0u; });
            }
        }
        ok &= Check(rejectedExtremePair,
            "a non-overlapping shortest/longest pair was incorrectly certified");
        ok &= Check(everyVerifiedEdgeUsesAllSites,
            "an exposure-fit edge omitted one or more CFA phases");
    }

    const std::filesystem::path estimatedNoiseDirectory =
        TemporaryDirectory("estimated-noise");
    TemporaryCleanup estimatedNoiseCleanup { estimatedNoiseDirectory };
    Raw::Hdr::Result estimatedNoise = Raw::Hdr::ProcessBurst(
        HdrRequest(estimatedNoiseDirectory),
        HdrServices(WindowOverlapBracket(Raw::CfaPattern::RGGB, false)));
    const bool estimatedNoisePublished = ValidatePublishedShape(estimatedNoise);
    ok &= estimatedNoisePublished;
    if (estimatedNoisePublished) {
        for (const Raw::Hdr::FrameDiagnostic& diagnostic :
                estimatedNoise.diagnostics.frames) {
            ok &= Check(!diagnostic.noiseModelQuality.empty() &&
                    !diagnostic.noiseModelSource.empty(),
                "missing-profile noise provenance was not surfaced");
            ok &= Check(diagnostic.noiseVarianceInflation >= 1.0,
                "burst residual validation made a noise model more optimistic");
        }
    }

    const std::filesystem::path gainDirectory =
        TemporaryDirectory("gain-variance");
    TemporaryCleanup gainCleanup { gainDirectory };
    Raw::Hdr::Request gainRequest = HdrRequest(gainDirectory);
    gainRequest.frames = {
        HdrInput("short.dng", 'a'), HdrInput("anchor.dng", 'b') };
    gainRequest.geometricReferenceFrameIndex = 0;
    gainRequest.radiometricAnchorFrameIndex = 1;
    Raw::Hdr::Result gainResult = Raw::Hdr::ProcessBurst(
        gainRequest, HdrServices(GainMapBracket()));
    const bool gainPublished = ValidatePublishedShape(gainResult);
    ok &= gainPublished;
    if (gainPublished) {
        std::uint64_t inspected = 0u;
        std::uint64_t longOwned = 0u;
        for (int y = 144; y < 232; ++y) {
            for (int x = 144; x < 232; ++x) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * kHdrWidth + x;
                if (gainResult.validityMask[index] == 0u) continue;
                ++inspected;
                if (gainResult.ownerFrame[index] == 1u) ++longOwned;
            }
        }
        ok &= Check(inspected > 1000u &&
                static_cast<double>(longOwned) / inspected > 0.95,
            "known gain propagation biased high-gain regions toward the exact reference");
    }
    std::cout << "[hdr-v2] calibration and mixed ISO "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

bool ValidateHdrTranslationAndFusion() {
    const std::filesystem::path temporary = TemporaryDirectory("translation");
    TemporaryCleanup cleanup { temporary };
    Raw::Hdr::Request request = HdrRequest(
        temporary, Raw::Hdr::AlignmentMode::AutoTranslation);
    Raw::Hdr::Result result = Raw::Hdr::ProcessBurst(
        request,
        HdrServices(ExactBracket(Raw::CfaPattern::BGGR, 1.25, -0.75)));
    bool ok = ValidatePublishedShape(result);
    const auto frame = std::find_if(
        result.diagnostics.frames.begin(), result.diagnostics.frames.end(),
        [](const Raw::Hdr::FrameDiagnostic& item) {
            return item.stableFrameId == "hdr-b";
        });
    ok &= Check(frame != result.diagnostics.frames.end(),
        "translated frame diagnostics are missing");
    if (frame != result.diagnostics.frames.end()) {
        const double translationError = std::hypot(
            frame->translationRawX - 1.25,
            frame->translationRawY + 0.75);
        ok &= Check(translationError < 0.10,
            "translation median error exceeds 0.10 RAW pixel (estimated " +
                std::to_string(frame->translationRawX) + ", " +
                std::to_string(frame->translationRawY) + ")");
    }
    ok &= ValidateRadianceAccuracy(result);

    const std::filesystem::path handoffDirectory =
        TemporaryDirectory("highlight-hole");
    TemporaryCleanup handoffCleanup { handoffDirectory };
    Raw::Hdr::Request handoffRequest = HdrRequest(handoffDirectory);
    handoffRequest.frames = {
        HdrInput("short.dng", 'a'),
        HdrInput("reference.dng", 'b')
    };
    handoffRequest.geometricReferenceFrameIndex = 1;
    handoffRequest.radiometricAnchorFrameIndex = 1;
    Raw::Hdr::Result handoff = Raw::Hdr::ProcessBurst(
        handoffRequest, HdrServices(HighlightHoleBracket()));
    const bool handoffPublished = ValidatePublishedShape(handoff);
    ok &= handoffPublished;
    if (handoffPublished) {
        for (int y = 104; y <= 105; ++y) {
            for (int x = 110; x <= 111; ++x) {
                const std::size_t index = static_cast<std::size_t>(y) * kHdrWidth + x;
                ok &= Check(handoff.virtualAnchorMosaic[index] > 2.0f,
                    "a dark reference anomaly punched through a clipped highlight");
                ok &= Check((handoff.flags[index] &
                    Raw::Hdr::ResultFlagHighlightSafeHandoff) != 0u,
                    "highlight-safe handoff was not recorded");
                ok &= Check(handoff.ownerFrame[index] == 0u,
                    "the valid short exposure did not own the highlight-hole handoff");
            }
        }
        const std::size_t ordinaryMotionIndex =
            static_cast<std::size_t>(40) * kHdrWidth + 40u;
        ok &= Check((handoff.flags[ordinaryMotionIndex] &
            Raw::Hdr::ResultFlagReferenceFallback) != 0u,
            "ordinary two-frame disagreement stopped preserving the reference");
        ok &= Check(handoff.diagnostics.highlightSafeHandoffPixelCount >= 4u,
            "highlight-safe handoff diagnostics were not accumulated");
    }

    const std::filesystem::path colorDirectory =
        TemporaryDirectory("color-coherence");
    TemporaryCleanup colorCleanup { colorDirectory };
    Raw::Hdr::Request colorRequest = HdrRequest(colorDirectory);
    colorRequest.frames = {
        HdrInput("short.dng", 'a'),
        HdrInput("reference.dng", 'b')
    };
    colorRequest.geometricReferenceFrameIndex = 1;
    colorRequest.radiometricAnchorFrameIndex = 1;
    Raw::Hdr::Result colorResult = Raw::Hdr::ProcessBurst(
        colorRequest, HdrServices(ColorCoherenceBracket()));
    const bool colorPublished = ValidatePublishedShape(colorResult);
    ok &= colorPublished;
    if (colorPublished) {
        const std::size_t repairedIndex =
            static_cast<std::size_t>(104) * kHdrWidth + 110u;
        ok &= Check(colorResult.virtualAnchorMosaic[repairedIndex] > 0.55f &&
                colorResult.virtualAnchorMosaic[repairedIndex] < 0.85f,
            "an isolated false black CFA sample was not replaced by source radiance");
        ok &= Check((colorResult.flags[repairedIndex] &
                Raw::Hdr::ResultFlagColorCoherentRepair) != 0u,
            "the color-coherent source repair was not recorded");
        ok &= Check(colorResult.ownerFrame[repairedIndex] == 0u,
            "the valid short exposure did not own the color-coherent repair");
        for (int y = 120; y <= 121; ++y) {
            for (int x = 120; x <= 121; ++x) {
                const std::size_t index =
                    static_cast<std::size_t>(y) * kHdrWidth + x;
                ok &= Check(colorResult.virtualAnchorMosaic[index] < 0.20f,
                    "a real dark object was brightened by spatial fallback");
                ok &= Check((colorResult.flags[index] &
                        Raw::Hdr::ResultFlagColorCoherentRepair) == 0u,
                    "a real dark object was marked as a color-coherent repair");
            }
        }
        ok &= Check(colorResult.diagnostics.colorCoherentRepairPixelCount >= 1u,
            "color-coherent repair diagnostics were not accumulated");
    }
    std::cout << "[hdr-v2] translation and fusion "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

bool ValidateHdrProjectFoundation() {
    bool ok = true;
    const nlohmann::json defaults = Stack::Project::MakeDefaultHdrOperationSettings();
    ok &= Check(defaults.value("algorithmId", std::string()) == Raw::Hdr::kAlgorithmId,
        "project HDR algorithm identity changed");
    ok &= Check(defaults.value("sharedPostHdrRecipe", nlohmann::json::object()).is_object(),
        "shared post-HDR recipe is missing");
    Stack::Project::RawProjectSnapshot snapshot;
    snapshot.projectId = "hdr-v1-project";
    snapshot.projectName = "HDR Validation";
    snapshot.hdrInputRevision = 12;
    nlohmann::json oldManifest = Stack::Project::SerializeRawProjectSnapshot(snapshot);
    oldManifest["schemaVersion"] = 3;
    oldManifest.erase("hdrInputRevision");
    Stack::Project::RawProjectSnapshot migrated;
    std::string error;
    ok &= Check(!Stack::Project::DeserializeRawProjectSnapshot(
        oldManifest, migrated, &error),
        "obsolete schema-3 projects should remain unsupported");

    Stack::Project::RawProjectSnapshot currentRoundTrip;
    error.clear();
    ok &= Check(Stack::Project::DeserializeRawProjectSnapshot(
        Stack::Project::SerializeRawProjectSnapshot(snapshot),
        currentRoundTrip,
        &error), error);
    ok &= Check(
        currentRoundTrip.schemaVersion ==
                Stack::Project::kRawProjectSourceSetSchemaVersion &&
            currentRoundTrip.hdrInputRevision == snapshot.hdrInputRevision,
        "the current HDR project schema did not preserve its input revision");

    for (std::uint32_t obsoleteVersion : { 1u, 2u, 3u }) {
        Stack::Project::RawProjectSnapshot obsoleteAlgorithm = snapshot;
        Stack::Project::MultiFrameSourceSet obsoleteSet;
        obsoleteSet.sourceSetId = "obsolete-hdr-set";
        obsoleteSet.name = "Obsolete HDR";
        obsoleteSet.inputFamily = Stack::Project::MultiFrameInputFamily::Raw;
        obsoleteSet.operationIntent =
            Stack::Project::MultiFrameOperationIntent::RawBurstHdr;
        obsoleteSet.operationSchemaVersion = Stack::Project::kHdrOperationSchemaVersion;
        obsoleteSet.settings = Stack::Project::MakeDefaultHdrOperationSettings();
        obsoleteSet.settings["algorithmVersion"] = obsoleteVersion;
        obsoleteAlgorithm.sourceSets.push_back(std::move(obsoleteSet));
        obsoleteAlgorithm.activeSourceSetId = "obsolete-hdr-set";
        Stack::Project::RawProjectSnapshot rejected;
        error.clear();
        ok &= Check(!Stack::Project::DeserializeRawProjectSnapshot(
            Stack::Project::SerializeRawProjectSnapshot(obsoleteAlgorithm),
            rejected,
            &error),
            "an obsolete HDR algorithm version was accepted");
    }

    Raw::RawMetadata referenceMetadata;
    referenceMetadata.rawWidth = referenceMetadata.visibleWidth = 128;
    referenceMetadata.rawHeight = referenceMetadata.visibleHeight = 96;
    referenceMetadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    referenceMetadata.cfaPattern = Raw::CfaPattern::GBRG;
    referenceMetadata.mosaiced = true;
    referenceMetadata.apertureFNumber = 4.0f;
    referenceMetadata.focalLengthMm = 50.0f;
    referenceMetadata.hasApertureFNumber = true;
    referenceMetadata.hasFocalLength = true;
    Raw::RawMetadata mismatched = referenceMetadata;
    mismatched.apertureFNumber = 5.6f;
    const auto reference = Stack::Project::BuildRawCaptureCompatibilitySummary(
        referenceMetadata);
    const auto candidate = Stack::Project::BuildRawCaptureCompatibilitySummary(
        mismatched);
    std::string reason;
    ok &= Check(Stack::Project::AreHdrCapturesStructurallyCompatible(
        reference, candidate, &reason, nullptr),
        "different apertures were rejected");
    Raw::RawMetadata focusMetadata = referenceMetadata;
    focusMetadata.focusDistanceMeters = 0.5f;
    focusMetadata.hasFocusDistance = true;
    Raw::RawMetadata refocusedMetadata = referenceMetadata;
    refocusedMetadata.focusDistanceMeters = 0.8f;
    refocusedMetadata.hasFocusDistance = true;
    std::vector<std::string> focusWarnings;
    ok &= Check(Stack::Project::AreHdrCapturesStructurallyCompatible(
        Stack::Project::BuildRawCaptureCompatibilitySummary(focusMetadata),
        Stack::Project::BuildRawCaptureCompatibilitySummary(refocusedMetadata),
        &reason,
        &focusWarnings),
        "known focus-distance mismatch was rejected");
    ok &= Check(std::any_of(
        focusWarnings.begin(),
        focusWarnings.end(),
        [](const std::string& warning) {
            return warning.find("Focus distances differ") != std::string::npos;
        }),
        "known focus-distance mismatch did not produce a warning");
    std::cout << "[hdr-v2] project foundation "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

bool ValidateHdrEndToEndProcessor() {
    bool ok = true;
    const Raw::CfaPattern patterns[] = {
        Raw::CfaPattern::RGGB, Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GRBG, Raw::CfaPattern::GBRG
    };
    std::string firstCacheKey;
    std::vector<float> firstPixels;
    std::vector<float> firstVariance;
    std::vector<float> firstConfidence;
    std::vector<float> firstEffectiveSamples;
    std::vector<float> firstHeadroom;
    std::vector<std::uint8_t> firstValidity;
    std::vector<std::uint8_t> firstOwners;
    std::vector<std::uint8_t> firstFlags;
    for (std::size_t i = 0; i < std::size(patterns); ++i) {
        const std::filesystem::path temporary = TemporaryDirectory("all-cfa");
        TemporaryCleanup cleanup { temporary };
        Raw::Hdr::Result result = Raw::Hdr::ProcessBurst(
            HdrRequest(temporary), HdrServices(ExactBracket(patterns[i])));
        ok &= ValidatePublishedShape(result);
        ok &= ValidateRadianceAccuracy(result);
        if (i == 0u) {
            firstCacheKey = result.diagnostics.cacheKey;
            firstPixels = result.virtualAnchorMosaic;
            firstVariance = result.varianceProxy;
            firstConfidence = result.mergeConfidence;
            firstEffectiveSamples = result.effectiveSampleCount;
            firstHeadroom = result.recoveredHeadroomStops;
            firstValidity = result.validityMask;
            firstOwners = result.ownerFrame;
            firstFlags = result.flags;
        }
    }
    const std::filesystem::path repeatDirectory = TemporaryDirectory("determinism");
    TemporaryCleanup repeatCleanup { repeatDirectory };
    Raw::Hdr::Request repeatRequest = HdrRequest(repeatDirectory);
    repeatRequest.workerCount = 4u;
    Raw::Hdr::Result repeat = Raw::Hdr::ProcessBurst(
        repeatRequest,
        HdrServices(ExactBracket(Raw::CfaPattern::RGGB)));
    ok &= Check(repeat.diagnostics.cacheKey == firstCacheKey,
        "identical HDR inputs/settings produced a different cache identity");
    ok &= Check(repeat.virtualAnchorMosaic == firstPixels,
        "parallel HDR fusion changed the virtual Bayer pixels");
    ok &= Check(repeat.varianceProxy == firstVariance &&
            repeat.mergeConfidence == firstConfidence &&
            repeat.effectiveSampleCount == firstEffectiveSamples &&
            repeat.recoveredHeadroomStops == firstHeadroom &&
            repeat.validityMask == firstValidity &&
            repeat.ownerFrame == firstOwners && repeat.flags == firstFlags,
        "parallel HDR fusion changed a per-pixel sidecar or ownership flag");

    const std::filesystem::path twentyDirectory =
        TemporaryDirectory("twenty-frames");
    TemporaryCleanup twentyCleanup { twentyDirectory };
    Raw::Hdr::Request twentyRequest = HdrRequest(twentyDirectory);
    auto twentyFrames = TwentyFrameBracket(
        twentyRequest, Raw::CfaPattern::RGGB);
    Raw::Hdr::Result twenty = Raw::Hdr::ProcessBurst(
        twentyRequest, HdrServices(std::move(twentyFrames)));
    const bool twentyPublished = ValidatePublishedShape(twenty);
    ok &= twentyPublished;
    if (twentyPublished) {
        ok &= ValidateRadianceAccuracy(twenty);
        ok &= Check(twenty.diagnostics.frames.size() == Raw::Hdr::kMaximumFrameCount,
            "the processor did not retain diagnostics for all twenty frames");
    }

    Raw::Hdr::Request tooMany = twentyRequest;
    tooMany.frames.push_back(IndexedHdrInput("frame-20.dng", 20u));
    Raw::Hdr::Result rejected = Raw::Hdr::ProcessBurst(
        tooMany, Raw::Hdr::Services {});
    ok &= Check(rejected.status == Raw::Hdr::ProcessingStatus::Failed &&
        rejected.message.find("twenty") != std::string::npos,
        "the processor did not reject a twenty-first HDR frame cleanly");
    ok &= ValidateFailurePublicationSafety();
    std::cout << "[hdr-v2] end-to-end all-CFA processor "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

bool ValidateHdrGpuFusion() {
    bool ok = true;
    if (!glfwInit())
        return Check(false, "GLFW could not initialize the HDR GPU validation context");
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(
        16, 16, "Stack HDR GPU Validation", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return Check(false, "OpenGL 4.3 is unavailable for HDR GPU validation");
    }
    glfwMakeContextCurrent(window);
    if (!LoadGLFunctions()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return Check(false, "OpenGL functions could not be loaded for HDR GPU validation");
    }

    const auto directExecutor = [](
        Raw::Hdr::OpenGlTask task,
        std::string& error) {
        return task ? task(error) : false;
    };
    struct GpuParityScenario {
        Raw::CfaPattern pattern;
        double translationX = 0.0;
        double translationY = 0.0;
        Raw::Hdr::AlignmentMode alignment = Raw::Hdr::AlignmentMode::Identity;
        bool allRejectedPixel = false;
    };
    const GpuParityScenario scenarios[] = {
        { Raw::CfaPattern::RGGB }, { Raw::CfaPattern::BGGR },
        { Raw::CfaPattern::GRBG }, { Raw::CfaPattern::GBRG },
        { Raw::CfaPattern::RGGB, 0.75, -0.50,
          Raw::Hdr::AlignmentMode::AutoTranslation, false },
        { Raw::CfaPattern::RGGB, 0.0, 0.0,
          Raw::Hdr::AlignmentMode::Identity, true }
    };
    for (const GpuParityScenario& scenario : scenarios) {
        const std::filesystem::path cpuDirectory =
            TemporaryDirectory("gpu-reference-cpu");
        const std::filesystem::path gpuDirectory =
            TemporaryDirectory("gpu-reference-gl");
        TemporaryCleanup cpuCleanup { cpuDirectory };
        TemporaryCleanup gpuCleanup { gpuDirectory };
        Raw::Hdr::Request cpuRequest = HdrRequest(cpuDirectory, scenario.alignment);
        cpuRequest.preferGpuFusion = false;
        Raw::Hdr::Services cpuServices = HdrServices(
            scenario.allRejectedPixel
                ? AllRejectedPixelBracket(scenario.pattern)
                : ExactBracket(
                    scenario.pattern,
                    scenario.translationX,
                    scenario.translationY));
        Raw::Hdr::Result cpu = Raw::Hdr::ProcessBurst(cpuRequest, cpuServices);
        Raw::Hdr::Request gpuRequest = HdrRequest(gpuDirectory, scenario.alignment);
        Raw::Hdr::Services gpuServices = HdrServices(
            scenario.allRejectedPixel
                ? AllRejectedPixelBracket(scenario.pattern)
                : ExactBracket(
                    scenario.pattern,
                    scenario.translationX,
                    scenario.translationY));
        gpuServices.executeOpenGlTask = directExecutor;
        Raw::Hdr::Result gpu = Raw::Hdr::ProcessBurst(gpuRequest, gpuServices);
        const bool bothPublished = ValidatePublishedShape(cpu) &&
            ValidatePublishedShape(gpu);
        ok &= bothPublished;
        if (!bothPublished) continue;
        ok &= Check(gpu.diagnostics.executionBackend.find("opengl-4.3") == 0u,
            "HDR processing silently fell back instead of executing OpenGL compute: " +
                gpu.diagnostics.gpuFallbackReason);
        ok &= Check(gpu.diagnostics.gpuDispatchedTileCount > 0u,
            "OpenGL HDR fusion did not report any completed dispatch tile");
        ok &= Check(cpu.validityMask == gpu.validityMask,
            "GPU HDR changed the validity decisions");
        ok &= Check(cpu.ownerFrame == gpu.ownerFrame,
            "GPU HDR changed the dominant-frame ownership decisions");
        ok &= Check(cpu.flags == gpu.flags,
            "GPU HDR changed a categorical fusion/repair decision");
        if (scenario.allRejectedPixel) {
            const std::size_t fallbackPixel =
                static_cast<std::size_t>(200 * kHdrWidth + 200);
            ok &= Check(
                std::isfinite(cpu.virtualAnchorMosaic[fallbackPixel]) &&
                cpu.validityMask[fallbackPixel] != 0u &&
                (cpu.flags[fallbackPixel] &
                    Raw::Hdr::ResultFlagReferenceFallback) != 0u &&
                cpu.ownerFrame[fallbackPixel] == 0u,
                "all-rejected HDR evidence did not produce a finite, flagged exact fallback");
        }
        const auto closeVectors = [&](const std::vector<float>& expected,
                                      const std::vector<float>& actual,
                                      double absoluteTolerance,
                                      double relativeTolerance,
                                      const char* label) {
            if (expected.size() != actual.size())
                return Check(false, std::string("GPU HDR changed ") + label + " shape");
            for (std::size_t pixel = 0; pixel < expected.size(); ++pixel) {
                const double a = expected[pixel];
                const double b = actual[pixel];
                if (!std::isfinite(a) || !std::isfinite(b)) {
                    if ((std::isnan(a) && std::isnan(b)) ||
                        (std::isinf(a) && std::isinf(b) && std::signbit(a) == std::signbit(b)))
                        continue;
                    return Check(false, std::string("GPU HDR changed non-finite ") + label);
                }
                const double tolerance = absoluteTolerance +
                    relativeTolerance * std::max(std::abs(a), std::abs(b));
                if (std::abs(a - b) > tolerance) {
                    return Check(false, std::string("GPU HDR exceeded the ") +
                        label + " parity tolerance at pixel " +
                        std::to_string(pixel) + ": cpu=" + std::to_string(a) +
                        " gpu=" + std::to_string(b));
                }
            }
            return true;
        };
        ok &= closeVectors(cpu.virtualAnchorMosaic, gpu.virtualAnchorMosaic,
            2.0e-5, 4.0e-4, "radiance");
        ok &= closeVectors(cpu.varianceProxy, gpu.varianceProxy,
            2.0e-8, 2.0e-3, "variance");
        ok &= closeVectors(cpu.mergeConfidence, gpu.mergeConfidence,
            2.0e-4, 2.0e-3, "confidence");
        ok &= closeVectors(cpu.effectiveSampleCount, gpu.effectiveSampleCount,
            2.0e-4, 2.0e-3, "effective-sample");
        ok &= closeVectors(cpu.recoveredHeadroomStops, gpu.recoveredHeadroomStops,
            2.0e-5, 4.0e-4, "headroom");
    }

    const std::filesystem::path fallbackDirectory =
        TemporaryDirectory("gpu-forced-fallback");
    TemporaryCleanup fallbackCleanup { fallbackDirectory };
    Raw::Hdr::Request fallbackRequest = HdrRequest(fallbackDirectory);
    Raw::Hdr::Services fallbackServices = HdrServices(
        ExactBracket(Raw::CfaPattern::RGGB));
    fallbackServices.executeOpenGlTask = [](
        Raw::Hdr::OpenGlTask,
        std::string& error) {
        error = "forced validation failure";
        return false;
    };
    Raw::Hdr::Result fallback = Raw::Hdr::ProcessBurst(
        fallbackRequest, fallbackServices);
    ok &= ValidatePublishedShape(fallback);
    ok &= Check(fallback.diagnostics.executionBackend == "cpu-reference" &&
            fallback.diagnostics.gpuFallbackReason == "forced validation failure",
        "a GPU failure did not fall back to the CPU reference path cleanly");

    const std::filesystem::path cancelDirectory =
        TemporaryDirectory("gpu-tile-cancel");
    TemporaryCleanup cancelCleanup { cancelDirectory };
    Raw::Hdr::Request cancelRequest = HdrRequest(cancelDirectory);
    std::atomic<bool> gpuTaskActive { false };
    std::atomic<std::uint32_t> gpuCancelChecks { 0u };
    cancelRequest.shouldCancel = [&]() {
        return gpuTaskActive.load(std::memory_order_relaxed) &&
            gpuCancelChecks.fetch_add(1u, std::memory_order_relaxed) >= 8u;
    };
    Raw::Hdr::Services cancelServices = HdrServices(
        ExactBracket(Raw::CfaPattern::RGGB));
    cancelServices.executeOpenGlTask = [&](
        Raw::Hdr::OpenGlTask task,
        std::string& error) {
        gpuTaskActive.store(true, std::memory_order_relaxed);
        return task ? task(error) : false;
    };
    Raw::Hdr::Result canceled = Raw::Hdr::ProcessBurst(
        cancelRequest, cancelServices);
    ok &= Check(canceled.status == Raw::Hdr::ProcessingStatus::Canceled &&
            canceled.virtualAnchorMosaic.empty(),
        "tile-boundary GPU cancellation exposed a partial HDR result");

    EditorRenderWorker worker;
    ok &= Check(worker.Initialize(window),
        "the production render worker could not initialize its shared GPU context");
    if (ok) {
        const std::filesystem::path workerDirectory =
            TemporaryDirectory("gpu-render-worker");
        TemporaryCleanup workerCleanup { workerDirectory };
        Raw::Hdr::Request workerRequest = HdrRequest(workerDirectory);
        Raw::Hdr::Services workerServices = HdrServices(
            ExactBracket(Raw::CfaPattern::RGGB));
        workerServices.executeOpenGlTask = [&](
            Raw::Hdr::OpenGlTask task,
            std::string& error) {
            return worker.ExecuteOpenGlTaskBlocking(std::move(task), error);
        };
        Raw::Hdr::Result workerResult = Raw::Hdr::ProcessBurst(
            workerRequest, workerServices);
        ok &= ValidatePublishedShape(workerResult);
        ok &= Check(workerResult.diagnostics.executionBackend.find(
                "opengl-4.3") == 0u,
            "the production render-worker handoff did not execute GPU HDR: " +
                workerResult.diagnostics.gpuFallbackReason);
    }
    worker.Shutdown();

    glfwMakeContextCurrent(nullptr);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "[hdr-v4] OpenGL compute parity and fallback "
              << (ok ? "passed" : "failed") << std::endl;
    return ok;
}

} // namespace Stack::Validation
