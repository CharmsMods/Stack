#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "EditorNodeGraphUIVisuals.h"

void EditorNodeGraphUI::PrepareWireRoutes(const EditorNodeGraph::Graph& graph) {
    if(m_WireGraph!=&graph) {
        m_WireRoutes.reset();m_PreparedWireWorldRevision=0;++m_WireWorldRevision;m_WireGraph=&graph;
    }
    if(!m_WireRoutes)m_WireRoutes=std::make_unique<Stack::Editor::NodeGraphUIVisuals::WireRouteCache>();
    // Visible links use the original cubic curve. Do not prepare obstacle
    // scenes or schedule routing jobs for geometry that will not be drawn.
}
const Stack::Editor::NodeGraphUIVisuals::ProjectedWire& EditorNodeGraphUI::WirePath(
    const EditorNodeGraph::Graph& graph,const EditorNodeGraph::Link& link) const {
    if(!m_WireRoutes)m_WireRoutes=std::make_unique<Stack::Editor::NodeGraphUIVisuals::WireRouteCache>();
    const auto from=OutputPinScreenPos(*graph.FindNode(link.fromNodeId),link.fromSocketId);
    const auto to=InputPinScreenPos(*graph.FindNode(link.toNodeId),link.toSocketId);
    const auto key=Stack::Editor::NodeGraphUIVisuals::LinkAnimationKey(link);
    const auto entry=m_CompoundEntryLinks.find(key);
    const ImVec2* via=entry==m_CompoundEntryLinks.end() || !LayoutGroupVisible(m_CompoundEntryRows[entry->second].group)
        ?nullptr:&m_CompoundEntryRows[entry->second].input;
    const ImVec2* departure=via?&m_CompoundEntryRows[entry->second].output:nullptr;
    return m_WireRoutes->Project(Stack::Editor::NodeGraphUIVisuals::LinkAnimationKey(link),
        {from.x,from.y},{to.x,to.y},{m_CanvasOrigin.x+m_Pan.x,m_CanvasOrigin.y+m_Pan.y},m_Zoom,
        Stack::Editor::NodeGraphUIVisuals::GraphStraightLinksEnabled(m_ActiveEditor),via,departure);
}
