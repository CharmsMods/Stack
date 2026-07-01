#include "EditorNodeGraphUI.h"

#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <imgui.h>
#include <string>
#include <unordered_set>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

float SnapToPixel(float value) {
    return std::round(value);
}

ImVec2 SnapToPixel(const ImVec2& value) {
    return ImVec2(SnapToPixel(value.x), SnapToPixel(value.y));
}

constexpr float kGraphPositionLimit = 20000.0f;

float SanitizeFinite(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
}

EditorNodeGraph::Vec2 ClampGraphPosition(EditorNodeGraph::Vec2 position) {
    position.x = std::clamp(SanitizeFinite(position.x), -kGraphPositionLimit, kGraphPositionLimit);
    position.y = std::clamp(SanitizeFinite(position.y), -kGraphPositionLimit, kGraphPositionLimit);
    return position;
}

struct OrderedNodeEntry {
    int id = -1;
    bool richSurface = false;
    std::uint64_t frontOrder = 0;
};

using namespace Stack::Editor::NodeGraphUIVisuals;

} // namespace

EditorNodeGraphUI::NodeLayoutCache EditorNodeGraphUI::BuildNodeLayoutCache(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    const NodeLayoutMetrics metrics = MetricsForNode(node);
    NodeLayoutMetrics adjustedMetrics = metrics;
    ApplyModernCompactMetrics(node, adjustedMetrics);
    ApplyLayerSurfaceMetrics(this, m_ActiveEditor, node, adjustedMetrics);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(m_ActiveEditor);
    const NodePresentationProfile profile = BuildNodePresentationProfile(this, m_ActiveEditor, node, graphStyle);
    const float uiScale = NodeContentScale();
    const float pinRadius = NodePinRadius();
    const float headerInsetX = adjustedMetrics.headerInsetX * uiScale;
    const float headerInsetY = adjustedMetrics.headerInsetY * uiScale;
    const float bodyInsetBottom = adjustedMetrics.bodyInsetBottom * uiScale;
    const float sectionGap = adjustedMetrics.sectionGap * uiScale;
    const float laneInset = std::max(
        14.0f * uiScale,
        ((adjustedMetrics.width - adjustedMetrics.contentLaneWidth) * 0.5f - adjustedMetrics.headerInsetX) * uiScale);

    const EditorNodeGraph::Vec2 safePosition = ClampGraphPosition(node.position);
    const EditorNodeGraph::Vec2 nodeScreenPos = GraphToScreen(safePosition);
    const EditorNodeGraph::Vec2 nodeSize = NodeScreenSize(node);
    const ImVec2 frameMin = ToImVec2(nodeScreenPos);
    const ImVec2 frameMax(frameMin.x + nodeSize.x, frameMin.y + nodeSize.y);

    const bool showKindLabel = profile.showKindLabel;
    const float kindLabelBlock = showKindLabel ? (adjustedMetrics.kindLabelHeight * uiScale) + (2.0f * uiScale) : 0.0f;
    const float titleBlock = profile.showTitle ? (adjustedMetrics.titleHeight * uiScale) : 0.0f;
    const float headerVisualHeight = headerInsetY + kindLabelBlock + titleBlock;
    const float expandedHeaderHeight = headerVisualHeight + std::max(6.0f, sectionGap * 0.65f);
    const float collapsedHeaderHeight = std::max(headerVisualHeight + (headerInsetY * 0.45f), frameMax.y - frameMin.y);
    const float headerBottom = std::min(
        frameMax.y - std::max(6.0f * uiScale, bodyInsetBottom * 0.35f),
        frameMin.y + (node.expanded ? expandedHeaderHeight : collapsedHeaderHeight));

    const float contentMinX = std::min(frameMax.x - headerInsetX, frameMin.x + headerInsetX + laneInset);
    const float contentMaxX = std::max(contentMinX + 24.0f, frameMax.x - headerInsetX - laneInset);
    const float contentMinY = node.expanded ? headerBottom : frameMin.y + headerInsetY;
    const float contentMaxY = std::max(contentMinY, frameMax.y - bodyInsetBottom);
    const float inputPinX = frameMin.x + std::max(pinRadius + (6.0f * uiScale), laneInset * 0.52f);
    const float outputPinX = frameMax.x - std::max(pinRadius + (6.0f * uiScale), laneInset * 0.52f);

    NodeLayoutCache cache;
    cache.frameRect = CachedRect{ frameMin, frameMax };
    cache.headerRect = CachedRect{ SnapToPixel(frameMin), SnapToPixel(ImVec2(frameMax.x, headerBottom)) };
    cache.contentRect = CachedRect{
        SnapToPixel(ImVec2(contentMinX, contentMinY)),
        SnapToPixel(ImVec2(contentMaxX, contentMaxY))
    };
    if (profile.kind == NodePresentationKind::FramelessMedia) {
        cache.headerRect = CachedRect{ frameMin, frameMax };
        cache.contentRect = CachedRect{
            SnapToPixel(ImVec2(frameMin.x + 2.0f * uiScale, frameMin.y + 2.0f * uiScale)),
            SnapToPixel(ImVec2(frameMax.x - 2.0f * uiScale, frameMax.y - 2.0f * uiScale))
        };
    }

    std::vector<EditorNodeGraph::SocketDefinition> inputSockets;
    std::vector<EditorNodeGraph::SocketDefinition> outputSockets;
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
        if (!socket.visible) {
            continue;
        }
        if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
            inputSockets.push_back(socket);
        } else {
            outputSockets.push_back(socket);
        }
    }

    auto distributeAnchors = [&](const std::vector<EditorNodeGraph::SocketDefinition>& sockets, EditorNodeGraph::SocketDirection direction) {
        if (sockets.empty()) {
            return;
        }

        const float top = cache.headerRect.min.y + std::max(pinRadius + (4.0f * uiScale), headerInsetY * 0.9f);
        const float bottom = node.expanded
            ? std::max(top, cache.contentRect.max.y - std::max(pinRadius + (2.0f * uiScale), bodyInsetBottom * 0.2f))
            : std::max(top, cache.frameRect.max.y - std::max(pinRadius + (4.0f * uiScale), headerInsetY * 0.9f));

        for (size_t index = 0; index < sockets.size(); ++index) {
            const float y = sockets.size() == 1
                ? (top + bottom) * 0.5f
                : (top + ((bottom - top) * static_cast<float>(index) / static_cast<float>(sockets.size() - 1)));
            cache.socketAnchors.push_back(SocketAnchor{
                sockets[index].id,
                direction,
                ImVec2(
                    direction == EditorNodeGraph::SocketDirection::Input ? inputPinX : outputPinX,
                    y)
            });
        }
    };

    distributeAnchors(inputSockets, EditorNodeGraph::SocketDirection::Input);
    distributeAnchors(outputSockets, EditorNodeGraph::SocketDirection::Output);
    return cache;
}

void EditorNodeGraphUI::RefreshNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) {
    m_NodeLayoutCache[node.id] = BuildNodeLayoutCache(graph, node);
}

const EditorNodeGraphUI::NodeLayoutCache* EditorNodeGraphUI::FindNodeLayoutCache(int nodeId) const {
    const auto it = m_NodeLayoutCache.find(nodeId);
    return it != m_NodeLayoutCache.end() ? &it->second : nullptr;
}

void EditorNodeGraphUI::RefreshNodeLookupCache(const EditorNodeGraph::Graph& graph, bool force) {
    const std::vector<EditorNodeGraph::Node>& nodes = graph.GetNodes();
    if (!force &&
        m_NodeLookupCacheGraph == &graph &&
        m_NodeLookupCacheGraphRevision == graph.GetStructureRevision() &&
        m_NodeLookupCacheNodeCount == nodes.size()) {
        return;
    }

    m_NodeLookupCache.clear();
    m_NodeLookupCache.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        m_NodeLookupCache[nodes[index].id] = index;
    }
    m_NodeLookupCacheGraph = &graph;
    m_NodeLookupCacheGraphRevision = graph.GetStructureRevision();
    m_NodeLookupCacheNodeCount = nodes.size();
}

const EditorNodeGraph::Node* EditorNodeGraphUI::FindCachedNode(const EditorNodeGraph::Graph& graph, int nodeId) {
    auto lookup = [&]() -> const EditorNodeGraph::Node* {
        const std::vector<EditorNodeGraph::Node>& nodes = graph.GetNodes();
        const auto it = m_NodeLookupCache.find(nodeId);
        if (it == m_NodeLookupCache.end() || it->second >= nodes.size()) {
            return nullptr;
        }
        const EditorNodeGraph::Node& node = nodes[it->second];
        return node.id == nodeId ? &node : nullptr;
    };

    RefreshNodeLookupCache(graph);
    if (const EditorNodeGraph::Node* node = lookup()) {
        return node;
    }
    RefreshNodeLookupCache(graph, true);
    return lookup();
}

EditorNodeGraph::Node* EditorNodeGraphUI::FindCachedNode(EditorNodeGraph::Graph& graph, int nodeId) {
    const EditorNodeGraph::Node* found = FindCachedNode(static_cast<const EditorNodeGraph::Graph&>(graph), nodeId);
    if (!found) {
        return nullptr;
    }
    std::vector<EditorNodeGraph::Node>& nodes = graph.GetNodes();
    const auto it = m_NodeLookupCache.find(nodeId);
    if (it == m_NodeLookupCache.end() || it->second >= nodes.size()) {
        return nullptr;
    }
    EditorNodeGraph::Node& node = nodes[it->second];
    return node.id == nodeId ? &node : nullptr;
}

void EditorNodeGraphUI::RefreshNodeOrderCache(const EditorNodeGraph::Graph& graph) {
    const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    const std::vector<EditorNodeGraph::Node>& nodes = graph.GetNodes();
    if (m_NodeOrderCacheFrame == frame &&
        m_NodeOrderCacheGraph == &graph &&
        m_NodeOrderCacheGraphRevision == graph.GetStructureRevision() &&
        m_NodeOrderCacheNodeCount == nodes.size() &&
        m_NodeOrderCacheFrontCounter == m_NodeFrontOrderCounter) {
        return;
    }

    m_NodeLookupCache.clear();
    m_NodeLookupCache.reserve(nodes.size());
    m_NodeLookupCacheGraph = &graph;
    m_NodeLookupCacheGraphRevision = graph.GetStructureRevision();
    m_NodeLookupCacheNodeCount = nodes.size();

    std::vector<OrderedNodeEntry> order;
    order.reserve(nodes.size());
    std::unordered_set<int> activeNodeIds;
    activeNodeIds.reserve(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const EditorNodeGraph::Node& node = nodes[index];
        m_NodeLookupCache[node.id] = index;
        activeNodeIds.insert(node.id);
        auto frontOrderIt = m_NodeFrontOrder.find(node.id);
        if (frontOrderIt == m_NodeFrontOrder.end()) {
            frontOrderIt = m_NodeFrontOrder.emplace(node.id, m_NodeFrontOrderCounter++).first;
        }
        order.push_back(OrderedNodeEntry{
            node.id,
            node.kind == EditorNodeGraph::NodeKind::Layer && node.expanded && ResolveLayerUsesRichNodeSurface(m_ActiveEditor, node.layerIndex),
            frontOrderIt->second });
    }

    PruneAnimatedState(m_NodeSelectionAnim, activeNodeIds);
    PruneAnimatedState(m_NodeHoverAnim, activeNodeIds);

    std::sort(order.begin(), order.end(), [](const OrderedNodeEntry& a, const OrderedNodeEntry& b) {
        if (a.richSurface != b.richSurface) {
            return !a.richSurface;
        }
        if (a.frontOrder != b.frontOrder) {
            return a.frontOrder < b.frontOrder;
        }
        return a.id < b.id;
    });

    m_NodeRenderOrderCache.clear();
    m_NodeRenderOrderCache.reserve(order.size());
    for (const OrderedNodeEntry& node : order) {
        m_NodeRenderOrderCache.push_back(node.id);
    }

    m_NodeHitTestOrderCache.clear();
    m_NodeHitTestOrderCache.reserve(order.size());
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        m_NodeHitTestOrderCache.push_back(it->id);
    }

    m_NodeOrderCacheFrame = frame;
    m_NodeOrderCacheGraph = &graph;
    m_NodeOrderCacheGraphRevision = graph.GetStructureRevision();
    m_NodeOrderCacheNodeCount = nodes.size();
    m_NodeOrderCacheFrontCounter = m_NodeFrontOrderCounter;
}

const std::vector<int>& EditorNodeGraphUI::GetNodeRenderOrder(const EditorNodeGraph::Graph& graph) {
    RefreshNodeOrderCache(graph);
    return m_NodeRenderOrderCache;
}

const std::vector<int>& EditorNodeGraphUI::GetNodeHitTestOrder(const EditorNodeGraph::Graph& graph) {
    RefreshNodeOrderCache(graph);
    return m_NodeHitTestOrderCache;
}

void EditorNodeGraphUI::TouchNodeFront(int nodeId) {
    if (nodeId <= 0) {
        return;
    }
    m_NodeFrontOrder[nodeId] = m_NodeFrontOrderCounter++;
}

const EditorNodeGraphUI::SocketAnchor* EditorNodeGraphUI::FindSocketAnchor(
    const NodeLayoutCache& cache,
    const std::string& socketId,
    EditorNodeGraph::SocketDirection direction) const {
    for (const SocketAnchor& anchor : cache.socketAnchors) {
        if (anchor.direction == direction && anchor.socketId == socketId) {
            return &anchor;
        }
    }
    return nullptr;
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeSize(const EditorNodeGraph::Node& node) const {
    if (node.kind == EditorNodeGraph::NodeKind::ChannelSplit ||
        node.kind == EditorNodeGraph::NodeKind::ChannelCombine ||
        IsSummaryOnlyNode(this, m_ActiveEditor, node)) {
        if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
            return EditorNodeGraph::Vec2{ 128.0f, 72.0f };
        }
        return EditorNodeGraph::Vec2{ 90.0f, 90.0f };
    }

    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(this, m_ActiveEditor, node, metrics);
    const float measuredLayerHeight = [&]() -> float {
        const auto it = m_NodeMeasuredBaseHeights.find(node.id);
        return it != m_NodeMeasuredBaseHeights.end() ? it->second : 0.0f;
    }();
    const float expandedHeight = (!node.expanded)
        ? metrics.collapsedHeight
        : std::max(
            metrics.minExpandedHeight,
            (UsesMeasuredNodeHeight(node) && measuredLayerHeight > 0.0f)
                ? measuredLayerHeight
                : ExpandedContractHeight(node, metrics, measuredLayerHeight));
    return EditorNodeGraph::Vec2{
        metrics.width,
        std::max(metrics.collapsedHeight, SanitizeFinite(expandedHeight, metrics.minExpandedHeight))
    };
}
