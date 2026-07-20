#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/GraphConnectionPresentation.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"
#include "Editor/NodeGraph/SocketPresentation.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include <algorithm>
#include <cfloat>
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

void PrepareAnchoredDetailCard(const ImVec2& anchor) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workMin = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 workMax = viewport
        ? ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y)
        : ImGui::GetIO().DisplaySize;
    constexpr float kCardWidth = 340.0f;
    constexpr float kEstimatedHeight = 260.0f;
    constexpr float kMargin = 12.0f;
    const bool placeRight = anchor.x + kMargin + kCardWidth <= workMax.x - 8.0f;
    const bool placeBelow = anchor.y + kMargin + kEstimatedHeight <= workMax.y - 8.0f;
    const ImVec2 position(
        std::clamp(anchor.x + (placeRight ? kMargin : -kMargin), workMin.x + 8.0f, workMax.x - 8.0f),
        std::clamp(anchor.y + (placeBelow ? kMargin : -kMargin), workMin.y + 8.0f, workMax.y - 8.0f));
    const ImVec2 pivot(placeRight ? 0.0f : 1.0f, placeBelow ? 0.0f : 1.0f);
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(380.0f, FLT_MAX));
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float ResolveLinkHandle(const ImVec2& p1, const ImVec2& p2, bool straightLinks) {
    return straightLinks ? 0.0f : LinkBezierHandle(p1, p2);
}

void DrawStraightLinkStroke(
    ImDrawList* drawList,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness,
    bool dotted) {
    if (!drawList) {
        return;
    }

    if (dotted) {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt((dx * dx) + (dy * dy));
        if (length <= 1e-4f) {
            return;
        }

        const float radius = std::max(0.25f, thickness * 0.5f);
        const float spacing = std::max(1.4f, radius * 2.6f);
        for (float distance = 0.0f; distance <= length; distance += spacing) {
            const float t = distance / length;
            const ImVec2 dotPos(
                start.x + dx * t,
                start.y + dy * t);
            drawList->AddCircleFilled(dotPos, radius, color);
        }
        return;
    }

    drawList->AddLine(start, end, color, thickness);
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

struct LinkTextContent {
    std::string primary;
    std::string secondary;
};

LinkTextContent BuildLinkTextContent(
    EditorModule* editor,
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Link& link) {
    using namespace EditorNodeGraph::SocketPresentation;
    EditorNodeGraph::SocketDefinition sourceSocket;
    if (!graph.FindSocket(link.fromNodeId, link.fromSocketId, &sourceSocket)) {
        sourceSocket.id = link.fromSocketId;
        sourceSocket.nodeId = link.fromNodeId;
        sourceSocket.direction = EditorNodeGraph::SocketDirection::Output;
        sourceSocket.label = "Unknown";
        NormalizeSocketDefinition(EditorNodeGraph::NodeKind::Output, sourceSocket);
    }

    Stack::NodeMath::ValueDescriptor descriptor;
    const bool hasDescriptor = editor &&
        editor->TryGetGraphLinkSemanticDescriptor(link, descriptor);
    if (hasDescriptor) {
        if (descriptor.logicalType != Stack::NodeMath::LogicalValueType::Invalid) {
            sourceSocket.logicalType = descriptor.logicalType;
        }
        if (descriptor.channels.state != Stack::NodeMath::KnowledgeState::NotApplicable) {
            sourceSocket.declaredChannels = descriptor.channels;
        }
        if (descriptor.units.state != Stack::NodeMath::KnowledgeState::NotApplicable) {
            sourceSocket.declaredUnits = descriptor.units;
        }
    }

    LinkTextContent content;
    content.primary = PrimaryDescription(sourceSocket);
    const bool imageLike = Stack::NodeMath::IsImageLike(sourceSocket.logicalType) ||
        sourceSocket.logicalType == Stack::NodeMath::LogicalValueType::Mask ||
        sourceSocket.logicalType == Stack::NodeMath::LogicalValueType::ComplexSpectrum;
    if (hasDescriptor && imageLike) {
        content.secondary = ImageStateDescription(descriptor);
    } else {
        const std::string units = UnitName(sourceSocket.declaredUnits);
        content.secondary = units.empty()
            ? StorageName(sourceSocket.logicalType)
            : units + " · " + StorageName(sourceSocket.logicalType);
    }
    if (content.primary.empty()) content.primary = "Unknown";
    if (content.secondary.empty()) content.secondary = "Unknown state";
    return content;
}

float ApproximateLinkLength(
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    bool straight) {
    if (straight) {
        const float dx = p3.x - p0.x;
        const float dy = p3.y - p0.y;
        return std::sqrt(dx * dx + dy * dy);
    }
    float length = 0.0f;
    ImVec2 previous = p0;
    for (int index = 1; index <= 32; ++index) {
        const ImVec2 current = SampleCubicBezierPoint(
            p0, p1, p2, p3, static_cast<float>(index) / 32.0f);
        const float dx = current.x - previous.x;
        const float dy = current.y - previous.y;
        length += std::sqrt(dx * dx + dy * dy);
        previous = current;
    }
    return length;
}

void DrawRotatedText(
    ImDrawList* drawList,
    ImFont* font,
    float fontSize,
    const ImVec2& center,
    const ImVec2& measuredSize,
    float angle,
    ImU32 color,
    const char* text) {
    if (!drawList || !font || !text || text[0] == '\0') return;
    const int vertexStart = drawList->VtxBuffer.Size;
    drawList->AddText(
        font,
        fontSize,
        ImVec2(center.x - measuredSize.x * 0.5f, center.y - measuredSize.y * 0.5f),
        color,
        text);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    for (int index = vertexStart; index < drawList->VtxBuffer.Size; ++index) {
        ImVec2 delta(
            drawList->VtxBuffer[index].pos.x - center.x,
            drawList->VtxBuffer[index].pos.y - center.y);
        drawList->VtxBuffer[index].pos = ImVec2(
            center.x + delta.x * c - delta.y * s,
            center.y + delta.x * s + delta.y * c);
    }
}

void DrawSemanticLinkInspection(
    EditorModule* editor,
    const EditorNodeGraph::Link& link,
    const ImVec2& anchor) {
    if (!editor) return;
    Stack::NodeMath::ValueDescriptor descriptor;
    if (!editor->TryGetGraphLinkSemanticDescriptor(link, descriptor)) return;
    PrepareAnchoredDetailCard(anchor);
    ImGui::BeginTooltip();
    ImGui::TextUnformatted("Wire state");
    ImGui::Separator();
    ImGui::TextWrapped("%s", Stack::NodeMath::CompactDescriptorLabel(descriptor).c_str());
    if (descriptor.spatial.state == Stack::NodeMath::KnowledgeState::Known) {
        const auto& rect = descriptor.spatial.value.dataWindow;
        ImGui::Text("Extent: %lld x %lld", static_cast<long long>(rect.width), static_cast<long long>(rect.height));
    } else {
        ImGui::TextDisabled("Extent: Unknown");
    }
    if (descriptor.range.state == Stack::NodeMath::KnowledgeState::Known) {
        ImGui::Text(
            "Nominal range: %.4g to %.4g%s%s",
            descriptor.range.value.nominalMinimum,
            descriptor.range.value.nominalMaximum,
            descriptor.range.value.allowsBelowNominal ? " | below allowed" : "",
            descriptor.range.value.allowsAboveNominal ? " | above allowed" : "");
    } else {
        ImGui::TextDisabled("Range: Unknown");
    }
    if (descriptor.precision.state == Stack::NodeMath::KnowledgeState::Known) {
        ImGui::TextDisabled("Precision: %s",
            EditorNodeGraph::SocketPresentation::PrecisionName(
                descriptor.precision.value));
    } else {
        ImGui::TextDisabled("Precision: Unknown");
    }
    if (descriptor.provenance.state == Stack::NodeMath::KnowledgeState::Known) {
        const std::string provenance = descriptor.provenance.value.operationIdentity.empty()
            ? descriptor.provenance.value.sourceIdentity
            : descriptor.provenance.value.operationIdentity;
        if (!provenance.empty()) ImGui::TextWrapped("From: %s", provenance.c_str());
    }
    const std::string affected = "node-" + std::to_string(link.toNodeId);
    for (const Stack::NodeMath::Diagnostic& diagnostic : editor->GetGraphSemanticDiagnostics()) {
        if (diagnostic.affectedIdentity != affected) continue;
        const ImVec4 color = diagnostic.severity == Stack::NodeMath::DiagnosticSeverity::Information
            ? ImVec4(0.62f, 0.76f, 0.88f, 1.0f)
            : ImVec4(0.94f, 0.73f, 0.36f, 1.0f);
        ImGui::TextColored(color, "%s", diagnostic.message.c_str());
    }
    ImGui::EndTooltip();
}

void DrawLinkStrokeWithEdgeFade(
    ImDrawList* drawList,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    ImU32 color,
    float thickness,
    bool dotted,
    bool straight,
    const ImVec2& fadeMin,
    const ImVec2& fadeMax,
    float fadeDistance,
    float gapStartT = -1.0f,
    float gapEndT = -1.0f) {
    if (!drawList || thickness <= 0.0f) {
        return;
    }
    const bool hasGap = gapStartT >= 0.0f && gapEndT > gapStartT;
    const auto insideGap = [&](float t) {
        return hasGap && t >= gapStartT && t <= gapEndT;
    };
    if (straight) {
        if (fadeDistance <= 0.0f && !hasGap) {
            DrawStraightLinkStroke(drawList, p0, p3, color, thickness, dotted);
            return;
        }

        const float dx = p3.x - p0.x;
        const float dy = p3.y - p0.y;
        const float length = std::sqrt((dx * dx) + (dy * dy));
        const int sampleCount = std::clamp(static_cast<int>(length / 5.0f), 12, 180);

        if (dotted) {
            const float radius = std::max(0.25f, thickness * 0.5f);
            const float spacing = std::max(1.4f, radius * 2.6f);
            for (float distance = 0.0f; distance <= length; distance += spacing) {
                const float t = length > 1e-4f ? (distance / length) : 0.0f;
                if (insideGap(t)) continue;
                const ImVec2 dotPos(
                    p0.x + dx * t,
                    p0.y + dy * t);
                const float alpha = LinkEdgeFadeAlpha(dotPos, fadeMin, fadeMax, fadeDistance);
                if (alpha > 0.001f) {
                    drawList->AddCircleFilled(dotPos, radius, ScaleColorAlpha(color, alpha));
                }
            }
            return;
        }

        ImVec2 previous = p0;
        for (int index = 1; index <= sampleCount; ++index) {
            const float t = static_cast<float>(index) / static_cast<float>(sampleCount);
            const ImVec2 current(
                p0.x + dx * t,
                p0.y + dy * t);
            const ImVec2 midpoint((previous.x + current.x) * 0.5f, (previous.y + current.y) * 0.5f);
            const float midpointT = (static_cast<float>(index) - 0.5f) /
                static_cast<float>(sampleCount);
            const float alpha = LinkEdgeFadeAlpha(midpoint, fadeMin, fadeMax, fadeDistance);
            if (!insideGap(midpointT) && alpha > 0.001f) {
                drawList->AddLine(previous, current, ScaleColorAlpha(color, alpha), thickness);
            }
            previous = current;
        }
        return;
    }

    if (fadeDistance <= 0.0f && !hasGap) {
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
                const float curveT = (static_cast<float>(index - 1) + dotT) /
                    static_cast<float>(sampleCount);
                const ImVec2 dotPos(
                    previous.x + (delta.x * dotT),
                    previous.y + (delta.y * dotT));
                const float alpha = LinkEdgeFadeAlpha(dotPos, fadeMin, fadeMax, fadeDistance);
                if (!insideGap(curveT) && alpha > 0.001f) {
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
        const float midpointT = (static_cast<float>(index) - 0.5f) /
            static_cast<float>(sampleCount);
        const float alpha = LinkEdgeFadeAlpha(midpoint, fadeMin, fadeMax, fadeDistance);
        if (!insideGap(midpointT) && alpha > 0.001f) {
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
    const bool straightLinks = GraphStraightLinksEnabled(m_ActiveEditor);
    const float linkCullMargin = std::clamp(
        std::min(m_CanvasMax.x - m_CanvasMin.x, m_CanvasMax.y - m_CanvasMin.y) * 0.35f,
        180.0f,
        560.0f);
    std::unordered_set<std::string> activeLinkKeys;
    activeLinkKeys.reserve(graph.GetLinks().size());
    std::vector<CachedRect> occupiedLabelRects;
    const EditorNodeGraph::Link hoveredLink = (!m_MiddlePanCaptureActive && IsGraphCanvasHovered())
        ? FindLinkAt(graph, ToGraphVec2(ImGui::GetMousePos()))
        : EditorNodeGraph::Link{};
    m_LinkLabelHitRects.clear();
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
        bool hovered =
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
        const float cullHandle = ResolveLinkHandle(p1, p2, straightLinks);
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
        const float labelHandle = ResolveLinkHandle(p1, p2, straightLinks);
        const ImVec2 labelC1(p1.x + labelHandle, p1.y);
        const ImVec2 labelC2(p2.x - labelHandle, p2.y);
        const float screenLength = ApproximateLinkLength(
            p1, labelC1, labelC2, p2, straightLinks);
        const bool endpointSelected = graph.IsNodeSelected(link.fromNodeId) ||
            graph.IsNodeSelected(link.toNodeId);
        const bool interactionReveal = selected || hovered || endpointSelected;
        const StackAppearance::GraphConnectionLabelVisibility labelVisibility = appearance
            ? appearance->GetGraphConnectionLabels()
            : StackAppearance::GraphConnectionLabelVisibility::Adaptive;
        const StackAppearance::GraphConnectionTextLayout textLayout = appearance
            ? appearance->GetGraphConnectionTextLayout()
            : StackAppearance::GraphConnectionTextLayout::Floating;
        const StackAppearance::GraphConnectionTextSizing textSizing = appearance
            ? appearance->GetGraphConnectionTextSizing()
            : StackAppearance::GraphConnectionTextSizing::ZoomAware;
        const float textBaseSize = appearance
            ? appearance->GetGraphConnectionTextSize()
            : StackAppearance::kGraphConnectionTextSizeDefault;
        const bool textOutline = appearance && appearance->GetGraphConnectionTextOutline();
        int labelLineCount = EditorNodeGraph::ConnectionPresentation::VisibleLineCount(
            labelVisibility, m_Zoom, screenLength, interactionReveal);

        LinkTextContent linkText;
        const float labelFontSize = EditorNodeGraph::ConnectionPresentation::ConnectionTextSize(
            textBaseSize, m_Zoom, textSizing);
        float labelFontScale = labelFontSize / std::max(1.0f, ImGui::GetFontSize());
        ImVec2 primarySize {};
        ImVec2 secondarySize {};
        ImVec2 labelCenter = straightLinks
            ? ImVec2((p1.x + p2.x) * 0.5f, (p1.y + p2.y) * 0.5f)
            : SampleCubicBezierPoint(p1, labelC1, labelC2, p2, 0.5f);
        ImVec2 primaryCenter {};
        ImVec2 secondaryCenter {};
        ImVec2 labelTangent(1.0f, 0.0f);
        ImVec2 labelNormal(0.0f, 1.0f);
        float labelAngle = 0.0f;
        float labelT = 0.5f;
        float labelNormalOffset = 0.0f;
        CachedRect combinedLabelRect {};
        bool drawLabels = false;
        if (labelLineCount > 0) {
            linkText = BuildLinkTextContent(m_ActiveEditor, graph, link);
            const ImVec2 rawPrimarySize = ImGui::CalcTextSize(linkText.primary.c_str());
            const ImVec2 rawSecondarySize = ImGui::CalcTextSize(linkText.secondary.c_str());
            primarySize = ImVec2(rawPrimarySize.x * labelFontScale, rawPrimarySize.y * labelFontScale);
            secondarySize = ImVec2(rawSecondarySize.x * labelFontScale, rawSecondarySize.y * labelFontScale);

            auto overlaps = [](const CachedRect& left, const CachedRect& right, float padding) {
                return left.max.x + padding >= right.min.x &&
                    left.min.x - padding <= right.max.x &&
                    left.max.y + padding >= right.min.y &&
                    left.min.y - padding <= right.max.y;
            };
            auto candidateRect = [&](
                float t,
                float normalOffset,
                int lines,
                ImVec2* centerOut,
                ImVec2* primaryCenterOut,
                ImVec2* secondaryCenterOut,
                ImVec2* tangentOut,
                ImVec2* normalOut,
                float* angleOut) {
                ImVec2 center = straightLinks
                    ? ImVec2(p1.x + (p2.x - p1.x) * t, p1.y + (p2.y - p1.y) * t)
                    : SampleCubicBezierPoint(p1, labelC1, labelC2, p2, t);
                const ImVec2 tangent = straightLinks
                    ? EditorNodeGraph::ConnectionPresentation::NormalizedTangent(
                        ImVec2(p2.x - p1.x, p2.y - p1.y))
                    : EditorNodeGraph::ConnectionPresentation::CubicBezierTangent(
                        p1, labelC1, labelC2, p2, t);
                const ImVec2 normal(-tangent.y, tangent.x);
                center.x += normal.x * normalOffset;
                center.y += normal.y * normalOffset;
                const float angle = EditorNodeGraph::ConnectionPresentation::NormalizeUprightAngle(
                    std::atan2(tangent.y, tangent.x));
                const ImVec2 primaryLineCenter(
                    center.x - normal.x * (primarySize.y * 0.5f + 4.0f),
                    center.y - normal.y * (primarySize.y * 0.5f + 4.0f));
                const ImVec2 secondaryLineCenter(
                    center.x + normal.x * (secondarySize.y * 0.5f + 4.0f),
                    center.y + normal.y * (secondarySize.y * 0.5f + 4.0f));
                const auto primaryBounds = EditorNodeGraph::ConnectionPresentation::BoundsForRotatedRect(
                    primaryLineCenter, primarySize, angle);
                ImVec2 minimum = primaryBounds.minimum;
                ImVec2 maximum = primaryBounds.maximum;
                if (lines > 1) {
                    const auto secondaryBounds = EditorNodeGraph::ConnectionPresentation::BoundsForRotatedRect(
                        secondaryLineCenter, secondarySize, angle);
                    minimum.x = std::min(minimum.x, secondaryBounds.minimum.x);
                    minimum.y = std::min(minimum.y, secondaryBounds.minimum.y);
                    maximum.x = std::max(maximum.x, secondaryBounds.maximum.x);
                    maximum.y = std::max(maximum.y, secondaryBounds.maximum.y);
                }
                if (centerOut) *centerOut = center;
                if (primaryCenterOut) *primaryCenterOut = primaryLineCenter;
                if (secondaryCenterOut) *secondaryCenterOut = secondaryLineCenter;
                if (tangentOut) *tangentOut = tangent;
                if (normalOut) *normalOut = normal;
                if (angleOut) *angleOut = angle;
                return CachedRect{ minimum, maximum };
            };
            auto collides = [&](const CachedRect& rect) {
                for (const CachedRect& occupied : occupiedLabelRects) {
                    if (overlaps(rect, occupied, 4.0f)) return true;
                }
                for (const auto& nodeLayout : m_NodeLayoutCache) {
                    if (overlaps(rect, nodeLayout.second.frameRect, 3.0f)) return true;
                }
                return false;
            };

            const struct Candidate { float t; float y; } candidates[] = {
                { 0.50f, 0.0f }, { 0.42f, 0.0f }, { 0.58f, 0.0f },
                { 0.50f, -18.0f }, { 0.50f, 18.0f }
            };
            for (int requestedLines = labelLineCount; requestedLines >= 1 && !drawLabels; --requestedLines) {
                for (const Candidate& candidate : candidates) {
                    ImVec2 candidateCenter;
                    ImVec2 candidatePrimaryCenter;
                    ImVec2 candidateSecondaryCenter;
                    ImVec2 candidateTangent;
                    ImVec2 candidateNormal;
                    float candidateAngle = 0.0f;
                    const CachedRect rect = candidateRect(
                        candidate.t,
                        candidate.y,
                        requestedLines,
                        &candidateCenter,
                        &candidatePrimaryCenter,
                        &candidateSecondaryCenter,
                        &candidateTangent,
                        &candidateNormal,
                        &candidateAngle);
                    if (interactionReveal || !collides(rect)) {
                        labelLineCount = requestedLines;
                        labelCenter = candidateCenter;
                        primaryCenter = candidatePrimaryCenter;
                        secondaryCenter = candidateSecondaryCenter;
                        labelTangent = candidateTangent;
                        labelNormal = candidateNormal;
                        labelAngle = candidateAngle;
                        labelT = candidate.t;
                        labelNormalOffset = candidate.y;
                        combinedLabelRect = rect;
                        drawLabels = true;
                        break;
                    }
                }
            }
            if (drawLabels) {
                occupiedLabelRects.push_back(combinedLabelRect);
                m_LinkLabelHitRects[animationKey] = combinedLabelRect;
                const ImVec2 mouse = ImGui::GetMousePos();
                hovered = hovered || combinedLabelRect.Contains(mouse);
            }
        }

        float gapStartT = -1.0f;
        float gapEndT = -1.0f;
        if (drawLabels &&
            textLayout == StackAppearance::GraphConnectionTextLayout::BreakLine &&
            std::abs(labelNormalOffset) < 0.01f) {
            const float textWidth = std::max(primarySize.x,
                labelLineCount > 1 ? secondarySize.x : 0.0f);
            EditorNodeGraph::ConnectionPresentation::BreakLineGap(
                screenLength, textWidth, labelT, gapStartT, gapEndT);
        }
        activeLinkKeys.insert(animationKey);
        const float emphasis = UpdateAnimatedState(
            m_LinkEmphasisAnim,
            animationKey,
            selected ? 1.0f : (hovered ? 0.62f : 0.0f),
            deltaTime,
            18.0f,
            11.0f);
        auto drawConnectionLabels = [&](ImU32 color) {
            if (!drawLabels) return;
            const ImU32 shadow = IM_COL32(0, 0, 0, 185);
            auto drawText = [&](const ImVec2& center, const ImVec2& size, const char* text) {
                if (textOutline) {
                    const ImVec2 offsets[] = {
                        ImVec2(labelTangent.x, labelTangent.y),
                        ImVec2(-labelTangent.x, -labelTangent.y),
                        ImVec2(labelNormal.x, labelNormal.y),
                        ImVec2(-labelNormal.x, -labelNormal.y)
                    };
                    for (const ImVec2& offset : offsets) {
                        DrawRotatedText(
                            drawList,
                            ImGui::GetFont(),
                            labelFontSize,
                            ImVec2(center.x + offset.x, center.y + offset.y),
                            size,
                            labelAngle,
                            shadow,
                            text);
                    }
                }
                DrawRotatedText(
                    drawList,
                    ImGui::GetFont(),
                    labelFontSize,
                    center,
                    size,
                    labelAngle,
                    color,
                    text);
            };
            drawText(primaryCenter, primarySize, linkText.primary.c_str());
            if (labelLineCount > 1) {
                drawText(secondaryCenter, secondarySize, linkText.secondary.c_str());
            }
        };
        auto drawDelayedWireDetail = [&]() {
            if (hovered && DetailCardDelayElapsed("link:" + animationKey)) {
                DrawSemanticLinkInspection(m_ActiveEditor, link, labelCenter);
            }
        };

        if (graphStyle.enabled) {
            const float handle = ResolveLinkHandle(p1, p2, straightLinks);
            const ImVec2 c1(p1.x + handle, p1.y);
            const ImVec2 c2(p2.x - handle, p2.y);
            ImVec4 linkColor = LinkColorVec(visualStyle, graphStyle);
            if (emphasis > 0.001f) {
                const ImVec4 emphasisColor = selected ? graphStyle.text : graphStyle.spotlightHalo;
                linkColor = WithAlpha(
                    BlendColor(linkColor, emphasisColor, selected ? (0.18f + emphasis * 0.12f) : (0.08f + emphasis * 0.16f)),
                    std::clamp(linkColor.w + emphasis * 0.10f, 0.0f, 1.0f));
            }

            DrawLinkStrokeWithEdgeFade(
                drawList,
                p1,
                c1,
                c2,
                p2,
                ColorWithAlpha(graphStyle.linkUnderlay, 0.48f + emphasis * (selected ? 0.34f : 0.22f)),
                (4.6f + emphasis * (selected ? 2.8f : 1.6f)) * thicknessScale,
                visualStyle.dotted,
                straightLinks,
                fadeMin,
                fadeMax,
                edgeFadeDistance,
                gapStartT,
                gapEndT);
            if (emphasis > 0.01f) {
                DrawLinkStrokeWithEdgeFade(
                    drawList,
                    p1,
                    c1,
                    c2,
                    p2,
                    ColorWithAlpha(selected ? graphStyle.selected : graphStyle.spotlightHalo, selected ? (0.34f + emphasis * 0.30f) : (0.14f + emphasis * 0.22f)),
                    (3.2f + emphasis * (selected ? 2.0f : 1.45f)) * thicknessScale,
                    visualStyle.dotted,
                    straightLinks,
                    fadeMin,
                    fadeMax,
                    edgeFadeDistance,
                    gapStartT,
                    gapEndT);
            }
            DrawLinkStrokeWithEdgeFade(
                drawList,
                p1,
                c1,
                c2,
                p2,
                ColorToU32(linkColor),
                (2.35f + emphasis * (selected ? 1.15f : 0.80f)) * thicknessScale,
                visualStyle.dotted,
                straightLinks,
                fadeMin,
                fadeMax,
                edgeFadeDistance,
                gapStartT,
                gapEndT);
            if (selected) {
                DrawLinkStrokeWithEdgeFade(
                    drawList,
                    p1,
                    c1,
                    c2,
                    p2,
                    ColorWithAlpha(graphStyle.text, 0.44f),
                    std::max(1.0f, 1.05f * thicknessScale),
                    visualStyle.dotted,
                    straightLinks,
                    fadeMin,
                    fadeMax,
                    edgeFadeDistance,
                    gapStartT,
                    gapEndT);
            }
            drawConnectionLabels(ColorToU32(linkColor));
            drawDelayedWireDetail();
            continue;
        }

        const float handle = LinkBezierHandle(p1, p2);
        DrawLinkStrokeWithEdgeFade(
            drawList,
            p1,
            ImVec2(p1.x + handle, p1.y),
            ImVec2(p2.x - handle, p2.y),
            p2,
            LinkColorClassic(visualStyle, selected),
            (3.0f + emphasis * (selected ? 1.5f : 0.9f)) * thicknessScale,
            visualStyle.dotted,
            straightLinks,
            fadeMin,
            fadeMax,
            edgeFadeDistance,
            gapStartT,
            gapEndT);
        drawConnectionLabels(LinkColorClassic(visualStyle, selected));
        drawDelayedWireDetail();
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
        graph.CanConnectSockets(m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId);
    const float dragPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.5f);
    LinkVisualStyle visualStyle = hoveredInputConnectable
        ? ResolvePendingLinkVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId)
        : ResolvePendingLinkVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, EditorNodeGraph::SocketDirection::Output);
    if (!GraphDottedMaskLinksEnabled(editor)) {
        visualStyle.dotted = false;
    }
    const bool straightLinks = GraphStraightLinksEnabled(editor);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const ImVec2 sourcePin = ToImVec2(OutputPinScreenPos(*from, m_DragOutputSocketId));
    ImVec2 p1 = sourcePin;
    ImVec2 p2 = ImGui::GetMousePos();
    const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
    p1.y += laneOffset;
    p2.y += laneOffset;
    const float handle = ResolveLinkHandle(p1, p2, straightLinks);
    const ImU32 dragColor = graphStyle.enabled
        ? ColorToU32(LinkColorVec(visualStyle, graphStyle))
        : LinkColorClassic(visualStyle, false);
    DrawLinkStrokeWithEdgeFade(
        drawList,
        p1,
        ImVec2(p1.x + handle, p1.y),
        ImVec2(p2.x - handle, p2.y),
        p2,
        dragColor,
        std::max(0.35f, (hoveredInputConnectable ? (2.8f + dragPulse * 0.45f) : 2.5f) * NodeUiScaleFromZoom(m_Zoom)),
        visualStyle.dotted,
        straightLinks,
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
        graph.CanConnectSockets(hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId);
    const float dragPulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.5f);
    LinkVisualStyle visualStyle = hoveredOutputConnectable
        ? ResolvePendingLinkVisualStyle(graph, hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId)
        : ResolvePendingLinkVisualStyle(graph, m_DragInputNodeId, m_DragInputSocketId, EditorNodeGraph::SocketDirection::Input);
    if (!GraphDottedMaskLinksEnabled(editor)) {
        visualStyle.dotted = false;
    }
    const bool straightLinks = GraphStraightLinksEnabled(editor);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    ImVec2 p1 = ImGui::GetMousePos();
    const ImVec2 targetPin = ToImVec2(InputPinScreenPos(*to, m_DragInputSocketId));
    ImVec2 p2 = targetPin;
    const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
    p1.y += laneOffset;
    p2.y += laneOffset;
    const float handle = ResolveLinkHandle(p1, p2, straightLinks);
    const ImU32 dragColor = graphStyle.enabled
        ? ColorToU32(LinkColorVec(visualStyle, graphStyle))
        : LinkColorClassic(visualStyle, false);
    DrawLinkStrokeWithEdgeFade(
        drawList,
        p1,
        ImVec2(p1.x + handle, p1.y),
        ImVec2(p2.x - handle, p2.y),
        p2,
        dragColor,
        std::max(0.35f, (hoveredOutputConnectable ? (2.8f + dragPulse * 0.45f) : 2.5f) * NodeUiScaleFromZoom(m_Zoom)),
        visualStyle.dotted,
        straightLinks,
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
