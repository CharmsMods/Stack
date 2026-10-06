#include "Editor/EditorModule.h"
#include "Utils/HashUtils.h"

Stack::GraphRendering::RequestTag EditorModule::CurrentGraphRenderTag() const {
    Stack::GraphRendering::RequestTag tag;
    tag.enabled = !m_RawWorkspaceRootTabActive || !IsRawWorkspaceProjectActive();
    tag.outputNodeId = m_Project->graph.ResolvePreviewOutputNodeId();
    tag.inspectionNodeId = m_AutoGainMaskPreviewNodeId;
    tag.composite = GetViewportMode() == ViewportMode::CompositeCanvas;
    tag.structureRevision = m_Project->graph.GetStructureRevision();
    tag.revision = m_RenderRevision;
    if (IsEditingRawLayerMaskGraph())
        StackHash::HashCombine(tag.sourceIdentity, StackHash::HashValue(m_RawLayerMaskWorkspace->layerId));
    for (const auto& node : m_Project->graph.GetNodes()) {
        using Kind = EditorNodeGraph::NodeKind;
        if (node.kind == Kind::Image || node.kind == Kind::RawSource ||
            node.kind == Kind::RawProjectFrame || node.kind == Kind::RawProjectSourceSet) {
            StackHash::HashCombine(tag.sourceIdentity, StackHash::HashValue(node.instanceUuid));
            if (node.kind == Kind::Image) {
                StackHash::HashCombine(tag.sourceIdentity, node.image.pixelsFingerprint);
                StackHash::HashCombine(tag.sourceIdentity, node.image.importRequestId);
                StackHash::HashCombine(tag.sourceIdentity, node.image.width);
                StackHash::HashCombine(tag.sourceIdentity, node.image.height);
                StackHash::HashCombine(tag.sourceIdentity, node.image.channels);
            } else if (node.kind == Kind::RawSource &&
                !node.rawSource.metadata.sourceContentSha256.empty()) {
                StackHash::HashCombine(tag.sourceIdentity, StackHash::HashValue(node.rawSource.sourcePath));
                StackHash::HashCombine(tag.sourceIdentity,
                    StackHash::HashValue(node.rawSource.metadata.sourceContentSha256));
                StackHash::HashCombine(tag.sourceIdentity, node.rawSource.metadata.sourceByteSize);
            } else {
                // Sources without an immutable content identity stay conservative.
                StackHash::HashCombine(tag.sourceIdentity, GetNodeDirtyGeneration(node.id));
            }
        }
    }
    return tag;
}

bool EditorModule::IsCurrentGraphResult(const EditorRenderWorker::Result& result) const {
    if (!result.graphRequest.enabled) return result.generation >= m_RenderGeneration;
    return Stack::GraphRendering::MayAdopt(result.graphRequest, CurrentGraphRenderTag(),
        result.generation, m_GraphAcceptedResultGeneration);
}

bool EditorModule::GraphRenderBackendReady() {
    m_Viewport.FrameTransition().Validate(CurrentGraphRenderTag(),
        ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0);
    const bool available = IsRawWorkspaceProjectActive()
        ? m_RawRenderClientId != 0
        : m_RenderWorkerAvailable && m_RenderWorker.IsAvailable();
    if (available) {
        m_GraphRenderBackendFailureReported = false;
        if (!IsRawWorkspaceProjectActive())
            m_RenderWorker.UpdateGraphContext(CurrentGraphRenderTag());
        return true;
    }
    if (!m_GraphRenderBackendFailureReported && (m_RenderDirty || m_Project->graph.IsOutputConnected())) {
        PostNotification(UiNotificationSeverity::Error,
            "Graph rendering is unavailable. The previous image has been retained.",
            "graph-render-worker-unavailable");
        m_GraphRenderBackendFailureReported = true;
    }
    m_RenderPending = false;
    return false;
}

void EditorModule::ReportGraphRenderFailure(const EditorRenderWorker::Result& result) {
    if (result.error == "Render superseded by a newer snapshot.") return;
    PostNotification(UiNotificationSeverity::Error,
        "Previous graph image retained: " +
            (result.error.empty() ? std::string("Graph render failed.") : result.error),
        "graph-render-failed");
}
