#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

bool EditorNodeGraphUI::FinishPendingNodeNotes(EditorModule* editor, std::string* error) {
    return m_NodeNotes.FinishEditing([&](const auto& note) {
        return editor->SetGraphNodeNotes(note.documentId, note.graphId, note.nodeId,
            note.instanceUuid, note.text, error);
    });
}

void EditorNodeGraphUI::RenderNodeNotes(EditorModule* editor, const EditorNodeGraph::Node& node,
    bool hovered, float selectedAlpha) {
    using namespace Stack::Editor::NodeGraphUIVisuals;
    const auto* layout = FindNodeLayoutCache(node.id);
    if (!layout) return;
    auto& graph = GetActiveGraph(editor);
    const auto tokens = BuildGraphStyleTokens(editor);
    const auto style = StyleForFamily(FamilyForNode(node), tokens);
    const float scale = NodeContentScale();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    DrawGraphNodeSpotlightSurface(draw, layout->frameRect.min, layout->frameRect.max,
        style.fill, style.border, style.accent, tokens, graph.IsNodeSelected(node.id),
        true, scale, 8.f*scale, 1.15f*scale, selectedAlpha);

    // Keep the existing native socket geometry and data semantics while the
    // interior shows authored notes instead of parameters or preview resources.
    for (const auto& socket : PresentedSockets(graph, node)) {
        if (!IsSocketVisibleDuringDrag(graph, node.id, socket.id, socket.direction)) continue;
        const auto* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
        if (!anchor) continue;
        const bool pinHovered = socket.direction == EditorNodeGraph::SocketDirection::Input
            ? m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id
            : m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id;
        const auto display = ResolveSocketDisplayDefinition(graph, socket, editor);
        const auto icon = display.type == EditorNodeGraph::SocketType::Image ||
            display.type == EditorNodeGraph::SocketType::ImageOrChannel
            ? m_SocketIconTexture.GetImageSocketAperture() : 0;
        ImGui::PushID(node.id);
        DrawSocketPin(draw, anchor->screenPos, NodePinRadius(), SocketColor(display, style, tokens),
            tokens, pinHovered, display, IsSocketConnected(graph, socket), pinHovered ? 1.f : 0.f, icon);
        ImGui::PopID();
    }
    const bool wasEditing = m_NodeNotes.Editing();
    const bool contentHovered = m_NodeNotes.Draw(node, m_GraphContext, editor->GetProjectEditRevision(),
        layout->frameRect.min, layout->frameRect.max, scale, style.text, style.mutedText,
        hovered, m_LinkLabelRevealAlpha);
    m_NodeContentHovered |= contentHovered;
    m_NodeContentActive |= wasEditing || m_NodeNotes.Editing();
}
