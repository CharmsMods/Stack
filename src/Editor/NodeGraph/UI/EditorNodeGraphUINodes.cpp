#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/GraphConnectionPresentation.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/SocketPresentation.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <string>
#include <vector>

namespace {

float SanitizeFinite(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
}

float SnapToPixel(float value) {
    return std::round(value);
}

ImVec2 SnapToPixel(const ImVec2& value) {
    return ImVec2(SnapToPixel(value.x), SnapToPixel(value.y));
}

ImVec2 FitPreviewRect(const ImVec2& bounds, const ImVec2& sourceSize) {
    if (bounds.x <= 1.0f || bounds.y <= 1.0f || sourceSize.x <= 1.0f || sourceSize.y <= 1.0f) {
        return bounds;
    }
    const float scale = std::min(bounds.x / sourceSize.x, bounds.y / sourceSize.y);
    return ImVec2(
        std::max(1.0f, sourceSize.x * scale),
        std::max(1.0f, sourceSize.y * scale));
}

float SmoothStep01(float value) {
    const float t = std::clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float CanvasEdgeFadePointAlpha(const ImVec2& point, const ImVec2& min, const ImVec2& max, float fadeDistance) {
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

float CanvasEdgeFadeRectAlpha(const ImVec2& rectMin, const ImVec2& rectMax, const ImVec2& fadeMin, const ImVec2& fadeMax, float fadeDistance) {
    if (fadeDistance <= 0.0f) {
        return 1.0f;
    }

    const ImVec2 center((rectMin.x + rectMax.x) * 0.5f, (rectMin.y + rectMax.y) * 0.5f);
    const ImVec2 samples[] = {
        rectMin,
        ImVec2(center.x, rectMin.y),
        ImVec2(rectMax.x, rectMin.y),
        ImVec2(rectMin.x, center.y),
        center,
        ImVec2(rectMax.x, center.y),
        ImVec2(rectMin.x, rectMax.y),
        ImVec2(center.x, rectMax.y),
        rectMax,
    };

    float alphaSum = 0.0f;
    float alphaMin = 1.0f;
    for (const ImVec2& sample : samples) {
        const float alpha = CanvasEdgeFadePointAlpha(sample, fadeMin, fadeMax, fadeDistance);
        alphaSum += alpha;
        alphaMin = std::min(alphaMin, alpha);
    }
    const float alphaAverage = alphaSum / static_cast<float>(IM_ARRAYSIZE(samples));
    return std::clamp((alphaAverage * 0.72f) + (alphaMin * 0.28f), 0.0f, 1.0f);
}

struct ScopedNodeEdgeAlpha {
    explicit ScopedNodeEdgeAlpha(float alpha) {
        if (alpha < 0.999f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * std::clamp(alpha, 0.0f, 1.0f));
            active = true;
        }
    }

    ~ScopedNodeEdgeAlpha() {
        if (active) {
            ImGui::PopStyleVar();
        }
    }

    bool active = false;
};

using namespace Stack::Editor::NodeGraphUIVisuals;

void PrepareAnchoredDetailCard(const ImVec2& anchor) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 workMin = viewport ? viewport->WorkPos : ImVec2(0.0f, 0.0f);
    const ImVec2 workMax = viewport
        ? ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y + viewport->WorkSize.y)
        : ImGui::GetIO().DisplaySize;
    constexpr float kCardWidth = 340.0f;
    constexpr float kEstimatedHeight = 290.0f;
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

} // namespace

void EditorNodeGraphUI::DrawSocketDetailCard(
    EditorModule* editor,
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::SocketDefinition& socket,
    const ImVec2& anchor) {
    using namespace EditorNodeGraph::SocketPresentation;
    PrepareAnchoredDetailCard(anchor);
    ImGui::BeginTooltip();
    ImGui::Text("%s — %s",
        socket.label.c_str(),
        socket.direction == EditorNodeGraph::SocketDirection::Input ? "Input" : "Output");
    ImGui::TextDisabled("%s", VisibilityName(socket.visibilityTier));
    ImGui::Separator();
    ImGui::TextWrapped("%s", PrimaryDescription(socket).c_str());
    ImGui::TextDisabled("%s", StorageName(socket.logicalType));
    const std::string units = UnitName(socket.declaredUnits);
    if (!units.empty()) ImGui::TextDisabled("Units: %s", units.c_str());

    if (editor) {
        for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
            const bool matches = socket.direction == EditorNodeGraph::SocketDirection::Input
                ? (link.toNodeId == socket.nodeId && link.toSocketId == socket.id)
                : (link.fromNodeId == socket.nodeId && link.fromSocketId == socket.id);
            if (!matches) continue;
            Stack::NodeMath::ValueDescriptor descriptor;
            if (editor->TryGetGraphLinkSemanticDescriptor(link, descriptor)) {
                ImGui::Separator();
                ImGui::TextDisabled("Current connected state");
                ImGui::TextWrapped("%s",
                    Stack::NodeMath::CompactDescriptorLabel(descriptor).c_str());
                if (descriptor.range.state == Stack::NodeMath::KnowledgeState::Known) {
                    ImGui::Text(
                        "Range: %.4g to %.4g%s%s",
                        descriptor.range.value.nominalMinimum,
                        descriptor.range.value.nominalMaximum,
                        descriptor.range.value.allowsBelowNominal ? " | below allowed" : "",
                        descriptor.range.value.allowsAboveNominal ? " | above allowed" : "");
                }
                if (descriptor.precision.state == Stack::NodeMath::KnowledgeState::Known) {
                    ImGui::TextDisabled("Precision: %s",
                        PrecisionName(descriptor.precision.value));
                }
                if (descriptor.spatial.state == Stack::NodeMath::KnowledgeState::Known) {
                    const auto& rect = descriptor.spatial.value.dataWindow;
                    ImGui::TextDisabled("Extent: %lld x %lld",
                        static_cast<long long>(rect.width),
                        static_cast<long long>(rect.height));
                }
                if (descriptor.provenance.state == Stack::NodeMath::KnowledgeState::Known) {
                    const std::string provenance =
                        descriptor.provenance.value.operationIdentity.empty()
                        ? descriptor.provenance.value.sourceIdentity
                        : descriptor.provenance.value.operationIdentity;
                    if (!provenance.empty()) {
                        ImGui::TextWrapped("From: %s", provenance.c_str());
                    }
                }
            }
            break;
        }
        const std::string affected = "node-" + std::to_string(socket.nodeId);
        for (const Stack::NodeMath::Diagnostic& diagnostic :
                editor->GetGraphSemanticDiagnostics()) {
            if (diagnostic.affectedIdentity != affected) continue;
            const ImVec4 color =
                diagnostic.severity == Stack::NodeMath::DiagnosticSeverity::Information
                ? ImVec4(0.62f, 0.76f, 0.88f, 1.0f)
                : ImVec4(0.94f, 0.73f, 0.36f, 1.0f);
            ImGui::TextColored(color, "%s", diagnostic.message.c_str());
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Compatible family: %s", LogicalTypeName(socket.logicalType));
    ImGui::TextWrapped("Compatible but unusual connections remain available. Connections that require a conversion need an explicit conversion or extraction node.");
    ImGui::EndTooltip();
}

void EditorNodeGraphUI::DrawAdvancedSocketRevealRows(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node,
    const NodeLayoutCache& layout,
    ImDrawList* drawList,
    ImU32 textColor,
    float uiScale) {
    int hiddenInputs = 0;
    int hiddenOutputs = 0;
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, false)) {
        if (socket.visibilityTier != EditorNodeGraph::SocketVisibilityTier::Advanced ||
            IsSocketConnected(graph, socket)) {
            continue;
        }
        const bool revealed = socket.direction == EditorNodeGraph::SocketDirection::Input
            ? m_RevealedAdvancedInputNodes.count(node.id) != 0
            : m_RevealedAdvancedOutputNodes.count(node.id) != 0;
        if (revealed) continue;
        if (socket.direction == EditorNodeGraph::SocketDirection::Input) ++hiddenInputs;
        else ++hiddenOutputs;
    }
    if (hiddenInputs == 0 && hiddenOutputs == 0) return;

    const float fontSize = std::max(8.0f, ImGui::GetFontSize() * uiScale * 0.72f);
    const float y = layout.frameRect.max.y - fontSize - 4.0f * uiScale;
    auto drawRow = [&](bool input, int count) {
        if (count <= 0) return;
        const std::string text = "+ " + std::to_string(count) +
            (input ? (count == 1 ? " Input" : " Inputs")
                   : (count == 1 ? " Output" : " Outputs"));
        const ImVec2 measured = ImGui::CalcTextSize(text.c_str());
        const float scale = fontSize / std::max(1.0f, ImGui::GetFontSize());
        const ImVec2 size(measured.x * scale, measured.y * scale);
        const ImVec2 pos(
            input ? layout.frameRect.min.x + 8.0f * uiScale
                  : layout.frameRect.max.x - size.x - 8.0f * uiScale,
            y);
        const ImVec2 hitMin(pos.x - 3.0f * uiScale, pos.y - 2.0f * uiScale);
        const ImVec2 hitMax(pos.x + size.x + 3.0f * uiScale, pos.y + size.y + 2.0f * uiScale);
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = mouse.x >= hitMin.x && mouse.x <= hitMax.x &&
            mouse.y >= hitMin.y && mouse.y <= hitMax.y;
        drawList->AddText(
            ImGui::GetFont(), fontSize, pos,
            hovered ? IM_COL32(255, 255, 255, 255) : textColor,
            text.c_str());
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (input) m_RevealedAdvancedInputNodes.insert(node.id);
            else m_RevealedAdvancedOutputNodes.insert(node.id);
        }
    };
    drawRow(true, hiddenInputs);
    drawRow(false, hiddenOutputs);
}

void EditorNodeGraphUI::RenderNode(EditorModule* editor, EditorNodeGraph::Node& node) {
    EditorNodeGraph::Graph& graph = GetActiveGraph(editor);
    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(this, editor, node, metrics);
    const float widthScale = NodeWidthScale();
    metrics.width *= widthScale;
    metrics.contentLaneWidth *= widthScale;
    metrics.previewWidth *= widthScale;
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const NodePresentationProfile presentation = BuildNodePresentationProfile(this, editor, node, graphStyle);
    const NodeFamilyStyle familyStyle = StyleForFamily(FamilyForNode(node), graphStyle);
    if (!FindNodeLayoutCache(node.id)) {
        RefreshNodeLayoutCache(graph, node);
    }
    const NodeLayoutCache* layout = FindNodeLayoutCache(node.id);
    if (!layout) {
        return;
    }

    const ImVec2 min = layout->frameRect.min;
    const ImVec2 max = layout->frameRect.max;
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const ImVec2 canvasSize(
        std::max(1.0f, m_CanvasMax.x - m_CanvasMin.x),
        std::max(1.0f, m_CanvasMax.y - m_CanvasMin.y));
    const float edgeFadeDistance = wallpaperSurfaces
        ? std::min(144.0f, std::max(72.0f, std::min(canvasSize.x, canvasSize.y) * 0.14f))
        : 0.0f;
    const float nodeEdgeAlpha = CanvasEdgeFadeRectAlpha(
        min,
        max,
        ImVec2(m_CanvasMin.x, m_CanvasMin.y),
        ImVec2(m_CanvasMax.x, m_CanvasMax.y),
        edgeFadeDistance);
    if (nodeEdgeAlpha <= 0.001f) {
        return;
    }
    ScopedNodeEdgeAlpha scopedNodeEdgeAlpha(nodeEdgeAlpha);
    const bool selected = graph.IsNodeSelected(node.id);
    const bool hovered =
        !m_RenderPreviewOnly &&
        !m_RenderCaptureOnly &&
        !m_MiddlePanCaptureActive &&
        ImGui::IsMouseHoveringRect(min, max, false);
    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    const float selectedAnim = UpdateAnimatedState(
        m_NodeSelectionAnim,
        node.id,
        selected ? 1.0f : 0.0f,
        deltaTime,
        17.0f,
        11.0f);
    const float hoverAnim = UpdateAnimatedState(
        m_NodeHoverAnim,
        node.id,
        hovered ? 1.0f : 0.0f,
        deltaTime,
        15.0f,
        10.0f);
    const float nodeEmphasis = std::clamp(selectedAnim * 0.90f + hoverAnim * 0.55f, 0.0f, 1.0f);
    const float uiScale = NodeContentScale();
    const float pinRadius = NodePinRadius();
    auto socketInteractionEmphasis = [&](EditorNodeGraph::SocketDirection direction, const std::string& socketId, bool hoveredSocket) -> float {
        float emphasis = 0.0f;
        if (direction == EditorNodeGraph::SocketDirection::Output &&
            m_DragOutputNodeId == node.id &&
            m_DragOutputSocketId == socketId) {
            emphasis = 0.82f;
        } else if (
            direction == EditorNodeGraph::SocketDirection::Input &&
            m_DragInputNodeId == node.id &&
            m_DragInputSocketId == socketId) {
            emphasis = 0.82f;
        }

        if (direction == EditorNodeGraph::SocketDirection::Input &&
            m_DragOutputNodeId > 0 &&
            hoveredSocket &&
            graph.CanConnectSockets(m_DragOutputNodeId, m_DragOutputSocketId, node.id, socketId)) {
            emphasis = std::max(emphasis, 1.0f);
        } else if (
            direction == EditorNodeGraph::SocketDirection::Output &&
            m_DragInputNodeId > 0 &&
            hoveredSocket &&
            graph.CanConnectSockets(node.id, socketId, m_DragInputNodeId, m_DragInputSocketId)) {
            emphasis = std::max(emphasis, 1.0f);
        }

        return emphasis;
    };

    if (presentation.kind == NodePresentationKind::FramelessMedia) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        unsigned int texture = 0;
        ImVec2 imageSourceSize(1.0f, 1.0f);
        if (node.kind == EditorNodeGraph::NodeKind::Image) {
            texture = GetImagePreviewTexture(node);
            imageSourceSize = ImVec2(
                static_cast<float>(std::max(1, node.image.width)),
                static_cast<float>(std::max(1, node.image.height)));
        } else if (node.kind == EditorNodeGraph::NodeKind::Output && editor) {
            texture = editor->GetPipeline().GetOutputTexture();
            imageSourceSize = ImVec2(
                static_cast<float>(std::max(1, editor->GetPipeline().GetCanvasWidth())),
                static_cast<float>(std::max(1, editor->GetPipeline().GetCanvasHeight())));
        }
        const ImVec2 frameSize(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y));
        const ImVec2 fittedSize = FitPreviewRect(frameSize, imageSourceSize);
        const ImVec2 imageMin(
            min.x + (frameSize.x - fittedSize.x) * 0.5f,
            min.y + (frameSize.y - fittedSize.y) * 0.5f);
        const ImVec2 imageMax(imageMin.x + fittedSize.x, imageMin.y + fittedSize.y);
        if (texture != 0) {
            drawList->AddImage(
                (ImTextureID)(intptr_t)texture,
                imageMin,
                imageMax,
                ImVec2(0, 1),
                ImVec2(1, 0),
                ApplyStyleAlpha(IM_COL32_WHITE));
        } else {
            drawList->AddRectFilled(imageMin, imageMax, ColorWithAlpha(graphStyle.nodeSurface, 0.42f), std::max(3.0f, 5.0f * uiScale));
        }
        const float previewRounding = std::max(3.0f, 5.0f * uiScale);
        if (selectedAnim > 0.001f) {
            drawList->AddRectFilled(
                imageMin,
                imageMax,
                ApplyStyleAlpha(IM_COL32(255, 255, 255, static_cast<int>(14.0f + 24.0f * selectedAnim))),
                previewRounding);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
            const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
            if (!anchor) {
                continue;
            }
            const bool hoveredSocket = socket.direction == EditorNodeGraph::SocketDirection::Input
                ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
                : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
            const float socketFocus = socketInteractionEmphasis(socket.direction, socket.id, hoveredSocket);
            const ImU32 socketColor = SocketColor(socket, familyStyle, graphStyle);
            DrawSocketPin(drawList, anchor->screenPos, pinRadius, socketColor, graphStyle, hoveredSocket, socketFocus);
            if (selected || hovered || hoveredSocket) {
                const float labelSize = std::max(8.0f, ImGui::GetFontSize() * uiScale * 0.72f);
                const ImVec2 measured = ImGui::CalcTextSize(socket.label.c_str());
                const float scale = labelSize / std::max(1.0f, ImGui::GetFontSize());
                const ImVec2 labelPos = socket.direction == EditorNodeGraph::SocketDirection::Input
                    ? ImVec2(anchor->screenPos.x + pinRadius * 2.2f, anchor->screenPos.y - labelSize * 0.45f)
                    : ImVec2(anchor->screenPos.x - pinRadius * 2.2f - measured.x * scale, anchor->screenPos.y - labelSize * 0.45f);
                drawList->AddText(ImGui::GetFont(), labelSize, labelPos, socketColor, socket.label.c_str());
            }
            if (hoveredSocket && DetailCardDelayElapsed(
                    "socket:" + std::to_string(node.id) + ":" + socket.id +
                    (socket.direction == EditorNodeGraph::SocketDirection::Input ? ":in" : ":out"))) {
                DrawSocketDetailCard(editor, graph, socket, anchor->screenPos);
            }
        }
        DrawAdvancedSocketRevealRows(
            graph, node, *layout, drawList,
            graphStyle.enabled ? ColorWithAlpha(graphStyle.mutedText, 0.92f)
                               : ApplyStyleAlpha(IM_COL32(180, 195, 205, 220)),
            uiScale);

        return;
    }

    const bool isSquareNode = (node.kind == EditorNodeGraph::NodeKind::Output) ||
        (node.kind == EditorNodeGraph::NodeKind::ChannelSplit) ||
        (node.kind == EditorNodeGraph::NodeKind::ChannelCombine) ||
        IsSummaryOnlyNode(this, editor, node);

    if (isSquareNode) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float frameRounding = std::max(4.0f, 8.0f * uiScale);
        const float borderThickness = std::max(0.95f, 1.15f * uiScale);

        ImVec4 fillColor = BlendColor(familyStyle.fill, ImVec4(0.10f, 0.12f, 0.13f, familyStyle.fill.w), 0.18f);
        ImVec4 borderColor = familyStyle.border;
        if (node.kind == EditorNodeGraph::NodeKind::Output && !node.outputEnabled) {
            fillColor = ImVec4(0.26f, 0.08f, 0.08f, 1.0f);
            borderColor = ImVec4(0.65f, 0.20f, 0.20f, 1.0f);
        }
        if (nodeEmphasis > 0.001f) {
            fillColor = BlendColor(fillColor, familyStyle.accent, 0.05f * hoverAnim + 0.10f * selectedAnim);
            if (selectedAnim > 0.001f) {
                borderColor = BlendColor(borderColor, graphStyle.selected, 0.14f + selectedAnim * 0.34f);
            }
        }

        DrawGraphNodeSpotlightSurface(
            drawList,
            min,
            max,
            fillColor,
            borderColor,
            familyStyle.accent,
            graphStyle,
            selected,
            true,
            uiScale,
            frameRounding,
            borderThickness);
        // Custom compact labels for square nodes
        std::string squareLabel = "Output";
        if (node.kind == EditorNodeGraph::NodeKind::Layer) {
            squareLabel = CompactAdvancedLayerLabel(node);
        } else if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
            squareLabel = "RAW";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment) {
            squareLabel = "RAW Dev";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            squareLabel = "RAW Denoise";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDecode) {
            squareLabel = "RAW Decode";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDevelop) {
            squareLabel = "Develop";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
            squareLabel = "Auto Mask";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDetailFusion) {
            squareLabel = "Pre-Local";
        } else if (node.kind == EditorNodeGraph::NodeKind::HdrMerge) {
            squareLabel = "HDR Merge";
        } else if (node.kind == EditorNodeGraph::NodeKind::Mfsr) {
            squareLabel = "MFSR";
        } else if (node.kind == EditorNodeGraph::NodeKind::Lut) {
            squareLabel = "LUT";
        } else if (node.kind == EditorNodeGraph::NodeKind::CustomMask) {
            squareLabel = "Mask Edit";
        } else if (node.kind == EditorNodeGraph::NodeKind::Output && !node.outputEnabled) {
            squareLabel = "Deactivated";
        } else if (node.kind == EditorNodeGraph::NodeKind::ChannelSplit) {
            squareLabel = "Split";
        } else if (node.kind == EditorNodeGraph::NodeKind::ChannelCombine) {
            squareLabel = "Combine";
        }

        const float fontSize = ImGui::GetFontSize() * uiScale;
        const std::string squareDisplayLabel = EllipsizeLabel(
            squareLabel,
            std::max(28.0f, (max.x - min.x - (12.0f * uiScale)) / std::max(0.01f, uiScale)));
        ImVec2 textSize = ImGui::CalcTextSize(squareDisplayLabel.c_str());
        ImVec2 scaledTextSize = ImVec2(textSize.x * uiScale, textSize.y * uiScale);

        bool isOutputFourPins = false;
        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
                if (socket.id == "r" || socket.id == "g" || socket.id == "b" || socket.id == "a") {
                    isOutputFourPins = true;
                    break;
                }
            }
        }

        const char* outputModeLabel = isOutputFourPins ? "RGBA" : "Full";
        const float modeFontSize = std::max(9.0f, fontSize * 0.72f);
        ImVec2 scaledModeTextSize {};
        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            const ImVec2 modeTextSize = ImGui::CalcTextSize(outputModeLabel);
            const float modeScale = modeFontSize / std::max(1.0f, ImGui::GetFontSize());
            scaledModeTextSize = ImVec2(modeTextSize.x * modeScale, modeTextSize.y * modeScale);
        }
        const float labelBlockHeight = (node.kind == EditorNodeGraph::NodeKind::Output)
            ? (scaledTextSize.y + scaledModeTextSize.y + 3.0f * uiScale)
            : scaledTextSize.y;
        ImVec2 textPos = ImVec2(
            isOutputFourPins ? min.x + (max.x - min.x) * 0.54f - scaledTextSize.x * 0.5f : min.x + (max.x - min.x - scaledTextSize.x) * 0.5f,
            min.y + (max.y - min.y - labelBlockHeight) * 0.5f
        );

        drawList->AddText(
            ImGui::GetFont(),
            fontSize,
            textPos,
            ColorToU32(familyStyle.text),
            squareDisplayLabel.c_str());

        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            const ImVec2 modePos(
                min.x + (max.x - min.x - scaledModeTextSize.x) * 0.5f,
                textPos.y + scaledTextSize.y + 3.0f * uiScale);
            drawList->AddText(
                ImGui::GetFont(),
                modeFontSize,
                modePos,
                graphStyle.enabled ? ColorWithAlpha(graphStyle.mutedText, 0.92f) : ApplyStyleAlpha(IM_COL32(180, 195, 205, 220)),
                outputModeLabel);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
            const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
            if (anchor) {
                const ImVec2 pin = anchor->screenPos;
                const bool hoveredSocket = (socket.direction == EditorNodeGraph::SocketDirection::Input)
                    ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
                    : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
                const float socketFocus = socketInteractionEmphasis(socket.direction, socket.id, hoveredSocket);

                const ImU32 baseColor = SocketColor(socket, familyStyle, graphStyle);
                DrawSocketPin(drawList, pin, pinRadius, baseColor, graphStyle, hoveredSocket, socketFocus);

                const bool isChannelSocket = socket.id == "r" || socket.id == "g" || socket.id == "b" || socket.id == "a";
                const bool showCompactLabel = isChannelSocket || selected || hovered || hoveredSocket;
                if (isSquareNode && showCompactLabel) {
                    std::string labelText = socket.label;
                    if (labelText == "A (Generated)") {
                        labelText = "A (Gen)";
                    }
                    ImVec2 pinLabelPos;
                    if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
                        pinLabelPos = ImVec2(pin.x + pinRadius * 2.2f, pin.y - fontSize * 0.45f);
                        drawList->AddText(ImGui::GetFont(), fontSize * 0.72f, pinLabelPos, baseColor, labelText.c_str());
                    } else {
                        ImVec2 labelSize = ImGui::CalcTextSize(labelText.c_str());
                        const float labelFontScale = (fontSize * 0.72f) / std::max(1.0f, ImGui::GetFontSize());
                        float scaledLabelWidth = labelSize.x * labelFontScale;
                        pinLabelPos = ImVec2(pin.x - pinRadius * 2.2f - scaledLabelWidth, pin.y - fontSize * 0.45f);
                        drawList->AddText(ImGui::GetFont(), fontSize * 0.72f, pinLabelPos, baseColor, labelText.c_str());
                    }
                }

                if (hoveredSocket && DetailCardDelayElapsed(
                        "socket:" + std::to_string(node.id) + ":" + socket.id +
                        (socket.direction == EditorNodeGraph::SocketDirection::Input ? ":in" : ":out"))) {
                    DrawSocketDetailCard(editor, graph, socket, pin);
                }
            }
        }
        DrawAdvancedSocketRevealRows(
            graph, node, *layout, drawList,
            graphStyle.enabled ? ColorWithAlpha(graphStyle.mutedText, 0.92f)
                               : ApplyStyleAlpha(IM_COL32(180, 195, 205, 220)),
            uiScale);

        if (IsSummaryOnlyNode(this, editor, node) && hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(node.title.empty() ? squareLabel.c_str() : node.title.c_str());
            if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
                if (!node.rawSource.label.empty()) {
                    ImGui::TextDisabled("%s", node.rawSource.label.c_str());
                }
                if (!node.rawSource.sourcePath.empty()) {
                    ImGui::TextDisabled("%s", node.rawSource.sourcePath.c_str());
                }
            } else if (node.kind == EditorNodeGraph::NodeKind::RawDevelopment) {
                const std::string displayName = Stack::RawRecipe::RecipeDisplayName(node.rawDevelopment.recipe);
                if (!displayName.empty()) {
                    ImGui::TextDisabled("%s", displayName.c_str());
                }
                if (!node.rawDevelopment.projectStatus.empty()) {
                    ImGui::TextDisabled("Project: %s", node.rawDevelopment.projectStatus.c_str());
                }
                if (node.rawDevelopment.edited || node.rawDevelopment.autosaved) {
                    ImGui::TextDisabled(
                        "State: %s%s",
                        node.rawDevelopment.edited ? "edited" : "clean",
                        node.rawDevelopment.autosaved ? ", autosaved" : "");
                }
            } else if (node.kind == EditorNodeGraph::NodeKind::Lut) {
                if (!node.lut.label.empty()) {
                    ImGui::TextDisabled("%s", node.lut.label.c_str());
                }
                if (!node.lut.importedTitle.empty()) {
                    ImGui::TextDisabled("%s", node.lut.importedTitle.c_str());
                }
                if (!node.lut.importError.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "%s", node.lut.importError.c_str());
                } else {
                    ImGui::TextDisabled("%s", ColorLut::LutTypeSummary(node.lut));
                }
            } else if (HasDedicatedComplexEditor(this, editor, node)) {
                ImGui::TextDisabled("Detailed controls are in the node inspector.");
            }
            ImGui::EndTooltip();
        }

        return;
    }

    const bool expanded = node.expanded;
    const bool richExpandedSurface = node.kind == EditorNodeGraph::NodeKind::Layer && ResolveLayerUsesRichNodeSurface(editor, node.layerIndex);
    const NodeSurfaceSpec nodeSurfaceSpec = node.kind == EditorNodeGraph::NodeKind::Layer
        ? ResolveLayerSurfaceSpec(editor, node.layerIndex)
        : NodeSurfaceSpec{};
    const float densityScale = richExpandedSurface
        ? (nodeSurfaceSpec.density == NodeSurfaceDensity::UltraDense ? 0.72f : 0.82f)
        : 1.0f;
    const float contentScale = uiScale * densityScale;
    const float controlWidth = std::max(24.0f, layout->contentRect.max.x - layout->contentRect.min.x);
    const float safeContentWidth = std::max(20.0f, controlWidth - std::max(2.0f, 4.0f * uiScale));
    const float logicalControlWidth = controlWidth / std::max(0.001f, uiScale);
    const float logicalSafeContentWidth = safeContentWidth / std::max(0.001f, uiScale);
    const ImVec2 previewSize = NodePreviewSizeForScale(metrics, uiScale);
    const float frameRounding = std::max(4.0f, 8.0f * uiScale);
    const float borderThickness = std::max(0.95f, 1.15f * uiScale);
    const float headerY = metrics.headerInsetY * uiScale;
    const float sectionGap = metrics.sectionGap * uiScale;
    const float itemGap = metrics.itemGap * uiScale;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 fillColor = expanded
        ? familyStyle.fill
        : BlendColor(familyStyle.fill, ImVec4(0.10f, 0.12f, 0.13f, familyStyle.fill.w), 0.18f);
    ImVec4 borderColor = familyStyle.border;
    if (nodeEmphasis > 0.001f) {
        fillColor = BlendColor(fillColor, familyStyle.accent, 0.04f * hoverAnim + 0.09f * selectedAnim);
        if (selectedAnim > 0.001f) {
            borderColor = BlendColor(borderColor, graphStyle.selected, 0.14f + selectedAnim * 0.34f);
        }
    }
    DrawGraphNodeSpotlightSurface(
        drawList,
        min,
        max,
        fillColor,
        borderColor,
        familyStyle.accent,
        graphStyle,
        selected,
        expanded,
        uiScale,
        frameRounding,
        borderThickness);
    if (!graphStyle.enabled) {
        const float accentHeight = std::max(2.0f, 3.0f * uiScale);
        drawList->AddRectFilled(
            ImVec2(min.x, min.y),
            ImVec2(max.x, min.y + accentHeight),
            ColorWithAlpha(familyStyle.accent, expanded ? 0.42f : 0.28f),
            frameRounding,
            ImDrawFlags_RoundCornersTop);
    }
    if (expanded && layout->headerRect.IsValid()) {
        const float headerBottom = std::clamp(
            layout->headerRect.max.y,
            min.y + 1.0f,
            max.y - 1.0f);
        drawList->AddRectFilled(
            ImVec2(min.x + borderThickness, min.y + borderThickness),
            ImVec2(max.x - borderThickness, headerBottom),
            ColorWithAlpha(familyStyle.accent, graphStyle.light ? 0.035f : 0.055f),
            frameRounding,
            ImDrawFlags_RoundCornersTop);
        drawList->AddLine(
            ImVec2(min.x + 8.0f * uiScale, headerBottom),
            ImVec2(max.x - 8.0f * uiScale, headerBottom),
            ColorWithAlpha(familyStyle.border, 0.44f),
            std::max(0.75f, uiScale));
    }

    for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
        const bool isInput = socket.direction == EditorNodeGraph::SocketDirection::Input;
        const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
        if (!anchor) {
            continue;
        }
        const ImVec2 pin = anchor->screenPos;
        const bool hoveredSocket = isInput
            ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
            : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
        const float socketFocus = socketInteractionEmphasis(socket.direction, socket.id, hoveredSocket);
        const ImU32 baseColor = SocketColor(socket, familyStyle, graphStyle);
        DrawSocketPin(drawList, pin, pinRadius, baseColor, graphStyle, hoveredSocket, socketFocus);
        const bool showShortLabel = expanded || selected || hovered || hoveredSocket;
        if (showShortLabel) {
            const float pinLabelSize = std::max(3.5f, ImGui::GetFontSize() * uiScale * 0.86f);
            const float pinLabelScale = pinLabelSize / std::max(1.0f, ImGui::GetFontSize());
            const auto horizontal = EditorNodeGraph::ConnectionPresentation::PinLabelRange(
                layout->contentRect.min.x,
                layout->contentRect.max.x,
                pin.x,
                isInput,
                10.0f * uiScale,
                4.0f * uiScale);
            const ImVec2 labelMin(horizontal.minimum, pin.y - (pinLabelSize * 0.85f));
            const ImVec2 labelMax(horizontal.maximum, pin.y + (pinLabelSize * 0.85f));
            if (labelMax.x > labelMin.x) {
            const float maxLabelWidth = labelMax.x - labelMin.x;
            const std::string displayLabel = EllipsizeLabel(socket.label, maxLabelWidth / std::max(0.01f, pinLabelScale));
            const ImVec2 textSize = ImGui::CalcTextSize(displayLabel.c_str());
            const ImVec2 labelPos = isInput
                ? labelMin
                : ImVec2(labelMax.x - (textSize.x * pinLabelScale), labelMin.y);
            drawList->PushClipRect(labelMin, labelMax, true);
            drawList->AddText(
                ImGui::GetFont(),
                pinLabelSize,
                labelPos,
                ColorWithAlpha(
                    BlendColor(
                        familyStyle.mutedText,
                        BlendColor(familyStyle.text, familyStyle.accent, 0.28f),
                        std::clamp(socketFocus * 0.72f + (hoveredSocket ? 0.18f : 0.0f), 0.0f, 1.0f)),
                    0.94f),
                displayLabel.c_str());
            drawList->PopClipRect();
            }
        }
        if (hoveredSocket && DetailCardDelayElapsed(
                "socket:" + std::to_string(node.id) + ":" + socket.id +
                (isInput ? ":in" : ":out"))) {
            DrawSocketDetailCard(editor, graph, socket, pin);
        }
    }
    DrawAdvancedSocketRevealRows(
        graph, node, *layout, drawList,
        ColorWithAlpha(familyStyle.mutedText, 0.90f), uiScale);

    auto nodePrimaryTitle = [&]() -> std::string {
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::Layer:
                return std::to_string(node.layerIndex + 1) + ". " + (node.title.empty() ? "Layer" : node.title);
            case EditorNodeGraph::NodeKind::MaskGenerator:
                return MaskLabel(node.maskKind);
            case EditorNodeGraph::NodeKind::CustomMask:
                return node.title.empty() ? "Custom Mask" : node.title;
            case EditorNodeGraph::NodeKind::MaskCombine:
                return node.title.empty() ? "Intersect Mask" : node.title;
            case EditorNodeGraph::NodeKind::MaskUtility:
                return MaskUtilityLabel(node.maskUtilityKind);
            case EditorNodeGraph::NodeKind::ImageToMask:
                return node.imageToMaskKind == EditorNodeGraph::ImageToMaskKind::SampledRange
                    ? "Sampled Range Mask"
                    : "Luminance Mask";
            case EditorNodeGraph::NodeKind::ImageGenerator:
                return ImageGeneratorLabel(node.imageGeneratorKind);
            case EditorNodeGraph::NodeKind::Scope:
                return ScopeLabel(node.scopeKind);
            case EditorNodeGraph::NodeKind::Mix:
                return node.title.empty() ? "Blend Images" : node.title;
            case EditorNodeGraph::NodeKind::DataMath:
                return node.title.empty() ? DataMathLabel(node.dataMathMode) : node.title;
            case EditorNodeGraph::NodeKind::Compound:
                return node.title.empty() ? "Compound" : node.title;
            case EditorNodeGraph::NodeKind::FrequencyFft:
            case EditorNodeGraph::NodeKind::FrequencyIfft:
            case EditorNodeGraph::NodeKind::SpectrumView:
            case EditorNodeGraph::NodeKind::FrequencyMask:
            case EditorNodeGraph::NodeKind::SpectrumMath:
            case EditorNodeGraph::NodeKind::MagnitudePhase:
            case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
                return node.title.empty() ? NodeKindLabel(node.kind) : node.title;
            case EditorNodeGraph::NodeKind::Preview:
                return node.title.empty() ? "Preview" : node.title;
            case EditorNodeGraph::NodeKind::RawDetailAutoMask:
                return node.title.empty() ? "RAW Detail Auto Mask" : node.title;
            case EditorNodeGraph::NodeKind::Image:
            case EditorNodeGraph::NodeKind::RawSource:
            case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            case EditorNodeGraph::NodeKind::RawDecode:
            case EditorNodeGraph::NodeKind::RawDevelop:
            case EditorNodeGraph::NodeKind::RawDetailFusion:
            case EditorNodeGraph::NodeKind::Output:
            case EditorNodeGraph::NodeKind::Composite:
            case EditorNodeGraph::NodeKind::ChannelSplit:
            case EditorNodeGraph::NodeKind::ChannelCombine:
                return node.title.empty() ? NodeKindLabel(node.kind) : node.title;
        }
        return node.title.empty() ? NodeKindLabel(node.kind) : node.title;
    }();

    const bool blackNodeMode = graphStyle.mode == StackAppearance::GraphVisualMode::BlackNodes;
    const ImVec4 frameBg = blackNodeMode
        ? WithAlpha(BlendColor(familyStyle.fill, familyStyle.accent, 0.08f), 0.88f)
        : graphStyle.enabled
        ? WithAlpha(BlendColor(graphStyle.canvas, familyStyle.accent, graphStyle.light ? 0.035f : 0.050f), graphStyle.light ? 0.20f : 0.18f)
        : BlendColor(familyStyle.fill, ImVec4(0.12f, 0.14f, 0.15f, 1.0f), 0.34f);
    const ImVec4 frameBgHovered = blackNodeMode
        ? WithAlpha(BlendColor(frameBg, familyStyle.accent, 0.22f), 0.94f)
        : graphStyle.enabled
        ? WithAlpha(BlendColor(frameBg, familyStyle.accent, 0.18f), graphStyle.light ? 0.30f : 0.28f)
        : BlendColor(frameBg, familyStyle.accent, 0.12f);
    const ImVec4 frameBgActive = blackNodeMode
        ? WithAlpha(BlendColor(frameBg, familyStyle.accent, 0.34f), 0.98f)
        : graphStyle.enabled
        ? WithAlpha(BlendColor(frameBg, familyStyle.accent, 0.28f), graphStyle.light ? 0.40f : 0.36f)
        : BlendColor(frameBg, familyStyle.accent, 0.22f);
    const ImVec4 buttonBg = blackNodeMode
        ? WithAlpha(BlendColor(familyStyle.fill, familyStyle.accent, 0.12f), 0.82f)
        : graphStyle.enabled
        ? WithAlpha(BlendColor(graphStyle.canvas, familyStyle.accent, 0.08f), graphStyle.light ? 0.18f : 0.16f)
        : BlendColor(familyStyle.fill, familyStyle.accent, 0.18f);
    const ImVec4 buttonBgHovered = blackNodeMode
        ? WithAlpha(BlendColor(buttonBg, familyStyle.accent, 0.24f), 0.90f)
        : graphStyle.enabled
        ? WithAlpha(BlendColor(buttonBg, familyStyle.accent, 0.24f), graphStyle.light ? 0.30f : 0.28f)
        : BlendColor(buttonBg, familyStyle.accent, 0.20f);
    const ImVec4 buttonBgActive = blackNodeMode
        ? WithAlpha(BlendColor(buttonBg, familyStyle.accent, 0.36f), 0.96f)
        : graphStyle.enabled
        ? WithAlpha(BlendColor(buttonBg, familyStyle.accent, 0.34f), graphStyle.light ? 0.40f : 0.36f)
        : BlendColor(buttonBg, familyStyle.accent, 0.32f);
    const ImVec4 textMuted = BlendColor(familyStyle.mutedText, familyStyle.text, 0.18f);

    ImGui::PushID(node.id);
    const float titleFontSize = ImGui::GetFontSize() * uiScale;
    const ImVec2 headerTextMin(layout->contentRect.min.x, min.y + headerY);
    ImVec2 headerCursor = headerTextMin;
    if (presentation.showKindLabel) {
        const float kindFontSize = std::max(8.0f, ImGui::GetFontSize() * uiScale * 0.86f);
        drawList->AddText(
            ImGui::GetFont(),
            kindFontSize,
            headerCursor,
            ColorWithAlpha(textMuted, 0.96f),
            NodeKindLabel(node.kind));
        headerCursor.y += kindFontSize + (2.0f * uiScale);
    }
    if (presentation.showTitle) {
        const std::string displayTitle = EllipsizeLabel(nodePrimaryTitle, std::max(36.0f, controlWidth - (6.0f * uiScale)));
        drawList->AddText(
            ImGui::GetFont(),
            titleFontSize,
            headerCursor,
            ColorToU32(familyStyle.text),
            displayTitle.c_str());
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::max(0.35f, 6.0f * uiScale * densityScale), std::max(0.25f, 2.5f * uiScale * densityScale)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::max(0.35f, metrics.itemGap * uiScale * 0.75f * densityScale), std::max(0.35f, metrics.itemGap * uiScale * 0.58f * densityScale)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(std::max(0.35f, 4.0f * uiScale * densityScale), std::max(0.25f, 2.0f * uiScale * densityScale)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(0.25f, 5.0f * uiScale * densityScale));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, std::max(0.75f, 6.5f * uiScale * densityScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(0.20f, 1.0f * uiScale));
    ImGui::PushStyleColor(ImGuiCol_Text, familyStyle.text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, textMuted);
    ImGui::PushStyleColor(ImGuiCol_Border, BlendColor(familyStyle.border, familyStyle.accent, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, frameBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, frameBgHovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, frameBgActive);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, familyStyle.accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, BlendColor(familyStyle.accent, familyStyle.text, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_Button, buttonBg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonBgHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonBgActive);
    ImGui::PushStyleColor(ImGuiCol_Header, buttonBg);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, buttonBgHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, buttonBgActive);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, familyStyle.accent);
    if (!expanded) {
        ImGui::PopStyleColor(15);
        ImGui::PopStyleVar(7);
        ImGui::PopID();
        RefreshNodeLayoutCache(graph, node);
        return;
    }

    ImGui::SetCursorScreenPos(SnapToPixel(layout->contentRect.min));
    bool fontScalePushed = false;
    if (std::abs(contentScale - 1.0f) > 0.0001f) {
        ImGui::SetWindowFontScale(contentScale);
        fontScalePushed = true;
    }
    ImGui::PushItemWidth(controlWidth);
    ImGuiExtras::ResetNodeControlState();
    ImGuiExtras::GraphNodeControlScopeConfig graphControlConfig {};
    graphControlConfig.labelWidth = 68.0f * contentScale;
    graphControlConfig.valueWidth = 46.0f * contentScale;
    graphControlConfig.minSliderWidth = 82.0f * contentScale;
    graphControlConfig.scale = contentScale;
    graphControlConfig.interactionScale = uiScale;
    graphControlConfig.allowSliderTextEntry = true;
    graphControlConfig.useScrubHandles = true;
    graphControlConfig.rangePolicy = GraphSliderRangePolicyForNodeKind(node.kind);
    graphControlConfig.scrubSensitivity = appearance
        ? appearance->GetGraphNodeSliderDragSensitivity()
        : StackAppearance::kGraphNodeSliderDragSensitivityDefault;
    ImGuiExtras::BeginGraphNodeControlScope(graphControlConfig);
    ImGui::BeginGroup();
    const ImVec2 contentUsedMin = ImGui::GetCursorScreenPos();
    auto captureIfActive = [&]() {
        const bool itemHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        const bool itemActive = ImGui::IsItemActive();
        const bool itemEdited = ImGui::IsItemEdited();
        if (itemHovered || itemActive || itemEdited) {
            m_NodeContentHovered |= itemHovered;
            m_NodeContentActive |= itemActive || itemEdited;
            m_LastNodeControlId = ImGui::GetItemID();
        }
    };
    auto renderSlider = [&](const char* label, const char* id, float* value, float minValue, float maxValue) -> bool {
        const bool changed = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, "%.3f", controlWidth);
        captureIfActive();
        return changed;
    };
    auto drawInlineSeparator = [&]() {
        const ImVec2 separatorPos = ImGui::GetCursorScreenPos();
        const float separatorWidth = std::max(18.0f, controlWidth);
        const float separatorHeight = std::max(7.0f, metrics.itemGap * uiScale * 0.55f);
        const float separatorY = separatorPos.y + separatorHeight * 0.5f;
        drawList->AddLine(
            ImVec2(separatorPos.x, separatorY),
            ImVec2(separatorPos.x + separatorWidth, separatorY),
            ApplyStyleAlpha(IM_COL32(118, 134, 144, 90)),
            1.0f);
        ImGui::Dummy(ImVec2(separatorWidth, separatorHeight));
    };

    if (node.kind == EditorNodeGraph::NodeKind::Layer) {
        auto* layers = GetActiveLayers(editor);
        if (layers && node.layerIndex >= 0 && node.layerIndex < static_cast<int>(layers->size()) && (*layers)[node.layerIndex]) {
            auto renderLayerControls = [&](LayerBase& layer) {
                RenderLayerMetadataNotes(graph, node, controlWidth);
                if (richExpandedSurface) {
                    NodeSurfaceContext surfaceContext;
                    surfaceContext.nodeId = node.id;
                    surfaceContext.availableWidth = controlWidth;
                    surfaceContext.safeContentWidth = safeContentWidth;
                    surfaceContext.logicalAvailableWidth = logicalControlWidth;
                    surfaceContext.logicalSafeContentWidth = logicalSafeContentWidth;
                    surfaceContext.layoutScale = uiScale;
                    surfaceContext.contentScale = contentScale;
                    surfaceContext.itemGap = itemGap;
                    surfaceContext.sectionGap = sectionGap;
                    surfaceContext.focused = selected;
                    surfaceContext.density = nodeSurfaceSpec.density;
                    surfaceContext.canvasToolActive = editor->GetCanvasToolOwnerNodeId() == node.id;
                    surfaceContext.canvasToolStatusText = editor->GetCanvasToolStatusText().empty()
                        ? nullptr
                        : editor->GetCanvasToolStatusText().c_str();
                    layer.RenderExpandedNodeSurface(editor, surfaceContext);
                } else {
                    layer.RenderUI(editor);
                }
            };
            if (m_RenderPreviewOnly) {
                ImGui::BeginDisabled();
                renderLayerControls(*(*layers)[node.layerIndex]);
                ImGui::EndDisabled();
            } else if (m_RenderCaptureOnly) {
                renderLayerControls(*(*layers)[node.layerIndex]);
            } else {
                editor->RenderLayerControlsWithDirtyTracking(node, [&](LayerBase& layer) {
                    renderLayerControls(layer);
                });
            }
        } else {
            ImGui::TextDisabled("Layer unavailable");
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Image) {
        const unsigned int texture = GetImagePreviewTexture(node);
        const ImVec2 imageSourceSize(
            static_cast<float>(std::max(1, node.image.width)),
            static_cast<float>(std::max(1, node.image.height)));
        const ImVec2 fittedPreviewSize = FitPreviewRect(previewSize, imageSourceSize);
        if (texture != 0) {
            const float indent = std::max(0.0f, (previewSize.x - fittedPreviewSize.x) * 0.5f);
            if (indent > 0.0f) {
                ImGui::Dummy(ImVec2(indent, 0.0f));
                ImGui::SameLine(0.0f, 0.0f);
            }
            ImGui::Image((ImTextureID)(intptr_t)texture, fittedPreviewSize, ImVec2(0, 1), ImVec2(1, 0));
            DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
            ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        } else {
            ImGui::Dummy(previewSize);
            DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
            ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        }
        if (node.image.width > 0 && node.image.height > 0) {
            ImGui::TextDisabled("%d x %d", node.image.width, node.image.height);
        }
        if (node.image.sourceColorMetadata.descriptor.logicalType ==
            Stack::NodeMath::LogicalValueType::ColorImage) {
            ImGui::TextWrapped(
                "%s",
                Stack::NodeMath::CompactDescriptorLabel(
                    node.image.sourceColorMetadata.descriptor).c_str());
        } else {
            ImGui::TextDisabled("Unknown color | Unknown transfer");
        }
        if (node.image.isLoading) {
            ImGui::TextDisabled("Loading slice...");
        } else if (node.image.isEmbedding) {
            ImGui::TextDisabled("Preparing portable source...");
        } else {
            ImGui::TextDisabled("%s", graph.GetActiveImageNodeId() == node.id ? "Active slice" : "Unconnected slice");
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
        editor->RenderRawSourceControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
        editor->RenderRawNeuralDenoiseControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::RawDecode) {
        editor->RenderRawDecodeControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::RawDevelop) {
        editor->RenderRawDevelopControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        const unsigned int texture = GetGraphPreviewTexture(editor, node);
        const ImVec2 graphPreviewSize = [&]() {
            auto it = m_GraphPreviewSizes.find(node.id);
            return it != m_GraphPreviewSizes.end() ? it->second : previewSize;
        }();
        const ImVec2 fittedPreviewSize = FitPreviewRect(previewSize, graphPreviewSize);
        if (texture != 0) {
            const float indent = std::max(0.0f, (previewSize.x - fittedPreviewSize.x) * 0.5f);
            if (indent > 0.0f) {
                ImGui::Dummy(ImVec2(indent, 0.0f));
                ImGui::SameLine(0.0f, 0.0f);
            }
            ImGui::Image((ImTextureID)(intptr_t)texture, fittedPreviewSize, ImVec2(0, 1), ImVec2(1, 0));
            DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
            ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        } else {
            ImGui::Dummy(previewSize);
            DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
            ImGui::TextDisabled("Auto mask preview unavailable");
        }
        editor->RenderRawDetailAutoMaskControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::RawDetailFusion) {
        editor->RenderRawDetailFusionControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::HdrMerge) {
        const EditorModule::HdrMergeNodeStatus status = editor->GetHdrMergeNodeStatus(node.id);
        for (const EditorModule::HdrMergeInputSummary& input : status.inputs) {
            if (input.label.empty() || (!input.active && input.socketId == EditorNodeGraph::kHdrMergeInput3SocketId)) {
                continue;
            }
            const std::string value = input.active
                ? input.sourceLabel
                : std::string("Inactive");
            ImGui::TextDisabled("%s", input.label.c_str());
            ImGui::TextWrapped("%s", value.c_str());
            if (!input.metadataSummary.empty()) {
                ImGui::TextDisabled("%s", input.metadataSummary.c_str());
            }
            if (!input.normalizationSummary.empty()) {
                ImGui::TextDisabled("%s", input.normalizationSummary.c_str());
            }
        }
        ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.45f));
        ImGui::TextDisabled("Status");
        if (status.state == EditorModule::HdrMergeRenderState::Failed ||
            status.state == EditorModule::HdrMergeRenderState::IncompatibleInput) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "%s", status.message.c_str());
        } else if (status.state == EditorModule::HdrMergeRenderState::BlockedMissingInput) {
            ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.42f, 1.0f), "%s", status.message.c_str());
        } else {
            ImGui::TextWrapped("%s", status.message.c_str());
        }
        if (!status.normalizationMessage.empty()) {
            ImGui::TextDisabled("Normalization");
            ImGui::TextWrapped("%s", status.normalizationMessage.c_str());
        }
        if (!status.reliabilityMessage.empty()) {
            ImGui::TextDisabled("Reliability");
            ImGui::TextWrapped("%s", status.reliabilityMessage.c_str());
        }
        if (!status.warningMessage.empty()) {
            ImGui::TextColored(ImVec4(0.92f, 0.76f, 0.42f, 1.0f), "%s", status.warningMessage.c_str());
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Mfsr) {
        ImGui::TextDisabled("Status");
        if (!node.mfsr.errorMessage.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "%s", node.mfsr.errorMessage.c_str());
        } else {
            ImGui::TextWrapped("%s", node.mfsr.placeholderStatus.c_str());
        }
        ImGui::TextDisabled("%s", node.mfsr.hasPlaceholderCachedOutput
            ? "Placeholder cache marked"
            : "No MFSR render cache");
    } else if (node.kind == EditorNodeGraph::NodeKind::Output) {
        bool outputUsesRgbaPins = false;
        for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
            if (socket.id == "r" || socket.id == "g" || socket.id == "b" || socket.id == "a") {
                outputUsesRgbaPins = true;
                break;
            }
        }
        ImGui::TextDisabled("Mode: %s", outputUsesRgbaPins ? "RGBA" : "Full");
        if (!node.outputEnabled) {
            ImGui::TextDisabled("This output is deactivated.");
        } else {
            ImGui::TextDisabled("%s", graph.IsOutputConnected() ? "Connected to output chain" : "Output is not connected");
        }
        Stack::NodeMath::ValueDescriptor outputDescriptor;
        if (editor->TryGetGraphOutputSemanticDescriptor(outputDescriptor)) {
            ImGui::TextWrapped(
                "Direct result: %s",
                Stack::NodeMath::CompactDescriptorLabel(outputDescriptor).c_str());
        }
        const std::string affectedOutput = "node-" + std::to_string(node.id);
        for (const Stack::NodeMath::Diagnostic& diagnostic : editor->GetGraphSemanticDiagnostics()) {
            if (diagnostic.affectedIdentity != affectedOutput ||
                diagnostic.severity == Stack::NodeMath::DiagnosticSeverity::Information) {
                continue;
            }
            ImGui::TextColored(
                ImVec4(0.94f, 0.73f, 0.36f, 1.0f),
                "%s", diagnostic.message.c_str());
        }
        if (node.outputEnabled && editor->OutputPathNeedsViewTransform(node.id)) {
            ImGui::TextWrapped("Scene-referred HDR input may clip at Output. Add or reconnect through View Transform for display compression.");
        }
        if (node.outputEnabled && graph.IsOutputConnected()) {
            RenderTextureStats outputStats = editor->GetPipeline().GetOutputTextureStats();
            if (outputStats.valid) {
                ImGui::TextDisabled("Rendered RGB %.3f to %.3f", outputStats.minRgb, outputStats.maxRgb);
                ImGui::TextDisabled("HDR > 1.0: %.1f%%   Display-edge pixels: %.1f%%",
                    outputStats.hdrPixelPercent,
                    outputStats.displayClipPercent);
            }
        }
        ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        ImGui::BeginDisabled(!node.outputEnabled || !graph.IsOutputConnected() || editor->IsExportBusy());
        if (ImGui::Button("Export", ImVec2(controlWidth, 0.0f))) {
            const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
            if (!path.empty()) {
                editor->RequestExportImage(path);
            }
        }
        ImGui::EndDisabled();
        if (ImGui::Button(node.outputEnabled ? "Deactivate Output" : "Activate Output", ImVec2(controlWidth, 0.0f))) {
            editor->ToggleOutputNodeEnabled(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Composite) {
        ImGui::TextDisabled("%d completed chains", editor->GetCompletedChainCount());
        ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        const std::vector<int>& zOrder = editor->GetCompositeZOrder();
        if (zOrder.empty()) {
            ImGui::TextDisabled("Add a second completed chain to enable the canvas stack.");
        } else {
            ImGui::BeginGroup();
            for (int outputNodeId : zOrder) {
                const EditorModule::CompositeSceneItem* item = editor->FindCompositeSceneItem(outputNodeId);
                const std::string label = item && !item->label.empty()
                    ? item->label
                    : ("Output " + std::to_string(outputNodeId));
                const std::string displayLabel = EllipsizeLabel(label, std::max(24.0f, controlWidth - (12.0f * uiScale)));
                const std::string selectableLabel = displayLabel + "##CompositeZ" + std::to_string(outputNodeId);
                const bool selected = editor->GetCompositeSelectedOutputNodeId() == outputNodeId;
                if (ImGui::Selectable(selectableLabel.c_str(), selected, 0)) {
                    editor->SetCompositeSelectedOutputNodeId(outputNodeId);
                }
                if (ImGui::BeginDragDropSource()) {
                    ImGui::SetDragDropPayload("CompositeZOrderItem", &outputNodeId, sizeof(outputNodeId));
                    ImGui::TextUnformatted(label.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CompositeZOrderItem")) {
                        const int draggedOutputNodeId = *static_cast<const int*>(payload->Data);
                        editor->ReorderCompositeOutputBefore(draggedOutputNodeId, outputNodeId);
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            ImGui::EndGroup();
        }

        drawInlineSeparator();
        EditorModule::CompositeSnapModePreset snapPreset = editor->GetCompositeSnapModePreset();
        static const EditorModule::CompositeSnapModePreset snapPresets[] = {
            EditorModule::CompositeSnapModePreset::Off,
            EditorModule::CompositeSnapModePreset::ObjectOnly,
            EditorModule::CompositeSnapModePreset::Full,
            EditorModule::CompositeSnapModePreset::Custom
        };
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::BeginCombo("Snap Mode", CompositeSnapPresetLabel(snapPreset))) {
            for (const auto preset : snapPresets) {
                const bool selected = preset == snapPreset;
                if (ImGui::Selectable(CompositeSnapPresetLabel(preset), selected)) {
                    editor->ApplyCompositeSnapModePreset(preset);
                    snapPreset = editor->GetCompositeSnapModePreset();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        auto& snapSettings = editor->GetMutableCompositeSnapSettings();
        ImGui::TextDisabled("Advanced snap tuning:");
        bool tuningChanged = false;
        tuningChanged |= ImGuiExtras::NodeCheckbox("Snap To Objects", "##SnapToObjects", &snapSettings.snapToObjects, controlWidth);
        tuningChanged |= ImGuiExtras::NodeCheckbox("Snap To Centers", "##SnapToCenters", &snapSettings.snapToCenters, controlWidth);
        tuningChanged |= ImGuiExtras::NodeCheckbox("Snap To Canvas Center", "##SnapToCanvasCenter", &snapSettings.snapToCanvasCenter, controlWidth);
        tuningChanged |= ImGuiExtras::NodeCheckbox("Snap To Export Bounds", "##SnapToExportBounds", &snapSettings.snapToExportBounds, controlWidth);
        captureIfActive();
        if (ImGuiExtras::NodeInputFloat("Rotate Step", "##RotateStep", &snapSettings.rotateSnapStep, 1.0f, 5.0f, "%.0f deg", controlWidth)) {
            snapSettings.rotateSnapStep = std::clamp(snapSettings.rotateSnapStep, 0.0f, 180.0f);
            if (snapSettings.rotateSnapStep > 0.0f) {
                snapSettings.lastNonZeroRotateSnapStep = snapSettings.rotateSnapStep;
            }
            tuningChanged = true;
        }
        captureIfActive();
        if (ImGuiExtras::NodeInputFloat("Scale Step", "##ScaleStep", &snapSettings.scaleSnapStep, 0.01f, 0.05f, "%.2f", controlWidth)) {
            snapSettings.scaleSnapStep = std::clamp(snapSettings.scaleSnapStep, 0.0f, 1.0f);
            if (snapSettings.scaleSnapStep > 0.0f) {
                snapSettings.lastNonZeroScaleSnapStep = snapSettings.scaleSnapStep;
            }
            tuningChanged = true;
        }
        if (tuningChanged) {
            snapSettings.enabled =
            snapSettings.snapToObjects ||
            snapSettings.snapToCenters ||
            snapSettings.snapToCanvasCenter ||
            snapSettings.snapToExportBounds ||
            snapSettings.rotateSnapStep > 0.0f ||
            snapSettings.scaleSnapStep > 0.0f;
        }
        drawInlineSeparator();
        ImGui::TextDisabled(
            "Resize Mode: %s (%s)",
            editor->GetCompositeResizeMode() == EditorModule::CompositeResizeMode::Stretch ? "Stretch" : "Scale",
            editor->GetCompositeScaleOriginMode() == EditorModule::CompositeScaleOriginMode::Center ? "Center" : "Opposite");

    } else if (node.kind == EditorNodeGraph::NodeKind::Scope) {
        const EditorNodeGraph::Link* input = graph.FindScopeInputLink(node.id);
        if (input) {
            const EditorNodeGraph::Node* from = FindCachedNode(graph, input->fromNodeId);
            ImGui::TextDisabled("Input: %s", from ? from->title.c_str() : "Missing");
        } else {
            ImGui::TextDisabled("No input");
        }
        ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.5f));
        editor->RenderGraphScopeNode(node.scopeKind, input ? input->fromNodeId : -1);
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator) {
        bool changed = false;
        if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::Solid) {
            changed |= renderSlider("Value", "##SolidValue", &node.maskSettings.value, 0.0f, 1.0f);
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
            changed |= renderSlider("Angle", "##LinearAngle", &node.maskSettings.angle, -180.0f, 180.0f);
            changed |= renderSlider("Offset", "##LinearOffset", &node.maskSettings.offset, -1.0f, 1.0f);
            changed |= renderSlider("Scale", "##LinearScale", &node.maskSettings.scale, 0.1f, 4.0f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##LinearInvert", &node.maskSettings.invert, controlWidth);
            captureIfActive();
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient) {
            changed |= renderSlider("Center X", "##RadialCenterX", &node.maskSettings.centerX, 0.0f, 1.0f);
            changed |= renderSlider("Center Y", "##RadialCenterY", &node.maskSettings.centerY, 0.0f, 1.0f);
            changed |= renderSlider("Radius", "##RadialRadius", &node.maskSettings.radius, 0.01f, 1.5f);
            changed |= renderSlider("Feather", "##RadialFeather", &node.maskSettings.feather, 0.001f, 1.0f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##RadialInvert", &node.maskSettings.invert, controlWidth);
            captureIfActive();
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::Noise) {
            changed |= renderSlider("Scale", "##NoiseScale", &node.maskSettings.scale, 0.05f, 8.0f);
            changed |= renderSlider("Contrast", "##NoiseContrast", &node.maskSettings.value, 0.0f, 2.0f);
            changed |= renderSlider("Seed", "##NoiseSeed", &node.maskSettings.offset, 0.0f, 100.0f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##NoiseInvert", &node.maskSettings.invert, controlWidth);
            captureIfActive();
        }
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskCombine) {
        bool changed = false;
        int mode = static_cast<int>(node.maskCombineMode);
        const char* modes[] = { "Add", "Subtract", "Intersect", "Difference" };
        if (ImGuiExtras::NodeCombo("Mode", "##MaskCombineMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.maskCombineMode = static_cast<EditorNodeGraph::MaskCombineMode>(std::clamp(mode, 0, 3));
            changed = true;
        }
        ImGui::TextDisabled("Mask A is the base. Mask B refines it.");
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::DataMath) {
        bool changed = false;
        int mode = static_cast<int>(node.dataMathMode);
        const char* modes[] = { "Clamp", "Add", "Subtract", "Multiply", "Divide", "Average", "Minimum", "Maximum", "Difference", "Remap", "Average Images" };
        if (ImGuiExtras::NodeCombo("Mode", "##DataMathMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.dataMathMode = static_cast<EditorNodeGraph::DataMathMode>(std::clamp(mode, 0, 10));
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            if (node.dataMathMode == EditorNodeGraph::DataMathMode::Average ||
                node.dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage) {
                std::vector<EditorNodeGraph::Link> linksToRemove;
                for (const EditorNodeGraph::Link& link : editor->GetNodeGraph().GetLinks()) {
                    if (link.toNodeId == node.id &&
                        (link.toSocketId == EditorNodeGraph::kDataMathBaseInputSocketId ||
                         link.toSocketId == EditorNodeGraph::kMaskInputSocketId)) {
                        linksToRemove.push_back(link);
                    }
                }
                for (const EditorNodeGraph::Link& link : linksToRemove) {
                    editor->RemoveGraphLink(link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId);
                }
            }
            changed = true;
        }
        ImGui::TextDisabled("%s", node.dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage
            ? "Averages 2+ full image inputs only."
            : "Works on scalar fields or images.");
        if (node.dataMathMode == EditorNodeGraph::DataMathMode::Average) {
            ImGui::TextDisabled("Outputs one scalar field from any number of scalar inputs.");
        } else if (node.dataMathMode == EditorNodeGraph::DataMathMode::ImageAverage) {
            ImGui::TextDisabled("Press G to split into per-channel averages.");
        }
        if (node.dataMathMode == EditorNodeGraph::DataMathMode::Clamp) {
            changed |= renderSlider("Min", "##DataMathMin", &node.dataMathSettings.minValue, -4.0f, 4.0f);
            changed |= renderSlider("Max", "##DataMathMax", &node.dataMathSettings.maxValue, -4.0f, 4.0f);
        } else if (node.dataMathMode == EditorNodeGraph::DataMathMode::Remap) {
            changed |= renderSlider("In Min", "##DataMathInMin", &node.dataMathSettings.minValue, -4.0f, 4.0f);
            changed |= renderSlider("In Max", "##DataMathInMax", &node.dataMathSettings.maxValue, -4.0f, 4.0f);
            changed |= renderSlider("Out Min", "##DataMathOutMin", &node.dataMathSettings.outMin, -4.0f, 4.0f);
            changed |= renderSlider("Out Max", "##DataMathOutMax", &node.dataMathSettings.outMax, -4.0f, 4.0f);
        }
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::FrequencyFft ||
               node.kind == EditorNodeGraph::NodeKind::FrequencyIfft) {
        bool changed = false;
        EditorNodeGraph::FrequencyFftSettings& settings =
            node.kind == EditorNodeGraph::NodeKind::FrequencyFft
                ? node.frequencyFftSettings
                : node.frequencyIfftSettings;
        changed |= ImGuiExtras::NodeCheckbox("Luminance", "##FrequencyLuminance", &settings.luminanceOnly, controlWidth);
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumView) {
        bool changed = false;
        int lut = static_cast<int>(node.spectrumViewSettings.lut);
        const char* luts[] = { "Turbo", "Viridis", "Inferno", "Grayscale" };
        if (ImGuiExtras::NodeCombo("LUT", "##SpectrumViewLut", &lut, luts, IM_ARRAYSIZE(luts), controlWidth)) {
            node.spectrumViewSettings.lut = static_cast<EditorNodeGraph::SpectrumViewLut>(std::clamp(lut, 0, 3));
            changed = true;
        }
        changed |= renderSlider("Exposure", "##SpectrumViewExposure", &node.spectrumViewSettings.exposure, 0.01f, 8.0f);
        changed |= renderSlider("Gamma", "##SpectrumViewGamma", &node.spectrumViewSettings.gamma, 0.2f, 3.0f);
        changed |= ImGuiExtras::NodeCheckbox("Center DC", "##SpectrumViewCenterDc", &node.spectrumViewSettings.centerDc, controlWidth);
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::FrequencyMask) {
        bool changed = false;
        int shape = static_cast<int>(node.frequencyMaskShape);
        const char* shapes[] = { "Low Pass", "High Pass", "Band Pass", "Band Stop", "Notch", "Gaussian", "Butterworth" };
        if (ImGuiExtras::NodeCombo("Shape", "##FrequencyMaskShape", &shape, shapes, IM_ARRAYSIZE(shapes), controlWidth)) {
            node.frequencyMaskShape = static_cast<EditorNodeGraph::FrequencyMaskShape>(std::clamp(shape, 0, 6));
            node.frequencyMaskSettings.shape = node.frequencyMaskShape;
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            changed = true;
        }
        node.frequencyMaskSettings.shape = node.frequencyMaskShape;
        changed |= renderSlider("Cutoff", "##FrequencyMaskCutoff", &node.frequencyMaskSettings.cutoff, 0.0f, 1.0f);
        changed |= renderSlider("Width", "##FrequencyMaskWidth", &node.frequencyMaskSettings.width, 0.0f, 1.0f);
        changed |= renderSlider("Feather", "##FrequencyMaskFeather", &node.frequencyMaskSettings.feather, 0.0f, 0.5f);
        if (node.frequencyMaskShape == EditorNodeGraph::FrequencyMaskShape::Butterworth) {
            changed |= renderSlider("Order", "##FrequencyMaskOrder", &node.frequencyMaskSettings.order, 1.0f, 12.0f);
        }
        if (node.frequencyMaskShape == EditorNodeGraph::FrequencyMaskShape::Notch) {
            changed |= renderSlider("Center X", "##FrequencyMaskCenterX", &node.frequencyMaskSettings.centerX, 0.0f, 1.0f);
            changed |= renderSlider("Center Y", "##FrequencyMaskCenterY", &node.frequencyMaskSettings.centerY, 0.0f, 1.0f);
        }
        changed |= ImGuiExtras::NodeCheckbox("Invert", "##FrequencyMaskInvert", &node.frequencyMaskSettings.invert, controlWidth);
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumMath) {
        bool changed = false;
        int mode = static_cast<int>(node.spectrumMathMode);
        const char* modes[] = { "Multiply", "Add", "Subtract", "Difference" };
        if (ImGuiExtras::NodeCombo("Mode", "##SpectrumMathMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.spectrumMathMode = static_cast<EditorNodeGraph::SpectrumMathMode>(std::clamp(mode, 0, 3));
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            changed = true;
        }
        changed |= renderSlider("Amount", "##SpectrumMathAmount", &node.spectrumMathSettings.amount, 0.0f, 4.0f);
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::MagnitudePhase) {
        bool changed = false;
        int mode = static_cast<int>(node.magnitudePhaseMode);
        const char* modes[] = { "Magnitude", "Phase", "Recombine" };
        if (ImGuiExtras::NodeCombo("Mode", "##MagnitudePhaseMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.magnitudePhaseMode = static_cast<EditorNodeGraph::MagnitudePhaseMode>(std::clamp(mode, 0, 2));
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            changed = true;
        }
        changed |= renderSlider("Exposure", "##MagnitudePhaseExposure", &node.magnitudePhaseSettings.exposure, 0.01f, 8.0f);
        changed |= renderSlider("Gamma", "##MagnitudePhaseGamma", &node.magnitudePhaseSettings.gamma, 0.2f, 3.0f);
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumAnalyzer) {
        bool changed = false;
        changed |= renderSlider("Inner", "##SpectrumAnalyzerInner", &node.spectrumAnalyzerSettings.innerRadius, 0.0f, 1.0f);
        changed |= renderSlider("Outer", "##SpectrumAnalyzerOuter", &node.spectrumAnalyzerSettings.outerRadius, 0.0f, 1.0f);
        if (node.spectrumAnalyzerSettings.outerRadius < node.spectrumAnalyzerSettings.innerRadius) {
            std::swap(node.spectrumAnalyzerSettings.innerRadius, node.spectrumAnalyzerSettings.outerRadius);
            changed = true;
        }
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Lut) {
        const bool hasLutData = ColorLut::HasAnyLutData(node.lut);
        ImGui::TextDisabled("%s", hasLutData
            ? (node.lut.label.empty() ? "Loaded LUT" : node.lut.label.c_str())
            : "No LUT loaded");
        ImGui::TextDisabled("%s", ColorLut::LutTypeSummary(node.lut));
        if (!node.lut.importError.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "%s", node.lut.importError.c_str());
        } else {
            ImGui::TextDisabled("%s", ColorLut::LutImportFormatLabel(node.lut.importFormat));
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::CustomMask) {
        ImGui::TextDisabled("%d x %d grayscale mask", node.customMask.width, node.customMask.height);
        ImGui::TextDisabled("%zu object%s", node.customMask.objects.size(), node.customMask.objects.size() == 1 ? "" : "s");
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskUtility) {
        bool changed = false;
        if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert) {
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##InvertMaskEnabled", &node.maskUtilitySettings.enabled, controlWidth);
        } else if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Levels) {
            changed |= renderSlider("Black", "##LevelsBlack", &node.maskUtilitySettings.blackPoint, 0.0f, 1.0f);
            changed |= renderSlider("White", "##LevelsWhite", &node.maskUtilitySettings.whitePoint, 0.0f, 1.0f);
            changed |= renderSlider("Gamma", "##LevelsGamma", &node.maskUtilitySettings.gamma, 0.1f, 4.0f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##LevelsInvert", &node.maskUtilitySettings.invert, controlWidth);
            captureIfActive();
        } else if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Threshold) {
            changed |= renderSlider("Threshold", "##Threshold", &node.maskUtilitySettings.threshold, 0.0f, 1.0f);
            changed |= renderSlider("Softness", "##ThresholdSoftness", &node.maskUtilitySettings.softness, 0.0f, 0.5f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##ThresholdInvert", &node.maskUtilitySettings.invert, controlWidth);
            captureIfActive();
        }
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::ImageToMask) {
        bool changed = false;
        if (node.imageToMaskKind == EditorNodeGraph::ImageToMaskKind::SampledRange) {
            changed |= renderSlider("Tone Similarity", "##SampledRangeToneSimilarity", &node.imageToMaskSettings.toneSimilarity, 0.02f, 0.35f);
            changed |= renderSlider("Color Similarity", "##SampledRangeColorSimilarity", &node.imageToMaskSettings.colorSimilarity, 0.02f, 0.50f);
            changed |= renderSlider("Area Radius", "##SampledRangeRegionRadius", &node.imageToMaskSettings.regionRadius, 0.05f, 1.0f);
            changed |= renderSlider("Region Feather", "##SampledRangeRegionFeather", &node.imageToMaskSettings.regionFeather, 0.0f, 1.0f);
            changed |= renderSlider("Edge Sensitivity", "##SampledRangeEdgeSensitivity", &node.imageToMaskSettings.edgeSensitivity, 0.0f, 1.0f);
            changed |= renderSlider("Local Coherence", "##SampledRangeLocalCoherence", &node.imageToMaskSettings.localCoherence, 0.0f, 1.0f);
            changed |= renderSlider("Softness", "##SampledRangeSoftness", &node.imageToMaskSettings.softness, 0.0f, 0.5f);
            ImGui::TextDisabled(
                "Seed RGB: %.3f %.3f %.3f",
                node.imageToMaskSettings.sampleRgb[0],
                node.imageToMaskSettings.sampleRgb[1],
                node.imageToMaskSettings.sampleRgb[2]);
            ImGui::TextDisabled(
                "Seed Luma / UV: %.3f  (%.3f, %.3f)",
                node.imageToMaskSettings.sampleLuma,
                node.imageToMaskSettings.sampleU,
                node.imageToMaskSettings.sampleV);
            ImGui::TextDisabled("Samples: %d / 5", std::clamp(node.imageToMaskSettings.sampleCount, 1, 5));
            for (int i = 0; i < std::max(0, std::clamp(node.imageToMaskSettings.sampleCount, 1, 5) - 1); ++i) {
                ImGui::TextDisabled(
                    "Extra %d: RGB %.3f %.3f %.3f  Luma %.3f",
                    i + 2,
                    node.imageToMaskSettings.extraSampleRgb[i][0],
                    node.imageToMaskSettings.extraSampleRgb[i][1],
                    node.imageToMaskSettings.extraSampleRgb[i][2],
                    node.imageToMaskSettings.extraSampleLuma[i]);
            }
            if (node.imageToMaskSettings.sampleCount > 1 && ImGui::Button("Remove Last Sample")) {
                const int removeIndex = std::clamp(node.imageToMaskSettings.sampleCount - 2, 0, 3);
                node.imageToMaskSettings.extraSampleRgb[removeIndex][0] = 0.5f;
                node.imageToMaskSettings.extraSampleRgb[removeIndex][1] = 0.5f;
                node.imageToMaskSettings.extraSampleRgb[removeIndex][2] = 0.5f;
                node.imageToMaskSettings.extraSampleLuma[removeIndex] = 0.5f;
                node.imageToMaskSettings.sampleCount -= 1;
                changed = true;
            }
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##SampledRangeInvert", &node.imageToMaskSettings.invert, controlWidth);
        } else {
            changed |= renderSlider("Low", "##LumLow", &node.imageToMaskSettings.low, 0.0f, 1.0f);
            changed |= renderSlider("High", "##LumHigh", &node.imageToMaskSettings.high, 0.0f, 1.0f);
            changed |= renderSlider("Softness", "##LumSoftness", &node.imageToMaskSettings.softness, 0.0f, 0.5f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##ImageToMaskInvert", &node.imageToMaskSettings.invert, controlWidth);
        }
        captureIfActive();
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Value) {
        bool changed = false;
        Stack::NodeMath::FirstClassValue& value = node.value.value;
        using Availability = Stack::NodeMath::ValueAvailability;
        using Logical = Stack::NodeMath::LogicalValueType;
        if (value.availability != Availability::Known) {
            const char* state = value.availability == Availability::Missing ? "Missing" :
                value.availability == Availability::Failure ? "Failure" : "Unknown";
            ImGui::TextDisabled("%s value", state);
            if (!value.message.empty()) ImGui::TextWrapped("%s", value.message.c_str());
        } else if (value.logicalType == Logical::Boolean) {
            if (bool* item = std::get_if<bool>(&value.payload)) {
                changed |= ImGuiExtras::NodeCheckbox("Value", "##TypedBoolean", item, controlWidth);
            }
        } else if (value.logicalType == Logical::Integer) {
            if (std::int64_t* item = std::get_if<std::int64_t>(&value.payload)) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputScalar("##TypedInteger", ImGuiDataType_S64, item);
            }
        } else if (value.logicalType == Logical::Scalar) {
            if (double* item = std::get_if<double>(&value.payload)) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputDouble("##TypedScalar", item, 0.0, 0.0, "%.6f");
            }
        } else if (auto* item2 = std::get_if<std::array<double, 2>>(&value.payload)) {
            const char* labels[] = { "X", "Y" };
            for (int i = 0; i < 2; ++i) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputDouble((std::string(labels[i]) + "##Typed2" + std::to_string(i)).c_str(), &(*item2)[i], 0.0, 0.0, "%.6f");
            }
        } else if (auto* item3 = std::get_if<std::array<double, 3>>(&value.payload)) {
            for (int i = 0; i < 3; ++i) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputDouble(("Component " + std::to_string(i + 1) + "##Typed3").c_str(), &(*item3)[i], 0.0, 0.0, "%.6f");
            }
        } else if (auto* item4 = std::get_if<std::array<double, 4>>(&value.payload)) {
            for (int i = 0; i < 4; ++i) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputDouble(("Component " + std::to_string(i + 1) + "##Typed4").c_str(), &(*item4)[i], 0.0, 0.0, "%.6f");
            }
        } else if (auto* matrix3 = std::get_if<std::array<double, 9>>(&value.payload)) {
            for (int row = 0; row < 3; ++row) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputScalarN(("Row " + std::to_string(row + 1) + "##Matrix3").c_str(), ImGuiDataType_Double, matrix3->data() + row * 3, 3);
            }
        } else if (auto* matrix4 = std::get_if<std::array<double, 16>>(&value.payload)) {
            for (int row = 0; row < 4; ++row) {
                ImGui::SetNextItemWidth(controlWidth);
                changed |= ImGui::InputScalarN(("Row " + std::to_string(row + 1) + "##Matrix4").c_str(), ImGuiDataType_Double, matrix4->data() + row * 4, 4);
            }
        } else if (const auto* curve = std::get_if<Stack::NodeMath::CurveValue>(&value.payload)) {
            ImGui::TextDisabled("Curve: %d control points", static_cast<int>(curve->points.size()));
            ImGui::TextDisabled("%s interpolation / %s extrapolation", curve->interpolation.c_str(), curve->extrapolation.c_str());
        }
        if (changed) {
            for (const EditorNodeGraph::Link& link : editor->GetNodeGraph().GetLinks()) {
                if (link.fromNodeId == node.id && link.fromSocketId == EditorNodeGraph::kValueOutputSocketId) {
                    editor->MarkRenderDirty(link.toNodeId);
                }
            }
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Compound) {
        const Stack::NodeMath::CompoundDefinition* definition =
            graph.FindCompoundDefinition(node.compound.instance.definition);
        if (!definition || !node.definitionResolved) {
            ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.42f, 1.0f), "Unresolved definition");
            if (!node.definitionResolutionError.empty()) {
                ImGui::TextWrapped("%s", node.definitionResolutionError.c_str());
            }
        } else {
            ImGui::TextDisabled("%s · v%s",
                Stack::NodeMath::CompoundDefinitionClassName(definition->definitionClass),
                Stack::NodeMath::ToString(definition->identity.version).c_str());
            bool changed = false;
            int parameterIndex = 0;
            for (const Stack::NodeMath::CompoundParameterDefinition& parameter : definition->parameters) {
                if (parameter.type != Stack::NodeMath::ParameterType::Scalar) continue;
                const Stack::NodeMath::ParameterValue resolved =
                    Stack::NodeMath::ResolveCompoundParameterValue(node.compound.instance, parameter);
                const double* scalar = std::get_if<double>(&resolved);
                if (!scalar) continue;
                float value = static_cast<float>(*scalar);
                const float minimum = parameter.hardDomain.applicable
                    ? static_cast<float>(parameter.hardDomain.minimum) : -16.0f;
                const float maximum = parameter.hardDomain.applicable
                    ? static_cast<float>(parameter.hardDomain.maximum) : 16.0f;
                const std::string widgetId = "##CompoundParameter" + std::to_string(parameterIndex++);
                if (renderSlider(parameter.label.c_str(), widgetId.c_str(), &value, minimum, maximum)) {
                    Stack::NodeMath::SetCompoundParameterOverride(
                        node.compound.instance, parameter.id, static_cast<double>(value));
                    changed = true;
                }
            }
            if (definition->parameters.empty()) ImGui::TextDisabled("No promoted controls");
            if (changed) editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::FieldMean) {
        ImGui::TextDisabled("Arithmetic mean of every field pixel");
        ImGui::TextDisabled("Full-frame reduction · strict finite values");
    } else if (node.kind == EditorNodeGraph::NodeKind::Reformat) {
        bool changed = false;
        ImGui::SetNextItemWidth(controlWidth);
        changed |= ImGui::InputInt("Width##ReformatWidth", &node.reformatSettings.width, 1, 128);
        ImGui::SetNextItemWidth(controlWidth);
        changed |= ImGui::InputInt("Height##ReformatHeight", &node.reformatSettings.height, 1, 128);
        node.reformatSettings.width = std::clamp(
            node.reformatSettings.width, 1, Stack::NodeMath::kMaximumReformatDimension);
        node.reformatSettings.height = std::clamp(
            node.reformatSettings.height, 1, Stack::NodeMath::kMaximumReformatDimension);
        const char* filters[] = { "Nearest", "Linear" };
        int filter = node.reformatSettings.filter == Stack::NodeMath::ReconstructionFilter::Nearest ? 0 : 1;
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::Combo("Filter##ReformatFilter", &filter, filters, 2)) {
            node.reformatSettings.filter = filter == 0
                ? Stack::NodeMath::ReconstructionFilter::Nearest
                : Stack::NodeMath::ReconstructionFilter::Linear;
            changed = true;
        }
        ImGui::TextDisabled("Pixel centers - Clamp border");
        ImGui::TextDisabled("No color or alpha conversion");
        if (changed) editor->MarkRenderDirty(node.id);
    } else if (node.kind == EditorNodeGraph::NodeKind::TechnicalImage) {
        bool changed = false;
        using Operation = Stack::NodeMath::TechnicalImageOperation;
        if (node.technicalImageSettings.operation == Operation::Exposure) {
            const bool hasValueInput = editor->GetNodeGraph().FindAnyInputLink(
                node.id, EditorNodeGraph::kExposureValueInputSocketId) != nullptr;
            ImGui::BeginDisabled(hasValueInput);
            changed |= renderSlider(
                hasValueInput ? "Fallback Exposure" : "Exposure", "##TechnicalExposureEV",
                &node.technicalImageSettings.exposureValue, -16.0f, 16.0f);
            ImGui::EndDisabled();
            if (hasValueInput) ImGui::TextDisabled("Driven by connected Scalar Value");
            ImGui::TextDisabled("Stops (EV); RGB only, alpha unchanged");
        } else if (node.technicalImageSettings.operation == Operation::AssignSrgb ||
                   node.technicalImageSettings.operation == Operation::AssignLinearSrgb ||
                   node.technicalImageSettings.operation == Operation::AssignLinearDisplayP3) {
            ImGui::TextDisabled("Metadata only; pixels are unchanged");
        } else if (node.technicalImageSettings.operation == Operation::Unpremultiply) {
            ImGui::TextDisabled("Alpha <= 0.000001 becomes transparent black");
        } else {
            ImGui::TextDisabled("Explicit RGB transform; alpha unchanged");
        }
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::ImageGenerator) {
        bool changed = false;
        if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Text) {
            changed |= ImGuiExtras::NodeTextMultiline("Text", "##GeneratorText", node.imageGeneratorSettings.text, controlWidth, 4);
            captureIfActive();
            changed |= ImGuiExtras::NodeColorEdit4("Color", "##GeneratorColorA", node.imageGeneratorSettings.colorA, ImGuiColorEditFlags_NoInputs, controlWidth);
            captureIfActive();
            changed |= renderSlider("Size", "##GeneratorFontSize", &node.imageGeneratorSettings.fontSize, 16.0f, 192.0f);
            changed |= renderSlider("Backdrop Blur", "##GeneratorBackdropBlur", &node.imageGeneratorSettings.textBackdropBlur, 0.0f, 96.0f);
            changed |= renderSlider("Backdrop Opacity", "##GeneratorBackdropOpacity", &node.imageGeneratorSettings.textBackdropOpacity, 0.0f, 1.0f);
            changed |= renderSlider("Backdrop Padding", "##GeneratorBackdropPadding", &node.imageGeneratorSettings.textBackdropPadding, 0.0f, 96.0f);
        } else {
            changed |= ImGuiExtras::NodeColorEdit4("A", "##GeneratorColorA", node.imageGeneratorSettings.colorA, ImGuiColorEditFlags_NoInputs, controlWidth);
            captureIfActive();
        }
        if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::ColorGradient) {
            changed |= ImGuiExtras::NodeColorEdit4("B", "##GeneratorColorB", node.imageGeneratorSettings.colorB, ImGuiColorEditFlags_NoInputs, controlWidth);
            captureIfActive();
            changed |= renderSlider("Angle", "##GradientAngle", &node.imageGeneratorSettings.angle, -180.0f, 180.0f);
            changed |= renderSlider("Offset", "##GradientOffset", &node.imageGeneratorSettings.offset, -1.0f, 1.0f);
        }
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Mix) {
        bool changed = false;
        int mode = static_cast<int>(node.mixBlendMode);
        const char* modes[] = {
            "Normal / Lerp", "Average", "Add", "Multiply", "Screen",
            "Source Over (Straight)", "Source Over (Premultiplied)"
        };
        if (ImGuiExtras::NodeCombo("Blend", "##MixBlendMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.mixBlendMode = static_cast<EditorNodeGraph::MixBlendMode>(mode);
            changed = true;
        }
        captureIfActive();
        changed |= renderSlider("Factor", "##MixFactor", &node.mixFactor, 0.0f, 1.0f);
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Preview) {
        const EditorNodeGraph::Link* input = graph.FindAnyInputLink(node.id, EditorNodeGraph::kPreviewInputSocketId);
        if (input) {
            const EditorNodeGraph::Node* from = FindCachedNode(graph, input->fromNodeId);
            ImGui::TextDisabled("Input: %s", from ? from->title.c_str() : "Missing");
            ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.5f));
            const unsigned int texture = GetGraphPreviewTexture(editor, node);
            const ImVec2 graphPreviewSize = [&]() {
                auto it = m_GraphPreviewSizes.find(node.id);
                return it != m_GraphPreviewSizes.end() ? it->second : previewSize;
            }();
            const ImVec2 fittedPreviewSize = FitPreviewRect(previewSize, graphPreviewSize);
            if (texture != 0) {
                const float indent = std::max(0.0f, (previewSize.x - fittedPreviewSize.x) * 0.5f);
                if (indent > 0.0f) {
                    ImGui::Dummy(ImVec2(indent, 0.0f));
                    ImGui::SameLine(0.0f, 0.0f);
                }
                ImGui::Image((ImTextureID)(intptr_t)texture, fittedPreviewSize, ImVec2(0, 1), ImVec2(1, 0));
                DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
            } else {
                ImGui::Dummy(previewSize);
                DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
                ImGui::TextDisabled("Preview unavailable");
            }
        } else {
            ImGui::TextDisabled("No input");
            ImGui::Dummy(previewSize);
            DrawPreviewFrame(drawList, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), graphStyle, uiScale);
        }
    }
    const ImVec2 contentCursorMax = ImGui::GetCursorScreenPos();
    ImGui::EndGroup();
    const ImVec2 groupUsedMax = ImGui::GetItemRectMax();
    const ImVec2 contentUsedMax(
        std::max(groupUsedMax.x, contentCursorMax.x),
        std::max(groupUsedMax.y, contentCursorMax.y));
    const bool contentHovered =
        !m_MiddlePanCaptureActive &&
        ImGui::IsMouseHoveringRect(contentUsedMin, contentUsedMax, false);
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const float bottomSafetyPadding = std::max(8.0f, metrics.itemGap * 0.75f);
    const float renderedHeightPixels =
        std::max(0.0f, contentUsedMax.y - min.y) +
        ((metrics.bodyInsetBottom + bottomSafetyPadding) * uiScale);
    const float renderedBaseHeight =
        std::round(renderedHeightPixels) / std::max(0.001f, uiScale);
    if (expanded && UsesMeasuredNodeHeight(node)) {
        m_NodeMeasuredBaseHeights[node.id] = std::max(
            metrics.minExpandedHeight,
            SanitizeFinite(renderedBaseHeight, metrics.minExpandedHeight));
    }
    RefreshNodeLayoutCache(graph, node);
    NodeLayoutCache& refreshedLayout = m_NodeLayoutCache[node.id];
    refreshedLayout.contentUsedRect = CachedRect{ contentUsedMin, contentUsedMax };
    m_NodeContentOverflow[node.id] = refreshedLayout.contentUsedRect.IsValid() &&
        refreshedLayout.contentUsedRect.max.y > refreshedLayout.frameRect.max.y - ((metrics.bodyInsetBottom + bottomSafetyPadding) * uiScale * 0.35f);
    const ImGuiExtras::NodeControlState& nodeControlState = ImGuiExtras::GetNodeControlState();
    if (contentHovered ||
        (!m_MiddlePanCaptureActive && refreshedLayout.contentUsedRect.Contains(ImGui::GetMousePos()))) {
        m_NodeContentHovered = true;
    }
    if (nodeControlState.hovered ||
        nodeControlState.active ||
        nodeControlState.edited ||
        nodeControlState.popupOpen ||
        (anyPopupOpen &&
         !m_MiddlePanCaptureActive &&
         refreshedLayout.contentUsedRect.Contains(ImGui::GetMousePos()))) {
        m_NodeContentHovered |= nodeControlState.hovered || contentHovered;
        m_NodeContentActive |= nodeControlState.active || nodeControlState.edited || nodeControlState.popupOpen;
        m_LastNodeControlId = nodeControlState.id;
    }
    ImGui::PopItemWidth();
    ImGuiExtras::EndGraphNodeControlScope();
    if (fontScalePushed) {
        ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::PopStyleColor(15);
    ImGui::PopStyleVar(7);
    ImGui::PopID();
}
