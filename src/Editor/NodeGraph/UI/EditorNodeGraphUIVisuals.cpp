#include "Editor/LayerRegistry.h"
#include "ContinuousLinkStroke.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/NodeGraph/EditorNodeGraphUIMetrics.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/UI/NodeNumericDefaults.h"

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
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
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
        case EditorNodeGraph::NodeKind::Reformat:
            return NodeFamily::Gray;
        case EditorNodeGraph::NodeKind::Layer:
            return NodeFamily::Layer;
        case EditorNodeGraph::NodeKind::Lut:
        case EditorNodeGraph::NodeKind::Compound:
            return NodeFamily::Layer;
        case EditorNodeGraph::NodeKind::Preview:
            return NodeFamily::Preview;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::MaskCombine:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::DataMath:
        case EditorNodeGraph::NodeKind::FrequencyMask:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            return NodeFamily::Mask;
        case EditorNodeGraph::NodeKind::Scope:
            return NodeFamily::Scope;
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::Value:
        case EditorNodeGraph::NodeKind::FieldMean:
        case EditorNodeGraph::NodeKind::ConstantChannel:
            return NodeFamily::Generator;
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::Mfsr:
        case EditorNodeGraph::NodeKind::ChannelSplit:
        case EditorNodeGraph::NodeKind::ChannelCombine:
        case EditorNodeGraph::NodeKind::FrequencyFilter:
        case EditorNodeGraph::NodeKind::FrequencyResponse:
        case EditorNodeGraph::NodeKind::FrequencyFft:
        case EditorNodeGraph::NodeKind::FrequencyIfft:
        case EditorNodeGraph::NodeKind::SpectrumView:
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
        case EditorNodeGraph::NodeKind::CombineSpectra:
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
        case EditorNodeGraph::NodeKind::SpectrumMath:
        case EditorNodeGraph::NodeKind::MagnitudePhase:
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
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
    const ImVec4 slate = tokens.mutedText;
    const ImVec4 green = tokens.socketImage;
    const ImVec4 violet = tokens.socketMask;
    const ImVec4 amber = tokens.socketAnalysis;
    const ImVec4 blue = tokens.socketRaw;
    const ImVec4 cyan = tokens.socketValue;

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
    GraphStyleTokens t{};
    const auto* appearance=editor ? editor->GetAppearance() : nullptr;
    static const auto fallback=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
    const auto& p=appearance ? appearance->GetResolvedCreamPalette() : fallback;
    t.nodeAppearance=p.nodeAppearance;
    t.enabled=true; t.light=ColorLuminance(p.workspace.background)>=0.52f;
    t.monochrome=p.colorPolicy==StackAppearance::PaletteColorPolicy::Monochrome;
    t.gridLineOpacity=appearance ? appearance->GetGraphLineOpacity() : 1;
    t.canvas=p.workspace.background; t.nodeSurface=t.nodeSurfaceCollapsed=p.nodeAppearance.surface;
    t.text=p.node.foreground; t.mutedText=p.node.mutedForeground;
    t.spotlightCenter=p.surface; t.spotlightEdge=WithAlpha(p.surface,0);
    t.spotlightHalo=p.border; t.selectionGlow=WithAlpha(p.focus,0); t.selected=p.nodeAppearance.selection;
    t.socketImage=p.imageSocket; t.socketMask=p.maskSocket; t.socketAnalysis=p.analysisSocket;
    t.socketValue=p.valueSocket; t.socketRaw=p.rawSocket;
    t.linkImage=p.imageSocket; t.linkMask=p.maskSocket; t.linkAnalysis=p.analysisSocket;
    t.linkUnderlay=WithAlpha(p.canvas,0.65f);
    t.groupFill=WithAlpha(p.surface,0.30f); t.groupHeader=p.surface; t.groupBorder=p.border;
    return t;
}

GraphZoomDialStyle BuildGraphZoomDialStyle(EditorModule* editor, const GraphStyleTokens& tokens) {
    GraphZoomDialStyle style {};
    const ImVec4 baseColor = tokens.text;
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

bool GraphStraightLinksEnabled(EditorModule* editor) {
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    return appearance ? appearance->GetGraphStraightLinks() : false;
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
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
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
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
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
                profile.showTitle = !SharesIdentityWithParameter(node);
            }
            break;
    }
    return profile;
}

NodeFamilyStyle StyleForFamily(NodeFamily family, const GraphStyleTokens& tokens) {
    const auto& n=tokens.nodeAppearance;
    return {n.surface,tokens.light ? tokens.groupBorder : ImVec4(0.32f,0.32f,0.32f,1.0f),n.number,n.text,n.mutedText};
}

const NodeFamilyStyle& StyleForFamily(NodeFamily family) {
    // Compatibility overload for drawing without an editor. The graph itself
    // supplies its cached palette through the overload above.
    static const auto palette=StackAppearance::ResolveCreamPalette(StackAppearance::CreamPalette{});
    static std::array<NodeFamilyStyle,7> styles;
    auto& style=styles[static_cast<size_t>(family)];
    style={palette.surface,palette.border,palette.focus,palette.text,palette.mutedText};
    if (ImGui::GetCurrentContext()) {
        style.fill=ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);
        style.text=ImGui::GetStyleColorVec4(ImGuiCol_Text);
        style.mutedText=ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        style.border=ImGui::GetStyleColorVec4(ImGuiCol_Border);
        style.accent=ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    }
    return style;
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
        case EditorNodeGraph::NodeKind::FrequencyFilter:
        case EditorNodeGraph::NodeKind::FrequencyResponse:
            metrics.width = 302.0f;
            metrics.contentLaneWidth = 228.0f;
            metrics.previewWidth = 180.0f;
            metrics.previewHeight = 180.0f;
            metrics.minExpandedHeight = 360.0f;
            metrics.collapsedHeight = 48.0f;
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

Stack::Editor::NodeGraphUILayout::NodeWidthClass ResolveNodeWidthClass(
    const EditorNodeGraphUI* ui,
    const EditorModule* editor,
    const EditorNodeGraph::Node& node) {
    const GraphStyleTokens geometryOnlyTokens {};
    const NodePresentationProfile presentation =
        BuildNodePresentationProfile(ui, editor, node, geometryOnlyTokens);
    const bool richExpandedSurface =
        node.kind == EditorNodeGraph::NodeKind::Layer &&
        ui &&
        ui->ResolveLayerUsesRichNodeSurface(editor, node.layerIndex);
    return Stack::Editor::NodeGraphUILayout::ResolveWidthClass(
        node.kind,
        presentation.kind == NodePresentationKind::SummaryOnly,
        presentation.kind == NodePresentationKind::RouteSquare,
        richExpandedSurface);
}

void ApplyCanonicalNodeMetrics(
    const EditorNodeGraphUI* ui,
    const EditorModule* editor,
    const EditorNodeGraph::Node& node,
    NodeLayoutMetrics& metrics) {
    using namespace Stack::Editor::NodeGraphUILayout;
    const NodeWidthClass widthClass =
        ResolveNodeWidthClass(ui, editor, node);
    metrics.width = WidthForClass(widthClass);
    if (widthClass == NodeWidthClass::Compact || widthClass == NodeWidthClass::Standard)
        metrics.width = CompactControlNodeWidth(node).value_or(metrics.width);
    if (node.kind == EditorNodeGraph::NodeKind::MaskUtility &&
        node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert) metrics.minExpandedHeight = 72.0f;
    metrics.collapsedHeight = kCollapsedNodeHeight;
    if (SharesIdentityWithParameter(node)) metrics.minExpandedHeight = kCollapsedNodeHeight;
    metrics.contentLaneWidth = std::max(48.0f, metrics.width - 24.0f);
    metrics.previewWidth = std::min(
        std::max(1.0f, metrics.previewWidth),
        std::max(1.0f, metrics.width - 24.0f));

    switch (widthClass) {
        case NodeWidthClass::Tile:
            metrics.collapsedHeight = kTileNodeSize;
            metrics.minExpandedHeight = kTileNodeSize;
            break;
        case NodeWidthClass::RawSource:
            metrics.collapsedHeight = kRawSourceHeight;
            metrics.minExpandedHeight = kRawSourceHeight;
            break;
        case NodeWidthClass::Media:
            metrics.collapsedHeight = kMediaNodeHeight;
            metrics.minExpandedHeight = kMediaNodeHeight;
            metrics.previewWidth = kMediaNodeWidth - 4.0f;
            metrics.previewHeight = kMediaNodeHeight - 4.0f;
            break;
        case NodeWidthClass::Compact:
        case NodeWidthClass::Standard:
        case NodeWidthClass::Wide:
        case NodeWidthClass::Complex:
            break;
    }
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
    return logicalWidth * uiScale;
}

ImVec2 NodePreviewSizeForScale(const NodeLayoutMetrics& metrics, float uiScale) {
    return ImVec2(
        metrics.previewWidth * uiScale,
        metrics.previewHeight * uiScale);
}

ImU32 TypedSocketColor(EditorNodeGraph::SocketType type, const NodeFamilyStyle& familyStyle) {
    const ImVec4 imageBase(0.60f, 0.69f, 0.79f, 1.0f);
    const ImVec4 maskBase(0.71f, 0.64f, 0.82f, 1.0f);
    const ImVec4 analysisBase(0.76f, 0.68f, 0.57f, 1.0f);
    const ImVec4 valueBase(0.66f, 0.73f, 0.73f, 1.0f);
    const ImVec4 rawBase(0.62f, 0.77f, 0.66f, 1.0f);
    const ImVec4 channelBase(0.40f, 0.82f, 0.76f, 1.0f);
    const ImVec4 spectrumBase(0.76f, 0.48f, 0.96f, 1.0f);
    const ImVec4 responseBase(0.96f, 0.68f, 0.28f, 1.0f);
    ImVec4 base = imageBase;
    switch (type) {
        case EditorNodeGraph::SocketType::Image: base = imageBase; break;
        case EditorNodeGraph::SocketType::ImageOrChannel:
            base = BlendColor(imageBase, channelBase, 0.5f);
            break;
        case EditorNodeGraph::SocketType::Channel: base = channelBase; break;
        case EditorNodeGraph::SocketType::Spectrum:
        case EditorNodeGraph::SocketType::SpectrumMagnitude:
        case EditorNodeGraph::SocketType::SpectrumPhase: base = spectrumBase; break;
        case EditorNodeGraph::SocketType::FrequencyResponse: base = responseBase; break;
        case EditorNodeGraph::SocketType::Mask:
        case EditorNodeGraph::SocketType::ScalarField: base = maskBase; break;
        case EditorNodeGraph::SocketType::Analysis: base = analysisBase; break;
        case EditorNodeGraph::SocketType::Value:
        case EditorNodeGraph::SocketType::Boolean:
        case EditorNodeGraph::SocketType::Integer:
        case EditorNodeGraph::SocketType::Scalar:
        case EditorNodeGraph::SocketType::Vector2:
        case EditorNodeGraph::SocketType::Vector3:
        case EditorNodeGraph::SocketType::Vector4:
        case EditorNodeGraph::SocketType::Matrix3:
        case EditorNodeGraph::SocketType::Matrix4:
        case EditorNodeGraph::SocketType::Curve:
        case EditorNodeGraph::SocketType::Coordinate:
        case EditorNodeGraph::SocketType::Histogram:
        case EditorNodeGraph::SocketType::Statistics:
        case EditorNodeGraph::SocketType::Metadata:
        case EditorNodeGraph::SocketType::Handle: base = valueBase; break;
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
    const std::string& toSocketId,
    EditorModule* editor) {
    LinkVisualStyle style;
    EditorNodeGraph::GraphOutputDescription local;
    const auto* output = editor ? editor->GetGraphOutputDescription(fromNodeId, fromSocketId) : nullptr;
    if (!output) {
        local = EditorNodeGraph::DescribeGraphOutput(graph, fromNodeId, fromSocketId);
        output = &local;
    }
    style.channel = EditorNodeGraph::OutputChannelColor(*output);
    style.scalarStream = EditorNodeGraph::IsSingleChannelValue(output->descriptor.logicalType);
    style.dotted = style.scalarStream;
    if (output->descriptor.logicalType == Stack::NodeMath::LogicalValueType::Raw) style.kind = LinkVisualKind::Raw;
    else if (output->descriptor.logicalType == Stack::NodeMath::LogicalValueType::Analysis) style.kind = LinkVisualKind::Analysis;
    return style;
}

LinkVisualStyle ResolveLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Link& link,
    EditorModule* editor) {
    return ResolveLinkVisualStyle(graph, link.fromNodeId, link.fromSocketId, link.toNodeId, link.toSocketId, editor);
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
    if (direction == EditorNodeGraph::SocketDirection::Output) {
        return ResolveLinkVisualStyle(graph, nodeId, socketId, 0, {}, nullptr);
    }
    return {};
}

EditorNodeGraph::SocketDefinition ResolveSocketDisplayDefinition(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::SocketDefinition& socket,
    EditorModule* editor) {
    using Type = EditorNodeGraph::SocketType;
    using Direction = EditorNodeGraph::SocketDirection;
    using LogicalType = Stack::NodeMath::LogicalValueType;

    const bool isMainImageInput =
        socket.direction == Direction::Input &&
        socket.id != EditorNodeGraph::kMaskInputSocketId &&
        (socket.type == Type::Image || socket.type == Type::ImageOrChannel);
    if (!isMainImageInput) {
        return socket;
    }

    const EditorNodeGraph::Link* link = graph.FindInputLink(socket.nodeId, socket.id);
    if (!link) {
        return socket;
    }

    EditorNodeGraph::GraphOutputDescription localOutput;
    const EditorNodeGraph::GraphOutputDescription* output =
        editor && &graph == &editor->GetNodeGraph()
            ? editor->GetGraphOutputDescription(link->fromNodeId, link->fromSocketId)
            : nullptr;
    if (!output) {
        localOutput = EditorNodeGraph::DescribeGraphOutput(
            graph, link->fromNodeId, link->fromSocketId);
        output = &localOutput;
    }
    if (output->descriptor.logicalType != LogicalType::Mask) {
        return socket;
    }

    EditorNodeGraph::SocketDefinition displaySocket = socket;
    displaySocket.type = Type::Mask;
    return displaySocket;
}

ImVec4 ChannelColorVec(const std::string& channel, const GraphStyleTokens& tokens) {
    const bool light = tokens.enabled && tokens.light;
    if (tokens.monochrome) {
        if (channel == "r") return ImVec4(0.90f,0.90f,0.90f,1.0f);
        if (channel == "g") return ImVec4(0.75f,0.75f,0.75f,1.0f);
        if (channel == "b") return ImVec4(0.60f,0.60f,0.60f,1.0f);
        if (channel == "a") return ImVec4(0.48f,0.48f,0.48f,1.0f);
    }
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
        case EditorNodeGraph::SocketType::Image: return tokens.socketImage;
        case EditorNodeGraph::SocketType::ImageOrChannel:
            return tokens.socketImage;
        case EditorNodeGraph::SocketType::Channel: return tokens.socketImage;
        case EditorNodeGraph::SocketType::Spectrum:
        case EditorNodeGraph::SocketType::SpectrumMagnitude:
        case EditorNodeGraph::SocketType::SpectrumPhase: return tokens.socketAnalysis;
        case EditorNodeGraph::SocketType::FrequencyResponse: return tokens.socketValue;
        case EditorNodeGraph::SocketType::Mask:
        case EditorNodeGraph::SocketType::ScalarField: return tokens.socketMask;
        case EditorNodeGraph::SocketType::Analysis: return tokens.socketAnalysis;
        case EditorNodeGraph::SocketType::Value:
        case EditorNodeGraph::SocketType::Boolean:
        case EditorNodeGraph::SocketType::Integer:
        case EditorNodeGraph::SocketType::Scalar:
        case EditorNodeGraph::SocketType::Vector2:
        case EditorNodeGraph::SocketType::Vector3:
        case EditorNodeGraph::SocketType::Vector4:
        case EditorNodeGraph::SocketType::Matrix3:
        case EditorNodeGraph::SocketType::Matrix4:
        case EditorNodeGraph::SocketType::Curve:
        case EditorNodeGraph::SocketType::Coordinate:
        case EditorNodeGraph::SocketType::Histogram:
        case EditorNodeGraph::SocketType::Statistics:
        case EditorNodeGraph::SocketType::Metadata:
        case EditorNodeGraph::SocketType::Handle: return tokens.socketValue;
        case EditorNodeGraph::SocketType::Raw: return tokens.socketRaw;
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
    if (!style.channel.empty()) return WithAlpha(ChannelColorVec(style.channel, tokens), 0.95f);
    if (style.scalarStream) return tokens.mutedText;
    return tokens.enabled ? WithAlpha(tokens.text, 0.9f) : ImVec4(0.94f, 0.94f, 0.94f, 0.9f);
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

    return style.scalarStream ? ApplyStyleAlpha(IM_COL32(166, 166, 166, 230))
        : ApplyStyleAlpha(IM_COL32(240, 240, 240, 230));
}

void DrawDottedBezierStroke(
    ImDrawList* drawList,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    ImU32 color,
    float thickness) {
    const float radius = std::max(0.25f, thickness * 0.5f);
    const float spacing = std::max(1.4f, radius * 2.6f);
    const auto samples=SampleLinkCurve(p0,p1,p2,p3);
    ImVec2 previous = p0;
    float distanceToNextDot = 0.0f;
    for (size_t index = 1; index < samples.size(); ++index) {
        const ImVec2 current = samples[index].point;
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
    DrawContinuousLinkStroke(drawList,p0,p1,p2,p3,color,thickness,false,{}, {},0);
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

void DrawPreviewFrame(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const GraphStyleTokens& tokens, float uiScale) {
    if (!tokens.enabled || max.x <= min.x || max.y <= min.y) {
        return;
    }
    const float rounding = 4.0f * uiScale;
    drawList->AddRect(
        min,
        max,
        ColorWithAlpha(tokens.spotlightHalo, 0.075f),
        rounding,
        0,
        0.55f * uiScale);
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
                case EditorNodeGraph::MaskGeneratorKind::Square:
                case EditorNodeGraph::MaskGeneratorKind::RadialGradient:
                    return headerBlock + sectionGap + 6 * (row + gap + sliderRow + gap) + checkboxRow + bottomPadding;
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
        case EditorNodeGraph::NodeKind::ConstantChannel:
            return headerBlock + sectionGap +
                row * 3.0f + gap * 2.0f + bottomPadding;
        case EditorNodeGraph::NodeKind::DataMath:
            if (node.dataMathMode == EditorNodeGraph::DataMathMode::Clamp) {
                return headerBlock + sectionGap + row + gap + row + gap + sliderRow + gap + row + gap + sliderRow + bottomPadding;
            }
            if (node.dataMathMode == EditorNodeGraph::DataMathMode::Remap) {
                return headerBlock + sectionGap + row + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + bottomPadding;
            }
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::Value:
            return headerBlock + sectionGap + row * 6.0f + gap * 5.0f + bottomPadding;
        case EditorNodeGraph::NodeKind::FieldMean:
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::Reformat:
            return headerBlock + sectionGap + row + gap + row + gap + row + gap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::FrequencyFft:
        case EditorNodeGraph::NodeKind::FrequencyIfft:
            return headerBlock + sectionGap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::FrequencyFilter: {
            const float notchRows =
                node.frequencyFilterSettings.localResponse.mode ==
                        EditorNodeGraph::FrequencyFilterMode::NotchReject
                    ? static_cast<float>(
                        std::max<std::size_t>(
                            1, node.frequencyFilterSettings.localResponse.notches.size())) * 3.0f
                    : 0.0f;
            return headerBlock + sectionGap + row + gap + 180.0f +
                sectionGap + (6.0f + notchRows) * row +
                (5.0f + notchRows) * gap + bottomPadding;
        }
        case EditorNodeGraph::NodeKind::FrequencyResponse: {
            const float notchRows =
                node.frequencyResponseSettings.mode ==
                        EditorNodeGraph::FrequencyFilterMode::NotchReject
                    ? static_cast<float>(
                        node.frequencyResponseSettings.notches.size()) * 3.0f + 1.0f
                    : 0.0f;
            return headerBlock + sectionGap + row + gap + row + gap + 180.0f +
                sectionGap + (5.0f + notchRows) * row +
                (5.0f + notchRows) * gap + bottomPadding;
        }
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
        case EditorNodeGraph::NodeKind::CombineSpectra:
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
            return headerBlock + sectionGap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            return headerBlock + sectionGap + row + gap + row + gap +
                checkboxRow + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::SpectrumView:
            return headerBlock + sectionGap + row + gap + row + gap +
                sliderRow + gap + row + gap + sliderRow + gap + row + bottomPadding;
        case EditorNodeGraph::NodeKind::FrequencyMask:
            return headerBlock + sectionGap + row + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
        case EditorNodeGraph::NodeKind::SpectrumMath:
            return headerBlock + sectionGap + row + gap + row + gap + sliderRow + bottomPadding;
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            return headerBlock + sectionGap + row + gap + row + gap + sliderRow + gap + row + gap + sliderRow + bottomPadding;
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
        case EditorNodeGraph::NodeKind::RawProjectFrame:
            return headerBlock + sectionGap + row * 3.0f + gap * 2.0f + bottomPadding;
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
            return headerBlock + sectionGap + row * 3.0f + gap * 2.0f + bottomPadding;
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            return headerBlock + sectionGap + row + gap + row + bottomPadding;
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
        case EditorNodeGraph::NodeKind::TechnicalImage:
            return headerBlock + sectionGap +
                row * 3.0f + gap * 2.0f + bottomPadding;
        case EditorNodeGraph::NodeKind::Compound:
            // Compound definitions can promote a variable number of controls.
            // Reserve a conservative first-layout surface; the canonical
            // logical measurement replaces it on the following frame.
            return std::max(
                metrics.minExpandedHeight,
                headerBlock + sectionGap +
                    row * 8.0f + gap * 7.0f + bottomPadding);
    }
    return metrics.minExpandedHeight;
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
        std::size_t codePointStart = trimmed.size() - 1;
        while (codePointStart > 0 &&
               (static_cast<unsigned char>(
                    trimmed[codePointStart]) &
                0xC0u) == 0x80u) {
            --codePointStart;
        }
        trimmed.erase(codePointStart);
        const std::string candidate = trimmed + kEllipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            return candidate;
        }
    }
    return kEllipsis;
}

const char* NodeKindLabel(EditorNodeGraph::NodeKind kind) {
    switch (kind) {
        case EditorNodeGraph::NodeKind::Image: return "Slice";
        case EditorNodeGraph::NodeKind::RawSource: return "RAW";
        case EditorNodeGraph::NodeKind::RawDevelopment: return "RAW Development";
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: return "RAW Denoise";
        case EditorNodeGraph::NodeKind::RawDecode: return "RAW Decode";
        case EditorNodeGraph::NodeKind::RawDevelop: return "Develop";
        case EditorNodeGraph::NodeKind::RawDetailAutoMask: return "RAW Detail Auto Mask";
        case EditorNodeGraph::NodeKind::RawDetailFusion: return "Pre-Local Exposure";
        case EditorNodeGraph::NodeKind::HdrMerge: return "HDR Merge";
        case EditorNodeGraph::NodeKind::Mfsr: return "MFSR";
        case EditorNodeGraph::NodeKind::RawProjectFrame: return "RAW Frame";
        case EditorNodeGraph::NodeKind::MultiFrameDenoise: return "MFD";
        case EditorNodeGraph::NodeKind::RawProjectSourceSet: return "RAW Project Source Set";
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
        case EditorNodeGraph::NodeKind::Value: return "Value";
        case EditorNodeGraph::NodeKind::FieldMean: return "Reduction";
        case EditorNodeGraph::NodeKind::Reformat: return "Geometry";
        case EditorNodeGraph::NodeKind::TechnicalImage: return "Technical Image";
        case EditorNodeGraph::NodeKind::Compound: return "Compound";
        case EditorNodeGraph::NodeKind::FrequencyFilter: return "Frequency Filter";
        case EditorNodeGraph::NodeKind::FrequencyResponse: return "Frequency Response";
        case EditorNodeGraph::NodeKind::FrequencyFft: return "Fourier Transform";
        case EditorNodeGraph::NodeKind::FrequencyIfft: return "Inverse Fourier Transform";
        case EditorNodeGraph::NodeKind::SpectrumView: return "Spectrum View";
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse: return "Apply Response";
        case EditorNodeGraph::NodeKind::CombineSpectra: return "Combine Spectra";
        case EditorNodeGraph::NodeKind::SpectrumSeparate: return "Separate Spectrum";
        case EditorNodeGraph::NodeKind::SpectrumRecombine: return "Recombine Spectrum";
        case EditorNodeGraph::NodeKind::FrequencyMask: return "Frequency Mask";
        case EditorNodeGraph::NodeKind::SpectrumMath: return "Spectrum Math";
        case EditorNodeGraph::NodeKind::MagnitudePhase: return "Magnitude / Phase";
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer: return "Spectrum Analyzer";
        case EditorNodeGraph::NodeKind::ChannelSplit: return "Channel Split";
        case EditorNodeGraph::NodeKind::ChannelCombine: return "Image Combine";
        case EditorNodeGraph::NodeKind::ConstantChannel: return "Constant Channel";
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
        case EditorNodeGraph::MaskGeneratorKind::Square: return "Square Mask";
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
        case EditorNodeGraph::MixBlendMode::StraightSourceOver: return "Source Over (Straight)";
        case EditorNodeGraph::MixBlendMode::PremultipliedSourceOver: return "Source Over (Premultiplied)";
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

std::string PrimaryNodeTitle(const EditorNodeGraph::Node& node) {
    if (!node.title.empty() && node.kind != EditorNodeGraph::NodeKind::Layer)
        return node.title;
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
            return node.title.empty() ? "Layer" : node.title;
        case EditorNodeGraph::NodeKind::MaskGenerator:
            return MaskLabel(node.maskKind);
        case EditorNodeGraph::NodeKind::CustomMask:
            return node.title.empty() ? "Custom Mask" : node.title;
        case EditorNodeGraph::NodeKind::MaskCombine:
            return node.title.empty() ? "Intersect Mask" : node.title;
        case EditorNodeGraph::NodeKind::MaskUtility:
            return MaskUtilityLabel(node.maskUtilityKind);
        case EditorNodeGraph::NodeKind::ImageToMask:
            return node.imageToMaskKind ==
                    EditorNodeGraph::ImageToMaskKind::SampledRange
                ? "Sampled Range Mask"
                : "Luminance Mask";
        case EditorNodeGraph::NodeKind::ImageGenerator:
            return ImageGeneratorLabel(node.imageGeneratorKind);
        case EditorNodeGraph::NodeKind::Scope:
            return ScopeLabel(node.scopeKind);
        case EditorNodeGraph::NodeKind::Mix:
            return node.title.empty() ? "Blend Images" : node.title;
        case EditorNodeGraph::NodeKind::DataMath:
            return node.title.empty() ? DataMathLabel(node.dataMathMode)
                                      : node.title;
        case EditorNodeGraph::NodeKind::Compound:
            return node.title.empty() ? "Compound" : node.title;
        case EditorNodeGraph::NodeKind::Preview:
            return node.title.empty() ? "Preview" : node.title;
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            return node.title.empty() ? "RAW Detail Auto Mask" : node.title;
        default:
            return node.title.empty() ? NodeKindLabel(node.kind) : node.title;
    }
}

std::string CompactNodeTitle(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Layer:
            return CompactAdvancedLayerLabel(node);
        case EditorNodeGraph::NodeKind::RawSource:
            return "RAW";
        case EditorNodeGraph::NodeKind::RawDevelopment:
            return "RAW Dev";
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            return "RAW Denoise";
        case EditorNodeGraph::NodeKind::RawDecode:
            return "RAW Decode";
        case EditorNodeGraph::NodeKind::RawDevelop:
            return "Develop";
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
            return "Auto Mask";
        case EditorNodeGraph::NodeKind::RawDetailFusion:
            return "Pre-Local";
        case EditorNodeGraph::NodeKind::HdrMerge:
            return "HDR Merge";
        case EditorNodeGraph::NodeKind::Mfsr:
            return "MFSR";
        case EditorNodeGraph::NodeKind::RawProjectFrame:
            return "RAW Frame";
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
            return "MFD";
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
            return "Source Set";
        case EditorNodeGraph::NodeKind::Lut:
            return "LUT";
        case EditorNodeGraph::NodeKind::CustomMask:
            return "Mask Edit";
        case EditorNodeGraph::NodeKind::Output:
            return node.outputEnabled ? "Output" : "Deactivated";
        case EditorNodeGraph::NodeKind::ChannelSplit:
            return "Split";
        case EditorNodeGraph::NodeKind::ChannelCombine:
            return "Combine";
        default:
            return PrimaryNodeTitle(node);
    }
}

std::vector<std::string> WrapNodeTitle(
    const std::string& title,
    float maxLogicalWidth) {
    if (title.empty()) {
        return {};
    }
    if (maxLogicalWidth <= 1.0f ||
        ImGui::CalcTextSize(title.c_str()).x <= maxLogicalWidth) {
        return { title };
    }

    std::string firstLine;
    std::string secondLine;
    std::size_t cursor = 0;
    while (cursor < title.size()) {
        while (cursor < title.size() && title[cursor] == ' ') {
            ++cursor;
        }
        if (cursor >= title.size()) {
            break;
        }
        const std::size_t wordEnd = title.find(' ', cursor);
        const std::string word = title.substr(
            cursor,
            wordEnd == std::string::npos
                ? std::string::npos
                : wordEnd - cursor);
        const std::string candidate =
            firstLine.empty() ? word : firstLine + " " + word;
        if (firstLine.empty() ||
            ImGui::CalcTextSize(candidate.c_str()).x <= maxLogicalWidth) {
            firstLine = candidate;
        } else {
            secondLine = title.substr(cursor);
            break;
        }
        cursor = wordEnd == std::string::npos
            ? title.size()
            : wordEnd + 1;
    }

    if (firstLine.empty()) {
        return { EllipsizeLabel(title, maxLogicalWidth) };
    }
    if (secondLine.empty()) {
        return { EllipsizeLabel(firstLine, maxLogicalWidth) };
    }
    return {
        firstLine,
        EllipsizeLabel(secondLine, maxLogicalWidth)
    };
}

} // namespace Stack::Editor::NodeGraphUIVisuals
