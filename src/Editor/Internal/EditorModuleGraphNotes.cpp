#include "Editor/EditorModule.h"
#include "Project/RawProjectGraph.h"

bool EditorModule::SetGraphNodeNotes(const std::string& documentId, const std::string& graphId,
    int nodeId, const std::string& instanceUuid, const std::string& notes, std::string* error) {
    const auto fail=[&](const std::string& message) {if(error)*error=message;return false;};
    if (documentId!=GetProjectDocumentId()) return fail("These notes belong to another project.");
    auto context=GetGraphEditorContext();
    if (context.graphId!=graphId || !context.graph) return fail("These notes belong to another graph.");
    auto* node=context.graph->FindNode(nodeId);
    if (!node || node->instanceUuid!=instanceUuid) return fail("The annotated node was replaced or removed.");
    if (node->notes==notes) {if(error)error->clear();return true;}

    if (IsEditingRawLayerMaskGraph()) {
        if (!CommitRawLayerMaskGraph()) return fail(m_RawLayerStatus);
        auto& workspace=*m_RawLayerMaskWorkspace;
        node=workspace.graph.FindNode(nodeId);
        if (!node || node->instanceUuid!=instanceUuid || !workspace.projectCanvas ||
            workspace.revision!=m_Project->rawLayers.Revision())
            return fail("The annotated graph changed; reopen the note before editing it.");
        std::string previous=node->notes;
        node->notes=notes;
        std::string reason;
        if (!m_Project->rawLayers.ApplyGraphLayout(*workspace.projectCanvas,workspace.graph,false,reason,true)) {
            node->notes=std::move(previous);
            return fail(reason);
        }
        workspace.revision=m_Project->rawLayers.Revision();
    } else {
        m_Project->graphHistory.EndGesture();
        BeginProjectGraphHistory();
        node->notes=notes;
        m_Project->graphHistory.Observe(CaptureProjectGraphState(),false,false);
    }
    MarkDirty(false); // An annotation cannot change the Gallery cover image.
    if(error)error->clear();
    return true;
}
