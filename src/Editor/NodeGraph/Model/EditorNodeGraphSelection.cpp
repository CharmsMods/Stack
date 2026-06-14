#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <algorithm>

namespace EditorNodeGraph {
namespace {

Link MakeSocketLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    Link link;
    link.fromNodeId = fromNodeId;
    link.fromSocketId = fromSocketId;
    link.toNodeId = toNodeId;
    link.toSocketId = toSocketId;
    return link;
}

Vec2 DefaultSelectionSize(const Node& node) {
    return node.kind == NodeKind::Layer && node.expanded
        ? Vec2{ 334.0f, 520.0f }
        : (node.kind == NodeKind::Image ? Vec2{ 232.0f, 128.0f }
        : (node.kind == NodeKind::Scope ? Vec2{ 300.0f, 270.0f }
        : (node.kind == NodeKind::MaskGenerator ? Vec2{ 270.0f, 246.0f }
        : (node.kind == NodeKind::Mix ? Vec2{ 250.0f, 170.0f }
        : (node.kind == NodeKind::DataMath ? Vec2{ 250.0f, 170.0f }
        : (node.kind == NodeKind::Preview ? Vec2{ 322.0f, 244.0f }
        : (node.kind == NodeKind::Composite ? Vec2{ 286.0f, 360.0f }
        : Vec2{ 232.0f, 82.0f })))))));
}

} // namespace

void Graph::SelectNode(int nodeId, bool additive) {
    if (!FindNode(nodeId)) {
        ClearSelection();
        return;
    }

    ClearSelectedLink();
    if (!additive) {
        m_SelectedNodeIds.clear();
    }

    auto it = std::find(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), nodeId);
    if (it == m_SelectedNodeIds.end()) {
        m_SelectedNodeIds.push_back(nodeId);
    } else if (additive) {
        m_SelectedNodeIds.erase(it);
    }

    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
}

bool Graph::IsNodeSelected(int nodeId) const {
    return std::find(m_SelectedNodeIds.begin(), m_SelectedNodeIds.end(), nodeId) != m_SelectedNodeIds.end();
}

void Graph::ClearSelection() {
    m_SelectedNodeId = -1;
    m_SelectedNodeIds.clear();
    ClearSelectedLink();
}

void Graph::SelectNodesInRect(Vec2 min, Vec2 max, bool additive) {
    SelectNodesInRect(min, max, DefaultSelectionSize, additive);
}

void Graph::SelectNodesInRect(
    Vec2 min,
    Vec2 max,
    const std::function<Vec2(const Node&)>& sizeResolver,
    bool additive) {
    if (!additive) {
        m_SelectedNodeIds.clear();
    }
    ClearSelectedLink();

    const float left = std::min(min.x, max.x);
    const float right = std::max(min.x, max.x);
    const float top = std::min(min.y, max.y);
    const float bottom = std::max(min.y, max.y);

    for (const Node& node : m_Nodes) {
        const Vec2 size = sizeResolver ? sizeResolver(node) : DefaultSelectionSize(node);
        const bool overlaps =
            node.position.x <= right &&
            node.position.x + size.x >= left &&
            node.position.y <= bottom &&
            node.position.y + size.y >= top;
        if (overlaps && !IsNodeSelected(node.id)) {
            m_SelectedNodeIds.push_back(node.id);
        }
    }

    m_SelectedNodeId = m_SelectedNodeIds.empty() ? -1 : m_SelectedNodeIds.back();
}

void Graph::SelectLink(int fromNodeId, int toNodeId) {
    const Node* from = FindNode(fromNodeId);
    const Node* to = FindNode(toNodeId);
    SelectLink(
        fromNodeId,
        from ? DefaultOutputSocket(*from) : std::string(),
        toNodeId,
        to ? DefaultInputSocket(*to) : std::string());
}

void Graph::SelectLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    m_SelectedLink = MakeSocketLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    m_HasSelectedLink = HasLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    if (m_HasSelectedLink) {
        m_SelectedNodeId = -1;
        m_SelectedNodeIds.clear();
    }
}

void Graph::ClearSelectedLink() {
    m_SelectedLink = {};
    m_HasSelectedLink = false;
}

const Link* Graph::GetSelectedLink() const {
    if (!m_HasSelectedLink) {
        return nullptr;
    }
    auto it = std::find_if(m_Links.begin(), m_Links.end(), [this](const Link& link) {
        return link.fromNodeId == m_SelectedLink.fromNodeId &&
            link.fromSocketId == m_SelectedLink.fromSocketId &&
            link.toNodeId == m_SelectedLink.toNodeId &&
            link.toSocketId == m_SelectedLink.toSocketId;
    });
    return it != m_Links.end() ? &(*it) : nullptr;
}

bool Graph::HasSelectedLink() const {
    return GetSelectedLink() != nullptr;
}

} // namespace EditorNodeGraph
