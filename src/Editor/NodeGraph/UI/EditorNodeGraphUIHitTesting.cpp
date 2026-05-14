#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include <algorithm>
#include <cmath>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

float NodeUiScaleFromZoom(float zoom) {
    return std::clamp(zoom, 0.16f, 2.5f);
}

float PinRadiusForZoom(float zoom) {
    return std::max(1.8f, 5.3f * NodeUiScaleFromZoom(zoom));
}

float DistancePointToSegment(const ImVec2& point, const ImVec2& a, const ImVec2& b) {
    const ImVec2 ab(b.x - a.x, b.y - a.y);
    const ImVec2 ap(point.x - a.x, point.y - a.y);
    const float abLenSq = ab.x * ab.x + ab.y * ab.y;
    if (abLenSq <= 1e-6f) {
        const float dx = point.x - a.x;
        const float dy = point.y - a.y;
        return std::sqrt(dx * dx + dy * dy);
    }

    const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f);
    const ImVec2 closest(a.x + ab.x * t, a.y + ab.y * t);
    const float dx = point.x - closest.x;
    const float dy = point.y - closest.y;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

EditorNodeGraph::Vec2 EditorNodeGraphUI::InputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(node.id)) {
        if (const SocketAnchor* anchor = FindSocketAnchor(*cache, socketId, EditorNodeGraph::SocketDirection::Input)) {
            return EditorNodeGraph::Vec2{ anchor->screenPos.x, anchor->screenPos.y };
        }
    }
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    const EditorNodeGraph::Vec2 screenSize{ size.x * m_Zoom, size.y * m_Zoom };
    const EditorNodeGraph::Vec2 pos = GraphToScreen(node.position);
    return EditorNodeGraph::Vec2{ pos.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::OutputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(node.id)) {
        if (const SocketAnchor* anchor = FindSocketAnchor(*cache, socketId, EditorNodeGraph::SocketDirection::Output)) {
            return EditorNodeGraph::Vec2{ anchor->screenPos.x, anchor->screenPos.y };
        }
    }
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    const EditorNodeGraph::Vec2 screenSize{ size.x * m_Zoom, size.y * m_Zoom };
    const EditorNodeGraph::Vec2 pos = GraphToScreen(node.position);
    return EditorNodeGraph::Vec2{ pos.x + screenSize.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    const float hitRadius = std::max(12.0f, PinRadiusForZoom(m_Zoom) + 8.0f);
    const float hitRadiusSq = hitRadius * hitRadius;
    const auto& nodes = graph.GetNodes();
    for (auto nodeIt = nodes.rbegin(); nodeIt != nodes.rend(); ++nodeIt) {
        const EditorNodeGraph::Node& node = *nodeIt;
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
                continue;
            }
            const EditorNodeGraph::Vec2 pin = InputPinScreenPos(node, socket.id);
            const float dx = pin.x - screenPos.x;
            const float dy = pin.y - screenPos.y;
            if ((dx * dx + dy * dy) <= hitRadiusSq) {
                return SocketHit{ node.id, socket.id };
            }
        }
    }
    return {};
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    const float hitRadius = std::max(12.0f, PinRadiusForZoom(m_Zoom) + 8.0f);
    const float hitRadiusSq = hitRadius * hitRadius;
    const auto& nodes = graph.GetNodes();
    for (auto nodeIt = nodes.rbegin(); nodeIt != nodes.rend(); ++nodeIt) {
        const EditorNodeGraph::Node& node = *nodeIt;
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
                continue;
            }
            const EditorNodeGraph::Vec2 pin = OutputPinScreenPos(node, socket.id);
            const float dx = pin.x - screenPos.x;
            const float dy = pin.y - screenPos.y;
            if ((dx * dx + dy * dy) <= hitRadiusSq) {
                return SocketHit{ node.id, socket.id };
            }
        }
    }
    return {};
}

int EditorNodeGraphUI::FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    const auto& nodes = graph.GetNodes();
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (const NodeLayoutCache* cache = FindNodeLayoutCache(it->id)) {
            if (cache->frameRect.Contains(ToImVec2(screenPos))) {
                return it->id;
            }
            continue;
        }

        const EditorNodeGraph::Vec2 pos = GraphToScreen(it->position);
        const EditorNodeGraph::Vec2 size = NodeScreenSize(*it);
        if (screenPos.x >= pos.x && screenPos.x <= pos.x + size.x &&
            screenPos.y >= pos.y && screenPos.y <= pos.y + size.y) {
            return it->id;
        }
    }
    return -1;
}

EditorNodeGraph::Link EditorNodeGraphUI::FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        const EditorNodeGraph::Node* from = graph.FindNode(link.fromNodeId);
        const EditorNodeGraph::Node* to = graph.FindNode(link.toNodeId);
        if (!from || !to) {
            continue;
        }
        if (IsPointNearLink(screenPos, OutputPinScreenPos(*from, link.fromSocketId), InputPinScreenPos(*to, link.toSocketId))) {
            return link;
        }
    }
    return {};
}

bool EditorNodeGraphUI::IsPointNearLink(const EditorNodeGraph::Vec2& point, const EditorNodeGraph::Vec2& a, const EditorNodeGraph::Vec2& b) const {
    return DistancePointToSegment(ToImVec2(point), ToImVec2(a), ToImVec2(b)) <= std::max(6.0f, 8.0f * NodeUiScaleFromZoom(m_Zoom));
}

void EditorNodeGraphUI::DrawClippedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color) const {
    drawList->PushClipRect(min, max, true);
    drawList->AddText(min, color, text);
    drawList->PopClipRect();
}
