#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <string>
#include <unordered_set>

namespace {

using EditorNodeGraphUIMetrics::LinkBezierHandle;
using EditorNodeGraphUIMetrics::LinkThicknessScaleFromZoom;
using EditorNodeGraphUIMetrics::NodeUiScaleFromZoom;

using namespace Stack::Editor::NodeGraphUIVisuals;

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

ImVec2 SampleCubicBezierPoint(const ImVec2& p0, const ImVec2& p1, const ImVec2& p2, const ImVec2& p3, float t) {
    const float omt = 1.0f - t;
    const float omt2 = omt * omt;
    const float omt3 = omt2 * omt;
    const float t2 = t * t;
    const float t3 = t2 * t;
    return ImVec2(
        (omt3 * p0.x) + (3.0f * omt2 * t * p1.x) + (3.0f * omt * t2 * p2.x) + (t3 * p3.x),
        (omt3 * p0.y) + (3.0f * omt2 * t * p1.y) + (3.0f * omt * t2 * p2.y) + (t3 * p3.y));
}

ImU32 ScaleColorAlpha(ImU32 color, float alphaScale) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alphaScale, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

float LinkEdgeFadeAlpha(const ImVec2& point, const ImVec2& min, const ImVec2& max, float fadeDistance) {
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

bool RectOverlapsExpandedCanvas(
    const ImVec2& rectMin,
    const ImVec2& rectMax,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax,
    float margin) {
    return rectMax.x >= canvasMin.x - margin &&
        rectMin.x <= canvasMax.x + margin &&
        rectMax.y >= canvasMin.y - margin &&
        rectMin.y <= canvasMax.y + margin;
}

void DrawBezierLinkStrokeWithEdgeFade(
    ImDrawList* drawList,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    ImU32 color,
    float thickness,
    bool dotted,
    const ImVec2& fadeMin,
    const ImVec2& fadeMax,
    float fadeDistance) {
    if (!drawList || thickness <= 0.0f) {
        return;
    }
    if (fadeDistance <= 0.0f) {
        DrawBezierLinkStroke(drawList, p0, p1, p2, p3, color, thickness, dotted);
        return;
    }

    auto pointDistance = [](const ImVec2& a, const ImVec2& b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        return std::sqrt((dx * dx) + (dy * dy));
    };

    const float estimate =
        pointDistance(p0, p1) +
        pointDistance(p1, p2) +
        pointDistance(p2, p3);
    const int sampleCount = std::clamp(static_cast<int>(estimate / 5.0f), 32, 160);

    if (dotted) {
        const float radius = std::max(0.25f, thickness * 0.5f);
        const float spacing = std::max(1.4f, radius * 2.6f);
        ImVec2 previous = p0;
        float distanceToNextDot = 0.0f;
        for (int index = 1; index <= sampleCount; ++index) {
            const float t = static_cast<float>(index) / static_cast<float>(sampleCount);
            const ImVec2 current = SampleCubicBezierPoint(p0, p1, p2, p3, t);
            const ImVec2 delta(current.x - previous.x, current.y - previous.y);
            const float segmentLength = std::sqrt((delta.x * delta.x) + (delta.y * delta.y));
            if (segmentLength <= 1e-4f) {
                previous = current;
                continue;
            }

            while (distanceToNextDot <= segmentLength) {
                const float dotT = distanceToNextDot / segmentLength;
                const ImVec2 dotPos(
                    previous.x + (delta.x * dotT),
                    previous.y + (delta.y * dotT));
                const float alpha = LinkEdgeFadeAlpha(dotPos, fadeMin, fadeMax, fadeDistance);
                if (alpha > 0.001f) {
                    drawList->AddCircleFilled(dotPos, radius, ScaleColorAlpha(color, alpha));
                }
                distanceToNextDot += spacing;
            }

            distanceToNextDot -= segmentLength;
            previous = current;
        }
        return;
    }

    ImVec2 previous = p0;
    for (int index = 1; index <= sampleCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(sampleCount);
        const ImVec2 current = SampleCubicBezierPoint(p0, p1, p2, p3, t);
        const ImVec2 midpoint((previous.x + current.x) * 0.5f, (previous.y + current.y) * 0.5f);
        const float alpha = LinkEdgeFadeAlpha(midpoint, fadeMin, fadeMax, fadeDistance);
        if (alpha > 0.001f) {
            drawList->AddLine(previous, current, ScaleColorAlpha(color, alpha), thickness);
        }
        previous = current;
    }
}

} // namespace

void EditorNodeGraphUI::RenderLinks(const EditorNodeGraph::Graph& graph) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(m_ActiveEditor);
    const StackAppearance::AppearanceManager* appearance = m_ActiveEditor ? m_ActiveEditor->GetAppearance() : nullptr;
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const ImVec2 fadeMin(m_CanvasMin.x, m_CanvasMin.y);
    const ImVec2 fadeMax(m_CanvasMax.x, m_CanvasMax.y);
    const float edgeFadeDistance = wallpaperSurfaces ? 132.0f : 0.0f;
    const float thicknessScale = LinkThicknessScaleFromZoom(m_Zoom);
    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    const float linkCullMargin = std::clamp(
        std::min(m_CanvasMax.x - m_CanvasMin.x, m_CanvasMax.y - m_CanvasMin.y) * 0.35f,
        180.0f,
        560.0f);
    std::unordered_set<std::string> activeLinkKeys;
    activeLinkKeys.reserve(graph.GetLinks().size());
    const EditorNodeGraph::Link hoveredLink = (graphStyle.enabled && !m_MiddlePanCaptureActive && IsGraphCanvasHovered())
        ? FindLinkAt(graph, ToGraphVec2(ImGui::GetMousePos()))
        : EditorNodeGraph::Link{};
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        const EditorNodeGraph::Node* from = FindCachedNode(graph, link.fromNodeId);
        const EditorNodeGraph::Node* to = FindCachedNode(graph, link.toNodeId);
        if (!from || !to) {
            continue;
        }

        ImVec2 p1 = ToImVec2(OutputPinScreenPos(*from, link.fromSocketId));
        ImVec2 p2 = ToImVec2(InputPinScreenPos(*to, link.toSocketId));
        const bool selected = graph.GetSelectedLink() &&
            graph.GetSelectedLink()->fromNodeId == link.fromNodeId &&
            graph.GetSelectedLink()->fromSocketId == link.fromSocketId &&
            graph.GetSelectedLink()->toNodeId == link.toNodeId &&
            graph.GetSelectedLink()->toSocketId == link.toSocketId;
        const bool hovered =
            hoveredLink.fromNodeId == link.fromNodeId &&
            hoveredLink.fromSocketId == link.fromSocketId &&
            hoveredLink.toNodeId == link.toNodeId &&
            hoveredLink.toSocketId == link.toSocketId;
        LinkVisualStyle visualStyle = ResolveLinkVisualStyle(graph, link);
        if (!GraphDottedMaskLinksEnabled(m_ActiveEditor)) {
            visualStyle.dotted = false;
        }
        const std::string& channel = visualStyle.channel;
        const float laneOffset = graphStyle.enabled ? ChannelLaneOffset(channel, m_Zoom) : 0.0f;
        p1.y += laneOffset;
        p2.y += laneOffset;
        const float cullHandle = LinkBezierHandle(p1, p2);
        const ImVec2 linkMin(
            std::min({ p1.x, p2.x, p1.x + cullHandle, p2.x - cullHandle }),
            std::min(p1.y, p2.y));
        const ImVec2 linkMax(
            std::max({ p1.x, p2.x, p1.x + cullHandle, p2.x - cullHandle }),
            std::max(p1.y, p2.y));
        if (!RectOverlapsExpandedCanvas(linkMin, linkMax, fadeMin, fadeMax, linkCullMargin) && !selected && !hovered) {
            continue;
        }
        const std::string animationKey = LinkAnimationKey(link);
        activeLinkKeys.insert(animationKey);
        const float emphasis = UpdateAnimatedState(
            m_LinkEmphasisAnim,
            animationKey,
            selected ? 1.0f : (hovered ? 0.62f : 0.0f),
            deltaTime,
            18.0f,
            11.0f);

        if (graphStyle.enabled) {
            const float handle = LinkBezierHandle(p1, p2);
            const ImVec2 c1(p1.x + handle, p1.y);
            const ImVec2 c2(p2.x - handle, p2.y);
            ImVec4 linkColor = LinkColorVec(visualStyle, graphStyle);
            if (emphasis > 0.001f) {
                const ImVec4 emphasisColor = selected ? graphStyle.text : graphStyle.spotlightHalo;
                linkColor = WithAlpha(
                    BlendColor(linkColor, emphasisColor, selected ? (0.18f + emphasis * 0.12f) : (0.08f + emphasis * 0.16f)),
                    std::clamp(linkColor.w + emphasis * 0.10f, 0.0f, 1.0f));
            }

            DrawBezierLinkStrokeWithEdgeFade(
                drawList,
                p1,
                c1,
                c2,
                p2,
                ColorWithAlpha(graphStyle.linkUnderlay, 0.48f + emphasis * (selected ? 0.34f : 0.22f)),
                (4.6f + emphasis * (selected ? 2.8f : 1.6f)) * thicknessScale,
                visualStyle.dotted,
                fadeMin,
                fadeMax,
                edgeFadeDistance);
            if (emphasis > 0.01f) {
                DrawBezierLinkStrokeWithEdgeFade(
                    drawList,
                    p1,
                    c1,
                    c2,
                    p2,
                    ColorWithAlpha(selected ? graphStyle.selected : graphStyle.spotlightHalo, selected ? (0.34f + emphasis * 0.30f) : (0.14f + emphasis * 0.22f)),
                    (3.2f + emphasis * (selected ? 2.0f : 1.45f)) * thicknessScale,
                    visualStyle.dotted,
                    fadeMin,
                    fadeMax,
                    edgeFadeDistance);
            }
            DrawBezierLinkStrokeWithEdgeFade(
                drawList,
                p1,
                c1,
                c2,
                p2,
                ColorToU32(linkColor),
                (2.35f + emphasis * (selected ? 1.15f : 0.80f)) * thicknessScale,
                visualStyle.dotted,
                fadeMin,
                fadeMax,
                edgeFadeDistance);
            if (selected) {
                DrawBezierLinkStrokeWithEdgeFade(
                    drawList,
                    p1,
                    c1,
                    c2,
                    p2,
                    ColorWithAlpha(graphStyle.text, 0.44f),
                    std::max(1.0f, 1.05f * thicknessScale),
                    visualStyle.dotted,
                    fadeMin,
                    fadeMax,
                    edgeFadeDistance);
            }
            continue;
        }

        const float handle = LinkBezierHandle(p1, p2);
        DrawBezierLinkStrokeWithEdgeFade(
            drawList,
            p1,
            ImVec2(p1.x + handle, p1.y),
            ImVec2(p2.x - handle, p2.y),
            p2,
            LinkColorClassic(visualStyle, selected),
            (3.0f + emphasis * (selected ? 1.5f : 0.9f)) * thicknessScale,
            visualStyle.dotted,
            fadeMin,
            fadeMax,
            edgeFadeDistance);
    }
    PruneAnimatedState(m_LinkEmphasisAnim, activeLinkKeys);
}

void EditorNodeGraphUI::RenderPendingOutputLinkDrag(
    EditorModule* editor,
    const EditorNodeGraph::Graph& graph,
    const SocketHit& hoveredInput) {
    const EditorNodeGraph::Node* from = graph.FindNode(m_DragOutputNodeId);
    if (!from) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const ImVec2 fadeMin(m_CanvasMin.x, m_CanvasMin.y);
    const ImVec2 fadeMax(m_CanvasMax.x, m_CanvasMax.y);
    const float edgeFadeDistance = wallpaperSurfaces ? 132.0f : 0.0f;
    const bool hoveredInputConnectable = hoveredInput.IsValid() &&
        graph.CanConnectSocketsOrInsertExtractor(m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId);
    const float dragPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.5f);
    LinkVisualStyle visualStyle = hoveredInputConnectable
        ? ResolvePendingLinkVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId)
        : ResolvePendingLinkVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, EditorNodeGraph::SocketDirection::Output);
    if (!GraphDottedMaskLinksEnabled(editor)) {
        visualStyle.dotted = false;
    }
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const ImVec2 sourcePin = ToImVec2(OutputPinScreenPos(*from, m_DragOutputSocketId));
    ImVec2 p1 = sourcePin;
    ImVec2 p2 = ImGui::GetMousePos();
    const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
    p1.y += laneOffset;
    p2.y += laneOffset;
    const float handle = LinkBezierHandle(p1, p2);
    const ImU32 dragColor = graphStyle.enabled
        ? ColorToU32(LinkColorVec(visualStyle, graphStyle))
        : LinkColorClassic(visualStyle, false);
    DrawBezierLinkStrokeWithEdgeFade(
        drawList,
        p1,
        ImVec2(p1.x + handle, p1.y),
        ImVec2(p2.x - handle, p2.y),
        p2,
        dragColor,
        std::max(0.35f, (hoveredInputConnectable ? (2.8f + dragPulse * 0.45f) : 2.5f) * NodeUiScaleFromZoom(m_Zoom)),
        visualStyle.dotted,
        fadeMin,
        fadeMax,
        edgeFadeDistance);
    drawList->AddCircleFilled(
        sourcePin,
        std::max(2.0f, NodePinRadius() * (1.15f + dragPulse * 0.10f)),
        graphStyle.enabled
            ? ColorWithAlpha(graphStyle.selected, 0.32f + dragPulse * 0.12f)
            : ApplyStyleAlpha(IM_COL32(220, 232, 242, 165)),
        18);
    if (hoveredInputConnectable) {
        const EditorNodeGraph::Node* hoveredInputNode = graph.FindNode(hoveredInput.nodeId);
        const ImVec2 targetPin = hoveredInputNode
            ? ToImVec2(InputPinScreenPos(*hoveredInputNode, hoveredInput.socketId))
            : ImGui::GetMousePos();
        drawList->AddCircle(
            targetPin,
            std::max(4.0f, NodePinRadius() * (1.55f + dragPulse * 0.24f)),
            graphStyle.enabled
                ? ColorWithAlpha(graphStyle.selected, 0.46f + dragPulse * 0.18f)
                : ApplyStyleAlpha(IM_COL32(220, 232, 242, 220)),
            22,
            std::max(0.9f, 1.2f * NodeUiScaleFromZoom(m_Zoom)));
    }
}

void EditorNodeGraphUI::RenderPendingInputLinkDrag(
    EditorModule* editor,
    const EditorNodeGraph::Graph& graph,
    const SocketHit& hoveredOutput) {
    const EditorNodeGraph::Node* to = graph.FindNode(m_DragInputNodeId);
    if (!to) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const ImVec2 fadeMin(m_CanvasMin.x, m_CanvasMin.y);
    const ImVec2 fadeMax(m_CanvasMax.x, m_CanvasMax.y);
    const float edgeFadeDistance = wallpaperSurfaces ? 132.0f : 0.0f;
    const bool hoveredOutputConnectable = hoveredOutput.IsValid() &&
        graph.CanConnectSocketsOrInsertExtractor(hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId);
    const float dragPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.5f);
    LinkVisualStyle visualStyle = hoveredOutputConnectable
        ? ResolvePendingLinkVisualStyle(graph, hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId)
        : ResolvePendingLinkVisualStyle(graph, m_DragInputNodeId, m_DragInputSocketId, EditorNodeGraph::SocketDirection::Input);
    if (!GraphDottedMaskLinksEnabled(editor)) {
        visualStyle.dotted = false;
    }
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    ImVec2 p1 = ImGui::GetMousePos();
    const ImVec2 targetPin = ToImVec2(InputPinScreenPos(*to, m_DragInputSocketId));
    ImVec2 p2 = targetPin;
    const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
    p1.y += laneOffset;
    p2.y += laneOffset;
    const float handle = LinkBezierHandle(p1, p2);
    const ImU32 dragColor = graphStyle.enabled
        ? ColorToU32(LinkColorVec(visualStyle, graphStyle))
        : LinkColorClassic(visualStyle, false);
    DrawBezierLinkStrokeWithEdgeFade(
        drawList,
        p1,
        ImVec2(p1.x + handle, p1.y),
        ImVec2(p2.x - handle, p2.y),
        p2,
        dragColor,
        std::max(0.35f, (hoveredOutputConnectable ? (2.8f + dragPulse * 0.45f) : 2.5f) * NodeUiScaleFromZoom(m_Zoom)),
        visualStyle.dotted,
        fadeMin,
        fadeMax,
        edgeFadeDistance);
    drawList->AddCircleFilled(
        targetPin,
        std::max(2.0f, NodePinRadius() * (1.15f + dragPulse * 0.10f)),
        graphStyle.enabled
            ? ColorWithAlpha(graphStyle.selected, 0.32f + dragPulse * 0.12f)
            : ApplyStyleAlpha(IM_COL32(220, 232, 242, 165)),
        18);
    if (hoveredOutputConnectable) {
        const EditorNodeGraph::Node* hoveredOutputNode = graph.FindNode(hoveredOutput.nodeId);
        const ImVec2 sourcePin = hoveredOutputNode
            ? ToImVec2(OutputPinScreenPos(*hoveredOutputNode, hoveredOutput.socketId))
            : ImGui::GetMousePos();
        drawList->AddCircle(
            sourcePin,
            std::max(4.0f, NodePinRadius() * (1.55f + dragPulse * 0.24f)),
            graphStyle.enabled
                ? ColorWithAlpha(graphStyle.selected, 0.46f + dragPulse * 0.18f)
                : ApplyStyleAlpha(IM_COL32(220, 232, 242, 220)),
            22,
            std::max(0.9f, 1.2f * NodeUiScaleFromZoom(m_Zoom)));
    }
}

void EditorNodeGraphUI::RenderGroups(EditorModule* editor, EditorNodeGraph::Graph& graph) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    std::unordered_set<int> activeGroupIds;
    activeGroupIds.reserve(graph.GetGroups().size());
    auto& groups = graph.GetGroups();
    const ImVec2 canvasMin(m_CanvasMin.x, m_CanvasMin.y);
    const ImVec2 canvasMax(m_CanvasMax.x, m_CanvasMax.y);
    const float groupCullMargin = std::clamp(
        std::min(m_CanvasMax.x - m_CanvasMin.x, m_CanvasMax.y - m_CanvasMin.y) * 0.35f,
        180.0f,
        560.0f);

    for (auto& group : groups) {
        ImVec2 minPos = ToImVec2(GraphToScreen(group.position));
        ImVec2 maxPos = ToImVec2(GraphToScreen({ group.position.x + group.size.x, group.position.y + group.size.y }));

        float rounding = 8.0f * m_Zoom;

        bool isHovered = (m_HoveredGroupId == group.id);
        bool isEditing = (m_EditingGroupId == group.id);
        bool isDragged = (m_DragGroupId == group.id);
        bool isResized = (m_ResizingGroupId == group.id);
        if (!isHovered &&
            !isEditing &&
            !isDragged &&
            !isResized &&
            !RectOverlapsExpandedCanvas(minPos, maxPos, canvasMin, canvasMax, groupCullMargin)) {
            continue;
        }
        activeGroupIds.insert(group.id);
        const float emphasis = UpdateAnimatedState(
            m_GroupEmphasisAnim,
            group.id,
            (isDragged || isResized) ? 1.0f : (isHovered ? 0.65f : 0.0f),
            deltaTime,
            16.0f,
            10.0f);

        ImU32 bgColor;
        ImU32 borderColor;
        ImU32 headerColor;

        if (graphStyle.enabled) {
            const float activeBoost = 0.10f * emphasis + ((isDragged || isResized) ? 0.18f : 0.0f);
            bgColor = ColorWithAlpha(graphStyle.groupFill, graphStyle.groupFill.w + activeBoost);
            borderColor = ColorWithAlpha(
                isDragged || isResized ? graphStyle.selected : BlendColor(graphStyle.groupBorder, graphStyle.spotlightHalo, emphasis * 0.28f),
                (isDragged || isResized) ? 0.92f : (graphStyle.groupBorder.w + emphasis * 0.26f));
            headerColor = ColorWithAlpha(graphStyle.groupHeader, graphStyle.groupHeader.w + activeBoost);
        } else if (isDragged || isResized) {
            bgColor = ApplyStyleAlpha(IM_COL32(24, 30, 48, 140));
            borderColor = ApplyStyleAlpha(IM_COL32(90, 160, 255, 230));
            headerColor = ApplyStyleAlpha(IM_COL32(42, 54, 80, 240));
        } else {
            bgColor = ApplyStyleAlpha(IM_COL32(18, 22, 33, 90));
            borderColor = ApplyStyleAlpha(IM_COL32(66, 120, 180, 120));
            headerColor = ApplyStyleAlpha(IM_COL32(32, 40, 56, 190));
        }

        if (graphStyle.enabled && emphasis > 0.02f) {
            DrawSoftSpotlightHalo(
                drawList,
                minPos,
                maxPos,
                ColorWithAlpha(isDragged || isResized ? graphStyle.selected : graphStyle.spotlightHalo, 0.08f + emphasis * 0.16f),
                std::max(10.0f, 12.0f * m_Zoom + emphasis * 6.0f),
                std::max(0.9f, (0.95f + emphasis * 0.70f) * m_Zoom),
                2.8f);
        }

        drawList->AddRectFilled(minPos, maxPos, bgColor, rounding);

        ImVec2 headerMin = minPos;
        ImVec2 headerMax = ImVec2(maxPos.x, minPos.y + 28.0f * m_Zoom);
        drawList->AddRectFilled(headerMin, headerMax, headerColor, rounding, ImDrawFlags_RoundCornersTop);

        drawList->AddRect(minPos, maxPos, borderColor, rounding, 0, std::max(1.0f, 2.0f * m_Zoom));

        if (isHovered || isResized || emphasis > 0.20f) {
            drawList->AddTriangleFilled(
                ImVec2(maxPos.x - 4.0f * m_Zoom, maxPos.y - 12.0f * m_Zoom),
                ImVec2(maxPos.x - 12.0f * m_Zoom, maxPos.y - 4.0f * m_Zoom),
                ImVec2(maxPos.x - 4.0f * m_Zoom, maxPos.y - 4.0f * m_Zoom),
                borderColor);
        }

        if (isEditing) {
            ImGui::PushID(group.id);

            float inputWidth = (group.size.x - 16.0f) * m_Zoom;

            ImGui::SetCursorScreenPos(ImVec2(minPos.x + 8.0f * m_Zoom, minPos.y + 4.0f * m_Zoom));
            ImGui::SetNextItemWidth(inputWidth);

            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsAnyItemActive()) {
                ImGui::SetKeyboardFocusHere();
            }

            if (ImGui::InputText("##rename", m_GroupRenameBuffer, sizeof(m_GroupRenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                group.title = m_GroupRenameBuffer;
                m_EditingGroupId = -1;
            }

            if (ImGui::IsItemDeactivated()) {
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    group.title = m_GroupRenameBuffer;
                }
                m_EditingGroupId = -1;
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            ImGui::PopID();
        } else {
            ImVec2 textPos = ImVec2(minPos.x + 8.0f * m_Zoom, minPos.y + 6.0f * m_Zoom);
            float titleFontSize = ImGui::GetFontSize() * m_Zoom;
            drawList->AddText(
                ImGui::GetFont(),
                titleFontSize,
                textPos,
                graphStyle.enabled ? ColorWithAlpha(graphStyle.text, 0.86f) : ApplyStyleAlpha(IM_COL32(255, 255, 255, 220)),
                group.title.c_str());
        }
    }
    PruneAnimatedState(m_GroupEmphasisAnim, activeGroupIds);
}
