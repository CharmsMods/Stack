#include "Utils/GraphCursor.h"
#include "App/WorkspacePresentation.h"
#include "App/WorkspaceInputScope.h"
#include "App/UiPerformanceTrace.h"
#include "App/UiPerformanceProbe.h"
#include "Utils/GraphNumericControls.h"
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
#include <new>
#include <optional>
#include <stdexcept>
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

class ScopedGraphPositionRestore {
public:
    explicit ScopedGraphPositionRestore(
        EditorNodeGraph::Graph& graph)
        : m_Graph(graph) {
        m_Positions.reserve(graph.GetNodes().size());
        for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
            m_Positions.push_back(node.position);
        }
    }

    ~ScopedGraphPositionRestore() {
        const std::size_t restoreCount =
            std::min(m_Positions.size(), m_Graph.GetNodes().size());
        for (std::size_t index = 0; index < restoreCount; ++index) {
            m_Graph.GetNodes()[index].position = m_Positions[index];
        }
    }

    ScopedGraphPositionRestore(
        const ScopedGraphPositionRestore&) = delete;
    ScopedGraphPositionRestore& operator=(
        const ScopedGraphPositionRestore&) = delete;

private:
    EditorNodeGraph::Graph& m_Graph;
    std::vector<EditorNodeGraph::Vec2> m_Positions;
};

bool InsertNewNodeOnExistingLink(
    EditorNodeGraphUI* ui,
    EditorModule* editor,
    const EditorNodeGraph::Link& link,
    int newNodeId) {
    if (!ui || !editor || newNodeId <= 0 || link.fromNodeId <= 0 || link.toNodeId <= 0) {
        return false;
    }

    auto context = editor->GetGraphEditorContext();
    auto proposal = Stack::GraphModel::ProposeInsertion(*context.graph,context.revision,newNodeId,link);
    std::string error;
    if(proposal.analysis.valid)Stack::GraphLayout::ArrangeConnection(proposal.candidate,link.fromNodeId,newNodeId,
        [&](const auto& graph,const auto& node){return ui->MeasureNodeForLayout(graph,node);},error);
    if (context.applyEdit(std::move(proposal),error)) return true;
    editor->RemoveGraphNode(newNodeId,false);
    editor->ShowUiNotification(UiNotificationSeverity::Warning,error,"editor-node-graph-insert");
    return false;
}

int AddNodeFromBrowserEntry(EditorModule* editor, const NodeBrowserEntry& entry, const EditorNodeGraph::Vec2& graphPos) {
    if (!editor) {
        return -1;
    }
    const int firstNewNodeId = editor->GetNodeGraph().GetNextNodeId();
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
        case EditorNodeGraph::NodeKind::MaskTexture:
        case EditorNodeGraph::NodeKind::MaskGenerator:
            editor->AddMaskNodeAt(static_cast<EditorNodeGraph::MaskGeneratorKind>(entry.value), graphPos);
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
        case EditorNodeGraph::NodeKind::Value: {
            EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
            editor->AddValueNodeAt(std::move(prototype.value.value), graphPos);
            break;
        }
        case EditorNodeGraph::NodeKind::FieldMean:
            editor->AddFieldMeanNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Reformat:
            editor->AddReformatNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawOperation:
            editor->AddRawOperationNodeAt(static_cast<Stack::RawRecipe::GraphOperationKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::TechnicalImage:
            editor->AddTechnicalImageNodeAt(
                static_cast<Stack::NodeMath::TechnicalImageOperation>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Compound:
            editor->AddCompoundTemplateNodeAt(static_cast<std::size_t>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            editor->AddChannelSplitNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            editor->AddChannelCombineNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ConstantChannel:
            editor->AddConstantChannelNodeAt(graphPos);
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
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
        case EditorNodeGraph::NodeKind::Composite:
            break;
    }
    const int selectedNodeId = editor->GetNodeGraph().GetSelectedNodeId();
    return selectedNodeId >= firstNewNodeId ? selectedNodeId : -1;
}

} // namespace

void EditorNodeGraphUI::Render(EditorModule* editor) {
    m_ActiveEditor = editor;
    auto context = editor->GetGraphEditorContext();
    if (context.documentId != m_GraphContext.documentId || context.graphId != m_GraphContext.graphId) {
        ResetProjectInteractionState();
    }
    ObserveLayoutRevision(context);
    if (context.documentId != m_GraphContext.documentId || context.graphId != m_GraphContext.graphId ||
        context.documentRevision != m_GraphContext.documentRevision || context.revision != m_GraphContext.revision)
        m_ConnectionCapabilityCache.clear();
    m_GraphContext = context;
    AdvanceLayoutMotion(context);
    EditorNodeGraph::Graph& graph = *context.graph;
    if (editor->IsRawProjectCanvas()) for (auto& node : graph.GetNodes()) node.expanded = true;
    const std::uint64_t structureRevision = graph.GetStructureRevision();
    bool syncVisualCaches = false;
    if (m_LastGraphStructureRevision == 0) {
        m_LastGraphStructureRevision = structureRevision;
        syncVisualCaches = true;
    } else if (structureRevision != m_LastGraphStructureRevision) {
        ResetPerGraphVisualCaches();
        m_LastGraphStructureRevision = structureRevision;
        syncVisualCaches = true;
    }
    if (syncVisualCaches) {
        SyncPerGraphVisualCaches(graph);
    }
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize = ImVec2(std::max(320.0f, available.x), std::max(320.0f, available.y));
    ImGui::Dummy(canvasSize);
    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    GraphRenderOptions options;
    RenderGraphCanvas(editor, graph, canvasMin, canvasMax, options);
}

Stack::EditorGraphCapture::GraphViewportSnapshot EditorNodeGraphUI::GetGraphCaptureViewportSnapshot() const {
    Stack::EditorGraphCapture::GraphViewportSnapshot snapshot;
    snapshot.logicalWidth = std::max(0.0f, m_CanvasMax.x - m_CanvasMin.x);
    snapshot.logicalHeight = std::max(0.0f, m_CanvasMax.y - m_CanvasMin.y);
    snapshot.panX = m_Pan.x;
    snapshot.panY = m_Pan.y;
    snapshot.zoom = m_Zoom;
    return snapshot;
}

Stack::Editor::NodeOpenTransition::Appearance EditorNodeGraphUI::CaptureNodeAppearance(
    EditorModule* editor, int nodeId, ImVec2 canvasMinimum, ImVec2 canvasMaximum, bool preserveViewport) {
    Stack::Editor::NodeOpenTransition::Appearance appearance;
    if (canvasMaximum.x <= canvasMinimum.x || canvasMaximum.y <= canvasMinimum.y) return appearance;
    m_ActiveEditor = editor;
    m_GraphContext = editor->GetGraphEditorContext();
    auto& graph = *m_GraphContext.graph;
    auto* node = graph.FindNode(nodeId);
    if (!node) return appearance;

    // Resolve once against the destination canvas. The next visible Graph frame
    // uses this same camera and the same canonical node layout.
    editor->CancelGraphAutoFocusTracking();
    m_SmoothZoomActive = false;
    m_ZoomTarget = m_Zoom;
    m_CanvasOrigin = m_CanvasMin = ToGraphVec2(canvasMinimum);
    m_CanvasMax = ToGraphVec2(canvasMaximum);
    for (auto& item : graph.GetNodes()) {
        if (editor->IsRawProjectCanvas()) item.expanded = true;
        RefreshLogicalNodeLayoutCache(graph, item, m_NodeLayoutCache[item.id]);
    }
    if(!preserveViewport)ClampPanToContent(graph);
    ProjectNodeLayoutCache(*node, m_NodeLayoutCache[nodeId]);
    auto bounds = m_NodeLayoutCache[nodeId].frameRect;
    // A tool selected from View may not have been visible in the old graph.
    if (!preserveViewport && (bounds.max.x < canvasMinimum.x || bounds.min.x > canvasMaximum.x ||
        bounds.max.y < canvasMinimum.y || bounds.min.y > canvasMaximum.y)) {
        m_Pan.x += (canvasMinimum.x+canvasMaximum.x-bounds.min.x-bounds.max.x)*.5f;
        m_Pan.y += (canvasMinimum.y+canvasMaximum.y-bounds.min.y-bounds.max.y)*.5f;
        ClampPanToContent(graph);
        ProjectNodeLayoutCache(*node, m_NodeLayoutCache[nodeId]);
        bounds = m_NodeLayoutCache[nodeId].frameRect;
    }
    if(!preserveViewport)graph.SelectNode(nodeId);

    // Render only this node into CPU geometry, without submitting a window or
    // processing an image. Suppress input without disabled-style dimming.
    auto* window = ImGui::GetCurrentWindow();
    ImDrawList scratch(ImGui::GetDrawListSharedData());
    scratch._ResetForNewFrame();
    scratch.PushTextureID(ImGui::GetIO().Fonts->TexRef);
    scratch.PushClipRectFullScreen();
    {
        struct RestoreWindow {
            ImGuiWindow* window;
            ImDrawList* drawList;
            ImGuiWindowTempData layout;
            ImGuiLastItemData lastItem;
            ImRect clip;
            bool& capture;
            bool previousCapture;
            ~RestoreWindow() {
                window->DrawList = drawList; window->DC = layout;
                window->ClipRect = clip;
                ImGui::GetCurrentContext()->LastItemData = lastItem;
                capture = previousCapture;
            }
        } restore{window, window->DrawList, window->DC, ImGui::GetCurrentContext()->LastItemData, window->ClipRect,
            m_RenderCaptureOnly, m_RenderCaptureOnly};
        window->DrawList = &scratch;
        // Widget culling uses the window clip independently of the draw list.
        // The destination node can be outside View's image/control child.
        window->ClipRect = ImRect(canvasMinimum, canvasMaximum);
        m_RenderCaptureOnly = true;
        Stack::Workspace::InputScope input(true, false);
        std::function<void()> ignoredAction;
        RenderNode(editor, *node, ignoredAction);
    }
    appearance.Capture(scratch, 0, bounds.min, bounds.max);
    appearance.nodeUuid = node->instanceUuid;
    return appearance;
}

void EditorNodeGraphUI::RenderGraphCanvas(
    EditorModule* editor,
    EditorNodeGraph::Graph& graph,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax,
    const GraphRenderOptions& options) {
    Stack::UiPerformance::Span canvasTiming(Stack::UiPerformance::Stage::GraphCanvas);
    Stack::UiPerformance::Add(Stack::UiPerformance::Counter::Nodes, graph.GetNodes().size());
    bool draggingMask = false;
    bool draggingImage = false;
    if (options.interactive && m_DragOutputNodeId > 0) {
        EditorNodeGraph::SocketDefinition sock;
        if (graph.FindSocket(m_DragOutputNodeId, m_DragOutputSocketId, &sock)) {
            const auto channel = ResolveOutputVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, editor).channel;
            if (sock.type == EditorNodeGraph::SocketType::Mask ||
                sock.type == EditorNodeGraph::SocketType::ScalarField ||
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
            const auto channel = ResolveOutputVisualStyle(graph, m_DragInputNodeId, m_DragInputSocketId, editor).channel;
            if (sock.type == EditorNodeGraph::SocketType::Mask ||
                sock.type == EditorNodeGraph::SocketType::ScalarField ||
                sock.id == "r" || sock.id == "g" || sock.id == "b" || sock.id == "a" ||
                !channel.empty()) {
                draggingMask = true;
            } else if (sock.type == EditorNodeGraph::SocketType::Image) {
                draggingImage = true;
            }
        }
    }
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
    m_NodeLayoutCache.reserve(graph.GetNodes().size());

    // ImGui requires the canvas item to remain current while accepting. Own
    // the delivery value now; resolve geometry after this frame's camera/layout.
    std::optional<NodeBrowserEntry> deliveredEntry;
    ImVec2 deliveredPosition{};
    if (options.allowDropTarget && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ADD_NODE_DRAG_PAYLOAD")) {
            if (payload->IsDelivery() && payload->DataSize == sizeof(const NodeBrowserEntry*)) {
                const NodeBrowserEntry* entry = *static_cast<const NodeBrowserEntry**>(payload->Data);
                if (entry) {
                    deliveredEntry = *entry;
                    deliveredPosition = ImGui::GetIO().MousePos;
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
        float drawersWidth = std::max(editor->GetLeftPanelWidthAnim(),
            HasCatalogHost() ? 0.0f : editor->GetNodesPanelWidthAnim());
        if (mousePos.x >= canvasMin.x && mousePos.x <= canvasMin.x + drawersWidth) {
            graphHovered = false;
        }
    }
    const bool detailCardsAllowed =
        options.interactive &&
        graphHovered &&
        ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !m_MiddlePanCaptureActive &&
        m_DragOutputNodeId <= 0 &&
        m_DragInputNodeId <= 0 &&
        m_DragNodeId <= 0 &&
        !HasDrawerOpen();
    BeginDetailCardFrame(detailCardsAllowed);
    if (options.interactive && m_MiddlePanCaptureActive) {
        (void)UpdateMiddlePanCapture(editor, graphHovered, false);
    }
    if (graphHovered) {
        m_LastGraphMousePos = ScreenToGraph(ToGraphVec2(ImGui::GetIO().MousePos));
        m_HasLastGraphMousePos = true;
    }

    if (options.interactive) {
        m_NodeNotes.BeginFrame(editor->CanConsumeEditorCommandKeys() && ImGui::IsKeyDown(ImGuiKey_F),
            [&](const auto& note) {
                const auto* target = graph.FindNode(note.nodeId);
                return note.documentId == m_GraphContext.documentId && note.graphId == m_GraphContext.graphId &&
                    target && target->instanceUuid == note.instanceUuid;
            });
        const bool reveal = m_NodeNotes.Revealed();
        const float revealTarget = reveal ? 1.f : 0.f;
        if (m_LinkLabelRevealAlpha != revealTarget)
            m_LinkLabelRevealAlpha = GraphReducedMotion() ? revealTarget :
                ImGuiExtras::AnimateTowards(m_LinkLabelRevealAlpha, revealTarget,
                    std::clamp(ImGui::GetIO().DeltaTime,0.f,.05f), reveal ? 26.f : 22.f);
        auto& lastTabDown = m_LastTabDown;
        bool tabPressed = false;
        if (ImGui::IsKeyDown(ImGuiKey_Tab)) {
            if (!lastTabDown) {
                tabPressed = true;
            }
            lastTabDown = true;
        } else {
            lastTabDown = false;
        }

        const auto& tabIo = ImGui::GetIO();
        const bool catalogShortcutAvailable = HasCatalogHost() && !tabIo.WantTextInput &&
            !ImGui::IsAnyItemActive() && !ImGui::IsAnyMouseDown();
        if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && tabPressed &&
            !tabIo.KeyCtrl && !tabIo.KeyAlt && !tabIo.KeyShift && !tabIo.KeySuper) {
            if (HasCatalogHost() && (catalogShortcutAvailable ||
                (m_NodeBrowserSearchFocused && !ImGui::IsAnyMouseDown()))) {
                const bool open = !editor->WorkspacePanelOpen();
                CloseNodeBrowser();
                editor->SetWorkspacePanelOpen(open);
                m_NodeBrowserFocusSearch = open && !m_CatalogAssetsTab;
            } else if (IsCatalogDocked()) {
                if ((editor->CanConsumeEditorCommandKeys() || m_NodeBrowserSearchFocused || catalogShortcutAvailable) &&
                    !ImGui::IsAnyMouseDown() &&
                    (graphHovered || m_NodeBrowserSearchFocused || catalogShortcutAvailable || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))
                    m_NodeBrowserFocusSearch = true;
            } else if (IsNodeBrowserOpen()) {
                CloseNodeBrowser();
            } else if (catalogShortcutAvailable || (editor->CanConsumeEditorCommandKeys() &&
                (graphHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)))) {
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

    // Plain wheel input belongs to the camera. Start it before projection so
    // this frame draws the new zoom. Ctrl-wheel is routed by node controls
    // below; only an unconsumed modifier wheel may fall back to camera zoom.
    const bool noteWheel = options.interactive && ImGui::GetIO().MouseWheel != 0.f &&
        m_NodeNotes.Revealed() && graphHovered &&
        FindNodeAt(graph, ToGraphVec2(ImGui::GetMousePos())) > 0;
    const bool wheelZoomHandled = options.interactive && graphHovered && !noteWheel &&
        !ImGui::GetIO().KeyCtrl && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !ImGuiExtras::IsSliderWheelConsumed() && ImGui::GetIO().MouseWheel != 0.0f;
    if (wheelZoomHandled) {
        editor->CancelGraphAutoFocusTracking();
        ZoomAtMouse(ImGui::GetIO().MouseWheel);
    }
    if (options.interactive && m_SmoothZoomActive) {
        const float nextZoom = GraphReducedMotion() ? m_ZoomTarget :
            ImGuiExtras::AnimateTowards(m_Zoom, m_ZoomTarget, ImGui::GetIO().DeltaTime, 18.0f);
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
    const bool wallpaperSurfaces = false;
    const ImVec4 workspaceBg = editor->GetWorkspaceBaseColor();
    ImVec4 canvasBg = graphStyle.enabled ? graphStyle.canvas : workspaceBg;
    canvasBg.w=1.f;
    const ImVec2 fadeMin = canvasMin;
    const ImVec2 fadeMax = canvasMax;
    const float wallpaperEdgeFadeDistance = wallpaperSurfaces ? 132.0f : 0.0f;
    const bool drawCanvasBackground =
        options.background != Stack::EditorGraphCapture::Background::Transparent;
    if (drawCanvasBackground) {
        drawList->AddRectFilled(canvasMin, canvasMax, ColorToU32(canvasBg));
    }

    if (options.showGrid && !graphStyle.spotlightSurface) {
        const float gridStep = std::max(8.0f, 32.0f * m_Zoom);
        const float luminance = 0.2126f * workspaceBg.x + 0.7152f * workspaceBg.y + 0.0722f * workspaceBg.z;
        const float gridOpacity = std::clamp(graphStyle.gridLineOpacity, 0.0f, 1.0f);
        const ImU32 gridColor = wallpaperSurfaces
            ? ((luminance < 0.5f)
                ? ApplyStyleAlpha(IM_COL32(255, 255, 255, static_cast<int>(28.0f * gridOpacity)))
                : ApplyStyleAlpha(IM_COL32(0, 0, 0, static_cast<int>(10.0f * gridOpacity))))
            : ((luminance < 0.5f)
                ? ApplyStyleAlpha(IM_COL32(255, 255, 255, static_cast<int>(36.0f * gridOpacity)))  // Dark background: soft light grid
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
    if (options.interactive) UpdateActiveNodeDrag(editor, graph);
    const auto prepareNodeProjection = [&] {
        {
            Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::LogicalLayout);
            for (auto& node : graph.GetNodes()) {
                const auto position = ClampGraphPosition(node.position);
                if (position.x != node.position.x || position.y != node.position.y) node.position = position;
                RefreshLogicalNodeLayoutCache(graph, node, m_NodeLayoutCache[node.id]);
            }
        }
        // Neighbor motion must not move the camera underneath a captured pointer.
        if(!m_DragLayout && !m_ExpansionMotion)ClampPanToContent(graph);
        {
            Stack::UiPerformance::Span timing(Stack::UiPerformance::Stage::Projection);
            for (const auto& node : graph.GetNodes()) ProjectNodeLayoutCache(node, m_NodeLayoutCache[node.id]);
        }
    };
    prepareNodeProjection();
    if (deliveredEntry) {
        // Compound gateways participate in picking even when only their group
        // moved. Resolve all drop geometry before any node widgets capture IDs.
        PrepareCompoundEntries(graph);
        PrepareWireRoutes(graph);
        // Labels still describe last frame here. As in RenderLinks, discard
        // them before picking the current wire geometry; drawing rebuilds them.
        m_LinkLabelHitRects.clear();
        m_LinkHitTestFrame = -1;
        const auto graphPosition = ScreenToGraph(ToGraphVec2(deliveredPosition));
        const auto hoveredDropLink = FindLinkAt(graph, ToGraphVec2(deliveredPosition));
        const int newNodeId = AddNodeFromBrowserEntry(editor, *deliveredEntry, graphPosition);
        if (newNodeId > 0 && hoveredDropLink.fromNodeId > 0 && hoveredDropLink.toNodeId > 0 &&
            hoveredDropLink.fromNodeId != newNodeId && hoveredDropLink.toNodeId != newNodeId) {
            InsertNewNodeOnExistingLink(this, editor, hoveredDropLink, newNodeId);
        }
        // Adding/inserting can replace node storage or arrange neighbors.
        // Refresh before obtaining render-order references and drawing them.
        prepareNodeProjection();
    }
    const float graphElementCullMargin = std::clamp(
        std::min(canvasSize.x, canvasSize.y) * 0.35f,
        180.0f,
        520.0f);
    if (Stack::UiPerformance::Probe::Active())
        Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Camera,
            Stack::UiPerformance::Probe::Key(m_Pan.x,m_Pan.y,m_Zoom),
            {canvasMin.x,canvasMin.y,canvasSize.x,canvasSize.y});
    const auto rectOverlapsCanvas = [&](const CachedRect& rect) {
        if (!rect.IsValid()) {
            return true;
        }
        return rect.max.x >= canvasMin.x - graphElementCullMargin &&
            rect.min.x <= canvasMax.x + graphElementCullMargin &&
            rect.max.y >= canvasMin.y - graphElementCullMargin &&
            rect.min.y <= canvasMax.y + graphElementCullMargin;
    };

    if (options.interactive) {
        const auto* cursorAppearance=editor->GetAppearance();
        static const auto fallback=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
        ImGuiExtras::ConfigureGraphCursor(&graph,canvasMin,ImVec2(canvasMin.x+canvasSize.x,canvasMin.y+canvasSize.y),
            graphHovered,cursorAppearance ? cursorAppearance->GetResolvedCreamPalette() : fallback,ImGui::GetWindowViewport());
    }
    ImGuiExtras::BeginGraphWheelFrame(&graph, options.interactive);
    if (!m_GraphDrawSplitter) m_GraphDrawSplitter = std::make_unique<ImDrawListSplitter>();
    auto& graphSplitter = *m_GraphDrawSplitter;
    graphSplitter.Split(drawList, 2);
    graphSplitter.SetCurrentChannel(drawList, 1);
    const std::vector<int>& nodeRenderOrder = GetNodeRenderOrder(graph);
    m_VisibleNodeFrameRects.clear();
    m_VisibleNodeFrameRects.reserve(nodeRenderOrder.size());
    std::function<void()> pendingGraphAction;
    const int openingNode = options.interactive && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
        ? FindNodeAt(graph, ToGraphVec2(ImGui::GetMousePos())) : -1;
    for (int nodeId : nodeRenderOrder) {
        const float revealAlpha=ExpansionNodeAlpha(nodeId);
        if(revealAlpha<=0)continue;
        const NodeLayoutCache* layout = FindNodeLayoutCache(nodeId);
        const bool forceRenderForInteraction =
            nodeId == ImGuiExtras::GraphWheelTargetNode(&graph) ||
            nodeId == m_DragNodeId ||
            nodeId == m_DragOutputNodeId ||
            nodeId == m_DragInputNodeId ||
            nodeId == m_NodeNotes.EditingNode();
        if (!forceRenderForInteraction &&
            layout &&
            !rectOverlapsCanvas(layout->persistentVisualRect)) {
            continue;
        }
        if (layout) {
            m_VisibleNodeFrameRects.push_back(layout->frameRect);
        }
        if (EditorNodeGraph::Node* node = FindCachedNode(graph, nodeId)) {
            if (nodeId == Stack::UiPerformance::Probe::WatchedNode() && layout)
                Stack::UiPerformance::Probe::Drawn(Stack::UiPerformance::Probe::Visual::Node,
                    Stack::UiPerformance::Probe::Key(node->position.x,node->position.y),
                    {layout->headerRect.min.x,layout->headerRect.min.y,
                     layout->headerRect.max.x-layout->headerRect.min.x,layout->headerRect.max.y-layout->headerRect.min.y});
            Stack::UiPerformance::Add(Stack::UiPerformance::Counter::VisibleNodes);
            const bool captureOpen = layout && openingNode == nodeId;
            const auto openingBounds = captureOpen ? layout->frameRect : CachedRect{};
            const int firstVertex = drawList->VtxBuffer.Size;
            const unsigned firstIndex = static_cast<unsigned>(drawList->IdxBuffer.Size);
            if(revealAlpha<1)ImGui::BeginDisabled();
            RenderNode(editor, *node, pendingGraphAction);
            if(revealAlpha<1)ImGui::EndDisabled();
            if(revealAlpha<1)for(int i=firstVertex;i<drawList->VtxBuffer.Size;++i) {
                auto& color=drawList->VtxBuffer[i].col;
                color=(color&~IM_COL32_A_MASK)|(static_cast<ImU32>((color>>IM_COL32_A_SHIFT)*revealAlpha)<<IM_COL32_A_SHIFT);
            }
            if(revealAlpha<1)Stack::UiPerformance::Add(Stack::UiPerformance::Counter::LayoutRevealingNodes);
            if (captureOpen) {
                m_NodeOpenAppearance.Capture(*drawList, firstVertex, openingBounds.min, openingBounds.max, firstIndex);
                m_NodeOpenAppearance.nodeUuid = node->instanceUuid;
            }
        }
    }
    DrawExpansionOrigin(*drawList);
    graphSplitter.SetCurrentChannel(drawList, 0);
    RenderGroups(editor, graph);
    PrepareWireRoutes(graph);
    RenderLinks(graph);
    RenderCompoundEntries(graph);
    graphSplitter.Merge(drawList);

    if (options.interactive) {
        RenderInteraction(editor, graph);
        RenderMaskTextureInspector(editor);
    }

    if (options.interactive &&
        !wheelZoomHandled &&
        !noteWheel &&
        graphHovered &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        !ImGuiExtras::IsSliderWheelConsumed() &&
        ImGui::GetIO().MouseWheel != 0.0f) {
        editor->CancelGraphAutoFocusTracking();
        ZoomAtMouse(ImGui::GetIO().MouseWheel);
    }

    if (options.interactive) {
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

    if (options.syncEditorViewTransform) {
        editor->SetGraphViewTransform(canvasMin.x, canvasMin.y, m_Pan.x, m_Pan.y, m_Zoom);
    }

    EndDetailCardFrame();
    drawList->PopClipRect();

    // Graph commands replace node storage, so finish all canvas reads first.
    if (pendingGraphAction) {
        pendingGraphAction();
    }
    std::string noteError;
    if (!m_NodeNotes.CommitPending([&](const auto& note) {
            return editor->SetGraphNodeNotes(note.documentId, note.graphId, note.nodeId,
                note.instanceUuid, note.text, &noteError);
        }))
        editor->ShowUiNotification(UiNotificationSeverity::Warning, noteError, "graph-node-notes");
    if (!Stack::Workspace::IsPreview() && !ImGui::IsAnyMouseDown() && !ImGui::IsAnyItemActive())
        editor->GetProjectSession().graphHistory.EndGesture();
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
        const auto nodeBounds = NodeGraphBounds(graph, node);
        minX = std::min(minX, nodeBounds.min.x);
        minY = std::min(minY, nodeBounds.min.y);
        maxX = std::max(maxX, nodeBounds.max.x);
        maxY = std::max(maxY, nodeBounds.max.y);
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

void EditorNodeGraphUI::FitGraphCaptureToCanvas(
    EditorModule* editor,
    const EditorNodeGraph::Graph& graph,
    const ImVec2& canvasSize,
    float paddingPercent) {
    (void)editor;
    Stack::EditorGraphCapture::FloatBounds bounds;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        const auto nodeBounds = NodeGraphBounds(graph, node);
        if (!bounds.valid) {
            bounds.minX = nodeBounds.min.x;
            bounds.minY = nodeBounds.min.y;
            bounds.maxX = nodeBounds.max.x;
            bounds.maxY = nodeBounds.max.y;
            bounds.valid = true;
        } else {
            bounds.minX = std::min(bounds.minX, nodeBounds.min.x);
            bounds.minY = std::min(bounds.minY, nodeBounds.min.y);
            bounds.maxX = std::max(bounds.maxX, nodeBounds.max.x);
            bounds.maxY = std::max(bounds.maxY, nodeBounds.max.y);
        }
    }
    for (const EditorNodeGraph::NodeGroup& group : graph.GetGroups()) {
        if (!bounds.valid) {
            bounds.minX = group.position.x;
            bounds.minY = group.position.y;
            bounds.maxX = group.position.x + group.size.x;
            bounds.maxY = group.position.y + group.size.y;
            bounds.valid = true;
        } else {
            bounds.minX = std::min(bounds.minX, group.position.x);
            bounds.minY = std::min(bounds.minY, group.position.y);
            bounds.maxX = std::max(bounds.maxX, group.position.x + group.size.x);
            bounds.maxY = std::max(bounds.maxY, group.position.y + group.size.y);
        }
    }

    const Stack::EditorGraphCapture::CameraTransform transform =
        Stack::EditorGraphCapture::FitBoundsToCanvas(
            bounds,
            canvasSize.x,
            canvasSize.y,
            paddingPercent,
            0.01f,
            4.5f);
    m_Pan = { transform.panX, transform.panY };
    m_Zoom = transform.zoom;
    m_ZoomTarget = m_Zoom;
    m_SmoothZoomActive = false;
}

void EditorNodeGraphUI::RenderGraphCapture(
    EditorModule* editor,
    EditorNodeGraph::Graph& graph,
    std::vector<std::shared_ptr<LayerBase>>* layers,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax,
    const Stack::EditorGraphCapture::Settings& settings,
    const Stack::EditorGraphCapture::GraphViewportSnapshot& viewport) {
    if (!editor || canvasMax.x <= canvasMin.x || canvasMax.y <= canvasMin.y) {
        return;
    }

    EditorNodeGraph::Graph* previousGraphOverride = m_RenderGraphOverride;
    std::vector<std::shared_ptr<LayerBase>>* previousLayersOverride = m_RenderLayersOverride;
    const bool previousPreviewOnly = m_RenderPreviewOnly;
    const bool previousCaptureOnly = m_RenderCaptureOnly;
    EditorModule* previousEditor = m_ActiveEditor;

    m_RenderGraphOverride = &graph;
    m_RenderLayersOverride = layers;
    m_RenderPreviewOnly = false;
    m_RenderCaptureOnly = true;
    m_ActiveEditor = editor;

    const std::uint64_t structureRevision = graph.GetStructureRevision();
    if (m_LastGraphStructureRevision == 0) {
        m_LastGraphStructureRevision = structureRevision;
    } else if (structureRevision != m_LastGraphStructureRevision) {
        ResetPerGraphVisualCaches();
        m_LastGraphStructureRevision = structureRevision;
    }
    SyncPerGraphVisualCaches(graph);

    const ImVec2 canvasSize(canvasMax.x - canvasMin.x, canvasMax.y - canvasMin.y);
    if (settings.scope == Stack::EditorGraphCapture::Scope::EntireGraph) {
        FitGraphCaptureToCanvas(editor, graph, canvasSize, settings.paddingPercent);
    } else {
        m_Pan = { viewport.panX, viewport.panY };
        m_Zoom = std::max(0.01f, viewport.zoom);
        m_ZoomTarget = m_Zoom;
        m_SmoothZoomActive = false;
    }

    GraphRenderOptions options;
    options.interactive = false;
    options.syncEditorViewTransform = false;
    options.allowDropTarget = false;
    options.showValidation = false;
    options.showContextMenu = false;
    options.showNodeBrowser = false;
    options.showGrid = settings.showGrid;
    options.background = settings.background;
    RenderGraphCanvas(editor, graph, canvasMin, canvasMax, options);

    m_RenderGraphOverride = previousGraphOverride;
    m_RenderLayersOverride = previousLayersOverride;
    m_RenderPreviewOnly = previousPreviewOnly;
    m_RenderCaptureOnly = previousCaptureOnly;
    m_ActiveEditor = previousEditor;
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
    const bool previousCaptureOnly = m_RenderCaptureOnly;
    EditorModule* previousEditor = m_ActiveEditor;

    m_RenderGraphOverride = &graph;
    m_RenderLayersOverride = layers;
    m_RenderPreviewOnly = true;
    m_RenderCaptureOnly = false;
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

    // Preview rendering clamps positions for safety. Preserve only the small
    // position array instead of deep-copying every graph payload (including
    // embedded source pixels and custom-mask rasters) on each preview frame.
    ScopedGraphPositionRestore positionRestore(graph);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::clamp(opacity, 0.0f, 1.0f));
    RenderGraphCanvas(editor, graph, canvasMin, canvasMax, options);
    ImGui::PopStyleVar();

    m_RenderGraphOverride = previousGraphOverride;
    m_RenderLayersOverride = previousLayersOverride;
    m_RenderPreviewOnly = previousPreviewOnly;
    m_RenderCaptureOnly = previousCaptureOnly;
    m_ActiveEditor = previousEditor;
}
