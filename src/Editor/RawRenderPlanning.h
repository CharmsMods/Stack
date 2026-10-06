#pragma once

#include "Editor/RawRenderPurpose.h"
#include "Raw/RawGpuMemoryBudget.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace Stack::EditorRendering {

struct RawRenderPlanInput {
    bool exportRequested = false;
    bool targetAuxiliary = false;
    bool explicitInspectionRequested = false;
    bool fullResolutionPreviewRequested = false;
    bool fullResolutionDiagnosticRequested = false;
    bool proxyResolutionDiagnosticRequested = false;
    bool analysisRequested = false;
    bool complexNodePreview = false;
    int interactiveTargetEdge = 0;
    int complexNodeTargetEdge = 0;
    int analysisTargetEdge = 0;
    int fullFrameWidth = 0;
    int fullFrameHeight = 0;
    int maximumTextureSize = 0;
    std::uint64_t workingBudgetBytes = 0;
    bool forceMinimumMemoryTiling = false;
    double visibleAreaFraction = 1.0;
    int fullInputSurfaces = 7;
    int surfaceBytesPerPixel = 8;
};

struct RawRenderPlan {
    RawRenderPurpose purpose =
        RawRenderPurpose::InteractivePresentation;
    int targetEdge = 0;
    Raw::RawFullFramePreviewDecision fullFrameDecision;
    std::uint64_t activeWorkingSetBytes = 0;
    std::uint64_t cacheBudgetBytes = 0;
    std::uint64_t minimumRawStageCacheBytes = 0;
    bool fullFrameRequested = false;
    bool nativePresentationAllowed = false;
    bool disableViewportTiling = false;
};

inline RawRenderPurpose ResolveRawRenderPurpose(
    const RawRenderPlanInput& input) {
    if (input.exportRequested) {
        return RawRenderPurpose::ExplicitExport;
    }
    if (input.fullResolutionDiagnosticRequested) {
        return RawRenderPurpose::ExplicitInspection;
    }
    if (input.proxyResolutionDiagnosticRequested) {
        return RawRenderPurpose::InteractivePresentation;
    }
    if (input.targetAuxiliary) {
        return RawRenderPurpose::InteractiveSample;
    }
    if (input.explicitInspectionRequested) {
        return RawRenderPurpose::ExplicitInspection;
    }
    if (input.fullResolutionPreviewRequested) {
        return RawRenderPurpose::ViewportRefinement;
    }
    if (input.analysisRequested) {
        return RawRenderPurpose::AnalysisScopes;
    }
    return RawRenderPurpose::InteractivePresentation;
}

inline std::uint64_t EstimateRawRenderWorkingSetBytes(
    int fullWidth,
    int fullHeight,
    int targetEdge,
    int surfaceBytesPerPixel = 8) {
    if (fullWidth <= 0 || fullHeight <= 0) {
        return 0;
    }
    int width = fullWidth;
    int height = fullHeight;
    const int longestSide = std::max(width, height);
    if (targetEdge > 0 && longestSide > targetEdge) {
        width = std::max(1, static_cast<int>(
            (static_cast<long long>(width) * targetEdge + longestSide / 2) /
            longestSide));
        height = std::max(1, static_cast<int>(
            (static_cast<long long>(height) * targetEdge + longestSide / 2) /
            longestSide));
    }

    // Six graph/stage surfaces plus one accepted presentation copy.
    const std::uint64_t kProtectedBytesPerPixel = 7u * static_cast<std::uint64_t>(std::max(1,surfaceBytesPerPixel));
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);
    return pixels > std::numeric_limits<std::uint64_t>::max() /
            kProtectedBytesPerPixel
        ? std::numeric_limits<std::uint64_t>::max()
        : pixels * kProtectedBytesPerPixel;
}

inline std::uint64_t ResolveRawRenderCacheBudgetBytes(
    std::uint64_t workingBudgetBytes,
    std::uint64_t activeWorkingSetBytes) {
    if (workingBudgetBytes == 0) {
        return 0;
    }
    constexpr std::uint64_t kLegacyCombinedCacheCeiling =
        (512ull + 512ull + 256ull) * 1024ull * 1024ull;
    const std::uint64_t headroom =
        activeWorkingSetBytes < workingBudgetBytes
        ? workingBudgetBytes - activeWorkingSetBytes
        : 0;
    // Cache residency may use at most two thirds of the same working budget.
    // The remainder stays protected for decoder/source resources that are not
    // represented by the RGBA16F graph estimate above.
    const std::uint64_t cacheShare =
        (workingBudgetBytes / 3u) * 2u;
    return std::min({ headroom, cacheShare, kLegacyCombinedCacheCeiling });
}

inline RawRenderPlan BuildRawRenderPlan(
    const RawRenderPlanInput& input) {
    RawRenderPlan plan;
    plan.purpose = ResolveRawRenderPurpose(input);
    plan.fullFrameRequested =
        plan.purpose == RawRenderPurpose::ExplicitInspection ||
        plan.purpose == RawRenderPurpose::ViewportRefinement;
    if (plan.fullFrameRequested) {
        plan.fullFrameDecision = Raw::ResolveRawFullFramePreviewDecision(
            input.fullFrameWidth,
            input.fullFrameHeight,
            input.maximumTextureSize,
            input.workingBudgetBytes,
            input.forceMinimumMemoryTiling,
            input.surfaceBytesPerPixel);
        if (plan.purpose == RawRenderPurpose::ViewportRefinement && input.visibleAreaFraction < 1.0) {
            // Full-input stages still need complete rasters. Only downstream
            // surfaces and publication shrink to the validated visible region.
            const double full = std::clamp(input.fullInputSurfaces,2,7);
            const double surfaces = full+(7.0-full)*std::clamp(input.visibleAreaFraction,0.0,1.0);
            plan.fullFrameDecision.estimatedWorkingSetBytes = static_cast<std::uint64_t>(
                plan.fullFrameDecision.estimatedWorkingSetBytes*(surfaces/7.0));
            plan.fullFrameDecision.fitsWorkingBudget = input.workingBudgetBytes>0 &&
                plan.fullFrameDecision.estimatedWorkingSetBytes<=input.workingBudgetBytes;
            plan.fullFrameDecision.allowed = !input.forceMinimumMemoryTiling &&
                plan.fullFrameDecision.fitsTextureLimits && plan.fullFrameDecision.fitsWorkingBudget;
        }
        plan.nativePresentationAllowed = plan.fullFrameDecision.allowed;
        plan.disableViewportTiling =
            plan.nativePresentationAllowed ||
            input.fullResolutionDiagnosticRequested;
    }

    if (input.fullResolutionDiagnosticRequested) {
        // A denoise diagnostic is evidence about individual source pixels.
        // Never substitute a scaled proxy when the native attempt exceeds the
        // ordinary settled-preview budget; failure is preferable to a mask
        // whose brightness and coverage describe different pixels.
        plan.targetEdge = 0;
    } else if (plan.purpose == RawRenderPurpose::ExplicitExport) {
        // Export has its own authoritative tiling path.
        plan.targetEdge = 0;
    } else if (input.proxyResolutionDiagnosticRequested) {
        // A preview-quality diagnostic shares the image preview's exact target
        // edge. It must not introduce an independent mask resolution.
        plan.targetEdge = std::max(0, input.interactiveTargetEdge);
    } else if (plan.purpose == RawRenderPurpose::AnalysisScopes && input.analysisTargetEdge > 0) {
        plan.targetEdge = std::min(input.analysisTargetEdge,std::max(input.fullFrameWidth,input.fullFrameHeight));
    } else if (input.complexNodePreview) {
        plan.targetEdge = std::max(0, input.complexNodeTargetEdge);
    } else if (plan.fullFrameRequested && plan.nativePresentationAllowed) {
        plan.targetEdge = 0;
    } else {
        plan.targetEdge = std::max(0, input.interactiveTargetEdge);
    }

    plan.activeWorkingSetBytes = EstimateRawRenderWorkingSetBytes(
        input.fullFrameWidth,
        input.fullFrameHeight,
        plan.targetEdge,
        input.surfaceBytesPerPixel);
    if (plan.purpose == RawRenderPurpose::ViewportRefinement && plan.nativePresentationAllowed)
        plan.activeWorkingSetBytes = plan.fullFrameDecision.estimatedWorkingSetBytes;
    // Cache only from verified headroom left after the active render working
    // set. Native RAW boundaries are particularly valuable: retaining the
    // current CFA-denoised RawBase lets downstream tone/color/view edits avoid
    // repeating expensive sensor-domain work. Resolution is part of every
    // stage fingerprint, so proxy entries can never satisfy a native lookup.
    plan.cacheBudgetBytes = ResolveRawRenderCacheBudgetBytes(
        input.workingBudgetBytes,
        plan.activeWorkingSetBytes);
    // One stage is one seventh of the protected render estimate.
    // Carry this reservation explicitly so the renderer's internal cache
    // partition cannot accidentally make upstream RAW reuse impossible.
    plan.minimumRawStageCacheBytes = std::min(
        plan.cacheBudgetBytes,
        EstimateRawRenderWorkingSetBytes(input.fullFrameWidth,input.fullFrameHeight,plan.targetEdge,input.surfaceBytesPerPixel) / 7u);
    return plan;
}

inline bool ShouldAdoptRawPresentation(
    std::uint64_t resultGeneration,
    std::size_t resultRecipeRevision,
    std::uint64_t latestRequestedGeneration,
    std::size_t latestRequestedRecipeRevision,
    std::uint64_t displayedGeneration,
    bool allowActiveGestureIntermediate = false,
    bool sameGenerationUpgrade = false) {
    if (allowActiveGestureIntermediate) {
        // Interactive scheduling intentionally lets the active frame finish
        // while retaining one newest pending value. Publishing that completed
        // frame keeps the viewport moving without permitting an older result
        // to replace a newer displayed generation.
        return resultRecipeRevision != 0 &&
            resultGeneration > displayedGeneration &&
            resultGeneration <= latestRequestedGeneration;
    }
    return resultRecipeRevision == latestRequestedRecipeRevision &&
        resultGeneration >= latestRequestedGeneration &&
        (resultGeneration > displayedGeneration ||
         (sameGenerationUpgrade &&
          resultGeneration == displayedGeneration));
}

} // namespace Stack::EditorRendering
