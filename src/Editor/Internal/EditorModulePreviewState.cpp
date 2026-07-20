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
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_ActiveComplexNodeId);
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
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(previewNodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        const EditorNodeGraph::Link* input =
            m_NodeGraph.FindInputLink(previewNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            return 0;
        }
        return std::max<std::uint64_t>(
            1,
            std::max(GetNodeDirtyGeneration(previewNodeId), GetNodeDirtyGeneration(input->fromNodeId)));
    }
    const EditorNodeGraph::Link* input =
        m_NodeGraph.FindAnyInputLink(previewNodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return 0;
    }
    return std::max<std::uint64_t>(1, GetNodeDirtyGeneration(input->fromNodeId));
}

const EditorModule::GraphPreviewPixels* EditorModule::GetCachedPreviewPixelsForNode(int previewNodeId) const {
    const auto it = m_PreviewPixelCache.find(previewNodeId);
    return it != m_PreviewPixelCache.end() ? &it->second : nullptr;
}

std::uint64_t EditorModule::GetScopeNodeRevision(int sourceNodeId) const {
    if (sourceNodeId <= 0) {
        m_ScopeDisplayedRevisions[sourceNodeId] = 0;
        return 0;
    }

    const std::uint64_t desiredRevision = GetNodeDirtyGeneration(sourceNodeId);
    std::uint64_t& displayedRevision = m_ScopeDisplayedRevisions[sourceNodeId];
    if (CanRefreshPreviewLikeNodes() && !HasPendingPreviewRefreshes()) {
        displayedRevision = desiredRevision;
    }
    return displayedRevision;
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
    ImGui::Text("Worker busy: %s", (m_RenderPending || m_RenderWorker.IsBusy()) ? "Yes" : "No");
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
        "Persistent cache: %.1f / %.1f MB (%d evicted)",
        static_cast<double>(cacheStats.persistentCacheBytes) / (1024.0 * 1024.0),
        static_cast<double>(cacheStats.persistentCacheBudgetBytes) / (1024.0 * 1024.0),
        cacheStats.persistentCacheEvictions);
    ImGui::Text(
        "Transient pool: %.1f MB (%d alloc / %d reuse)",
        static_cast<double>(cacheStats.transientPoolBytes) / (1024.0 * 1024.0),
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
