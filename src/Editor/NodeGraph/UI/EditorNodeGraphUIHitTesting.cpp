#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"

#include <algorithm>
#include <cmath>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

using EditorNodeGraphUIMetrics::IsPointNearCubicBezier;
using EditorNodeGraphUIMetrics::LinkBezierHandle;
using EditorNodeGraphUIMetrics::LinkHitRadiusForZoom;

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

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    const float hitRadius = std::max(1.0f, NodePinRadius() + (8.0f * NodeContentScale()));
    const float hitRadiusSq = hitRadius * hitRadius;
    std::vector<const EditorNodeGraph::Node*> orderedNodes;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        orderedNodes.push_back(&node);
    }
    std::sort(orderedNodes.begin(), orderedNodes.end(), [&](const EditorNodeGraph::Node* a, const EditorNodeGraph::Node* b) {
        const bool richA = a->kind == EditorNodeGraph::NodeKind::Layer && a->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(a->layerIndex);
        const bool richB = b->kind == EditorNodeGraph::NodeKind::Layer && b->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(b->layerIndex);
        if (richA != richB) {
            return richA > richB;
        }
        const auto itA = m_NodeFrontOrder.find(a->id);
        const auto itB = m_NodeFrontOrder.find(b->id);
        const std::uint64_t stampA = itA != m_NodeFrontOrder.end() ? itA->second : 0;
        const std::uint64_t stampB = itB != m_NodeFrontOrder.end() ? itB->second : 0;
        return stampA > stampB;
    });
    for (const EditorNodeGraph::Node* nodePtr : orderedNodes) {
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

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    const float hitRadius = std::max(1.0f, NodePinRadius() + (8.0f * NodeContentScale()));
    const float hitRadiusSq = hitRadius * hitRadius;
    std::vector<const EditorNodeGraph::Node*> orderedNodes;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        orderedNodes.push_back(&node);
    }
    std::sort(orderedNodes.begin(), orderedNodes.end(), [&](const EditorNodeGraph::Node* a, const EditorNodeGraph::Node* b) {
        const bool richA = a->kind == EditorNodeGraph::NodeKind::Layer && a->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(a->layerIndex);
        const bool richB = b->kind == EditorNodeGraph::NodeKind::Layer && b->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(b->layerIndex);
        if (richA != richB) {
            return richA > richB;
        }
        const auto itA = m_NodeFrontOrder.find(a->id);
        const auto itB = m_NodeFrontOrder.find(b->id);
        const std::uint64_t stampA = itA != m_NodeFrontOrder.end() ? itA->second : 0;
        const std::uint64_t stampB = itB != m_NodeFrontOrder.end() ? itB->second : 0;
        return stampA > stampB;
    });
    for (const EditorNodeGraph::Node* nodePtr : orderedNodes) {
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

int EditorNodeGraphUI::FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    std::vector<const EditorNodeGraph::Node*> orderedNodes;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        orderedNodes.push_back(&node);
    }
    std::sort(orderedNodes.begin(), orderedNodes.end(), [&](const EditorNodeGraph::Node* a, const EditorNodeGraph::Node* b) {
        const bool richA = a->kind == EditorNodeGraph::NodeKind::Layer && a->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(a->layerIndex);
        const bool richB = b->kind == EditorNodeGraph::NodeKind::Layer && b->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(b->layerIndex);
        if (richA != richB) {
            return richA > richB;
        }
        const auto itA = m_NodeFrontOrder.find(a->id);
        const auto itB = m_NodeFrontOrder.find(b->id);
        const std::uint64_t stampA = itA != m_NodeFrontOrder.end() ? itA->second : 0;
        const std::uint64_t stampB = itB != m_NodeFrontOrder.end() ? itB->second : 0;
        return stampA > stampB;
    });
    for (const EditorNodeGraph::Node* node : orderedNodes) {
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
