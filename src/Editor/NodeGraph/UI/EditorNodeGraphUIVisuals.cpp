#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

namespace Stack::Editor::NodeGraphUIVisuals {

using EditorNodeGraphUIMetrics::CubicBezierPoint;
using EditorNodeGraphUIMetrics::NodeUiScaleFromZoom;
using EditorNodeGraphUIMetrics::PinRadiusForZoom;

ImGuiExtras::GraphSliderRangePolicy GraphSliderRangePolicyForNodeKind(EditorNodeGraph::NodeKind kind) {
    switch (kind) {
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
            return ImGuiExtras::GraphSliderRangePolicy::Bounded;
        default:
            return ImGuiExtras::GraphSliderRangePolicy::Unclamped;
    }
}

NodeFamily FamilyForNode(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Composite:
            return NodeFamily::Gray;
        case EditorNodeGraph::NodeKind::Layer:
            return NodeFamily::Layer;
        case EditorNodeGraph::NodeKind::Lut:
            return NodeFamily::Layer;
        case EditorNodeGraph::NodeKind::Preview:
            return NodeFamily::Preview;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::MaskCombine:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::DataMath:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            return NodeFamily::Mask;
        case EditorNodeGraph::NodeKind::Scope:
            return NodeFamily::Scope;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            return NodeFamily::Generator;
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::ChannelSplit:
        case EditorNodeGraph::NodeKind::ChannelCombine:
            return NodeFamily::Merge;
    }
    return NodeFamily::Gray;
}

ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        a.x + (b.x - a.x) * clampedT,
        a.y + (b.y - a.y) * clampedT,
        a.z + (b.z - a.z) * clampedT,
        a.w + (b.w - a.w) * clampedT);
}

float ColorLuminance(const ImVec4& color) {
    return (0.2126f * color.x) + (0.7152f * color.y) + (0.0722f * color.z);
}

ImVec4 WithAlpha(ImVec4 color, float alpha) {
    color.w = std::clamp(alpha, 0.0f, 1.0f) * ImGui::GetStyle().Alpha;
    return color;
}

ImVec4 WithScaledAlpha(ImVec4 color, float scale) {
    color.w = std::clamp(color.w * scale, 0.0f, 1.0f) * ImGui::GetStyle().Alpha;
    return color;
}

ImU32 ApplyStyleAlpha(ImU32 color) {
    const float styleAlpha = ImGui::GetStyle().Alpha;
    if (styleAlpha >= 0.999f) {
        return color;
    }
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= styleAlpha;
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

ImU32 ColorToU32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(color.x, color.y, color.z, std::clamp(color.w * ImGui::GetStyle().Alpha, 0.0f, 1.0f)));
}

std::string LinkAnimationKey(const EditorNodeGraph::Link& link) {
    return std::to_string(link.fromNodeId) + ":" + link.fromSocketId + ">" +
        std::to_string(link.toNodeId) + ":" + link.toSocketId;
}

ImVec4 BrightenedSelectionFill(ImVec4 fill, const ImVec4& accent, const GraphStyleTokens& tokens) {
    const ImVec4 lift = tokens.enabled
        ? BlendColor(tokens.text, accent, 0.28f)
        : ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    const float alpha = fill.w;
    fill = BlendColor(fill, lift, tokens.enabled ? 0.075f : 0.10f);
    fill.w = alpha;
    return fill;
}

ImVec4 FamilyAccent(NodeFamily family, const GraphStyleTokens& tokens) {
    const ImVec4 slate = tokens.light ? ImVec4(0.30f, 0.42f, 0.54f, 1.0f) : ImVec4(0.64f, 0.78f, 0.92f, 1.0f);
    const ImVec4 green = tokens.light ? ImVec4(0.16f, 0.58f, 0.42f, 1.0f) : ImVec4(0.44f, 0.90f, 0.62f, 1.0f);
    const ImVec4 violet = tokens.light ? ImVec4(0.54f, 0.32f, 0.76f, 1.0f) : ImVec4(0.82f, 0.62f, 1.0f, 1.0f);
    const ImVec4 amber = tokens.light ? ImVec4(0.72f, 0.46f, 0.10f, 1.0f) : ImVec4(1.0f, 0.74f, 0.34f, 1.0f);
    const ImVec4 blue = tokens.light ? ImVec4(0.16f, 0.46f, 0.84f, 1.0f) : ImVec4(0.36f, 0.72f, 1.0f, 1.0f);
    const ImVec4 cyan = tokens.light ? ImVec4(0.02f, 0.58f, 0.66f, 1.0f) : ImVec4(0.30f, 0.92f, 1.0f, 1.0f);

    switch (family) {
        case NodeFamily::Gray: return slate;
        case NodeFamily::Layer: return green;
        case NodeFamily::Preview: return amber;
        case NodeFamily::Mask: return violet;
        case NodeFamily::Scope: return amber;
        case NodeFamily::Generator: return blue;
        case NodeFamily::Merge: return cyan;
    }
    return slate;
}

GraphStyleTokens BuildGraphStyleTokens(EditorModule* editor) {
    GraphStyleTokens tokens;
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    tokens.mode = appearance ? appearance->GetGraphVisualMode() : StackAppearance::GraphVisualMode::Classic;
    tokens.enabled = tokens.mode != StackAppearance::GraphVisualMode::Classic;
    tokens.spotlightSurface = tokens.mode == StackAppearance::GraphVisualMode::SpotlightPrototype;
    tokens.haloOutlines = appearance ? (tokens.spotlightSurface && appearance->GetGraphSpotlightHaloOutlines()) : false;
    tokens.gridLineOpacity = appearance ? appearance->GetGraphLineOpacity() : 1.0f;

    const ImGuiStyle& imguiStyle = ImGui::GetStyle();
    const StackAppearance::ThemeDefinition* theme = appearance ? &appearance->GetWorkingTheme() : nullptr;
    const ImVec4 window = theme ? theme->colors[ImGuiCol_WindowBg] : imguiStyle.Colors[ImGuiCol_WindowBg];
    const ImVec4 accent = theme ? theme->colors[ImGuiCol_CheckMark] : imguiStyle.Colors[ImGuiCol_CheckMark];
    tokens.text = theme ? theme->colors[ImGuiCol_Text] : imguiStyle.Colors[ImGuiCol_Text];
    tokens.mutedText = theme ? theme->colors[ImGuiCol_TextDisabled] : imguiStyle.Colors[ImGuiCol_TextDisabled];
    tokens.light = ColorLuminance(window) >= 0.52f;

    if (!tokens.enabled) {
        tokens.canvas = window;
        tokens.nodeSurface = window;
        tokens.nodeSurfaceCollapsed = window;
        tokens.spotlightHalo = imguiStyle.Colors[ImGuiCol_Border];
        tokens.selectionGlow = accent;
        tokens.selected = accent;
        return tokens;
    }

    if (tokens.mode == StackAppearance::GraphVisualMode::BlackNodes) {
        tokens.canvas = window;
        tokens.light = false;
        tokens.text = ImVec4(0.94f, 0.96f, 0.98f, 1.0f);
        tokens.mutedText = ImVec4(0.67f, 0.72f, 0.77f, 1.0f);
        tokens.nodeSurface = WithAlpha(BlendColor(ImVec4(0.02f, 0.025f, 0.03f, 1.0f), accent, 0.03f), 0.97f);
        tokens.nodeSurfaceCollapsed = WithAlpha(BlendColor(ImVec4(0.035f, 0.04f, 0.045f, 1.0f), accent, 0.02f), 0.99f);
        tokens.spotlightCenter = tokens.nodeSurface;
        tokens.spotlightEdge = WithAlpha(tokens.canvas, 0.0f);
        tokens.spotlightHalo = WithAlpha(BlendColor(accent, tokens.text, 0.22f), 0.22f);
        tokens.selectionGlow = WithAlpha(BlendColor(accent, tokens.text, 0.30f), 0.54f);
        tokens.selected = WithAlpha(BlendColor(accent, tokens.text, 0.40f), 0.96f);

        tokens.socketImage = ImVec4(0.44f, 0.80f, 1.0f, 1.0f);
        tokens.socketMask = ImVec4(0.82f, 0.58f, 1.0f, 1.0f);
        tokens.socketAnalysis = ImVec4(1.0f, 0.72f, 0.34f, 1.0f);
        tokens.socketValue = ImVec4(0.46f, 0.94f, 0.94f, 1.0f);
        tokens.socketRaw = ImVec4(0.48f, 0.94f, 0.62f, 1.0f);

        tokens.linkImage = WithAlpha(tokens.socketImage, 0.92f);
        tokens.linkMask = WithAlpha(BlendColor(tokens.socketMask, tokens.socketRaw, 0.14f), 0.90f);
        tokens.linkAnalysis = WithAlpha(tokens.socketAnalysis, 0.88f);
        tokens.linkUnderlay = ImVec4(0.0f, 0.0f, 0.0f, 0.62f);
        tokens.groupFill = ImVec4(0.02f, 0.03f, 0.04f, 0.46f);
        tokens.groupHeader = WithAlpha(BlendColor(tokens.groupFill, accent, 0.10f), 0.72f);
        tokens.groupBorder = WithAlpha(BlendColor(accent, tokens.text, 0.18f), 0.48f);
        return tokens;
    }

    const ImVec4 coolDark(0.00f, 0.10f, 0.13f, 1.0f);
    const ImVec4 coolLight(0.86f, 0.96f, 1.0f, 1.0f);
    tokens.canvas = tokens.light
        ? BlendColor(window, coolLight, 0.20f)
        : BlendColor(window, coolDark, 0.36f);
    const float canvasLuminance = ColorLuminance(tokens.canvas);
    const ImVec4 canvasGray(canvasLuminance, canvasLuminance, canvasLuminance, 1.0f);
    const ImVec4 desaturatedCanvas = BlendColor(tokens.canvas, canvasGray, tokens.light ? 0.24f : 0.34f);
    const ImVec4 shiftedCanvas = tokens.light
        ? BlendColor(desaturatedCanvas, ImVec4(0.0f, 0.0f, 0.0f, 1.0f), 0.18f)
        : BlendColor(desaturatedCanvas, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.32f);
    tokens.spotlightCenter = WithAlpha(shiftedCanvas, tokens.light ? 0.68f : 0.84f);
    tokens.spotlightEdge = WithAlpha(tokens.canvas, 0.0f);
    tokens.spotlightHalo = WithAlpha(BlendColor(accent, tokens.text, tokens.light ? 0.10f : 0.18f), tokens.light ? 0.34f : 0.40f);
    tokens.selectionGlow = WithAlpha(BlendColor(accent, tokens.text, 0.22f), tokens.light ? 0.56f : 0.68f);
    tokens.nodeSurface = tokens.spotlightCenter;
    tokens.nodeSurfaceCollapsed = WithAlpha(tokens.spotlightCenter, tokens.light ? 0.56f : 0.72f);
    tokens.selected = WithAlpha(BlendColor(accent, tokens.text, 0.18f), 0.95f);

    tokens.socketImage = tokens.light ? ImVec4(0.08f, 0.42f, 0.80f, 1.0f) : ImVec4(0.42f, 0.78f, 1.0f, 1.0f);
    tokens.socketMask = tokens.light ? ImVec4(0.56f, 0.28f, 0.76f, 1.0f) : ImVec4(0.82f, 0.56f, 1.0f, 1.0f);
    tokens.socketAnalysis = tokens.light ? ImVec4(0.76f, 0.44f, 0.08f, 1.0f) : ImVec4(1.0f, 0.72f, 0.34f, 1.0f);
    tokens.socketValue = tokens.light ? ImVec4(0.02f, 0.54f, 0.60f, 1.0f) : ImVec4(0.44f, 0.90f, 0.92f, 1.0f);
    tokens.socketRaw = tokens.light ? ImVec4(0.12f, 0.56f, 0.32f, 1.0f) : ImVec4(0.48f, 0.94f, 0.62f, 1.0f);

    tokens.linkImage = WithAlpha(tokens.socketImage, 0.86f);
    tokens.linkMask = WithAlpha(BlendColor(tokens.socketMask, tokens.socketRaw, 0.18f), 0.88f);
    tokens.linkAnalysis = WithAlpha(tokens.socketAnalysis, 0.82f);
    tokens.linkUnderlay = tokens.light ? ImVec4(0.0f, 0.08f, 0.12f, 0.16f) : ImVec4(0.0f, 0.0f, 0.0f, 0.44f);
    tokens.groupFill = tokens.light ? ImVec4(0.82f, 0.94f, 0.98f, 0.28f) : ImVec4(0.02f, 0.12f, 0.16f, 0.34f);
    tokens.groupHeader = WithAlpha(BlendColor(tokens.groupFill, accent, 0.18f), tokens.light ? 0.42f : 0.48f);
    tokens.groupBorder = WithAlpha(BlendColor(accent, tokens.text, 0.16f), tokens.light ? 0.42f : 0.52f);
    return tokens;
}

GraphZoomDialStyle BuildGraphZoomDialStyle(EditorModule* editor, const GraphStyleTokens& tokens) {
    GraphZoomDialStyle style {};
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    const std::string presetId = appearance ? appearance->GetActivePresetId() : std::string();

    ImVec4 baseColor = tokens.text;
    if (presetId == StackAppearance::kSolarizedPresetId) {
        baseColor = ImVec4(0.965f, 0.925f, 0.820f, 1.0f);
    } else if (presetId == StackAppearance::kDarkPresetId) {
        baseColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    } else if (presetId == StackAppearance::kSolarizedLightPresetId) {
        baseColor = ImVec4(0.408f, 0.329f, 0.251f, 1.0f);
    }

    const float tickAlpha = tokens.light ? 0.82f : 0.90f;
    const float glowAlpha = tokens.light ? 0.12f : 0.18f;
    style.tick = WithAlpha(baseColor, tickAlpha);
    style.glow = WithAlpha(baseColor, glowAlpha);
    return style;
}

bool GraphDottedMaskLinksEnabled(EditorModule* editor) {
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    return appearance ? appearance->GetGraphDottedMaskLinks() : true;
}

bool IsSummaryOnlyNode(const EditorNodeGraphUI* ui, const EditorModule* editor, const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawDevelopment:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::CustomMask:
            return true;
        default:
            break;
    }
    return ui && ui->ResolveNodeUsesSidebarOnlyComplexEditor(editor, node);
}

NodePresentationProfile BuildNodePresentationProfile(
    const EditorNodeGraphUI* ui,
    const EditorModule* editor,
    const EditorNodeGraph::Node& node,
    const GraphStyleTokens& tokens) {
    NodePresentationProfile profile;
    (void)tokens;

    profile.showKindLabel = false;
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Output:
            profile.kind = NodePresentationKind::FramelessMedia;
            profile.showFrame = false;
            profile.showTitle = false;
            profile.inlineControls = false;
            profile.hoverDetails = true;
            break;
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawDevelopment:
            profile.kind = NodePresentationKind::SummaryOnly;
            profile.inlineControls = false;
            profile.hoverDetails = true;
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDecode:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::CustomMask:
            profile.kind = NodePresentationKind::SummaryOnly;
            profile.inlineControls = false;
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
        case EditorNodeGraph::NodeKind::ChannelCombine:
            profile.kind = NodePresentationKind::RouteSquare;
            break;
        case EditorNodeGraph::NodeKind::Preview:
            profile.kind = NodePresentationKind::PreviewPanel;
            break;
        case EditorNodeGraph::NodeKind::Scope:
            profile.kind = NodePresentationKind::ScopePanel;
            break;
        default:
            if (ui && ui->ResolveNodeUsesSidebarOnlyComplexEditor(editor, node)) {
                profile.kind = NodePresentationKind::SummaryOnly;
                profile.inlineControls = false;
            } else {
                profile.kind = NodePresentationKind::CompactControls;
                profile.showTitle = node.kind != EditorNodeGraph::NodeKind::Layer;
            }
            break;
    }
    return profile;
}

NodeFamilyStyle StyleForFamily(NodeFamily family, const GraphStyleTokens& tokens) {
    NodeFamilyStyle style = StyleForFamily(family);
    if (!tokens.enabled) {
        return style;
    }

    const ImVec4 accent = FamilyAccent(family, tokens);
    if (!tokens.spotlightSurface) {
        style.fill = WithAlpha(BlendColor(tokens.nodeSurface, accent, 0.028f), tokens.nodeSurface.w);
        style.border = WithAlpha(BlendColor(tokens.spotlightHalo, accent, 0.10f), 0.72f);
        style.accent = accent;
        style.text = tokens.text;
        style.mutedText = BlendColor(tokens.mutedText, accent, 0.05f);
        return style;
    }

    style.fill = WithAlpha(BlendColor(tokens.nodeSurface, accent, tokens.light ? 0.04f : 0.06f), tokens.nodeSurface.w);
    style.border = WithAlpha(BlendColor(tokens.spotlightHalo, accent, 0.28f), tokens.light ? 0.30f : 0.36f);
    style.accent = accent;
    style.text = tokens.text;
    style.mutedText = BlendColor(tokens.mutedText, accent, tokens.light ? 0.08f : 0.12f);
    return style;
}

const NodeFamilyStyle& StyleForFamily(NodeFamily family) {
    static const NodeFamilyStyle kGray {
        ImVec4(0.31f, 0.34f, 0.36f, 0.93f),
        ImVec4(0.44f, 0.47f, 0.49f, 0.96f),
        ImVec4(0.56f, 0.60f, 0.62f, 1.0f),
        ImVec4(0.92f, 0.94f, 0.95f, 1.0f),
        ImVec4(0.76f, 0.79f, 0.81f, 1.0f)
    };
    static const NodeFamilyStyle kLayer {
        ImVec4(0.30f, 0.35f, 0.33f, 0.93f),
        ImVec4(0.42f, 0.49f, 0.45f, 0.96f),
        ImVec4(0.55f, 0.66f, 0.60f, 1.0f),
        ImVec4(0.92f, 0.95f, 0.93f, 1.0f),
        ImVec4(0.76f, 0.82f, 0.79f, 1.0f)
    };
    static const NodeFamilyStyle kPreview {
        ImVec4(0.36f, 0.35f, 0.28f, 0.93f),
        ImVec4(0.50f, 0.48f, 0.39f, 0.96f),
        ImVec4(0.68f, 0.65f, 0.50f, 1.0f),
        ImVec4(0.97f, 0.96f, 0.90f, 1.0f),
        ImVec4(0.84f, 0.82f, 0.72f, 1.0f)
    };
    static const NodeFamilyStyle kMask {
        ImVec4(0.34f, 0.30f, 0.38f, 0.93f),
        ImVec4(0.48f, 0.42f, 0.54f, 0.96f),
        ImVec4(0.66f, 0.58f, 0.76f, 1.0f),
        ImVec4(0.95f, 0.93f, 0.98f, 1.0f),
        ImVec4(0.82f, 0.79f, 0.88f, 1.0f)
    };
    static const NodeFamilyStyle kScope {
        ImVec4(0.38f, 0.31f, 0.26f, 0.93f),
        ImVec4(0.53f, 0.42f, 0.35f, 0.96f),
        ImVec4(0.73f, 0.57f, 0.45f, 1.0f),
        ImVec4(0.98f, 0.94f, 0.90f, 1.0f),
        ImVec4(0.87f, 0.78f, 0.72f, 1.0f)
    };
    static const NodeFamilyStyle kGenerator {
        ImVec4(0.27f, 0.32f, 0.37f, 0.93f),
        ImVec4(0.39f, 0.46f, 0.54f, 0.96f),
        ImVec4(0.53f, 0.64f, 0.76f, 1.0f),
        ImVec4(0.92f, 0.95f, 0.98f, 1.0f),
        ImVec4(0.77f, 0.82f, 0.88f, 1.0f)
    };
    static const NodeFamilyStyle kMerge {
        ImVec4(0.24f, 0.35f, 0.36f, 0.93f),
        ImVec4(0.35f, 0.50f, 0.51f, 0.96f),
        ImVec4(0.49f, 0.69f, 0.70f, 1.0f),
        ImVec4(0.91f, 0.97f, 0.97f, 1.0f),
        ImVec4(0.75f, 0.85f, 0.85f, 1.0f)
    };

    switch (family) {
        case NodeFamily::Gray: return kGray;
        case NodeFamily::Layer: return kLayer;
        case NodeFamily::Preview: return kPreview;
        case NodeFamily::Mask: return kMask;
        case NodeFamily::Scope: return kScope;
        case NodeFamily::Generator: return kGenerator;
        case NodeFamily::Merge: return kMerge;
    }
    return kGray;
}

NodeLayoutMetrics MetricsForNode(const EditorNodeGraph::Node& node) {
    NodeLayoutMetrics metrics;
    if (node.kind == EditorNodeGraph::NodeKind::Output) {
        metrics.width = 176.0f;
        metrics.collapsedHeight = 108.0f;
        metrics.minExpandedHeight = 108.0f;
        metrics.contentLaneWidth = 156.0f;
        metrics.previewWidth = 156.0f;
        metrics.previewHeight = 96.0f;
        return metrics;
    }

    if (node.kind == EditorNodeGraph::NodeKind::Composite) {
        metrics.width = 286.0f;
        metrics.contentLaneWidth = 220.0f;
        metrics.previewWidth = 220.0f;
        metrics.previewHeight = 148.0f;
        metrics.minExpandedHeight = 360.0f;
        metrics.sectionGap = 12.0f;
        metrics.itemGap = 9.0f;
        return metrics;
    }

    switch (FamilyForNode(node)) {
        case NodeFamily::Gray:
            metrics.width = 232.0f;
            metrics.contentLaneWidth = 168.0f;
            metrics.previewWidth = 156.0f;
            metrics.previewHeight = 88.0f;
            metrics.minExpandedHeight = 128.0f;
            break;
        case NodeFamily::Layer:
            metrics.width = 334.0f;
            metrics.contentLaneWidth = 252.0f;
            metrics.previewWidth = 206.0f;
            metrics.previewHeight = 100.0f;
            metrics.minExpandedHeight = 168.0f;
            break;
        case NodeFamily::Preview:
            metrics.width = 266.0f;
            metrics.contentLaneWidth = 194.0f;
            metrics.previewWidth = 194.0f;
            metrics.previewHeight = 114.0f;
            metrics.minExpandedHeight = 196.0f;
            break;
        case NodeFamily::Mask:
            metrics.width = 282.0f;
            metrics.contentLaneWidth = 202.0f;
            metrics.minExpandedHeight = 126.0f;
            break;
        case NodeFamily::Scope:
            metrics.width = 294.0f;
            metrics.contentLaneWidth = 210.0f;
            metrics.scopeHeight = 186.0f;
            metrics.minExpandedHeight = 256.0f;
            break;
        case NodeFamily::Generator:
            metrics.width = 286.0f;
            metrics.contentLaneWidth = 204.0f;
            metrics.minExpandedHeight = 132.0f;
            break;
        case NodeFamily::Merge:
            metrics.width = 262.0f;
            metrics.contentLaneWidth = 184.0f;
            metrics.minExpandedHeight = 128.0f;
            break;
    }
    return metrics;
}

void ApplyModernCompactMetrics(const EditorNodeGraph::Node& node, NodeLayoutMetrics& metrics) {
    metrics.headerInsetX = 12.0f;
    metrics.headerInsetY = 10.0f;
    metrics.bodyInsetBottom = 10.0f;
    metrics.itemGap = 6.0f;
    metrics.sectionGap = 7.0f;
    metrics.titleHeight = 16.0f;
    metrics.kindLabelHeight = 0.0f;
    metrics.rowHeight = 22.0f;
    metrics.sliderHeight = 20.0f;

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Output:
            metrics.width = 176.0f;
            metrics.collapsedHeight = 108.0f;
            metrics.minExpandedHeight = 108.0f;
            metrics.contentLaneWidth = 156.0f;
            metrics.previewWidth = 156.0f;
            metrics.previewHeight = 96.0f;
            break;
        case EditorNodeGraph::NodeKind::RawSource:
            metrics.width = 128.0f;
            metrics.collapsedHeight = 72.0f;
            metrics.minExpandedHeight = 72.0f;
            metrics.contentLaneWidth = 100.0f;
            break;
        case EditorNodeGraph::NodeKind::Layer:
            metrics.width = 250.0f;
            metrics.contentLaneWidth = 204.0f;
            metrics.minExpandedHeight = 70.0f;
            metrics.collapsedHeight = 48.0f;
            metrics.previewWidth = 176.0f;
            metrics.previewHeight = 86.0f;
            break;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::DataMath:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::MaskCombine:
            metrics.width = std::min(metrics.width, 238.0f);
            metrics.contentLaneWidth = std::min(metrics.contentLaneWidth, 192.0f);
            metrics.minExpandedHeight = std::max(74.0f, metrics.minExpandedHeight - 24.0f);
            metrics.collapsedHeight = 48.0f;
            break;
        case EditorNodeGraph::NodeKind::Preview:
            metrics.width = 232.0f;
            metrics.contentLaneWidth = 196.0f;
            metrics.previewWidth = 196.0f;
            metrics.previewHeight = 118.0f;
            metrics.minExpandedHeight = 168.0f;
            break;
        case EditorNodeGraph::NodeKind::Scope:
            metrics.width = 268.0f;
            metrics.contentLaneWidth = 220.0f;
            metrics.scopeHeight = 168.0f;
            metrics.minExpandedHeight = 226.0f;
            break;
        case EditorNodeGraph::NodeKind::Composite:
            metrics.width = 270.0f;
            metrics.contentLaneWidth = 220.0f;
            break;
        default:
            break;
    }
}

void ApplyLayerSurfaceMetrics(const EditorNodeGraphUI* ui, const EditorModule* editor, const EditorNodeGraph::Node& node, NodeLayoutMetrics& metrics) {
    if (!ui || !editor || node.kind != EditorNodeGraph::NodeKind::Layer) {
        return;
    }
    const NodeSurfaceSpec spec = ui->ResolveLayerSurfaceSpec(editor, node.layerIndex);
    if (spec.presentation != NodeSurfacePresentation::RichExpandedSurface) {
        return;
    }

    const float minWidth = 300.0f;
    const float maxWidth = std::max(minWidth, std::max(spec.preferredWidth, spec.maxWidth));
    metrics.width = std::clamp(spec.preferredWidth, minWidth, maxWidth);
    metrics.contentLaneWidth = std::max(180.0f, metrics.width - 78.0f);
    metrics.minExpandedHeight = spec.density == NodeSurfaceDensity::UltraDense ? 188.0f : 204.0f;
    metrics.sectionGap = spec.density == NodeSurfaceDensity::UltraDense ? 7.0f : 8.0f;
    metrics.itemGap = spec.density == NodeSurfaceDensity::UltraDense ? 5.0f : 6.0f;
    metrics.rowHeight = spec.density == NodeSurfaceDensity::UltraDense ? 20.0f : 22.0f;
    metrics.sliderHeight = spec.density == NodeSurfaceDensity::UltraDense ? 16.0f : 18.0f;
    metrics.colorRowHeight = spec.density == NodeSurfaceDensity::UltraDense ? 22.0f : 24.0f;
    metrics.checkboxHeight = spec.density == NodeSurfaceDensity::UltraDense ? 16.0f : 18.0f;
}

ImU32 ColorWithAlpha(const ImVec4& color, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(color.x, color.y, color.z, std::clamp(alpha, 0.0f, 1.0f) * ImGui::GetStyle().Alpha));
}

bool HasDedicatedComplexEditor(const EditorNodeGraphUI* ui, const EditorModule* editor, const EditorNodeGraph::Node& node) {
    return ui && ui->ResolveNodeHasDedicatedComplexEditor(editor, node);
}

std::string CompactAdvancedLayerLabel(const EditorNodeGraph::Node& node) {
    if (node.layerType == LayerType::BackgroundPatcher) {
        return "Remover";
    }
    if (node.layerType == LayerType::ColorGrade) {
        return "Grade";
    }

    std::string label = node.title.empty() ? "Advanced" : node.title;
    const std::size_t space = label.find(' ');
    if (space != std::string::npos && space > 0) {
        label = label.substr(0, space);
    }
    if (label.size() > 8) {
        label = label.substr(0, 8);
    }
    return label.empty() ? std::string("Advanced") : label;
}

const char* ChannelDisplayName(const std::string& channel) {
    if (channel == "r") return "R";
    if (channel == "g") return "G";
    if (channel == "b") return "B";
    if (channel == "a") return "A";
    return "";
}

ImVec4 ChannelPolicyColor(LayerChannelPolicy policy) {
    switch (policy) {
        case LayerChannelPolicy::ChannelSafe:
            return ImVec4(0.42f, 0.72f, 0.56f, 1.0f);
        case LayerChannelPolicy::ChannelUsefulWithWarning:
            return ImVec4(0.88f, 0.70f, 0.34f, 1.0f);
        case LayerChannelPolicy::FullImagePreferred:
        case LayerChannelPolicy::FullImageOnly:
        case LayerChannelPolicy::ReworkBeforeExpose:
            return ImVec4(0.95f, 0.55f, 0.38f, 1.0f);
    }
    return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
}

void RenderLayerMetadataNotes(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node,
    float controlWidth) {
    const LayerDescriptor* descriptor = LayerRegistry::GetDescriptor(node.layerType);
    if (!descriptor) {
        return;
    }

    const std::string channel = graph.ResolveSocketChannel(node.id, EditorNodeGraph::kImageOutputSocketId);
    const bool hasChannel = !channel.empty();
    const bool isScalarStream = !hasChannel && graph.IsScalarSocketStream(node.id, EditorNodeGraph::kImageOutputSocketId);
    const bool lifecycleNeedsNote = descriptor->lifecycleStatus != LayerLifecycleStatus::Stable;
    if (!hasChannel && !isScalarStream && !lifecycleNeedsNote) {
        return;
    }

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + std::max(120.0f, controlWidth));
    if (hasChannel) {
        ImGui::TextColored(
            ChannelPolicyColor(descriptor->channelPolicy),
            "Channel stream: %s (%s)",
            ChannelDisplayName(channel),
            LayerRegistry::ChannelPolicyLabel(descriptor->channelPolicy));
        if (descriptor->channelPolicy != LayerChannelPolicy::ChannelSafe &&
            descriptor->channelNote &&
            descriptor->channelNote[0] != '\0') {
            ImGui::TextDisabled("%s", descriptor->channelNote);
        }
    } else if (isScalarStream) {
        ImGui::TextColored(
            ChannelPolicyColor(descriptor->channelPolicy),
            "Scalar stream (%s)",
            LayerRegistry::ChannelPolicyLabel(descriptor->channelPolicy));
        if (descriptor->channelPolicy != LayerChannelPolicy::ChannelSafe &&
            descriptor->channelNote &&
            descriptor->channelNote[0] != '\0') {
            ImGui::TextDisabled("%s", descriptor->channelNote);
        }
    }

    if (lifecycleNeedsNote) {
        ImGui::TextColored(
            ImVec4(0.90f, 0.68f, 0.32f, 1.0f),
            "Status: %s",
            LayerRegistry::LifecycleStatusLabel(descriptor->lifecycleStatus));
        if (descriptor->lifecycleNote && descriptor->lifecycleNote[0] != '\0') {
            ImGui::TextDisabled("%s", descriptor->lifecycleNote);
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
}

float NodeControlWidthForScale(float logicalWidth, float uiScale) {
    return std::max(1.0f, logicalWidth * uiScale);
}

ImVec2 NodePreviewSizeForScale(const NodeLayoutMetrics& metrics, float uiScale) {
    return ImVec2(std::max(1.0f, metrics.previewWidth * uiScale), std::max(1.0f, metrics.previewHeight * uiScale));
}

ImU32 TypedSocketColor(EditorNodeGraph::SocketType type, const NodeFamilyStyle& familyStyle) {
    const ImVec4 imageBase(0.60f, 0.69f, 0.79f, 1.0f);
    const ImVec4 maskBase(0.71f, 0.64f, 0.82f, 1.0f);
    const ImVec4 analysisBase(0.76f, 0.68f, 0.57f, 1.0f);
    const ImVec4 valueBase(0.66f, 0.73f, 0.73f, 1.0f);
    const ImVec4 rawBase(0.62f, 0.77f, 0.66f, 1.0f);
    ImVec4 base = imageBase;
    switch (type) {
        case EditorNodeGraph::SocketType::Image: base = imageBase; break;
        case EditorNodeGraph::SocketType::Mask: base = maskBase; break;
        case EditorNodeGraph::SocketType::Analysis: base = analysisBase; break;
        case EditorNodeGraph::SocketType::Value: base = valueBase; break;
        case EditorNodeGraph::SocketType::Raw: base = rawBase; break;
    }
    return ColorToU32(BlendColor(base, familyStyle.accent, 0.38f));
}

bool IsChannelSocketId(const std::string& id) {
    return id == "r" || id == "g" || id == "b" || id == "a";
}

LinkVisualStyle ResolveLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    LinkVisualStyle style;
    EditorNodeGraph::SocketDefinition fromSocket;
    EditorNodeGraph::SocketDefinition toSocket;
    if (!graph.FindSocket(fromNodeId, fromSocketId, &fromSocket) ||
        !graph.FindSocket(toNodeId, toSocketId, &toSocket)) {
        if (IsChannelSocketId(fromSocketId)) {
            style.channel = fromSocketId;
        } else if (IsChannelSocketId(toSocketId)) {
            style.channel = toSocketId;
        }
        return style;
    }

    style.channel = graph.ResolveSocketChannel(fromNodeId, fromSocketId);
    if (style.channel.empty() && IsChannelSocketId(toSocketId)) {
        style.channel = toSocketId;
    }
    style.scalarStream = fromSocket.type == EditorNodeGraph::SocketType::Mask ||
        graph.IsScalarSocketStream(fromNodeId, fromSocketId);

    const EditorNodeGraph::Link probeLink { fromNodeId, fromSocketId, toNodeId, toSocketId };
    if (graph.GetLinkRole(probeLink) == EditorNodeGraph::LinkRole::Scope) {
        style.kind = LinkVisualKind::Analysis;
        return style;
    }
    if (fromSocket.type == EditorNodeGraph::SocketType::Raw ||
        toSocket.type == EditorNodeGraph::SocketType::Raw) {
        style.kind = LinkVisualKind::Raw;
        return style;
    }
    if (fromSocket.type == EditorNodeGraph::SocketType::Mask ||
        toSocket.type == EditorNodeGraph::SocketType::Mask) {
        style.kind = LinkVisualKind::MaskEndpoint;
        style.dotted = true;
        return style;
    }
    return style;
}

LinkVisualStyle ResolveLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Link& link) {
    return ResolveLinkVisualStyle(graph, link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId);
}

LinkVisualStyle ResolvePendingLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    int outputNodeId,
    const std::string& outputSocketId,
    int inputNodeId,
    const std::string& inputSocketId) {
    return ResolveLinkVisualStyle(graph, outputNodeId, outputSocketId, inputNodeId, inputSocketId);
}

LinkVisualStyle ResolvePendingLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    int nodeId,
    const std::string& socketId,
    EditorNodeGraph::SocketDirection direction) {
    LinkVisualStyle style;
    EditorNodeGraph::SocketDefinition socket;
    if (!graph.FindSocket(nodeId, socketId, &socket)) {
        if (IsChannelSocketId(socketId)) {
            style.channel = socketId;
        }
        return style;
    }

    style.channel = IsChannelSocketId(socketId) ? socketId : graph.ResolveSocketChannel(nodeId, socketId);
    style.scalarStream = direction == EditorNodeGraph::SocketDirection::Output &&
        (socket.type == EditorNodeGraph::SocketType::Mask || graph.IsScalarSocketStream(nodeId, socketId));

    if (socket.type == EditorNodeGraph::SocketType::Analysis) {
        style.kind = LinkVisualKind::Analysis;
        return style;
    }
    if (socket.type == EditorNodeGraph::SocketType::Raw) {
        style.kind = LinkVisualKind::Raw;
        return style;
    }
    if (socket.type == EditorNodeGraph::SocketType::Mask) {
        style.kind = LinkVisualKind::MaskEndpoint;
        style.dotted = true;
        return style;
    }
    return style;
}

ImVec4 ChannelColorVec(const std::string& channel, const GraphStyleTokens& tokens) {
    const bool light = tokens.enabled && tokens.light;
    if (channel == "r") return light ? ImVec4(0.82f, 0.12f, 0.12f, 1.0f) : ImVec4(1.0f, 0.24f, 0.24f, 1.0f);
    if (channel == "g") return light ? ImVec4(0.05f, 0.62f, 0.24f, 1.0f) : ImVec4(0.32f, 1.0f, 0.42f, 1.0f);
    if (channel == "b") return light ? ImVec4(0.10f, 0.34f, 0.92f, 1.0f) : ImVec4(0.34f, 0.56f, 1.0f, 1.0f);
    if (channel == "a") return light ? ImVec4(0.50f, 0.54f, 0.58f, 1.0f) : ImVec4(0.90f, 0.92f, 0.94f, 1.0f);
    return tokens.enabled ? tokens.socketImage : ImVec4(0.60f, 0.69f, 0.79f, 1.0f);
}

ImVec4 SocketColorVec(
    const EditorNodeGraph::SocketDefinition& socket,
    const NodeFamilyStyle& familyStyle,
    const GraphStyleTokens& tokens) {
    if (IsChannelSocketId(socket.id)) {
        return ChannelColorVec(socket.id, tokens);
    }
    if (!tokens.enabled) {
        return ImGui::ColorConvertU32ToFloat4(TypedSocketColor(socket.type, familyStyle));
    }
    switch (socket.type) {
        case EditorNodeGraph::SocketType::Image: return BlendColor(tokens.socketImage, familyStyle.accent, 0.16f);
        case EditorNodeGraph::SocketType::Mask: return BlendColor(tokens.socketMask, familyStyle.accent, 0.12f);
        case EditorNodeGraph::SocketType::Analysis: return BlendColor(tokens.socketAnalysis, familyStyle.accent, 0.10f);
        case EditorNodeGraph::SocketType::Value: return BlendColor(tokens.socketValue, familyStyle.accent, 0.10f);
        case EditorNodeGraph::SocketType::Raw: return BlendColor(tokens.socketRaw, familyStyle.accent, 0.12f);
    }
    return tokens.socketImage;
}

ImU32 SocketColor(
    const EditorNodeGraph::SocketDefinition& socket,
    const NodeFamilyStyle& familyStyle,
    const GraphStyleTokens& tokens) {
    return ColorToU32(SocketColorVec(socket, familyStyle, tokens));
}

ImVec4 LinkColorVec(const LinkVisualStyle& style, const GraphStyleTokens& tokens) {
    if (!style.channel.empty()) {
        return WithAlpha(ChannelColorVec(style.channel, tokens), 0.88f);
    }

    switch (style.kind) {
        case LinkVisualKind::Analysis:
            return tokens.enabled ? tokens.linkAnalysis : ImVec4(0.51f, 0.90f, 0.67f, 0.90f);
        case LinkVisualKind::MaskEndpoint:
            return tokens.enabled ? tokens.linkMask : ImVec4(0.51f, 0.90f, 0.67f, 0.90f);
        case LinkVisualKind::Raw:
        case LinkVisualKind::Image:
            return tokens.enabled ? tokens.linkImage : ImVec4(0.47f, 0.67f, 1.0f, 0.90f);
    }
    return tokens.enabled ? tokens.linkImage : ImVec4(0.47f, 0.67f, 1.0f, 0.90f);
}

ImU32 LinkColorClassic(const LinkVisualStyle& style, bool selected) {
    if (selected) {
        return ApplyStyleAlpha(IM_COL32(255, 255, 255, 255));
    }
    if (!style.channel.empty()) {
        if (style.channel == "r") return ApplyStyleAlpha(IM_COL32(255, 64, 64, 210));
        if (style.channel == "g") return ApplyStyleAlpha(IM_COL32(64, 255, 64, 210));
        if (style.channel == "b") return ApplyStyleAlpha(IM_COL32(64, 128, 255, 210));
        if (style.channel == "a") return ApplyStyleAlpha(IM_COL32(220, 220, 220, 210));
    }

    switch (style.kind) {
        case LinkVisualKind::Analysis:
        case LinkVisualKind::MaskEndpoint:
            return ApplyStyleAlpha(IM_COL32(130, 230, 170, 230));
        case LinkVisualKind::Raw:
        case LinkVisualKind::Image:
            return ApplyStyleAlpha(IM_COL32(120, 170, 255, 230));
    }
    return ApplyStyleAlpha(IM_COL32(120, 170, 255, 230));
}

void DrawDottedBezierStroke(
    ImDrawList* drawList,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    ImU32 color,
    float thickness) {
    auto pointDistance = [](const ImVec2& a, const ImVec2& b) {
        const float dx = b.x - a.x;
        const float dy = b.y - a.y;
        return std::sqrt((dx * dx) + (dy * dy));
    };
    const float radius = std::max(0.25f, thickness * 0.5f);
    const float spacing = std::max(1.4f, radius * 2.6f);
    const float estimate =
        pointDistance(p0, p1) +
        pointDistance(p1, p2) +
        pointDistance(p2, p3);
    const int sampleCount = std::clamp(static_cast<int>(estimate / 6.0f), 24, 128);

    ImVec2 previous = p0;
    float distanceToNextDot = 0.0f;
    for (int index = 1; index <= sampleCount; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(sampleCount);
        const ImVec2 current = CubicBezierPoint(p0, p1, p2, p3, t);
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
            drawList->AddCircleFilled(dotPos, radius, color);
            distanceToNextDot += spacing;
        }

        distanceToNextDot -= segmentLength;
        previous = current;
    }
}

void DrawBezierLinkStroke(
    ImDrawList* drawList,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    ImU32 color,
    float thickness,
    bool dotted) {
    if (dotted) {
        DrawDottedBezierStroke(drawList, p0, p1, p2, p3, color, thickness);
        return;
    }
    drawList->AddBezierCubic(p0, p1, p2, p3, color, thickness);
}

ImVec2 SuperellipsePoint(const ImVec2& center, float radiusX, float radiusY, float angle, float exponent) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float power = 2.0f / std::max(0.1f, exponent);
    const float x = std::copysign(std::pow(std::abs(c), power), c) * radiusX;
    const float y = std::copysign(std::pow(std::abs(s), power), s) * radiusY;
    return ImVec2(center.x + x, center.y + y);
}

void DrawSoftSpotlightBlob(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    ImVec4 centerColor,
    ImVec4 edgeColor,
    float feather,
    float exponent,
    float coreRatio,
    float edgePower) {
    if (centerColor.w <= 0.001f || max.x <= min.x || max.y <= min.y) {
        return;
    }

    constexpr int kSegments = 64;
    constexpr int kRings = 12;
    constexpr float kTau = 6.28318530718f;

    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float radiusX = (max.x - min.x) * 0.5f + feather;
    const float radiusY = (max.y - min.y) * 0.5f + feather;
    const int vtxCount = 1 + (kRings * kSegments);
    const int idxCount = (kSegments * 3) + ((kRings - 1) * kSegments * 6);
    const ImVec2 uv = drawList->_Data->TexUvWhitePixel;

    drawList->PrimReserve(idxCount, vtxCount);
    const ImDrawIdx base = static_cast<ImDrawIdx>(drawList->_VtxCurrentIdx);

    for (int segment = 0; segment < kSegments; ++segment) {
        const int next = (segment + 1) % kSegments;
        drawList->PrimWriteIdx(base);
        drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + segment));
        drawList->PrimWriteIdx(static_cast<ImDrawIdx>(base + 1 + next));
    }

    for (int ring = 1; ring < kRings; ++ring) {
        const int previousStart = 1 + ((ring - 1) * kSegments);
        const int currentStart = 1 + (ring * kSegments);
        for (int segment = 0; segment < kSegments; ++segment) {
            const int next = (segment + 1) % kSegments;
            const ImDrawIdx previous = static_cast<ImDrawIdx>(base + previousStart + segment);
            const ImDrawIdx previousNext = static_cast<ImDrawIdx>(base + previousStart + next);
            const ImDrawIdx current = static_cast<ImDrawIdx>(base + currentStart + segment);
            const ImDrawIdx currentNext = static_cast<ImDrawIdx>(base + currentStart + next);
            drawList->PrimWriteIdx(previous);
            drawList->PrimWriteIdx(current);
            drawList->PrimWriteIdx(currentNext);
            drawList->PrimWriteIdx(previous);
            drawList->PrimWriteIdx(currentNext);
            drawList->PrimWriteIdx(previousNext);
        }
    }

    drawList->PrimWriteVtx(center, uv, ColorToU32(centerColor));
    const float clampedCoreRatio = std::clamp(coreRatio, 0.05f, 0.92f);
    for (int ring = 0; ring < kRings; ++ring) {
        const float ratio = static_cast<float>(ring + 1) / static_cast<float>(kRings);
        const float fadeT = std::clamp((ratio - clampedCoreRatio) / std::max(0.001f, 1.0f - clampedCoreRatio), 0.0f, 1.0f);
        const float smoothT = fadeT * fadeT * (3.0f - 2.0f * fadeT);
        ImVec4 ringColor = BlendColor(centerColor, edgeColor, smoothT);
        ringColor.w = (ring == kRings - 1)
            ? 0.0f
            : centerColor.w * std::pow(std::max(0.0f, 1.0f - smoothT), edgePower);
        const float ringRadiusX = radiusX * ratio;
        const float ringRadiusY = radiusY * ratio;
        for (int segment = 0; segment < kSegments; ++segment) {
            const float angle = (static_cast<float>(segment) / static_cast<float>(kSegments)) * kTau;
            drawList->PrimWriteVtx(
                SuperellipsePoint(center, ringRadiusX, ringRadiusY, angle, exponent),
                uv,
                ColorToU32(ringColor));
        }
    }
}

void DrawSoftSpotlightHalo(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    ImU32 color,
    float feather,
    float thickness,
    float exponent) {
    constexpr int kSegments = 72;
    constexpr float kTau = 6.28318530718f;
    ImVec2 points[kSegments];
    const ImVec2 center((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
    const float radiusX = (max.x - min.x) * 0.5f + feather;
    const float radiusY = (max.y - min.y) * 0.5f + feather;
    for (int segment = 0; segment < kSegments; ++segment) {
        const float angle = (static_cast<float>(segment) / static_cast<float>(kSegments)) * kTau;
        points[segment] = SuperellipsePoint(center, radiusX, radiusY, angle, exponent);
    }
    drawList->AddPolyline(points, kSegments, color, thickness, ImDrawFlags_Closed);
}

void DrawGraphNodeSpotlightSurface(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    const ImVec4& fillColor,
    const ImVec4& borderColor,
    const ImVec4& accentColor,
    const GraphStyleTokens& tokens,
    bool selected,
    bool expanded,
    float uiScale,
    float rounding,
    float borderThickness) {
    const ImVec4 surfaceFill = selected
        ? BrightenedSelectionFill(fillColor, accentColor, tokens)
        : fillColor;
    if (!tokens.enabled) {
        drawList->AddRectFilled(min, max, ColorToU32(surfaceFill), rounding);
        drawList->AddRect(min, max, ColorToU32(borderColor), rounding, 0, borderThickness);
        return;
    }

    if (!tokens.spotlightSurface) {
        drawList->AddRectFilled(min, max, ColorToU32(surfaceFill), rounding);
        drawList->AddRect(min, max, ColorToU32(borderColor), rounding, 0, borderThickness);
        return;
    }

    const float width = std::max(1.0f, max.x - min.x);
    const float height = std::max(1.0f, max.y - min.y);
    const float feather = std::clamp(std::min(width, height) * 0.20f, 12.0f * uiScale, 32.0f * uiScale);
    const float coreRatioX = (width * 0.5f) / ((width * 0.5f) + feather);
    const float coreRatioY = (height * 0.5f) / ((height * 0.5f) + feather);
    const float contentCoreRatio = std::clamp(std::max(coreRatioX, coreRatioY) + 0.025f, 0.64f, 0.86f);
    const ImVec4 familySpot = selected
        ? BrightenedSelectionFill(WithAlpha(BlendColor(tokens.spotlightCenter, accentColor, tokens.light ? 0.035f : 0.055f), expanded ? tokens.spotlightCenter.w : tokens.nodeSurfaceCollapsed.w), accentColor, tokens)
        : WithAlpha(BlendColor(tokens.spotlightCenter, accentColor, tokens.light ? 0.035f : 0.055f), expanded ? tokens.spotlightCenter.w : tokens.nodeSurfaceCollapsed.w);
    const ImVec4 familyEdge = WithAlpha(BlendColor(tokens.spotlightEdge, accentColor, tokens.light ? 0.025f : 0.04f), tokens.spotlightEdge.w);

    DrawSoftSpotlightBlob(drawList, min, max, familySpot, familyEdge, feather, 3.2f, contentCoreRatio, 1.45f);

    if (tokens.haloOutlines) {
        DrawSoftSpotlightHalo(
            drawList,
            min,
            max,
            ColorWithAlpha(tokens.spotlightHalo, 0.28f),
            feather * 0.42f,
            std::max(0.65f, 0.85f * uiScale),
            3.2f);
    }
}

void DrawSocketPin(
    ImDrawList* drawList,
    const ImVec2& pin,
    float radius,
    ImU32 baseColor,
    const GraphStyleTokens& tokens,
    bool hovered,
    float interactionEmphasis) {
    if (!tokens.enabled) {
        drawList->AddCircleFilled(pin, radius, hovered ? ApplyStyleAlpha(IM_COL32(255, 255, 255, 255)) : baseColor);
        return;
    }

    const ImVec4 baseVec = ImGui::ColorConvertU32ToFloat4(baseColor);
    const float emphasis = std::clamp(interactionEmphasis, 0.0f, 1.0f);
    const float hoverBoost = hovered ? 1.0f : 0.0f;
    const float activePulse = (hovered || emphasis > 0.001f)
        ? (0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 8.0f))
        : 0.0f;
    const float outerRadiusScale = 1.55f + emphasis * 0.34f + hoverBoost * 0.27f + activePulse * (0.10f + emphasis * 0.12f);
    const float outerAlpha = 0.18f + emphasis * 0.18f + hoverBoost * 0.06f + activePulse * (0.02f + emphasis * 0.04f);
    const float coreRadiusScale = 0.78f + hoverBoost * 0.08f + emphasis * 0.10f + activePulse * (0.02f + emphasis * 0.03f);
    const float ringRadiusScale = 1.14f + emphasis * 0.06f + activePulse * (0.02f + emphasis * 0.02f);
    const ImVec4 highlightColor = BlendColor(baseVec, tokens.selected, 0.32f + emphasis * 0.30f);
    drawList->AddCircleFilled(
        pin,
        radius * outerRadiusScale,
        ColorWithAlpha(hovered || emphasis > 0.001f ? highlightColor : baseVec, outerAlpha));
    drawList->AddCircleFilled(pin, radius * 1.14f, ColorWithAlpha(tokens.canvas, 0.86f));
    drawList->AddCircleFilled(
        pin,
        radius * coreRadiusScale,
        ColorToU32(BlendColor(baseVec, tokens.text, hoverBoost * 0.18f + emphasis * 0.22f + activePulse * emphasis * 0.08f)));
    drawList->AddCircle(
        pin,
        radius * ringRadiusScale,
        ColorWithAlpha(hovered || emphasis > 0.001f ? tokens.selected : tokens.spotlightHalo, 0.52f + emphasis * 0.22f + hoverBoost * 0.20f + activePulse * emphasis * 0.16f),
        16,
        std::max(0.8f, radius * (0.18f + emphasis * 0.03f + hoverBoost * 0.02f)));
    if (emphasis > 0.01f) {
        drawList->AddCircle(
            pin,
            radius * (1.34f + emphasis * 0.16f + activePulse * 0.05f),
            ColorWithAlpha(tokens.selected, 0.12f + emphasis * 0.20f),
            18,
            std::max(0.75f, radius * 0.12f));
    }
}

void DrawPreviewFrame(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const GraphStyleTokens& tokens, float uiScale) {
    if (!tokens.enabled || max.x <= min.x || max.y <= min.y) {
        return;
    }
    const float rounding = std::max(2.0f, 4.0f * uiScale);
    drawList->AddRect(min, max, ColorWithAlpha(tokens.spotlightHalo, 0.075f), rounding, 0, std::max(0.45f, 0.55f * uiScale));
}

float ChannelLaneOffset(const std::string& channel, float zoom) {
    const float lane = std::max(0.05f, zoom) * 2.2f;
    if (channel == "r") return -lane * 1.5f;
    if (channel == "g") return -lane * 0.5f;
    if (channel == "b") return lane * 0.5f;
    if (channel == "a") return lane * 1.5f;
    return 0.0f;
}

float ExpandedContractHeight(const EditorNodeGraph::Node& node, const NodeLayoutMetrics& metrics, float measuredLayerHeight) {
    const float headerBlock = metrics.headerInsetY + metrics.kindLabelHeight + 2.0f + metrics.titleHeight;
    const float bottomPadding = metrics.bodyInsetBottom;
    const float row = metrics.rowHeight;
    const float sliderRow = std::max(metrics.sliderHeight, row);
    const float colorRow = std::max(metrics.colorRowHeight, row);
    const float checkboxRow = std::max(metrics.checkboxHeight, row);
    const float gap = metrics.itemGap;
    const float sectionGap = metrics.sectionGap;

    if (!node.expanded) {
        return metrics.collapsedHeight;
    }

    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
            return std::max(metrics.minExpandedHeight, measuredLayerHeight > 0.0f ? measuredLayerHeight : metrics.minExpandedHeight);
        case EditorNodeGraph::NodeKind::Image:
            return headerBlock + sectionGap + metrics.previewHeight + gap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::Output:
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::Composite:
            return headerBlock + sectionGap + row + gap + std::max(136.0f, metrics.previewHeight + 24.0f) + sectionGap + row + gap + row + gap + checkboxRow + gap + checkboxRow + gap + checkboxRow + gap + checkboxRow + gap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::Scope:
            return headerBlock + sectionGap + row + sectionGap + metrics.scopeHeight + bottomPadding;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            switch (node.maskKind) {
                case EditorNodeGraph::MaskGeneratorKind::Solid:
                    return headerBlock + sectionGap + row + gap + sliderRow + bottomPadding;
                case EditorNodeGraph::MaskGeneratorKind::LinearGradient:
                    return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
                case EditorNodeGraph::MaskGeneratorKind::RadialGradient:
                    return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
                case EditorNodeGraph::MaskGeneratorKind::Noise:
                    return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
            }
            break;
        case EditorNodeGraph::NodeKind::MaskUtility:
            switch (node.maskUtilityKind) {
                case EditorNodeGraph::MaskUtilityKind::Invert:
                    return headerBlock + sectionGap + row + bottomPadding;
                case EditorNodeGraph::MaskUtilityKind::Levels:
                    return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
                case EditorNodeGraph::MaskUtilityKind::Threshold:
                    return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
            }
            break;
        case EditorNodeGraph::NodeKind::CustomMask:
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::Lut:
            return headerBlock + sectionGap + row + gap + row + gap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::MaskCombine:
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::DataMath:
            if (node.dataMathMode == EditorNodeGraph::DataMathMode::Clamp) {
                return headerBlock + sectionGap + row + gap + row + gap + sliderRow + gap + row + gap + sliderRow + bottomPadding;
            }
            if (node.dataMathMode == EditorNodeGraph::DataMathMode::Remap) {
                return headerBlock + sectionGap + row + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + bottomPadding;
            }
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::ImageToMask:
            if (node.imageToMaskKind == EditorNodeGraph::ImageToMaskKind::SampledRange) {
                const int extraSamples = std::max(0, std::clamp(node.imageToMaskSettings.sampleCount, 1, 5) - 1);
                const float extraSampleRows = static_cast<float>(extraSamples + 3);
                return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap +
                    row + gap + sliderRow + gap + extraSampleRows * row + gap * extraSampleRows + checkboxRow + bottomPadding;
            }
            return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            return headerBlock + sectionGap + row + gap + metrics.previewHeight + sectionGap + metrics.minExpandedHeight + bottomPadding;
        case EditorNodeGraph::NodeKind::HdrMerge: {
            const float inputRows = 4.0f;
            return headerBlock + sectionGap + inputRows * row + gap * (inputRows - 1.0f) + bottomPadding;
        }
        case EditorNodeGraph::NodeKind::Mfsr: {
            const float inputRows = 5.0f;
            return headerBlock + sectionGap + inputRows * row + gap * (inputRows - 1.0f) + bottomPadding;
        }
        case EditorNodeGraph::NodeKind::ImageGenerator:
            if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::SolidColor ||
                node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Square ||
                node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Circle) {
                return headerBlock + sectionGap + row + gap + colorRow + bottomPadding;
            }
            if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Text) {
                const float textBlock = row * 4.2f;
                return headerBlock + sectionGap + row + gap + textBlock + gap + colorRow + gap + row + gap + sliderRow + bottomPadding;
            }
            return headerBlock + sectionGap + row + gap + colorRow + gap + colorRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + bottomPadding;
        case EditorNodeGraph::NodeKind::Mix:
            return headerBlock + sectionGap + row + gap + row + gap + sliderRow + bottomPadding;
        case EditorNodeGraph::NodeKind::Preview:
            return headerBlock + sectionGap + row + gap + metrics.previewHeight + bottomPadding;
    }
    return metrics.minExpandedHeight;
}

bool UsesMeasuredNodeHeight(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Composite:
            return true;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::Preview:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::MaskCombine:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::DataMath:
            return false;
    }
    return false;
}

bool ShouldShowKindLabel(const EditorNodeGraph::Node& node) {
    return node.expanded &&
        node.kind != EditorNodeGraph::NodeKind::Composite;
}

std::string EllipsizeLabel(const std::string& value, float maxWidth) {
    if (value.empty() || maxWidth <= 12.0f) {
        return value;
    }
    if (ImGui::CalcTextSize(value.c_str()).x <= maxWidth) {
        return value;
    }

    static constexpr const char* kEllipsis = "...";
    std::string trimmed = value;
    while (!trimmed.empty()) {
        trimmed.pop_back();
        const std::string candidate = trimmed + kEllipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return kEllipsis;
}

const char* NodeKindLabel(EditorNodeGraph::NodeKind kind) {
    switch (kind) {
        case EditorNodeGraph::NodeKind::Image: return "Image";
        case EditorNodeGraph::NodeKind::RawSource: return "RAW";
        case EditorNodeGraph::NodeKind::RawDevelopment: return "RAW Development";
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: return "RAW Denoise";
        case EditorNodeGraph::NodeKind::RawDecode: return "RAW Decode";
        case EditorNodeGraph::NodeKind::RawDevelop: return "Develop";
        case EditorNodeGraph::NodeKind::RawDetailAutoMask: return "RAW Detail Auto Mask";
        case EditorNodeGraph::NodeKind::RawDetailFusion: return "Pre-Local Exposure";
        case EditorNodeGraph::NodeKind::HdrMerge: return "HDR Merge";
        case EditorNodeGraph::NodeKind::Mfsr: return "MFSR";
        case EditorNodeGraph::NodeKind::Lut: return "LUT";
        case EditorNodeGraph::NodeKind::Layer: return "Layer";
        case EditorNodeGraph::NodeKind::Output: return "Output";
        case EditorNodeGraph::NodeKind::Composite: return "Composite";
        case EditorNodeGraph::NodeKind::Scope: return "Scope";
        case EditorNodeGraph::NodeKind::MaskGenerator: return "Mask";
        case EditorNodeGraph::NodeKind::CustomMask: return "Custom Mask";
        case EditorNodeGraph::NodeKind::MaskCombine: return "Mask Combine";
        case EditorNodeGraph::NodeKind::Mix: return "Image Blend";
        case EditorNodeGraph::NodeKind::Preview: return "Preview";
        case EditorNodeGraph::NodeKind::MaskUtility: return "Mask Utility";
        case EditorNodeGraph::NodeKind::ImageToMask: return "Image To Mask";
        case EditorNodeGraph::NodeKind::ImageGenerator: return "Generator";
        case EditorNodeGraph::NodeKind::DataMath: return "Math";
        case EditorNodeGraph::NodeKind::ChannelSplit: return "Channel Split";
        case EditorNodeGraph::NodeKind::ChannelCombine: return "Channel Combine";
    }
    return "Node";
}

const char* ExportAspectPresetLabel(EditorModule::CompositeExportAspectPreset preset) {
    switch (preset) {
        case EditorModule::CompositeExportAspectPreset::Ratio4x3: return "4:3";
        case EditorModule::CompositeExportAspectPreset::Ratio3x2: return "3:2";
        case EditorModule::CompositeExportAspectPreset::Ratio16x9: return "16:9";
        case EditorModule::CompositeExportAspectPreset::Ratio9x16: return "9:16";
        case EditorModule::CompositeExportAspectPreset::Ratio2x3: return "2:3";
        case EditorModule::CompositeExportAspectPreset::Ratio5x4: return "5:4";
        case EditorModule::CompositeExportAspectPreset::Ratio21x9: return "21:9";
        case EditorModule::CompositeExportAspectPreset::Custom: return "Custom";
        case EditorModule::CompositeExportAspectPreset::Ratio1x1:
        default:
            return "1:1";
    }
}

const char* CompositeSnapPresetLabel(EditorModule::CompositeSnapModePreset preset) {
    switch (preset) {
        case EditorModule::CompositeSnapModePreset::ObjectOnly: return "Object Only";
        case EditorModule::CompositeSnapModePreset::Full: return "Full";
        case EditorModule::CompositeSnapModePreset::Custom: return "Custom";
        case EditorModule::CompositeSnapModePreset::Off:
        default:
            return "Off";
    }
}

const char* ScopeLabel(EditorNodeGraph::ScopeKind kind) {
    switch (kind) {
        case EditorNodeGraph::ScopeKind::Histogram: return "Histogram";
        case EditorNodeGraph::ScopeKind::Vectorscope: return "Vectorscope";
        case EditorNodeGraph::ScopeKind::RGBParade: return "RGB Parade";
    }
    return "Scope";
}

const char* MaskLabel(EditorNodeGraph::MaskGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::Solid: return "Solid Mask";
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return "Linear Gradient Mask";
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return "Radial Gradient Mask";
        case EditorNodeGraph::MaskGeneratorKind::Noise: return "Noise Mask";
    }
    return "Mask";
}

const char* MaskUtilityLabel(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return "Invert Mask";
        case EditorNodeGraph::MaskUtilityKind::Levels: return "Remap Mask";
        case EditorNodeGraph::MaskUtilityKind::Threshold: return "Threshold Mask";
    }
    return "Mask Utility";
}

const char* ImageGeneratorLabel(EditorNodeGraph::ImageGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageGeneratorKind::SolidColor: return "Solid Color Image";
        case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return "Color Gradient Image";
        case EditorNodeGraph::ImageGeneratorKind::Square: return "Square";
        case EditorNodeGraph::ImageGeneratorKind::Circle: return "Circle";
        case EditorNodeGraph::ImageGeneratorKind::Text: return "Text";
    }
    return "Generated Image";
}

const char* MixBlendLabel(EditorNodeGraph::MixBlendMode mode) {
    switch (mode) {
        case EditorNodeGraph::MixBlendMode::Normal: return "Normal / Lerp";
        case EditorNodeGraph::MixBlendMode::Average: return "Average Images";
        case EditorNodeGraph::MixBlendMode::Add: return "Add";
        case EditorNodeGraph::MixBlendMode::Multiply: return "Multiply";
        case EditorNodeGraph::MixBlendMode::Screen: return "Screen";
        case EditorNodeGraph::MixBlendMode::AlphaOver: return "Alpha Over";
    }
    return "Normal / Lerp";
}

const char* DataMathLabel(EditorNodeGraph::DataMathMode mode) {
    switch (mode) {
        case EditorNodeGraph::DataMathMode::Clamp: return "Clamp";
        case EditorNodeGraph::DataMathMode::Add: return "Add";
        case EditorNodeGraph::DataMathMode::Subtract: return "Subtract";
        case EditorNodeGraph::DataMathMode::Multiply: return "Multiply";
        case EditorNodeGraph::DataMathMode::Divide: return "Divide";
        case EditorNodeGraph::DataMathMode::Average: return "Average";
        case EditorNodeGraph::DataMathMode::Min: return "Minimum";
        case EditorNodeGraph::DataMathMode::Max: return "Maximum";
        case EditorNodeGraph::DataMathMode::Difference: return "Difference";
        case EditorNodeGraph::DataMathMode::Remap: return "Remap";
        case EditorNodeGraph::DataMathMode::ImageAverage: return "Average Images";
    }
    return "Clamp";
}

} // namespace Stack::Editor::NodeGraphUIVisuals
