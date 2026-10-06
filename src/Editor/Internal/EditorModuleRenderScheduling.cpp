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

void EditorModule::SubmitRenderIfReady() noexcept {
    try {
        SubmitRenderIfReadyImpl();
    } catch (...) {
        ResetIncompleteCompositeOutputRequestsForRetry();
        ResetIncompletePreviewRequestsForRetry();
        const auto submitted =
            m_HdrMergeSubmittedNodesByGeneration.find(
                m_RenderGeneration);
        if (submitted !=
            m_HdrMergeSubmittedNodesByGeneration.end()) {
            for (const int nodeId : submitted->second) {
                m_HdrMergeRequestedGenerations.erase(nodeId);
            }
            m_HdrMergeSubmittedNodesByGeneration.erase(
                submitted);
        }
        m_HdrMergeRenderingNodeIds.clear();
        m_RenderPending =
            IsAnyRenderBackendBusy();
        m_RenderDirty = true;
        m_LastSubmittedRenderRevision =
            m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
    }
}

void EditorModule::SubmitRenderIfReadyImpl() {
    const bool rawWorkspaceActive = IsRawWorkspaceProjectActive();
    if ((!m_RawWorkspaceRootTabActive || !rawWorkspaceActive) && !GraphRenderBackendReady()) return;
    const bool compositeMode = Stack::EditorRenderScheduling::
        ShouldUseCompositeRenderPath(
            GetViewportMode() == ViewportMode::CompositeCanvas,
            rawWorkspaceActive,
            m_RawWorkspaceRootTabActive);
    m_Pipeline.SetRawRgbDenoiseAsyncEnabled(rawWorkspaceActive);
    if (rawWorkspaceActive &&
        m_Pipeline.ConsumeRawRgbDenoiseAsyncCompletion()) {
        // The recipe did not change, but its external model result did. Force
        // one follow-up graph evaluation so the provisional before-state is
        // replaced by the accepted denoised pixels.
        MarkRenderRefreshDirty();
    }
    const bool allowBackgroundRenderWorker =
        rawWorkspaceActive
            ? m_RawRenderClientId != 0
            : m_RenderWorkerAvailable;
    const double now = ImGui::GetTime();
    if (m_ActiveSubWindow == EditorSubWindow::NodeGraph &&
        m_Sidebar.GetNodeGraphUI().IsGraphMiddlePanActive()) {
        return;
    }
    if (m_RawWorkspaceRootTabActive) UpdateRawWorkspaceSettledPreviewRender(now);
    if (rawWorkspaceActive &&
        allowBackgroundRenderWorker &&
        TrySubmitRawWorkspaceCachePrewarm(now)) {
        return;
    }
    if (rawWorkspaceActive && allowBackgroundRenderWorker &&
        TrySubmitRawViewportMaintenance(now)) return;
    if (rawWorkspaceActive && allowBackgroundRenderWorker &&
        TrySubmitRawViewportCalibration(now)) return;
    if (!allowBackgroundRenderWorker && !m_RenderDirty) {
        return;
    }
    RefreshDeferredDevelopCandidateFeedbackIfReady(now);
    // The UI frame already coalesces input. A quiet-period debounce here can
    // starve Graph rendering while a control changes every frame; the worker
    // instead keeps useful active work and replaces its single pending request.
    const bool workerBusy = allowBackgroundRenderWorker &&
        (m_RenderPending || IsAnyRenderBackendBusy());
    m_GraphPerformanceStats.lastPreviewRequestBuildMs = 0.0f;
    m_GraphPerformanceStats.lastCompositeRequestBuildMs = 0.0f;
    std::vector<EditorRenderWorker::PreviewRequest> previewRequests;
    if (allowBackgroundRenderWorker && !workerBusy && !ShouldDeferPreviewLikeWork(now)) {
        const auto previewBuildBegin = std::chrono::steady_clock::now();
        try {
            previewRequests = BuildPreviewRequests();
        } catch (const std::bad_alloc&) {
            ResetIncompletePreviewRequestsForRetry();
            return;
        } catch (const std::length_error&) {
            ResetIncompletePreviewRequestsForRetry();
            return;
        }
        m_GraphPerformanceStats.lastPreviewRequestBuildMs =
            MillisecondsBetween(previewBuildBegin, std::chrono::steady_clock::now());
    }
    if (!m_RenderDirty && previewRequests.empty()) {
        return;
    }
    // A newer single-output edit should be allowed to replace stale in-flight
    // background Develop feedback instead of waiting for every old probe to
    // drain. The worker checks the pending generation at safe GL boundaries.
    if (!compositeMode && m_RenderPending && !m_RenderDirty) {
        return;
    }

    const bool rawStage = UsesRawWorkspaceStageRender();
    const int activeOutputNodeId = rawStage
        ? ResolveRawWorkspaceStageOutputNodeId() : m_Project->graph.ResolvePreviewOutputNodeId();
    const std::vector<int> activeHdrMergeNodeIds =
        (!compositeMode && activeOutputNodeId > 0) ? CollectHdrMergeNodesForOutput(activeOutputNodeId) : std::vector<int>{};
    const auto submitPreviewOnlyRequests = [&](std::vector<EditorRenderWorker::PreviewRequest>& requests) {
        if (requests.empty() || !allowBackgroundRenderWorker) {
            m_RenderPending = false;
            return;
        }
        m_RenderGeneration =
            Stack::EditorRenderScheduling::NextGlobalGeneration();
        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot;
        if (!TryBuildRenderSnapshot(
                m_RenderGeneration,
                snapshot)) {
            m_RenderPending = false;
            return;
        }
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.outputConnected = false;
        snapshot.sourcePixels = {};
        if (!snapshot.rawWorkspace.sourceKey.empty()) {
            snapshot.rawRenderPurpose = RawRenderPurpose::Thumbnail;
            snapshot.rawWorkspace.analysisRequested = false;
        }
        snapshot.developCandidateRenders.clear();
        snapshot.previews = std::move(requests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        m_RenderPending = true;
        if (!SubmitRenderSnapshot(std::move(snapshot))) {
            m_RenderPending = false;
        }
    };
    const auto recordHdrMergeSubmission = [&]() {
        m_HdrMergeRenderingNodeIds.clear();
        for (int nodeId : activeHdrMergeNodeIds) {
            m_HdrMergeRequestedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeFailureMessages.erase(nodeId);
            m_HdrMergeRenderingNodeIds.insert(nodeId);
        }
        m_HdrMergeSubmittedNodesByGeneration[m_RenderGeneration] = activeHdrMergeNodeIds;
    };
    const auto blockInvalidHdrMergeOutput = [&]() -> bool {
        if (activeHdrMergeNodeIds.empty()) {
            return false;
        }

        bool blocked = false;
        for (int nodeId : activeHdrMergeNodeIds) {
            const HdrMergeNodeStatus status = GetHdrMergeNodeStatus(nodeId);
            if (status.state != HdrMergeRenderState::BlockedMissingInput &&
                status.state != HdrMergeRenderState::IncompatibleInput) {
                continue;
            }
            const EditorNodeGraph::Node* node = m_Project->graph.FindNode(nodeId);
            const std::string nodeName = (node && !node->title.empty())
                ? node->title
                : std::string("HDR Merge");
            PostNotification(
                UiNotificationSeverity::Error,
                nodeName + ": " + status.message,
                "hdr-merge-invalid-" + std::to_string(nodeId));
            blocked = true;
        }
        if (!blocked) {
            return false;
        }

        m_RenderDirty = false;
        m_RenderPending = false;
        m_HdrMergeRenderingNodeIds.clear();
        submitPreviewOnlyRequests(previewRequests);
        return true;
    };

    if (compositeMode) {
        const auto compositeBuildBegin = std::chrono::steady_clock::now();
        std::vector<EditorRenderWorker::CompositeOutputRequest> requests;
        try {
            requests = BuildCompositeOutputRequests();
        } catch (const std::bad_alloc&) {
            ResetIncompleteCompositeOutputRequestsForRetry();
            m_RenderDirty = true;
            return;
        } catch (const std::length_error&) {
            ResetIncompleteCompositeOutputRequestsForRetry();
            m_RenderDirty = true;
            return;
        }
        m_GraphPerformanceStats.lastCompositeRequestBuildMs =
            MillisecondsBetween(compositeBuildBegin, std::chrono::steady_clock::now());
        if (requests.empty() && previewRequests.empty()) {
            m_RenderDirty = false;
            if (!allowBackgroundRenderWorker || !IsAnyRenderBackendBusy()) {
                m_RenderPending = false;
            }
            return;
        }

        m_RenderGeneration =
            Stack::EditorRenderScheduling::NextGlobalGeneration();
        m_LastSubmittedRenderRevision = m_RenderRevision;
        m_RenderDirty = false;
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(previewRequests.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = static_cast<int>(requests.size());
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;

        if (allowBackgroundRenderWorker) {
            const auto snapshotBuildBegin = std::chrono::steady_clock::now();
            EditorRenderWorker::Snapshot snapshot;
            if (!TryBuildRenderSnapshot(
                    m_RenderGeneration,
                    snapshot)) {
                m_RenderPending = false;
                m_RenderDirty = true;
                m_LastSubmittedRenderRevision =
                    m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
                return;
            }
            m_GraphPerformanceStats.lastSnapshotBuildMs =
                MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
            snapshot.outputConnected = false;
            snapshot.compositeOutputs = std::move(requests);
            snapshot.previews = std::move(previewRequests);
            m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
            m_GraphPerformanceStats.lastSubmittedCompositeCount = static_cast<int>(snapshot.compositeOutputs.size());
            m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
            m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
            m_RenderPending = true;
            if (!SubmitRenderSnapshot(std::move(snapshot))) {
                m_RenderPending = false;
                m_RenderDirty = true;
                m_LastSubmittedRenderRevision =
                    m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
                ResetIncompleteCompositeOutputRequestsForRetry();
            }
        } else {
            m_RenderPending = false;
            for (const EditorRenderWorker::CompositeOutputRequest& request : requests) {
                int texW = 0;
                int texH = 0;
                std::vector<unsigned char> pixels;
                try {
                    pixels = GetCompositePixelsForOutputNode(
                        request.outputNodeId,
                        texW,
                        texH);
                } catch (const std::bad_alloc&) {
                    ResetCompositeOutputRequestForRetry(
                        request.outputNodeId);
                    continue;
                } catch (const std::length_error&) {
                    ResetCompositeOutputRequestForRetry(
                        request.outputNodeId);
                    continue;
                }
                (void)PublishCompositeOutputPixels(
                    request.outputNodeId,
                    std::move(pixels),
                    texW,
                    texH,
                    request.dirtyGeneration,
                    request.chainFingerprint);
            }
        }
        return;
    }
    if (!m_RenderDirty && !previewRequests.empty()) {
        submitPreviewOnlyRequests(previewRequests);
        return;
    }
    if (rawStage && activeOutputNodeId <= 0) {
        m_RawWorkspaceStaleRenderStatusText = "The project's RAW development stage is unavailable.";
        m_RenderDirty = false;
        m_RenderPending = false;
        return;
    }
    if (!rawStage && !m_Project->graph.IsOutputConnected()) {
        InvalidateRenderSnapshotsBefore(m_RenderGeneration + 1);
        const std::string outputDiagnostic = m_Project->graph.GetOutputConnectionDiagnostic();
        if (!outputDiagnostic.empty() && outputDiagnostic != m_LastOutputConnectionDiagnostic) {
            PostNotification(
                UiNotificationSeverity::Error,
                outputDiagnostic,
                "graph-output-compound-unresolved");
        }
        m_LastOutputConnectionDiagnostic = outputDiagnostic;
        m_RenderDirty = false;
        ClearViewportOutputTiles();
        m_Pipeline.ClearOutput();
        submitPreviewOnlyRequests(previewRequests);
        m_HdrMergeRenderingNodeIds.clear();
        return;
    }
    m_LastOutputConnectionDiagnostic.clear();
    if (m_RenderRevision <= m_LastSubmittedRenderRevision) {
        // Visible layer thumbnails become eligible after the main image has
        // settled. Their refresh must not require another document edit.
        submitPreviewOnlyRequests(previewRequests);
        return;
    }
    if (blockInvalidHdrMergeOutput()) {
        return;
    }

    m_RenderGeneration =
        Stack::EditorRenderScheduling::NextGlobalGeneration();
    m_LastSubmittedRenderRevision = m_RenderRevision;
    m_RenderDirty = false;
    m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(previewRequests.size());
    m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
    m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
    m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;

    if (allowBackgroundRenderWorker &&
        rawWorkspaceActive &&
        m_RawWorkspaceRootTabActive &&
        TrySubmitRawRenderCommand(
            m_RenderGeneration,
            previewRequests)) {
        recordHdrMergeSubmission();
        m_RenderPending = true;
        m_GraphPerformanceStats.lastSnapshotBuildMs = 0.0;
        return;
    }

    if (allowBackgroundRenderWorker) {
        recordHdrMergeSubmission();
        m_RenderPending = true;
        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot;
        if (!TryBuildRenderSnapshot(
                m_RenderGeneration,
                snapshot)) {
            m_RenderPending = false;
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision =
                m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            m_HdrMergeSubmittedNodesByGeneration.erase(
                m_RenderGeneration);
            for (const int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeRequestedGenerations.erase(nodeId);
                m_HdrMergeRenderingNodeIds.erase(nodeId);
            }
            return;
        }
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.previews = std::move(previewRequests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        if (!SubmitRenderSnapshot(std::move(snapshot))) {
            m_RenderPending = false;
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision =
                m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            m_HdrMergeSubmittedNodesByGeneration.erase(m_RenderGeneration);
            for (const int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeRequestedGenerations.erase(nodeId);
                m_HdrMergeRenderingNodeIds.erase(nodeId);
            }
        }
    } else {
        if (rawWorkspaceActive &&
            (IsAnyRenderBackendBusy() ||
             (m_NodeBrowserRenderWorkerAvailable && m_NodeBrowserRenderWorker.IsBusy()))) {
            InvalidateRenderSnapshotsBefore(m_RenderGeneration);
            m_NodeBrowserRenderWorker.InvalidateSnapshotsBefore(m_RenderGeneration);
            m_RenderPending = true;
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision = m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            return;
        }

        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot;
        if (!TryBuildRenderSnapshot(
                m_RenderGeneration,
                snapshot)) {
            m_RenderDirty = true;
            m_LastSubmittedRenderRevision =
                m_RenderRevision > 0 ? m_RenderRevision - 1 : 0;
            return;
        }
        if (snapshot.rawRenderPurpose ==
                RawRenderPurpose::ViewportRefinement) {
            m_RawWorkspaceFullResolutionPreviewRequestGeneration =
                snapshot.generation;
        }
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        m_Pipeline.SetPreviewMaxDimension(snapshot.previewMaxDimension);
        m_Pipeline.SetRawDevelopmentInteractivePreview(
            snapshot.rawRenderPurpose ==
                RawRenderPurpose::InteractivePresentation &&
            snapshot.telemetry.interactionActive);
        m_Pipeline.SetRawDevelopmentColorWarpMaskPolicy(
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport ||
                    snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitInspection
                ? 2048
                : (snapshot.telemetry.interactionActive ? 384 : 1024),
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport);
        m_Pipeline.SetRawDevelopmentViewportValidationEnabled(
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport ||
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitInspection ||
            snapshot.rawWorkspace.analysisRequested);
        m_Pipeline.SetRawDevelopmentPreferredCacheInputStage(
            snapshot.rawWorkspace.preferredCacheInputStage);
        m_Pipeline.SetRawDevelopmentGlobalExposureInteraction(
            snapshot.rawWorkspace.globalExposureInteractionActive);
        m_Pipeline.SetRawDevelopmentAnalysisEnabled(snapshot.rawWorkspace.analysisRequested);
        m_Pipeline.SetRawDevelopmentGraphScopeReadbackRequest(
            snapshot.rawWorkspace.graphScopeStage,
            snapshot.rawWorkspace.graphScopeStage == RawDevelopmentGraphScopeStage::None
                ? 0
                : std::clamp(
                    snapshot.rawWorkspace.graphScopeMaxDimension,
                    kRawWorkspaceGraphScopeMaxDimension,
                    2048));
        m_Pipeline.SetRawDevelopmentGradingScopeReadbackRequest(
            snapshot.rawWorkspace.gradingScopeSource,
            snapshot.rawWorkspace.gradingScopeSource ==
                    RawDevelopmentGradingScopeSource::None
                ? 0
                : kRawWorkspaceGradingScopeMaxDimension,
            snapshot.generation,
            snapshot.rawWorkspace.sourceKey);
        const auto mainRenderBegin = std::chrono::steady_clock::now();
        m_Pipeline.ExecuteGraph(snapshot.graph);
        const auto graphExecuteEnd = std::chrono::steady_clock::now();
        const GraphExecutionStats& graphExecutionStats =
            m_Pipeline.GetLastGraphExecutionStats();
        const std::string rawRgbDenoiseError =
            rawWorkspaceActive
                ? m_Pipeline.GetLastRawRgbDenoiseError()
                : std::string();
        const bool preserveRawPresentationForTargetHover =
            rawWorkspaceActive &&
            Stack::RawLocalRangeTargetInteraction::
                ShouldPreserveBasePresentation(
                    snapshot.rawWorkspace.localRangeTargetSampleRequested &&
                        snapshot.rawWorkspace.localRangeTargetHoverSample,
                    snapshot.rawWorkspace.localRangeOverlayMode ==
                        "target-outline",
                    snapshot.rawWorkspace.localRangeTargetPreview.enabled,
                    snapshot.rawWorkspace.localRangeTargetPreview
                        .interactionEditing);
        Stack::EditorRenderScheduling::RawPresentationExtent
            expectedRawPresentationExtent;
        if (rawWorkspaceActive && snapshot.rawWorkspace.hasRecipe) {
            const Stack::RawRecipe::RawCropRotationRecipe& crop =
                snapshot.rawWorkspace.recipe.cropRotation;
            expectedRawPresentationExtent =
                Stack::EditorRenderScheduling::
                    ResolveExpectedRawPresentationExtent(
                        snapshot.rawWorkspace.fullFrameWidth,
                        snapshot.rawWorkspace.fullFrameHeight,
                        crop.rotationDegrees,
                        crop.cropEnabled,
                        crop.cropX,
                        crop.cropY,
                        crop.cropWidth,
                        crop.cropHeight);
        }
        bool publishedRawPresentation = false;
        if (rawWorkspaceActive &&
            !preserveRawPresentationForTargetHover &&
            m_Pipeline.GetOutputTexture() != 0) {
            EditorRenderWorker::SharedTextureResult publishedTexture;
            publishedTexture.texture =
                m_Pipeline.PublishSharedOutputTexture(
                    publishedTexture.width,
                    publishedTexture.height,
                    true);
            if (AdoptRawWorkspacePresentationTexture(publishedTexture)) {
                // The presentation copy is owned by EditorModule rather than
                // RenderPipeline. Graph execution can now invalidate or
                // recycle its internal output without touching a texture
                // already referenced by the current ImGui draw list.
                QueueViewportOutputTileSetRelease(m_ViewportOutputTiles);
                publishedRawPresentation = true;
                if (snapshot.rawWorkspace.localRangeTargetPreview
                        .interactionEditing) {
                    m_RawWorkspaceLocalRangeTargetPreview.interactionEditing =
                        false;
                }
            }
        } else if (rawWorkspaceActive &&
                   m_Pipeline.GetOutputTexture() == 0) {
            m_RawWorkspaceStaleRenderStatusText =
                !rawRgbDenoiseError.empty()
                    ? rawRgbDenoiseError
                    : (!graphExecutionStats.lastSpecializedFailure.empty()
                        ? graphExecutionStats.lastSpecializedFailure
                        : "The RAW graph produced no displayable image.");
            PostNotification(
                UiNotificationSeverity::Error,
                "Preview retained as stale: " +
                    m_RawWorkspaceStaleRenderStatusText,
                "raw-workspace-render-stale");
        }
        if (rawWorkspaceActive) {
            int overlayWidth = 0;
            int overlayHeight = 0;
            Stack::Renderer::ScopedGLTexture renderedOverlay(
                m_Pipeline.TakeRawDevelopmentLocalRangeOverlayTexture(
                    overlayWidth,
                    overlayHeight));

            const bool renderedOverlayMatches =
                renderedOverlay &&
                overlayWidth > 0 &&
                overlayHeight > 0 &&
                !snapshot.rawWorkspace.localRangeOverlayMode.empty() &&
                snapshot.rawWorkspace.localRangeOverlayMode != "none" &&
                snapshot.rawWorkspace.localRangeOverlayMode ==
                    m_RawWorkspaceLocalRangeOverlayMode &&
                (snapshot.rawWorkspace.localRangeOverlayMode != "target-outline" ||
                    snapshot.rawWorkspace.localRangeTargetPreview.generation ==
                        m_RawWorkspaceLocalRangeTargetPreview.generation);
            if (renderedOverlayMatches) {
                std::string acceptedSourceKey =
                    snapshot.rawWorkspace.sourceKey;
                std::string acceptedMode =
                    snapshot.rawWorkspace.localRangeOverlayMode;
                ClearRawWorkspaceLocalRangeOverlayState();
                m_RawWorkspaceLocalRangeOverlayTexture =
                    renderedOverlay.Release();
                m_RawWorkspaceLocalRangeOverlayWidth = overlayWidth;
                m_RawWorkspaceLocalRangeOverlayHeight = overlayHeight;
                m_RawWorkspaceLocalRangeOverlaySourceKey =
                    std::move(acceptedSourceKey);
                m_RawWorkspaceLocalRangeOverlayAcceptedMode =
                    std::move(acceptedMode);
                m_RawWorkspaceLocalRangeOverlayGeneration =
                    m_RenderGeneration;
                m_RawWorkspaceLocalRangeOverlayTargetPreviewGeneration =
                    snapshot.rawWorkspace.localRangeTargetPreview.generation;
                if (snapshot.rawWorkspace.localRangeOverlayMode ==
                    "target-outline") {
                    m_RawWorkspaceLocalRangeTargetPreviewRefined =
                        m_Pipeline
                            .IsRawDevelopmentLocalRangeTargetPreviewRefined();
                    m_RawWorkspaceLocalRangeTargetPreviewRefinementPending =
                        m_Pipeline
                            .IsRawDevelopmentLocalRangeTargetPreviewRefinementPending();
                }
            } else if (!preserveRawPresentationForTargetHover) {
                ClearRawWorkspaceLocalRangeOverlayState();
            }
        }
        if (rawWorkspaceActive && publishedRawPresentation && !snapshot.rawWorkspace.analysisRequested &&
            snapshot.rawWorkspace.graphScopeStage != RawDevelopmentGraphScopeStage::None) {
            CacheRawWorkspaceGraphScopeReadback(snapshot.rawWorkspace.sourceKey,
                snapshot.rawWorkspace.graphScopeInputFingerprint, m_Pipeline.GetRawDevelopmentGraphScopeReadback());
            RestoreRawWorkspaceGraphScopeReadbackForActiveLabTool();
        }
        if (rawWorkspaceActive && snapshot.rawWorkspace.analysisRequested) {
            m_RawWorkspaceViewTransformInputStats = m_Pipeline.GetRawDevelopmentViewTransformInputStats();
            m_RawWorkspaceFinalDisplayStats = m_Pipeline.GetRawDevelopmentFinalDisplayStats();
            m_RawWorkspaceStageStatsReadbacks = m_Pipeline.GetRawDevelopmentStageStatsReadbacks();
            m_RawWorkspaceGraphScopeReadback =
                m_Pipeline.GetRawDevelopmentGraphScopeReadback();
            if (publishedRawPresentation) {
                m_BracketingPresentedSourceHash=snapshot.rawWorkspace.sourceHash;
                CacheRawWorkspaceGraphScopeReadback(
                    snapshot.rawWorkspace.sourceKey,
                    snapshot.rawWorkspace.graphScopeInputFingerprint,
                    m_RawWorkspaceGraphScopeReadback);
            }
            m_RawWorkspaceStartPointDiagnostics =
                m_Pipeline.BuildRawDevelopmentStartPointDiagnostics(
                    snapshot.rawWorkspace.sourceKey);
            m_RawWorkspaceAnalysis =
                Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
                    ToRawCurrentFrameInputStats(m_RawWorkspaceViewTransformInputStats),
                    snapshot.rawWorkspace.sourceKey);
            // The automatic starting-point UI is archived. Keep its backend
            // callable for explicit validation/precise jobs, but ordinary
            // manual renders must not synthesize recommendations or render
            // hidden candidates after every edit.
            Stack::EditorModuleTypes::ClearRawStartingPointCandidateRenderQueue(
                m_RawWorkspaceStartPointCandidateRenderQueue);
            m_RawWorkspaceStartPointCandidateRenderResults.clear();
            AdoptRawWorkspacePreciseAppliedRender();
        } else if (!rawWorkspaceActive) {
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
        if (rawWorkspaceActive && snapshot.rawWorkspace.localRangeTargetSampleRequested) {
            EditorRenderWorker::Result targetSampleResult;
            targetSampleResult.generation = m_RenderGeneration;
            targetSampleResult.previewMaxDimension = snapshot.previewMaxDimension;
            targetSampleResult.rawWorkspace.sourceKey =
                snapshot.rawWorkspace.sourceKey;
            targetSampleResult.rawWorkspace.localRangeTargetSample.u =
                snapshot.rawWorkspace.localRangeTargetSampleU;
            targetSampleResult.rawWorkspace.localRangeTargetSample.v =
                snapshot.rawWorkspace.localRangeTargetSampleV;
            float sceneEv = 0.0f;
            float sceneLuma = 0.0f;
            float sampleU = 0.0f;
            float sampleV = 0.0f;
            std::array<float, 3> sceneRgb = { 0.0f, 0.0f, 0.0f };
            std::uint32_t authoredZoneHitBits = 0;
            float strongestAuthoredZoneWeight = 0.0f;
            if (m_Pipeline.GetRawDevelopmentLocalRangeTargetSample(
                    sceneEv,
                    sceneLuma,
                    sampleU,
                    sampleV,
                    &sceneRgb,
                    &authoredZoneHitBits,
                    &strongestAuthoredZoneWeight)) {
                targetSampleResult.rawWorkspace.localRangeTargetSample.valid = true;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneEv = sceneEv;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneLuma = sceneLuma;
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneR = sceneRgb[0];
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneG = sceneRgb[1];
                targetSampleResult.rawWorkspace.localRangeTargetSample.sceneB = sceneRgb[2];
                targetSampleResult.rawWorkspace.localRangeTargetSample.u = sampleU;
                targetSampleResult.rawWorkspace.localRangeTargetSample.v = sampleV;
                targetSampleResult.rawWorkspace.localRangeTargetSample
                    .authoredZoneHitBits = authoredZoneHitBits;
                targetSampleResult.rawWorkspace.localRangeTargetSample
                    .strongestAuthoredZoneWeight =
                    strongestAuthoredZoneWeight;
            }
            AdoptRawWorkspaceLocalRangeTargetSampleFromResult(targetSampleResult);
        }
        const auto mainRenderEnd = std::chrono::steady_clock::now();
        m_GraphPerformanceStats.lastMainRenderMs =
            MillisecondsBetween(mainRenderBegin, mainRenderEnd);
        m_GraphPerformanceStats.lastMainGraphExecuteMs =
            MillisecondsBetween(mainRenderBegin, graphExecuteEnd);
        m_GraphPerformanceStats.lastMainPostExecuteMs =
            MillisecondsBetween(graphExecuteEnd, mainRenderEnd);
        m_GraphPerformanceStats.lastRawWorkspaceRender = rawWorkspaceActive;
        m_GraphPerformanceStats.lastRawInteractivePreview =
            rawWorkspaceActive && snapshot.previewMaxDimension > 0;
        m_GraphPerformanceStats.lastRawAnalysisCaptured =
            rawWorkspaceActive && snapshot.rawWorkspace.analysisRequested;
        m_GraphPerformanceStats.lastRawPreviewMaxDimension =
            snapshot.previewMaxDimension;
        m_GraphPerformanceStats.lastMainGraphStats = graphExecutionStats;
        m_GraphPerformanceStats.lastPreviewRenderMs = 0.0f;
        m_GraphPerformanceStats.lastCompositeRenderMs = 0.0f;
        m_GraphPerformanceStats.lastRenderedPreviewCount = 0;
        m_GraphPerformanceStats.lastRenderedCompositeCount = 0;
        m_GraphPerformanceStats.lastMainOutputTiled = false;
        m_GraphPerformanceStats.lastMainOutputTileCount = 0;
        m_GraphPerformanceStats.lastMainRegionPlanAvailable = false;
        m_GraphPerformanceStats.lastMainRegionPlanTileable = false;
        m_GraphPerformanceStats.lastMainRegionPlanHaloX = 0;
        m_GraphPerformanceStats.lastMainRegionPlanHaloY = 0;
        m_GraphPerformanceStats.lastMainRegionPlanReason.clear();
        m_RenderPending = false;
        if (rawWorkspaceActive) {
            if (publishedRawPresentation) {
                m_RawWorkspacePreviewOutputKind =
                    RawWorkspacePreviewOutputKind::SingleTexture;
                m_ViewportOutputRawWorkspaceSourceKey =
                    snapshot.rawWorkspace.sourceKey;
                m_ViewportOutputExpectedNativeWidth =
                    expectedRawPresentationExtent.width;
                m_ViewportOutputExpectedNativeHeight =
                    expectedRawPresentationExtent.height;
                m_ViewportOutputNativeExtentVerified =
                    Stack::EditorRenderScheduling::
                        IsRawPresentationNativeExtent(
                            m_RawWorkspacePresentationTexture.width,
                            m_RawWorkspacePresentationTexture.height,
                            m_ViewportOutputExpectedNativeWidth,
                            m_ViewportOutputExpectedNativeHeight);
                m_ViewportOutputPreviewMaxDimension =
                    Stack::EditorRenderScheduling::
                        ResolveRawPresentationPreviewMaxDimension(
                            snapshot.previewMaxDimension,
                            m_RawWorkspacePresentationTexture.width,
                            m_RawWorkspacePresentationTexture.height,
                            m_ViewportOutputExpectedNativeWidth,
                            m_ViewportOutputExpectedNativeHeight);
                m_ViewportOutputRenderGeneration = m_RenderGeneration;
            }
            const bool settledRenderBlocked =
                m_RenderDirty ||
                m_Pipeline.IsRawRgbDenoiseAsyncPending() ||
                !rawRgbDenoiseError.empty();
            const auto nativeRefinementCompletion =
                Stack::EditorRenderScheduling::
                    ClassifyRawNativeRefinementCompletion(
                        snapshot.rawRenderPurpose,
                        snapshot.previewMaxDimension,
                        snapshot.telemetry.fullFrameRefinementRequested,
                        snapshot.telemetry.fullFrameEstimatedWorkingSetBytes > 0,
                        snapshot.telemetry.fullFrameRefinementBudgetAllowed,
                        publishedRawPresentation &&
                            (snapshot.rawRenderPurpose ==
                                 RawRenderPurpose::ExplicitInspection ||
                             Stack::EditorRenderScheduling::
                                 RawRenderResultOwnsTrackedRequest(
                                     RawRenderPurpose::ViewportRefinement,
                                     snapshot.rawRenderPurpose,
                                     snapshot.generation,
                                     m_RawWorkspaceFullResolutionPreviewRequestGeneration)),
                        m_ViewportOutputNativeExtentVerified,
                        settledRenderBlocked);
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
                publishedRawPresentation &&
                m_ViewportOutputNativeExtentVerified &&
                m_ViewportOutputPreviewMaxDimension == 0 &&
                snapshot.rawRenderPurpose !=
                    RawRenderPurpose::ViewportRefinement;
            const bool rearmNativeAfterProxy =
                Stack::EditorRenderScheduling::
                    ShouldRearmRawNativeRefinementAfterAcceptedPresentation(
                        m_ViewportOutputPreviewMaxDimension,
                        nativeRefinementCompletion,
                        publishedRawPresentation);
            if (completedBudgetFallback &&
                !settledRenderBlocked) {
                m_RawWorkspaceFullResolutionPreviewPending = true;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFullResolutionPreviewDeferredByBudget = true;
                m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes =
                    m_RawWorkspaceVramWorkingBudgetBytes;
                m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
                m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
                m_RawWorkspaceFastPreviewUntilTime = -1.0;
            } else if ((completedExplicitFullQuality ||
                        completedSettledDisplayPreview) &&
                       !settledRenderBlocked) {
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
            } else if (completedExtentMismatch &&
                       !settledRenderBlocked) {
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
            } else if (rearmNativeAfterProxy) {
                m_RawWorkspaceFullResolutionPreviewPending = true;
                m_RawWorkspaceFullResolutionPreviewRequested = false;
                m_RawWorkspaceFullResolutionPreviewDeferredByBudget = false;
                m_RawWorkspaceFullResolutionPreviewDeferredBudgetBytes = 0;
                m_RawWorkspaceFullResolutionPreviewRequestGeneration = 0;
                m_RawWorkspaceFullResolutionPreviewRetryCount = 0;
            }
            if (Stack::EditorRenderScheduling::ShouldRefreshRawProjectCover(
                    completedExplicitFullQuality,
                    completedSettledDisplayPreview,
                    m_RenderDirty)) {
                RefreshPendingMultiFrameProjectCover();
            }
        }
        ApplyToneCurveAutoRewriteFeedback(m_Pipeline.GetToneCurveAutoRewriteFeedback());
        for (int nodeId : activeHdrMergeNodeIds) {
            m_HdrMergeRequestedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeCompletedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeFailureMessages.erase(nodeId);
        }
        m_HdrMergeRenderingNodeIds.clear();
        m_LastCompletedRenderGeneration = m_RenderGeneration;
    }
}
