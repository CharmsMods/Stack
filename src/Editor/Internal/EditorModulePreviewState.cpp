#include "Editor/EditorModule.h"

#include "App/settings/AppearanceTheme.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <sstream>

#include <imgui.h>

namespace {

constexpr ImVec4 kEditorWorkspaceBaseColor = ImVec4(0.016f, 0.231f, 0.274f, 1.0f);

} // namespace

bool EditorModule::CanToggleActiveAutoGainMaskPreview() const {
    if (GetViewportMode() != ViewportMode::SingleOutputPreview ||
        m_ActiveSubWindow != EditorSubWindow::ComplexNode) {
        return false;
    }
    const EditorNodeGraph::Node* node = m_Project->graph.FindNode(m_ActiveComplexNodeId);
    return node && node->kind == EditorNodeGraph::NodeKind::RawDetailFusion;
}

void EditorModule::ToggleActiveAutoGainMaskPreview() {
    if (!CanToggleActiveAutoGainMaskPreview()) {
        ClearAutoGainMaskPreview();
        return;
    }
    const int nodeId = m_ActiveComplexNodeId;
    m_AutoGainMaskPreviewNodeId = (m_AutoGainMaskPreviewNodeId == nodeId) ? -1 : nodeId;
    m_RenderDirty = true;
    ++m_RenderRevision;
    m_LastRenderDirtyTime = ImGui::GetTime();
}

void EditorModule::ClearAutoGainMaskPreview() {
    if (m_AutoGainMaskPreviewNodeId <= 0) {
        return;
    }
    m_AutoGainMaskPreviewNodeId = -1;
    m_RenderDirty = true;
    ++m_RenderRevision;
    m_LastRenderDirtyTime = ImGui::GetTime();
}

std::uint64_t EditorModule::GetPreviewNodeRevision(int previewNodeId) const {
    if (IsEditingRawLayerMaskGraph()) return std::max<std::uint64_t>(1, m_RenderRevision);
    const EditorNodeGraph::Node* node = m_Project->graph.FindNode(previewNodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        const EditorNodeGraph::Link* input =
            m_Project->graph.FindInputLink(previewNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            return 0;
        }
        return std::max<std::uint64_t>(
            1,
            std::max(GetNodeDirtyGeneration(previewNodeId), GetNodeDirtyGeneration(input->fromNodeId)));
    }
    if (node && node->kind == EditorNodeGraph::NodeKind::FrequencyFilter) {
        const EditorNodeGraph::Link* input =
            m_Project->graph.FindAnyInputLink(
                previewNodeId, EditorNodeGraph::kChannelInputSocketId);
        if (!input) return 0;
        return std::max<std::uint64_t>(
            1,
            std::max(
                GetNodeDirtyGeneration(previewNodeId),
                GetNodeDirtyGeneration(input->fromNodeId)));
    }
    const EditorNodeGraph::Link* input =
        m_Project->graph.FindAnyInputLink(previewNodeId,
            node && node->kind == EditorNodeGraph::NodeKind::Scope
                ? EditorNodeGraph::kScopeInputSocketId : EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return 0;
    }
    return std::max<std::uint64_t>(1,
        std::max(GetNodeDirtyGeneration(previewNodeId), GetNodeDirtyGeneration(input->fromNodeId)));
}

const EditorModule::GraphPreviewPixels* EditorModule::GetCachedPreviewPixelsForNode(int previewNodeId) const {
    const auto it = m_PreviewPixelCache.find(previewNodeId);
    return it != m_PreviewPixelCache.end() ? &it->second : nullptr;
}

std::uint64_t EditorModule::GetScopeNodeRevision(int sourceNodeId) const {
    const auto* cached = GetCachedPreviewPixelsForNode(sourceNodeId);
    return cached ? cached->revision : 0;
}

ImVec4 EditorModule::GetWorkspaceBaseColor() const {
    if (const StackAppearance::AppearanceManager* appearance = GetAppearance()) {
        return appearance->GetEffectiveWindowBackgroundColor();
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        return ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    }
    return kEditorWorkspaceBaseColor;
}

void EditorModule::RenderGraphPerformancePopup(const ImVec2& graphPaneMin, const ImVec2& graphPaneMax) {
    if (!m_ShowGraphPerformancePopup) {
        return;
    }

    const ImVec2 popupPos(graphPaneMin.x + 18.0f, graphPaneMin.y + 18.0f);
    const float maxWidth = std::max(260.0f, std::min(360.0f, graphPaneMax.x - graphPaneMin.x - 36.0f));
    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(260.0f, 0.0f),
        ImVec2(maxWidth, std::numeric_limits<float>::max()));
    ImGui::SetNextWindowBgAlpha(0.88f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing;
    if (!ImGui::Begin("Graph Performance##Overlay", nullptr, flags)) {
        ImGui::End();
        return;
    }

    const GraphPerformanceStats& stats = m_GraphPerformanceStats;
    const GraphExecutionStats& cacheStats = stats.lastMainGraphStats;
    const bool previewDeferred = ShouldDeferPreviewLikeWork();
    const int totalImageCacheEvents = cacheStats.imageCacheHits + cacheStats.imageCacheMisses;
    const int totalMaskCacheEvents = cacheStats.maskCacheHits + cacheStats.maskCacheMisses;
    const int totalRawCacheEvents = cacheStats.rawStageCacheHits + cacheStats.rawStageCacheMisses;

    ImGui::TextUnformatted("Graph Performance");
    ImGui::Separator();
    ImGui::Text("Render dirty: %s", m_RenderDirty ? "Yes" : "No");
    ImGui::Text("Worker busy: %s",
        (m_RenderPending || IsAnyRenderBackendBusy()) ? "Yes" : "No");
    ImGui::Text("Preview idle gate: %s", previewDeferred ? "Deferred" : "Open");
    ImGui::Text("Invalidation: %s", stats.lastInvalidationWasFull ? "Full" : "Local");
    ImGui::Text("Touched node: %d", stats.lastTouchedNodeId);
    ImGui::Text("Dirty nodes: %d", stats.lastDirtyNodeCount);
    ImGui::Text("Dirty outputs: %d", stats.lastDirtyOutputCount);
    ImGui::Spacing();
    ImGui::Text("Snapshot build: %.2f ms", stats.lastSnapshotBuildMs);
    ImGui::Text("Preview request build: %.2f ms", stats.lastPreviewRequestBuildMs);
    ImGui::Text("Composite request build: %.2f ms", stats.lastCompositeRequestBuildMs);
    ImGui::Spacing();
    ImGui::Text("Submitted gen: %llu", static_cast<unsigned long long>(stats.lastSubmittedGeneration));
    ImGui::Text("Main output submitted: %s", stats.lastSubmissionIncludedMainOutput ? "Yes" : "No");
    ImGui::Text("Submitted previews: %d", stats.lastSubmittedPreviewCount);
    ImGui::Text("Submitted composite: %d", stats.lastSubmittedCompositeCount);
    ImGui::Spacing();
    ImGui::Text("Main render: %.2f ms", stats.lastMainRenderMs);
    ImGui::Text("Graph execute: %.2f ms", stats.lastMainGraphExecuteMs);
    ImGui::Text("Post-render work: %.2f ms", stats.lastMainPostExecuteMs);
    if (stats.lastRawWorkspaceRender) {
        ImGui::Text(
            "RAW render: %s%s",
            stats.lastRawInteractivePreview ? "Interactive proxy" : "Settled full resolution",
            stats.lastRawAnalysisCaptured ? " + analysis" : "");
        if (stats.lastRawPreviewMaxDimension > 0) {
            ImGui::Text("RAW proxy max edge: %d px", stats.lastRawPreviewMaxDimension);
        }
        if (!stats.lastRawRenderPurpose.empty()) {
            ImGui::Text("RAW purpose: %s", stats.lastRawRenderPurpose.c_str());
            ImGui::Text(
                "RAW queue / worker / adopt: %.2f / %.2f / %.2f ms",
                stats.lastRawQueueWaitMs,
                stats.lastRawWorkerTotalMs,
                stats.lastRawUiAdoptionMs);
            ImGui::Text(
                "RAW adaptive scale: %.0f%% / viewport cost estimate: %.2f ms",
                static_cast<double>(stats.rawAdaptivePreviewScale) * 100.0,
                stats.rawAdaptiveFrameTimeMs);
            ImGui::Text(
                "RAW source / publication / readback: %.2f / %.2f / %.2f MiB%s",
                static_cast<double>(stats.lastRawSourceTransferredBytes) /
                    (1024.0 * 1024.0),
                static_cast<double>(stats.lastRawPublishedTextureBytes) /
                    (1024.0 * 1024.0),
                static_cast<double>(stats.lastRawReadbackTransferredBytes) /
                    (1024.0 * 1024.0),
                stats.lastRawSuperseded ? " (superseded)" : "");
            if (stats.lastRawFullFrameRefinementRequested) {
                ImGui::Text(
                    "RAW full-frame release refine: %s (estimated %.0f MiB)",
                    stats.lastRawFullFrameRefinementBudgetAllowed
                        ? "Allowed"
                        : "Retained 1.0x fit proxy",
                    static_cast<double>(
                        stats.lastRawFullFrameEstimatedWorkingSetBytes) /
                        (1024.0 * 1024.0));
            }
            if (stats.rawVramWorkingBudgetBytes > 0u) {
                ImGui::Text(
                    "RAW VRAM working / available: %.0f / %.0f MiB%s",
                    static_cast<double>(stats.rawVramWorkingBudgetBytes) /
                        (1024.0 * 1024.0),
                    static_cast<double>(stats.rawVramAvailableBytes) /
                        (1024.0 * 1024.0),
                    stats.rawMinimumMemoryTiling
                        ? " (minimum-memory tiling)"
                        : "");
            }
            const Raw::RawGpuResidencySnapshot sharedResidency =
                Raw::RawGpuImageLease::Residency();
            ImGui::Text(
                "RAW shared image leases: %.1f MiB (presentation %.1f / overlay %.1f)",
                static_cast<double>(sharedResidency.totalBytes) /
                    (1024.0 * 1024.0),
                static_cast<double>(sharedResidency.familyBytes[
                    static_cast<std::size_t>(
                        Raw::RawGpuImageFamily::Presentation)]) /
                    (1024.0 * 1024.0),
                static_cast<double>(sharedResidency.familyBytes[
                    static_cast<std::size_t>(
                        Raw::RawGpuImageFamily::AuxiliaryOverlay)]) /
                    (1024.0 * 1024.0));
        }
    }
    ImGui::Text("Main tiling: %s (%d)", stats.lastMainOutputTiled ? "Yes" : "No", stats.lastMainOutputTileCount);
    if (stats.lastMainRegionPlanAvailable) {
        ImGui::Text(
            "Region plan: %s, halo %d x %d",
            stats.lastMainRegionPlanTileable ? "Tileable" : "Full frame",
            stats.lastMainRegionPlanHaloX,
            stats.lastMainRegionPlanHaloY);
        if (!stats.lastMainRegionPlanReason.empty()) {
            ImGui::TextWrapped("Region reason: %s", stats.lastMainRegionPlanReason.c_str());
        }
    }
    ImGui::Text("Preview render: %.2f ms (%d)", stats.lastPreviewRenderMs, stats.lastRenderedPreviewCount);
    ImGui::Text("Composite render: %.2f ms (%d)", stats.lastCompositeRenderMs, stats.lastRenderedCompositeCount);
    ImGui::Spacing();
    ImGui::Text(
        "Project save: %s",
        m_Project->saves.IsBusy() ? "Background write active" : "Idle");
    ImGui::Text(
        "Save snapshot (UI): %.2f ms",
        stats.lastProjectSaveSnapshotMs);
    ImGui::Text(
        "Save commit (worker): %.2f ms",
        stats.lastProjectSaveCommitMs);
    if (stats.lastSliceImportWidth > 0 && stats.lastSliceImportHeight > 0) {
        ImGui::Spacing();
        ImGui::Text(
            "Last slice import: %d x %d (%.1f MB)",
            stats.lastSliceImportWidth,
            stats.lastSliceImportHeight,
            static_cast<double>(stats.lastSliceImportPixelBytes) / (1024.0 * 1024.0));
        ImGui::Text("Queue wait: %.2f ms", stats.lastSliceImportQueueMs);
        ImGui::Text("Decode: %.2f ms", stats.lastSliceImportDecodeMs);
        ImGui::Text("Thumbnail: %.2f ms", stats.lastSliceImportPreviewMs);
        ImGui::Text("Embed copy: %.2f ms", stats.lastSliceImportStorageCopyMs);
        ImGui::Text(
            "Embed PNG: %.2f ms (%.1f MB)",
            stats.lastSliceImportEmbedMs,
            static_cast<double>(stats.lastSliceImportEmbeddedBytes) / (1024.0 * 1024.0));
    }
    ImGui::Spacing();
    ImGui::Text(
        "Image cache: %d hit / %d miss%s",
        cacheStats.imageCacheHits,
        cacheStats.imageCacheMisses,
        totalImageCacheEvents == 0 ? " (idle)" : "");
    ImGui::Text(
        "Mask cache: %d hit / %d miss%s",
        cacheStats.maskCacheHits,
        cacheStats.maskCacheMisses,
        totalMaskCacheEvents == 0 ? " (idle)" : "");
    ImGui::Text(
        "RAW stage cache: %d hit / %d miss%s",
        cacheStats.rawStageCacheHits,
        cacheStats.rawStageCacheMisses,
        totalRawCacheEvents == 0 ? " (idle)" : "");
    const int totalRawPreprocessEvents =
        cacheStats.rawGpuPreprocessDispatches +
        cacheStats.rawPreprocessCacheHits +
        cacheStats.rawCpuPreprocessFallbacks;
    if (totalRawPreprocessEvents > 0) {
        ImGui::Text(
            "RAW cold preprocess: %d GPU / %d cache / %d CPU fallback",
            cacheStats.rawGpuPreprocessDispatches,
            cacheStats.rawPreprocessCacheHits,
            cacheStats.rawCpuPreprocessFallbacks);
        ImGui::Text(
            "R16UI upload / metadata build / metadata upload: %.2f / %.2f / %.2f ms",
            cacheStats.rawSensorUploadMs,
            cacheStats.rawMetadataBuildMs,
            cacheStats.rawMetadataUploadMs);
        ImGui::Text(
            "GPU preprocess submit: %.2f ms",
            cacheStats.rawGpuPreprocessSubmitMs);
        ImGui::Text(
            "CPU normalize / variance: %.2f / %.2f ms",
            cacheStats.rawCpuNormalizationMs,
            cacheStats.rawCpuVarianceMs);
        ImGui::Text(
            "Float uploads corrected / variance: %.2f / %.2f ms",
            cacheStats.rawCorrectedUploadMs,
            cacheStats.rawVarianceUploadMs);
        ImGui::Text(
            "RAW cold transfer R16UI / metadata / float outputs: %.2f / %.3f / %.2f MiB",
            static_cast<double>(cacheStats.rawSensorUploadBytes) /
                (1024.0 * 1024.0),
            static_cast<double>(cacheStats.rawMetadataUploadBytes) /
                (1024.0 * 1024.0),
            static_cast<double>(
                cacheStats.rawCorrectedUploadBytes +
                cacheStats.rawVarianceUploadBytes) /
                (1024.0 * 1024.0));
        if (!cacheStats.lastRawPreprocessFallback.empty()) {
            ImGui::TextWrapped(
                "RAW CPU fallback: %s",
                cacheStats.lastRawPreprocessFallback.c_str());
        }
    }
    if (cacheStats.allocationFailed) {
        ImGui::TextColored(
            ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
            "Graph render failed safely after host-memory exhaustion; the prior presentation was retained.");
    }

    ImGui::Spacing();
    ImGui::Text(
        "Pointwise fusion: %d group / %d nodes / %d passes avoided",
        cacheStats.fusedPointwiseGroups,
        cacheStats.fusedPointwiseNodes,
        cacheStats.avoidedPointwisePasses);
    ImGui::Text(
        "Generated programs: %d hit / %d miss / %d fallback",
        cacheStats.pointwiseProgramCacheHits,
        cacheStats.pointwiseProgramCacheMisses,
        cacheStats.pointwiseFallbacks);
    ImGui::Text(
        "Reductions: %d pass / %d cache hit / %d miss",
        cacheStats.reductionPasses,
        cacheStats.reductionCacheHits,
        cacheStats.reductionCacheMisses);
    for (const ReductionExecutionStats& reduction : cacheStats.reductions) {
        ImGui::Text(
            "Field Mean node %d: %.9g from %llu samples%s",
            reduction.nodeId,
            reduction.value,
            static_cast<unsigned long long>(reduction.sampleCount),
            reduction.cacheHit ? " (cache hit)" : "");
    }
    if (!cacheStats.lastReductionFailure.empty()) {
        ImGui::TextWrapped(
            "Reduction failed at node %d: %s",
            cacheStats.lastReductionFailureNodeId,
            cacheStats.lastReductionFailure.c_str());
    }
    ImGui::Text(
        "RAW stage cache memory: %.1f / %.1f MB",
        static_cast<double>(cacheStats.rawStageCacheBytes) / (1024.0 * 1024.0),
        static_cast<double>(cacheStats.rawStageCacheBudgetBytes) / (1024.0 * 1024.0));
    ImGui::Text(
        "Persistent cache: %.1f / %.1f MB (%d evicted)",
        static_cast<double>(cacheStats.persistentCacheBytes) / (1024.0 * 1024.0),
        static_cast<double>(cacheStats.persistentCacheBudgetBytes) / (1024.0 * 1024.0),
        cacheStats.persistentCacheEvictions);
    ImGui::Text(
        "Transient pool: %.1f / %.1f MB (%d alloc / %d reuse)",
        static_cast<double>(cacheStats.transientPoolBytes) / (1024.0 * 1024.0),
        static_cast<double>(cacheStats.transientPoolBudgetBytes) / (1024.0 * 1024.0),
        cacheStats.transientTargetAllocations,
        cacheStats.transientTargetReuses);
    for (std::size_t index = 0;
         index < std::min<std::size_t>(cacheStats.pointwiseGroups.size(), 4);
         ++index) {
        const PointwiseExecutionGroupStats& group = cacheStats.pointwiseGroups[index];
        std::ostringstream nodes;
        for (std::size_t nodeIndex = 0; nodeIndex < group.authoredNodeIds.size(); ++nodeIndex) {
            if (nodeIndex != 0) nodes << " -> ";
            nodes << group.authoredNodeIds[nodeIndex];
        }
        ImGui::Text(
            "Fused %s: %s, %.1f MB, %.3f ms%s",
            nodes.str().c_str(),
            group.targetFormat.c_str(),
            static_cast<double>(group.targetBytes) / (1024.0 * 1024.0),
            group.cpuSubmitMilliseconds,
            group.programCacheHit ? " (program hit)" : "");
    }
    if (!cacheStats.lastPointwiseFailure.empty()) {
        std::ostringstream nodes;
        for (std::size_t index = 0; index < cacheStats.lastPointwiseFailureNodeIds.size(); ++index) {
            if (index != 0) nodes << ", ";
            nodes << cacheStats.lastPointwiseFailureNodeIds[index];
        }
        ImGui::TextWrapped(
            "Fusion fallback at node(s) %s: %s",
            nodes.str().c_str(),
            cacheStats.lastPointwiseFailure.c_str());
    }

    ImGui::End();
}
