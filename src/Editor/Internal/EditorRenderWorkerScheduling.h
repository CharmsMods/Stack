#pragma once

#include "Editor/RawRenderPurpose.h"
#include "Raw/RawViewportSettings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Stack::EditorRenderScheduling {

inline bool ShouldUseCompositeRenderPath(
    bool viewportHasMultipleCompletedChains,
    bool rawWorkspaceActive,
    bool rawWorkspaceRootTabActive) {
    // RAW's root workspace owns one dedicated presentation surface even when
    // its managed graph contains multiple completed/output chains. Routing it
    // through the editor composite canvas bypasses RawRenderService entirely,
    // so neither settled native refinement nor RAW presentation adoption can
    // occur.
    return viewportHasMultipleCompletedChains &&
        !(rawWorkspaceActive && rawWorkspaceRootTabActive);
}

constexpr double kRawPreviewTargetFrameMs = 1000.0 / 30.0;
constexpr double kRawPreviewMinimumFrameMs = 1000.0 / 20.0;
constexpr float kRawPreviewMinimumScale = 0.0625f;
inline bool IsRawGestureIntermediate(bool resultInteractive, bool gestureActive,
    std::uint64_t resultGesture, std::uint64_t activeGesture) {
    // A completed first frame is still useful after a cold start or queue
    // stall. Source, viewport and monotonic generation gates remain separate.
    return resultInteractive && gestureActive && resultGesture == activeGesture;
}

// Estimate pixel cost at viewport resolution, independent of the resolution
// of the sample. This avoids repeatedly shrinking from an old, larger frame.
inline double RawViewportEquivalentRenderMs(double renderMs, float sampleScale, double fixedMs = 0.0) {
    if (!std::isfinite(renderMs) || renderMs <= 0.0 ||
        !std::isfinite(sampleScale) || sampleScale <= 0.0f) return 0.0;
    fixedMs = std::clamp(fixedMs, 0.0, renderMs);
    return fixedMs + (renderMs - fixedMs) / (sampleScale * sampleScale);
}

inline float RawPreviewScaleForCost(double viewportRenderMs, float maximumScale = 1.0f,
    int targetFps = Raw::kDefaultViewportTargetFps, double fixedMs = 0.0) {
    if (!std::isfinite(viewportRenderMs) || viewportRenderMs <= 0.0) return 1.0f;
    fixedMs = std::clamp(fixedMs, 0.0, viewportRenderMs);
    const double budget = std::max(1000.0 / Raw::ClampViewportTargetFps(targetFps), fixedMs * 1.05);
    if (viewportRenderMs <= fixedMs) return std::max(1.0f, maximumScale);
    return std::clamp(static_cast<float>(std::sqrt(
        (budget - fixedMs) / (viewportRenderMs - fixedMs))), kRawPreviewMinimumScale, std::max(1.0f, maximumScale));
}

// The user changing the FPS target explicitly changes the pixel budget.
inline float RawPreviewScaleAfterTargetChange(float currentScale,
    double viewportRenderMs, float maximumScale, int targetFps) {
    if (!std::isfinite(viewportRenderMs) || viewportRenderMs <= 0.0)
        return currentScale;
    return RawPreviewScaleForCost(viewportRenderMs, maximumScale, targetFps);
}

inline bool ShouldLearnRawSteadyRenderCost(bool firstPresentation,
    bool evaluatedImageWork) {
    return !firstPresentation && evaluatedImageWork;
}

// A resolution change invalidates some caches. Give it time to settle before
// interpreting its transient cost as evidence for another change.
inline float StabilizeRawPreviewScale(float current, float desired,
    int& healthySamples, int& slowSamples, int& cooldownSamples,
    bool allowIncrease = true) {
    if (cooldownSamples > 0) {
        --cooldownSamples;
        healthySamples = slowSamples = 0;
        return current;
    }
    if (desired < current * 0.88f) {
        healthySamples = 0;
        if (++slowSamples >= 2) {
            slowSamples = 0;
            cooldownSamples = 2;
            return std::max(desired, current * 0.5f);
        }
    } else if (desired > current * 1.20f) {
        slowSamples = 0;
        healthySamples = std::min(4, healthySamples + 1);
        if (healthySamples >= 4 && allowIncrease) {
            healthySamples = 0;
            cooldownSamples = 2;
            return std::min(desired, current * 1.35f);
        }
    } else {
        healthySamples = slowSamples = 0;
    }
    return current;
}

inline bool ShouldRequestRawRefinementOnRelease(
    bool gestureWasActive, bool interactionActive, bool presentationReady) {
    return gestureWasActive && !interactionActive && presentationReady;
}

inline float ClampRawInteractivePreviewScale(float scale, float maximumScale = 1.0f) {
    return std::clamp(scale, kRawPreviewMinimumScale, std::max(1.0f, maximumScale));
}

inline double UpdateRawInteractiveFrameTimeEma(
    double currentEmaMs,
    double measuredFrameMs) {
    if (!std::isfinite(measuredFrameMs) || measuredFrameMs <= 0.0) {
        return std::max(0.0, currentEmaMs);
    }
    const double sample = std::clamp(measuredFrameMs, 1.0, 10000.0);
    if (!std::isfinite(currentEmaMs) || currentEmaMs <= 0.0) {
        return sample;
    }
    constexpr double kNewSampleWeight = 0.25;
    return currentEmaMs * (1.0 - kNewSampleWeight) +
        sample * kNewSampleWeight;
}

inline float ReduceRawInteractivePreviewScaleForFrameTime(
    float currentScale,
    double frameTimeEmaMs) {
    const float scale = ClampRawInteractivePreviewScale(currentScale);
    // Leave a generous dead band around 30 fps so harmless timing noise does
    // not make the raster breathe. Below roughly 25 fps, reduce the linear
    // edge by the square root of the time ratio because render cost follows
    // pixel area. Crossing the 20 fps floor permits a more decisive step.
    constexpr double kReductionThresholdMs = 40.0;
    if (!std::isfinite(frameTimeEmaMs) ||
        frameTimeEmaMs <= kReductionThresholdMs) {
        return scale;
    }
    const double rawRatio = std::sqrt(
        kRawPreviewTargetFrameMs / frameTimeEmaMs);
    const double minimumRatio =
        frameTimeEmaMs >= kRawPreviewMinimumFrameMs ? 0.65 : 0.78;
    const double maximumRatio =
        frameTimeEmaMs >= kRawPreviewMinimumFrameMs ? 0.90 : 0.96;
    return ClampRawInteractivePreviewScale(
        scale * static_cast<float>(std::clamp(
            rawRatio,
            minimumRatio,
            maximumRatio)));
}

inline float ReduceRawInteractivePreviewScaleForBacklog(
    float currentScale) {
    return ClampRawInteractivePreviewScale(currentScale * 0.85f);
}

inline bool IsRawInteractiveFrameTimeHealthy(double frameTimeEmaMs) {
    return std::isfinite(frameTimeEmaMs) &&
        frameTimeEmaMs > 0.0 &&
        frameTimeEmaMs <= kRawPreviewTargetFrameMs;
}

inline bool ShouldIncludeRawInteractiveCadenceSample(
    double acceptedCommandGapMs,
    double acceptedPresentationGapMs) {
    // Adapt to steady presentation cadence, not pointer-down startup latency
    // or a user pausing while still holding the control. The first accepted
    // frame has no prior accepted command/presentation and is therefore
    // always warm-up. Steady processing cost is measured independently of
    // queue and presentation latency.
    constexpr double kMaximumContinuousCommandGapMs = 100.0;
    constexpr double kMaximumUncontendedPresentationGapMs = 250.0;
    return std::isfinite(acceptedCommandGapMs) &&
        std::isfinite(acceptedPresentationGapMs) &&
        acceptedCommandGapMs > 0.0 &&
        acceptedCommandGapMs <= kMaximumContinuousCommandGapMs &&
        acceptedPresentationGapMs > 0.0 &&
        acceptedPresentationGapMs <=
            kMaximumUncontendedPresentationGapMs;
}

inline float RaiseRawInteractivePreviewScale(float currentScale) {
    return ClampRawInteractivePreviewScale(currentScale + 0.0625f);
}

inline bool ShouldStartRawNativeRefinement(
    bool refinementPending,
    bool refinementRequested,
    bool presentationReady,
    bool interactionActive,
    bool refinementQuietElapsed,
    bool renderBackendIdle) {
    // Never supersede the only render capable of filling an empty viewport.
    // Once one truthful presentation exists, release/native work may begin
    // immediately and remains cancelable by newer foreground interaction.
    return refinementPending &&
        !refinementRequested &&
        presentationReady &&
        !interactionActive &&
        refinementQuietElapsed &&
        renderBackendIdle;
}

inline bool ShouldStartRawAnalysisAfterPreviewSettles(
    bool analysisPending,
    bool analysisRequested,
    bool presentationReady,
    bool interactionActive,
    bool analysisQuietElapsed) {
    // A scope is a compact interpretation of the display preview, not a
    // prerequisite for native presentation. Run it once the visible preview
    // has settled so a slow or memory-deferred native render cannot leave the
    // graph empty.
    return analysisPending &&
        !analysisRequested &&
        presentationReady &&
        !interactionActive &&
        analysisQuietElapsed;
}

enum class RawNativeRefinementCompletion : std::uint8_t {
    None = 0,
    NativePresentation,
    BudgetFallback,
    ExtentMismatch,
};

struct RawPresentationExtent {
    int width = 0;
    int height = 0;

    bool IsKnown() const {
        return width > 0 && height > 0;
    }
};

inline RawPresentationExtent ResolveExpectedRawPresentationExtent(
    int fullFrameWidth,
    int fullFrameHeight,
    int manualRotationDegrees,
    bool cropEnabled,
    float cropX,
    float cropY,
    float cropWidth,
    float cropHeight) {
    RawPresentationExtent extent;
    if (fullFrameWidth <= 0 || fullFrameHeight <= 0) {
        return extent;
    }

    const int normalizedRotation =
        ((manualRotationDegrees % 360) + 360) % 360;
    const bool swapsDimensions =
        normalizedRotation == 90 || normalizedRotation == 270;
    extent.width = swapsDimensions ? fullFrameHeight : fullFrameWidth;
    extent.height = swapsDimensions ? fullFrameWidth : fullFrameHeight;

    if (!cropEnabled) {
        return extent;
    }

    const float clampedX = std::clamp(cropX, 0.0f, 1.0f);
    const float clampedY = std::clamp(cropY, 0.0f, 1.0f);
    const float clampedWidth =
        std::clamp(cropWidth, 0.0f, 1.0f - clampedX);
    const float clampedHeight =
        std::clamp(cropHeight, 0.0f, 1.0f - clampedY);
    const int left = std::clamp(
        static_cast<int>(std::floor(
            clampedX * static_cast<float>(extent.width))),
        0,
        extent.width - 1);
    const int top = std::clamp(
        static_cast<int>(std::floor(
            clampedY * static_cast<float>(extent.height))),
        0,
        extent.height - 1);
    const int right = std::clamp(
        static_cast<int>(std::ceil(
            (clampedX + clampedWidth) *
            static_cast<float>(extent.width))),
        left + 1,
        extent.width);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(
            (clampedY + clampedHeight) *
            static_cast<float>(extent.height))),
        top + 1,
        extent.height);
    extent.width = right - left;
    extent.height = bottom - top;
    return extent;
}

inline bool IsRawPresentationNativeExtent(
    int actualWidth,
    int actualHeight,
    int expectedWidth,
    int expectedHeight) {
    return actualWidth > 0 && actualHeight > 0 &&
        expectedWidth > 0 && expectedHeight > 0 &&
        actualWidth == expectedWidth && actualHeight == expectedHeight;
}

inline int ResolveRawPresentationPreviewMaxDimension(
    int requestedPreviewMaxDimension,
    int actualWidth,
    int actualHeight,
    int expectedWidth,
    int expectedHeight) {
    if (IsRawPresentationNativeExtent(
            actualWidth,
            actualHeight,
            expectedWidth,
            expectedHeight)) {
        return 0;
    }
    if (requestedPreviewMaxDimension > 0) {
        return requestedPreviewMaxDimension;
    }
    return std::max(actualWidth, actualHeight);
}

inline RawNativeRefinementCompletion ClassifyRawNativeRefinementCompletion(
    RawRenderPurpose purpose,
    int previewMaxDimension,
    bool fullFrameRefinementRequested,
    bool fullFrameDimensionsKnown,
    bool fullFrameRefinementBudgetAllowed,
    bool presentationAdopted,
    bool nativeExtentVerified,
    bool renderDirty) {
    const bool viewportRefinement =
        purpose == RawRenderPurpose::ViewportRefinement;
    const bool explicitInspection =
        purpose == RawRenderPurpose::ExplicitInspection;
    if ((!viewportRefinement && !explicitInspection) ||
        !fullFrameRefinementRequested ||
        !presentationAdopted ||
        renderDirty) {
        return RawNativeRefinementCompletion::None;
    }
    if (previewMaxDimension == 0 &&
        fullFrameRefinementBudgetAllowed &&
        nativeExtentVerified &&
        viewportRefinement) {
        return RawNativeRefinementCompletion::NativePresentation;
    }
    if (previewMaxDimension == 0 &&
        fullFrameRefinementBudgetAllowed &&
        !nativeExtentVerified &&
        viewportRefinement) {
        return RawNativeRefinementCompletion::ExtentMismatch;
    }
    if (previewMaxDimension > 0 &&
        fullFrameDimensionsKnown &&
        !fullFrameRefinementBudgetAllowed) {
        return RawNativeRefinementCompletion::BudgetFallback;
    }
    return RawNativeRefinementCompletion::None;
}

inline bool RawRenderResultOwnsTrackedRequest(
    RawRenderPurpose expectedPurpose,
    RawRenderPurpose resultPurpose,
    std::uint64_t resultGeneration,
    std::uint64_t trackedRequestGeneration) {
    return resultPurpose == expectedPurpose &&
        trackedRequestGeneration != 0 &&
        resultGeneration == trackedRequestGeneration;
}

inline bool IsRawNativePresentationCurrent(
    int previewMaxDimension,
    std::uint64_t displayedGeneration,
    std::uint64_t latestPresentationGeneration,
    bool renderDirty) {
    // Auxiliary RAW commands use their own generation lane and must not make
    // an already-adopted native presentation look stale. A newer accepted
    // presentation command does, because it owns the next visible result.
    return previewMaxDimension == 0 &&
        displayedGeneration != 0 &&
        displayedGeneration == latestPresentationGeneration &&
        !renderDirty;
}

inline bool ShouldRearmRawNativeRefinementAfterAcceptedPresentation(
    int previewMaxDimension,
    RawNativeRefinementCompletion refinementCompletion,
    bool presentationAdopted) {
    // A display-sized presentation is never terminal. Ordinary UI refreshes
    // may legitimately replace a native texture with a proxy, but doing so
    // must restore the one-way proxy -> native refinement transition. A
    // memory-budget fallback from the tracked native request is handled by
    // the separate deferred-resource state instead of this generic re-arm.
    return presentationAdopted &&
        previewMaxDimension > 0 &&
        refinementCompletion != RawNativeRefinementCompletion::BudgetFallback &&
        refinementCompletion != RawNativeRefinementCompletion::ExtentMismatch;
}

inline bool ShouldRearmUnsubmittedRawNativeRefinement(
    bool refinementPending,
    bool refinementRequested,
    std::uint64_t trackedRequestGeneration,
    bool renderDirty,
    bool renderPending,
    bool renderBackendBusy) {
    // Requested with generation zero is only the scheduler's pre-submission
    // state. If no dirty work and no backend can still accept that request,
    // return it to Pending instead of leaving refinement permanently armed.
    return refinementPending &&
        refinementRequested &&
        trackedRequestGeneration == 0 &&
        !renderDirty &&
        !renderPending &&
        !renderBackendBusy;
}

inline bool ShouldRetryRawNativeRefinementAfterFailure(
    int completedRetryCount,
    int maximumRetryCount) {
    return completedRetryCount >= 0 &&
        completedRetryCount < maximumRetryCount;
}

inline bool ShouldResumeRawNativeRefinementAfterBudgetChange(
    bool refinementDeferredByBudget,
    std::uint64_t deferredWorkingBudgetBytes,
    std::uint64_t currentWorkingBudgetBytes,
    bool minimumMemoryTiling) {
    return refinementDeferredByBudget &&
        !minimumMemoryTiling &&
        currentWorkingBudgetBytes > deferredWorkingBudgetBytes;
}

inline bool ShouldCountRawPreviewSupersession(
    bool interactivePresentation,
    bool telemetrySuperseded,
    bool workerStarted) {
    // Pending snapshots coalesced before worker execution are normal UI-frame
    // batching, not evidence that the GPU cannot sustain the current tier.
    return interactivePresentation && telemetrySuperseded && workerStarted;
}

inline std::uint64_t NextGlobalGeneration() {
    static std::atomic<std::uint64_t> generation { 1 };
    return generation.fetch_add(1, std::memory_order_relaxed);
}

// Legacy compatibility query retained for older call sites. Submit each UI
// frame's latest state; RawRenderService owns active-frame completion and
// coalesces the pending values without blocking input on the UI thread.
inline bool DeferNewInteractivePreviewWhileFrameIsRendering(
    bool rawWorkspaceActive,
    bool interactivePreviewActive,
    bool renderBackendBusy,
    bool renderDirty) {
    (void)rawWorkspaceActive;
    (void)interactivePreviewActive;
    (void)renderBackendBusy;
    (void)renderDirty;
    return false;
}

// Cover thumbnails are rebuildable cache data, but acquiring them performs a
// synchronous GPU readback. Never do that for an interactive proxy frame;
// wait for a settled analysis/full-quality result instead.
inline bool ShouldRefreshRawProjectCover(
    bool completedExplicitFullQuality,
    bool completedSettledDisplayPreview,
    bool renderDirty) {
    return !renderDirty &&
        (completedExplicitFullQuality || completedSettledDisplayPreview);
}

// Project adoption is complete once the render revision requested by the
// loader has produced a newly accepted presentation. Lower-priority RAW work
// may already be queued at that point, so global worker-idle state is not a
// valid part of this gate.
inline bool ShouldAdvanceProjectLoadAfterFirstPresentation(
    bool outputConnected,
    std::uint64_t targetRenderRevision,
    std::uint64_t lastSubmittedRenderRevision,
    std::uint64_t acceptedGenerationAtStart,
    std::uint64_t lastAcceptedGeneration,
    bool presentationReady) {
    if (!outputConnected) {
        return true;
    }
    return targetRenderRevision != 0 &&
        lastSubmittedRenderRevision >= targetRenderRevision &&
        lastAcceptedGeneration > acceptedGenerationAtStart &&
        presentationReady;
}

// Node-browser thumbnails are rebuildable background cache entries. Their
// completion must never hold the project-loading modal open after the first
// usable frame has been presented.
inline bool ShouldBlockProjectReadyForOptionalThumbnails(
    std::size_t warmPending,
    std::size_t renderPending) {
    (void)warmPending;
    (void)renderPending;
    return false;
}

inline bool AcceptSubmission(
    std::uint64_t generation,
    bool stopRequested,
    std::uint64_t invalidBeforeGeneration,
    std::uint64_t latestSubmittedGeneration) {
    return !stopRequested &&
        generation >= invalidBeforeGeneration &&
        generation >= latestSubmittedGeneration;
}

// A generation becomes the newest RAW result gate only after the owning
// worker/service has accepted the submission. Keeping this commit conditional
// prevents a rejected submission from making the last valid presentation look
// stale while no replacement render exists.
inline void CommitRawSubmissionGeneration(
    bool submitted,
    bool mayPublishPresentation,
    std::uint64_t generation,
    std::uint64_t& latestPresentationGeneration,
    std::uint64_t& latestAuxiliaryGeneration) {
    if (!submitted) {
        return;
    }
    if (mayPublishPresentation) {
        latestPresentationGeneration = generation;
        latestAuxiliaryGeneration = std::max(
            latestAuxiliaryGeneration,
            generation);
    } else {
        latestAuxiliaryGeneration = generation;
    }
}

inline bool DiscardCompletedResult(
    std::uint64_t generation,
    bool stopRequested,
    std::uint64_t invalidBeforeGeneration,
    std::uint64_t latestSubmittedGeneration,
    bool carriesCancellationAcknowledgement) {
    if (stopRequested) {
        return true;
    }
    const bool stale =
        generation < invalidBeforeGeneration ||
        generation < latestSubmittedGeneration;
    return stale && !carriesCancellationAcknowledgement;
}

} // namespace Stack::EditorRenderScheduling
