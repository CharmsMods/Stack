#include "EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "App/UiPerformanceTrace.h"

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
    RefreshLogicalNodeLayoutCache(graph, node, cache);
    ProjectNodeLayoutCache(node, cache);
}

std::uint64_t EditorNodeGraphUI::LogicalNodeInputRevision(
    const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) const {
    // Socket/schema changes use structural and processing revisions. Placement
    // advances document history without changing declarations or logical size.
    // Expansion, revealed sockets and connection-preview intent are presentation
    // inputs and can change without authoring a document edit.
    std::uint64_t revision = graph.GetStructureRevision();
    const auto mix = [&](std::uint64_t value) { MixRevision(revision, value); };
    mix(reinterpret_cast<std::uintptr_t>(&graph));
    mix(reinterpret_cast<std::uintptr_t>(&node));
    mix(reinterpret_cast<std::uintptr_t>(m_ActiveEditor));
    mix(m_GraphContext.revision);
    if (m_ActiveEditor) {
        mix(m_ActiveEditor->GetRenderRevision());
    } else mix(m_GraphContext.documentRevision);
    mix(static_cast<std::uint64_t>(node.kind));
    mix(static_cast<std::uint64_t>(node.layerType));
    mix(static_cast<std::uint64_t>(node.layerIndex));
    mix(static_cast<std::uint64_t>(node.role));
    mix(std::hash<std::string>{}(node.rawLayerOwnerId));
    mix(static_cast<std::uint64_t>(node.rawOperation.kind));
    mix(static_cast<std::uint64_t>(node.maskKind));
    mix(static_cast<std::uint64_t>(node.maskUtilityKind));
    mix(static_cast<std::uint64_t>(node.imageToMaskKind));
    mix(static_cast<std::uint64_t>(node.imageToMaskSettings.sampleCount));
    mix(static_cast<std::uint64_t>(node.imageGeneratorKind));
    mix(static_cast<std::uint64_t>(node.scopeKind));
    mix(static_cast<std::uint64_t>(node.dataMathMode));
    mix(static_cast<std::uint64_t>(node.technicalImageSettings.operation));
    mix(node.expanded); mix(node.outputEnabled);
    mix(std::hash<std::string>{}(node.title));
    mix(std::hash<std::string>{}(node.typeId));
    mix(static_cast<std::uint64_t>(graph.GetSocketPreviewIntent(node.id)));
    mix(node.kind == EditorNodeGraph::NodeKind::MaskGenerator &&
        node.rawCoverage.value("canvas", std::string{}) == "project");
    mix(m_RevealedAdvancedInputNodes.count(node.id));
    mix(m_RevealedAdvancedOutputNodes.count(node.id));
    mix(reinterpret_cast<std::uintptr_t>(ImGui::GetFont()));
    mix(std::hash<float>{}(ImGui::GetStyle().FontSizeBase));
    if (node.kind == EditorNodeGraph::NodeKind::Layer) {
        const auto spec = ResolveLayerSurfaceSpec(m_ActiveEditor, node.layerIndex);
        mix(static_cast<std::uint64_t>(spec.presentation));
        mix(static_cast<std::uint64_t>(spec.density));
        mix(std::hash<float>{}(spec.preferredWidth));
        mix(std::hash<float>{}(spec.maxWidth));
        mix(ResolveLayerUsesRichNodeSurface(m_ActiveEditor, node.layerIndex));
    }
    return revision;
}

void EditorNodeGraphUI::RefreshLogicalNodeLayoutCache(
    const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node,
    NodeLayoutCache& cache) const {
    using namespace Stack::Editor::NodeGraphUILayout;
    const std::uint64_t inputRevision = LogicalNodeInputRevision(graph, node);
    const bool canReuseInputs = !m_RenderPreviewOnly && !m_RenderCaptureOnly &&
        m_GraphContext.graph == &graph && cache.hasLogicalInputs &&
        cache.logicalInputRevision == inputRevision;
    if (!canReuseInputs) {
        cache.declaredSockets = graph.GetSockets(node, false);
        cache.instanceUuid = node.instanceUuid;
    }
    cache.logicalInputRevision = inputRevision;
    cache.hasLogicalInputs = !m_RenderPreviewOnly && !m_RenderCaptureOnly && m_GraphContext.graph == &graph;
    const std::uint64_t logicalContentRevision = canReuseInputs
        ? cache.logicalContentRevision : LogicalNodeContentRevision(graph, node);
    if (canReuseInputs) Stack::UiPerformance::Add(Stack::UiPerformance::Counter::LogicalReuse);
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
        auto logical = BuildLogicalNodeLayout(graph, node);
        const auto& previous=cache.logicalLayout;
        bool wireGeometryChanged=!cache.hasLogicalLayout || previous.body.min.x!=logical.body.min.x ||
            previous.body.min.y!=logical.body.min.y || previous.body.max.x!=logical.body.max.x ||
            previous.body.max.y!=logical.body.max.y || previous.sockets.size()!=logical.sockets.size();
        for(std::size_t i=0;!wireGeometryChanged && i<logical.sockets.size();++i) {
            const auto& a=previous.sockets[i];const auto& b=logical.sockets[i];
            wireGeometryChanged=a.socketId!=b.socketId || a.direction!=b.direction || a.center.x!=b.center.x || a.center.y!=b.center.y;
        }
        if(wireGeometryChanged)++m_WireWorldRevision;
        cache.logicalLayout=std::move(logical);
        cache.logicalContentRevision =
            logicalContentRevision;
        cache.logicalRevision = logicalRevision;
        cache.hasLogicalLayout = true;
        Stack::UiPerformance::Add(Stack::UiPerformance::Counter::LogicalRebuild);
    }
}

void EditorNodeGraphUI::ProjectNodeLayoutCache(
    const EditorNodeGraph::Node& node, NodeLayoutCache& cache) const {
    using namespace Stack::Editor::NodeGraphUILayout;
    const auto position = ClampGraphPosition(DisplayNodePosition(node));
    const bool folding=!m_RenderPreviewOnly && !m_RenderCaptureOnly && m_ExpansionMotion &&
        m_ExpansionMotion->channelFold && m_ExpansionMotion->columns.count(node.id)!=0;
    cache.contentUsedRect = {};
    if (cache.hasProjection && cache.projectedLogicalRevision == cache.logicalRevision &&
        cache.projectedPosition.x == position.x && cache.projectedPosition.y == position.y &&
        cache.projectedOrigin.x == m_CanvasOrigin.x && cache.projectedOrigin.y == m_CanvasOrigin.y &&
        cache.projectedPan.x == m_Pan.x && cache.projectedPan.y == m_Pan.y && cache.projectedZoom == m_Zoom && !folding) {
        cache.overlayRect = cache.baseOverlayRect;
        Stack::UiPerformance::Add(Stack::UiPerformance::Counter::ProjectionReuse);
        return;
    }
    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::ProjectionRebuild);
    if(!cache.hasProjection ||
        cache.projectedPosition.x!=position.x || cache.projectedPosition.y!=position.y)++m_WireWorldRevision;
    ++m_NodeProjectionRevision;
    // Project only what the persistent cache consumes. Building a temporary
    // NodeProjectedLayout copies every socket/control-slot string on each pan,
    // workspace slide or layout-animation frame, then discards those vectors.
    const auto projectRect = [&](const LogicalRect& logical) {
        const auto rect = ProjectRect(logical, position, m_CanvasOrigin, m_Pan, m_Zoom);
        return CachedRect {
            ImVec2(rect.min.x, rect.min.y),
            ImVec2(rect.max.x, rect.max.y)
        };
    };
    const auto& logical = cache.logicalLayout;
    cache.frameRect = projectRect(logical.body);
    cache.headerRect = projectRect(logical.header);
    cache.contentRect = projectRect(logical.content);
    cache.contentUsedRect = {};
    cache.persistentVisualRect = projectRect(logical.bounds.persistentVisual);
    cache.overlayRect = projectRect(logical.bounds.overlay);
    cache.baseOverlayRect = cache.overlayRect;
    cache.interactionRect = projectRect(logical.bounds.interaction);
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
    // Retain socket/string capacity while geometry moves. Layout changes still
    // replace each identity and direction from the current logical layout.
    cache.socketAnchors.resize(logical.sockets.size());
    for (std::size_t index = 0; index < logical.sockets.size(); ++index) {
        const SocketLayout& socket = logical.sockets[index];
        const auto screen = ProjectPoint(socket.center, position, m_CanvasOrigin, m_Pan, m_Zoom);
        auto& anchor = cache.socketAnchors[index];
        anchor.socketId = socket.socketId;
        anchor.direction = socket.direction;
        anchor.logicalPos = socket.center;
        anchor.screenPos = ImVec2(screen.x, screen.y);
        if(folding && socket.direction==EditorNodeGraph::SocketDirection::Output) {
            const auto start=m_ExpansionMotion->socketOrigins.find(EditorNodeGraph::GraphOutputIdentity(node.id,socket.socketId));
            if(start!=m_ExpansionMotion->socketOrigins.end()) {
                const float t=std::clamp(float(m_ExpansionMotion->elapsed/.22),0.f,1.f),ease=t*t*(3-2*t);
                const auto origin=GraphToScreen(start->second);
                auto& point=anchor.screenPos;
                point={origin.x+(point.x-origin.x)*ease,origin.y+(point.y-origin.y)*ease};
            }
        }
    }
    cache.projectedLogicalRevision = cache.logicalRevision;
    cache.projectedPosition = position;
    cache.projectedOrigin = m_CanvasOrigin;
    cache.projectedPan = m_Pan;
    cache.projectedZoom = m_Zoom;
    // Moving component pins are temporary presentation geometry. Reproject
    // after completion or interruption, even if the node itself did not move.
    cache.hasProjection = !folding;
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
    EditorNodeGraph::Vec2 nodeSize = NodeSize(node);
    if(node.kind==EditorNodeGraph::NodeKind::Compound && node.expanded && !m_NodeMeasuredBaseHeights.count(node.id)) {
        if(const auto* definition=graph.FindCompoundDefinition(node.compound.instance.definition)) {
            float controls=0;
            for(const auto& parameter:definition->parameters) {
                if(parameter.type!=Stack::NodeMath::ParameterType::Scalar)continue;
                controls+=metrics.rowHeight+metrics.sliderHeight+2*metrics.itemGap;
                if(graph.FindAnyInputLink(node.id,EditorNodeGraph::ParameterInputSocketId(parameter.id)))
                    controls+=metrics.rowHeight+metrics.itemGap;
            }
            nodeSize.y=std::max(nodeSize.y,metrics.headerInsetY+metrics.kindLabelHeight+metrics.titleHeight+
                metrics.sectionGap+2*metrics.rowHeight+controls+metrics.bodyInsetBottom);
        }
    }
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
    const auto& sockets =
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

const std::vector<EditorNodeGraph::SocketDefinition>& EditorNodeGraphUI::PresentedSockets(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    auto& layout = m_NodeLayoutCache[node.id];
    const auto inputRevision = LogicalNodeInputRevision(graph, node);
    const bool reusable = !m_RenderPreviewOnly && !m_RenderCaptureOnly &&
        m_GraphContext.graph == &graph;
    if (reusable && layout.hasPresentedSockets &&
        layout.presentedInputRevision == inputRevision)
        return layout.presentedSockets;

    std::vector<EditorNodeGraph::SocketDefinition> presented;
    const bool current = reusable && layout.hasLogicalInputs &&
        layout.logicalInputRevision == inputRevision;
    const auto uncached = current ? std::vector<EditorNodeGraph::SocketDefinition>{} : graph.GetSockets(node, false);
    const auto& sockets = current ? layout.declaredSockets : uncached;
    presented.reserve(sockets.size());
    for (EditorNodeGraph::SocketDefinition socket : sockets) {
        // Retain existing authored tap wires, without advertising a second
        // image output on every adjustment or an input on a canvas-owned shape.
        if (node.kind == EditorNodeGraph::NodeKind::RawOperation && socket.id == "inputImageOut" && !IsSocketConnected(graph,socket))
            continue;
        if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator && node.rawCoverage.value("canvas",std::string{}) == "project" &&
            socket.direction == EditorNodeGraph::SocketDirection::Input)
            continue;
        const bool revealed = socket.direction == EditorNodeGraph::SocketDirection::Input
            ? m_RevealedAdvancedInputNodes.count(node.id) != 0
            : m_RevealedAdvancedOutputNodes.count(node.id) != 0;
        if (socket.visibilityTier != EditorNodeGraph::SocketVisibilityTier::Advanced ||
            revealed || IsSocketConnected(graph, socket)) {
            socket.visible = true;
            presented.push_back(std::move(socket));
        }
    }
    // Borrow this node's immutable presentation until a real schema, connection
    // or reveal change. Preview/capture graphs deliberately keep fresh inputs.
    layout.presentedSockets = std::move(presented);
    layout.presentedInputRevision = inputRevision;
    layout.hasPresentedSockets = reusable;
    return layout.presentedSockets;
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
    Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::NodeOrder);
    const int frame = ImGui::GetCurrentContext() ? ImGui::GetFrameCount() : -1;
    const std::vector<EditorNodeGraph::Node>& nodes = graph.GetNodes();
    if (m_NodeOrderCacheFrame == frame &&
        m_NodeOrderCacheGraph == &graph &&
        m_NodeOrderCacheNodes == nodes.data() &&
        m_NodeOrderCacheGraphRevision == graph.GetStructureRevision() &&
        m_NodeOrderCacheNodeCount == nodes.size() &&
        m_NodeOrderCacheFrontCounter == m_NodeFrontOrderCounter) {
        return;
    }

    std::uint64_t presentation = 1469598103934665603ull;
    for (const auto& node : nodes) {
        MixRevision(presentation, static_cast<std::uint64_t>(node.id));
        MixRevision(presentation, node.kind == EditorNodeGraph::NodeKind::Layer &&
            node.expanded && ResolveLayerUsesRichNodeSurface(m_ActiveEditor, node.layerIndex));
    }
    if (m_NodeOrderCacheGraph == &graph && m_NodeOrderCacheNodes == nodes.data() &&
        m_NodeOrderCacheGraphRevision == graph.GetStructureRevision() &&
        m_NodeOrderCacheNodeCount == nodes.size() &&
        m_NodeOrderCacheFrontCounter == m_NodeFrontOrderCounter &&
        m_NodeOrderCachePresentation == presentation) {
        m_NodeOrderCacheFrame = frame;
        Stack::UiPerformance::Add(Stack::UiPerformance::Counter::OrderReuse);
        return;
    }
    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::OrderRebuild);

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
    m_NodeOrderCacheNodes = nodes.data();
    m_NodeOrderCacheGraphRevision = graph.GetStructureRevision();
    m_NodeOrderCacheNodeCount = nodes.size();
    m_NodeOrderCacheFrontCounter = m_NodeFrontOrderCounter;
    m_NodeOrderCachePresentation = presentation;
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
