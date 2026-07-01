#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <string>

namespace {

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
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

bool IsPointInRect(
    const EditorNodeGraph::Vec2& point,
    const EditorNodeGraph::Vec2& min,
    const EditorNodeGraph::Vec2& max) {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}

} // namespace

bool EditorNodeGraphUI::IsGraphCanvasHovered() const {
    if (m_RenderPreviewOnly) {
        return false;
    }
    return ImGui::IsMouseHoveringRect(ToImVec2(m_CanvasMin), ToImVec2(m_CanvasMax), false);
}

bool EditorNodeGraphUI::CanOpenChannelSplitConfirm(const EditorNodeGraph::Graph& graph, int nodeId) const {
    const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
    if (!node) {
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer) {
        if (!graph.FindInputLink(nodeId, EditorNodeGraph::kImageInputSocketId)) {
            return false;
        }
        for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
            if (link.fromNodeId == nodeId && link.fromSocketId == EditorNodeGraph::kImageOutputSocketId) {
                return true;
            }
        }
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::DataMath &&
        node->dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage) {
        int imageInputCount = 0;
        for (int inputIndex = 0; inputIndex < EditorNodeGraph::kMaxDataMathInputCount; ++inputIndex) {
            const EditorNodeGraph::Link* input = graph.FindInputLink(
                nodeId,
                EditorNodeGraph::DataMathInputSocketId(inputIndex));
            if (input && !graph.IsScalarSocketStream(input->fromNodeId, input->fromSocketId)) {
                ++imageInputCount;
            }
        }
        return imageInputCount >= 2;
    }

    return false;
}

void EditorNodeGraphUI::CancelChannelSplitConfirm() {
    m_ChannelSplitConfirmNodeId = -1;
    m_ChannelSplitConfirmStartTime = 0.0;
    m_ChannelSplitConfirmRect = {};
}

EditorNodeGraphUI::GraphMouseOwner EditorNodeGraphUI::ResolveMouseOwner(
    const EditorNodeGraph::Graph& graph,
    bool graphHovered,
    const SocketHit& hoveredInput,
    const SocketHit& hoveredOutput,
    int hoveredNodeId,
    const EditorNodeGraph::Link& hoveredLink) const {
    (void)graph;
    if (!graphHovered) {
        return GraphMouseOwner::None;
    }

    if (ImGui::IsAnyItemHovered() && hoveredNodeId <= 0 && !hoveredInput.IsValid() && !hoveredOutput.IsValid()) {
        return GraphMouseOwner::None;
    }

    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (anyPopupOpen && !IsNodeBrowserOpen()) {
        return GraphMouseOwner::Popup;
    }
    if (hoveredOutput.IsValid()) {
        return GraphMouseOwner::OutputPin;
    }
    if (hoveredInput.IsValid()) {
        return GraphMouseOwner::InputPin;
    }
    if (hoveredNodeId > 0) {
        if (const NodeLayoutCache* cache = FindNodeLayoutCache(hoveredNodeId)) {
            const ImVec2 mouse = ImGui::GetMousePos();
            if (cache->headerRect.Contains(mouse)) {
                return GraphMouseOwner::NodeHeader;
            }
            const bool overContent =
                cache->contentRect.Contains(mouse) ||
                (cache->contentUsedRect.IsValid() && cache->contentUsedRect.Contains(mouse));
            const ImGuiContext* context = ImGui::GetCurrentContext();
            const bool overRealWidget = (context && (context->HoveredId != 0 || context->ActiveId != 0)) ||
                ImGui::IsAnyItemHovered() ||
                ImGui::IsAnyItemActive();
            if (overContent && overRealWidget) {
                return GraphMouseOwner::NodeContent;
            }
            if (cache->frameRect.Contains(mouse)) {
                return GraphMouseOwner::NodeFrame;
            }
        }
    }
    if (hoveredLink.fromNodeId > 0 && hoveredLink.toNodeId > 0) {
        return GraphMouseOwner::Link;
    }
    return GraphMouseOwner::Canvas;
}

bool EditorNodeGraphUI::IsPointInNodeHeader(int nodeId, const ImVec2& point) const {
    if (const NodeLayoutCache* cache = FindNodeLayoutCache(nodeId)) {
        return cache->headerRect.Contains(point);
    }
    return false;
}

bool EditorNodeGraphUI::IsPointInNodeDraggableRegion(int nodeId, const ImVec2& point) const {
    const NodeLayoutCache* cache = FindNodeLayoutCache(nodeId);
    if (!cache || !cache->frameRect.Contains(point)) {
        return false;
    }
    if (cache->headerRect.Contains(point)) {
        return true;
    }
    if (cache->contentUsedRect.IsValid() && cache->contentUsedRect.Contains(point)) {
        return false;
    }
    return true;
}

void EditorNodeGraphUI::RenderValidationStatus(const EditorNodeGraph::Graph& graph) {
    if (graph.GetNodes().empty()) {
        return;
    }
    const EditorNodeGraph::ValidationResult validation = graph.Validate();
    if (validation.valid && validation.outputConnected && m_StatusMessage.empty()) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pos(m_CanvasOrigin.x + 16.0f, m_CanvasOrigin.y + 16.0f);
    const ImU32 color = validation.valid && validation.outputConnected
        ? IM_COL32(170, 230, 180, 235)
        : IM_COL32(255, 205, 135, 235);
    std::string text = validation.outputConnected ? "Graph valid" : "Output disconnected";
    if (!validation.messages.empty()) {
        text = validation.messages.front();
    }
    if (!m_StatusMessage.empty()) {
        text = m_StatusMessage;
    }
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 pad(10.0f, 6.0f);
    drawList->AddRectFilled(
        ImVec2(pos.x - pad.x, pos.y - pad.y),
        ImVec2(pos.x + textSize.x + pad.x, pos.y + textSize.y + pad.y),
        IM_COL32(18, 20, 24, 170),
        10.0f);
    drawList->AddText(pos, color, text.c_str());
}

void EditorNodeGraphUI::RenderChannelSplitConfirmPrompt(EditorModule* editor) {
    if (m_ChannelSplitConfirmNodeId <= 0 || !editor) {
        m_ChannelSplitConfirmRect = {};
        return;
    }

    const double now = ImGui::GetTime();
    const float elapsed = static_cast<float>(now - m_ChannelSplitConfirmStartTime);
    const float appearT = std::clamp(elapsed / 0.18f, 0.0f, 1.0f);
    const float appearEase = 1.0f - std::pow(1.0f - appearT, 3.0f);
    const EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(m_ChannelSplitConfirmNodeId);
    const bool splittingImageAverage =
        node &&
        node->kind == EditorNodeGraph::NodeKind::DataMath &&
        node->dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage;
    const char* message = splittingImageAverage ? "Split Average Images?" : "Channel Split?";
    const char* helper = "Press G or Enter to confirm";
    const ImVec2 messageSize = ImGui::CalcTextSize(message);
    const ImVec2 helperSize = ImGui::CalcTextSize(helper);
    const float width = std::clamp(std::max(messageSize.x, helperSize.x) + 40.0f, 240.0f, 420.0f);
    const float height = 58.0f;
    const float centerX = (m_CanvasMin.x + m_CanvasMax.x) * 0.5f;
    const float targetY = m_CanvasMin.y + 28.0f;
    const float y = targetY - ((1.0f - appearEase) * 18.0f);
    const ImVec2 min(centerX - width * 0.5f, y);
    const ImVec2 max(centerX + width * 0.5f, y + height);
    m_ChannelSplitConfirmRect = { min, max };

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const float alpha = appearEase;
    drawList->AddRectFilled(min, max, IM_COL32(30, 36, 44, static_cast<int>(228.0f * alpha)), 21.0f);
    drawList->AddRect(min, max, IM_COL32(124, 182, 255, static_cast<int>(220.0f * alpha)), 21.0f, 0, 1.2f);
    drawList->AddText(
        ImVec2(centerX - messageSize.x * 0.5f, y + 10.0f),
        IM_COL32(245, 248, 252, static_cast<int>(255.0f * alpha)),
        message);
    drawList->AddText(
        ImVec2(centerX - helperSize.x * 0.5f, y + 31.0f),
        IM_COL32(180, 206, 230, static_cast<int>(235.0f * alpha)),
        helper);
}

void EditorNodeGraphUI::RenderInteractionDebugOverlay(
    const EditorNodeGraph::Graph& graph,
    int hoveredNodeId,
    GraphMouseOwner owner) {
    if (!m_DebugInteractionOverlay) {
        return;
    }

    auto ownerLabel = [](GraphMouseOwner value) -> const char* {
        switch (value) {
            case GraphMouseOwner::None: return "None";
            case GraphMouseOwner::Canvas: return "Canvas";
            case GraphMouseOwner::NodeFrame: return "NodeFrame";
            case GraphMouseOwner::NodeHeader: return "NodeHeader";
            case GraphMouseOwner::NodeContent: return "NodeContent";
            case GraphMouseOwner::InputPin: return "InputPin";
            case GraphMouseOwner::OutputPin: return "OutputPin";
            case GraphMouseOwner::Link: return "Link";
            case GraphMouseOwner::Popup: return "Popup";
        }
        return "Unknown";
    };

    const ImGuiContext* context = ImGui::GetCurrentContext();
    const EditorNodeGraph::Link hoveredLink = IsGraphCanvasHovered()
        ? FindLinkAt(graph, ToGraphVec2(ImGui::GetMousePos()))
        : EditorNodeGraph::Link{};
    ImGui::SetNextWindowPos(ImVec2(m_CanvasMax.x - 308.0f, m_CanvasMin.y + 16.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(292.0f, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18, 22, 24, 224));
    if (ImGui::Begin(
            "Graph Interaction Debug",
            nullptr,
            ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::Text("Hovered node: %d", hoveredNodeId);
        ImGui::Text("Hovered input: %d / %s", m_HoveredInputNodeId, m_HoveredInputSocketId.c_str());
        ImGui::Text("Hovered output: %d / %s", m_HoveredOutputNodeId, m_HoveredOutputSocketId.c_str());
        ImGui::Text("Hovered link: %d -> %d", hoveredLink.fromNodeId, hoveredLink.toNodeId);
        ImGui::Text("Owner: %s", ownerLabel(owner));
        ImGui::Text("Active item: %u", context ? static_cast<unsigned int>(context->ActiveId) : 0u);
        ImGui::Text("Last node control: %u", static_cast<unsigned int>(m_LastNodeControlId));
        const float measuredHeight = [&]() {
            const auto it = m_NodeMeasuredBaseHeights.find(hoveredNodeId);
            return it != m_NodeMeasuredBaseHeights.end() ? it->second : 0.0f;
        }();
        const float finalHeight = [&]() {
            const NodeLayoutCache* cache = FindNodeLayoutCache(hoveredNodeId);
            return cache && cache->frameRect.IsValid() ? cache->frameRect.max.y - cache->frameRect.min.y : 0.0f;
        }();
        const bool overflow = [&]() {
            const auto it = m_NodeContentOverflow.find(hoveredNodeId);
            return it != m_NodeContentOverflow.end() && it->second;
        }();
        ImGui::Text("Drag node: %d", m_DragNodeId);
        ImGui::Text("Measured/final h: %.1f / %.1f", measuredHeight, finalHeight);
        ImGui::Text("Overflow: %s", overflow ? "yes" : "no");
        ImGui::Text("Node hovered: %s", m_NodeContentHovered ? "yes" : "no");
        ImGui::Text("Node active: %s", m_NodeContentActive ? "yes" : "no");
        ImGui::Text("Graph blocked: %s", m_GraphInteractionBlocked ? "yes" : "no");
        ImGui::Text("Any popup: %s", ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ? "yes" : "no");
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

void EditorNodeGraphUI::StopMiddlePanCapture() {
    m_MiddlePanCaptureActive = false;
    m_MiddlePanSuppressDeltaFrames = 0;
    m_MiddlePanLastUpdateFrame = -1;
    m_MiddlePanAnchorScreenPos = ImVec2(0.0f, 0.0f);
    m_MiddlePanRestoreScreenPos = ImVec2(0.0f, 0.0f);
}

bool EditorNodeGraphUI::UpdateMiddlePanCapture(EditorModule* editor, bool graphHovered, bool allowStart) {
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (m_MiddlePanCaptureActive &&
        (!ImGui::IsMouseDown(ImGuiMouseButton_Middle) || anyPopupOpen)) {
        StopMiddlePanCapture();
        return false;
    }

    bool middlePanStartedThisFrame = false;
    if (!m_MiddlePanCaptureActive &&
        allowStart &&
        graphHovered &&
        !anyPopupOpen &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
        if (editor) {
            editor->CancelGraphAutoFocusTracking();
        }
        m_MiddlePanCaptureActive = true;
        m_MiddlePanSuppressDeltaFrames = 1;
        m_MiddlePanLastUpdateFrame = -1;
        m_MiddlePanAnchorScreenPos = ImGui::GetMousePos();
        m_MiddlePanRestoreScreenPos = m_MiddlePanAnchorScreenPos;
        middlePanStartedThisFrame = true;
    }

    if (!m_MiddlePanCaptureActive) {
        return false;
    }

    const int frame = ImGui::GetFrameCount();
    if (m_MiddlePanLastUpdateFrame != frame) {
        ImVec2 delta(0.0f, 0.0f);
        if (middlePanStartedThisFrame) {
            delta = ImVec2(0.0f, 0.0f);
        } else if (m_MiddlePanSuppressDeltaFrames > 0) {
            --m_MiddlePanSuppressDeltaFrames;
        } else {
            delta = ImGui::GetIO().MouseDelta;
        }
        if (std::isfinite(delta.x) && std::isfinite(delta.y)) {
            m_Pan.x += delta.x;
            m_Pan.y += delta.y;
        }
        m_MiddlePanLastUpdateFrame = frame;
    }

    ImGuiExtras::SubmitCursorCaptureRequest(ImGuiExtras::CursorCaptureRequest{
        ImGuiExtras::CursorCaptureMode::LockedPan,
        m_MiddlePanAnchorScreenPos,
        m_MiddlePanRestoreScreenPos
    });
    return true;
}

void EditorNodeGraphUI::RenderInteraction(EditorModule* editor, const EditorNodeGraph::Graph& graph) {
    if (m_EditingGroupId > 0) {
        StopMiddlePanCapture();
        m_GraphInteractionBlocked = true;
        m_MouseOwner = GraphMouseOwner::Popup;
        m_BoxSelecting = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_EditingGroupId = -1;
        }
        return;
    }

    const bool graphHovered = IsGraphCanvasHovered();
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool graphWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (m_MiddlePanCaptureActive) {
        m_HoveredInputNodeId = -1;
        m_HoveredInputSocketId.clear();
        m_HoveredOutputNodeId = -1;
        m_HoveredOutputSocketId.clear();
        m_MouseOwner = GraphMouseOwner::Canvas;
        m_GraphInteractionBlocked = false;
        m_BoxSelecting = false;
        if (graphWindowFocused &&
            ImGui::GetIO().KeyCtrl &&
            ImGui::GetIO().KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_G, false)) {
            m_DebugInteractionOverlay = !m_DebugInteractionOverlay;
        }
        (void)UpdateMiddlePanCapture(editor, graphHovered, false);
        return;
    }

    const EditorNodeGraph::Vec2 mouse = ToGraphVec2(ImGui::GetMousePos());
    const SocketHit hoveredInput = graphHovered ? FindInputPinAt(graph, mouse) : SocketHit{};
    const SocketHit hoveredOutput = graphHovered ? FindOutputPinAt(graph, mouse) : SocketHit{};
    m_HoveredInputNodeId = hoveredInput.nodeId;
    m_HoveredInputSocketId = hoveredInput.socketId;
    m_HoveredOutputNodeId = hoveredOutput.nodeId;
    m_HoveredOutputSocketId = hoveredOutput.socketId;
    const int hoveredNodeId = graphHovered ? FindNodeAt(graph, mouse) : -1;
    const EditorNodeGraph::Link hoveredLink = graphHovered ? FindLinkAt(graph, mouse) : EditorNodeGraph::Link{};
    const bool additiveSelect = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
    const bool graphHotkeysBlockedByNodeControls = m_NodeContentActive;
    if (graphWindowFocused &&
        ImGui::GetIO().KeyCtrl &&
        ImGui::GetIO().KeyAlt &&
        ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        m_DebugInteractionOverlay = !m_DebugInteractionOverlay;
    }

    const bool plainGPressed =
        editor->CanConsumeEditorCommandKeys() &&
        !graphHotkeysBlockedByNodeControls &&
        !ImGui::GetIO().KeyCtrl &&
        !ImGui::GetIO().KeyAlt &&
        !ImGui::GetIO().KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_G, false);
    const auto& selectedNodeIds = graph.GetSelectedNodeIds();
    if (m_ChannelSplitConfirmNodeId > 0) {
        const bool selectionStillValid =
            selectedNodeIds.size() == 1 &&
            selectedNodeIds.front() == m_ChannelSplitConfirmNodeId &&
            CanOpenChannelSplitConfirm(graph, m_ChannelSplitConfirmNodeId);
        const ImVec2 mousePos = ImGui::GetMousePos();
        const bool clickedOutsidePrompt =
            (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) &&
            (!m_ChannelSplitConfirmRect.IsValid() || !m_ChannelSplitConfirmRect.Contains(mousePos));
        if (!graphWindowFocused ||
            anyPopupOpen ||
            !selectionStillValid ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
            clickedOutsidePrompt) {
            CancelChannelSplitConfirm();
        } else if (plainGPressed || ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            const int confirmNodeId = m_ChannelSplitConfirmNodeId;
            const EditorNodeGraph::Node* confirmNode = graph.FindNode(confirmNodeId);
            const bool splitImageAverage =
                confirmNode &&
                confirmNode->kind == EditorNodeGraph::NodeKind::DataMath &&
                confirmNode->dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage;
            CancelChannelSplitConfirm();
            const bool splitOk = splitImageAverage
                ? editor->SplitImageAverageNodeIntoChannelAverages(confirmNodeId)
                : editor->SplitLayerNodeIntoChannels(confirmNodeId);
            if (splitOk) {
                m_StatusMessage = splitImageAverage
                    ? "Image average split into channel averages."
                    : "Channel split created.";
            } else {
                m_StatusMessage = splitImageAverage
                    ? "Image average split failed."
                    : "Channel split failed.";
            }
            return;
        }
    } else if (plainGPressed &&
               graphWindowFocused &&
               !anyPopupOpen &&
               !HasDrawerOpen() &&
               selectedNodeIds.size() == 1 &&
               CanOpenChannelSplitConfirm(graph, selectedNodeIds.front())) {
        m_ChannelSplitConfirmNodeId = selectedNodeIds.front();
        m_ChannelSplitConfirmStartTime = ImGui::GetTime();
        m_ChannelSplitConfirmRect = {};
        return;
    }

    if (HasDrawerOpen()) {
        StopMiddlePanCapture();
        CancelChannelSplitConfirm();
        m_GraphInteractionBlocked = true;
        m_MouseOwner = GraphMouseOwner::Popup;
        m_BoxSelecting = false;
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_DragNodeId = -1;
        }
        return;
    }

    m_MouseOwner = ResolveMouseOwner(graph, graphHovered, hoveredInput, hoveredOutput, hoveredNodeId, hoveredLink);
    const bool ownerIsNode = m_MouseOwner == GraphMouseOwner::NodeHeader || m_MouseOwner == GraphMouseOwner::NodeFrame;
    const bool ownerIsContent = m_MouseOwner == GraphMouseOwner::NodeContent || m_MouseOwner == GraphMouseOwner::Popup;
    const bool ownerIsLink = m_MouseOwner == GraphMouseOwner::Link;
    m_GraphInteractionBlocked = ownerIsContent;

    // Intercept Canvas clicks to handle group box selection, resizing, and dragging
    int hitGroupId = -1;
    bool hitResize = false;
    bool hitHeader = false;
    bool hitBackground = false;
    
    if (graphHovered && m_MouseOwner == GraphMouseOwner::Canvas) {
        const EditorNodeGraph::Vec2 mouseGraph = ScreenToGraph(mouse);
        auto& mutableGraph = editor->GetNodeGraph();
        const auto& groups = mutableGraph.GetGroups();
        for (auto it = groups.rbegin(); it != groups.rend(); ++it) {
            const auto& group = *it;
            EditorNodeGraph::Vec2 groupMin = group.position;
            EditorNodeGraph::Vec2 groupMax = {group.position.x + group.size.x, group.position.y + group.size.y};
            EditorNodeGraph::Vec2 headerMax = {group.position.x + group.size.x, group.position.y + 28.0f};
            EditorNodeGraph::Vec2 resizeMin = {group.position.x + group.size.x - 16.0f, group.position.y + group.size.y - 16.0f};
            
            if (IsPointInRect(mouseGraph, resizeMin, groupMax)) {
                hitGroupId = group.id;
                hitResize = true;
                break;
            } else if (IsPointInRect(mouseGraph, groupMin, headerMax)) {
                hitGroupId = group.id;
                hitHeader = true;
                break;
            } else if (IsPointInRect(mouseGraph, groupMin, groupMax)) {
                hitGroupId = group.id;
                hitBackground = true;
                break;
            }
        }
    }

    m_HoveredGroupId = hitGroupId;

    if (hitGroupId > 0) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hitResize) {
                m_ResizingGroupId = hitGroupId;
            } else {
                m_DragGroupId = hitGroupId;
            }
            m_BoxSelecting = false;
        }
        
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hitHeader) {
            m_EditingGroupId = hitGroupId;
            if (const auto* g = editor->GetNodeGraph().FindGroup(hitGroupId)) {
                strncpy_s(m_GroupRenameBuffer, g->title.c_str(), sizeof(m_GroupRenameBuffer) - 1);
            }
            m_DragGroupId = -1;
            m_ResizingGroupId = -1;
        }
    }

    if (m_ResizingGroupId > 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if (std::isfinite(delta.x) && std::isfinite(delta.y) && m_Zoom > 0.0001f) {
                if (auto* group = editor->GetNodeGraph().FindGroup(m_ResizingGroupId)) {
                    group->size.x += delta.x / m_Zoom;
                    group->size.y += delta.y / m_Zoom;
                    group->size.x = std::max(100.0f, group->size.x);
                    group->size.y = std::max(80.0f, group->size.y);
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_ResizingGroupId = -1;
        }
        return;
    }

    if (m_DragGroupId > 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if (std::isfinite(delta.x) && std::isfinite(delta.y) && m_Zoom > 0.0001f) {
                if (auto* group = editor->GetNodeGraph().FindGroup(m_DragGroupId)) {
                    float dx = delta.x / m_Zoom;
                    float dy = delta.y / m_Zoom;
                    
                    // Shift nodes whose visible bounds overlap the group.
                    for (auto& node : editor->GetNodeGraph().GetNodes()) {
                        const EditorNodeGraph::Vec2 nodeSize = NodeGraphFootprintSize(node);
                        const bool overlapsGroup =
                            node.position.x < group->position.x + group->size.x &&
                            node.position.x + nodeSize.x > group->position.x &&
                            node.position.y < group->position.y + group->size.y &&
                            node.position.y + nodeSize.y > group->position.y;
                        if (overlapsGroup) {
                            
                            node.position.x += dx;
                            node.position.y += dy;
                            node.position = ClampGraphPosition(node.position);
                        }
                    }
                    
                    // Shift group position
                    group->position.x += dx;
                    group->position.y += dy;
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_DragGroupId = -1;
        }
        return;
    }

    if (UpdateMiddlePanCapture(editor, graphHovered && !ownerIsContent, true)) {
        return;
    }

    if (m_MouseOwner == GraphMouseOwner::OutputPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        TouchNodeFront(m_HoveredOutputNodeId);
        m_DragOutputNodeId = m_HoveredOutputNodeId;
        m_DragOutputSocketId = m_HoveredOutputSocketId;
    }
    if (m_MouseOwner == GraphMouseOwner::InputPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        TouchNodeFront(m_HoveredInputNodeId);
        m_DragInputNodeId = m_HoveredInputNodeId;
        m_DragInputSocketId = m_HoveredInputSocketId;
    }

    if (!anyPopupOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
        (m_MouseOwner == GraphMouseOwner::Canvas || ownerIsNode || ownerIsLink)) {
        m_ContextGraphPos = ScreenToGraph(ToGraphVec2(ImGui::GetMousePos()));
        m_ContextTarget = ContextTarget::Canvas;
        m_ContextNodeId = -1;
        m_ContextLink = {};
        if (ownerIsLink) {
            m_ContextTarget = ContextTarget::Link;
            m_ContextLink = hoveredLink;
            editor->GetNodeGraph().SelectLink(hoveredLink.fromNodeId, hoveredLink.fromSocketId, hoveredLink.toNodeId, hoveredLink.toSocketId);
        } else if (ownerIsNode && hoveredNodeId > 0) {
            m_ContextTarget = ContextTarget::Node;
            m_ContextNodeId = hoveredNodeId;
            TouchNodeFront(hoveredNodeId);
            if (!editor->GetNodeGraph().IsNodeSelected(hoveredNodeId)) {
                editor->SelectGraphNode(hoveredNodeId);
            }
        }
        m_ContextMenuOpenedAt = ImGui::GetTime();
        m_ContextMenuFadeActive = true;
        ImGui::OpenPopup("EditorNodeGraphContextMenu");
    }

    if (m_DragOutputNodeId > 0) {
        RenderPendingOutputLinkDrag(editor, graph, hoveredInput);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredInput.IsValid()) {
                std::string error;
                if (!editor->ConnectGraphSockets(m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId, &error)) {
                    m_StatusMessage = error;
                } else {
                    m_StatusMessage.clear();
                }
            } else if (hoveredNodeId > 0) {
                if (!EditorNodeGraphUI::ConnectOutputToBestInput(editor, m_DragOutputNodeId, m_DragOutputSocketId, hoveredNodeId)) {
                    m_StatusMessage = "No compatible input socket found on target node.";
                } else {
                    m_StatusMessage.clear();
                }
            } else if (graphHovered) {
                m_NodeBrowserDragFromNodeId = m_DragOutputNodeId;
                m_NodeBrowserDragFromSocketId = m_DragOutputSocketId;
                OpenNodeBrowser(NodeBrowserMode::ConnectFromOutput, ScreenToGraph(ToGraphVec2(ImGui::GetMousePos())));
            }
            m_DragOutputNodeId = -1;
            m_DragOutputSocketId.clear();
        }
        return;
    }

    if (m_DragInputNodeId > 0) {
        RenderPendingInputLinkDrag(editor, graph, hoveredOutput);

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredOutput.IsValid()) {
                std::string error;
                if (!editor->ConnectGraphSockets(hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId, &error)) {
                    m_StatusMessage = error;
                } else {
                    m_StatusMessage.clear();
                }
            } else if (hoveredNodeId > 0) {
                if (!EditorNodeGraphUI::ConnectBestOutputToInput(editor, hoveredNodeId, m_DragInputNodeId, m_DragInputSocketId)) {
                    m_StatusMessage = "No compatible output socket found on target node.";
                } else {
                    m_StatusMessage.clear();
                }
            } else if (graphHovered) {
                m_NodeBrowserDragToNodeId = m_DragInputNodeId;
                m_NodeBrowserDragToSocketId = m_DragInputSocketId;
                OpenNodeBrowser(NodeBrowserMode::ConnectFromInput, ScreenToGraph(ToGraphVec2(ImGui::GetMousePos())));
            }
            m_DragInputNodeId = -1;
            m_DragInputSocketId.clear();
        }
        return;
    }

    if (m_DragNodeId > 0) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            if (std::isfinite(delta.x) && std::isfinite(delta.y) && m_Zoom > 0.0001f) {
                for (int nodeId : editor->GetNodeGraph().GetSelectedNodeIds()) {
                    if (EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(nodeId)) {
                        node->position.x += delta.x / m_Zoom;
                        node->position.y += delta.y / m_Zoom;
                        node->position = ClampGraphPosition(node->position);
                    }
                }
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_DragNodeId = -1;
        }
        return;
    }

    if (m_MouseOwner == GraphMouseOwner::NodeContent &&
        hoveredNodeId > 0 &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(hoveredNodeId);
        if (node && ResolveNodeHasDedicatedComplexEditor(editor, *node)) {
            TouchNodeFront(node->id);
            editor->SwitchToComplexNodeSubWindow(node->id);
            return;
        }
    }

    if (ownerIsContent) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_BoxSelecting = false;
        }
        return;
    }

    if (ownerIsLink && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (editor->RemoveGraphLink(hoveredLink.fromNodeId, hoveredLink.fromSocketId, hoveredLink.toNodeId, hoveredLink.toSocketId)) {
            m_StatusMessage = "Link removed.";
        }
        return;
    }

    if (ownerIsLink && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor->GetNodeGraph().SelectLink(hoveredLink.fromNodeId, hoveredLink.fromSocketId, hoveredLink.toNodeId, hoveredLink.toSocketId);
        return;
    }

    if ((m_MouseOwner == GraphMouseOwner::NodeHeader ||
         (m_MouseOwner == GraphMouseOwner::NodeFrame &&
          hoveredNodeId > 0 &&
          editor->GetNodeGraph().FindNode(hoveredNodeId) &&
          ResolveNodeHasDedicatedComplexEditor(editor, *editor->GetNodeGraph().FindNode(hoveredNodeId)))) &&
        hoveredNodeId > 0 &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(hoveredNodeId);
        if (node) {
            if (node->kind == EditorNodeGraph::NodeKind::Output) {
                return;
            }
            TouchNodeFront(node->id);
            if (ResolveNodeHasDedicatedComplexEditor(editor, *node)) {
                editor->SwitchToComplexNodeSubWindow(node->id);
                return;
            }

            const bool expanding = !node->expanded;
            node->expanded = !node->expanded;
            editor->SelectGraphNode(node->id);
            if (expanding) {
                editor->RequestGraphNodeAutoFocus(node->id, node->position, NodeGraphFootprintSize(*node), m_Pan.x, m_Pan.y, m_Zoom);
            } else {
                editor->ClearGraphAutoFocusIfTrackedNode(node->id);
            }
            return;
        }
    }

    if (m_MouseOwner == GraphMouseOwner::Canvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_BoxSelecting = true;
        m_BoxSelectStart = ScreenToGraph(ToGraphVec2(ImGui::GetMousePos()));
        m_BoxSelectCurrent = m_BoxSelectStart;
        if (!additiveSelect) {
            editor->GetNodeGraph().ClearSelection();
        }
    }

    if (m_BoxSelecting) {
        m_BoxSelectCurrent = ScreenToGraph(ToGraphVec2(ImGui::GetMousePos()));
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 a = ToImVec2(GraphToScreen(m_BoxSelectStart));
        const ImVec2 b = ToImVec2(GraphToScreen(m_BoxSelectCurrent));
        drawList->AddRectFilled(a, b, IM_COL32(120, 170, 255, 35));
        drawList->AddRect(a, b, IM_COL32(170, 210, 255, 180));
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            EditorNodeGraph::Graph& mutableGraph = editor->GetNodeGraph();
            if (!additiveSelect) {
                mutableGraph.ClearSelection();
            }
            mutableGraph.SelectNodesInRect(
                m_BoxSelectStart,
                m_BoxSelectCurrent,
                [this](const EditorNodeGraph::Node& node) {
                    return NodeGraphFootprintSize(node);
                },
                true);
            m_BoxSelecting = false;
        }
        return;
    }

    if (ownerIsNode && hoveredNodeId > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        TouchNodeFront(hoveredNodeId);
        if (!editor->GetNodeGraph().IsNodeSelected(hoveredNodeId)) {
            editor->GetNodeGraph().SelectNode(hoveredNodeId, additiveSelect);
            const EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(hoveredNodeId);
            if (node && node->kind == EditorNodeGraph::NodeKind::Layer) {
                editor->SelectLayer(node->layerIndex);
            } else {
                editor->SelectLayer(-1);
            }
        } else if (additiveSelect) {
            editor->GetNodeGraph().SelectNode(hoveredNodeId, true);
        }
        m_DragNodeId = hoveredNodeId;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_DragNodeId = -1;
    }

    if (editor->CanConsumeEditorCommandKeys() &&
        graphWindowFocused &&
        !anyPopupOpen &&
        !HasDrawerOpen() &&
        !graphHotkeysBlockedByNodeControls &&
        (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))) {
        const int direction = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ? -1 : 1;
        if (editor->SelectAdjacentMainChainNode(direction)) {
            const int selectedNodeId = editor->GetNodeGraph().GetSelectedNodeId();
            if (const EditorNodeGraph::Node* selectedNode = editor->GetNodeGraph().FindNode(selectedNodeId)) {
                editor->RequestGraphNodeAutoFocus(
                    selectedNode->id,
                    selectedNode->position,
                    NodeGraphFootprintSize(*selectedNode),
                    m_Pan.x,
                    m_Pan.y,
                    m_Zoom);
            }
            m_StatusMessage.clear();
        }
    }

    if (editor->CanConsumeEditorCommandKeys() &&
        graphWindowFocused &&
        !anyPopupOpen &&
        !HasDrawerOpen() &&
        !graphHotkeysBlockedByNodeControls &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        if (m_HoveredGroupId > 0) {
            if (editor->GetNodeGraph().RemoveGroup(m_HoveredGroupId)) {
                m_StatusMessage = "Group deleted.";
            }
            if (m_EditingGroupId == m_HoveredGroupId) m_EditingGroupId = -1;
            if (m_DragGroupId == m_HoveredGroupId) m_DragGroupId = -1;
            if (m_ResizingGroupId == m_HoveredGroupId) m_ResizingGroupId = -1;
            m_HoveredGroupId = -1;
        } else if (editor->DeleteSelectedGraphLink()) {
            m_StatusMessage = "Link deleted.";
        } else if (editor->DeleteSelectedGraphNodes()) {
            m_StatusMessage = "Node deleted.";
        }
    }

    if (editor->CanConsumeEditorCommandKeys() &&
        graphWindowFocused &&
        !anyPopupOpen &&
        !HasDrawerOpen() &&
        !graphHotkeysBlockedByNodeControls) {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            CopySelectedNodes(editor, true);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            PasteNodes(editor, true);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            DuplicateSelectedNodes(editor);
        }
        if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_D, false)) {
            const auto& selectedIds = editor->GetNodeGraph().GetSelectedNodeIds();
            if (selectedIds.size() == 1) {
                if (const auto* selectedNode = editor->GetNodeGraph().FindNode(selectedIds.front());
                    selectedNode && selectedNode->kind == EditorNodeGraph::NodeKind::Output) {
                    editor->ToggleOutputNodeEnabled(selectedNode->id);
                }
            }
        }
        if (!ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyShift && !ImGui::GetIO().KeyAlt &&
            ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            const auto& selectedIds = editor->GetNodeGraph().GetSelectedNodeIds();
            if (!selectedIds.empty()) {
                float minX = std::numeric_limits<float>::max();
                float minY = std::numeric_limits<float>::max();
                float maxX = -std::numeric_limits<float>::max();
                float maxY = -std::numeric_limits<float>::max();
                bool hasNodes = false;
                for (int nodeId : selectedIds) {
                    if (const auto* node = editor->GetNodeGraph().FindNode(nodeId)) {
                        EditorNodeGraph::Vec2 size = NodeGraphFootprintSize(*node);
                        minX = std::min(minX, node->position.x);
                        minY = std::min(minY, node->position.y);
                        maxX = std::max(maxX, node->position.x + size.x);
                        maxY = std::max(maxY, node->position.y + size.y);
                        hasNodes = true;
                    }
                }
                if (hasNodes) {
                    float padding = 45.0f;
                    float x = minX - padding;
                    float y = minY - padding;
                    float w = (maxX - minX) + padding * 2.0f;
                    float h = (maxY - minY) + padding * 2.0f;

                    editor->GetNodeGraph().AddGroup("New Group", { x, y }, { w, h });
                }
            }
        }
    }
}
