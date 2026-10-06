#include "Renderer/ViewportTextureCopy.h"
#include "Editor/EditorModule.h"

#include "App/settings/AppearanceTheme.h"
#include "Async/TaskSystem.h"
#include "Editor/Layers/ToneLayers.h"
#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/RawRenderPlanning.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawGpuMemoryBudget.h"
#include "Raw/RawLoader.h"
#include "Raw/RawProcessingMath.h"
#include "Raw/RawViewportDetail.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/ScopedGLObjects.h"
#include "Utils/PixelBufferUtils.h"
#include "Utils/PngEncodingUtils.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "Editor/Internal/EditorRenderPresentationHelpers.h"
using namespace Stack::EditorRendering::Internal;

void EditorModule::ConsumeRenderWorkerResults() {
    // Expire retained frames even while the Raw viewport is hidden.
    if (m_RawViewportFadeDuration > 0 && ImGui::GetCurrentContext() &&
        ((m_RawViewportFadeStarted >= 0 && ImGui::GetTime()-m_RawViewportFadeStarted >= m_RawViewportFadeDuration) ||
         (m_RawViewportFadeStarted < 0 && ImGui::GetTime()-m_RawViewportPresentationTime > 1.0)))
        ClearRawViewportTransition();
    PumpViewportOutputTextureDeletes();
    PumpViewportOutputTileTextureDeletes();

    auto releaseDeferredResultResources = [this](EditorRenderWorker::Result& result) {
        QueueViewportOutputTextureRelease(result.outputTexture);
        QueueViewportOutputTileSetRelease(result.outputTiles);
        QueueViewportOutputTextureRelease(
            result.rawWorkspace.localRangeOverlayTexture);
    };
    auto deferResultUntilReady = [&](EditorRenderWorker::Result&& result) {
        for (EditorRenderWorker::Result& pending : m_DeferredRenderResults) {
            releaseDeferredResultResources(pending);
        }
        m_DeferredRenderResults.clear();
        try {
            m_DeferredRenderResults.push_back(std::move(result));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(result);
        } catch (const std::length_error&) {
            releaseDeferredResultResources(result);
        }
    };

    std::deque<EditorRenderWorker::Result> resultsToProcess;
    while (!m_DeferredRenderResults.empty()) {
        try {
            resultsToProcess.push_back(
                std::move(m_DeferredRenderResults.front()));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(
                m_DeferredRenderResults.front());
        } catch (const std::length_error&) {
            releaseDeferredResultResources(
                m_DeferredRenderResults.front());
        }
        m_DeferredRenderResults.pop_front();
    }

    EditorRenderWorker::Result result;
    while (m_RenderWorker.TryConsumeCompleted(result)) {
        try {
            resultsToProcess.push_back(std::move(result));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(result);
        } catch (const std::length_error&) {
            releaseDeferredResultResources(result);
        }
    }
    while (m_RawRenderClientId != 0 &&
           Stack::EditorRendering::RawRenderService::Get()
               .TryConsumeCompleted(m_RawRenderClientId, result)) {
        try {
            resultsToProcess.push_back(std::move(result));
        } catch (const std::bad_alloc&) {
            releaseDeferredResultResources(result);
        } catch (const std::length_error&) {
            releaseDeferredResultResources(result);
        }
    }

    while (m_RenderWorker.TryConsumeViewportTiming(result)) ObserveRawViewportTiming(result);
    while (m_RawRenderClientId && Stack::EditorRendering::RawRenderService::Get().TryConsumeViewportTiming(m_RawRenderClientId,result))
        ObserveRawViewportTiming(result);
    while (!resultsToProcess.empty()) {
        EditorRenderWorker::Result result = std::move(resultsToProcess.front());
        resultsToProcess.pop_front();

        try {
        if (result.outputTexture.texture != 0) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(result.outputTexture.readyFence, fenceFailed)) {
                if (fenceFailed) {
                    QueueViewportOutputTextureRelease(result.outputTexture);
                    result.success = false;
                    result.error = "Output synchronization failed.";
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (!result.outputTiles.tiles.empty()) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(result.outputTiles.readyFence, fenceFailed)) {
                if (fenceFailed) {
                    QueueViewportOutputTileSetRelease(result.outputTiles);
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (result.rawWorkspace.localRangeOverlayTexture.texture != 0) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(
                    result.rawWorkspace.localRangeOverlayTexture.readyFence,
                    fenceFailed)) {
                if (fenceFailed) {
                    QueueViewportOutputTextureRelease(
                        result.rawWorkspace.localRangeOverlayTexture);
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (result.telemetry.snapshotReadyAt.time_since_epoch().count() != 0) {
            result.telemetry.uiAdoptionMs = MillisecondsBetween(
                result.telemetry.snapshotReadyAt,
                std::chrono::steady_clock::now());
        }
        if (result.timingOnly) { ObserveRawViewportTiming(result); continue; }
        if (result.rawRenderPurpose == RawRenderPurpose::ViewportOverview ||
            result.generation == m_RawViewportMaintenanceGeneration) {
            ObserveRawViewportTiming(result);
            AdoptRawViewportMaintenance(result);
            releaseDeferredResultResources(result);
            m_RenderPending = IsAnyRenderBackendBusy();
            continue;
        }
        if (result.rawRenderPurpose == RawRenderPurpose::ViewportCalibration) {
            AdoptRawViewportCalibration(result);
            releaseDeferredResultResources(result);
            m_RenderPending = IsAnyRenderBackendBusy();
            continue;
        }
        ObserveRawViewportTiming(result);
        const bool rawResult = !result.rawWorkspace.sourceKey.empty();
        if (rawResult &&
            result.rawRenderPurpose == RawRenderPurpose::CachePrewarm) {
            ObserveRawViewportTiming(result);
            AdoptRawWorkspaceCachePrewarmResult(result);
            releaseDeferredResultResources(result);
            m_RenderPending = IsAnyRenderBackendBusy();
            continue;
        }
        const bool rawPresentationResult = rawResult &&
            RawRenderPurposeMayPublishPresentation(
                result.rawRenderPurpose);
        const std::uint64_t newestMatchingRawGeneration =
            rawPresentationResult
                ? m_LatestRawPresentationGeneration
                : m_LatestRawAuxiliaryGeneration;
        const std::size_t newestMatchingRawRecipeRevision =
            rawPresentationResult
                ? m_LatestRawPresentationRecipeRevision
                : m_LatestRawAuxiliaryRecipeRevision;
        const bool activeGestureIntermediate =
            rawPresentationResult &&
            result.success &&
            !result.telemetry.superseded &&
            result.rawRenderPurpose ==
                RawRenderPurpose::InteractivePresentation &&
            Stack::EditorRenderScheduling::IsRawGestureIntermediate(
                result.telemetry.interactionActive,
                m_RawWorkspaceAdaptiveGestureActive,
                result.telemetry.gestureId, m_RawViewportGestureId);
        const bool currentRawGeneration = !rawResult ||
            (rawPresentationResult
                ? Stack::EditorRendering::ShouldAdoptRawPresentation(
                      result.generation,
                      result.rawWorkspace.recipeRevision,
                      newestMatchingRawGeneration,
                      newestMatchingRawRecipeRevision,
                      m_ViewportOutputRenderGeneration,
                      activeGestureIntermediate,
                      result.rawRenderPurpose ==
                          RawRenderPurpose::DenoiseCompletion)
                : result.generation >= newestMatchingRawGeneration);
        const bool currentRawRecipe = !rawResult ||
            rawPresentationResult ||
            result.rawWorkspace.recipeRevision ==
                newestMatchingRawRecipeRevision;
        // A changed view does not obsolete native pixels that cover its new
        // bounds. Source, recipe and worker-generation checks still apply.
        const bool nativeResultCoversView = rawPresentationResult && result.success &&
            !result.graphRequest.enabled && !result.rawWorkspace.viewportDiagnostic &&
            result.rawRenderPurpose != RawRenderPurpose::ExplicitInspection &&
            Raw::ViewportDetailCoversNativeRegion(result.rawWorkspace.viewportRegion,
                result.outputTexture.width, result.outputTexture.height, m_RawViewportRequest.visible);
        const bool obsoleteViewport = rawPresentationResult && !result.graphRequest.enabled &&
            result.rawWorkspace.viewportGeneration != m_RawViewportRequest.generation && !nativeResultCoversView;
        if (obsoleteViewport) result.telemetry.skipReason = RawPreviewSkipReason::ObsoleteView;
        const bool staleResult = result.graphRequest.enabled
            ? !IsCurrentGraphResult(result) || result.telemetry.superseded
            : (rawResult
                ? !currentRawGeneration || !currentRawRecipe || obsoleteViewport ||
                    (!m_RawWorkspaceRootTabActive && result.rawRenderPurpose == RawRenderPurpose::InteractivePresentation)
                : !IsCurrentGraphResult(result));
        const bool ownedRawNativeRefinementResult =
            Stack::EditorRenderScheduling::
                RawRenderResultOwnsTrackedRequest(
                    RawRenderPurpose::ViewportRefinement,
                    result.rawRenderPurpose,
                    result.generation,
                    m_RawWorkspaceFullResolutionPreviewRequestGeneration);
        if (ownedRawNativeRefinementResult &&
            (staleResult || result.telemetry.superseded ||
             result.error == "Render superseded by a newer snapshot.")) {
            // The service may replace native work with a newer foreground
            // sample, edit, project load, or inspection. The superseded
            // generation no longer owns any worker slot, so return the
            // logical request to Pending instead of waiting forever for a
            // completion that cannot arrive.
            m_RawWorkspaceFullResolutionPreviewPending = true;
            m_RawWorkspaceFullResolutionPreviewRequested = false;
            m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
            m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
            m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
            m_RawWorkspaceFastPreviewUntilTime =
                ImGui::GetCurrentContext() != nullptr
                ? ImGui::GetTime() + 0.20
                : -1.0;
        }
        if (staleResult) {
            // Discarding the pixels still completes this worker request.
            // A stale last result must not leave RAW permanently busy.
            m_RenderPending = IsAnyRenderBackendBusy();
            if (Stack::EditorRenderScheduling::
                    ShouldCountRawPreviewSupersession(
                        rawPresentationResult &&
                            result.rawRenderPurpose ==
                                RawRenderPurpose::InteractivePresentation &&
                            result.telemetry.interactionActive &&
                            m_RawWorkspaceAdaptiveGestureActive && result.telemetry.overloadSkipped,
                        result.telemetry.superseded,
                        result.telemetry.workerStarted)) {
                ++m_RawWorkspacePreviewSupersededStreak;
                m_RawWorkspacePreviewHealthyStreak = 0;
                // Cancellation describes obsolete work, not the cost of a
                // completed frame. Startup and changing view bounds can both
                // cancel several jobs without demonstrating pixel pressure.
            }
            if (result.rawWorkspace.preciseSolveResult.has_value() &&
                result.rawWorkspace.preciseSolveResult->canceled) {
                // A cancel request intentionally advances the live render
                // generation. The isolated worker's image result is stale,
                // but its cancellation acknowledgment is still required to
                // queue the first normal RAW preview after that worker stops.
                HandleRawWorkspacePreciseSolveResult(
                    *result.rawWorkspace.preciseSolveResult);
            }
            if (result.rawRenderPurpose ==
                    RawRenderPurpose::ExplicitExport) {
                CompleteRawWorkspaceExportRender(result, false);
            }
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            if (!IsAnyRenderBackendBusy()) {
                ResetIncompleteCompositeOutputRequestsForRetry();
                ResetIncompletePreviewRequestsForRetry();
            }
            continue;
        }
        const bool awaitedFirstPresentationFailed =
            !result.success &&
            result.error != "Render superseded by a newer snapshot." &&
            RawRenderPurposeMayPublishPresentation(
                result.rawRenderPurpose) &&
            m_DeferredLoadedProjectApply.active &&
            m_DeferredLoadedProjectApply.step ==
                DeferredLoadedProjectApplyState::Step::WaitForFirstRender &&
            result.generation >
                m_DeferredLoadedProjectApply.acceptedRenderGenerationAtStart;
        if (awaitedFirstPresentationFailed) {
            m_RenderPending = IsAnyRenderBackendBusy();
            const std::string loadFailure = result.error.empty()
                ? "The project opened, but its first viewport render failed."
                : "The project opened, but its first viewport render failed: " +
                    result.error;
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            m_HdrMergeSubmittedNodesByGeneration.erase(
                result.generation);
            ResetIncompleteCompositeOutputRequestsForRetry();
            ResetIncompletePreviewRequestsForRetry();
            FailDeferredLoadedProjectApply(loadFailure);
            continue;
        }
        if (result.graphRequest.enabled)
            m_GraphAcceptedResultGeneration = result.generation;
        const auto submittedIt = m_HdrMergeSubmittedNodesByGeneration.find(result.generation);
        const std::vector<int> activeHdrMergeNodeIds =
            submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()
                ? submittedIt->second
                : std::vector<int>{};
        m_RenderPending = IsAnyRenderBackendBusy();
        m_HdrMergeRenderingNodeIds.clear();
        if (result.rawWorkspace.preciseSolveResult.has_value()) {
            // A precise solve returns only an isolated verified-recipe record.
            // Never let that worker result replace or clear the live viewport,
            // analysis, readbacks, or diagnostics; the accepted recipe queues
            // its own ordinary render after the one atomic main-thread apply.
            HandleRawWorkspacePreciseSolveResult(
                *result.rawWorkspace.preciseSolveResult);
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            if (submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()) {
                m_HdrMergeSubmittedNodesByGeneration.erase(submittedIt);
            }
            continue;
        }
        m_GraphPerformanceStats.lastMainRenderMs = result.mainRenderMs;
        m_GraphPerformanceStats.lastMainGraphExecuteMs = result.mainRenderMs;
        m_GraphPerformanceStats.lastMainPostExecuteMs = 0.0;
        m_GraphPerformanceStats.lastRawWorkspaceRender =
            !result.rawWorkspace.sourceKey.empty();
        m_GraphPerformanceStats.lastRawInteractivePreview =
            !result.rawWorkspace.sourceKey.empty() && result.previewMaxDimension > 0;
        m_GraphPerformanceStats.lastRawAnalysisCaptured =
            result.rawWorkspace.analysisCaptured;
        m_GraphPerformanceStats.lastRawPreviewMaxDimension =
            result.previewMaxDimension;
        m_GraphPerformanceStats.lastRawRenderPurpose =
            RawRenderPurposeName(result.rawRenderPurpose);
        m_GraphPerformanceStats.lastRawQueueWaitMs =
            result.telemetry.queueWaitMs;
        m_GraphPerformanceStats.lastRawWorkerTotalMs =
            result.telemetry.workerTotalMs;
        m_GraphPerformanceStats.lastRawUiAdoptionMs =
            result.telemetry.uiAdoptionMs;
        m_GraphPerformanceStats.lastRawSourceTransferredBytes =
            result.telemetry.sourceTransferredBytes;
        m_GraphPerformanceStats.lastRawPublishedTextureBytes =
            result.telemetry.publishedTextureBytes;
        m_GraphPerformanceStats.lastRawReadbackTransferredBytes =
            result.telemetry.readbackTransferredBytes;
        m_GraphPerformanceStats.lastRawFullFrameEstimatedWorkingSetBytes =
            result.telemetry.fullFrameEstimatedWorkingSetBytes;
        m_GraphPerformanceStats.lastRawWorkerStarted =
            result.telemetry.workerStarted;
        m_GraphPerformanceStats.lastRawFullFrameRefinementRequested =
            result.telemetry.fullFrameRefinementRequested;
        m_GraphPerformanceStats.lastRawFullFrameRefinementBudgetAllowed =
            result.telemetry.fullFrameRefinementBudgetAllowed;
        m_GraphPerformanceStats.lastRawSuperseded =
            result.telemetry.superseded;
        m_GraphPerformanceStats.lastPreviewRenderMs = result.previewRenderMs;
        m_GraphPerformanceStats.lastCompositeRenderMs = result.compositeRenderMs;
        m_GraphPerformanceStats.lastRenderedPreviewCount = result.renderedPreviewCount;
        m_GraphPerformanceStats.lastRenderedCompositeCount = result.renderedCompositeCount;
        m_GraphPerformanceStats.lastMainOutputTiled =
            result.outputTiles.tiled && result.outputTiles.complete && !result.outputTiles.tiles.empty();
        m_GraphPerformanceStats.lastMainOutputTileCount =
            m_GraphPerformanceStats.lastMainOutputTiled
                ? static_cast<int>(result.outputTiles.tiles.size())
                : 0;
        m_GraphPerformanceStats.lastMainRegionPlanAvailable = result.mainRegionPlanAvailable;
        m_GraphPerformanceStats.lastMainRegionPlanTileable = result.mainRegionPlanTileable;
        m_GraphPerformanceStats.lastMainRegionPlanHaloX = result.mainRegionPlanHaloX;
        m_GraphPerformanceStats.lastMainRegionPlanHaloY = result.mainRegionPlanHaloY;
        m_GraphPerformanceStats.lastMainRegionPlanReason = result.mainRegionPlanReason;
        m_GraphPerformanceStats.lastMainGraphStats = result.mainGraphStats;
        const std::string activePreviewIdentity =
            GetActiveRawWorkspacePreviewIdentity();
        const Stack::RawWorkspace::SourceRecord* activeRawWorkspaceSource =
            result.rawWorkspace.sourceKey.empty() ||
                    IsMultiFrameRawProjectActive()
                ? nullptr
                : FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);
        const std::uint64_t activeRawWorkspaceSourceHash =
            IsMultiFrameRawProjectActive()
                ? GetActiveRawWorkspacePreviewSourceHash()
                : (activeRawWorkspaceSource == nullptr
                    ? 0
                    : BuildRawWorkspaceAutoBaseSourceHash(
                        *activeRawWorkspaceSource));
        const bool rawWorkspaceResultMatchesActive =
            !result.rawWorkspace.sourceKey.empty() &&
            result.rawWorkspace.sourceKey == activePreviewIdentity &&
            Stack::EditorModuleTypes::RawStartingPointSourceHashesCompatible(
                result.rawWorkspace.sourceHash,
                activeRawWorkspaceSourceHash);
        if (result.rawRenderPurpose == RawRenderPurpose::ExplicitExport) {
            CompleteRawWorkspaceExportRender(
                result,
                rawWorkspaceResultMatchesActive);
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            if (submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()) {
                m_HdrMergeSubmittedNodesByGeneration.erase(submittedIt);
            }
            continue;
        }
        const auto& gradingScopePacket = result.rawWorkspace.gradingScopeVisualization;
        const auto expectedGradingScopeSource = m_RawWorkspaceLabUi.gradingScopesShowInput
            ? RawDevelopmentGradingScopeSource::NeutralScene
            : RawDevelopmentGradingScopeSource::DisplayCandidate;
        if (rawWorkspaceResultMatchesActive && gradingScopePacket &&
            gradingScopePacket->source == expectedGradingScopeSource &&
            gradingScopePacket->sourceKey == GetActiveRawWorkspacePreviewIdentity() &&
            (!m_RawWorkspaceGradingScopeVisualization ||
             m_RawWorkspaceGradingScopeVisualization->source != gradingScopePacket->source ||
             m_RawWorkspaceGradingScopeVisualization->sourceKey != gradingScopePacket->sourceKey ||
             gradingScopePacket->generation > m_RawWorkspaceGradingScopeVisualization->generation)) {
            m_RawWorkspaceGradingScopeVisualization = gradingScopePacket;
        }
        const auto adoptColorWarpCloudPacket = [&]() {
            Raw::RawColorCloudPacket& packet =
                result.rawWorkspace.colorWarpCloudPacket;
            if (!rawWorkspaceResultMatchesActive ||
                packet.sourceKey != activePreviewIdentity ||
                packet.samples.empty()) {
                return;
            }
            m_RawWorkspaceLabUi.colorWarpCloudSourceKey =
                packet.sourceKey;
            m_RawWorkspaceLabUi.colorWarpCloudInputFingerprint =
                packet.inputFingerprint;
            m_RawWorkspaceLabUi.colorWarpCloudWorkingSpace =
                packet.workingSpace * 10 + packet.colorWarpVersion;
            m_RawWorkspaceLabUi.colorWarpCloud =
                std::move(packet.samples);
        };
        bool rawPresentationAdopted = false;
        auto markMainOutputAccepted = [&](RawWorkspacePreviewOutputKind outputKind) {
            m_BracketingPresentedSourceHash = result.rawWorkspace.sourceHash;
            m_RawViewportPresentationContent = result.rawWorkspace.presentationFingerprint;
            m_RawViewportPresentedRegion = result.rawWorkspace.viewportRegion;
            m_RawViewportPresentedGeneration = nativeResultCoversView
                ? m_RawViewportRequest.generation : result.rawWorkspace.viewportGeneration;
            if (nativeResultCoversView) m_RawViewportLastPresentation.view = m_RawViewportRequest.generation;
            m_RawWorkspacePreviewOutputKind = result.rawWorkspace.sourceKey.empty()
                ? RawWorkspacePreviewOutputKind::None
                : outputKind;
            m_ViewportOutputRawWorkspaceSourceKey = result.rawWorkspace.sourceKey;
            const int actualOutputWidth = outputKind ==
                    RawWorkspacePreviewOutputKind::Tiled
                ? m_ViewportOutputTiles.fullWidth
                : (m_RawViewportPresentedRegion.Valid() ? m_RawViewportPresentedRegion.fullWidth : m_RawWorkspacePresentationTexture.width);
            const int actualOutputHeight = outputKind ==
                    RawWorkspacePreviewOutputKind::Tiled
                ? m_ViewportOutputTiles.fullHeight
                : (m_RawViewportPresentedRegion.Valid() ? m_RawViewportPresentedRegion.fullHeight : m_RawWorkspacePresentationTexture.height);
            m_ViewportOutputExpectedNativeWidth =
                result.rawWorkspace.expectedNativeOutputWidth;
            m_ViewportOutputExpectedNativeHeight =
                result.rawWorkspace.expectedNativeOutputHeight;
            m_ViewportOutputNativeExtentVerified =
                !result.rawWorkspace.sourceKey.empty() &&
                Stack::EditorRenderScheduling::
                    IsRawPresentationNativeExtent(
                        actualOutputWidth,
                        actualOutputHeight,
                        m_ViewportOutputExpectedNativeWidth,
                        m_ViewportOutputExpectedNativeHeight);
            m_ViewportOutputPreviewMaxDimension =
                result.rawWorkspace.sourceKey.empty()
                ? result.previewMaxDimension
                : Stack::EditorRenderScheduling::
                      ResolveRawPresentationPreviewMaxDimension(
                          result.previewMaxDimension,
                          actualOutputWidth,
                          actualOutputHeight,
                          m_ViewportOutputExpectedNativeWidth,
                          m_ViewportOutputExpectedNativeHeight);
            m_ViewportOutputRenderGeneration = result.generation;
            rawPresentationAdopted =
                !result.rawWorkspace.sourceKey.empty();
            ObserveRawViewportFeedback(result);
            m_GraphPerformanceStats.rawAdaptiveFrameTimeMs =
                m_RawWorkspaceAdaptiveFrameTimeMs;
            m_GraphPerformanceStats.rawAdaptivePreviewScale =
                m_RawWorkspaceAdaptivePreviewScale;
            if (!result.rawWorkspace.sourceKey.empty()) {
                m_RawWorkspaceStaleRenderStatusText.clear();
                m_RawWorkspaceLocalRangeTargetPreview.interactionEditing =
                    false;
                if (!result.rawWorkspace.analysisCaptured &&
                    result.rawWorkspace.graphScopeReadback.valid &&
                    result.rawWorkspace.graphScopeReadback.stage !=
                        RawDevelopmentGraphScopeStage::None) {
                    CacheRawWorkspaceGraphScopeReadback(result.rawWorkspace.sourceKey,
                        result.rawWorkspace.graphScopeInputFingerprint, result.rawWorkspace.graphScopeReadback);
                    RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
                }
                if (result.rawWorkspace.analysisCaptured) {
                    m_RawWorkspaceViewTransformInputStats = result.rawWorkspace.viewTransformInputStats;
                    m_RawWorkspaceFinalDisplayStats = result.rawWorkspace.finalDisplayStats;
                    m_RawWorkspaceStageStatsReadbacks = result.rawWorkspace.stageStatsReadbacks;
                    CacheRawWorkspaceGraphScopeReadback(
                        result.rawWorkspace.sourceKey,
                        result.rawWorkspace.graphScopeInputFingerprint,
                        result.rawWorkspace.graphScopeReadback);
                    adoptColorWarpCloudPacket();
                    bool graphScopeRestored = true;
                    if (m_RawWorkspaceLabUi.activeTool == RawLabTool::Zones ||
                        m_RawWorkspaceLabUi.activeTool == RawLabTool::Tone ||
                        m_RawWorkspaceLabUi.activeTool == RawLabTool::Color) {
                        graphScopeRestored =
                            RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
                    } else {
                        m_RawWorkspaceGraphScopeReadback = {};
                    }
                    m_RawWorkspaceStartPointDiagnostics = result.rawWorkspace.startPointDiagnostics;
                    Stack::EditorModuleTypes::StoreRawStartingPointCandidateRenderQueue(
                        m_RawWorkspaceStartPointCandidateRenderQueue,
                        result.rawWorkspace.sourceKey,
                        result.rawWorkspace.startPointCandidateRenderRequests,
                        result.generation,
                        result.rawWorkspace.sourceHash);
                    m_RawWorkspaceStartPointCandidateRenderResults =
                        result.rawWorkspace.startPointCandidateRenderResults;
                    m_RawWorkspaceAnalysis = result.rawWorkspace.analysis;
                    if (result.rawWorkspace.recommendations.localReport.valid ||
                        !result.rawWorkspace.recommendations.localSuggestionRationale.empty() ||
                        !result.rawWorkspace.recommendations.localAdjustments.empty()) {
                        m_RawWorkspaceAutoBaseUi.recommendations = result.rawWorkspace.recommendations;
                    }
                    TryContinueRawWorkspaceStartingPointOnAnalysis();
                    if (result.rawRenderPurpose ==
                        RawRenderPurpose::AnalysisScopes) {
                        const bool retryScope = !graphScopeRestored &&
                            m_RawWorkspaceAnalysisScopeRetryCount++ == 0;
                        if (graphScopeRestored) {
                            m_RawWorkspaceAnalysisScopeRetryCount = 0;
                        }
                        m_RawWorkspaceAnalysisPending = retryScope;
                        m_RawWorkspaceAnalysisRequested = false;
                        m_RawWorkspaceAnalysisQuietUntilTime = retryScope
                            ? ImGui::GetTime() + 0.25
                            : -1.0;
                    }
                }
                AdoptRawWorkspaceLocalRangeTargetSampleFromResult(result);
                AdoptRawWorkspaceLocalRangeOverlayFromResult(result);
                AdoptRawWorkspacePreciseAppliedRender();
            } else {
                m_RawWorkspaceViewTransformInputStats = {};
                m_RawWorkspaceFinalDisplayStats = {};
                m_RawWorkspaceStageStatsReadbacks.clear();
                m_RawWorkspaceGraphScopeReadback = {};
                m_RawWorkspaceStartPointDiagnostics =
                    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
                m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
                ClearRawWorkspaceLocalRangeTargetState(true);
                ClearRawWorkspaceLocalRangeOverlayState();
            }
            const auto nativeRefinementCompletion =
                Stack::EditorRenderScheduling::
                    ClassifyRawNativeRefinementCompletion(
                        result.rawRenderPurpose,
                        result.previewMaxDimension,
                        result.telemetry.fullFrameRefinementRequested,
                        result.telemetry.fullFrameEstimatedWorkingSetBytes > 0,
                        result.telemetry.fullFrameRefinementBudgetAllowed,
                        rawPresentationAdopted &&
                            (result.rawRenderPurpose ==
                                 RawRenderPurpose::ExplicitInspection ||
                             Stack::EditorRenderScheduling::
                                 RawRenderResultOwnsTrackedRequest(
                                     RawRenderPurpose::ViewportRefinement,
                                     result.rawRenderPurpose,
                                     result.generation,
                                     m_RawWorkspaceFullResolutionPreviewRequestGeneration)),
                        m_ViewportOutputNativeExtentVerified,
                        m_RenderDirty);
            const bool completedSettledDisplayPreview =
                nativeRefinementCompletion ==
                    Stack::EditorRenderScheduling::
                        RawNativeRefinementCompletion::NativePresentation;
            const bool completedBudgetFallback =
                nativeRefinementCompletion ==
                    Stack::EditorRenderScheduling::
                        RawNativeRefinementCompletion::BudgetFallback;
            const bool completedExtentMismatch =
                nativeRefinementCompletion ==
                    Stack::EditorRenderScheduling::
                        RawNativeRefinementCompletion::ExtentMismatch;
            const bool completedExplicitFullQuality =
                m_ViewportOutputNativeExtentVerified &&
                m_ViewportOutputPreviewMaxDimension == 0 &&
                result.rawRenderPurpose !=
                    RawRenderPurpose::ViewportRefinement;
            const bool rearmNativeAfterProxy =
                Stack::EditorRenderScheduling::
                    ShouldRearmRawNativeRefinementAfterAcceptedPresentation(
                        m_ViewportOutputPreviewMaxDimension,
                        nativeRefinementCompletion,
                        rawPresentationAdopted);
            if (!result.rawWorkspace.sourceKey.empty() &&
                completedBudgetFallback &&
                !m_RenderDirty) {
                m_RawWorkspaceFullResolutionPreviewPending = true;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFullResolutionPreviewDeferredByBudget = true;
                m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes =
                    m_RawWorkspaceVramWorkingBudgetBytes;
                m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
                m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
                m_RawWorkspaceFastPreviewUntilTime = -1.0;
            } else if (!result.rawWorkspace.sourceKey.empty() &&
                       (completedExplicitFullQuality ||
                        completedSettledDisplayPreview) &&
                       !m_RenderDirty) {
                m_RawWorkspaceFullResolutionPreviewPending = false;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
                m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
                m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
                m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
                m_RawWorkspaceFastPreviewUntilTime = -1.0;
                if (completedExplicitFullQuality) {
                    m_RawWorkspaceExplicitFullQualityRenderRequested = false;
                }
            } else if (!result.rawWorkspace.sourceKey.empty() &&
                       completedExtentMismatch &&
                       !m_RenderDirty) {
                const bool retryNative = Stack::EditorRenderScheduling::
                    ShouldRetryRawNativeRefinementAfterFailure(
                        m_RawWorkspaceFullResolutionPreviewRetryCount,
                        kRawNativeRefinementMaximumRetryCount);
                if (retryNative) {
                    ++m_RawWorkspaceFullResolutionPreviewRetryCount;
                }
                m_RawWorkspaceFullResolutionPreviewPending = retryNative;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
                m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
                m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
                m_RawWorkspaceFastPreviewUntilTime = retryNative &&
                    ImGui::GetCurrentContext() != nullptr
                    ? ImGui::GetTime() + 0.50
                    : -1.0;
            } else if (!result.rawWorkspace.sourceKey.empty() &&
                       rearmNativeAfterProxy) {
                m_RawWorkspaceFullResolutionPreviewPending = true;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
                m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
                m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
                m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
            }
            if (!result.rawWorkspace.sourceKey.empty() &&
                Stack::EditorRenderScheduling::ShouldRefreshRawProjectCover(
                    completedExplicitFullQuality,
                    completedSettledDisplayPreview,
                    m_RenderDirty)) {
                RefreshPendingMultiFrameProjectCover();
            }
            // Retain only read-only, already-computed uniform outputs. Wire
            // readouts consult this snapshot opportunistically and never
            // request evaluation merely to populate a label.
            m_LastGraphUniformOutputValues.clear();
            for (const ReductionExecutionStats& reduction : result.mainGraphStats.reductions) {
                if (!std::isfinite(reduction.value)) continue;
                m_LastGraphUniformOutputValues[
                    EditorNodeGraph::WireReadout::OutputIdentity(
                        reduction.nodeId,
                        EditorNodeGraph::kValueOutputSocketId)] =
                    Stack::NodeMath::MakeUniformScalar(reduction.value);
            }
            if (result.mainGraphStats.lastReductionFailureNodeId > 0 &&
                !result.mainGraphStats.lastReductionFailure.empty()) {
                m_LastGraphUniformOutputValues[
                    EditorNodeGraph::WireReadout::OutputIdentity(
                        result.mainGraphStats.lastReductionFailureNodeId,
                        EditorNodeGraph::kValueOutputSocketId)] =
                    Stack::NodeMath::MakeFailureValue(
                        Stack::NodeMath::LogicalValueType::Scalar,
                        Stack::NodeMath::ValueStorageClass::Uniform,
                        result.mainGraphStats.lastReductionFailure);
            }
            m_LastGraphUniformOutputGeneration = result.generation;
            m_LastCompletedRenderGeneration = result.generation;
            for (int nodeId : activeHdrMergeNodeIds) {
                if (result.graphRequest.enabled && result.graphRequest.revision != m_RenderRevision)
                    continue;
                m_HdrMergeCompletedGenerations[nodeId] = std::max(
                    m_HdrMergeCompletedGenerations[nodeId],
                    m_HdrMergeRequestedGenerations.count(nodeId) ? m_HdrMergeRequestedGenerations[nodeId] : GetNodeDirtyGeneration(nodeId));
                m_HdrMergeFailureMessages.erase(nodeId);
            }
        };
        const bool rawWorkspaceSourceKeyMismatch =
            !result.rawWorkspace.sourceKey.empty() &&
            result.rawWorkspace.sourceKey != activePreviewIdentity;
        const bool rawWorkspaceSourceMismatch =
            !result.rawWorkspace.sourceKey.empty() &&
            !rawWorkspaceResultMatchesActive;
        const bool resultMayPublishPresentation =
            RawRenderPurposeMayPublishPresentation(
                result.rawRenderPurpose);
        if (rawWorkspaceSourceMismatch) {
            if (Stack::EditorModuleTypes::ShouldClearRawStartingPointCandidateRenderQueueForRejectedResult(
                    m_RawWorkspaceStartPointCandidateRenderQueue,
                    result.rawWorkspace.sourceKey,
                    activePreviewIdentity,
                    result.rawWorkspace.sourceHash,
                    activeRawWorkspaceSourceHash)) {
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
            }
            if (rawWorkspaceSourceKeyMismatch &&
                m_ViewportOutputRawWorkspaceSourceKey == result.rawWorkspace.sourceKey) {
                ClearViewportOutputTiles();
                m_Pipeline.ClearOutput();
                m_RawWorkspaceViewTransformInputStats = {};
                m_RawWorkspaceFinalDisplayStats = {};
                m_RawWorkspaceStageStatsReadbacks.clear();
                m_RawWorkspaceGraphScopeReadback = {};
                m_RawWorkspaceStartPointDiagnostics =
                    Stack::RawAutoStartPoint::RawAutoStartPointDiagnostics();
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
                m_RawWorkspaceAnalysis = Stack::RawAnalysis::RawImageAnalysis();
            }
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
        } else if (result.success && !resultMayPublishPresentation) {
            // Samples, qualifiers, scopes, and status packets update only
            // their matching RAW consumer. They must never clear or replace
            // the last accepted presentation texture.
            if (rawWorkspaceResultMatchesActive) {
                AdoptRawWorkspaceLocalRangeTargetSampleFromResult(result);
                AdoptRawWorkspaceLocalRangeOverlayFromResult(result);
                if (result.rawWorkspace.analysisCaptured) {
                    m_RawWorkspaceViewTransformInputStats =
                        result.rawWorkspace.viewTransformInputStats;
                    m_RawWorkspaceFinalDisplayStats =
                        result.rawWorkspace.finalDisplayStats;
                    m_RawWorkspaceStageStatsReadbacks =
                        result.rawWorkspace.stageStatsReadbacks;
                    CacheRawWorkspaceGraphScopeReadback(
                        result.rawWorkspace.sourceKey,
                        result.rawWorkspace.graphScopeInputFingerprint,
                        result.rawWorkspace.graphScopeReadback);
                    adoptColorWarpCloudPacket();
                    const bool graphScopeRestored =
                        RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
                    m_RawWorkspaceStartPointDiagnostics =
                        result.rawWorkspace.startPointDiagnostics;
                    Stack::EditorModuleTypes::StoreRawStartingPointCandidateRenderQueue(
                        m_RawWorkspaceStartPointCandidateRenderQueue,
                        result.rawWorkspace.sourceKey,
                        result.rawWorkspace.startPointCandidateRenderRequests,
                        result.generation,
                        result.rawWorkspace.sourceHash);
                    m_RawWorkspaceStartPointCandidateRenderResults =
                        result.rawWorkspace.startPointCandidateRenderResults;
                    m_RawWorkspaceAnalysis = result.rawWorkspace.analysis;
                    if (result.rawWorkspace.recommendations.localReport.valid ||
                        !result.rawWorkspace.recommendations
                             .localSuggestionRationale.empty() ||
                        !result.rawWorkspace.recommendations
                             .localAdjustments.empty()) {
                        m_RawWorkspaceAutoBaseUi.recommendations =
                            result.rawWorkspace.recommendations;
                    }
                    TryContinueRawWorkspaceStartingPointOnAnalysis();
                    if (result.rawRenderPurpose ==
                        RawRenderPurpose::AnalysisScopes) {
                        const bool retryScope = !graphScopeRestored &&
                            m_RawWorkspaceAnalysisScopeRetryCount++ == 0;
                        if (graphScopeRestored) {
                            m_RawWorkspaceAnalysisScopeRetryCount = 0;
                        }
                        m_RawWorkspaceAnalysisPending = retryScope;
                        m_RawWorkspaceAnalysisRequested = false;
                        m_RawWorkspaceAnalysisQuietUntilTime = retryScope
                            ? ImGui::GetTime() + 0.25
                            : -1.0;
                    }
                }
            }
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
        } else if (result.success &&
            !result.rawWorkspace.sourceKey.empty() &&
            !result.outputTexture.texture && !result.pixels.empty() &&
            result.width > 0 &&
            result.height > 0) {
            if (m_Pipeline.UploadOutputFromPixels(
                    result.pixels.data(),
                    result.width,
                    result.height,
                    4)) {
                EditorRenderWorker::SharedTextureResult uploadedTexture;
                uploadedTexture.texture =
                    m_Pipeline.TakeExternalOutputTexture(
                        uploadedTexture.width,
                        uploadedTexture.height);
                result.outputTexture = std::move(uploadedTexture);
                if (AdoptRawViewportFrame(result)) {
                    QueueViewportOutputTileSetRelease(
                        m_ViewportOutputTiles);
                    markMainOutputAccepted(
                        RawWorkspacePreviewOutputKind::SingleTexture);
                } else {
                    result.error =
                        "Render produced no viewport texture.";
                }
            } else {
                result.error =
                    "Render output could not be uploaded.";
            }
        } else if (result.success && result.outputTexture.texture != 0) {
            auto& frameTransition = m_Viewport.FrameTransition();
            bool blendGraphFrame = false;
            if (result.graphRequest.enabled) {
                blendGraphFrame = frameTransition.Prepare(
                    {result.graphRequest, result.outputTexture.width, result.outputTexture.height,
                     result.rawWorkspace.viewportEncodedSrgb, ImGui::GetTime()},
                    result.telemetry.queueWaitMs + result.telemetry.workerTotalMs);
            } else {
                frameTransition.Reset();
            }
            if (!result.rawWorkspace.sourceKey.empty()) {
                if (result.graphRequest.enabled) {
                    ClearRawViewportTransition();
                    if (blendGraphFrame && m_RawWorkspacePresentationTexture.EnsureLease())
                        frameTransition.Retain(m_RawWorkspacePresentationTexture.lease);
                }
                const bool adopted = result.graphRequest.enabled
                    ? AdoptRawWorkspacePresentationTexture(result.outputTexture,result.rawWorkspace.viewportRegion)
                    : AdoptRawViewportFrame(result);
                if (adopted) {
                    QueueViewportOutputTileSetRelease(
                        m_ViewportOutputTiles);
                    markMainOutputAccepted(RawWorkspacePreviewOutputKind::SingleTexture);
                } else {
                    result.error = "Render produced no viewport texture.";
                }
            } else {
                ClearViewportOutputTiles();
                const int adoptedWidth = result.outputTexture.width;
                const int adoptedHeight = result.outputTexture.height;
                if (blendGraphFrame) {
                    int previousWidth = 0, previousHeight = 0;
                    const auto previous = m_Pipeline.TakeExternalOutputTexture(
                        previousWidth, previousHeight, true);
                    frameTransition.RetainOwned(previous, previousWidth, previousHeight);
                }
                const unsigned int adoptedTexture =
                    result.outputTexture.ReleaseTextureName();
                m_Pipeline.AdoptExternalOutputTexture(
                    adoptedTexture,
                    adoptedWidth,
                    adoptedHeight);
                markMainOutputAccepted(RawWorkspacePreviewOutputKind::SingleTexture);
            }
        } else if (result.success && result.outputTiles.complete && !result.outputTiles.tiles.empty()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
            m_ViewportOutputTiles = std::move(result.outputTiles);
            result.outputTiles = {};
            markMainOutputAccepted(RawWorkspacePreviewOutputKind::Tiled);
        } else if (!result.success && !result.rawWorkspace.sourceKey.empty()) {
            if (result.error ==
                "Render superseded by a newer snapshot.") {
                m_RenderDirty = true;
                m_LastSubmittedRenderRevision =
                    m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            }
            if (rawWorkspaceResultMatchesActive &&
                result.error != "Render superseded by a newer snapshot.") {
                m_RawWorkspaceStaleRenderStatusText =
                    result.error.empty() ? "RAW render failed." : result.error;
                PostNotification(
                    UiNotificationSeverity::Error,
                    "Preview retained as stale: " +
                        m_RawWorkspaceStaleRenderStatusText,
                    "raw-workspace-render-stale");
            }
            if (Stack::EditorModuleTypes::ShouldClearRawStartingPointCandidateRenderQueueForFailedResult(
                    m_RawWorkspaceStartPointCandidateRenderQueue,
                    result.rawWorkspace.sourceKey,
                    activePreviewIdentity,
                    result.rawWorkspace.sourceHash,
                    activeRawWorkspaceSourceHash)) {
                Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                    m_RawWorkspaceStartPointCandidateRenderQueue);
                m_RawWorkspaceStartPointCandidateRenderResults.clear();
            }
            if (rawWorkspaceResultMatchesActive &&
                Stack::EditorModuleTypes::MarkRawStartingPointRenderFailure(
                    m_RawWorkspaceAutoBaseUi,
                    result.error)) {
                PostNotification(
                    UiNotificationSeverity::Error,
                    m_RawWorkspaceAutoBaseUi.summary,
                    "raw-workspace-starting-point-render-failed");
            }
        } else if (!m_Project->graph.IsOutputConnected()) {
            ClearViewportOutputTiles();
            m_Pipeline.ClearOutput();
        } else if (!activeHdrMergeNodeIds.empty()) {
            const std::string baseMessage = result.error.empty() ? "Render failed" : result.error;
            for (int nodeId : activeHdrMergeNodeIds) {
                const EditorNodeGraph::Node* node = m_Project->graph.FindNode(nodeId);
                const std::string nodeName = (node && !node->title.empty())
                    ? node->title
                    : std::string("HDR Merge");
                m_HdrMergeFailureMessages[nodeId] = baseMessage;
                PostNotification(
                    UiNotificationSeverity::Error,
                    nodeName + ": " + baseMessage,
                    "hdr-merge-render-failed-" + std::to_string(nodeId));
            }
        }
        // A worker can finish successfully while the UI cannot adopt/upload its
        // texture. That is a terminal first-frame failure too, not a reason to
        // keep the loaded document disabled waiting for another completion.
        const bool awaitedFirstPresentationAdoptionFailed =
            result.success && !result.error.empty() &&
            resultMayPublishPresentation && rawWorkspaceResultMatchesActive &&
            !rawPresentationAdopted &&
            m_DeferredLoadedProjectApply.active &&
            m_DeferredLoadedProjectApply.step ==
                DeferredLoadedProjectApplyState::Step::WaitForFirstRender &&
            result.generation >
                m_DeferredLoadedProjectApply.acceptedRenderGenerationAtStart;
        if (awaitedFirstPresentationAdoptionFailed) {
            QueueViewportOutputTextureRelease(result.outputTexture);
            QueueViewportOutputTileSetRelease(result.outputTiles);
            QueueViewportOutputTextureRelease(
                result.rawWorkspace.localRangeOverlayTexture);
            m_HdrMergeSubmittedNodesByGeneration.erase(result.generation);
            ResetIncompleteCompositeOutputRequestsForRetry();
            ResetIncompletePreviewRequestsForRetry();
            FailDeferredLoadedProjectApply(
                "The project opened, but its first viewport frame could not be displayed: " +
                result.error);
            continue;
        }
        if (result.graphRequest.enabled && !result.success) ReportGraphRenderFailure(result);
        if (rawWorkspaceResultMatchesActive &&
            Stack::EditorRenderScheduling::
                RawRenderResultOwnsTrackedRequest(
                    RawRenderPurpose::ViewportRefinement,
                    result.rawRenderPurpose,
                    result.generation,
                    m_RawWorkspaceFullResolutionPreviewRequestGeneration) &&
            !rawPresentationAdopted &&
            !m_RenderDirty &&
            result.error != "Render superseded by a newer snapshot.") {
            const bool retryNative =
                Stack::EditorRenderScheduling::
                    ShouldRetryRawNativeRefinementAfterFailure(
                        m_RawWorkspaceFullResolutionPreviewRetryCount,
                        kRawNativeRefinementMaximumRetryCount);
            if (retryNative) {
                ++m_RawWorkspaceFullResolutionPreviewRetryCount;
            }
            // Keep the last truthful proxy visible while transient native
            // failures retry after a short quiet interval. A bounded retry
            // count prevents a permanent driver or graph failure from
            // becoming a render loop.
            m_RawWorkspaceFullResolutionPreviewPending = retryNative;
            m_RawWorkspaceFullResolutionPreviewRequested = false;
            m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
            m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
            m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
            m_RawWorkspaceFastPreviewUntilTime = retryNative &&
                ImGui::GetCurrentContext() != nullptr
                ? ImGui::GetTime() + 0.50
                : -1.0;
        }
        ApplyToneCurveAutoRewriteFeedback(result.toneCurveAutoRewrites);
        ApplyDevelopCandidateRenderFeedback(result.developCandidateRenders);
        if (submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()) {
            m_HdrMergeSubmittedNodesByGeneration.erase(submittedIt);
        }
        for (auto it = m_HdrMergeSubmittedNodesByGeneration.begin(); it != m_HdrMergeSubmittedNodesByGeneration.end();) {
            if (it->first < result.generation) {
                it = m_HdrMergeSubmittedNodesByGeneration.erase(it);
            } else {
                ++it;
            }
        }

        for (EditorRenderWorker::CompositeOutputResult& compositeResult : result.compositeOutputs) {
            if (!compositeResult.success || compositeResult.pixels.empty() || compositeResult.width <= 0 || compositeResult.height <= 0) {
                ResetCompositeOutputRequestForRetry(
                    compositeResult.outputNodeId);
                continue;
            }
            (void)PublishCompositeOutputPixels(
                compositeResult.outputNodeId,
                std::move(compositeResult.pixels),
                compositeResult.width,
                compositeResult.height,
                compositeResult.dirtyGeneration,
                compositeResult.chainFingerprint, compositeResult.pixelsPrepared);
        }
        if (!IsAnyRenderBackendBusy()) {
            ResetIncompleteCompositeOutputRequestsForRetry();
        }

        for (EditorRenderWorker::PreviewResult& previewResult : result.previews) {
            if (!previewResult.rawLayerId.empty()) {
                AdoptRawLayerThumbnail(previewResult);
                continue;
            }
            if (!GetNodeGraph().FindNode(previewResult.previewNodeId)) continue;
            if (!previewResult.success ||
                previewResult.pixels.empty() ||
                previewResult.width <= 0 ||
                previewResult.height <= 0) {
                ResetPreviewRequestForRetry(
                    previewResult.previewNodeId,
                    previewResult.dirtyGeneration);
                if (!previewResult.error.empty()) {
                    PostNotification(
                        UiNotificationSeverity::Error,
                        previewResult.error,
                        "editor-preview-failed-" + std::to_string(previewResult.previewNodeId));
                }
                continue;
            }
            const std::uint64_t desiredRevision = GetPreviewNodeRevision(previewResult.previewNodeId);
            if (previewResult.dirtyGeneration < desiredRevision) {
                ResetPreviewRequestForRetry(
                    previewResult.previewNodeId,
                    previewResult.dirtyGeneration);
                continue;
            }
            (void)PublishPreviewResultPixels(previewResult);
        }
        if (!IsAnyRenderBackendBusy()) {
            ResetIncompletePreviewRequestsForRetry();
        }
        // Any texture not adopted by one of the accepted-result branches is
        // still owned by the result. Queue it here so rejected RAW sources,
        // failed outputs, and partial auxiliary surfaces cannot leak GL
        // resources.
        releaseDeferredResultResources(result);
        } catch (...) {
            releaseDeferredResultResources(result);
            ResetIncompleteCompositeOutputRequestsForRetry();
            ResetIncompletePreviewRequestsForRetry();
            m_HdrMergeSubmittedNodesByGeneration.erase(result.generation);
            m_HdrMergeRenderingNodeIds.clear();
            m_RenderPending =
                IsAnyRenderBackendBusy();
            m_RenderDirty = true;
        }
    }
}

