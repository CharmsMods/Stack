#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "App/UiPerformanceTrace.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

using EditorNodeGraphUIMetrics::LinkHitRadiusForZoom;
using Stack::Editor::NodeGraphUIVisuals::ChannelLaneOffset;
using Stack::Editor::NodeGraphUIVisuals::GraphStraightLinksEnabled;
using Stack::Editor::NodeGraphUIVisuals::LinkAnimationKey;

} // namespace

EditorNodeGraph::Vec2 EditorNodeGraphUI::InputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(node.id)) {
        if (const SocketAnchor* anchor = FindSocketAnchor(*cache, socketId, EditorNodeGraph::SocketDirection::Input)) {
            return EditorNodeGraph::Vec2{ anchor->screenPos.x, anchor->screenPos.y };
        }
    }
    const EditorNodeGraph::Vec2 screenSize = NodeScreenSize(node);
    const EditorNodeGraph::Vec2 pos = GraphToScreen(DisplayNodePosition(node));
    return EditorNodeGraph::Vec2{ pos.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::OutputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(node.id)) {
        if (const SocketAnchor* anchor = FindSocketAnchor(*cache, socketId, EditorNodeGraph::SocketDirection::Output)) {
            return EditorNodeGraph::Vec2{ anchor->screenPos.x, anchor->screenPos.y };
        }
    }
    const EditorNodeGraph::Vec2 screenSize = NodeScreenSize(node);
    const EditorNodeGraph::Vec2 pos = GraphToScreen(DisplayNodePosition(node));
    return EditorNodeGraph::Vec2{ pos.x + screenSize.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const float hitRadius = std::max(
        8.0f,
        NodePinRadius() + (8.0f * NodeContentScale()));
    const float hitRadiusSq = hitRadius * hitRadius;
    for(const auto& row:m_CompoundEntryRows) {
        if(!LayoutGroupVisible(row.group))continue;
        const float dx=row.input.x-screenPos.x,dy=row.input.y-screenPos.y;
        if(dx*dx+dy*dy<=hitRadiusSq){if(m_DragInputNodeId<=0)m_CompoundEntryDragInput=int(&row-m_CompoundEntryRows.data());return SocketHit{row.target,row.socket};}
    }
    const std::vector<int>& orderedNodes = GetNodeHitTestOrder(graph);
    for (const int nodeId : orderedNodes) {
        if(!LayoutNodeInteractive(nodeId))continue;
        const NodeLayoutCache* layout = FindNodeLayoutCache(nodeId);
        if (!layout) {
            continue;
        }
        if (layout->interactionRect.IsValid() &&
            !layout->interactionRect.Contains(ToImVec2(screenPos))) {
            continue;
        }
        for (const SocketAnchor& anchor : layout->socketAnchors) {
            if (!IsSocketVisibleDuringDrag(graph, nodeId, anchor.socketId, anchor.direction)) continue;
            if (anchor.direction !=
                EditorNodeGraph::SocketDirection::Input) {
                continue;
            }
            const float dx = anchor.screenPos.x - screenPos.x;
            const float dy = anchor.screenPos.y - screenPos.y;
            if ((dx * dx + dy * dy) <= hitRadiusSq) {
                if(m_DragInputNodeId<=0)m_CompoundEntryDragInput=-1;return SocketHit{ nodeId, anchor.socketId };
            }
        }
    }
    return {};
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const float hitRadius = std::max(
        8.0f,
        NodePinRadius() + (8.0f * NodeContentScale()));
    const float hitRadiusSq = hitRadius * hitRadius;
    for(const auto& row:m_CompoundEntryRows) {
        if(!LayoutGroupVisible(row.group))continue;
        const float dx=row.output.x-screenPos.x,dy=row.output.y-screenPos.y;
        if(row.source && dx*dx+dy*dy<=hitRadiusSq){if(m_DragOutputNodeId<=0)m_CompoundEntryDragOutput=int(&row-m_CompoundEntryRows.data());return SocketHit{row.source,row.sourceSocket};}
    }
    const std::vector<int>& orderedNodes = GetNodeHitTestOrder(graph);
    for (const int nodeId : orderedNodes) {
        if(!LayoutNodeInteractive(nodeId))continue;
        const NodeLayoutCache* layout = FindNodeLayoutCache(nodeId);
        if (!layout) {
            continue;
        }
        if (layout->interactionRect.IsValid() &&
            !layout->interactionRect.Contains(ToImVec2(screenPos))) {
            continue;
        }
        for (const SocketAnchor& anchor : layout->socketAnchors) {
            if (!IsSocketVisibleDuringDrag(graph, nodeId, anchor.socketId, anchor.direction)) continue;
            if (anchor.direction !=
                EditorNodeGraph::SocketDirection::Output) {
                continue;
            }
            const float dx = anchor.screenPos.x - screenPos.x;
            const float dy = anchor.screenPos.y - screenPos.y;
            if ((dx * dx + dy * dy) <= hitRadiusSq) {
                if(m_DragOutputNodeId<=0)m_CompoundEntryDragOutput=-1;return SocketHit{ nodeId, anchor.socketId };
            }
        }
    }
    return {};
}

int EditorNodeGraphUI::FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const std::vector<int>& orderedNodes = GetNodeHitTestOrder(graph);
    for (const int nodeId : orderedNodes) {
        if(!LayoutNodeInteractive(nodeId))continue;
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

        const EditorNodeGraph::Vec2 pos = GraphToScreen(DisplayNodePosition(*node));
        const EditorNodeGraph::Vec2 size = NodeScreenSize(*node);
        if (screenPos.x >= pos.x && screenPos.x <= pos.x + size.x &&
            screenPos.y >= pos.y && screenPos.y <= pos.y + size.y) {
            return node->id;
        }
    }
    return -1;
}

EditorNodeGraph::Link EditorNodeGraphUI::FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) {
    const int frame =
        ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    const bool labelsHidden = m_LinkLabelRevealAlpha <= 0.001f && m_LinkLabelHitRects.empty();
    const bool straight = GraphStraightLinksEnabled(m_ActiveEditor);
    const auto documentRevision = m_ActiveEditor ? m_ActiveEditor->GetProjectSession().editRevision : 0u;
    if (frame >= 0 &&
        m_LinkHitTestFrame >= 0 &&
        (m_LinkHitTestFrame == frame || (labelsHidden && m_LinkHitTestLabelsHidden)) &&
        m_LinkHitTestGraph == &graph &&
        m_LinkHitTestGraphRevision == graph.GetStructureRevision() &&
        m_LinkHitTestProjectionRevision == m_NodeProjectionRevision &&
        m_LinkHitTestDocumentRevision == documentRevision &&
        m_LinkHitTestStraight == straight &&
        m_LinkHitTestScreenPos.x == screenPos.x &&
        m_LinkHitTestScreenPos.y == screenPos.y) {
        return m_LinkHitTestResult;
    }

    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::LinkHitTest);

    const auto& links = graph.GetLinks();
    EditorNodeGraph::Link nearest;
    float nearestDistance=LinkHitRadiusForZoom(m_Zoom);
    float priorDistance=std::numeric_limits<float>::infinity();
    const auto prior=m_LinkHitTestResult;
    const bool sameOwner=m_LinkHitTestGraph==&graph && m_LinkHitTestGraphRevision==graph.GetStructureRevision();
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        const EditorNodeGraph::Link& link = *it;
        if(ExpansionLinkProgress(link)<1)continue;
        if (!m_LinkLabelHitRects.empty()) {
            const auto labelRect = m_LinkLabelHitRects.find(LinkAnimationKey(link));
            if (labelRect != m_LinkLabelHitRects.end() && labelRect->second.Contains(ToImVec2(screenPos))) {
                CacheLinkHitTest(graph, screenPos, link);
                return link;
            }
        }
        const EditorNodeGraph::Node* from = FindCachedNode(graph, link.fromNodeId);
        const EditorNodeGraph::Node* to = FindCachedNode(graph, link.toNodeId);
        if (!from || !to) {
            continue;
        }

        const auto& path=WirePath(graph,link);
        const float distance=Stack::GraphRouting::DistanceToPath(path.geometry,screenPos);
        if(sameOwner && link.fromNodeId==prior.fromNodeId && link.toNodeId==prior.toNodeId &&
            link.fromSocketId==prior.fromSocketId && link.toSocketId==prior.toSocketId)priorDistance=distance;
        if(distance<nearestDistance) {nearestDistance=distance;nearest=link;}
    }
    // At a genuine intersection, a subpixel distance tie should not flicker
    // between signals. Moving clearly nearer another path still selects it.
    if(priorDistance<LinkHitRadiusForZoom(m_Zoom) && priorDistance<=nearestDistance+1.f)nearest=prior;
    CacheLinkHitTest(graph, screenPos, nearest);
    return nearest;
}

void EditorNodeGraphUI::CacheLinkHitTest(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Vec2& screenPos,
    const EditorNodeGraph::Link& link) {
    m_LinkHitTestFrame =
        ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    m_LinkHitTestGraph = &graph;
    m_LinkHitTestGraphRevision = graph.GetStructureRevision();
    m_LinkHitTestProjectionRevision = m_NodeProjectionRevision;
    m_LinkHitTestDocumentRevision = m_ActiveEditor ? m_ActiveEditor->GetProjectSession().editRevision : 0u;
    m_LinkHitTestStraight = GraphStraightLinksEnabled(m_ActiveEditor);
    m_LinkHitTestLabelsHidden = m_LinkLabelRevealAlpha <= 0.001f && m_LinkLabelHitRects.empty();
    m_LinkHitTestScreenPos = screenPos;
    m_LinkHitTestResult = link;
}

void EditorNodeGraphUI::DrawClippedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color) const {
    drawList->PushClipRect(min, max, true);
    drawList->AddText(min, color, text);
    drawList->PopClipRect();
}
