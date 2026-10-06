#include "Editor/EditorModule.h"

bool EditorModule::UsesRawWorkspaceStageRender() const {
    return m_RawWorkspaceRootTabActive && IsRawWorkspaceProjectActive() &&
        !m_RawWorkspaceExportRenderRequested && !m_Project->graph.IsOutputConnected();
}

int EditorModule::ResolveRawWorkspaceStageOutputNodeId() const {
    if (!IsRawWorkspaceProjectActive()) return 0;
    if (IsMultiFrameRawProjectActive() && m_Project->snapshot) {
        const auto& setId = m_Project->snapshot->activeSourceSetId;
        const auto* set = Stack::Project::FindSourceSet(*m_Project->snapshot, setId);
        int onlyResultNode = 0;
        int resultNodeCount = 0;
        for (const auto& node : m_Project->graph.GetNodes()) {
            if ((node.kind == EditorNodeGraph::NodeKind::MultiFrameHdr && node.multiFrameHdr.sourceSetId == setId) ||
                (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise && node.multiFrameDenoise.sourceSetId == setId)) {
                if (set && node.instanceUuid == set->graphBindingNodeId) return node.id;
                onlyResultNode = node.id;
                ++resultNodeCount;
            }
        }
        return resultNodeCount == 1 ? onlyResultNode : 0;
    }
    if (m_Project->snapshot && m_Project->snapshot->pipelineData.contains("rawLayerSourceNodeUuid")) {
        const auto uuid = m_Project->snapshot->pipelineData.value("rawLayerSourceNodeUuid",std::string{});
        for (const auto& node : m_Project->graph.GetNodes()) if (node.instanceUuid == uuid) return node.id;
        return 0;
    }
    if (m_Project->rawMode != Stack::RawWorkspace::RawProjectMode::UnifiedLayers) {
        // A decomposed RAW section has an explicit end, even when its output
        // is disconnected from the remainder of the graph.
        const int end = m_Project->managedRaw.viewTransformNodeId;
        if (end > 0 && m_Project->graph.FindNode(end)) return end;
        return 0;
    }
    int onlyRawNode = 0;
    int rawNodeCount = 0;
    for (const auto& node : m_Project->graph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::RawDevelopment) continue;
        const auto& source = node.rawDevelopment.recipe.source;
        const auto& active = m_Project->rawRecipe.source;
        if ((!active.fingerprint.empty() && source.fingerprint == active.fingerprint) ||
            (!active.sourcePath.empty() && source.sourcePath == active.sourcePath)) return node.id;
        ++rawNodeCount;
        onlyRawNode = node.id;
    }
    return rawNodeCount == 1 ? onlyRawNode : 0;
}
