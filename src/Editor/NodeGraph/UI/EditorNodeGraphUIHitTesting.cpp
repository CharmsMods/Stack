#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include <algorithm>
#include <cmath>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

using EditorNodeGraphUIMetrics::IsPointNearCubicBezier;
using EditorNodeGraphUIMetrics::LinkBezierHandle;
using EditorNodeGraphUIMetrics::LinkHitRadiusForZoom;
using Stack::Editor::NodeGraphUIVisuals::ChannelLaneOffset;
using Stack::Editor::NodeGraphUIVisuals::ResolveLinkVisualStyle;

} // namespace

EditorNodeGraph::Vec2 EditorNodeGraphUI::InputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(node.id)) {
        if (const SocketAnchor* anchor = FindSocketAnchor(*cache, socketId, EditorNodeGraph::SocketDirection::Input)) {
            return EditorNodeGraph::Vec2{ anchor->screenPos.x, anchor->screenPos.y };
        }
    }
    const EditorNodeGraph::Vec2 screenSize = NodeScreenSize(node);
    const EditorNodeGraph::Vec2 pos = GraphToScreen(node.position);
    return EditorNodeGraph::Vec2{ pos.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::OutputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(node.id)) {
        if (const SocketAnchor* anchor = FindSocketAnchor(*cache, socketId, EditorNodeGraph::SocketDirection::Output)) {
            return EditorNodeGraph::Vec2{ anchor->screenPos.x, anchor->screenPos.y };
        }
    }
    const EditorNodeGraph::Vec2 screenSize = NodeScreenSize(node);
    const EditorNodeGraph::Vec2 pos = GraphToScreen(node.position);
    return EditorNodeGraph::Vec2{ pos.x + screenSize.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const float hitRadius = std::max(1.0f, NodePinRadius() + (8.0f * NodeContentScale()));
    const float hitRadiusSq = hitRadius * hitRadius;
    const std::vector<int>& orderedNodes = GetNodeHitTestOrder(graph);
    for (const int nodeId : orderedNodes) {
        const EditorNodeGraph::Node* nodePtr = FindCachedNode(graph, nodeId);
        if (!nodePtr) {
            continue;
        }
        const EditorNodeGraph::Node& node = *nodePtr;
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

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const float hitRadius = std::max(1.0f, NodePinRadius() + (8.0f * NodeContentScale()));
    const float hitRadiusSq = hitRadius * hitRadius;
    const std::vector<int>& orderedNodes = GetNodeHitTestOrder(graph);
    for (const int nodeId : orderedNodes) {
        const EditorNodeGraph::Node* nodePtr = FindCachedNode(graph, nodeId);
        if (!nodePtr) {
            continue;
        }
        const EditorNodeGraph::Node& node = *nodePtr;
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

int EditorNodeGraphUI::FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const std::vector<int>& orderedNodes = GetNodeHitTestOrder(graph);
    for (const int nodeId : orderedNodes) {
        const EditorNodeGraph::Node* node = FindCachedNode(graph, nodeId);
        if (!node) {
            continue;
        }
        if (const NodeLayoutCache* cache = FindNodeLayoutCache(node->id)) {
            if (cache->frameRect.Contains(ToImVec2(screenPos))) {
                return node->id;
            }
            continue;
        }

        const EditorNodeGraph::Vec2 pos = GraphToScreen(node->position);
        const EditorNodeGraph::Vec2 size = NodeScreenSize(*node);
        if (screenPos.x >= pos.x && screenPos.x <= pos.x + size.x &&
            screenPos.y >= pos.y && screenPos.y <= pos.y + size.y) {
            return node->id;
        }
    }
    return -1;
}

EditorNodeGraph::Link EditorNodeGraphUI::FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const auto& links = graph.GetLinks();
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        const EditorNodeGraph::Link& link = *it;
        const EditorNodeGraph::Node* from = FindCachedNode(graph, link.fromNodeId);
        const EditorNodeGraph::Node* to = FindCachedNode(graph, link.toNodeId);
        if (!from || !to) {
            continue;
        }

        EditorNodeGraph::Vec2 fromPos = OutputPinScreenPos(*from, link.fromSocketId);
        EditorNodeGraph::Vec2 toPos = InputPinScreenPos(*to, link.toSocketId);
        const auto visualStyle = ResolveLinkVisualStyle(graph, link);
        const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
        fromPos.y += laneOffset;
        toPos.y += laneOffset;

        if (IsPointNearLink(screenPos, fromPos, toPos)) {
            return link;
        }
    }
    return {};
}

bool EditorNodeGraphUI::IsPointNearLink(const EditorNodeGraph::Vec2& point, const EditorNodeGraph::Vec2& a, const EditorNodeGraph::Vec2& b) const {
    const ImVec2 p0 = ToImVec2(a);
    const ImVec2 p3 = ToImVec2(b);
    const float handle = LinkBezierHandle(p0, p3);
    return IsPointNearCubicBezier(
        ToImVec2(point),
        p0,
        ImVec2(p0.x + handle, p0.y),
        ImVec2(p3.x - handle, p3.y),
        p3,
        LinkHitRadiusForZoom(m_Zoom));
}

void EditorNodeGraphUI::DrawClippedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color) const {
    drawList->PushClipRect(min, max, true);
    drawList->AddText(min, color, text);
    drawList->PopClipRect();
}
