#include "Raw/MultiFrameDenoise/Streaming.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    count = 0u;
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::uint64_t>::max() /
            extent.height) {
        return false;
    }
    const std::uint64_t samples = extent.width * extent.height;
    if (samples > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(samples);
    return true;
}

bool TileMatchesPlan(
    const FusionTileResult& tile,
    const FusionOutputTilePlan& plan) {
    std::size_t sampleCount = 0u;
    return tile.valid && tile.originRawX == plan.outputRect.x &&
        tile.originRawY == plan.outputRect.y &&
        tile.extent.width == plan.outputRect.width &&
        tile.extent.height == plan.outputRect.height &&
        CheckedSampleCount(tile.extent, sampleCount) &&
        tile.normalizedMosaic.size() == sampleCount &&
        tile.diagnostics.size() == sampleCount;
}

FusionTileRequest MakeTileRequest(
    const StreamingFusionRequest& request,
    const FusionOutputTilePlan& tile,
    const std::function<bool()>& shouldStop) {
    FusionTileRequest fusionRequest;
    fusionRequest.originRawX = tile.outputRect.x;
    fusionRequest.originRawY = tile.outputRect.y;
    fusionRequest.extent = { tile.outputRect.width, tile.outputRect.height };
    fusionRequest.alternateCount = request.alternateCount;
    fusionRequest.parameters = request.parameters;
    fusionRequest.referenceProvider = request.referenceProvider;
    fusionRequest.candidateProvider = request.candidateProvider;
    fusionRequest.shouldCancel = shouldStop;
    return fusionRequest;
}

bool MakeReferenceFallbackTile(
    const FusionTileRequest& request,
    FusionTileResult& tile,
    std::string* error) {
    tile = {};
    std::size_t sampleCount = 0u;
    if (!CheckedSampleCount(request.extent, sampleCount)) {
        if (error) *error = "MFD reference fallback tile extent is invalid.";
        return false;
    }
    try {
        tile.normalizedMosaic.resize(sampleCount);
        tile.diagnostics.resize(sampleCount);
    } catch (const std::bad_alloc&) {
        if (error) *error = "MFD reference fallback tile allocation failed.";
        return false;
    }
    for (std::size_t index = 0u; index < sampleCount; ++index) {
        if (request.shouldCancel && request.shouldCancel()) {
            tile = {};
            if (error) *error = "MFD reference fallback tile was canceled.";
            return false;
        }
        const std::uint64_t localX =
            static_cast<std::uint64_t>(index) % request.extent.width;
        const std::uint64_t localY =
            static_cast<std::uint64_t>(index) / request.extent.width;
        FusionReferenceSample reference;
        if (!request.referenceProvider(
                request.originRawX + localX,
                request.originRawY + localY,
                reference) ||
            !reference.valid || !std::isfinite(reference.normalizedValue) ||
            std::abs(reference.normalizedValue) >
                static_cast<double>(std::numeric_limits<float>::max())) {
            tile = {};
            if (error) {
                *error = "MFD exact-reference tile fallback could not read the reference.";
            }
            return false;
        }
        tile.normalizedMosaic[index] =
            static_cast<float>(reference.normalizedValue);
        FusionPixelDiagnostics& diagnostic = tile.diagnostics[index];
        diagnostic.decisionReason = DecisionReason::NumericalFallback;
        diagnostic.exactReferenceCopy = true;
        diagnostic.referenceIncluded = true;
        diagnostic.effectiveSampleCount = 1.0;
    }
    tile.valid = true;
    tile.message = "MFD tile used an exact-reference numerical fallback.";
    tile.originRawX = request.originRawX;
    tile.originRawY = request.originRawY;
    tile.extent = request.extent;
    tile.exactReferencePixelCount = static_cast<std::uint64_t>(sampleCount);
    return true;
}

void CopyTileToPublished(
    const FusionTileResult& tile,
    PixelExtent outputExtent,
    PublishedFusionResult& published) {
    for (std::uint64_t row = 0u; row < tile.extent.height; ++row) {
        const std::size_t sourceOffset = static_cast<std::size_t>(
            row * tile.extent.width);
        const std::size_t destinationOffset = static_cast<std::size_t>(
            (tile.originRawY + row) * outputExtent.width + tile.originRawX);
        const std::size_t width = static_cast<std::size_t>(tile.extent.width);
        std::copy_n(
            tile.normalizedMosaic.begin() + sourceOffset,
            width,
            published.normalizedMosaic.begin() + destinationOffset);
        std::copy_n(
            tile.diagnostics.begin() + sourceOffset,
            width,
            published.diagnostics.begin() + destinationOffset);
    }
}

} // namespace

StreamingFusionResult ExecuteStreamingFusion(
    const StreamingFusionRequest& request) {
    StreamingFusionResult result;
    result.diagnostics.requestedWorkerCount = request.workerCount;
    std::size_t outputSampleCount = 0u;
    std::string validationError;
    if (!CheckedSampleCount(request.rawExtent, outputSampleCount) ||
        !ValidateParameters(request.parameters, &validationError) ||
        request.alternateCount == 0u ||
        !LooksLikeSha256(request.fusedResultCacheKey) ||
        request.memoryBudgetBytes == 0u || request.workerCount == 0u ||
        !request.referenceProvider || !request.candidateProvider ||
        !request.publisher) {
        result.message = validationError.empty()
            ? "MFD streaming fusion request is invalid."
            : validationError;
        return result;
    }
    if (request.shouldCancel && request.shouldCancel()) {
        result.status = StreamingFusionStatus::Canceled;
        result.message = "MFD streaming fusion was canceled before planning.";
        return result;
    }

    std::vector<FusionOutputTilePlan> plans;
    try {
        if (!PlanFusionOutputTiles(
                request.rawExtent,
                request.parameters,
                request.alternateCount,
                request.memoryBudgetBytes,
                plans,
                &result.message)) {
            return result;
        }
    } catch (const std::bad_alloc&) {
        result.message = "MFD output tile planning allocation failed.";
        return result;
    }
    result.diagnostics.plannedTileCount =
        static_cast<std::uint64_t>(plans.size());
    if (request.reportProgress) {
        request.reportProgress(0u, result.diagnostics.plannedTileCount);
    }
    result.diagnostics.effectiveWorkerCount = static_cast<std::uint32_t>(
        std::min<std::size_t>(request.workerCount, plans.size()));

    PublishedFusionResult pending;
    pending.fusedResultCacheKey = request.fusedResultCacheKey;
    pending.extent = request.rawExtent;
    try {
        pending.normalizedMosaic.resize(outputSampleCount);
        pending.diagnostics.resize(outputSampleCount);
    } catch (const std::bad_alloc&) {
        result.message = "MFD temporary fusion result allocation failed.";
        return result;
    }

    MfdMemoryBudget memoryBudget(request.memoryBudgetBytes);
    std::atomic<std::size_t> nextTile { 0u };
    std::atomic<bool> abort { false };
    std::atomic<bool> canceled { false };
    std::atomic<std::uint64_t> computed { 0u };
    std::atomic<std::uint64_t> completed { 0u };
    std::atomic<std::uint64_t> cacheHits { 0u };
    std::atomic<std::uint64_t> cacheMisses { 0u };
    std::atomic<std::uint64_t> corruptRecoveries { 0u };
    std::atomic<std::uint64_t> cacheWriteFailures { 0u };
    std::atomic<std::uint64_t> strictRecoveries { 0u };
    std::atomic<std::uint64_t> referenceFallbacks { 0u };
    std::mutex errorMutex;
    std::string firstError;

    const auto userCanceled = [&]() {
        if (request.shouldCancel && request.shouldCancel()) {
            canceled.store(true, std::memory_order_relaxed);
            return true;
        }
        return false;
    };
    const auto shouldStop = [&]() {
        return abort.load(std::memory_order_relaxed) || userCanceled();
    };
    const auto fail = [&](const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(errorMutex);
            if (firstError.empty()) firstError = message;
        }
        abort.store(true, std::memory_order_relaxed);
    };

    const StreamingTileProcessor robustProcessor = [](
        const FusionTileRequest& tileRequest,
        FusionTileResult& tileResult,
        std::string* error) {
        return FuseRobustTile(tileRequest, tileResult, error);
    };
    const StreamingTileProcessor primary = request.primaryTileProcessor
        ? request.primaryTileProcessor
        : robustProcessor;
    const StreamingTileProcessor strict = request.strictScalarTileProcessor
        ? request.strictScalarTileProcessor
        : robustProcessor;

    const auto worker = [&]() {
        try {
            while (!shouldStop()) {
                const std::size_t planIndex =
                    nextTile.fetch_add(1u, std::memory_order_relaxed);
                if (planIndex >= plans.size()) return;
                const FusionOutputTilePlan& plan = plans[planIndex];
                MfdMemoryBudget::Reservation reservation;
                if (!memoryBudget.Acquire(
                        plan.estimatedWorkingBytes,
                        shouldStop,
                        reservation)) {
                    if (userCanceled()) {
                        abort.store(true, std::memory_order_relaxed);
                    }
                    return;
                }

                FusionTileResult tile;
                bool haveTile = false;
                if (request.tileCache) {
                    std::string cacheError;
                    const FusionTileCacheReadStatus cacheStatus =
                        request.tileCache->Read(
                            request.fusedResultCacheKey,
                            plan.ordinal,
                            tile,
                            &cacheError);
                    if (cacheStatus == FusionTileCacheReadStatus::Hit &&
                        TileMatchesPlan(tile, plan)) {
                        ++cacheHits;
                        haveTile = true;
                    } else {
                        ++cacheMisses;
                        if (cacheStatus == FusionTileCacheReadStatus::Corrupt ||
                            cacheStatus == FusionTileCacheReadStatus::Hit) {
                            ++corruptRecoveries;
                            std::string ignored;
                            request.tileCache->Erase(
                                request.fusedResultCacheKey,
                                plan.ordinal,
                                &ignored);
                        }
                        tile = {};
                    }
                } else {
                    ++cacheMisses;
                }

                if (!haveTile) {
                    const FusionTileRequest tileRequest =
                        MakeTileRequest(request, plan, shouldStop);
                    std::string tileError;
                    bool processed = primary(tileRequest, tile, &tileError) &&
                        TileMatchesPlan(tile, plan);
                    if (!processed && !shouldStop()) {
                        tile = {};
                        std::string strictError;
                        processed = strict(tileRequest, tile, &strictError) &&
                            TileMatchesPlan(tile, plan);
                        if (processed) {
                            ++strictRecoveries;
                        } else if (!strictError.empty()) {
                            tileError = std::move(strictError);
                        }
                    }
                    if (!processed && !shouldStop()) {
                        tile = {};
                        std::string fallbackError;
                        processed = MakeReferenceFallbackTile(
                            tileRequest, tile, &fallbackError) &&
                            TileMatchesPlan(tile, plan);
                        if (processed) {
                            ++referenceFallbacks;
                        } else if (!fallbackError.empty()) {
                            tileError = std::move(fallbackError);
                        }
                    }
                    if (!processed) {
                        if (userCanceled()) {
                            abort.store(true, std::memory_order_relaxed);
                        } else if (!abort.load(std::memory_order_relaxed)) {
                            fail(tileError.empty()
                                ? "MFD tile processing failed."
                                : tileError);
                        }
                        return;
                    }
                    ++computed;
                    if (request.tileCache && !shouldStop()) {
                        std::string ignored;
                        if (!request.tileCache->Write(
                                request.fusedResultCacheKey,
                                plan.ordinal,
                                tile,
                                &ignored)) {
                            ++cacheWriteFailures;
                        }
                    }
                }
                if (shouldStop()) return;
                CopyTileToPublished(tile, request.rawExtent, pending);
                const std::uint64_t completedNow =
                    completed.fetch_add(1u, std::memory_order_relaxed) + 1u;
                if (request.reportProgress) {
                    request.reportProgress(
                        completedNow,
                        static_cast<std::uint64_t>(plans.size()));
                }
            }
        } catch (const std::exception& error) {
            fail(std::string("MFD tile worker failed: ") + error.what());
        } catch (...) {
            fail("MFD tile worker failed with an unknown exception.");
        }
    };

    std::vector<std::thread> workers;
    try {
        workers.reserve(result.diagnostics.effectiveWorkerCount);
        for (std::uint32_t index = 0u;
             index < result.diagnostics.effectiveWorkerCount;
             ++index) {
            workers.emplace_back(worker);
        }
    } catch (const std::system_error& error) {
        fail(std::string("MFD worker creation failed: ") + error.what());
    } catch (const std::bad_alloc&) {
        fail("MFD worker allocation failed.");
    }
    for (std::thread& thread : workers) {
        if (thread.joinable()) thread.join();
    }

    result.diagnostics.computedTileCount = computed.load();
    result.diagnostics.cacheHitTileCount = cacheHits.load();
    result.diagnostics.cacheMissTileCount = cacheMisses.load();
    result.diagnostics.corruptCacheRecoveryCount = corruptRecoveries.load();
    result.diagnostics.cacheWriteFailureCount = cacheWriteFailures.load();
    result.diagnostics.strictScalarRecoveryCount = strictRecoveries.load();
    result.diagnostics.referenceTileFallbackCount = referenceFallbacks.load();
    result.diagnostics.peakWorkingBytes = memoryBudget.PeakResidentBytes();
    result.diagnostics.memoryWaitCount = memoryBudget.WaitCount();

    if (canceled.load() || (request.shouldCancel && request.shouldCancel())) {
        result.status = StreamingFusionStatus::Canceled;
        result.message = "MFD streaming fusion was canceled; no result was published.";
        return result;
    }
    if (abort.load()) {
        std::lock_guard<std::mutex> lock(errorMutex);
        result.message = firstError.empty()
            ? "MFD streaming fusion failed."
            : firstError;
        return result;
    }
    if (completed.load() != plans.size()) {
        result.message = "MFD streaming fusion ended before every tile completed.";
        return result;
    }

    std::string publicationError;
    if (!request.publisher->Publish(
            request.expectedPublicationGeneration,
            std::move(pending),
            result.published,
            &publicationError)) {
        const PublishedFusionSnapshot current = request.publisher->Snapshot();
        if (current.generation != request.expectedPublicationGeneration) {
            result.status = StreamingFusionStatus::PublicationConflict;
            result.message = "MFD result was complete but a newer generation won publication.";
        } else {
            result.message = publicationError.empty()
                ? "MFD result publication failed."
                : publicationError;
        }
        return result;
    }
    result.status = StreamingFusionStatus::Success;
    result.message = "MFD streaming fusion completed and published atomically.";
    return result;
}

} // namespace Raw::Mfd
