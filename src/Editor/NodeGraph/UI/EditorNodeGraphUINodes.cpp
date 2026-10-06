#include "Utils/GraphCursor.h"
#include "App/settings/NodeControlStyle.h"
#include "Utils/GraphNumericControls.h"
#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/SocketPresentation.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"
#include "Editor/NodeGraph/UI/NodeNumericDefaults.h"
#include "NodeMath/SemanticSpine.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <limits>
#include <string>
#include <vector>

namespace {

float SanitizeFinite(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
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

float EvaluateFrequencyResponsePreview(
    const EditorNodeGraph::FrequencyResponseSettings& settings,
    float fx,
    float fy) {
    const float radius = std::sqrt(fx * fx + fy * fy);
    const auto lowPass = [&](float cutoff) {
        cutoff = std::clamp(cutoff, 0.000001f, 0.70710678f);
        const float transition = std::max(settings.transitionWidth, 0.000001f);
        switch (settings.profile) {
            case EditorNodeGraph::FrequencyTransitionProfile::Gaussian: {
                const float sigma = cutoff / std::sqrt(2.0f * std::log(2.0f));
                return std::exp(-0.5f * radius * radius /
                    std::max(sigma * sigma, 0.0000001f));
            }
            case EditorNodeGraph::FrequencyTransitionProfile::Butterworth:
                return 1.0f / (1.0f + std::pow(
                    radius / cutoff,
                    2.0f * std::clamp(settings.butterworthOrder, 1.0f, 12.0f)));
            case EditorNodeGraph::FrequencyTransitionProfile::Hard:
                return radius <= cutoff ? 1.0f : 0.0f;
            case EditorNodeGraph::FrequencyTransitionProfile::Smooth:
            default: {
                const float lo = cutoff - transition * 0.5f;
                const float hi = cutoff + transition * 0.5f;
                const float t = std::clamp((radius - lo) / std::max(hi - lo, 0.000001f), 0.0f, 1.0f);
                const float smooth = t * t * (3.0f - 2.0f * t);
                return 1.0f - smooth;
            }
        }
    };

    float value = 1.0f;
    switch (settings.mode) {
        case EditorNodeGraph::FrequencyFilterMode::LowPass:
            value = lowPass(settings.lowCutoff);
            break;
        case EditorNodeGraph::FrequencyFilterMode::HighPass:
            value = 1.0f - lowPass(settings.lowCutoff);
            break;
        case EditorNodeGraph::FrequencyFilterMode::BandPass:
            value = (1.0f - lowPass(settings.lowCutoff)) * lowPass(settings.highCutoff);
            break;
        case EditorNodeGraph::FrequencyFilterMode::BandStop:
            value = 1.0f - (1.0f - lowPass(settings.lowCutoff)) * lowPass(settings.highCutoff);
            break;
        case EditorNodeGraph::FrequencyFilterMode::NotchReject:
            for (const EditorNodeGraph::FrequencyNotch& notch : settings.notches) {
                const float radians = notch.directionDegrees * 3.14159265358979323846f / 180.0f;
                const float nx = notch.frequency * std::cos(radians);
                const float ny = notch.frequency * std::sin(radians);
                const float d1 = std::hypot(fx - nx, fy - ny);
                const float d2 = std::hypot(fx + nx, fy + ny);
                const float distance = std::min(d1, d2);
                const float width = std::max(notch.width, 0.000001f);
                const float t = std::clamp(
                    (distance - width) /
                    std::max(settings.transitionWidth, 0.000001f),
                    0.0f, 1.0f);
                value *= t * t * (3.0f - 2.0f * t);
            }
            break;
        case EditorNodeGraph::FrequencyFilterMode::AllPass:
        default:
            break;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

bool DrawFrequencyResponsePreview(
    EditorNodeGraph::FrequencyResponseSettings& settings,
    float availableWidth,
    bool interactive,
    unsigned int spectrumTexture = 0) {
    const float size = std::clamp(availableWidth, 72.0f, 180.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##FrequencyResponsePreview", ImVec2(size, size));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (spectrumTexture != 0) {
        drawList->AddImage(
            static_cast<ImTextureID>(spectrumTexture),
            origin,
            ImVec2(origin.x + size, origin.y + size));
    }
    constexpr int cells = 32;
    const float cellSize = size / static_cast<float>(cells);
    for (int y = 0; y < cells; ++y) {
        for (int x = 0; x < cells; ++x) {
            const float fx = ((static_cast<float>(x) + 0.5f) / cells - 0.5f);
            const float fy = ((static_cast<float>(y) + 0.5f) / cells - 0.5f);
            const float response = EvaluateFrequencyResponsePreview(settings, fx, fy);
            const ImU32 color = spectrumTexture == 0
                ? IM_COL32(
                    static_cast<int>(18.0f + response * 225.0f),
                    static_cast<int>(24.0f + response * 150.0f),
                    static_cast<int>(40.0f + response * 75.0f),
                    255)
                : IM_COL32(
                    static_cast<int>(38.0f + response * 50.0f),
                    static_cast<int>(20.0f + response * 125.0f),
                    static_cast<int>(38.0f + response * 155.0f),
                    static_cast<int>(40.0f +
                        (1.0f - response) * 180.0f));
            drawList->AddRectFilled(
                ImVec2(origin.x + x * cellSize, origin.y + y * cellSize),
                ImVec2(origin.x + (x + 1) * cellSize + 1.0f,
                       origin.y + (y + 1) * cellSize + 1.0f),
                color);
        }
    }
    drawList->AddRect(
        origin, ImVec2(origin.x + size, origin.y + size),
        IM_COL32(205, 215, 228, 170), 4.0f);
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    const auto drawRing = [&](float frequency, ImU32 color) {
        drawList->AddCircle(
            center,
            std::clamp(frequency / 0.5f, 0.0f, 1.41421356f) * size * 0.5f,
            color, 64, 1.5f);
    };
    if (settings.mode == EditorNodeGraph::FrequencyFilterMode::LowPass ||
        settings.mode == EditorNodeGraph::FrequencyFilterMode::HighPass) {
        drawRing(settings.lowCutoff, IM_COL32(255, 221, 112, 245));
    } else if (settings.mode == EditorNodeGraph::FrequencyFilterMode::BandPass ||
               settings.mode == EditorNodeGraph::FrequencyFilterMode::BandStop) {
        drawRing(settings.lowCutoff, IM_COL32(255, 221, 112, 245));
        drawRing(settings.highCutoff, IM_COL32(113, 224, 255, 245));
    }
    for (const EditorNodeGraph::FrequencyNotch& notch : settings.notches) {
        const float angle = notch.directionDegrees * 3.14159265358979323846f / 180.0f;
        const ImVec2 offset(
            std::cos(angle) * notch.frequency / 0.5f * size * 0.5f,
            std::sin(angle) * notch.frequency / 0.5f * size * 0.5f);
        drawList->AddCircleFilled(
            ImVec2(center.x + offset.x, center.y + offset.y),
            4.0f, IM_COL32(255, 128, 110, 255));
        drawList->AddCircleFilled(
            ImVec2(center.x - offset.x, center.y - offset.y),
            4.0f, IM_COL32(255, 128, 110, 255));
    }

    if (!interactive || !ImGui::IsItemActive() || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        return false;
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float dx = mouse.x - center.x;
    const float dy = mouse.y - center.y;
    const float frequency = std::clamp(
        std::hypot(dx, dy) / (size * 0.5f) * 0.5f,
        0.0f, 0.70710678f);
    if (settings.mode == EditorNodeGraph::FrequencyFilterMode::NotchReject &&
        !settings.notches.empty()) {
        std::size_t nearestIndex = 0;
        float nearestDistance = std::numeric_limits<float>::max();
        for (std::size_t index = 0; index < settings.notches.size(); ++index) {
            const EditorNodeGraph::FrequencyNotch& candidate =
                settings.notches[index];
            const float angle =
                candidate.directionDegrees *
                3.14159265358979323846f / 180.0f;
            const float hx =
                std::cos(angle) * candidate.frequency / 0.5f *
                size * 0.5f;
            const float hy =
                std::sin(angle) * candidate.frequency / 0.5f *
                size * 0.5f;
            const float distance = std::min(
                std::hypot(dx - hx, dy - hy),
                std::hypot(dx + hx, dy + hy));
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearestIndex = index;
            }
        }
        EditorNodeGraph::FrequencyNotch& notch =
            settings.notches[nearestIndex];
        notch.frequency = frequency;
        notch.directionDegrees = std::atan2(dy, dx) * 180.0f / 3.14159265358979323846f;
    } else if (settings.mode == EditorNodeGraph::FrequencyFilterMode::BandPass ||
               settings.mode == EditorNodeGraph::FrequencyFilterMode::BandStop) {
        if (std::abs(frequency - settings.lowCutoff) <
            std::abs(frequency - settings.highCutoff))
            settings.lowCutoff = std::min(frequency, settings.highCutoff);
        else
            settings.highCutoff = std::max(frequency, settings.lowCutoff);
    } else {
        settings.lowCutoff = frequency;
    }
    return true;
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
        bool matchedLink = false;
        const auto describeLink = [&](const EditorNodeGraph::Link& link) {
            const bool matches =
                socket.direction == EditorNodeGraph::SocketDirection::Input
                ? link.toSocketId == socket.id
                : link.fromSocketId == socket.id;
            if (matchedLink || !matches) {
                return;
            }
            matchedLink = true;
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
        };
        if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
            graph.ForEachIncomingLink(socket.nodeId, describeLink);
        } else {
            graph.ForEachOutgoingLink(socket.nodeId, describeLink);
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

    const float fontSize = ImGui::GetFontSize() * uiScale * 0.72f;
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
            textColor,
            text.c_str());
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (input) m_RevealedAdvancedInputNodes.insert(node.id);
            else m_RevealedAdvancedOutputNodes.insert(node.id);
        }
    };
    drawRow(true, hiddenInputs);
    drawRow(false, hiddenOutputs);
}

bool EditorNodeGraphUI::IsSocketVisibleDuringDrag(const EditorNodeGraph::Graph& graph,
    int nodeId, const std::string& socketId, EditorNodeGraph::SocketDirection direction) const {
    using EditorNodeGraph::SocketDirection;
    if (m_DragOutputNodeId > 0) {
        return direction == SocketDirection::Output
            ? nodeId == m_DragOutputNodeId && socketId == m_DragOutputSocketId
            : CanConnectInContext(graph, m_DragOutputNodeId, m_DragOutputSocketId, nodeId, socketId);
    }
    if (m_DragInputNodeId > 0) {
        return direction == SocketDirection::Input
            ? nodeId == m_DragInputNodeId && socketId == m_DragInputSocketId
            : CanConnectInContext(graph, nodeId, socketId, m_DragInputNodeId, m_DragInputSocketId);
    }
    return true;
}

bool EditorNodeGraphUI::ShouldShowSocketContextLabel(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node,
    const EditorNodeGraph::SocketDefinition& socket,
    bool nodeHovered,
    bool socketHovered) const {
    if (socketHovered) {
        return true;
    }
    if (m_DragOutputNodeId > 0) {
        if (socket.direction == EditorNodeGraph::SocketDirection::Output) {
            return node.id == m_DragOutputNodeId &&
                socket.id == m_DragOutputSocketId;
        }
        return nodeHovered && CanConnectInContext(graph, 
            m_DragOutputNodeId,
            m_DragOutputSocketId,
            node.id,
            socket.id);
    }
    if (m_DragInputNodeId > 0) {
        if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
            return node.id == m_DragInputNodeId &&
                socket.id == m_DragInputSocketId;
        }
        return nodeHovered && CanConnectInContext(graph, 
            node.id,
            socket.id,
            m_DragInputNodeId,
            m_DragInputSocketId);
    }
    return false;
}

EditorNodeGraphUI::CachedRect EditorNodeGraphUI::DrawSocketContextLabel(
    ImDrawList* drawList,
    const EditorNodeGraph::SocketDefinition& socket,
    const SocketAnchor& anchor,
    ImU32 textColor,
    ImU32 backgroundColor,
    float uiScale) const {
    const std::string& label =
        socket.label.empty() ? socket.id : socket.label;
    const float baseFontSize = std::max(0.001f, ImGui::GetFontSize());
    const float fontSize = baseFontSize * uiScale * 0.82f;
    const float fontScale = fontSize / baseFontSize;
    const ImVec2 baseTextSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 textSize(
        baseTextSize.x * fontScale,
        baseTextSize.y * fontScale);
    const float padX = 5.0f * uiScale;
    const float padY = 3.0f * uiScale;
    const float labelGap = 4.0f * uiScale;
    const float pinRadius =
        Stack::Editor::NodeGraphUILayout::kSocketRadius * uiScale;
    ImVec2 labelMin;
    if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
        const float labelMaxX =
            anchor.screenPos.x - pinRadius - labelGap;
        labelMin = ImVec2(
            labelMaxX - textSize.x - padX * 2.0f,
            anchor.screenPos.y - textSize.y * 0.5f - padY);
    } else {
        labelMin = ImVec2(
            anchor.screenPos.x + pinRadius + labelGap,
            anchor.screenPos.y - textSize.y * 0.5f - padY);
    }
    const ImVec2 labelMax(
        labelMin.x + textSize.x + padX * 2.0f,
        labelMin.y + textSize.y + padY * 2.0f);
    const float rounding = 4.0f * uiScale;
    drawList->AddRectFilled(
        labelMin, labelMax, backgroundColor, rounding);
    drawList->AddRect(
        labelMin,
        labelMax,
        ColorWithAlpha(
            ImGui::ColorConvertU32ToFloat4(textColor),
            0.34f),
        rounding,
        0,
        0.75f * uiScale);
    drawList->AddText(
        ImGui::GetFont(),
        fontSize,
        ImVec2(labelMin.x + padX, labelMin.y + padY),
        textColor,
        label.c_str());
    return { labelMin, labelMax };
}

void EditorNodeGraphUI::ExtendNodeOverlayBounds(
    int nodeId,
    const CachedRect& rect) {
    if (!rect.IsValid()) {
        return;
    }
    auto it = m_NodeLayoutCache.find(nodeId);
    if (it == m_NodeLayoutCache.end()) {
        return;
    }
    CachedRect& overlay = it->second.overlayRect;
    if (!overlay.IsValid()) {
        overlay = rect;
        return;
    }
    overlay.min.x = std::min(overlay.min.x, rect.min.x);
    overlay.min.y = std::min(overlay.min.y, rect.min.y);
    overlay.max.x = std::max(overlay.max.x, rect.max.x);
    overlay.max.y = std::max(overlay.max.y, rect.max.y);
}

void EditorNodeGraphUI::RenderNode(EditorModule* editor, EditorNodeGraph::Node& node) {
    EditorNodeGraph::Graph& graph = GetActiveGraph(editor);
    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(this, editor, node, metrics);
    ApplyCanonicalNodeMetrics(this, editor, node, metrics);
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
    const float edgeFadeDistance = 0.0f;
    const float nodeEdgeAlpha = CanvasEdgeFadeRectAlpha(
        min,
        max,
        ImVec2(m_CanvasMin.x, m_CanvasMin.y),
        ImVec2(m_CanvasMax.x, m_CanvasMax.y),
        edgeFadeDistance);
    if (nodeEdgeAlpha <= 0.001f && ImGuiExtras::GraphWheelTargetNode(&graph) != node.id) {
        return;
    }
    ScopedNodeEdgeAlpha scopedNodeEdgeAlpha(nodeEdgeAlpha);
    const bool selected = graph.IsNodeSelected(node.id);
    const bool hovered =
        !m_RenderPreviewOnly &&
        !m_RenderCaptureOnly &&
        !m_MiddlePanCaptureActive &&
        ImGui::IsMouseHoveringRect(min, max, false) &&
        FindNodeAt(graph, { ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y }) == node.id;
    const auto& numericColors=graphStyle.nodeAppearance;
    if (!m_RenderPreviewOnly && !m_RenderCaptureOnly) ImGuiExtras::RegisterGraphCursorSurface(&graph,min,max,
        presentation.kind==NodePresentationKind::FramelessMedia ? graphStyle.canvas : numericColors.surface);
    const ImVec2 cursorAnchor((min.x+max.x)*0.5f,min.y-18.0f*ImGui::GetWindowViewport()->DpiScale);
    ImGuiExtras::GraphNumericNodeScope numericNodeScope(&graph, node.id, hovered && IsGraphCanvasHovered(),
        SharesIdentityWithParameter(node), &numericColors, &cursorAnchor);
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
            CanConnectInContext(graph, m_DragOutputNodeId, m_DragOutputSocketId, node.id, socketId)) {
            emphasis = std::max(emphasis, 1.0f);
        } else if (
            direction == EditorNodeGraph::SocketDirection::Output &&
            m_DragInputNodeId > 0 &&
            hoveredSocket &&
            CanConnectInContext(graph, node.id, socketId, m_DragInputNodeId, m_DragInputSocketId)) {
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
            int presentationWidth = 0;
            int presentationHeight = 0;
            if (editor->TryGetActiveRawWorkspacePresentationTexture(
                    texture,
                    presentationWidth,
                    presentationHeight)) {
                imageSourceSize = ImVec2(
                    static_cast<float>(std::max(1, presentationWidth)),
                    static_cast<float>(std::max(1, presentationHeight)));
            } else {
                texture = editor->GetPipeline().GetOutputTexture();
                imageSourceSize = ImVec2(
                    static_cast<float>(std::max(1, editor->GetPipeline().GetCanvasWidth())),
                    static_cast<float>(std::max(1, editor->GetPipeline().GetCanvasHeight())));
            }
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
            drawList->AddRectFilled(
                imageMin,
                imageMax,
                ColorWithAlpha(graphStyle.nodeSurface, 0.42f),
                5.0f * uiScale);
        }
        const float previewRounding = 5.0f * uiScale;
        if (selectedAnim > 0.001f) {
            drawList->AddRectFilled(
                imageMin,
                imageMax,
                ApplyStyleAlpha(IM_COL32(255, 255, 255, static_cast<int>(14.0f + 24.0f * selectedAnim))),
                previewRounding);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
            if (!IsSocketVisibleDuringDrag(graph, node.id, socket.id, socket.direction)) continue;
            const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
            if (!anchor) {
                continue;
            }
            const bool hoveredSocket = socket.direction == EditorNodeGraph::SocketDirection::Input
                ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
                : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
            const float socketFocus = socketInteractionEmphasis(socket.direction, socket.id, hoveredSocket);
            const EditorNodeGraph::SocketDefinition displaySocket =
                ResolveSocketDisplayDefinition(graph, socket, editor);
            const ImU32 socketColor = SocketColor(displaySocket, familyStyle, graphStyle);
            const unsigned int imageSocketIconTexture =
                (displaySocket.type == EditorNodeGraph::SocketType::Image ||
                 displaySocket.type == EditorNodeGraph::SocketType::ImageOrChannel)
                    ? m_SocketIconTexture.GetImageSocketAperture()
                    : 0;
            ImGui::PushID(node.id);
            DrawSocketPin(
                drawList, anchor->screenPos, pinRadius, socketColor, graphStyle,
                hoveredSocket, displaySocket, IsSocketConnected(graph, socket),
                socketFocus, imageSocketIconTexture);
            ImGui::PopID();
            if (ShouldShowSocketContextLabel(
                    graph, node, socket, hovered, hoveredSocket)) {
                ExtendNodeOverlayBounds(
                    node.id,
                    DrawSocketContextLabel(
                        drawList,
                        socket,
                        *anchor,
                        socketColor,
                        ColorWithAlpha(graphStyle.canvas, 0.92f),
                        uiScale));
            }
            if (hoveredSocket && DetailCardDelayElapsed(
                    "socket:" + std::to_string(node.id) + ":" + socket.id +
                    (socket.direction == EditorNodeGraph::SocketDirection::Input ? ":in" : ":out"))) {
                DrawSocketDetailCard(editor, graph, socket, anchor->screenPos);
            }
        }
        DrawAdvancedSocketRevealRows(
            graph, node, *layout, drawList,
            graphStyle.enabled ? ColorWithAlpha(familyStyle.mutedText, 0.92f)
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
        const float frameRounding = 8.0f * uiScale;
        const float borderThickness = 1.15f * uiScale;

        ImVec4 fillColor = familyStyle.fill;
        ImVec4 borderColor = familyStyle.border;
        if (node.kind == EditorNodeGraph::NodeKind::Output && !node.outputEnabled) {
            fillColor = familyStyle.fill;
            borderColor = graphStyle.nodeAppearance.error;
        }
        if (nodeEmphasis > 0.001f) {
            fillColor = familyStyle.fill;
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
            borderThickness, selectedAnim);
        numericNodeScope.PreserveCurrentDrawing();
        const std::string squareLabel = CompactNodeTitle(node);

        const float fontSize = ImGui::GetFontSize() * uiScale;
        const std::vector<std::string>& squareDisplayLines =
            layout->logicalLayout.titleLines;

        bool outputUsesChannel = false;
        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            if (const EditorNodeGraph::Link* input =
                    graph.FindInputLink(
                        node.id,
                        EditorNodeGraph::kImageInputSocketId)) {
                outputUsesChannel =
                    graph.IsScalarSocketStream(
                        input->fromNodeId,
                        input->fromSocketId);
            }
        }

        const char* outputModeLabel = outputUsesChannel
            ? Stack::NodeMath::OutputChannelViewModeLabel(
                node.outputSettings.channelViewMode)
            : "Image";
        const float modeFontSize = fontSize * 0.72f;
        ImVec2 scaledModeTextSize {};
        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            const ImVec2 modeTextSize = ImGui::CalcTextSize(outputModeLabel);
            const float modeScale = modeFontSize / std::max(1.0f, ImGui::GetFontSize());
            scaledModeTextSize = ImVec2(modeTextSize.x * modeScale, modeTextSize.y * modeScale);
        }
        const float titleLineGap = 2.0f * uiScale;
        const float titleBlockHeight = squareDisplayLines.empty()
            ? 0.0f
            : static_cast<float>(squareDisplayLines.size()) * fontSize +
                static_cast<float>(squareDisplayLines.size() - 1) *
                    titleLineGap;
        const float labelBlockHeight = (node.kind == EditorNodeGraph::NodeKind::Output)
            ? titleBlockHeight + scaledModeTextSize.y + 3.0f * uiScale
            : titleBlockHeight;
        float textY =
            min.y + (max.y - min.y - labelBlockHeight) * 0.5f;
        for (const std::string& displayLine : squareDisplayLines) {
            const ImVec2 textSize = ImGui::CalcTextSize(
                displayLine.c_str());
            const float scaledTextWidth = textSize.x * uiScale;
            drawList->AddText(
                ImGui::GetFont(),
                fontSize,
                ImVec2(
                    min.x +
                        (max.x - min.x - scaledTextWidth) * 0.5f,
                    textY),
                ColorToU32(familyStyle.text),
                displayLine.c_str());
            textY += fontSize + titleLineGap;
        }

        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            const ImVec2 modePos(
                min.x + (max.x - min.x - scaledModeTextSize.x) * 0.5f,
                textY + 3.0f * uiScale);
            drawList->AddText(
                ImGui::GetFont(),
                modeFontSize,
                modePos,
                graphStyle.enabled ? ColorWithAlpha(familyStyle.mutedText, 0.92f) : ApplyStyleAlpha(IM_COL32(180, 195, 205, 220)),
                outputModeLabel);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
            if (!IsSocketVisibleDuringDrag(graph, node.id, socket.id, socket.direction)) continue;
            const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
            if (anchor) {
                const ImVec2 pin = anchor->screenPos;
                const bool hoveredSocket = (socket.direction == EditorNodeGraph::SocketDirection::Input)
                    ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
                    : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
                const float socketFocus = socketInteractionEmphasis(socket.direction, socket.id, hoveredSocket);

                const EditorNodeGraph::SocketDefinition displaySocket =
                    ResolveSocketDisplayDefinition(graph, socket, editor);
                const ImU32 baseColor = SocketColor(displaySocket, familyStyle, graphStyle);
                const unsigned int imageSocketIconTexture =
                    (displaySocket.type == EditorNodeGraph::SocketType::Image ||
                     displaySocket.type == EditorNodeGraph::SocketType::ImageOrChannel)
                        ? m_SocketIconTexture.GetImageSocketAperture()
                        : 0;
                ImGui::PushID(node.id);
                DrawSocketPin(
                    drawList, pin, pinRadius, baseColor, graphStyle, hoveredSocket,
                    displaySocket, IsSocketConnected(graph, socket), socketFocus,
                    imageSocketIconTexture);
                ImGui::PopID();

                if (ShouldShowSocketContextLabel(
                        graph, node, socket, hovered, hoveredSocket)) {
                    ExtendNodeOverlayBounds(
                        node.id,
                        DrawSocketContextLabel(
                            drawList,
                            socket,
                            *anchor,
                            baseColor,
                            ColorWithAlpha(graphStyle.canvas, 0.92f),
                            uiScale));
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
            graphStyle.enabled ? ColorWithAlpha(familyStyle.mutedText, 0.92f)
                               : ApplyStyleAlpha(IM_COL32(180, 195, 205, 220)),
            uiScale);

        if (IsSummaryOnlyNode(this, editor, node) && hovered) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(node.title.empty() ? squareLabel.c_str() : node.title.c_str());
            if (!node.definitionResolved) {
                ImGui::TextColored(
                    ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
                    "Unresolved definition");
                if (!node.definitionResolutionError.empty()) {
                    ImGui::TextWrapped(
                        "%s",
                        node.definitionResolutionError.c_str());
                }
            } else if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
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
    const float controlWidth =
        layout->contentRect.max.x - layout->contentRect.min.x;
    const float safeContentWidth =
        controlWidth - 4.0f * uiScale;
    const float logicalControlWidth = controlWidth / std::max(0.001f, uiScale);
    const float logicalSafeContentWidth = safeContentWidth / std::max(0.001f, uiScale);
    const ImVec2 previewSize = NodePreviewSizeForScale(metrics, uiScale);
    const float frameRounding = 8.0f * uiScale;
    const float borderThickness = 1.15f * uiScale;
    const float headerY = metrics.headerInsetY * uiScale;
    const float sectionGap = metrics.sectionGap * uiScale;
    const float itemGap = metrics.itemGap * uiScale;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec4 fillColor = familyStyle.fill;
    ImVec4 borderColor = familyStyle.border;
    if (nodeEmphasis > 0.001f) {
        // Selection is expressed by the outline; keep the cream surface stable.
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
        borderThickness, selectedAnim);
    numericNodeScope.PreserveCurrentDrawing();
    for (const EditorNodeGraph::SocketDefinition& socket : PresentedSockets(graph, node)) {
            if (!IsSocketVisibleDuringDrag(graph, node.id, socket.id, socket.direction)) continue;
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
        const EditorNodeGraph::SocketDefinition displaySocket =
            ResolveSocketDisplayDefinition(graph, socket, editor);
        const ImU32 baseColor = SocketColor(displaySocket, familyStyle, graphStyle);
        const unsigned int imageSocketIconTexture =
            (displaySocket.type == EditorNodeGraph::SocketType::Image ||
             displaySocket.type == EditorNodeGraph::SocketType::ImageOrChannel)
                ? m_SocketIconTexture.GetImageSocketAperture()
                : 0;
        ImGui::PushID(node.id);
        DrawSocketPin(
            drawList, pin, pinRadius, baseColor, graphStyle, hoveredSocket,
            displaySocket, IsSocketConnected(graph, socket), socketFocus,
            imageSocketIconTexture);
        ImGui::PopID();
        if (ShouldShowSocketContextLabel(
                graph, node, socket, hovered, hoveredSocket)) {
            const ImU32 labelColor = ColorWithAlpha(
                BlendColor(
                    familyStyle.mutedText,
                    BlendColor(
                        familyStyle.text,
                        familyStyle.accent,
                        0.28f),
                    std::clamp(
                        socketFocus * 0.72f +
                            (hoveredSocket ? 0.18f : 0.0f),
                        0.0f,
                        1.0f)),
                0.96f);
            ExtendNodeOverlayBounds(
                node.id,
                DrawSocketContextLabel(
                    drawList,
                    socket,
                    *anchor,
                    labelColor,
                    ColorWithAlpha(graphStyle.canvas, 0.92f),
                    uiScale));
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

    const std::string nodePrimaryTitle = PrimaryNodeTitle(node);

    const ImVec4 textMuted = familyStyle.mutedText;

    ImGui::PushID(node.id);
    const float titleFontSize = ImGui::GetFontSize() * uiScale;
    const ImVec2 headerTextMin(layout->contentRect.min.x, min.y + headerY);
    ImVec2 headerCursor = headerTextMin;
    if (presentation.showKindLabel) {
        const float kindFontSize =
            ImGui::GetFontSize() * uiScale * 0.86f;
        drawList->AddText(
            ImGui::GetFont(),
            kindFontSize,
            headerCursor,
            ColorWithAlpha(textMuted, 0.96f),
            NodeKindLabel(node.kind));
        headerCursor.y += kindFontSize + (2.0f * uiScale);
    }
    if (presentation.showTitle) {
        const std::vector<std::string>& titleLines =
            layout->logicalLayout.titleLines;
        const float lineGap = 2.0f * uiScale;
        float titleY = headerCursor.y;
        for (const std::string& titleLine : titleLines) {
            const ImVec2 baseLineSize =
                ImGui::CalcTextSize(titleLine.c_str());
            const float lineWidth = baseLineSize.x * uiScale;
            const ImVec2 titlePos(
                layout->contentRect.min.x,
                titleY);
            drawList->AddText(
                ImGui::GetFont(),
                titleFontSize,
                titlePos,
                ColorToU32(familyStyle.text),
                titleLine.c_str());
            titleY += titleFontSize + lineGap;
        }
        if (hovered && layout->logicalLayout.titleEllipsized) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(nodePrimaryTitle.c_str());
            ImGui::EndTooltip();
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f * uiScale * densityScale, 2.5f * uiScale * densityScale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(metrics.itemGap * uiScale * 0.75f * densityScale, metrics.itemGap * uiScale * 0.58f * densityScale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4.0f * uiScale * densityScale, 2.0f * uiScale * densityScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f * uiScale * densityScale);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, 6.5f * uiScale * densityScale);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f * uiScale);
    StackAppearance::ScopedNodeControlStyle nodeControlStyle(graphStyle.nodeAppearance);
    if (!expanded) {
        nodeControlStyle.End();
        ImGui::PopStyleVar(7);
        ImGui::PopID();
        return;
    }

    ImGui::SetCursorScreenPos(layout->contentRect.min);
    ImGui::PushFont(
        nullptr,
        ImGui::GetStyle().FontSizeBase * contentScale);
    ImGui::PushItemWidth(controlWidth);
    ImGuiExtras::ResetNodeControlState();
    ImGuiExtras::GraphNodeControlScopeConfig graphControlConfig {};
    graphControlConfig.labelWidth = 68.0f;
    graphControlConfig.valueWidth = 46.0f;
    graphControlConfig.minSliderWidth = 82.0f;
    graphControlConfig.scale = contentScale;
    graphControlConfig.interactionScale = uiScale;
    graphControlConfig.allowSliderTextEntry = true;
    graphControlConfig.useScrubHandles = true;
    graphControlConfig.rangePolicy = GraphSliderRangePolicyForNodeKind(node.kind);
    graphControlConfig.scrubSensitivity = appearance
        ? appearance->GetGraphNodeSliderDragSensitivity()
        : StackAppearance::kGraphNodeSliderDragSensitivityDefault;
    graphControlConfig.numericDefault = [&node](const char* label, const char*, double minimum, double maximum, const char* format) {
        return LayerNumericDefault(node, label, minimum, maximum, format);
    };
    ImGuiExtras::BeginGraphNodeControlScope(graphControlConfig);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + controlWidth);
    ImGui::BeginGroup();
    const ImVec2 contentUsedMin = ImGui::GetCursorScreenPos();
    if (!node.definitionResolved) {
        ImGui::TextColored(
            ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
            "Unresolved definition");
        if (!node.definitionResolutionError.empty()) {
            ImGui::TextWrapped(
                "%s",
                node.definitionResolutionError.c_str());
        }
        ImGui::Dummy(ImVec2(0.0f, itemGap * 0.5f));
    }
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
        if (const auto defaultValue = NodeNumericDefault(node, value))
            ImGuiExtras::SetNextNodeNumericDefault(*defaultValue);
        const char* format = value == &node.technicalImageSettings.exposureValue ? "%.3f EV" : "%.3f";
        const bool changed = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
        captureIfActive();
        return changed;
    };
    auto drawInlineSeparator = [&]() {
        const ImVec2 separatorPos = ImGui::GetCursorScreenPos();
        const float separatorWidth = std::max(18.0f, controlWidth);
        const float separatorHeight =
            metrics.itemGap * uiScale * 0.55f;
        ImGui::Dummy(ImVec2(separatorWidth, separatorHeight));
    };

    NodeContentRenderContext contentContext {
        editor,
        graph,
        node,
        metrics,
        nodeSurfaceSpec,
        graphStyle,
        drawList,
        previewSize,
        controlWidth,
        safeContentWidth,
        logicalControlWidth,
        logicalSafeContentWidth,
        uiScale,
        contentScale,
        itemGap,
        sectionGap,
        richExpandedSurface,
        selected,
        captureIfActive,
        renderSlider,
        drawInlineSeparator
    };
    (void)(
        RenderSourceNodeContent(contentContext) ||
        RenderMaskDataNodeContent(contentContext) ||
        RenderFrequencyNodeContent(contentContext) ||
        RenderUtilityNodeContent(contentContext));
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
    const float renderedBaseHeight = std::ceil(
        (renderedHeightPixels / std::max(0.001f, uiScale)) * 2.0f) /
        2.0f;
    if (expanded) {
        const float nextHeight = std::max(
            metrics.minExpandedHeight,
            SanitizeFinite(renderedBaseHeight, metrics.minExpandedHeight));
        std::uint64_t contentRevision =
            m_NodeLayoutCache[node.id].logicalContentRevision;
        const auto mixRevision = [&](std::uint64_t value) {
            contentRevision ^= value +
                0x9e3779b97f4a7c15ull +
                (contentRevision << 6u) +
                (contentRevision >> 2u);
        };
        mixRevision(graph.GetStructureRevision());
        mixRevision(
            editor
                ? editor->GetRenderRevision()
                : 0u);
        const auto measuredIt = m_NodeMeasuredBaseHeights.find(node.id);
        const auto revisionIt =
            m_NodeMeasuredContentRevisions.find(node.id);
        if (revisionIt == m_NodeMeasuredContentRevisions.end() ||
            revisionIt->second != contentRevision) {
            m_NodeMeasuredBaseHeights[node.id] = nextHeight;
            m_NodeMeasuredContentRevisions[node.id] =
                contentRevision;
        }
    }
    NodeLayoutCache& currentLayout = m_NodeLayoutCache[node.id];
    currentLayout.contentUsedRect =
        CachedRect{ contentUsedMin, contentUsedMax };
    m_NodeContentOverflow[node.id] =
        currentLayout.contentUsedRect.IsValid() &&
        currentLayout.contentUsedRect.max.y >
            currentLayout.frameRect.max.y -
                ((metrics.bodyInsetBottom + bottomSafetyPadding) *
                    uiScale * 0.35f);
    const ImGuiExtras::NodeControlState& nodeControlState = ImGuiExtras::GetNodeControlState();
    if (contentHovered ||
        (!m_MiddlePanCaptureActive &&
         currentLayout.contentUsedRect.Contains(ImGui::GetMousePos()))) {
        m_NodeContentHovered = true;
    }
    if (nodeControlState.hovered ||
        nodeControlState.active ||
        nodeControlState.edited ||
        nodeControlState.popupOpen ||
        (anyPopupOpen &&
         !m_MiddlePanCaptureActive &&
         currentLayout.contentUsedRect.Contains(ImGui::GetMousePos()))) {
        m_NodeContentHovered |= nodeControlState.hovered || contentHovered;
        m_NodeContentActive |= nodeControlState.active || nodeControlState.edited || nodeControlState.popupOpen;
        m_LastNodeControlId = nodeControlState.id;
    }
    ImGui::PopItemWidth();
    ImGui::PopTextWrapPos();
    ImGuiExtras::EndGraphNodeControlScope();
    ImGui::PopFont();
    nodeControlStyle.End();
    ImGui::PopStyleVar(7);
    ImGui::PopID();
}

bool EditorNodeGraphUI::RenderSourceNodeContent(
    NodeContentRenderContext& context) {
    EditorModule* editor = context.editor;
    EditorNodeGraph::Graph& graph = context.graph;
    EditorNodeGraph::Node& node = context.node;
    const NodeLayoutMetrics& metrics = context.metrics;
    const NodeSurfaceSpec& nodeSurfaceSpec = context.nodeSurfaceSpec;
    const GraphStyleTokens& graphStyle = context.graphStyle;
    ImDrawList* drawList = context.drawList;
    const ImVec2 previewSize = context.previewSize;
    const float controlWidth = context.controlWidth;
    const float safeContentWidth = context.safeContentWidth;
    const float logicalControlWidth = context.logicalControlWidth;
    const float logicalSafeContentWidth = context.logicalSafeContentWidth;
    const float uiScale = context.uiScale;
    const float contentScale = context.contentScale;
    const float itemGap = context.itemGap;
    const float sectionGap = context.sectionGap;
    const bool richExpandedSurface = context.richExpandedSurface;
    const bool selected = context.selected;
    auto& captureIfActive = context.captureIfActive;
    auto& renderSlider = context.renderSlider;
    auto& drawInlineSeparator = context.drawInlineSeparator;

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Scope:
            break;
        default:
            return false;
    }

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
    } else if (node.kind == EditorNodeGraph::NodeKind::RawProjectFrame) {
        ImGui::TextDisabled("Managed mosaic input");
        ImGui::TextWrapped("%s", node.rawProjectFrame.displayLabel.empty()
            ? "Embedded RAW frame"
            : node.rawProjectFrame.displayLabel.c_str());
        ImGui::TextColored(
            node.rawProjectFrame.enabled
                ? ImVec4(0.58f, 0.86f, 0.66f, 1.0f)
                : ImVec4(0.70f, 0.70f, 0.70f, 1.0f),
            "%s%s",
            node.rawProjectFrame.enabled ? "Included" : "Excluded",
            node.rawProjectFrame.reference ? " - Reference" : "");
        if (!node.rawProjectFrame.compatibilityStatus.empty()) {
            ImGui::TextDisabled("%s", node.rawProjectFrame.compatibilityStatus.c_str());
        }
        if (node.rawProjectFrame.quarantined) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
                "Binding quarantined. Use Save Repaired Copy.");
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::MultiFrameDenoise) {
        ImGui::TextDisabled("Managed MFD burst");
        ImGui::TextWrapped("%s", node.multiFrameDenoise.presentationStatus.c_str());
        ImGui::TextDisabled(
            "%llu frame(s) - %s",
            static_cast<unsigned long long>(
                node.multiFrameDenoise.frameBindings.size()),
            node.multiFrameDenoise.resultState.c_str());
        if (node.multiFrameDenoise.quarantined) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
                "Binding quarantined. Use Save Repaired Copy.");
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::RawProjectSourceSet) {
        ImGui::TextDisabled("Managed source-set binding");
        ImGui::TextWrapped("%s", node.rawProjectSourceSet.presentationStatus.c_str());
        if (node.rawProjectSourceSet.quarantined) {
            ImGui::TextColored(
                ImVec4(0.95f, 0.55f, 0.42f, 1.0f),
                "Binding quarantined. Use Save Repaired Copy.");
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Output) {
        bool outputUsesChannel = false;
        if (const EditorNodeGraph::Link* input =
                graph.FindInputLink(
                    node.id,
                    EditorNodeGraph::kImageInputSocketId)) {
            outputUsesChannel =
                graph.IsScalarSocketStream(
                    input->fromNodeId,
                    input->fromSocketId);
        }
        ImGui::TextDisabled(
            "Value: %s",
            outputUsesChannel ? "Channel" : "Image");
        if (outputUsesChannel) {
            int viewMode =
                static_cast<int>(node.outputSettings.channelViewMode);
            const char* viewModes[] = {
                "Neutral",
                "Red",
                "Green",
                "Blue"
            };
            if (ImGuiExtras::NodeCombo(
                    "View As",
                    "##OutputChannelViewMode",
                    &viewMode,
                    viewModes,
                    IM_ARRAYSIZE(viewModes),
                    controlWidth)) {
                node.outputSettings.channelViewMode =
                    static_cast<Stack::NodeMath::OutputChannelViewMode>(
                        std::clamp(viewMode, 0, 3));
                editor->MarkGraphEdited(node.id);
            }
            ImGui::TextWrapped(
                "Inspection mapping affects only the viewport. "
                "The Channel samples and descriptor are unchanged.");
        }
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
        ImGui::BeginDisabled(
            !node.outputEnabled ||
            !graph.IsOutputConnected() ||
            outputUsesChannel ||
            editor->IsExportBusy());
        if (ImGui::Button("Export", ImVec2(controlWidth, 0.0f))) {
            const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
            if (!path.empty()) {
                editor->RequestExportImage(path);
            }
        }
        if (outputUsesChannel &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "PNG export requires an Image. Connect this Channel to "
                "Image Combine first.");
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
                const std::string displayLabel = EllipsizeLabel(
                    label,
                    std::max(1.0f, logicalControlWidth - 12.0f));
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
        editor->RenderGraphScopeNode(node.scopeKind, input ? node.id : -1);
    }
    return true;
}

bool EditorNodeGraphUI::RenderMaskDataNodeContent(
    NodeContentRenderContext& context) {
    EditorModule* editor = context.editor;
    EditorNodeGraph::Graph& graph = context.graph;
    EditorNodeGraph::Node& node = context.node;
    const NodeLayoutMetrics& metrics = context.metrics;
    const NodeSurfaceSpec& nodeSurfaceSpec = context.nodeSurfaceSpec;
    const GraphStyleTokens& graphStyle = context.graphStyle;
    ImDrawList* drawList = context.drawList;
    const ImVec2 previewSize = context.previewSize;
    const float controlWidth = context.controlWidth;
    const float safeContentWidth = context.safeContentWidth;
    const float logicalControlWidth = context.logicalControlWidth;
    const float logicalSafeContentWidth = context.logicalSafeContentWidth;
    const float uiScale = context.uiScale;
    const float contentScale = context.contentScale;
    const float itemGap = context.itemGap;
    const float sectionGap = context.sectionGap;
    const bool richExpandedSurface = context.richExpandedSurface;
    const bool selected = context.selected;
    auto& captureIfActive = context.captureIfActive;
    auto& renderSlider = context.renderSlider;
    auto& drawInlineSeparator = context.drawInlineSeparator;

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::MaskCombine:
        case EditorNodeGraph::NodeKind::DataMath:
            break;
        default:
            return false;
    }

    if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator) {
        bool changed = false;
        if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::Solid) {
            changed |= renderSlider("Value", "##SolidValue", &node.maskSettings.value, 0.0f, 1.0f);
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
            changed |= renderSlider("Angle", "##LinearAngle", &node.maskSettings.angle, -180.0f, 180.0f);
            changed |= renderSlider("Offset", "##LinearOffset", &node.maskSettings.offset, -1.0f, 1.0f);
            changed |= renderSlider("Scale", "##LinearScale", &node.maskSettings.scale, 0.1f, 4.0f);
            changed |= ImGuiExtras::NodeCheckbox("Invert", "##LinearInvert", &node.maskSettings.invert, controlWidth);
            captureIfActive();
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient ||
                   node.maskKind == EditorNodeGraph::MaskGeneratorKind::Square) {
            changed |= renderSlider("Center X", "##RadialCenterX", &node.maskSettings.centerX, 0.0f, 1.0f);
            changed |= renderSlider("Center Y", "##RadialCenterY", &node.maskSettings.centerY, 0.0f, 1.0f);
            changed |= renderSlider("Radius X", "##RadialRadius", &node.maskSettings.radius, 0.01f, 1.5f);
            changed |= renderSlider("Radius Y", "##RadialRadiusY", &node.maskSettings.radiusY, 0.01f, 1.5f);
            changed |= renderSlider("Angle", "##RadialAngle", &node.maskSettings.angle, -180.0f, 180.0f);
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
            editor->MarkGraphEdited(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskCombine) {
        bool changed = false;
        int mode = static_cast<int>(node.maskCombineMode);
        const char* modes[] = { "Add", "Subtract", "Intersect", "Difference" };
        if (ImGuiExtras::NodeCombo("Mode", "##MaskCombineMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            changed |= editor->GetNodeGraph().SetMaskCombineMode(
                node.id,
                static_cast<EditorNodeGraph::MaskCombineMode>(
                    std::clamp(mode, 0, 3)));
        }
        ImGui::TextDisabled("Mask A is the base. Mask B refines it.");
        if (changed) {
            editor->MarkGraphEdited(node.id);
        }
    } else if (
        node.kind ==
        EditorNodeGraph::NodeKind::ConstantChannel) {
        float value = SanitizeFinite(
            node.constantChannelSettings.value,
            1.0f);
        ImGui::TextDisabled("Value");
        ImGui::SetNextItemWidth(controlWidth);
        {
        ImGuiExtras::GraphNumericValueStyle numericValueStyle;
        if (ImGui::InputFloat(
                "##ConstantChannelValue",
                &value,
                0.01f,
                0.1f,
                "%.6g") &&
            std::isfinite(value)) {
            node.constantChannelSettings.value = value;
            editor->MarkGraphEdited(node.id);
        }
        }
        captureIfActive();
        if (!graph.FindInputLink(
                node.id,
                EditorNodeGraph::kMatchExtentInputSocketId)) {
            ImGui::TextDisabled(
                "Connect Match Extent to resolve.");
        } else {
            ImGui::TextDisabled(
                "Broadcasts lazily at the matched extent.");
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::DataMath) {
        bool changed = false;
        int mode = static_cast<int>(node.dataMathMode);
        const char* modes[] = { "Clamp", "Add", "Subtract", "Multiply", "Divide", "Average", "Minimum", "Maximum", "Difference", "Remap", "Average Images" };
        if (ImGuiExtras::NodeCombo("Mode", "##DataMathMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            changed |= editor->GetNodeGraph().SetDataMathMode(
                node.id,
                static_cast<EditorNodeGraph::DataMathMode>(
                    std::clamp(mode, 0, 10)));
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
            editor->MarkGraphEdited(node.id);
        }
    }
    return true;
}

bool EditorNodeGraphUI::RenderFrequencyNodeContent(
    NodeContentRenderContext& context) {
    EditorModule* editor = context.editor;
    EditorNodeGraph::Graph& graph = context.graph;
    EditorNodeGraph::Node& node = context.node;
    const NodeLayoutMetrics& metrics = context.metrics;
    const NodeSurfaceSpec& nodeSurfaceSpec = context.nodeSurfaceSpec;
    const GraphStyleTokens& graphStyle = context.graphStyle;
    ImDrawList* drawList = context.drawList;
    const ImVec2 previewSize = context.previewSize;
    const float controlWidth = context.controlWidth;
    const float safeContentWidth = context.safeContentWidth;
    const float logicalControlWidth = context.logicalControlWidth;
    const float logicalSafeContentWidth = context.logicalSafeContentWidth;
    const float uiScale = context.uiScale;
    const float contentScale = context.contentScale;
    const float itemGap = context.itemGap;
    const float sectionGap = context.sectionGap;
    const bool richExpandedSurface = context.richExpandedSurface;
    const bool selected = context.selected;
    auto& captureIfActive = context.captureIfActive;
    auto& renderSlider = context.renderSlider;
    auto& drawInlineSeparator = context.drawInlineSeparator;

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::FrequencyFilter:
        case EditorNodeGraph::NodeKind::FrequencyResponse:
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
        case EditorNodeGraph::NodeKind::CombineSpectra:
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
        case EditorNodeGraph::NodeKind::FrequencyFft:
        case EditorNodeGraph::NodeKind::FrequencyIfft:
        case EditorNodeGraph::NodeKind::SpectrumView:
        case EditorNodeGraph::NodeKind::FrequencyMask:
        case EditorNodeGraph::NodeKind::SpectrumMath:
        case EditorNodeGraph::NodeKind::MagnitudePhase:
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            break;
        default:
            return false;
    }

    if (node.kind == EditorNodeGraph::NodeKind::FrequencyFilter) {
        bool changed = false;
        EditorNodeGraph::FrequencyResponseSettings& response =
            node.frequencyFilterSettings.localResponse;
        const bool externalResponse = editor->GetNodeGraph().FindAnyInputLink(
            node.id, EditorNodeGraph::kFrequencyResponseInputSocketId) != nullptr;
        EditorNodeGraph::FrequencyResponseSettings previewResponse = response;
        if (externalResponse) {
            const EditorNodeGraph::Link* responseLink =
                editor->GetNodeGraph().FindAnyInputLink(
                    node.id,
                    EditorNodeGraph::kFrequencyResponseInputSocketId);
            const EditorNodeGraph::Node* responseNode = responseLink == nullptr
                ? nullptr
                : editor->GetNodeGraph().FindNode(
                    responseLink->fromNodeId);
            if (responseNode != nullptr &&
                responseNode->kind ==
                    EditorNodeGraph::NodeKind::FrequencyResponse) {
                previewResponse =
                    responseNode->frequencyResponseSettings;
            }
        }
        const unsigned int spectrumTexture =
            GetGraphPreviewTexture(editor, node);
        ImGui::BeginDisabled(externalResponse);
        int mode = static_cast<int>(response.mode);
        const char* modes[] = {
            "All Pass", "Low Pass", "High Pass",
            "Band Pass", "Band Stop", "Notch Reject"
        };
        if (ImGuiExtras::NodeCombo(
                "Mode", "##FrequencyFilterMode", &mode,
                modes, IM_ARRAYSIZE(modes), controlWidth)) {
            response.mode = static_cast<EditorNodeGraph::FrequencyFilterMode>(
                std::clamp(mode, 0, 5));
            changed = true;
        }
        int profile = static_cast<int>(response.profile);
        const char* profiles[] = {
            "Smooth", "Gaussian", "Butterworth", "Hard"
        };
        if (ImGuiExtras::NodeCombo(
                "Profile", "##FrequencyFilterProfile", &profile,
                profiles, IM_ARRAYSIZE(profiles), controlWidth)) {
            response.profile =
                static_cast<EditorNodeGraph::FrequencyTransitionProfile>(
                    std::clamp(profile, 0, 3));
            changed = true;
        }
        if (externalResponse) {
            DrawFrequencyResponsePreview(
                previewResponse, controlWidth, false, spectrumTexture);
        } else {
            changed |= DrawFrequencyResponsePreview(
                response, controlWidth, true, spectrumTexture);
        }
        if (response.mode == EditorNodeGraph::FrequencyFilterMode::LowPass ||
            response.mode == EditorNodeGraph::FrequencyFilterMode::HighPass ||
            response.mode == EditorNodeGraph::FrequencyFilterMode::BandPass ||
            response.mode == EditorNodeGraph::FrequencyFilterMode::BandStop) {
            changed |= renderSlider(
                "Low", "##FrequencyFilterLow", &response.lowCutoff, 0.0f, 0.5f);
            if (response.mode == EditorNodeGraph::FrequencyFilterMode::BandPass ||
                response.mode == EditorNodeGraph::FrequencyFilterMode::BandStop) {
                changed |= renderSlider(
                    "High", "##FrequencyFilterHigh", &response.highCutoff, 0.0f, 0.5f);
            }
            ImGui::TextDisabled(
                "%.4f cyc/px  |  %.1f px detail",
                response.lowCutoff,
                response.lowCutoff > 0.000001f ? 1.0f / response.lowCutoff : 0.0f);
            if (response.mode ==
                    EditorNodeGraph::FrequencyFilterMode::BandPass ||
                response.mode ==
                    EditorNodeGraph::FrequencyFilterMode::BandStop) {
                ImGui::TextDisabled(
                    "%.4f cyc/px  |  %.1f px detail",
                    response.highCutoff,
                    response.highCutoff > 0.000001f
                        ? 1.0f / response.highCutoff
                        : 0.0f);
            }
        }
        if (response.profile ==
            EditorNodeGraph::FrequencyTransitionProfile::Smooth) {
            changed |= renderSlider(
                "Transition", "##FrequencyFilterTransition",
                &response.transitionWidth, 0.001f, 0.25f);
        } else if (response.profile ==
                   EditorNodeGraph::FrequencyTransitionProfile::Butterworth) {
            changed |= renderSlider(
                "Order", "##FrequencyFilterOrder",
                &response.butterworthOrder, 1.0f, 12.0f);
        }
        if (response.mode == EditorNodeGraph::FrequencyFilterMode::NotchReject) {
            if (response.notches.empty()) {
                response.notches.push_back({ "notch-1", 0.25f, 0.0f, 0.025f });
                changed = true;
            }
            for (std::size_t i = 0; i < response.notches.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                EditorNodeGraph::FrequencyNotch& notch = response.notches[i];
                const auto renderNotchParameter = [&](const char* field,
                                                      const char* label,
                                                      const char* id,
                                                      float& value,
                                                      float minimum,
                                                      float maximum) {
                    const bool connected = editor->GetNodeGraph().FindAnyInputLink(
                        node.id,
                        EditorNodeGraph::ParameterInputSocketId(
                            EditorNodeGraph::FrequencyNotchParameterId(
                                notch.id, field))) != nullptr;
                    ImGui::BeginDisabled(connected);
                    const bool parameterChanged =
                        renderSlider(label, id, &value, minimum, maximum);
                    ImGui::EndDisabled();
                    return parameterChanged;
                };
                changed |= renderNotchParameter(
                    "frequency", "Frequency", "##NotchFrequency",
                    notch.frequency, 0.0f, 0.5f);
                changed |= renderNotchParameter(
                    "direction", "Direction", "##NotchDirection",
                    notch.directionDegrees, -180.0f, 180.0f);
                changed |= renderNotchParameter(
                    "width", "Width", "##NotchWidth",
                    notch.width, 0.001f, 0.25f);
                ImGui::PopID();
            }
            if (response.notches.size() < 16 &&
                ImGui::Button("Add mirrored notch pair")) {
                response.notches.push_back({
                    Stack::NodeMath::GenerateCanonicalUuid(),
                    0.25f, 0.0f, 0.025f
                });
                changed = true;
            }
            if (response.notches.size() > 1) {
                ImGui::SameLine();
                if (ImGui::Button("Remove last")) {
                    response.notches.pop_back();
                    changed = true;
                }
            }
        }
        ImGui::EndDisabled();
        if (externalResponse)
            ImGui::TextDisabled("Connected Response is authoritative; local settings are preserved.");
        int edgePolicy = static_cast<int>(node.frequencyFilterSettings.edgePolicy);
        const char* edgePolicies[] = { "Mirror", "Wrap", "Zero Pad" };
        if (ImGuiExtras::NodeCombo(
                "Edges", "##FrequencyFilterEdges", &edgePolicy,
                edgePolicies, IM_ARRAYSIZE(edgePolicies), controlWidth)) {
            node.frequencyFilterSettings.edgePolicy =
                static_cast<EditorNodeGraph::FrequencyEdgePolicy>(
                    std::clamp(edgePolicy, 0, 2));
            changed = true;
        }
        const bool strengthConnected = editor->GetNodeGraph().FindAnyInputLink(
            node.id,
            EditorNodeGraph::ParameterInputSocketId(
                EditorNodeGraph::kStrengthParameterId)) != nullptr;
        ImGui::BeginDisabled(strengthConnected);
        changed |= renderSlider(
            "Strength", "##FrequencyFilterStrength",
            &node.frequencyFilterSettings.strength, 0.0f, 1.0f);
        ImGui::EndDisabled();
        if (strengthConnected) ImGui::TextDisabled("Strength supplied by connected Value.");
        if (response.mode == EditorNodeGraph::FrequencyFilterMode::AllPass &&
            !externalResponse)
            ImGui::TextDisabled("Exact bypass: no FFT work.");
        if (changed) editor->MarkGraphEdited(node.id);
    } else if (node.kind == EditorNodeGraph::NodeKind::FrequencyResponse) {
        bool changed = false;
        EditorNodeGraph::FrequencyResponseSettings& response =
            node.frequencyResponseSettings;
        int mode = static_cast<int>(response.mode);
        const char* modes[] = {
            "All Pass", "Low Pass", "High Pass",
            "Band Pass", "Band Stop", "Notch Reject"
        };
        if (ImGuiExtras::NodeCombo(
                "Mode", "##FrequencyResponseMode", &mode,
                modes, IM_ARRAYSIZE(modes), controlWidth)) {
            response.mode = static_cast<EditorNodeGraph::FrequencyFilterMode>(
                std::clamp(mode, 0, 5));
            changed = true;
        }
        int profile = static_cast<int>(response.profile);
        const char* profiles[] = { "Smooth", "Gaussian", "Butterworth", "Hard" };
        if (ImGuiExtras::NodeCombo(
                "Profile", "##FrequencyResponseProfile", &profile,
                profiles, IM_ARRAYSIZE(profiles), controlWidth)) {
            response.profile =
                static_cast<EditorNodeGraph::FrequencyTransitionProfile>(
                    std::clamp(profile, 0, 3));
            changed = true;
        }
        changed |= DrawFrequencyResponsePreview(response, controlWidth, true);
        const auto renderParameter = [&](const char* label,
                                         const char* id,
                                         const char* parameterId,
                                         float& value,
                                         float minimum,
                                         float maximum) {
            const bool connected = editor->GetNodeGraph().FindAnyInputLink(
                node.id, EditorNodeGraph::ParameterInputSocketId(parameterId)) != nullptr;
            ImGui::BeginDisabled(connected);
            const bool parameterChanged =
                renderSlider(label, id, &value, minimum, maximum);
            ImGui::EndDisabled();
            return parameterChanged;
        };
        if (response.mode != EditorNodeGraph::FrequencyFilterMode::AllPass &&
            response.mode != EditorNodeGraph::FrequencyFilterMode::NotchReject) {
            changed |= renderParameter(
                "Low", "##FrequencyResponseLow",
                EditorNodeGraph::kLowCutoffParameterId,
                response.lowCutoff, 0.0f, 0.5f);
            if (response.mode == EditorNodeGraph::FrequencyFilterMode::BandPass ||
                response.mode == EditorNodeGraph::FrequencyFilterMode::BandStop) {
                changed |= renderParameter(
                    "High", "##FrequencyResponseHigh",
                    EditorNodeGraph::kHighCutoffParameterId,
                    response.highCutoff, 0.0f, 0.5f);
            }
            ImGui::TextDisabled(
                "%.4f cyc/px  |  %.1f px detail",
                response.lowCutoff,
                response.lowCutoff > 0.000001f ? 1.0f / response.lowCutoff : 0.0f);
            if (response.mode ==
                    EditorNodeGraph::FrequencyFilterMode::BandPass ||
                response.mode ==
                    EditorNodeGraph::FrequencyFilterMode::BandStop) {
                ImGui::TextDisabled(
                    "%.4f cyc/px  |  %.1f px detail",
                    response.highCutoff,
                    response.highCutoff > 0.000001f
                        ? 1.0f / response.highCutoff
                        : 0.0f);
            }
        }
        if (response.profile == EditorNodeGraph::FrequencyTransitionProfile::Smooth) {
            changed |= renderParameter(
                "Transition", "##FrequencyResponseTransition",
                EditorNodeGraph::kTransitionWidthParameterId,
                response.transitionWidth, 0.001f, 0.25f);
        } else if (response.profile == EditorNodeGraph::FrequencyTransitionProfile::Butterworth) {
            changed |= renderParameter(
                "Order", "##FrequencyResponseOrder",
                EditorNodeGraph::kButterworthOrderParameterId,
                response.butterworthOrder, 1.0f, 12.0f);
        }
        if (response.mode == EditorNodeGraph::FrequencyFilterMode::NotchReject) {
            for (std::size_t i = 0; i < response.notches.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                EditorNodeGraph::FrequencyNotch& notch = response.notches[i];
                const auto renderNotchParameter = [&](const char* field,
                                                      const char* label,
                                                      const char* id,
                                                      float& value,
                                                      float minimum,
                                                      float maximum) {
                    const bool connected = editor->GetNodeGraph().FindAnyInputLink(
                        node.id,
                        EditorNodeGraph::ParameterInputSocketId(
                            EditorNodeGraph::FrequencyNotchParameterId(
                                notch.id, field))) != nullptr;
                    ImGui::BeginDisabled(connected);
                    const bool parameterChanged =
                        renderSlider(label, id, &value, minimum, maximum);
                    ImGui::EndDisabled();
                    return parameterChanged;
                };
                changed |= renderNotchParameter(
                    "frequency", "Frequency", "##NotchFrequency",
                    notch.frequency, 0.0f, 0.5f);
                changed |= renderNotchParameter(
                    "direction", "Direction", "##NotchDirection",
                    notch.directionDegrees, -180.0f, 180.0f);
                changed |= renderNotchParameter(
                    "width", "Width", "##NotchWidth",
                    notch.width, 0.001f, 0.25f);
                ImGui::PopID();
            }
            if (response.notches.size() < 16 && ImGui::Button("Add mirrored notch pair")) {
                response.notches.push_back({
                    Stack::NodeMath::GenerateCanonicalUuid(),
                    0.25f, 0.0f, 0.025f
                });
                changed = true;
            }
            if (!response.notches.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("Remove last")) {
                    const std::string removedNotchId =
                        response.notches.back().id;
                    for (const char* field :
                         { "frequency", "direction", "width" }) {
                        editor->SetFrequencyParameterExposed(
                            node.id,
                            EditorNodeGraph::FrequencyNotchParameterId(
                                removedNotchId, field),
                            false);
                    }
                    response.notches.pop_back();
                    changed = true;
                }
            }
        }
        ImGui::TextDisabled("Resolution-independent response.");
        if (changed) editor->MarkGraphEdited(node.id);
    } else if (node.kind == EditorNodeGraph::NodeKind::ApplyFrequencyResponse) {
        bool changed = false;
        const bool strengthConnected = editor->GetNodeGraph().FindAnyInputLink(
            node.id,
            EditorNodeGraph::ParameterInputSocketId(
                EditorNodeGraph::kStrengthParameterId)) != nullptr;
        ImGui::BeginDisabled(strengthConnected);
        changed |= renderSlider(
            "Strength", "##ApplyFrequencyStrength",
            &node.applyFrequencyResponseSettings.strength, 0.0f, 1.0f);
        ImGui::EndDisabled();
        ImGui::TextDisabled("F × lerp(1, response, strength)");
        if (changed) editor->MarkGraphEdited(node.id);
    } else if (node.kind == EditorNodeGraph::NodeKind::CombineSpectra) {
        bool changed = false;
        int mode = static_cast<int>(node.combineSpectraSettings.mode);
        const char* modes[] = { "Add", "Subtract" };
        if (ImGuiExtras::NodeCombo(
                "Operation", "##CombineSpectraMode", &mode,
                modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.combineSpectraSettings.mode =
                static_cast<EditorNodeGraph::SpectrumCombineMode>(
                    std::clamp(mode, 0, 1));
            changed = true;
        }
        ImGui::TextDisabled("Inputs must have matching transform metadata.");
        if (changed) editor->MarkGraphEdited(node.id);
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumSeparate) {
        ImGui::TextDisabled("Raw amplitude and phase radians.");
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumRecombine) {
        ImGui::TextDisabled("Exact reversible reconstruction.");
    } else if (node.kind == EditorNodeGraph::NodeKind::FrequencyFft ||
               node.kind == EditorNodeGraph::NodeKind::FrequencyIfft) {
        bool changed = false;
        EditorNodeGraph::FrequencyFftSettings& settings =
            node.kind == EditorNodeGraph::NodeKind::FrequencyFft
                ? node.frequencyFftSettings
                : node.frequencyIfftSettings;
        if (node.kind == EditorNodeGraph::NodeKind::FrequencyFft) {
            int edgePolicy = static_cast<int>(settings.edgePolicy);
            const char* edgePolicies[] = { "Mirror", "Wrap", "Zero Pad" };
            if (ImGuiExtras::NodeCombo(
                    "Edges", "##FrequencyEdges", &edgePolicy,
                    edgePolicies, IM_ARRAYSIZE(edgePolicies), controlWidth)) {
                settings.edgePolicy = static_cast<EditorNodeGraph::FrequencyEdgePolicy>(
                    std::clamp(edgePolicy, 0, 2));
                changed = true;
            }
            ImGui::TextDisabled("Centered RG32F transform.");
        } else {
            ImGui::TextDisabled("Restores source role and crops padding.");
        }
        if (changed) {
            editor->MarkGraphEdited(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumView) {
        bool changed = false;
        int viewMode = static_cast<int>(node.spectrumViewSettings.mode);
        const char* viewModes[] = { "Magnitude", "Phase", "Real", "Imaginary" };
        if (ImGuiExtras::NodeCombo(
                "View", "##SpectrumViewMode", &viewMode,
                viewModes, IM_ARRAYSIZE(viewModes), controlWidth)) {
            node.spectrumViewSettings.mode =
                static_cast<EditorNodeGraph::SpectrumViewMode>(
                    std::clamp(viewMode, 0, 3));
            changed = true;
        }
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
            editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::SpectrumAnalyzer) {
        bool changed = false;
        const bool innerConnected = editor->GetNodeGraph().FindAnyInputLink(
            node.id,
            EditorNodeGraph::ParameterInputSocketId(
                EditorNodeGraph::kAnalyzerLowParameterId)) != nullptr;
        const bool outerConnected = editor->GetNodeGraph().FindAnyInputLink(
            node.id,
            EditorNodeGraph::ParameterInputSocketId(
                EditorNodeGraph::kAnalyzerHighParameterId)) != nullptr;
        ImGui::BeginDisabled(innerConnected);
        changed |= renderSlider("Band Low", "##SpectrumAnalyzerInner", &node.spectrumAnalyzerSettings.innerRadius, 0.0f, 0.70710678f);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(outerConnected);
        changed |= renderSlider("Band High", "##SpectrumAnalyzerOuter", &node.spectrumAnalyzerSettings.outerRadius, 0.0f, 0.70710678f);
        ImGui::EndDisabled();
        if (node.spectrumAnalyzerSettings.outerRadius < node.spectrumAnalyzerSettings.innerRadius) {
            std::swap(node.spectrumAnalyzerSettings.innerRadius, node.spectrumAnalyzerSettings.outerRadius);
            changed = true;
        }
        changed |= ImGuiExtras::NodeCheckbox(
            "Exclude DC", "##SpectrumAnalyzerExcludeDc",
            &node.spectrumAnalyzerSettings.excludeDc, controlWidth);
        ImGui::TextDisabled("256 radial bins; power uses |F|².");
        if (changed) {
            editor->MarkGraphEdited(node.id);
        }
    }
    return true;
}

bool EditorNodeGraphUI::RenderUtilityNodeContent(
    NodeContentRenderContext& context) {
    EditorModule* editor = context.editor;
    EditorNodeGraph::Graph& graph = context.graph;
    EditorNodeGraph::Node& node = context.node;
    const NodeLayoutMetrics& metrics = context.metrics;
    const NodeSurfaceSpec& nodeSurfaceSpec = context.nodeSurfaceSpec;
    const GraphStyleTokens& graphStyle = context.graphStyle;
    ImDrawList* drawList = context.drawList;
    const ImVec2 previewSize = context.previewSize;
    const float controlWidth = context.controlWidth;
    const float safeContentWidth = context.safeContentWidth;
    const float logicalControlWidth = context.logicalControlWidth;
    const float logicalSafeContentWidth = context.logicalSafeContentWidth;
    const float uiScale = context.uiScale;
    const float contentScale = context.contentScale;
    const float itemGap = context.itemGap;
    const float sectionGap = context.sectionGap;
    const bool richExpandedSurface = context.richExpandedSurface;
    const bool selected = context.selected;
    auto& captureIfActive = context.captureIfActive;
    auto& renderSlider = context.renderSlider;
    auto& drawInlineSeparator = context.drawInlineSeparator;

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::Value:
        case EditorNodeGraph::NodeKind::Compound:
        case EditorNodeGraph::NodeKind::FieldMean:
        case EditorNodeGraph::NodeKind::Reformat:
        case EditorNodeGraph::NodeKind::TechnicalImage:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::Preview:
            break;
        default:
            return false;
    }

    if (node.kind == EditorNodeGraph::NodeKind::Lut) {
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
            editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
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
            editor->GetNodeGraph().ForEachOutgoingLink(
                node.id,
                [&](const EditorNodeGraph::Link& link) {
                if (link.fromSocketId ==
                    EditorNodeGraph::kValueOutputSocketId) {
                    editor->MarkGraphEdited(link.toNodeId);
                }
            });
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Compound) {
        const Stack::NodeMath::CompoundDefinition* definition =
            graph.FindCompoundDefinition(node.compound.instance.definition);
        if (definition && node.definitionResolved) {
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
                if (const auto* defaultValue = std::get_if<double>(&parameter.defaultValue))
                    ImGuiExtras::SetNextNodeNumericDefault(*defaultValue);
                if (renderSlider(parameter.label.c_str(), widgetId.c_str(), &value, minimum, maximum)) {
                    Stack::NodeMath::SetCompoundParameterOverride(
                        node.compound.instance, parameter.id, static_cast<double>(value));
                    changed = true;
                }
            }
            if (definition->parameters.empty()) ImGui::TextDisabled("No promoted controls");
            if (changed) editor->MarkGraphEdited(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::FieldMean) {
        ImGui::TextDisabled("Arithmetic mean of every field pixel");
        ImGui::TextDisabled("Full-frame reduction · strict finite values");
    } else if (node.kind == EditorNodeGraph::NodeKind::Reformat) {
        bool changed = false;
        ImGui::SetNextItemWidth(controlWidth);
        {
            ImGuiExtras::GraphNumericValueStyle numericValueStyle;
            changed |= ImGui::InputInt("##ReformatWidth", &node.reformatSettings.width, 1, 128);
        }
        ImGui::SameLine(); ImGui::TextUnformatted("Width");
        ImGui::SetNextItemWidth(controlWidth);
        {
            ImGuiExtras::GraphNumericValueStyle numericValueStyle;
            changed |= ImGui::InputInt("##ReformatHeight", &node.reformatSettings.height, 1, 128);
        }
        ImGui::SameLine(); ImGui::TextUnformatted("Height");
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
        if (changed) editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
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
            editor->MarkGraphEdited(node.id);
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
    return true;
}

bool EditorNodeGraphUI::CanConnectInContext(const EditorNodeGraph::Graph& graph, int from, const std::string& output,
    int to, const std::string& input) const {
    if (m_GraphContext.graph != &graph || !m_GraphContext.canConnect) return graph.CanConnectSockets(from,output,to,input);
    const auto key = std::to_string(from)+"/"+output+"/"+std::to_string(to)+"/"+input;
    const auto cached = m_ConnectionCapabilityCache.find(key);
    if (cached != m_ConnectionCapabilityCache.end()) return cached->second;
    const bool accepted = m_GraphContext.canConnect(from,output,to,input,nullptr);
    m_ConnectionCapabilityCache.emplace(key,accepted);
    return accepted;
}
