#pragma once

#include "App/settings/AppearanceTheme.h"
#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>

class EditorNodeGraphUI;

namespace Stack::Editor::NodeGraphUIVisuals {

enum class NodeFamily {
    Gray,
    Layer,
    Preview,
    Mask,
    Scope,
    Generator,
    Merge
};

struct NodeFamilyStyle {
    ImVec4 fill;
    ImVec4 border;
    ImVec4 accent;
    ImVec4 text;
    ImVec4 mutedText;
};

struct GraphStyleTokens {
    StackAppearance::GraphVisualMode mode = StackAppearance::GraphVisualMode::Classic;
    bool enabled = false;
    bool light = false;
    bool haloOutlines = false;
    bool spotlightSurface = false;
    float gridLineOpacity = 1.0f;
    ImVec4 canvas;
    ImVec4 nodeSurface;
    ImVec4 nodeSurfaceCollapsed;
    ImVec4 spotlightCenter;
    ImVec4 spotlightEdge;
    ImVec4 spotlightHalo;
    ImVec4 selectionGlow;
    ImVec4 text;
    ImVec4 mutedText;
    ImVec4 selected;
    ImVec4 linkImage;
    ImVec4 linkMask;
    ImVec4 linkAnalysis;
    ImVec4 linkUnderlay;
    ImVec4 socketImage;
    ImVec4 socketMask;
    ImVec4 socketAnalysis;
    ImVec4 socketValue;
    ImVec4 socketRaw;
    ImVec4 groupFill;
    ImVec4 groupHeader;
    ImVec4 groupBorder;
};

struct GraphZoomDialStyle {
    ImVec4 tick;
    ImVec4 glow;
};

struct NodeLayoutMetrics {
    float width = 260.0f;
    float collapsedHeight = 52.0f;
    float contentLaneWidth = 188.0f;
    float headerInsetX = 16.0f;
    float headerInsetY = 14.0f;
    float bodyInsetBottom = 14.0f;
    float titleHeight = 18.0f;
    float kindLabelHeight = 14.0f;
    float rowHeight = 24.0f;
    float sliderHeight = 20.0f;
    float colorRowHeight = 30.0f;
    float checkboxHeight = 20.0f;
    float sectionGap = 10.0f;
    float itemGap = 8.0f;
    float previewWidth = 190.0f;
    float previewHeight = 96.0f;
    float scopeHeight = 176.0f;
    float minExpandedHeight = 96.0f;
};

enum class NodePresentationKind {
    Standard,
    FramelessMedia,
    CompactControls,
    SummaryOnly,
    RouteSquare,
    PreviewPanel,
    ScopePanel
};

struct NodePresentationProfile {
    NodePresentationKind kind = NodePresentationKind::Standard;
    bool showFrame = true;
    bool showTitle = true;
    bool showKindLabel = true;
    bool inlineControls = true;
    bool hoverDetails = false;
};

enum class LinkVisualKind {
    Image,
    MaskEndpoint,
    Analysis,
    Raw
};

struct LinkVisualStyle {
    LinkVisualKind kind = LinkVisualKind::Image;
    std::string channel;
    bool dotted = false;
    bool scalarStream = false;
};

template <typename TKey>
float UpdateAnimatedState(
    std::unordered_map<TKey, float>& animationMap,
    const TKey& key,
    float target,
    float deltaTime,
    float onSpeed = 18.0f,
    float offSpeed = 12.0f) {
    float& value = animationMap[key];
    const float speed = target > value ? onSpeed : offSpeed;
    value = ImGuiExtras::AnimateTowards(value, target, deltaTime, speed);
    if (target <= 0.0f && value <= 0.001f) {
        value = 0.0f;
    }
    return value;
}

template <typename TKey>
void PruneAnimatedState(
    std::unordered_map<TKey, float>& animationMap,
    const std::unordered_set<TKey>& activeKeys) {
    for (auto it = animationMap.begin(); it != animationMap.end();) {
        if (activeKeys.count(it->first) == 0) {
            it = animationMap.erase(it);
        } else {
            ++it;
        }
    }
}

ImGuiExtras::GraphSliderRangePolicy GraphSliderRangePolicyForNodeKind(EditorNodeGraph::NodeKind kind);
NodeFamily FamilyForNode(const EditorNodeGraph::Node& node);
ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t);
float ColorLuminance(const ImVec4& color);
ImVec4 WithAlpha(ImVec4 color, float alpha);
ImVec4 WithScaledAlpha(ImVec4 color, float scale);
ImU32 ApplyStyleAlpha(ImU32 color);
ImU32 ColorToU32(const ImVec4& color);
std::string LinkAnimationKey(const EditorNodeGraph::Link& link);
ImVec4 BrightenedSelectionFill(ImVec4 fill, const ImVec4& accent, const GraphStyleTokens& tokens);
ImVec4 FamilyAccent(NodeFamily family, const GraphStyleTokens& tokens);
GraphStyleTokens BuildGraphStyleTokens(EditorModule* editor);
GraphZoomDialStyle BuildGraphZoomDialStyle(EditorModule* editor, const GraphStyleTokens& tokens);
bool GraphDottedMaskLinksEnabled(EditorModule* editor);
bool IsSummaryOnlyNode(const EditorNodeGraphUI* ui, const EditorModule* editor, const EditorNodeGraph::Node& node);
NodePresentationProfile BuildNodePresentationProfile(
    const EditorNodeGraphUI* ui,
    const EditorModule* editor,
    const EditorNodeGraph::Node& node,
    const GraphStyleTokens& tokens);
NodeFamilyStyle StyleForFamily(NodeFamily family, const GraphStyleTokens& tokens);
const NodeFamilyStyle& StyleForFamily(NodeFamily family);
NodeLayoutMetrics MetricsForNode(const EditorNodeGraph::Node& node);
void ApplyModernCompactMetrics(const EditorNodeGraph::Node& node, NodeLayoutMetrics& metrics);
void ApplyLayerSurfaceMetrics(const EditorNodeGraphUI* ui, const EditorModule* editor, const EditorNodeGraph::Node& node, NodeLayoutMetrics& metrics);
ImU32 ColorWithAlpha(const ImVec4& color, float alpha);
bool HasDedicatedComplexEditor(const EditorNodeGraphUI* ui, const EditorModule* editor, const EditorNodeGraph::Node& node);
std::string CompactAdvancedLayerLabel(const EditorNodeGraph::Node& node);
const char* ChannelDisplayName(const std::string& channel);
ImVec4 ChannelPolicyColor(LayerChannelPolicy policy);
void RenderLayerMetadataNotes(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node,
    float controlWidth);
float NodeControlWidthForScale(float logicalWidth, float uiScale);
ImVec2 NodePreviewSizeForScale(const NodeLayoutMetrics& metrics, float uiScale);
ImU32 TypedSocketColor(EditorNodeGraph::SocketType type, const NodeFamilyStyle& familyStyle);
bool IsChannelSocketId(const std::string& id);
LinkVisualStyle ResolveLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId);
LinkVisualStyle ResolveLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Link& link);
LinkVisualStyle ResolvePendingLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    int outputNodeId,
    const std::string& outputSocketId,
    int inputNodeId,
    const std::string& inputSocketId);
LinkVisualStyle ResolvePendingLinkVisualStyle(
    const EditorNodeGraph::Graph& graph,
    int nodeId,
    const std::string& socketId,
    EditorNodeGraph::SocketDirection direction);
ImVec4 ChannelColorVec(const std::string& channel, const GraphStyleTokens& tokens);
ImVec4 SocketColorVec(
    const EditorNodeGraph::SocketDefinition& socket,
    const NodeFamilyStyle& familyStyle,
    const GraphStyleTokens& tokens);
ImU32 SocketColor(
    const EditorNodeGraph::SocketDefinition& socket,
    const NodeFamilyStyle& familyStyle,
    const GraphStyleTokens& tokens);
ImVec4 LinkColorVec(const LinkVisualStyle& style, const GraphStyleTokens& tokens);
ImU32 LinkColorClassic(const LinkVisualStyle& style, bool selected);
void DrawBezierLinkStroke(
    ImDrawList* drawList,
    const ImVec2& p0,
    const ImVec2& p1,
    const ImVec2& p2,
    const ImVec2& p3,
    ImU32 color,
    float thickness,
    bool dotted);
ImVec2 SuperellipsePoint(const ImVec2& center, float radiusX, float radiusY, float angle, float exponent);
void DrawSoftSpotlightBlob(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    ImVec4 centerColor,
    ImVec4 edgeColor,
    float feather,
    float exponent = 3.2f,
    float coreRatio = 0.72f,
    float edgePower = 1.35f);
void DrawSoftSpotlightHalo(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    ImU32 color,
    float feather,
    float thickness,
    float exponent = 3.2f);
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
    float borderThickness);
void DrawSocketPin(
    ImDrawList* drawList,
    const ImVec2& pin,
    float radius,
    ImU32 baseColor,
    const GraphStyleTokens& tokens,
    bool hovered,
    float interactionEmphasis = 0.0f);
void DrawPreviewFrame(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const GraphStyleTokens& tokens, float uiScale);
float ChannelLaneOffset(const std::string& channel, float zoom);
float ExpandedContractHeight(const EditorNodeGraph::Node& node, const NodeLayoutMetrics& metrics, float measuredLayerHeight = 0.0f);
bool UsesMeasuredNodeHeight(const EditorNodeGraph::Node& node);
bool ShouldShowKindLabel(const EditorNodeGraph::Node& node);
std::string EllipsizeLabel(const std::string& value, float maxWidth);
const char* NodeKindLabel(EditorNodeGraph::NodeKind kind);
const char* ExportAspectPresetLabel(EditorModule::CompositeExportAspectPreset preset);
const char* CompositeSnapPresetLabel(EditorModule::CompositeSnapModePreset preset);
const char* ScopeLabel(EditorNodeGraph::ScopeKind kind);
const char* MaskLabel(EditorNodeGraph::MaskGeneratorKind kind);
const char* MaskUtilityLabel(EditorNodeGraph::MaskUtilityKind kind);
const char* ImageGeneratorLabel(EditorNodeGraph::ImageGeneratorKind kind);
const char* MixBlendLabel(EditorNodeGraph::MixBlendMode mode);
const char* DataMathLabel(EditorNodeGraph::DataMathMode mode);

} // namespace Stack::Editor::NodeGraphUIVisuals
