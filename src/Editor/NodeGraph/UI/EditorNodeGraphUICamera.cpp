#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include <algorithm>
#include <cmath>

namespace {

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
}

} // namespace

EditorNodeGraph::Vec2 EditorNodeGraphUI::ScreenToGraph(const EditorNodeGraph::Vec2& screen) const {
    return EditorNodeGraph::Vec2{
        (screen.x - m_CanvasOrigin.x - m_Pan.x) / m_Zoom,
        (screen.y - m_CanvasOrigin.y - m_Pan.y) / m_Zoom
    };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::GraphToScreen(const EditorNodeGraph::Vec2& graph) const {
    return EditorNodeGraph::Vec2{
        m_CanvasOrigin.x + m_Pan.x + graph.x * m_Zoom,
        m_CanvasOrigin.y + m_Pan.y + graph.y * m_Zoom
    };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeScreenSize(const EditorNodeGraph::Node& node) const {
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    return EditorNodeGraph::Vec2{ size.x * m_Zoom, size.y * m_Zoom };
}

void EditorNodeGraphUI::ZoomAtMouse(float wheel) {
    const EditorNodeGraph::Vec2 mouseScreen = ToGraphVec2(ImGui::GetMousePos());
    const EditorNodeGraph::Vec2 before = ScreenToGraph(mouseScreen);
    const float oldZoom = m_Zoom;
    m_Zoom = std::clamp(m_Zoom * (wheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.16f, 4.5f);
    if (std::abs(m_Zoom - oldZoom) < 0.0001f) {
        return;
    }
    m_Pan.x = mouseScreen.x - m_CanvasOrigin.x - before.x * m_Zoom;
    m_Pan.y = mouseScreen.y - m_CanvasOrigin.y - before.y * m_Zoom;
}

void EditorNodeGraphUI::ClampPanToContent(const EditorNodeGraph::Graph& graph) {
    if (graph.GetNodes().empty()) {
        return;
    }

    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool first = true;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        const EditorNodeGraph::Vec2 size = NodeSize(node);
        if (first) {
            minX = node.position.x;
            minY = node.position.y;
            maxX = node.position.x + size.x;
            maxY = node.position.y + size.y;
            first = false;
        } else {
            minX = std::min(minX, node.position.x);
            minY = std::min(minY, node.position.y);
            maxX = std::max(maxX, node.position.x + size.x);
            maxY = std::max(maxY, node.position.y + size.y);
        }
    }

    const float margin = 360.0f;
    const float canvasW = std::max(1.0f, m_CanvasMax.x - m_CanvasMin.x);
    const float canvasH = std::max(1.0f, m_CanvasMax.y - m_CanvasMin.y);
    const float minPanX = canvasW - (maxX + margin) * m_Zoom;
    const float maxPanX = (-minX + margin) * m_Zoom;
    const float minPanY = canvasH - (maxY + margin) * m_Zoom;
    const float maxPanY = (-minY + margin) * m_Zoom;
    m_Pan.x = std::clamp(m_Pan.x, std::min(minPanX, maxPanX), std::max(minPanX, maxPanX));
    m_Pan.y = std::clamp(m_Pan.y, std::min(minPanY, maxPanY), std::max(minPanY, maxPanY));
}
