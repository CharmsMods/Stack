#include "EditorNodeGraphUI.h"

#include "Editor/NodeGraph/SocketPresentation.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Editor/NodeGraph/UI/NodeNumericDefaults.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <string>
#include <unordered_set>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
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

void MixRevision(std::uint64_t& seed, std::uint64_t value) {
    seed ^= value +
        0x9e3779b97f4a7c15ull +
        (seed << 6u) +
        (seed >> 2u);
}

struct OrderedNodeEntry {
    int id = -1;
    bool richSurface = false;
    std::uint64_t frontOrder = 0;
};

using namespace Stack::Editor::NodeGraphUIVisuals;

} // namespace

void EditorNodeGraphUI::BuildNodeLayoutCache(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node,
    NodeLayoutCache& cache) const {
    using namespace Stack::Editor::NodeGraphUILayout;
    const std::uint64_t logicalContentRevision =
        LogicalNodeContentRevision(graph, node);
    std::uint64_t logicalRevision = logicalContentRevision;
    const auto measuredIt =
        m_NodeMeasuredBaseHeights.find(node.id);
    MixRevision(
        logicalRevision,
        std::hash<float>{}(
            measuredIt != m_NodeMeasuredBaseHeights.end()
                ? measuredIt->second
                : 0.0f));
    if (!cache.hasLogicalLayout ||
        cache.logicalRevision != logicalRevision) {
        cache.logicalLayout = BuildLogicalNodeLayout(graph, node);
        cache.logicalContentRevision =
            logicalContentRevision;
        cache.logicalRevision = logicalRevision;
        cache.hasLogicalLayout = true;
    }
    const NodeProjectedLayout projected = ProjectNodeLayout(
        cache.logicalLayout,
        ClampGraphPosition(node.position),
        m_CanvasOrigin,
        m_Pan,
        m_Zoom);
    const auto toCachedRect = [](const LogicalRect& rect) {
        return CachedRect {
            ImVec2(rect.min.x, rect.min.y),
            ImVec2(rect.max.x, rect.max.y)
        };
    };

    cache.frameRect = toCachedRect(projected.body);
    cache.headerRect = toCachedRect(projected.header);
    cache.contentRect = toCachedRect(projected.content);
    cache.contentUsedRect = {};
    cache.persistentVisualRect =
        toCachedRect(projected.bounds.persistentVisual);
    cache.overlayRect = toCachedRect(projected.bounds.overlay);
    cache.interactionRect = toCachedRect(projected.bounds.interaction);
    const float projectedSocketHitRadius =
        (kSocketRadius + kSocketLogicalHitPadding) * m_Zoom;
    const float interactionExpansion =
        std::max(0.0f, 8.0f - projectedSocketHitRadius);
    if (interactionExpansion > 0.0f &&
        cache.interactionRect.IsValid()) {
        cache.interactionRect.min.x -= interactionExpansion;
        cache.interactionRect.min.y -= interactionExpansion;
        cache.interactionRect.max.x += interactionExpansion;
        cache.interactionRect.max.y += interactionExpansion;
    }
    cache.socketAnchors.clear();
    cache.socketAnchors.reserve(projected.sockets.size());
    for (std::size_t index = 0; index < projected.sockets.size(); ++index) {
        const SocketLayout& screenSocket = projected.sockets[index];
        const SocketLayout& logicalSocket = cache.logicalLayout.sockets[index];
        cache.socketAnchors.push_back(SocketAnchor {
            screenSocket.socketId,
            screenSocket.direction,
            logicalSocket.center,
            ImVec2(screenSocket.center.x, screenSocket.center.y)
        });
    }
}

std::uint64_t EditorNodeGraphUI::LogicalNodeContentRevision(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    using namespace Stack::Editor::NodeGraphUILayout;
    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(this, m_ActiveEditor, node, metrics);
    ApplyCanonicalNodeMetrics(this, m_ActiveEditor, node, metrics);
    const GraphStyleTokens geometryOnlyTokens {};
    const NodePresentationProfile profile = BuildNodePresentationProfile(
        this, m_ActiveEditor, node, geometryOnlyTokens);
    const NodeWidthClass widthClass =
        ResolveNodeWidthClass(this, m_ActiveEditor, node);
    const bool compactTitle =
        widthClass == NodeWidthClass::Tile ||
        profile.kind == NodePresentationKind::SummaryOnly ||
        profile.kind == NodePresentationKind::RouteSquare;
    const std::string title = compactTitle
        ? CompactNodeTitle(node)
        : PrimaryNodeTitle(node);

    std::uint64_t seed = 1469598103934665603ull;
    const auto mix = [&](std::size_t value) {
        seed ^= static_cast<std::uint64_t>(value);
        seed *= 1099511628211ull;
    };
    const auto mixFloat = [&](float value) {
        mix(std::hash<float>{}(value));
    };
    mix(static_cast<std::size_t>(node.kind));
    mix(static_cast<std::size_t>(widthClass));
    mix(static_cast<std::size_t>(profile.kind));
    mix(node.expanded ? 1u : 0u);
    mix(profile.showTitle ? 1u : 0u);
    mix(profile.showKindLabel ? 1u : 0u);
    mix(std::hash<std::string>{}(title));
    mixFloat(metrics.width);
    mixFloat(metrics.collapsedHeight);
    mixFloat(metrics.minExpandedHeight);
    mixFloat(metrics.headerInsetX);
    mixFloat(metrics.headerInsetY);
    mixFloat(metrics.bodyInsetBottom);
    mixFloat(metrics.sectionGap);
    mixFloat(metrics.kindLabelHeight);
    mixFloat(metrics.titleHeight);
    mixFloat(ImGui::GetStyle().FontSizeBase);
    mix(reinterpret_cast<std::uintptr_t>(ImGui::GetFont()));
    for (const EditorNodeGraph::SocketDefinition& socket :
         PresentedSockets(graph, node)) {
        mix(std::hash<std::string>{}(socket.id));
        mix(static_cast<std::size_t>(socket.direction));
    }
    return seed;
}

std::uint64_t EditorNodeGraphUI::LogicalNodeLayoutRevision(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    std::uint64_t revision =
        LogicalNodeContentRevision(graph, node);
    const auto measuredIt =
        m_NodeMeasuredBaseHeights.find(node.id);
    MixRevision(
        revision,
        std::hash<float>{}(
            measuredIt != m_NodeMeasuredBaseHeights.end()
                ? measuredIt->second
                : 0.0f));
    return revision;
}

Stack::Editor::NodeGraphUILayout::NodeLogicalLayout
EditorNodeGraphUI::BuildLogicalNodeLayout(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    using namespace Stack::Editor::NodeGraphUILayout;
    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(this, m_ActiveEditor, node, metrics);
    ApplyCanonicalNodeMetrics(this, m_ActiveEditor, node, metrics);

    const GraphStyleTokens geometryOnlyTokens {};
    const NodePresentationProfile profile = BuildNodePresentationProfile(
        this, m_ActiveEditor, node, geometryOnlyTokens);
    const EditorNodeGraph::Vec2 nodeSize = NodeSize(node);
    const NodeWidthClass widthClass =
        ResolveNodeWidthClass(this, m_ActiveEditor, node);
    const bool compactTitle =
        widthClass == NodeWidthClass::Tile ||
        profile.kind == NodePresentationKind::SummaryOnly ||
        profile.kind == NodePresentationKind::RouteSquare;
    const std::string title = compactTitle
        ? CompactNodeTitle(node)
        : PrimaryNodeTitle(node);
    const float titleWrapWidth = compactTitle
        ? std::max(1.0f, nodeSize.x - 12.0f)
        : std::max(
            1.0f,
            nodeSize.x - 2.0f * metrics.headerInsetX - 6.0f);
    const std::vector<std::string> titleLines =
        profile.showTitle
            ? WrapNodeTitle(title, titleWrapWidth)
            : std::vector<std::string> {};
    const bool titleEllipsized =
        titleLines.size() == 2 &&
        titleLines.back().size() >= 3 &&
        titleLines.back().compare(
            titleLines.back().size() - 3,
            3,
            "...") == 0;

    NodeLayoutBuildSpec spec;
    spec.widthClass = widthClass;
    spec.width = nodeSize.x;
    spec.height = nodeSize.y;
    spec.expanded = node.expanded;
    spec.sharedIdentityRow = SharesIdentityWithParameter(node);
    spec.framelessMedia =
        profile.kind == NodePresentationKind::FramelessMedia;
    spec.headerInsetX = metrics.headerInsetX;
    spec.headerInsetY = metrics.headerInsetY;
    spec.bodyInsetBottom = metrics.bodyInsetBottom;
    spec.sectionGap = metrics.sectionGap;
    spec.kindLabelHeight = metrics.kindLabelHeight;
    spec.titleLineHeight = metrics.titleHeight;
    spec.showKindLabel = profile.showKindLabel;
    spec.titleLines = titleLines;
    spec.titleEllipsized = titleEllipsized;
    const std::vector<EditorNodeGraph::SocketDefinition> sockets =
        PresentedSockets(graph, node);
    spec.sockets.reserve(sockets.size());
    for (const EditorNodeGraph::SocketDefinition& socket : sockets) {
        spec.sockets.push_back(SocketLayoutSpec {
            socket.id,
            socket.direction
        });
    }
    return Stack::Editor::NodeGraphUILayout::BuildLogicalNodeLayout(
        spec);
}

bool EditorNodeGraphUI::IsSocketConnected(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::SocketDefinition& socket) const {
    bool connected = false;
    const auto matchSocket = [&](const EditorNodeGraph::Link& link) {
        if (!connected &&
            (socket.direction == EditorNodeGraph::SocketDirection::Input
                ? link.toSocketId == socket.id
                : link.fromSocketId == socket.id)) {
            connected = true;
        }
    };
    if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
        graph.ForEachIncomingLink(socket.nodeId, matchSocket);
    } else {
        graph.ForEachOutgoingLink(socket.nodeId, matchSocket);
    }
    return connected;
}

std::vector<EditorNodeGraph::SocketDefinition> EditorNodeGraphUI::PresentedSockets(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    std::vector<EditorNodeGraph::SocketDefinition> presented;
    for (EditorNodeGraph::SocketDefinition socket : graph.GetSockets(node, false)) {
        const bool revealed = socket.direction == EditorNodeGraph::SocketDirection::Input
            ? m_RevealedAdvancedInputNodes.count(node.id) != 0
            : m_RevealedAdvancedOutputNodes.count(node.id) != 0;
        if (socket.visibilityTier != EditorNodeGraph::SocketVisibilityTier::Advanced ||
            revealed || IsSocketConnected(graph, socket)) {
            socket.visible = true;
            presented.push_back(std::move(socket));
        }
    }
    return presented;
}

void EditorNodeGraphUI::ResetDetailCardHover() {
    m_DetailCardHoverKey.clear();
    m_DetailCardHoverStartedAt = -1.0;
    m_DetailCardHoverSeenThisFrame = false;
}

void EditorNodeGraphUI::BeginDetailCardFrame(bool interactionAllowed) {
    m_DetailCardInteractionAllowed = interactionAllowed;
    m_DetailCardHoverSeenThisFrame = false;
    if (!interactionAllowed) {
        ResetDetailCardHover();
    }
}

void EditorNodeGraphUI::EndDetailCardFrame() {
    if (!m_DetailCardInteractionAllowed || !m_DetailCardHoverSeenThisFrame) {
        ResetDetailCardHover();
    }
}

bool EditorNodeGraphUI::DetailCardDelayElapsed(const std::string& key) {
    if (!m_DetailCardInteractionAllowed || key.empty()) {
        return false;
    }
    m_DetailCardHoverSeenThisFrame = true;
    const double now = ImGui::GetTime();
    if (m_DetailCardHoverKey != key) {
        m_DetailCardHoverKey = key;
        m_DetailCardHoverStartedAt = now;
        return false;
    }
    return m_DetailCardHoverStartedAt >= 0.0 &&
        now - m_DetailCardHoverStartedAt >= 0.7;
}

void EditorNodeGraphUI::RefreshNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) {
    BuildNodeLayoutCache(graph, node, m_NodeLayoutCache[node.id]);
}

const EditorNodeGraphUI::NodeLayoutCache* EditorNodeGraphUI::FindNodeLayoutCache(int nodeId) const {
    const auto it = m_NodeLayoutCache.find(nodeId);
    return it != m_NodeLayoutCache.end() ? &it->second : nullptr;
}

const EditorNodeGraph::Node* EditorNodeGraphUI::FindCachedNode(const EditorNodeGraph::Graph& graph, int nodeId) {
    return graph.FindNode(nodeId);
}

EditorNodeGraph::Node* EditorNodeGraphUI::FindCachedNode(EditorNodeGraph::Graph& graph, int nodeId) {
    return graph.FindNode(nodeId);
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

    std::vector<OrderedNodeEntry> order;
    order.reserve(nodes.size());
    std::unordered_set<int> activeNodeIds;
    activeNodeIds.reserve(nodes.size());
    m_NodeFrontOrder.reserve(nodes.size());
    for (const EditorNodeGraph::Node& node : nodes) {
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
    using namespace Stack::Editor::NodeGraphUILayout;
    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(this, m_ActiveEditor, node, metrics);
    ApplyCanonicalNodeMetrics(this, m_ActiveEditor, node, metrics);
    const NodeWidthClass widthClass =
        ResolveNodeWidthClass(this, m_ActiveEditor, node);
    if (widthClass == NodeWidthClass::Tile) {
        return { kTileNodeSize, kTileNodeSize };
    }
    if (widthClass == NodeWidthClass::RawSource) {
        return { kRawSourceWidth, kRawSourceHeight };
    }
    if (widthClass == NodeWidthClass::Media) {
        return { kMediaNodeWidth, kMediaNodeHeight };
    }

    const float measuredContentHeight = [&]() -> float {
        const auto it = m_NodeMeasuredBaseHeights.find(node.id);
        return it != m_NodeMeasuredBaseHeights.end() ? it->second : 0.0f;
    }();
    const GraphStyleTokens geometryOnlyTokens {};
    const NodePresentationProfile profile = BuildNodePresentationProfile(
        this, m_ActiveEditor, node, geometryOnlyTokens);
    const bool compactTitle =
        widthClass == NodeWidthClass::Tile ||
        profile.kind == NodePresentationKind::SummaryOnly ||
        profile.kind == NodePresentationKind::RouteSquare;
    const std::string title = compactTitle
        ? CompactNodeTitle(node)
        : PrimaryNodeTitle(node);
    const float titleWrapWidth = compactTitle
        ? std::max(1.0f, metrics.width - 12.0f)
        : std::max(
            1.0f,
            metrics.width - 2.0f * metrics.headerInsetX - 6.0f);
    const std::vector<std::string> titleLines = profile.showTitle
        ? WrapNodeTitle(
            title,
            titleWrapWidth)
        : std::vector<std::string>{};
    const float extraTitleHeight = titleLines.size() > 1
        ? metrics.titleHeight + 2.0f
        : 0.0f;
    const float collapsedHeight = std::max(
        metrics.collapsedHeight,
        kCollapsedNodeHeight + extraTitleHeight);
    const float expandedHeight = (!node.expanded)
        ? collapsedHeight
        : std::max(
            metrics.minExpandedHeight,
            measuredContentHeight > 0.0f
                ? measuredContentHeight
                : ExpandedContractHeight(node, metrics, 0.0f) +
                    extraTitleHeight);
    float logicalHeight = std::max(
        collapsedHeight,
        SanitizeFinite(expandedHeight, metrics.minExpandedHeight));
    return { metrics.width, logicalHeight };
}
