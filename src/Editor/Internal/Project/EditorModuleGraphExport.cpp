#include "Editor/EditorModule.h"

bool EditorModule::CanExportGraphOutput(int outputNodeId) const {
    if (IsExportBusy() || IsCompositeViewportMode() || IsAutoGainMaskPreviewActive())
        return false;
    const auto& graph = GetNodeGraph();
    const auto* output = graph.FindNode(outputNodeId);
    if (!output || output->kind != EditorNodeGraph::NodeKind::Output ||
        !output->outputEnabled ||
        !graph.FindInputLink(outputNodeId, EditorNodeGraph::kImageInputSocketId))
        return false;
    // The RAW canvas projects layer results as Output cards too. Only its
    // final terminal represents the complete project passed to the exporter.
    if (IsRawProjectCanvas())
        return outputNodeId == m_RawLayerMaskWorkspace->projectCanvas->output;
    if (IsEditingRawLayerMaskGraph())
        return false;
    const auto& completed = graph.GetCompletedChains();
    return completed.size() == 1 && completed.front().outputNodeId == outputNodeId;
}

bool EditorModule::RequestExportGraphOutput(int outputNodeId, const std::string& path,
    Stack::Hdr::FileFormat format) {
    // Drawing already has the socket's type for its controls. Keep semantic
    // analysis in this explicit action instead of repeating it for capability.
    if (!CanExportGraphOutput(outputNodeId) || !FinishWorkspaceInteraction() ||
        !CanExportGraphOutput(outputNodeId) || GetNodeGraph().IsOutputChannelInspection(outputNodeId)) {
        PostNotification(UiNotificationSeverity::Warning,
            "Export the connected final Image Output after finishing the current edit.",
            "graph-output-export");
        return false;
    }
    return RequestExportImage(path, format);
}
