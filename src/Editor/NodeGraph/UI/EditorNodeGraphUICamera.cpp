#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "App/settings/AppearanceTheme.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"

#include <algorithm>
#include <cmath>

namespace {

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
}

} // namespace

bool EditorNodeGraphUI::UsesFixedNodeViewport() const {
    return false;
}

float EditorNodeGraphUI::NodeContentScale() const {
    return EditorNodeGraphUIMetrics::NodeUiScaleFromZoom(m_Zoom);
}

float EditorNodeGraphUI::NodePinRadius() const {
    return EditorNodeGraphUIMetrics::PinRadiusForZoom(NodeContentScale());
}

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
    return NodeViewportSizePx(node);
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeViewportSizePx(const EditorNodeGraph::Node& node) const {
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    return EditorNodeGraph::Vec2{ size.x * m_Zoom, size.y * m_Zoom };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeGraphFootprintSize(const EditorNodeGraph::Node& node) const {
    return NodeSize(node);
}

void EditorNodeGraphUI::ZoomAtMouse(float wheel) {
    const EditorNodeGraph::Vec2 mouseScreen = ToGraphVec2(ImGui::GetMousePos());
    const EditorNodeGraph::Vec2 before = ScreenToGraph(mouseScreen);
    const float baseZoom = m_SmoothZoomActive ? m_ZoomTarget : m_Zoom;
    const float nextTarget = std::clamp(baseZoom * (wheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.16f, 4.5f);
    if (std::abs(nextTarget - baseZoom) < 0.0001f) {
        return;
    }
    m_ZoomTarget = nextTarget;
    m_SmoothZoomFocusScreen = mouseScreen;
    m_SmoothZoomFocusGraph = before;
    m_SmoothZoomActive = true;
}

void EditorNodeGraphUI::ClampPanToContent(const EditorNodeGraph::Graph& graph) {
    if (graph.GetNodes().empty() || m_MiddlePanCaptureActive) {
        return;
    }

    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool first = true;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        const EditorNodeGraph::Vec2 size = NodeGraphFootprintSize(node);
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

    const float canvasW = std::max(1.0f, m_CanvasMax.x - m_CanvasMin.x);
    const float canvasH = std::max(1.0f, m_CanvasMax.y - m_CanvasMin.y);
    const float marginPx = std::clamp(std::min(canvasW, canvasH) * 0.85f, 420.0f, 1200.0f);
    const float minPanX = -marginPx - (maxX * m_Zoom);
    const float maxPanX = canvasW + marginPx - (minX * m_Zoom);
    const float minPanY = -marginPx - (maxY * m_Zoom);
    const float maxPanY = canvasH + marginPx - (minY * m_Zoom);
    m_Pan.x = std::clamp(m_Pan.x, std::min(minPanX, maxPanX), std::max(minPanX, maxPanX));
    m_Pan.y = std::clamp(m_Pan.y, std::min(minPanY, maxPanY), std::max(minPanY, maxPanY));
}
