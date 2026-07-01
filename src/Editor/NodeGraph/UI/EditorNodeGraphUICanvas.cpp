#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using namespace Stack::Editor::NodeGraphUIVisuals;
using NodeBrowserEntry = EditorNodeGraphDefinitions::NodeCatalogEntry;

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
}

constexpr float kGraphPositionLimit = 20000.0f;

float SanitizeFinite(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

ImU32 ScaleColorAlpha(ImU32 color, float alphaScale) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alphaScale, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

float GraphEdgeFadeAlpha(const ImVec2& point, const ImVec2& min, const ImVec2& max, float fadeDistance) {
    if (fadeDistance <= 0.0f) {
        return 1.0f;
    }
    const float left = (point.x - min.x) / fadeDistance;
    const float right = (max.x - point.x) / fadeDistance;
    const float top = (point.y - min.y) / fadeDistance;
    const float bottom = (max.y - point.y) / fadeDistance;
    const float fade = SmoothStep01(std::min(std::min(left, right), std::min(top, bottom)));
    return fade * fade;
}

void DrawStraightLineWithEdgeFade(
    ImDrawList* drawList,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness,
    const ImVec2& fadeMin,
    const ImVec2& fadeMax,
    float fadeDistance) {
    if (!drawList) {
        return;
    }
    if (fadeDistance <= 0.0f) {
        drawList->AddLine(start, end, color, thickness);
        return;
    }

    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length = std::sqrt((dx * dx) + (dy * dy));
    const int segmentCount = std::clamp(static_cast<int>(length / 6.0f), 12, 180);
    ImVec2 previous = start;
    for (int index = 1; index <= segmentCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(segmentCount);
        const ImVec2 current(
            start.x + (dx * t),
            start.y + (dy * t));
        const ImVec2 midpoint((previous.x + current.x) * 0.5f, (previous.y + current.y) * 0.5f);
        const float alpha = GraphEdgeFadeAlpha(midpoint, fadeMin, fadeMax, fadeDistance);
        if (alpha > 0.001f) {
            drawList->AddLine(previous, current, ScaleColorAlpha(color, alpha), thickness);
        }
        previous = current;
    }
}

EditorNodeGraph::Vec2 ClampGraphPosition(EditorNodeGraph::Vec2 position) {
    position.x = std::clamp(SanitizeFinite(position.x), -kGraphPositionLimit, kGraphPositionLimit);
    position.y = std::clamp(SanitizeFinite(position.y), -kGraphPositionLimit, kGraphPositionLimit);
    return position;
}

bool InsertNewNodeOnExistingLink(
    EditorNodeGraphUI* ui,
    EditorModule* editor,
    const EditorNodeGraph::Link& link,
    int newNodeId) {
    if (!ui || !editor || newNodeId <= 0 || link.fromNodeId <= 0 || link.toNodeId <= 0) {
        return false;
    }

    if (!editor->RemoveGraphLink(link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId)) {
        return false;
    }

    const bool connectedFirst = EditorNodeGraphUI::ConnectOutputToBestInput(
        editor,
        link.fromNodeId,
        link.fromSocketId,
        newNodeId);
    const bool connectedSecond = connectedFirst
        ? EditorNodeGraphUI::ConnectBestOutputToInput(editor, newNodeId, link.toNodeId, link.toSocketId)
        : false;
    if (connectedFirst && connectedSecond) {
        return true;
    }

    editor->RemoveGraphNode(newNodeId);
    editor->ConnectGraphSockets(link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId, nullptr);
    return false;
}

int AddNodeFromBrowserEntry(EditorModule* editor, const NodeBrowserEntry& entry, const EditorNodeGraph::Vec2& graphPos) {
    if (!editor) {
        return -1;
    }
    switch (entry.kind) {
        case EditorNodeGraph::NodeKind::Output:
            editor->AddOutputNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Layer:
            editor->AddLayerNodeAt(static_cast<LayerType>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Scope:
            editor->AddScopeNodeAt(static_cast<EditorNodeGraph::ScopeKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Preview:
            editor->AddPreviewNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            editor->AddMaskNodeAt(static_cast<EditorNodeGraph::MaskGeneratorKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::CustomMask:
            editor->AddCustomMaskNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::MaskCombine:
            editor->AddMaskCombineNodeAt(static_cast<EditorNodeGraph::MaskCombineMode>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            editor->AddMaskUtilityNodeAt(static_cast<EditorNodeGraph::MaskUtilityKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ImageToMask:
            editor->AddImageToMaskNodeAt(static_cast<EditorNodeGraph::ImageToMaskKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            editor->AddImageGeneratorNodeAt(static_cast<EditorNodeGraph::ImageGeneratorKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Mix:
            editor->AddMixNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::DataMath:
            editor->AddDataMathNodeAt(static_cast<EditorNodeGraph::DataMathMode>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            editor->AddChannelSplitNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            editor->AddChannelCombineNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDevelopment:
            editor->AddRawDevelopmentNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDecode:
            editor->AddRawDecodeNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDevelop:
            editor->AddRawDevelopNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            editor->AddRawDetailAutoMaskNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            editor->AddRawDetailFusionNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::HdrMerge:
            editor->AddHdrMergeNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Mfsr:
            editor->AddMfsrNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Lut:
            editor->AddLutNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            editor->AddRawNeuralDenoiseNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::Composite:
            break;
    }
    return editor->GetNodeGraph().GetSelectedNodeId();
}

} // namespace

void EditorNodeGraphUI::Render(EditorModule* editor) {
    m_ActiveEditor = editor;
    EditorNodeGraph::Graph& graph = GetActiveGraph(editor);
    const std::uint64_t structureRevision = graph.GetStructureRevision();
    if (m_LastGraphStructureRevision == 0) {
        m_LastGraphStructureRevision = structureRevision;
    } else if (structureRevision != m_LastGraphStructureRevision) {
        ResetPerGraphVisualCaches();
        m_LastGraphStructureRevision = structureRevision;
    }
    SyncPerGraphVisualCaches(graph);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize = ImVec2(std::max(320.0f, available.x), std::max(320.0f, available.y));
    ImGui::Dummy(canvasSize);
    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    GraphRenderOptions options;
    RenderGraphCanvas(editor, graph, canvasMin, canvasMax, options);
}

void EditorNodeGraphUI::RenderGraphCanvas(
    EditorModule* editor,
    EditorNodeGraph::Graph& graph,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax,
    const GraphRenderOptions& options) {
    bool draggingMask = false;
    bool draggingImage = false;
    if (options.interactive && m_DragOutputNodeId > 0) {
        EditorNodeGraph::SocketDefinition sock;
        if (graph.FindSocket(m_DragOutputNodeId, m_DragOutputSocketId, &sock)) {
            std::unordered_set<int> visited;
            std::string channel = GetUpstreamChannel(graph, m_DragOutputNodeId, m_DragOutputSocketId, visited);
            if (sock.type == EditorNodeGraph::SocketType::Mask ||
                sock.id == "r" || sock.id == "g" || sock.id == "b" || sock.id == "a" ||
                !channel.empty()) {
                draggingMask = true;
            } else if (sock.type == EditorNodeGraph::SocketType::Image) {
                draggingImage = true;
            }
        }
    } else if (options.interactive && m_DragInputNodeId > 0) {
        EditorNodeGraph::SocketDefinition sock;
        if (graph.FindSocket(m_DragInputNodeId, m_DragInputSocketId, &sock)) {
            std::unordered_set<int> visited;
            std::string channel = GetUpstreamChannel(graph, m_DragInputNodeId, m_DragInputSocketId, visited);
            if (sock.type == EditorNodeGraph::SocketType::Mask ||
                sock.id == "r" || sock.id == "g" || sock.id == "b" || sock.id == "a" ||
                !channel.empty()) {
                draggingMask = true;
            } else if (sock.type == EditorNodeGraph::SocketType::Image) {
                draggingImage = true;
            }
        }
    }
    graph.SetForceOutputFourPins(draggingMask);
    if (options.interactive && IsGraphCanvasHovered() && (draggingMask || draggingImage)) {
        graph.SetSocketPreviewIntent(
            FindNodeAt(graph, ToGraphVec2(ImGui::GetMousePos())),
            draggingMask
                ? EditorNodeGraph::SocketPreviewIntent::MaskConnection
                : EditorNodeGraph::SocketPreviewIntent::ImageConnection);
    } else {
        graph.SetSocketPreviewIntent(-1, EditorNodeGraph::SocketPreviewIntent::None);
    }

    m_NodeContentActive = false;
    m_NodeContentHovered = false;
    m_GraphInteractionBlocked = false;
    m_MouseOwner = GraphMouseOwner::None;
    m_LastNodeControlId = 0;
    m_NodeLayoutCache.clear();

    if (options.allowDropTarget && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ADD_NODE_DRAG_PAYLOAD")) {
            if (payload->IsDelivery()) {
                const NodeBrowserEntry* entry = *static_cast<const NodeBrowserEntry**>(payload->Data);
                if (entry) {
                    ImVec2 dropPos = ImGui::GetIO().MousePos;
                    EditorNodeGraph::Vec2 graphPos = ScreenToGraph(ToGraphVec2(dropPos));
                    const EditorNodeGraph::Link hoveredDropLink = FindLinkAt(graph, ToGraphVec2(dropPos));
                    const int newNodeId = AddNodeFromBrowserEntry(editor, *entry, graphPos);
                    if (newNodeId > 0 &&
                        hoveredDropLink.fromNodeId > 0 &&
                        hoveredDropLink.toNodeId > 0 &&
                        hoveredDropLink.fromNodeId != newNodeId &&
                        hoveredDropLink.toNodeId != newNodeId) {
                        InsertNewNodeOnExistingLink(this, editor, hoveredDropLink, newNodeId);
                    }
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    m_CanvasOrigin = ToGraphVec2(canvasMin);
    m_CanvasMin = ToGraphVec2(canvasMin);
    m_CanvasMax = ToGraphVec2(canvasMax);
    const ImVec2 canvasSize(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y);
    if (options.syncEditorViewTransform) {
        editor->ApplyGraphAutoFocusFrame(canvasSize.x, canvasSize.y, m_Pan.x, m_Pan.y, m_Zoom);
        editor->SetGraphDropTargetRect(canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y);
    }
    if (!options.interactive || !m_SmoothZoomActive) {
        m_ZoomTarget = m_Zoom;
    }
    bool graphHovered = options.interactive && IsGraphCanvasHovered();
    if (graphHovered) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float drawersWidth = std::max(editor->GetLeftPanelWidthAnim(), editor->GetNodesPanelWidthAnim());
        if (mousePos.x >= canvasMin.x && mousePos.x <= canvasMin.x + drawersWidth) {
            graphHovered = false;
        }
    }
    if (options.interactive && m_MiddlePanCaptureActive) {
        (void)UpdateMiddlePanCapture(editor, graphHovered, false);
    }
    if (graphHovered) {
        m_LastGraphMousePos = ScreenToGraph(ToGraphVec2(ImGui::GetIO().MousePos));
        m_HasLastGraphMousePos = true;
    }

    if (options.interactive) {
        static bool lastTabDown = false;
        bool tabPressed = false;
        if (ImGui::IsKeyDown(ImGuiKey_Tab)) {
            if (!lastTabDown) {
                tabPressed = true;
            }
            lastTabDown = true;
        } else {
            lastTabDown = false;
        }

        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && tabPressed && !ImGui::GetIO().KeyCtrl) {
            if (IsNodeBrowserOpen()) {
                CloseNodeBrowser();
            } else if (editor->CanConsumeEditorCommandKeys() && (graphHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
                const std::vector<int>& selectedIds = graph.GetSelectedNodeIds();
                if (selectedIds.size() == 1) {
                    int selectedNodeId = selectedIds.front();
                    if (const EditorNodeGraph::Node* selectedNode = graph.FindNode(selectedNodeId)) {
                        m_PushedSourceNodeId = -1;
                        m_PushDistance = 0.0f;
                        m_PushedNodeIds.clear();

                        std::vector<int> downstreamIds = graph.GetDownstreamRenderNodeIds(selectedNodeId);
                        m_PushDistance = 340.0f;
                        m_PushedSourceNodeId = selectedNodeId;
                        for (int id : downstreamIds) {
                            if (id != selectedNodeId) {
                                if (EditorNodeGraph::Node* dsNode = graph.FindNode(id)) {
                                    dsNode->position.x += m_PushDistance;
                                    m_PushedNodeIds.push_back(id);
                                }
                            }
                        }
                        for (int id : m_PushedNodeIds) {
                            if (const EditorNodeGraph::Node* dsNode = graph.FindNode(id)) {
                                RefreshNodeLayoutCache(graph, *dsNode);
                            }
                        }

                        EditorNodeGraph::Vec2 spawnPos = { selectedNode->position.x + m_PushDistance, selectedNode->position.y };
                        OpenNodeBrowser(NodeBrowserMode::GeneralAdd, spawnPos);
                    }
                } else {
                    OpenNodeBrowser(NodeBrowserMode::GeneralAdd, ScreenToGraph(ToGraphVec2(ImGui::GetMousePos())));
                }
            }
        }
    }

    if (options.interactive && m_SmoothZoomActive) {
        const float nextZoom = m_Zoom + ((m_ZoomTarget - m_Zoom) * std::clamp(ImGui::GetIO().DeltaTime * 18.0f, 0.0f, 1.0f));
        m_Zoom = (std::abs(m_ZoomTarget - nextZoom) < 0.0005f) ? m_ZoomTarget : nextZoom;
        m_Pan.x = m_SmoothZoomFocusScreen.x - m_CanvasOrigin.x - m_SmoothZoomFocusGraph.x * m_Zoom;
        m_Pan.y = m_SmoothZoomFocusScreen.y - m_CanvasOrigin.y - m_SmoothZoomFocusGraph.y * m_Zoom;
        if (std::abs(m_ZoomTarget - m_Zoom) < 0.0005f) {
            m_Zoom = m_ZoomTarget;
            m_SmoothZoomActive = false;
        }
    }
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin, canvasMax, true);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const ImVec4 workspaceBg = editor->GetWorkspaceBaseColor();
    const ImVec4 canvasBg = graphStyle.enabled ? graphStyle.canvas : workspaceBg;
    const ImVec2 fadeMin = canvasMin;
    const ImVec2 fadeMax = canvasMax;
    const float wallpaperEdgeFadeDistance = wallpaperSurfaces
        ? std::min(144.0f, std::max(72.0f, std::min(canvasSize.x, canvasSize.y) * 0.14f))
        : 0.0f;
    if (!wallpaperSurfaces) {
        drawList->AddRectFilled(canvasMin, canvasMax, ColorToU32(canvasBg));
    }

    if (!graphStyle.spotlightSurface) {
        const float gridStep = std::max(8.0f, 32.0f * m_Zoom);
        const float luminance = 0.2126f * workspaceBg.x + 0.7152f * workspaceBg.y + 0.0722f * workspaceBg.z;
        const float gridOpacity = std::clamp(graphStyle.gridLineOpacity, 0.0f, 1.0f);
        const ImU32 gridColor = wallpaperSurfaces
            ? ((luminance < 0.5f)
                ? ApplyStyleAlpha(IM_COL32(255, 255, 255, static_cast<int>(12.0f * gridOpacity)))
                : ApplyStyleAlpha(IM_COL32(0, 0, 0, static_cast<int>(10.0f * gridOpacity))))
            : ((luminance < 0.5f)
                ? ApplyStyleAlpha(IM_COL32(255, 255, 255, static_cast<int>(20.0f * gridOpacity)))  // Dark background: soft light grid
                : ApplyStyleAlpha(IM_COL32(0, 0, 0, static_cast<int>(18.0f * gridOpacity))));      // Light background: soft dark grid
        for (float x = std::fmod(m_Pan.x, gridStep); x < canvasSize.x; x += gridStep) {
            DrawStraightLineWithEdgeFade(
                drawList,
                ImVec2(canvasMin.x + x, canvasMin.y),
                ImVec2(canvasMin.x + x, canvasMax.y),
                gridColor,
                1.0f,
                fadeMin,
                fadeMax,
                wallpaperEdgeFadeDistance);
        }
        for (float y = std::fmod(m_Pan.y, gridStep); y < canvasSize.y; y += gridStep) {
            DrawStraightLineWithEdgeFade(
                drawList,
                ImVec2(canvasMin.x, canvasMin.y + y),
                ImVec2(canvasMax.x, canvasMin.y + y),
                gridColor,
                1.0f,
                fadeMin,
                fadeMax,
                wallpaperEdgeFadeDistance);
        }
    }
    ClampPanToContent(graph);

    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        node.position = ClampGraphPosition(node.position);
    }

    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        RefreshNodeLayoutCache(graph, node);
    }
    const float graphElementCullMargin = std::clamp(
        std::min(canvasSize.x, canvasSize.y) * 0.35f,
        180.0f,
        520.0f);
    const auto rectOverlapsCanvas = [&](const CachedRect& rect) {
        if (!rect.IsValid()) {
            return true;
        }
        return rect.max.x >= canvasMin.x - graphElementCullMargin &&
            rect.min.x <= canvasMax.x + graphElementCullMargin &&
            rect.max.y >= canvasMin.y - graphElementCullMargin &&
            rect.min.y <= canvasMax.y + graphElementCullMargin;
    };

    ImDrawListSplitter graphSplitter;
    graphSplitter.Split(drawList, 2);
    graphSplitter.SetCurrentChannel(drawList, 1);
    const std::vector<int>& nodeRenderOrder = GetNodeRenderOrder(graph);
    for (int nodeId : nodeRenderOrder) {
        const NodeLayoutCache* layout = FindNodeLayoutCache(nodeId);
        const bool forceRenderForInteraction =
            nodeId == m_DragNodeId ||
            nodeId == m_DragOutputNodeId ||
            nodeId == m_DragInputNodeId;
        if (!forceRenderForInteraction && layout && !rectOverlapsCanvas(layout->frameRect)) {
            continue;
        }
        if (EditorNodeGraph::Node* node = FindCachedNode(graph, nodeId)) {
            RenderNode(editor, *node);
        }
    }
    graphSplitter.SetCurrentChannel(drawList, 0);
    RenderGroups(editor, graph);
    RenderLinks(graph);
    graphSplitter.Merge(drawList);

    if (options.interactive) {
        RenderInteraction(editor, graph);
    }

    if (options.interactive &&
        graphHovered &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !ImGuiExtras::IsSliderWheelConsumed() &&
        ImGui::GetIO().MouseWheel != 0.0f) {
        editor->CancelGraphAutoFocusTracking();
        ZoomAtMouse(ImGui::GetIO().MouseWheel);
    }

    if (options.interactive) {
        if (options.showValidation) {
            RenderValidationStatus(graph);
        }
        RenderChannelSplitConfirmPrompt(editor);
        const int debugHoveredNodeId = (!m_MiddlePanCaptureActive && IsGraphCanvasHovered())
            ? FindNodeAt(graph, ToGraphVec2(ImGui::GetMousePos()))
            : -1;
        RenderInteractionDebugOverlay(graph, debugHoveredNodeId, m_MouseOwner);
        if (options.showContextMenu) {
            RenderContextMenu(editor);
        }
        if (options.showNodeBrowser) {
            RenderNodeBrowser(editor);
        }
    }

    if (options.showZoomDial && !wallpaperSurfaces) {
        RenderGraphZoomDial(editor, drawList, canvasMin, canvasMax, canvasSize);
    }

    if (options.syncEditorViewTransform) {
        editor->SetGraphViewTransform(canvasMin.x, canvasMin.y, m_Pan.x, m_Pan.y, m_Zoom);
    }

    drawList->PopClipRect();
}

void EditorNodeGraphUI::FitGraphPreviewToCanvas(
    EditorModule* editor,
    const EditorNodeGraph::Graph& graph,
    const ImVec2& canvasSize) {
    (void)editor;

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = -std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();

    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        const EditorNodeGraph::Vec2 size = NodeSize(node);
        minX = std::min(minX, node.position.x);
        minY = std::min(minY, node.position.y);
        maxX = std::max(maxX, node.position.x + size.x);
        maxY = std::max(maxY, node.position.y + size.y);
    }
    for (const EditorNodeGraph::NodeGroup& group : graph.GetGroups()) {
        minX = std::min(minX, group.position.x);
        minY = std::min(minY, group.position.y);
        maxX = std::max(maxX, group.position.x + group.size.x);
        maxY = std::max(maxY, group.position.y + group.size.y);
    }

    if (!std::isfinite(minX) || !std::isfinite(minY) || !std::isfinite(maxX) || !std::isfinite(maxY)) {
        m_Pan = { 40.0f, 40.0f };
        m_Zoom = 1.0f;
        m_ZoomTarget = 1.0f;
        m_SmoothZoomActive = false;
        return;
    }

    const float padding = 72.0f;
    const float graphWidth = std::max(1.0f, maxX - minX);
    const float graphHeight = std::max(1.0f, maxY - minY);
    const float usableWidth = std::max(80.0f, canvasSize.x - (padding * 2.0f));
    const float usableHeight = std::max(80.0f, canvasSize.y - (padding * 2.0f));
    m_Zoom = std::clamp(std::min(usableWidth / graphWidth, usableHeight / graphHeight), 0.16f, 1.35f);
    m_ZoomTarget = m_Zoom;
    m_SmoothZoomActive = false;
    m_Pan.x = ((canvasSize.x - graphWidth * m_Zoom) * 0.5f) - (minX * m_Zoom);
    m_Pan.y = ((canvasSize.y - graphHeight * m_Zoom) * 0.5f) - (minY * m_Zoom);
}

void EditorNodeGraphUI::RenderStaticGraphPreview(
    EditorModule* editor,
    EditorNodeGraph::Graph& graph,
    std::vector<std::shared_ptr<LayerBase>>* layers,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax,
    float opacity) {
    if (!editor || opacity <= 0.001f) {
        return;
    }

    EditorNodeGraph::Graph* previousGraphOverride = m_RenderGraphOverride;
    std::vector<std::shared_ptr<LayerBase>>* previousLayersOverride = m_RenderLayersOverride;
    const bool previousPreviewOnly = m_RenderPreviewOnly;
    EditorModule* previousEditor = m_ActiveEditor;

    m_RenderGraphOverride = &graph;
    m_RenderLayersOverride = layers;
    m_RenderPreviewOnly = true;
    m_ActiveEditor = editor;

    const std::uint64_t structureRevision = graph.GetStructureRevision();
    if (m_LastGraphStructureRevision == 0) {
        m_LastGraphStructureRevision = structureRevision;
    } else if (structureRevision != m_LastGraphStructureRevision) {
        ResetPerGraphVisualCaches();
        m_LastGraphStructureRevision = structureRevision;
    }
    SyncPerGraphVisualCaches(graph);
    FitGraphPreviewToCanvas(editor, graph, ImVec2(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y));

    GraphRenderOptions options;
    options.interactive = false;
    options.syncEditorViewTransform = false;
    options.allowDropTarget = false;
    options.showValidation = false;
    options.showContextMenu = false;
    options.showNodeBrowser = false;
    options.showZoomDial = false;

    // Preview rendering can clamp node positions for fitting, so render a copy
    // to keep cached preset layouts stable across repeated hovers and opens.
    EditorNodeGraph::Graph previewGraph = graph;

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(opacity, 0.0f, 1.0f));
    RenderGraphCanvas(editor, previewGraph, canvasMin, canvasMax, options);
    ImGui::PopStyleVar();

    m_RenderGraphOverride = previousGraphOverride;
    m_RenderLayersOverride = previousLayersOverride;
    m_RenderPreviewOnly = previousPreviewOnly;
    m_ActiveEditor = previousEditor;
}

void EditorNodeGraphUI::RenderGraphZoomDial(
    EditorModule* editor,
    ImDrawList* drawList,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax,
    const ImVec2& canvasSize) const {
    if (!editor || !drawList) {
        return;
    }

    if (canvasSize.x < 240.0f || canvasSize.y < 180.0f) {
        return;
    }

    const float radius = std::clamp(std::min(canvasSize.x, canvasSize.y) * 0.12f, 96.0f, 144.0f);
    const float outwardOffset = radius * 0.02f;
    const ImVec2 center(canvasMax.x + outwardOffset, canvasMin.y - outwardOffset);
    const GraphZoomDialStyle dialStyle = BuildGraphZoomDialStyle(editor, BuildGraphStyleTokens(editor));

    constexpr float kMinZoom = 0.16f;
    constexpr float kMaxZoom = 4.5f;
    constexpr float kMaxRotationDegrees = 150.0f;
    constexpr int kTickCount = 64;
    constexpr float kTau = 6.28318530718f;
    const float safeZoom = std::clamp(m_Zoom, kMinZoom, kMaxZoom);
    float rotationT = 0.0f;
    if (safeZoom >= 1.0f) {
        rotationT = std::clamp(std::log(safeZoom) / std::log(kMaxZoom), 0.0f, 1.0f);
    } else {
        rotationT = -std::clamp(std::log(1.0f / safeZoom) / std::log(1.0f / kMinZoom), 0.0f, 1.0f);
    }
    const float rotation = rotationT * (kMaxRotationDegrees * IM_PI / 180.0f);
    const float baseAngle = IM_PI * 0.25f;

    const ImU32 tickColor = ColorToU32(dialStyle.tick);
    const ImU32 glowColor = ColorToU32(dialStyle.glow);
    for (int tickIndex = 0; tickIndex < kTickCount; ++tickIndex) {
        const float angle = baseAngle + rotation + (static_cast<float>(tickIndex) / static_cast<float>(kTickCount)) * kTau;
        const bool major = (tickIndex % 8) == 0;
        const float innerRadius = radius - (major ? radius * 0.16f : radius * 0.10f);
        const ImVec2 direction(std::cos(angle), std::sin(angle));
        const ImVec2 inner(center.x + direction.x * innerRadius, center.y + direction.y * innerRadius);
        const ImVec2 outer(center.x + direction.x * radius, center.y + direction.y * radius);
        drawList->AddLine(inner, outer, glowColor, major ? 4.8f : 3.2f);
        drawList->AddLine(inner, outer, tickColor, major ? 2.0f : 1.3f);
    }

    const float accentAngles[] = {
        IM_PI * 0.92f,
        IM_PI * 0.98f,
        IM_PI * 1.04f,
        IM_PI * 1.42f,
        IM_PI * 1.48f,
        IM_PI * 1.54f
    };
    const float accentLengths[] = {
        radius * 0.09f,
        radius * 0.12f,
        radius * 0.07f,
        radius * 0.07f,
        radius * 0.11f,
        radius * 0.08f
    };
    const float accentRadii[] = {
        radius * 0.38f,
        radius * 0.32f,
        radius * 0.26f,
        radius * 0.40f,
        radius * 0.33f,
        radius * 0.26f
    };

    for (int accentIndex = 0; accentIndex < IM_ARRAYSIZE(accentAngles); ++accentIndex) {
        const float angle = baseAngle + rotation + accentAngles[accentIndex];
        const ImVec2 direction(std::cos(angle), std::sin(angle));
        const ImVec2 tangent(-direction.y, direction.x);
        const ImVec2 basePoint(center.x + direction.x * accentRadii[accentIndex], center.y + direction.y * accentRadii[accentIndex]);
        const float halfLength = accentLengths[accentIndex] * 0.5f;
        const ImVec2 lineStart(basePoint.x - tangent.x * halfLength, basePoint.y - tangent.y * halfLength);
        const ImVec2 lineEnd(basePoint.x + tangent.x * halfLength, basePoint.y + tangent.y * halfLength);
        drawList->AddLine(lineStart, lineEnd, glowColor, 3.2f);
        drawList->AddLine(lineStart, lineEnd, tickColor, 1.4f);
    }
}
