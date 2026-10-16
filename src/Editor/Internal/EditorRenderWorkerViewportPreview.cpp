#include "Editor/EditorRenderWorker.h"
#include "Editor/RawRenderGraphOverlay.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr int kViewportOverviewEdge = 512;

// The main texture and its query batch have already been published. A second
// execution may replace pipeline output, but cannot change either result.
class OverviewPipelineState {
public:
    OverviewPipelineState(RenderPipeline& pipeline,
        const Stack::Renderer::ViewportPreviewRequest& request,
        bool validationEnabled)
        : m_Pipeline(pipeline), m_Request(request),
          m_RawRequest(pipeline.m_RawViewportRequest),
          m_Edge(pipeline.GetRawViewportCacheEdge()),
          m_AnalysisEnabled(pipeline.IsRawDevelopmentAnalysisEnabled()),
          m_ValidationEnabled(validationEnabled) {}

    ~OverviewPipelineState() {
        m_Pipeline.SetGraphViewportRequest(m_Request);
        m_Pipeline.SetRawViewportRequest(m_RawRequest);
        m_Pipeline.SetPreviewMaxDimension(m_Edge);
        m_Pipeline.SetRawDevelopmentAnalysisEnabled(m_AnalysisEnabled);
        m_Pipeline.SetRawDevelopmentViewportValidationEnabled(m_ValidationEnabled);
        // RenderSnapshot's callback captures its local cancellation latch.
        // Its lifetime ends before this optional, separate execution starts.
        m_Pipeline.SetRenderCancellationContext(0, {});
    }

private:
    RenderPipeline& m_Pipeline;
    Stack::Renderer::ViewportPreviewRequest m_Request;
    Raw::ViewportRequest m_RawRequest;
    int m_Edge = 0;
    bool m_AnalysisEnabled = false;
    bool m_ValidationEnabled = false;
};

} // namespace

void EditorRenderWorker::PrepareViewportOverview(const Snapshot& snapshot, Result& result) {
    if (!snapshot.prepareViewportOverview || !result.success ||
        !result.viewportPreview.enabled || result.viewportPreview.empty ||
        !result.viewportPreviewGeometry.Valid() || !m_PersistentPipeline ||
        result.outputTexture.texture == 0 || ShouldAbortStaleSnapshot(snapshot)) return;

    // Optional coverage must not turn a completed main result into a failure.
    // Its texture owns a separate immutable lease and readiness fence.
    try {
        const Snapshot& templateSnapshot = snapshot.rawSessionCommand && snapshot.rawSessionTemplate
            ? *snapshot.rawSessionTemplate : snapshot;
        if (templateSnapshot.graph.nodes.empty()) return;
        RenderGraphSnapshot graph = templateSnapshot.graph;
        if (snapshot.rawSessionCommand) {
            graph.rawWorkspaceLocalRangeOverlayMode = snapshot.rawWorkspace.localRangeOverlayMode;
            graph.rawWorkspaceLocalRangeTargetPreview = snapshot.rawWorkspace.localRangeTargetPreview;
            graph.rawWorkspaceLocalRangeTargetSampleRequested = snapshot.rawWorkspace.localRangeTargetSampleRequested;
            graph.rawWorkspaceLocalRangeTargetSampleU = snapshot.rawWorkspace.localRangeTargetSampleU;
            graph.rawWorkspaceLocalRangeTargetSampleV = snapshot.rawWorkspace.localRangeTargetSampleV;
            if (snapshot.rawWorkspace.hasRecipe) {
                Stack::EditorRendering::ApplyRawRecipeOverlayToRenderGraph(graph,
                    snapshot.rawWorkspace.recipe,
                    snapshot.rawWorkspace.managedRawDecodeNodeId,
                    snapshot.rawWorkspace.managedToneCurveNodeId,
                    snapshot.rawWorkspace.managedViewTransformNodeId);
            }
        }

        RenderPipeline& pipeline = *m_PersistentPipeline;
        OverviewPipelineState restore(pipeline, result.viewportPreview,
            snapshot.rawWorkspace.analysisRequested);
        bool canceled = false;
        pipeline.SetRenderCancellationContext(snapshot.generation, [&]() {
            canceled = canceled || ShouldAbortStaleSnapshot(snapshot);
            return canceled;
        });
        pipeline.BeginRawViewportTiming(false);
        pipeline.SetRawViewportRequest({});
        pipeline.SetPreviewMaxDimension(kViewportOverviewEdge);
        pipeline.SetRawDevelopmentAnalysisEnabled(false);
        pipeline.SetRawDevelopmentViewportValidationEnabled(false);
        auto request = result.viewportPreview;
        request.visible = {};
        request.empty = false;
        request.samplingScale = std::min(1.0, double(kViewportOverviewEdge) /
            std::max(result.viewportPreviewGeometry.logicalWidth, result.viewportPreviewGeometry.logicalHeight));
        // An enabled Full demand retains the bounded, whole-image sample grid
        // for ordinary graphs as well as RAW. Disabling it loses that contract.
        pipeline.SetGraphViewportRequest(request);
        // A separate thumbnail can install another fallback source buffer.
        // Restore this snapshot's source before rendering matching coverage.
        pipeline.LoadSourceFromSharedPixels(templateSnapshot.sourcePixels,
            templateSnapshot.width, templateSnapshot.height, templateSnapshot.channels);
        pipeline.ExecuteGraph(graph);
        if (canceled || ShouldAbortStaleSnapshot(snapshot)) return;
        const auto geometry = pipeline.GetGraphViewportGeometry();
        if (!geometry.Valid() || geometry.resident.Partial() ||
            geometry.logicalWidth != result.viewportPreviewGeometry.logicalWidth ||
            geometry.logicalHeight != result.viewportPreviewGeometry.logicalHeight ||
            std::max(geometry.storageWidth, geometry.storageHeight) > kViewportOverviewEdge ||
            !pipeline.GetLastGraphExecutionStats().lastSpecializedFailure.empty()) return;

        SharedTextureResult overview;
        overview.texture = pipeline.PublishSharedOutputTexture(overview.width, overview.height,
            !snapshot.rawWorkspace.sourceKey.empty());
        if (!overview.texture || !overview.EnsureLease()) return;
        overview.readyFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (!overview.readyFence) return;
        glFlush();
        result.viewportOverview = std::move(overview);
    } catch (...) {
        // The detail result remains adoptable. The caller may request its
        // missing coverage again after a later completed foreground render.
    }
}
