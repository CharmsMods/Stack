#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/Streaming.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 8 validation failed: " << message << std::endl;
    }
    return condition;
}

std::string BuildKey(
    Raw::Mfd::MfdCacheStage stage,
    const std::string& suffix = "baseline") {
    Raw::Mfd::MfdCacheKeyRequest request;
    request.stage = stage;
    for (const std::string& name :
         Raw::Mfd::RequiredMfdCacheDependencies(stage)) {
        request.dependencies.push_back({ name, name + "-" + suffix });
    }
    std::string key;
    std::string error;
    if (!Raw::Mfd::BuildMfdCacheKey(request, key, &error)) {
        std::cerr << "MFD Phase 8 key helper failed: " << error << std::endl;
    }
    return key;
}

Raw::Mfd::FusionReferenceSample MakeReference(
    std::uint64_t x,
    std::uint64_t y) {
    Raw::Mfd::FusionReferenceSample sample;
    sample.valid = true;
    sample.noiseQuality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    sample.normalizedValue = static_cast<double>(static_cast<float>(
        0.20 + 0.001 * static_cast<double>((x + 3u * y) % 17u)));
    sample.comparisonGain = 1.0;
    sample.gateVariance = 0.04;
    sample.fusionVariance = 0.04;
    sample.darkVariance = 0.01;
    sample.effectiveDnStep = 1.0e-4;
    return sample;
}

Raw::Mfd::FusionCandidateSample MakeCandidate(
    std::uint64_t x,
    std::uint64_t y) {
    const auto reference = MakeReference(x, y);
    Raw::Mfd::FusionCandidateSample sample;
    sample.valid = true;
    sample.hardValid = true;
    sample.noiseQuality = Raw::Mfd::NoiseModelQuality::TrustedMetadata;
    sample.value = reference.normalizedValue;
    sample.gateVariance = 0.04;
    sample.fusionVariance = 0.04;
    sample.darkVariance = 0.01;
    sample.effectiveDnStep = 1.0e-4;
    sample.reliability = 1.0;
    sample.referenceDefectGate = 1.0;
    return sample;
}

Raw::Mfd::StreamingFusionRequest MakeStreamingRequest(
    Raw::Mfd::AtomicFusionResultPublisher& publisher,
    std::size_t alternateCount = 3u,
    std::uint32_t workers = 4u) {
    Raw::Mfd::StreamingFusionRequest request;
    request.rawExtent = { 32u, 24u };
    request.parameters.fusion.outputTileRawPixels = 8u;
    request.alternateCount = alternateCount;
    request.fusedResultCacheKey = BuildKey(
        Raw::Mfd::MfdCacheStage::FusedResult,
        "streaming-" + std::to_string(alternateCount));
    request.memoryBudgetBytes = 4u * 1024u * 1024u;
    request.workerCount = workers;
    request.referenceProvider = [](
        std::uint64_t x,
        std::uint64_t y,
        Raw::Mfd::FusionReferenceSample& sample) {
        sample = MakeReference(x, y);
        return true;
    };
    request.candidateProvider = [](
        std::size_t,
        std::uint64_t x,
        std::uint64_t y,
        Raw::Mfd::FusionCandidateSample& sample) {
        sample = MakeCandidate(x, y);
        return true;
    };
    request.publisher = &publisher;
    return request;
}

Raw::Mfd::FusionTileResult MakeCacheTile() {
    Raw::Mfd::FusionTileResult tile;
    tile.valid = true;
    tile.message = "synthetic phase-8 cache tile";
    tile.originRawX = 5u;
    tile.originRawY = 7u;
    tile.extent = { 4u, 3u };
    tile.normalizedMosaic.resize(12u);
    tile.diagnostics.resize(12u);
    for (std::size_t index = 0u; index < 12u; ++index) {
        tile.normalizedMosaic[index] = static_cast<float>(0.01 * index);
        tile.diagnostics[index].decisionReason =
            Raw::Mfd::DecisionReason::NumericalFallback;
        tile.diagnostics[index].exactReferenceCopy = true;
        tile.diagnostics[index].effectiveSampleCount = 1.0;
    }
    tile.exactReferencePixelCount = 12u;
    return tile;
}

std::filesystem::path MakeTemporaryDirectory() {
    static std::atomic<std::uint64_t> counter { 0u };
    const auto stamp = std::chrono::steady_clock::now()
        .time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("stack-mfd-phase8-" + std::to_string(stamp) + "-" +
            std::to_string(counter.fetch_add(1u)));
}

bool ValidateCacheDependencyKeys() {
    bool ok = true;
    std::array<std::string, 6> keys;
    for (std::size_t index = 0u; index < keys.size(); ++index) {
        const auto stage = static_cast<Raw::Mfd::MfdCacheStage>(index);
        keys[index] = BuildKey(stage);
        ok &= Check(keys[index].size() == 64u,
            "a stage dependency key was not a SHA-256 identity");
        ok &= Check(BuildKey(stage, "changed") != keys[index],
            "a changed stage dependency did not invalidate its cache key");
    }

    Raw::Mfd::MfdCacheKeyRequest ordered;
    ordered.stage = Raw::Mfd::MfdCacheStage::FusedResult;
    for (const std::string& name :
         Raw::Mfd::RequiredMfdCacheDependencies(ordered.stage)) {
        ordered.dependencies.push_back({ name, name + "-identity" });
    }
    std::string orderedKey;
    std::string reversedKey;
    std::string changedKey;
    std::string error;
    ok &= Check(Raw::Mfd::BuildMfdCacheKey(
            ordered, orderedKey, &error),
        "the complete F dependency set was rejected: " + error);
    std::reverse(ordered.dependencies.begin(), ordered.dependencies.end());
    ok &= Check(Raw::Mfd::BuildMfdCacheKey(
            ordered, reversedKey, &error) && reversedKey == orderedKey,
        "dependency-list ordering changed a canonical cache key");
    for (auto& dependency : ordered.dependencies) {
        if (dependency.name == "ordered-frame-ids") {
            dependency.identity = Raw::Mfd::EncodeOrderedIdentities(
                { "frame-b", "frame-a" });
        }
    }
    ok &= Check(Raw::Mfd::BuildMfdCacheKey(
            ordered, changedKey, &error) && changedKey != orderedKey,
        "ordered frame identity changes did not invalidate cache F");
    ordered.dependencies.pop_back();
    std::string invalid;
    ok &= Check(!Raw::Mfd::BuildMfdCacheKey(ordered, invalid, &error),
        "a missing stage dependency produced an authoritative key");
    ok &= Check(
        Raw::Mfd::EncodeOrderedIdentities({ "a", "b" }) !=
            Raw::Mfd::EncodeOrderedIdentities({ "b", "a" }),
        "ordered identity encoding erased frame order");
    return ok;
}

Raw::Mfd::LocalMotionGrid MakeMotionGrid(bool usable) {
    Raw::Mfd::LocalMotionGrid grid;
    grid.valid = true;
    grid.referenceRawExtent = { 128u, 96u };
    grid.sourceRawExtent = { 128u, 96u };
    grid.width = 3u;
    grid.height = 3u;
    grid.originRawX = 0.0;
    grid.originRawY = 0.0;
    grid.spacingRawX = 64.0;
    grid.spacingRawY = 48.0;
    grid.nodes.resize(9u);
    for (std::uint32_t y = 0u; y < grid.height; ++y) {
        for (std::uint32_t x = 0u; x < grid.width; ++x) {
            auto& node = grid.nodes[y * grid.width + x];
            node.gridX = x;
            node.gridY = y;
            node.centerRaw = { 64.0 * x, 48.0 * y };
            node.residualRaw = { 3.0, -2.0 };
            node.covarianceRaw = { 0.1, 0.0, 0.1 };
            node.state = usable
                ? Raw::Mfd::MotionNodeState::Structured
                : Raw::Mfd::MotionNodeState::Rejected;
            node.confidence = usable ? 1.0 : 0.0;
        }
    }
    grid.structuredCount = usable ? 9u : 0u;
    grid.rejectedCount = usable ? 0u : 9u;
    return grid;
}

bool ValidateRoiAndMemoryPlanning() {
    bool ok = true;
    auto grid = MakeMotionGrid(true);
    Raw::Mfd::FusionSourceRoiRequest roiRequest;
    roiRequest.outputRect = { 20u, 20u, 32u, 24u };
    roiRequest.sourceExtent = { 128u, 96u };
    roiRequest.motionGrid = &grid;
    Raw::Mfd::FusionSourceRoiPlan roi;
    std::string error;
    ok &= Check(Raw::Mfd::PlanFusionSourceRoi(
            roiRequest, roi, &error) && roi.valid && !roi.requiresSplit &&
            roi.sourceRect.x == 19u && roi.sourceRect.y == 14u &&
            roi.sourceRect.width == 40u && roi.sourceRect.height == 32u,
        "the conservative motion ROI did not include warp extrema and halo: " +
            error);
    grid = MakeMotionGrid(false);
    roiRequest.motionGrid = &grid;
    roi = {};
    error.clear();
    ok &= Check(Raw::Mfd::PlanFusionSourceRoi(
            roiRequest, roi, &error) && roi.requiresSplit && !roi.valid,
        "an uncertain motion bound did not request a tile split");

    Raw::Mfd::Parameters parameters;
    std::vector<Raw::Mfd::FusionOutputTilePlan> highMegapixel;
    ok &= Check(Raw::Mfd::PlanFusionOutputTiles(
            { 12000u, 8000u },
            parameters,
            31u,
            64u * 1024u * 1024u,
            highMegapixel,
            &error),
        "high-megapixel planning failed without allocating an image: " + error);
    std::uint64_t covered = 0u;
    for (const auto& tile : highMegapixel) {
        covered += tile.outputRect.width * tile.outputRect.height;
    }
    ok &= Check(covered == 12000u * 8000u,
        "high-megapixel tile planning did not cover every output pixel");

    parameters.fusion.outputTileRawPixels = 64u;
    std::vector<Raw::Mfd::FusionOutputTilePlan> roomy;
    std::vector<Raw::Mfd::FusionOutputTilePlan> constrained;
    const std::uint64_t constrainedBudget =
        Raw::Mfd::EstimateFusionTileWorkingBytes({ 8u, 8u }, 31u);
    ok &= Check(Raw::Mfd::PlanFusionOutputTiles(
            { 128u, 64u }, parameters, 31u, 8u * 1024u * 1024u,
            roomy, &error) &&
            Raw::Mfd::PlanFusionOutputTiles(
                { 128u, 64u }, parameters, 31u, constrainedBudget,
                constrained, &error) && constrained.size() > roomy.size(),
        "low-memory planning did not split the deterministic base tiles");
    for (const auto& tile : constrained) {
        ok &= Check(tile.estimatedWorkingBytes <= constrainedBudget,
            "a planned tile exceeded the declared working-memory budget");
    }
    const std::uint64_t onePixel =
        Raw::Mfd::EstimateFusionTileWorkingBytes({ 1u, 1u }, 31u);
    std::vector<Raw::Mfd::FusionOutputTilePlan> impossible;
    ok &= Check(!Raw::Mfd::PlanFusionOutputTiles(
            { 1u, 1u }, parameters, 31u, onePixel - 1u,
            impossible, &error),
        "a budget too small for one scalar sample was accepted");
    return ok;
}

bool ValidateDirectoryCache() {
    bool ok = true;
    const std::filesystem::path directory = MakeTemporaryDirectory();
    Raw::Mfd::DirectoryFusionTileCache cache(directory);
    const std::string key = BuildKey(Raw::Mfd::MfdCacheStage::FusedResult);
    const auto original = MakeCacheTile();
    std::string error;
    ok &= Check(cache.Write(key, 4u, original, &error),
        "directory cache write failed: " + error);
    Raw::Mfd::FusionTileResult loaded;
    ok &= Check(cache.Read(key, 4u, loaded, &error) ==
            Raw::Mfd::FusionTileCacheReadStatus::Hit &&
            loaded.normalizedMosaic == original.normalizedMosaic,
        "directory cache did not round-trip an exact tile: " + error);
    {
        std::fstream corrupt(
            cache.TilePath(key, 4u),
            std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(0);
        corrupt.put('X');
    }
    ok &= Check(cache.Read(key, 4u, loaded, &error) ==
            Raw::Mfd::FusionTileCacheReadStatus::Corrupt,
        "a corrupted cache header remained authoritative");
    ok &= Check(cache.Erase(key, 4u, &error) &&
            cache.Read(key, 4u, loaded, &error) ==
                Raw::Mfd::FusionTileCacheReadStatus::Miss,
        "a corrupted cache tile could not be discarded");
    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    return ok;
}

bool ValidateBurstCountsAndDeterminism() {
    bool ok = true;
    for (std::size_t alternateCount : { 1u, 3u, 7u, 15u, 31u }) {
        Raw::Mfd::AtomicFusionResultPublisher publisher;
        auto request = MakeStreamingRequest(publisher, alternateCount, 4u);
        const auto fused = Raw::Mfd::ExecuteStreamingFusion(request);
        ok &= Check(fused.status == Raw::Mfd::StreamingFusionStatus::Success &&
                fused.published.result &&
                fused.published.result->normalizedMosaic.size() == 32u * 24u,
            "streaming failed for a 2/4/8/16/32-frame burst: " +
                fused.message);
    }

    Raw::Mfd::AtomicFusionResultPublisher serialPublisher;
    Raw::Mfd::AtomicFusionResultPublisher parallelPublisher;
    const auto serial = Raw::Mfd::ExecuteStreamingFusion(
        MakeStreamingRequest(serialPublisher, 7u, 1u));
    const auto parallel = Raw::Mfd::ExecuteStreamingFusion(
        MakeStreamingRequest(parallelPublisher, 7u, 4u));
    ok &= Check(serial.status == Raw::Mfd::StreamingFusionStatus::Success &&
            parallel.status == Raw::Mfd::StreamingFusionStatus::Success &&
            serial.published.result->contentHash ==
                parallel.published.result->contentHash &&
            serial.published.result->normalizedMosaic ==
                parallel.published.result->normalizedMosaic,
        "worker-count changes altered deterministic fusion output");
    return ok;
}

bool ValidateCacheRecoveryAndEquivalence() {
    bool ok = true;
    const std::filesystem::path directory = MakeTemporaryDirectory();
    Raw::Mfd::DirectoryFusionTileCache cache(directory);
    Raw::Mfd::AtomicFusionResultPublisher firstPublisher;
    auto firstRequest = MakeStreamingRequest(firstPublisher, 3u, 4u);
    firstRequest.tileCache = &cache;
    const auto first = Raw::Mfd::ExecuteStreamingFusion(firstRequest);

    Raw::Mfd::AtomicFusionResultPublisher hitPublisher;
    auto hitRequest = MakeStreamingRequest(hitPublisher, 3u, 4u);
    hitRequest.tileCache = &cache;
    const auto hit = Raw::Mfd::ExecuteStreamingFusion(hitRequest);
    ok &= Check(first.status == Raw::Mfd::StreamingFusionStatus::Success &&
            hit.status == Raw::Mfd::StreamingFusionStatus::Success &&
            hit.diagnostics.cacheHitTileCount ==
                hit.diagnostics.plannedTileCount &&
            first.published.result->contentHash == hit.published.result->contentHash,
        "cache-hit and cache-miss execution were not bit-equivalent");

    {
        std::fstream corrupt(
            cache.TilePath(firstRequest.fusedResultCacheKey, 0u),
            std::ios::binary | std::ios::in | std::ios::out);
        corrupt.seekp(0);
        corrupt.put('X');
    }
    Raw::Mfd::AtomicFusionResultPublisher recoveryPublisher;
    auto recoveryRequest = MakeStreamingRequest(recoveryPublisher, 3u, 4u);
    recoveryRequest.tileCache = &cache;
    const auto recovered = Raw::Mfd::ExecuteStreamingFusion(recoveryRequest);
    ok &= Check(recovered.status == Raw::Mfd::StreamingFusionStatus::Success &&
            recovered.diagnostics.corruptCacheRecoveryCount == 1u &&
            recovered.published.result->contentHash ==
                first.published.result->contentHash,
        "cache corruption was not discarded and recomputed exactly");

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    return ok;
}

bool ValidateRecoveryCancellationAndPublication() {
    bool ok = true;
    const Raw::Mfd::StreamingTileProcessor failProcessor = [](
        const Raw::Mfd::FusionTileRequest&,
        Raw::Mfd::FusionTileResult& result,
        std::string* error) {
        result = {};
        if (error) *error = "injected tile processor failure";
        return false;
    };

    Raw::Mfd::AtomicFusionResultPublisher strictPublisher;
    auto strictRequest = MakeStreamingRequest(strictPublisher, 3u, 4u);
    strictRequest.primaryTileProcessor = failProcessor;
    const auto strict = Raw::Mfd::ExecuteStreamingFusion(strictRequest);
    ok &= Check(strict.status == Raw::Mfd::StreamingFusionStatus::Success &&
            strict.diagnostics.strictScalarRecoveryCount ==
                strict.diagnostics.plannedTileCount,
        "strict scalar retry did not recover every injected primary failure");

    Raw::Mfd::AtomicFusionResultPublisher fallbackPublisher;
    auto fallbackRequest = MakeStreamingRequest(fallbackPublisher, 3u, 4u);
    fallbackRequest.primaryTileProcessor = failProcessor;
    fallbackRequest.strictScalarTileProcessor = failProcessor;
    const auto fallback = Raw::Mfd::ExecuteStreamingFusion(fallbackRequest);
    ok &= Check(fallback.status == Raw::Mfd::StreamingFusionStatus::Success &&
            fallback.diagnostics.referenceTileFallbackCount ==
                fallback.diagnostics.plannedTileCount &&
            std::all_of(
                fallback.published.result->diagnostics.begin(),
                fallback.published.result->diagnostics.end(),
                [](const Raw::Mfd::FusionPixelDiagnostics& diagnostic) {
                    return diagnostic.exactReferenceCopy &&
                        diagnostic.decisionReason ==
                            Raw::Mfd::DecisionReason::NumericalFallback;
                }),
        "exact-reference tile fallback did not survive both processor failures");

    Raw::Mfd::AtomicFusionResultPublisher publisher;
    const auto baseline = Raw::Mfd::ExecuteStreamingFusion(
        MakeStreamingRequest(publisher, 3u, 1u));
    ok &= Check(baseline.status == Raw::Mfd::StreamingFusionStatus::Success,
        "cancellation baseline could not be published");
    const auto before = publisher.Snapshot();
    for (std::uint64_t threshold : { 1u, 5u, 10u }) {
        std::atomic<std::uint64_t> checks { 0u };
        auto canceledRequest = MakeStreamingRequest(publisher, 31u, 4u);
        canceledRequest.expectedPublicationGeneration = before.generation;
        canceledRequest.shouldCancel = [&]() {
            return checks.fetch_add(1u) >= threshold;
        };
        const auto canceled = Raw::Mfd::ExecuteStreamingFusion(canceledRequest);
        const auto after = publisher.Snapshot();
        ok &= Check(canceled.status == Raw::Mfd::StreamingFusionStatus::Canceled &&
                after.generation == before.generation &&
                after.result == before.result,
            "randomized cancellation published or replaced a partial result");
    }

    auto conflictRequest = MakeStreamingRequest(publisher, 3u, 4u);
    conflictRequest.expectedPublicationGeneration = before.generation - 1u;
    const auto conflict = Raw::Mfd::ExecuteStreamingFusion(conflictRequest);
    const auto afterConflict = publisher.Snapshot();
    ok &= Check(conflict.status ==
                Raw::Mfd::StreamingFusionStatus::PublicationConflict &&
            afterConflict.generation == before.generation &&
            afterConflict.result == before.result,
        "a stale streaming completion replaced a newer published generation");

    Raw::Mfd::AtomicFusionResultPublisher lowMemoryPublisher;
    auto lowMemoryRequest = MakeStreamingRequest(
        lowMemoryPublisher, 31u, 4u);
    lowMemoryRequest.rawExtent = { 64u, 48u };
    lowMemoryRequest.parameters.fusion.outputTileRawPixels = 32u;
    lowMemoryRequest.memoryBudgetBytes =
        Raw::Mfd::EstimateFusionTileWorkingBytes({ 8u, 8u }, 31u);
    const std::filesystem::path spillDirectory = MakeTemporaryDirectory();
    Raw::Mfd::DirectoryFusionTileCache spillCache(spillDirectory);
    lowMemoryRequest.tileCache = &spillCache;
    const auto lowMemory = Raw::Mfd::ExecuteStreamingFusion(lowMemoryRequest);
    ok &= Check(lowMemory.status == Raw::Mfd::StreamingFusionStatus::Success &&
            lowMemory.diagnostics.plannedTileCount > 4u &&
            lowMemory.diagnostics.peakWorkingBytes <=
                lowMemoryRequest.memoryBudgetBytes &&
            std::filesystem::exists(spillCache.TilePath(
                lowMemoryRequest.fusedResultCacheKey, 0u)),
        "low-memory execution exceeded its budget or failed to split tiles");
    std::error_code cleanupError;
    std::filesystem::remove_all(spillDirectory, cleanupError);
    return ok;
}

} // namespace

bool ValidateMfdPhase8Streaming() {
    bool ok = true;
    ok &= ValidateCacheDependencyKeys();
    ok &= ValidateRoiAndMemoryPlanning();
    ok &= ValidateDirectoryCache();
    ok &= ValidateBurstCountsAndDeterminism();
    ok &= ValidateCacheRecoveryAndEquivalence();
    ok &= ValidateRecoveryCancellationAndPublication();
    if (ok) {
        std::cout << "MFD Phase 8 streaming validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
