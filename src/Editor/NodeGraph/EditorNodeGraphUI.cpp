#include "EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Editor/LayerRegistry.h"
#include "App/settings/AppearanceTheme.h"
#include "EditorNodeGraphDefinitions.h"
#include "EditorNodeGraphSerializer.h"
#include "EditorNodeGraphUIMetrics.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <imgui.h>
#include <imgui_internal.h>
#include "Renderer/GLLoader.h"
#include <sstream>
#include <string>
#include <unordered_set>

namespace {

using EditorNodeGraphUIMetrics::CubicBezierPoint;
using EditorNodeGraphUIMetrics::LinkBezierHandle;
using EditorNodeGraphUIMetrics::LinkThicknessScaleFromZoom;
using EditorNodeGraphUIMetrics::NodeUiScaleFromZoom;
using EditorNodeGraphUIMetrics::PinRadiusForZoom;

bool InsertNewNodeOnExistingLink(
    EditorNodeGraphUI* ui,
    EditorModule* editor,
    const EditorNodeGraph::Link& link,
    int newNodeId);

ImVec2 ToImVec2(const EditorNodeGraph::Vec2& value) {
    return ImVec2(value.x, value.y);
}

EditorNodeGraph::Vec2 ToGraphVec2(const ImVec2& value) {
    return EditorNodeGraph::Vec2{ value.x, value.y };
}

bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;

    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    };

    return lower(haystack).find(lower(needle)) != std::string::npos;
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

const NodeFamilyStyle& StyleForFamily(NodeFamily family);
bool ShouldShowKindLabel(const EditorNodeGraph::Node& node);

struct GraphStyleTokens {
    StackAppearance::GraphVisualMode mode = StackAppearance::GraphVisualMode::BlackNodes;
    bool enabled = false;
    bool light = false;
    bool haloOutlines = false;
    bool spotlightSurface = false;
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

using NodeBrowserEntry = EditorNodeGraphDefinitions::NodeCatalogEntry;

NodeFamily FamilyForNode(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Composite:
            return NodeFamily::Gray;
        case EditorNodeGraph::NodeKind::Layer:
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
    color.w = std::clamp(alpha, 0.0f, 1.0f);
    return color;
}

ImVec4 WithScaledAlpha(ImVec4 color, float scale) {
    color.w = std::clamp(color.w * scale, 0.0f, 1.0f);
    return color;
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
    tokens.mode = appearance ? appearance->GetGraphVisualMode() : StackAppearance::GraphVisualMode::BlackNodes;
    tokens.enabled = tokens.mode != StackAppearance::GraphVisualMode::Classic;
    tokens.spotlightSurface = tokens.mode == StackAppearance::GraphVisualMode::SpotlightPrototype;
    tokens.haloOutlines = appearance ? (tokens.spotlightSurface && appearance->GetGraphSpotlightHaloOutlines()) : false;

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

bool GraphDottedMaskLinksEnabled(EditorModule* editor) {
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    return appearance ? appearance->GetGraphDottedMaskLinks() : true;
}

bool IsSummaryOnlyNode(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::CustomMask:
            return true;
        default:
            return false;
    }
}

NodePresentationProfile BuildNodePresentationProfile(const EditorNodeGraph::Node& node, const GraphStyleTokens& tokens) {
    NodePresentationProfile profile;
    (void)tokens;

    profile.showKindLabel = false;
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
            profile.kind = NodePresentationKind::FramelessMedia;
            profile.showFrame = false;
            profile.showTitle = false;
            profile.inlineControls = false;
            profile.hoverDetails = true;
            break;
        case EditorNodeGraph::NodeKind::RawSource:
            profile.kind = NodePresentationKind::SummaryOnly;
            profile.inlineControls = false;
            profile.hoverDetails = true;
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
        case EditorNodeGraph::NodeKind::RawDevelop:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::RawDetailFusion:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::CustomMask:
            profile.kind = NodePresentationKind::SummaryOnly;
            profile.inlineControls = false;
            break;
        case EditorNodeGraph::NodeKind::Output:
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
            profile.kind = NodePresentationKind::CompactControls;
            profile.showTitle = node.kind != EditorNodeGraph::NodeKind::Layer;
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

std::vector<NodeBrowserEntry> BuildNodeBrowserEntries() {
    return EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
}



bool PrototypeHasCompatibleInput(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    const NodeBrowserEntry& entry) {
    EditorNodeGraph::Graph testGraph = graph;
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    EditorNodeGraph::Node testNode = prototype;
    testNode.id = std::max(1, graph.GetNextNodeId() + 1000);
    testGraph.GetNodes().push_back(testNode);

    for (const EditorNodeGraph::SocketDefinition& socket : testGraph.GetSockets(testNode, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (testGraph.CanConnectSockets(fromNodeId, fromSocketId, testNode.id, socket.id)) {
            return true;
        }
    }
    return false;
}

bool PrototypeHasCompatibleOutput(
    const EditorNodeGraph::Graph& graph,
    const NodeBrowserEntry& entry,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!to) {
        return false;
    }
    EditorNodeGraph::Graph testGraph = graph;
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    EditorNodeGraph::Node testNode = prototype;
    testNode.id = std::max(1, graph.GetNextNodeId() + 1000);
    testGraph.GetNodes().push_back(testNode);

    for (const EditorNodeGraph::SocketDefinition& socket : testGraph.GetSockets(testNode, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (testGraph.CanConnectSockets(testNode.id, socket.id, toNodeId, toSocketId)) {
            return true;
        }
    }
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

ImVec2 FitPreviewRect(const ImVec2& bounds, const ImVec2& sourceSize) {
    if (bounds.x <= 1.0f || bounds.y <= 1.0f || sourceSize.x <= 1.0f || sourceSize.y <= 1.0f) {
        return bounds;
    }
    const float scale = std::min(bounds.x / sourceSize.x, bounds.y / sourceSize.y);
    return ImVec2(
        std::max(1.0f, sourceSize.x * scale),
        std::max(1.0f, sourceSize.y * scale));
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
        metrics.width = 90.0f;
        metrics.collapsedHeight = 90.0f;
        metrics.minExpandedHeight = 90.0f;
        metrics.contentLaneWidth = 74.0f;
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

void ApplyLayerSurfaceMetrics(const EditorModule* editor, const EditorNodeGraph::Node& node, NodeLayoutMetrics& metrics) {
    if (!editor || node.kind != EditorNodeGraph::NodeKind::Layer) {
        return;
    }
    const NodeSurfaceSpec spec = editor->GetLayerNodeSurfaceSpec(node.layerIndex);
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
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, std::clamp(alpha, 0.0f, 1.0f)));
}

bool HasAdvancedLayerSurface(const EditorModule* editor, const EditorNodeGraph::Node& node) {
    if (!editor) {
        return false;
    }
    if (node.kind == EditorNodeGraph::NodeKind::RawSource ||
        node.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise ||
        node.kind == EditorNodeGraph::NodeKind::RawDevelop ||
        node.kind == EditorNodeGraph::NodeKind::RawDetailFusion) {
        return true;
    }
    if (node.kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }
    return editor->GetNodeSurfaceSpec(node.id).presentation == NodeSurfacePresentation::RichExpandedSurface;
}

bool HasDedicatedComplexEditor(const EditorModule* editor, const EditorNodeGraph::Node& node) {
    if (!editor) {
        return false;
    }
    if (node.kind == EditorNodeGraph::NodeKind::HdrMerge ||
        node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
        return true;
    }
    return HasAdvancedLayerSurface(editor, node);
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
    return ImGui::ColorConvertFloat4ToU32(BlendColor(base, familyStyle.accent, 0.38f));
}

bool IsChannelSocketId(const std::string& id) {
    return id == "r" || id == "g" || id == "b" || id == "a";
}

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
    return ImGui::ColorConvertFloat4ToU32(SocketColorVec(socket, familyStyle, tokens));
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
        return IM_COL32(255, 255, 255, 255);
    }
    if (!style.channel.empty()) {
        if (style.channel == "r") return IM_COL32(255, 64, 64, 210);
        if (style.channel == "g") return IM_COL32(64, 255, 64, 210);
        if (style.channel == "b") return IM_COL32(64, 128, 255, 210);
        if (style.channel == "a") return IM_COL32(220, 220, 220, 210);
    }

    switch (style.kind) {
        case LinkVisualKind::Analysis:
        case LinkVisualKind::MaskEndpoint:
            return IM_COL32(130, 230, 170, 230);
        case LinkVisualKind::Raw:
        case LinkVisualKind::Image:
            return IM_COL32(120, 170, 255, 230);
    }
    return IM_COL32(120, 170, 255, 230);
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
    float exponent = 3.2f,
    float coreRatio = 0.72f,
    float edgePower = 1.35f) {
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

    drawList->PrimWriteVtx(center, uv, ImGui::ColorConvertFloat4ToU32(centerColor));
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
                ImGui::ColorConvertFloat4ToU32(ringColor));
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
    float exponent = 3.2f) {
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
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(surfaceFill), rounding);
        drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(borderColor), rounding, 0, borderThickness);
        return;
    }

    if (!tokens.spotlightSurface) {
        drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(surfaceFill), rounding);
        drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(borderColor), rounding, 0, borderThickness);
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
    bool hovered) {
    if (!tokens.enabled) {
        drawList->AddCircleFilled(pin, radius, hovered ? IM_COL32(255, 255, 255, 255) : baseColor);
        return;
    }

    const ImVec4 baseVec = ImGui::ColorConvertU32ToFloat4(baseColor);
    const float hoverBoost = hovered ? 1.0f : 0.0f;
    drawList->AddCircleFilled(
        pin,
        radius * (hovered ? 1.95f : 1.55f),
        ColorWithAlpha(hovered ? tokens.selected : baseVec, hovered ? 0.32f : 0.18f));
    drawList->AddCircleFilled(pin, radius * 1.14f, ColorWithAlpha(tokens.canvas, 0.86f));
    drawList->AddCircleFilled(pin, radius * (hovered ? 0.92f : 0.78f), ImGui::ColorConvertFloat4ToU32(BlendColor(baseVec, tokens.text, hoverBoost * 0.22f)));
    drawList->AddCircle(pin, radius * 1.14f, ColorWithAlpha(hovered ? tokens.selected : tokens.spotlightHalo, hovered ? 0.90f : 0.52f), 16, std::max(0.8f, radius * 0.18f));
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

float ExpandedContractHeight(const EditorNodeGraph::Node& node, const NodeLayoutMetrics& metrics, float measuredLayerHeight = 0.0f) {
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
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Layer:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::Preview:
        case EditorNodeGraph::NodeKind::RawDetailAutoMask:
        case EditorNodeGraph::NodeKind::HdrMerge:
        case EditorNodeGraph::NodeKind::MaskCombine:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::CustomMask:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::ImageGenerator:
        case EditorNodeGraph::NodeKind::DataMath:
            return true;
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
        case EditorNodeGraph::NodeKind::RawNeuralDenoise: return "RAW Denoise";
        case EditorNodeGraph::NodeKind::RawDevelop: return "Develop";
        case EditorNodeGraph::NodeKind::RawDetailAutoMask: return "RAW Detail Auto Mask";
        case EditorNodeGraph::NodeKind::RawDetailFusion: return "Pre-Local Exposure";
        case EditorNodeGraph::NodeKind::HdrMerge: return "HDR Merge";
        case EditorNodeGraph::NodeKind::Layer: return "Layer";
        case EditorNodeGraph::NodeKind::Output: return "Output";
        case EditorNodeGraph::NodeKind::Composite: return "Composite";
        case EditorNodeGraph::NodeKind::Scope: return "Scope";
        case EditorNodeGraph::NodeKind::MaskGenerator: return "Mask";
        case EditorNodeGraph::NodeKind::CustomMask: return "Custom Mask";
        case EditorNodeGraph::NodeKind::MaskCombine: return "Scalar Combine";
        case EditorNodeGraph::NodeKind::Mix: return "Image Blend";
        case EditorNodeGraph::NodeKind::Preview: return "Preview";
        case EditorNodeGraph::NodeKind::MaskUtility: return "Scalar Utility";
        case EditorNodeGraph::NodeKind::ImageToMask: return "Image To Scalar";
        case EditorNodeGraph::NodeKind::ImageGenerator: return "Generator";
        case EditorNodeGraph::NodeKind::DataMath: return "Data Math";
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
        case EditorNodeGraph::MaskUtilityKind::Invert: return "Invert Scalar";
        case EditorNodeGraph::MaskUtilityKind::Levels: return "Remap Scalar";
        case EditorNodeGraph::MaskUtilityKind::Threshold: return "Threshold Scalar";
    }
    return "Scalar Utility";
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
    }
    return "Clamp";
}

} // namespace

bool EditorNodeGraphUI::ConnectOutputToBestInput(EditorModule* editor, int fromNodeId, const std::string& fromSocketId, int toNodeId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to) {
        return false;
    }

    EditorNodeGraph::SocketDefinition fromSocket;
    if (!graph.FindSocket(fromNodeId, fromSocketId, &fromSocket)) {
        return false;
    }

    // Heuristic: If carrying specific color channel, prioritize exact matching channel socket first!
    std::unordered_set<int> visited;
    std::string channel = GetUpstreamChannel(graph, fromNodeId, fromSocketId, visited);
    const bool fromScalarStream = graph.IsScalarSocketStream(fromNodeId, fromSocketId);
    if (!channel.empty()) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
            if (socket.direction == EditorNodeGraph::SocketDirection::Input && socket.id == channel) {
                std::string error;
                if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                    editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                    return true;
                }
            }
        }
    }

    if (fromScalarStream && to->kind == EditorNodeGraph::NodeKind::Layer) {
        std::string error;
        if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, EditorNodeGraph::kImageInputSocketId) &&
            editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, EditorNodeGraph::kImageInputSocketId, &error)) {
            return true;
        }
    }

    // Determine the preferred target socket type
    EditorNodeGraph::SocketType preferredType = fromSocket.type;
    if (from->kind == EditorNodeGraph::NodeKind::ChannelSplit || fromScalarStream) {
        preferredType = EditorNodeGraph::SocketType::Image;
    }

    // 1. Try to connect to an input socket of the preferred type first (Exact match)
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (socket.type == preferredType) {
            std::string error;
            if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                return true;
            }
        }
    }

    // 2. Try to connect to any other input socket (Cross-type match)
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (socket.type != preferredType) {
            std::string error;
            if (graph.CanConnectSockets(fromNodeId, fromSocketId, toNodeId, socket.id) &&
                editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
                return true;
            }
        }
    }

    return false;
}

bool EditorNodeGraphUI::ConnectBestOutputToInput(EditorModule* editor, int fromNodeId, int toNodeId, const std::string& toSocketId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to) {
        return false;
    }

    EditorNodeGraph::SocketDefinition toSocket;
    if (!graph.FindSocket(toNodeId, toSocketId, &toSocket)) {
        return false;
    }

    // Heuristic: If toSocketId is channel, prioritize connecting from matching channel output on source node (like ChannelSplit)
    if (toSocketId == "r" || toSocketId == "g" || toSocketId == "b" || toSocketId == "a") {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
            if (socket.direction == EditorNodeGraph::SocketDirection::Output && socket.id == toSocketId) {
                std::string error;
                if (graph.CanConnectSockets(fromNodeId, socket.id, toNodeId, toSocketId) &&
                    editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
                    return true;
                }
            }
        }
    }

    // Determine the preferred source socket type
    EditorNodeGraph::SocketType preferredType = toSocket.type;
    if (to->kind == EditorNodeGraph::NodeKind::ChannelSplit && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        preferredType = EditorNodeGraph::SocketType::Image;
    }

    // 1. Try to connect to an output socket of the preferred type first (Exact match)
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (socket.type == preferredType) {
            std::string error;
            if (graph.CanConnectSockets(fromNodeId, socket.id, toNodeId, toSocketId) &&
                editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
                return true;
            }
        }
    }

    // 2. Try to connect to any other output socket (Cross-type match)
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (socket.type != preferredType) {
            std::string error;
            if (graph.CanConnectSockets(fromNodeId, socket.id, toNodeId, toSocketId) &&
                editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
                return true;
            }
        }
    }

    return false;
}

std::string EditorNodeGraphUI::GetUpstreamChannel(const EditorNodeGraph::Graph& graph, int nodeId, const std::string& socketId, std::unordered_set<int>& visited) {
    (void)visited;
    return graph.ResolveSocketChannel(nodeId, socketId);
}

void EditorNodeGraphUI::Initialize() {}

bool EditorNodeGraphUI::IsGraphCanvasHovered() const {
    return ImGui::IsMouseHoveringRect(ToImVec2(m_CanvasMin), ToImVec2(m_CanvasMax), false);
}

bool EditorNodeGraphUI::CanOpenChannelSplitConfirm(const EditorNodeGraph::Graph& graph, int nodeId) const {
    const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Layer) {
        return false;
    }
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
    if (anyPopupOpen && !m_NodeBrowserOpen) {
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

void EditorNodeGraphUI::Render(EditorModule* editor) {
    m_ActiveEditor = editor;
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    bool draggingMask = false;
    if (m_DragOutputNodeId > 0) {
        EditorNodeGraph::SocketDefinition sock;
        if (graph.FindSocket(m_DragOutputNodeId, m_DragOutputSocketId, &sock)) {
            std::unordered_set<int> visited;
            std::string channel = GetUpstreamChannel(graph, m_DragOutputNodeId, m_DragOutputSocketId, visited);
            if (sock.type == EditorNodeGraph::SocketType::Mask || 
                sock.id == "r" || sock.id == "g" || sock.id == "b" || sock.id == "a" ||
                !channel.empty()) {
                draggingMask = true;
            }
        }
    } else if (m_DragInputNodeId > 0) {
        EditorNodeGraph::SocketDefinition sock;
        if (graph.FindSocket(m_DragInputNodeId, m_DragInputSocketId, &sock)) {
            std::unordered_set<int> visited;
            std::string channel = GetUpstreamChannel(graph, m_DragInputNodeId, m_DragInputSocketId, visited);
            if (sock.type == EditorNodeGraph::SocketType::Mask || 
                sock.id == "r" || sock.id == "g" || sock.id == "b" || sock.id == "a" ||
                !channel.empty()) {
                draggingMask = true;
            }
        }
    }
    graph.SetForceOutputFourPins(draggingMask);

    m_NodeContentActive = false;
    m_NodeContentHovered = false;
    m_GraphInteractionBlocked = false;
    m_MouseOwner = GraphMouseOwner::None;
    m_LastNodeControlId = 0;
    m_NodeLayoutCache.clear();

    // Minimal Guide Removed as requested by user to keep empty state pristine

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize = ImVec2(std::max(320.0f, available.x), std::max(320.0f, available.y));
    ImGui::Dummy(canvasSize);

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ADD_NODE_DRAG_PAYLOAD")) {
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
        ImGui::EndDragDropTarget();
    }

    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    m_CanvasOrigin = ToGraphVec2(canvasMin);
    m_CanvasMin = ToGraphVec2(canvasMin);
    m_CanvasMax = ToGraphVec2(canvasMax);
    editor->ApplyGraphAutoFocusFrame(canvasSize.x, canvasSize.y, m_Pan.x, m_Pan.y, m_Zoom);
    editor->SetGraphDropTargetRect(canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y);
    if (!m_SmoothZoomActive) {
        m_ZoomTarget = m_Zoom;
    }
    bool graphHovered = IsGraphCanvasHovered();
    if (graphHovered) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        float drawersWidth = std::max(editor->GetLeftPanelWidthAnim(), editor->GetNodesPanelWidthAnim());
        if (mousePos.x >= canvasMin.x && mousePos.x <= canvasMin.x + drawersWidth) {
            graphHovered = false;
        }
    }
    if (graphHovered) {
        m_LastGraphMousePos = ScreenToGraph(ToGraphVec2(ImGui::GetIO().MousePos));
        m_HasLastGraphMousePos = true;
    }

    // Robust raw Tab key press detection to bypass ImGui's internal text input focus interception/consumption
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

    if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && tabPressed) {
        
        if (m_NodeBrowserOpen) {
            CloseNodeBrowser();
        } else if (editor->CanConsumeEditorCommandKeys() && (graphHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))) {
            const std::vector<int>& selectedIds = graph.GetSelectedNodeIds();
            if (selectedIds.size() == 1) {
                int selectedNodeId = selectedIds.front();
                if (const EditorNodeGraph::Node* selectedNode = graph.FindNode(selectedNodeId)) {
                    // Clear any existing push state just in case
                    m_PushedSourceNodeId = -1;
                    m_PushDistance = 0.0f;
                    m_PushedNodeIds.clear();

                    // Push downstream nodes in the chain
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
                    // Refresh node layouts immediately so rendering aligns perfectly on the next draw call
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
    if (graphHovered && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::GetIO().MouseWheel != 0.0f) {
        editor->CancelGraphAutoFocusTracking();
        ZoomAtMouse(ImGui::GetIO().MouseWheel);
    }
    if (m_SmoothZoomActive) {
        const float nextZoom = m_Zoom + ((m_ZoomTarget - m_Zoom) * std::clamp(ImGui::GetIO().DeltaTime * 18.0f, 0.0f, 1.0f));
        m_Zoom = (std::abs(m_ZoomTarget - nextZoom) < 0.0005f) ? m_ZoomTarget : nextZoom;
        m_Pan.x = m_SmoothZoomFocusScreen.x - m_CanvasOrigin.x - m_SmoothZoomFocusGraph.x * m_Zoom;
        m_Pan.y = m_SmoothZoomFocusScreen.y - m_CanvasOrigin.y - m_SmoothZoomFocusGraph.y * m_Zoom;
        if (std::abs(m_ZoomTarget - m_Zoom) < 0.0005f) {
            m_Zoom = m_ZoomTarget;
            m_SmoothZoomActive = false;
        }
    }
    editor->SetGraphViewTransform(canvasMin.x, canvasMin.y, m_Pan.x, m_Pan.y, m_Zoom);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin, canvasMax, true);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const ImVec4 workspaceBg = editor->GetWorkspaceBaseColor();
    const ImVec4 canvasBg = graphStyle.enabled ? graphStyle.canvas : workspaceBg;
    drawList->AddRectFilled(canvasMin, canvasMax, ImGui::ColorConvertFloat4ToU32(canvasBg));

    if (!graphStyle.spotlightSurface) {
        const float gridStep = std::max(8.0f, 32.0f * m_Zoom);
        const float luminance = 0.2126f * workspaceBg.x + 0.7152f * workspaceBg.y + 0.0722f * workspaceBg.z;
        const ImU32 gridColor = (luminance < 0.5f)
            ? IM_COL32(255, 255, 255, 20)  // Dark background: soft light grid
            : IM_COL32(0, 0, 0, 18);        // Light background: soft dark grid
        for (float x = std::fmod(m_Pan.x, gridStep); x < canvasSize.x; x += gridStep) {
            drawList->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), gridColor);
        }
        for (float y = std::fmod(m_Pan.y, gridStep); y < canvasSize.y; y += gridStep) {
            drawList->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), gridColor);
        }
    }
    ClampPanToContent(graph);

    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        node.position = ClampGraphPosition(node.position);
    }

    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        RefreshNodeLayoutCache(graph, node);
    }

    ImDrawListSplitter graphSplitter;
    graphSplitter.Split(drawList, 2);
    graphSplitter.SetCurrentChannel(drawList, 1);
    const std::vector<int> nodeRenderOrder = GetNodeRenderOrder(graph);
    for (int nodeId : nodeRenderOrder) {
        if (EditorNodeGraph::Node* node = graph.FindNode(nodeId)) {
            RenderNode(editor, *node);
        }
    }
    graphSplitter.SetCurrentChannel(drawList, 0);
    RenderGroups(editor, graph);
    RenderLinks(graph);
    graphSplitter.Merge(drawList);

    RenderInteraction(editor, graph);
    RenderValidationStatus(graph);
    RenderChannelSplitConfirmPrompt(editor);
    const int debugHoveredNodeId = IsGraphCanvasHovered() ? FindNodeAt(graph, ToGraphVec2(ImGui::GetMousePos())) : -1;
    RenderInteractionDebugOverlay(graph, debugHoveredNodeId, m_MouseOwner);
    RenderContextMenu(editor);
    RenderNodeBrowser(editor);

    const float seamFade = std::min(120.0f, canvasSize.x);
    const float edgeFade = std::min(56.0f, std::min(canvasSize.x, canvasSize.y) * 0.10f);
    const float topSeamFade = std::min(26.0f, std::max(12.0f, edgeFade * 0.55f));
    const ImU32 fadeSoft = ColorWithAlpha(canvasBg, graphStyle.spotlightSurface ? 0.38f : 0.62f);
    const ImU32 fadeStrong = ColorWithAlpha(canvasBg, graphStyle.spotlightSurface ? 0.82f : 1.0f);
    const ImU32 fadeClear = ColorWithAlpha(canvasBg, 0.0f);
    if (edgeFade > 1.0f) {
        drawList->AddRectFilledMultiColor(canvasMin, ImVec2(canvasMin.x + edgeFade, canvasMax.y), fadeSoft, fadeClear, fadeClear, fadeSoft);
        drawList->AddRectFilledMultiColor(canvasMin, ImVec2(canvasMax.x, canvasMin.y + edgeFade), fadeSoft, fadeSoft, fadeClear, fadeClear);
        drawList->AddRectFilledMultiColor(ImVec2(canvasMin.x, canvasMax.y - edgeFade), canvasMax, fadeClear, fadeClear, fadeSoft, fadeSoft);
    }
    if (topSeamFade > 1.0f) {
        drawList->AddRectFilledMultiColor(
            canvasMin,
            ImVec2(canvasMax.x, canvasMin.y + topSeamFade),
            fadeStrong,
            fadeStrong,
            fadeClear,
            fadeClear);
    }
    if (seamFade > 1.0f) {
        drawList->AddRectFilledMultiColor(
            ImVec2(canvasMax.x - seamFade, canvasMin.y),
            canvasMax,
            fadeClear,
            fadeStrong,
            fadeStrong,
            fadeClear);
    }

    drawList->PopClipRect();
}

EditorNodeGraphUI::NodeLayoutCache EditorNodeGraphUI::BuildNodeLayoutCache(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& node) const {
    const NodeLayoutMetrics metrics = MetricsForNode(node);
    NodeLayoutMetrics adjustedMetrics = metrics;
    ApplyModernCompactMetrics(node, adjustedMetrics);
    ApplyLayerSurfaceMetrics(m_ActiveEditor, node, adjustedMetrics);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(m_ActiveEditor);
    const NodePresentationProfile profile = BuildNodePresentationProfile(node, graphStyle);
    const float uiScale = NodeContentScale();
    const float pinRadius = NodePinRadius();
    const float headerInsetX = adjustedMetrics.headerInsetX * uiScale;
    const float headerInsetY = adjustedMetrics.headerInsetY * uiScale;
    const float bodyInsetBottom = adjustedMetrics.bodyInsetBottom * uiScale;
    const float sectionGap = adjustedMetrics.sectionGap * uiScale;
    const float laneInset = std::max(
        14.0f * uiScale,
        ((adjustedMetrics.width - adjustedMetrics.contentLaneWidth) * 0.5f - adjustedMetrics.headerInsetX) * uiScale);

    const EditorNodeGraph::Vec2 safePosition = ClampGraphPosition(node.position);
    const EditorNodeGraph::Vec2 nodeScreenPos = GraphToScreen(safePosition);
    const EditorNodeGraph::Vec2 nodeSize = NodeScreenSize(node);
    const ImVec2 frameMin = ToImVec2(nodeScreenPos);
    const ImVec2 frameMax(frameMin.x + nodeSize.x, frameMin.y + nodeSize.y);

    const bool showKindLabel = profile.showKindLabel;
    const float kindLabelBlock = showKindLabel ? (adjustedMetrics.kindLabelHeight * uiScale) + (2.0f * uiScale) : 0.0f;
    const float titleBlock = profile.showTitle ? (adjustedMetrics.titleHeight * uiScale) : 0.0f;
    const float headerVisualHeight = headerInsetY + kindLabelBlock + titleBlock;
    const float expandedHeaderHeight = headerVisualHeight + std::max(6.0f, sectionGap * 0.65f);
    const float collapsedHeaderHeight = std::max(headerVisualHeight + (headerInsetY * 0.45f), frameMax.y - frameMin.y);
    const float headerBottom = std::min(
        frameMax.y - std::max(6.0f * uiScale, bodyInsetBottom * 0.35f),
        frameMin.y + (node.expanded ? expandedHeaderHeight : collapsedHeaderHeight));

    const float contentMinX = std::min(frameMax.x - headerInsetX, frameMin.x + headerInsetX + laneInset);
    const float contentMaxX = std::max(contentMinX + 24.0f, frameMax.x - headerInsetX - laneInset);
    const float contentMinY = node.expanded ? headerBottom : frameMin.y + headerInsetY;
    const float contentMaxY = std::max(contentMinY, frameMax.y - bodyInsetBottom);
    const float inputPinX = frameMin.x + std::max(pinRadius + (6.0f * uiScale), laneInset * 0.52f);
    const float outputPinX = frameMax.x - std::max(pinRadius + (6.0f * uiScale), laneInset * 0.52f);

    NodeLayoutCache cache;
    cache.frameRect = CachedRect{ frameMin, frameMax };
    cache.headerRect = CachedRect{ frameMin, ImVec2(frameMax.x, headerBottom) };
    cache.contentRect = CachedRect{
        ImVec2(contentMinX, contentMinY),
        ImVec2(contentMaxX, contentMaxY)
    };
    if (profile.kind == NodePresentationKind::FramelessMedia) {
        cache.headerRect = CachedRect{ frameMin, frameMax };
        cache.contentRect = CachedRect{
            ImVec2(frameMin.x + 2.0f * uiScale, frameMin.y + 2.0f * uiScale),
            ImVec2(frameMax.x - 2.0f * uiScale, frameMax.y - 2.0f * uiScale)
        };
    }

    std::vector<EditorNodeGraph::SocketDefinition> inputSockets;
    std::vector<EditorNodeGraph::SocketDefinition> outputSockets;
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
        if (!socket.visible) {
            continue;
        }
        if (socket.direction == EditorNodeGraph::SocketDirection::Input) {
            inputSockets.push_back(socket);
        } else {
            outputSockets.push_back(socket);
        }
    }

    auto distributeAnchors = [&](const std::vector<EditorNodeGraph::SocketDefinition>& sockets, EditorNodeGraph::SocketDirection direction) {
        if (sockets.empty()) {
            return;
        }

        const float top = cache.headerRect.min.y + std::max(pinRadius + (4.0f * uiScale), headerInsetY * 0.9f);
        const float bottom = node.expanded
            ? std::max(top, cache.contentRect.max.y - std::max(pinRadius + (2.0f * uiScale), bodyInsetBottom * 0.2f))
            : std::max(top, cache.frameRect.max.y - std::max(pinRadius + (4.0f * uiScale), headerInsetY * 0.9f));

        for (size_t index = 0; index < sockets.size(); ++index) {
            const float y = sockets.size() == 1
                ? (top + bottom) * 0.5f
                : (top + ((bottom - top) * static_cast<float>(index) / static_cast<float>(sockets.size() - 1)));
            cache.socketAnchors.push_back(SocketAnchor{
                sockets[index].id,
                direction,
                ImVec2(
                    direction == EditorNodeGraph::SocketDirection::Input ? inputPinX : outputPinX,
                    y)
            });
        }
    };

    distributeAnchors(inputSockets, EditorNodeGraph::SocketDirection::Input);
    distributeAnchors(outputSockets, EditorNodeGraph::SocketDirection::Output);
    return cache;
}

void EditorNodeGraphUI::RefreshNodeLayoutCache(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Node& node) {
    m_NodeLayoutCache[node.id] = BuildNodeLayoutCache(graph, node);
}

const EditorNodeGraphUI::NodeLayoutCache* EditorNodeGraphUI::FindNodeLayoutCache(int nodeId) const {
    const auto it = m_NodeLayoutCache.find(nodeId);
    return it != m_NodeLayoutCache.end() ? &it->second : nullptr;
}

std::vector<int> EditorNodeGraphUI::GetNodeRenderOrder(const EditorNodeGraph::Graph& graph) {
    std::vector<int> order;
    order.reserve(graph.GetNodes().size());
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        order.push_back(node.id);
        if (m_NodeFrontOrder.find(node.id) == m_NodeFrontOrder.end()) {
            m_NodeFrontOrder[node.id] = m_NodeFrontOrderCounter++;
        }
    }

    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const EditorNodeGraph::Node* nodeA = graph.FindNode(a);
        const EditorNodeGraph::Node* nodeB = graph.FindNode(b);
        if (!nodeA || !nodeB) {
            return a < b;
        }
        const bool richA = nodeA->kind == EditorNodeGraph::NodeKind::Layer && nodeA->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(nodeA->layerIndex);
        const bool richB = nodeB->kind == EditorNodeGraph::NodeKind::Layer && nodeB->expanded && m_ActiveEditor && m_ActiveEditor->LayerUsesRichNodeSurface(nodeB->layerIndex);
        if (richA != richB) {
            return !richA;
        }
        const std::uint64_t stampA = m_NodeFrontOrder[a];
        const std::uint64_t stampB = m_NodeFrontOrder[b];
        return stampA < stampB;
    });
    return order;
}

void EditorNodeGraphUI::TouchNodeFront(int nodeId) {
    if (nodeId <= 0) {
        return;
    }
    m_NodeFrontOrder[nodeId] = m_NodeFrontOrderCounter++;
}

const EditorNodeGraphUI::SocketAnchor* EditorNodeGraphUI::FindSocketAnchor(
    const NodeLayoutCache& cache,
    const std::string& socketId,
    EditorNodeGraph::SocketDirection direction) const {
    for (const SocketAnchor& anchor : cache.socketAnchors) {
        if (anchor.direction == direction && anchor.socketId == socketId) {
            return &anchor;
        }
    }
    return nullptr;
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

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeSize(const EditorNodeGraph::Node& node) const {
    if (node.kind == EditorNodeGraph::NodeKind::Output ||
        node.kind == EditorNodeGraph::NodeKind::ChannelSplit ||
        node.kind == EditorNodeGraph::NodeKind::ChannelCombine ||
        IsSummaryOnlyNode(node)) {
        if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
            return EditorNodeGraph::Vec2{ 128.0f, 72.0f };
        }
        return EditorNodeGraph::Vec2{ 90.0f, 90.0f };
    }

    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(m_ActiveEditor, node, metrics);
    const float measuredLayerHeight = [&]() -> float {
        const auto it = m_NodeMeasuredBaseHeights.find(node.id);
        return it != m_NodeMeasuredBaseHeights.end() ? it->second : 0.0f;
    }();
    const float expandedHeight = (!node.expanded)
        ? metrics.collapsedHeight
        : std::max(
            metrics.minExpandedHeight,
            (UsesMeasuredNodeHeight(node) && measuredLayerHeight > 0.0f)
                ? measuredLayerHeight
                : ExpandedContractHeight(node, metrics, measuredLayerHeight));
    return EditorNodeGraph::Vec2{
        metrics.width,
        std::max(metrics.collapsedHeight, SanitizeFinite(expandedHeight, metrics.minExpandedHeight))
    };
}

void EditorNodeGraphUI::RenderNode(EditorModule* editor, EditorNodeGraph::Node& node) {
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    NodeLayoutMetrics metrics = MetricsForNode(node);
    ApplyModernCompactMetrics(node, metrics);
    ApplyLayerSurfaceMetrics(editor, node, metrics);
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const NodePresentationProfile presentation = BuildNodePresentationProfile(node, graphStyle);
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
    const bool selected = graph.IsNodeSelected(node.id);
    const float uiScale = NodeContentScale();
    const float pinRadius = NodePinRadius();

    if (presentation.kind == NodePresentationKind::FramelessMedia) {
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsMouseHoveringRect(min, max, false);
        const unsigned int texture = GetImagePreviewTexture(node);
        const ImVec2 frameSize(std::max(1.0f, max.x - min.x), std::max(1.0f, max.y - min.y));
        const ImVec2 imageSourceSize(
            static_cast<float>(std::max(1, node.image.width)),
            static_cast<float>(std::max(1, node.image.height)));
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
                IM_COL32_WHITE);
        } else {
            drawList->AddRectFilled(imageMin, imageMax, ColorWithAlpha(graphStyle.nodeSurface, 0.42f), std::max(3.0f, 5.0f * uiScale));
        }
        if (selected) {
            drawList->AddRectFilled(imageMin, imageMax, IM_COL32(255, 255, 255, 22), std::max(3.0f, 5.0f * uiScale));
        } else if (hovered) {
            const float rounding = std::max(3.0f, 5.0f * uiScale);
            drawList->AddRect(
                imageMin,
                imageMax,
                ColorWithAlpha(graphStyle.spotlightHalo, 0.42f),
                rounding,
                0,
                std::max(0.8f, 1.0f * uiScale));
        }

        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
            const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
            if (!anchor) {
                continue;
            }
            const bool hoveredSocket = socket.direction == EditorNodeGraph::SocketDirection::Input
                ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
                : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
            DrawSocketPin(drawList, anchor->screenPos, pinRadius, SocketColor(socket, familyStyle, graphStyle), graphStyle, hoveredSocket);
        }

        if ((hovered || selected) && node.image.width > 0 && node.image.height > 0) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(node.title.empty() ? "Image" : node.title.c_str());
            ImGui::TextDisabled("%d x %d", node.image.width, node.image.height);
            ImGui::TextDisabled("%s", graph.GetActiveImageNodeId() == node.id ? "Active image" : "Unconnected image");
            ImGui::EndTooltip();
        }
        return;
    }

    const bool isSquareNode = (node.kind == EditorNodeGraph::NodeKind::Output) ||
        (node.kind == EditorNodeGraph::NodeKind::ChannelSplit) ||
        (node.kind == EditorNodeGraph::NodeKind::ChannelCombine) ||
        IsSummaryOnlyNode(node);

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
        } else if (node.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            squareLabel = "RAW Denoise";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDevelop) {
            squareLabel = "Develop";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask) {
            squareLabel = "Auto Mask";
        } else if (node.kind == EditorNodeGraph::NodeKind::RawDetailFusion) {
            squareLabel = "Pre-Local";
        } else if (node.kind == EditorNodeGraph::NodeKind::HdrMerge) {
            squareLabel = "HDR Merge";
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
            for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
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
            ImGui::ColorConvertFloat4ToU32(familyStyle.text),
            squareDisplayLabel.c_str());

        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            const ImVec2 modePos(
                min.x + (max.x - min.x - scaledModeTextSize.x) * 0.5f,
                textPos.y + scaledTextSize.y + 3.0f * uiScale);
            drawList->AddText(
                ImGui::GetFont(),
                modeFontSize,
                modePos,
                graphStyle.enabled ? ColorWithAlpha(graphStyle.mutedText, 0.92f) : IM_COL32(180, 195, 205, 220),
                outputModeLabel);
        }

        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
            const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
            if (anchor) {
                const ImVec2 pin = anchor->screenPos;
                const bool hoveredSocket = (socket.direction == EditorNodeGraph::SocketDirection::Input)
                    ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
                    : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);

                const ImU32 baseColor = SocketColor(socket, familyStyle, graphStyle);
                DrawSocketPin(drawList, pin, pinRadius, baseColor, graphStyle, hoveredSocket);

                const bool isChannelSocket = socket.id == "r" || socket.id == "g" || socket.id == "b" || socket.id == "a";
                if (isSquareNode && isChannelSocket) {
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

                if (hoveredSocket) {
                    ImGui::BeginTooltip();
                    if (socket.id == "a" && socket.label == "A (Generated)") {
                        ImGui::Text("Alpha (Generated Opaque - Original image has no alpha)");
                    } else {
                        ImGui::Text("%s", socket.label.c_str());
                    }
                    ImGui::EndTooltip();
                }
            }
        }

        if (IsSummaryOnlyNode(node) && ImGui::IsMouseHoveringRect(min, max, false)) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(node.title.empty() ? squareLabel.c_str() : node.title.c_str());
            if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
                if (!node.rawSource.label.empty()) {
                    ImGui::TextDisabled("%s", node.rawSource.label.c_str());
                }
                if (!node.rawSource.sourcePath.empty()) {
                    ImGui::TextDisabled("%s", node.rawSource.sourcePath.c_str());
                }
            } else if (HasDedicatedComplexEditor(editor, node)) {
                ImGui::TextDisabled("Detailed controls are in the node inspector.");
            }
            ImGui::EndTooltip();
        }
        
        return;
    }

    const bool expanded = node.expanded;
    const bool richExpandedSurface = node.kind == EditorNodeGraph::NodeKind::Layer && editor->LayerUsesRichNodeSurface(node.layerIndex);
    const NodeSurfaceSpec nodeSurfaceSpec = node.kind == EditorNodeGraph::NodeKind::Layer
        ? editor->GetLayerNodeSurfaceSpec(node.layerIndex)
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
    const ImVec4 fillColor = expanded
        ? familyStyle.fill
        : BlendColor(familyStyle.fill, ImVec4(0.10f, 0.12f, 0.13f, familyStyle.fill.w), 0.18f);
    const ImVec4 borderColor = familyStyle.border;
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

    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
        const bool isInput = socket.direction == EditorNodeGraph::SocketDirection::Input;
        const SocketAnchor* anchor = FindSocketAnchor(*layout, socket.id, socket.direction);
        if (!anchor) {
            continue;
        }
        const ImVec2 pin = anchor->screenPos;
        const bool hoveredSocket = isInput
            ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
            : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
        const ImU32 baseColor = SocketColor(socket, familyStyle, graphStyle);
        DrawSocketPin(drawList, pin, pinRadius, baseColor, graphStyle, hoveredSocket);
        const float pinLabelSize = std::max(3.5f, ImGui::GetFontSize() * uiScale * 0.86f);
        const float pinLabelScale = pinLabelSize / std::max(1.0f, ImGui::GetFontSize());
        const ImVec2 labelMin = isInput
            ? ImVec2(pin.x + (10.0f * uiScale), pin.y - (pinLabelSize * 0.85f))
            : ImVec2(layout->contentRect.max.x + (6.0f * uiScale), pin.y - (pinLabelSize * 0.85f));
        const ImVec2 labelMax = isInput
            ? ImVec2(layout->contentRect.min.x - (6.0f * uiScale), pin.y + (pinLabelSize * 0.85f))
            : ImVec2(pin.x - (10.0f * uiScale), pin.y + (pinLabelSize * 0.85f));
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
                ColorWithAlpha(familyStyle.mutedText, 0.94f),
                displayLabel.c_str());
            drawList->PopClipRect();
        }
    }

    auto nodePrimaryTitle = [&]() -> std::string {
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::Layer:
                return std::to_string(node.layerIndex + 1) + ". " + (node.title.empty() ? "Layer" : node.title);
            case EditorNodeGraph::NodeKind::MaskGenerator:
                return MaskLabel(node.maskKind);
            case EditorNodeGraph::NodeKind::CustomMask:
                return node.title.empty() ? "Custom Mask" : node.title;
            case EditorNodeGraph::NodeKind::MaskCombine:
                return node.title.empty() ? "Intersect Scalars" : node.title;
            case EditorNodeGraph::NodeKind::MaskUtility:
                return MaskUtilityLabel(node.maskUtilityKind);
            case EditorNodeGraph::NodeKind::ImageToMask:
                return node.imageToMaskKind == EditorNodeGraph::ImageToMaskKind::SampledRange
                    ? "Sampled Range Scalar"
                    : "Image To Scalar";
            case EditorNodeGraph::NodeKind::ImageGenerator:
                return ImageGeneratorLabel(node.imageGeneratorKind);
            case EditorNodeGraph::NodeKind::Scope:
                return ScopeLabel(node.scopeKind);
            case EditorNodeGraph::NodeKind::Mix:
                return node.title.empty() ? "Blend Images" : node.title;
            case EditorNodeGraph::NodeKind::DataMath:
                return node.title.empty() ? DataMathLabel(node.dataMathMode) : node.title;
            case EditorNodeGraph::NodeKind::Preview:
                return node.title.empty() ? "Preview" : node.title;
            case EditorNodeGraph::NodeKind::RawDetailAutoMask:
                return node.title.empty() ? "RAW Detail Auto Mask" : node.title;
            case EditorNodeGraph::NodeKind::Image:
            case EditorNodeGraph::NodeKind::RawSource:
            case EditorNodeGraph::NodeKind::RawNeuralDenoise:
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
            ImGui::ColorConvertFloat4ToU32(familyStyle.text),
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

    ImGui::SetCursorScreenPos(layout->contentRect.min);
    bool fontScalePushed = false;
    if (std::abs(contentScale - 1.0f) > 0.0001f) {
        ImGui::SetWindowFontScale(contentScale);
        fontScalePushed = true;
    }
    ImGui::PushItemWidth(controlWidth);
    ImGuiExtras::ResetNodeControlState();
    ImGuiExtras::BeginGraphNodeControlScope(ImGuiExtras::GraphNodeControlScopeConfig{
        68.0f * contentScale,
        46.0f * contentScale,
        82.0f * contentScale,
        contentScale
    });
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
            IM_COL32(118, 134, 144, 90),
            1.0f);
        ImGui::Dummy(ImVec2(separatorWidth, separatorHeight));
    };

    if (node.kind == EditorNodeGraph::NodeKind::Layer) {
        auto& layers = editor->GetLayers();
        if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(layers.size())) {
            editor->RenderLayerControlsWithDirtyTracking(node, [&](LayerBase& layer) {
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
            });
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
        ImGui::TextDisabled("%s", graph.GetActiveImageNodeId() == node.id ? "Active image" : "Unconnected image");
    } else if (node.kind == EditorNodeGraph::NodeKind::RawSource) {
        editor->RenderRawSourceControls(node, controlWidth, false);
    } else if (node.kind == EditorNodeGraph::NodeKind::RawNeuralDenoise) {
        editor->RenderRawNeuralDenoiseControls(node, controlWidth, false);
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
    } else if (node.kind == EditorNodeGraph::NodeKind::Output) {
        bool outputUsesRgbaPins = false;
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
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
            const EditorNodeGraph::Node* from = graph.FindNode(input->fromNodeId);
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
        ImGui::TextDisabled("Scalar A is the base. Scalar B refines it.");
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::DataMath) {
        bool changed = false;
        int mode = static_cast<int>(node.dataMathMode);
        const char* modes[] = { "Clamp", "Add", "Subtract", "Multiply", "Divide", "Average", "Minimum", "Maximum", "Difference", "Remap" };
        if (ImGuiExtras::NodeCombo("Mode", "##DataMathMode", &mode, modes, IM_ARRAYSIZE(modes), controlWidth)) {
            node.dataMathMode = static_cast<EditorNodeGraph::DataMathMode>(std::clamp(mode, 0, 9));
            EditorNodeGraphDefinitions::ApplyNodeMetadata(node);
            changed = true;
        }
        ImGui::TextDisabled("Works on scalar/channel streams or full RGBA images.");
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
    } else if (node.kind == EditorNodeGraph::NodeKind::CustomMask) {
        ImGui::TextDisabled("%d x %d grayscale mask", node.customMask.width, node.customMask.height);
        ImGui::TextDisabled("%zu object%s", node.customMask.objects.size(), node.customMask.objects.size() == 1 ? "" : "s");
        if (ImGuiExtras::RichFullWidthButton("Open Mask Editor", controlWidth, 26.0f)) {
            editor->SwitchToComplexNodeSubWindow(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskUtility) {
        bool changed = false;
        if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert) {
            ImGui::TextDisabled("Mask values are inverted.");
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
    } else if (node.kind == EditorNodeGraph::NodeKind::ImageGenerator) {
        bool changed = false;
        if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Text) {
            changed |= ImGuiExtras::NodeTextMultiline("Text", "##GeneratorText", node.imageGeneratorSettings.text, controlWidth, 4);
            captureIfActive();
            changed |= ImGuiExtras::NodeColorEdit4("Color", "##GeneratorColorA", node.imageGeneratorSettings.colorA, ImGuiColorEditFlags_NoInputs, controlWidth);
            captureIfActive();
            changed |= renderSlider("Size", "##GeneratorFontSize", &node.imageGeneratorSettings.fontSize, 16.0f, 192.0f);
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
        const char* modes[] = { "Normal / Lerp", "Average", "Add", "Multiply", "Screen", "Alpha Over" };
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
            const EditorNodeGraph::Node* from = graph.FindNode(input->fromNodeId);
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
    const bool contentHovered = ImGui::IsMouseHoveringRect(contentUsedMin, contentUsedMax, false);
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const float bottomSafetyPadding = std::max(8.0f, metrics.itemGap * 0.75f);
    const float renderedBaseHeight =
        (std::max(0.0f, contentUsedMax.y - min.y) / std::max(0.001f, uiScale)) +
        metrics.bodyInsetBottom +
        bottomSafetyPadding;
    if (expanded) {
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
    if (contentHovered || refreshedLayout.contentUsedRect.Contains(ImGui::GetMousePos())) {
        m_NodeContentHovered = true;
    }
    if (nodeControlState.hovered ||
        nodeControlState.active ||
        nodeControlState.edited ||
        nodeControlState.popupOpen ||
        (anyPopupOpen && refreshedLayout.contentUsedRect.Contains(ImGui::GetMousePos()))) {
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

void EditorNodeGraphUI::RenderLinks(const EditorNodeGraph::Graph& graph) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(m_ActiveEditor);
    const float thicknessScale = LinkThicknessScaleFromZoom(m_Zoom);
    const EditorNodeGraph::Link hoveredLink = (graphStyle.enabled && IsGraphCanvasHovered())
        ? FindLinkAt(graph, ToGraphVec2(ImGui::GetMousePos()))
        : EditorNodeGraph::Link{};
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        const EditorNodeGraph::Node* from = graph.FindNode(link.fromNodeId);
        const EditorNodeGraph::Node* to = graph.FindNode(link.toNodeId);
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

        if (graphStyle.enabled) {
            const float laneOffset = ChannelLaneOffset(channel, m_Zoom);
            p1.y += laneOffset;
            p2.y += laneOffset;
            const float handle = LinkBezierHandle(p1, p2);
            const ImVec2 c1(p1.x + handle, p1.y);
            const ImVec2 c2(p2.x - handle, p2.y);
            ImVec4 linkColor = LinkColorVec(visualStyle, graphStyle);
            if (selected) {
                linkColor = WithAlpha(BlendColor(linkColor, graphStyle.text, 0.28f), 0.98f);
            } else if (hovered) {
                linkColor = WithAlpha(BlendColor(linkColor, graphStyle.text, 0.16f), 0.94f);
            }

            DrawBezierLinkStroke(
                drawList,
                p1,
                c1,
                c2,
                p2,
                ColorWithAlpha(graphStyle.linkUnderlay, selected ? 0.78f : (hovered ? 0.64f : 0.48f)),
                (selected ? 7.0f : (hovered ? 5.8f : 4.6f)) * thicknessScale,
                visualStyle.dotted);
            if (selected || hovered) {
                DrawBezierLinkStroke(
                    drawList,
                    p1,
                    c1,
                    c2,
                    p2,
                    ColorWithAlpha(selected ? graphStyle.selected : graphStyle.spotlightHalo, selected ? 0.72f : 0.42f),
                    (selected ? 5.0f : 4.0f) * thicknessScale,
                    visualStyle.dotted);
            }
            DrawBezierLinkStroke(
                drawList,
                p1,
                c1,
                c2,
                p2,
                ImGui::ColorConvertFloat4ToU32(linkColor),
                (selected ? 3.4f : 2.35f) * thicknessScale,
                visualStyle.dotted);
            if (selected) {
                DrawBezierLinkStroke(
                    drawList,
                    p1,
                    c1,
                    c2,
                    p2,
                    ColorWithAlpha(graphStyle.text, 0.44f),
                    std::max(1.0f, 1.05f * thicknessScale),
                    visualStyle.dotted);
            }
            continue;
        }

        const float handle = LinkBezierHandle(p1, p2);
        DrawBezierLinkStroke(
            drawList,
            p1,
            ImVec2(p1.x + handle, p1.y),
            ImVec2(p2.x - handle, p2.y),
            p2,
            LinkColorClassic(visualStyle, selected),
            (selected ? 4.5f : 3.0f) * thicknessScale,
            visualStyle.dotted);
    }
}

static bool IsPointInRect(const EditorNodeGraph::Vec2& pt, const EditorNodeGraph::Vec2& min, const EditorNodeGraph::Vec2& max) {
    return pt.x >= min.x && pt.x <= max.x && pt.y >= min.y && pt.y <= max.y;
}

void EditorNodeGraphUI::RenderGroups(EditorModule* editor, EditorNodeGraph::Graph& graph) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    auto& groups = graph.GetGroups();

    for (auto& group : groups) {
        ImVec2 minPos = ToImVec2(GraphToScreen(group.position));
        ImVec2 maxPos = ToImVec2(GraphToScreen({group.position.x + group.size.x, group.position.y + group.size.y}));

        float rounding = 8.0f * m_Zoom;

        bool isHovered = (m_HoveredGroupId == group.id);
        bool isEditing = (m_EditingGroupId == group.id);
        bool isDragged = (m_DragGroupId == group.id);
        bool isResized = (m_ResizingGroupId == group.id);

        ImU32 bgColor;
        ImU32 borderColor;
        ImU32 headerColor;

        if (graphStyle.enabled) {
            const float activeBoost = (isDragged || isResized) ? 0.22f : (isHovered ? 0.12f : 0.0f);
            bgColor = ColorWithAlpha(graphStyle.groupFill, graphStyle.groupFill.w + activeBoost);
            borderColor = ColorWithAlpha(isDragged || isResized ? graphStyle.selected : graphStyle.groupBorder, (isDragged || isResized) ? 0.92f : (isHovered ? 0.70f : graphStyle.groupBorder.w));
            headerColor = ColorWithAlpha(graphStyle.groupHeader, graphStyle.groupHeader.w + activeBoost);
        } else if (isDragged || isResized) {
            bgColor = IM_COL32(24, 30, 48, 140);
            borderColor = IM_COL32(90, 160, 255, 230);
            headerColor = IM_COL32(42, 54, 80, 240);
        } else {
            bgColor = IM_COL32(18, 22, 33, 90);
            borderColor = IM_COL32(66, 120, 180, 120);
            headerColor = IM_COL32(32, 40, 56, 190);
        }

        // Draw background
        drawList->AddRectFilled(minPos, maxPos, bgColor, rounding);

        // Draw header bar (28.0f graph units height)
        ImVec2 headerMin = minPos;
        ImVec2 headerMax = ImVec2(maxPos.x, minPos.y + 28.0f * m_Zoom);
        drawList->AddRectFilled(headerMin, headerMax, headerColor, rounding, ImDrawFlags_RoundCornersTop);

        // Draw border
        drawList->AddRect(minPos, maxPos, borderColor, rounding, 0, std::max(1.0f, 2.0f * m_Zoom));

        // Draw resize corner
        if (isHovered || isResized) {
            drawList->AddTriangleFilled(
                ImVec2(maxPos.x - 4.0f * m_Zoom, maxPos.y - 12.0f * m_Zoom),
                ImVec2(maxPos.x - 12.0f * m_Zoom, maxPos.y - 4.0f * m_Zoom),
                ImVec2(maxPos.x - 4.0f * m_Zoom, maxPos.y - 4.0f * m_Zoom),
                borderColor);
        }

        // Draw header title or rename box
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
                graphStyle.enabled ? ColorWithAlpha(graphStyle.text, 0.86f) : IM_COL32(255, 255, 255, 220),
                group.title.c_str());
        }
    }
}

void EditorNodeGraphUI::RenderInteraction(EditorModule* editor, const EditorNodeGraph::Graph& graph) {
    if (m_EditingGroupId > 0) {
        m_GraphInteractionBlocked = true;
        m_MouseOwner = GraphMouseOwner::Popup;
        m_BoxSelecting = false;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            m_EditingGroupId = -1;
        }
        return;
    }

    const bool graphHovered = IsGraphCanvasHovered();
    const EditorNodeGraph::Vec2 mouse = ToGraphVec2(ImGui::GetMousePos());
    const SocketHit hoveredInput = graphHovered ? FindInputPinAt(graph, mouse) : SocketHit{};
    const SocketHit hoveredOutput = graphHovered ? FindOutputPinAt(graph, mouse) : SocketHit{};
    m_HoveredInputNodeId = hoveredInput.nodeId;
    m_HoveredInputSocketId = hoveredInput.socketId;
    m_HoveredOutputNodeId = hoveredOutput.nodeId;
    m_HoveredOutputSocketId = hoveredOutput.socketId;
    const int hoveredNodeId = graphHovered ? FindNodeAt(graph, mouse) : -1;
    const EditorNodeGraph::Link hoveredLink = graphHovered ? FindLinkAt(graph, mouse) : EditorNodeGraph::Link{};
    const GraphStyleTokens graphStyle = BuildGraphStyleTokens(editor);
    const bool additiveSelect = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    const bool graphWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        m_DebugInteractionOverlay = !m_DebugInteractionOverlay;
    }

    const bool plainGPressed =
        editor->CanConsumeEditorCommandKeys() &&
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
            CancelChannelSplitConfirm();
            if (editor->SplitLayerNodeIntoChannels(confirmNodeId)) {
                m_StatusMessage = "Channel split created.";
            } else {
                m_StatusMessage = "Channel split failed.";
            }
            return;
        }
    } else if (plainGPressed &&
               graphWindowFocused &&
               !anyPopupOpen &&
               !m_NodeBrowserOpen &&
               selectedNodeIds.size() == 1 &&
               CanOpenChannelSplitConfirm(graph, selectedNodeIds.front())) {
        m_ChannelSplitConfirmNodeId = selectedNodeIds.front();
        m_ChannelSplitConfirmStartTime = ImGui::GetTime();
        m_ChannelSplitConfirmRect = {};
        return;
    }

    if (m_NodeBrowserOpen) {
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
                    
                    // Shift contained nodes
                    for (auto& node : editor->GetNodeGraph().GetNodes()) {
                        if (node.position.x >= group->position.x &&
                            node.position.x <= group->position.x + group->size.x &&
                            node.position.y >= group->position.y &&
                            node.position.y <= group->position.y + group->size.y) {
                            
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

    if (graphHovered && !anyPopupOpen && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        editor->CancelGraphAutoFocusTracking();
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_Pan.x += delta.x;
        m_Pan.y += delta.y;
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
        ImGui::OpenPopup("EditorNodeGraphContextMenu");
    }

    if (m_DragOutputNodeId > 0) {
        const EditorNodeGraph::Node* from = graph.FindNode(m_DragOutputNodeId);
        if (from) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const bool hoveredInputConnectable = hoveredInput.IsValid() &&
                graph.CanConnectSockets(m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId);
            LinkVisualStyle visualStyle = hoveredInputConnectable
                ? ResolvePendingLinkVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId)
                : ResolvePendingLinkVisualStyle(graph, m_DragOutputNodeId, m_DragOutputSocketId, EditorNodeGraph::SocketDirection::Output);
            if (!GraphDottedMaskLinksEnabled(editor)) {
                visualStyle.dotted = false;
            }
            ImVec2 p1 = ToImVec2(OutputPinScreenPos(*from, m_DragOutputSocketId));
            ImVec2 p2 = ImGui::GetMousePos();
            const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
            p1.y += laneOffset;
            p2.y += laneOffset;
            const float handle = LinkBezierHandle(p1, p2);
            const ImU32 dragColor = graphStyle.enabled
                ? ImGui::ColorConvertFloat4ToU32(LinkColorVec(visualStyle, graphStyle))
                : LinkColorClassic(visualStyle, false);
            DrawBezierLinkStroke(
                drawList,
                p1,
                ImVec2(p1.x + handle, p1.y),
                ImVec2(p2.x - handle, p2.y),
                p2,
                dragColor,
                std::max(0.35f, 2.5f * NodeUiScaleFromZoom(m_Zoom)),
                visualStyle.dotted);
        }

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
        const EditorNodeGraph::Node* to = graph.FindNode(m_DragInputNodeId);
        if (to) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const bool hoveredOutputConnectable = hoveredOutput.IsValid() &&
                graph.CanConnectSockets(hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId);
            LinkVisualStyle visualStyle = hoveredOutputConnectable
                ? ResolvePendingLinkVisualStyle(graph, hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId)
                : ResolvePendingLinkVisualStyle(graph, m_DragInputNodeId, m_DragInputSocketId, EditorNodeGraph::SocketDirection::Input);
            if (!GraphDottedMaskLinksEnabled(editor)) {
                visualStyle.dotted = false;
            }
            ImVec2 p1 = ImGui::GetMousePos();
            ImVec2 p2 = ToImVec2(InputPinScreenPos(*to, m_DragInputSocketId));
            const float laneOffset = ChannelLaneOffset(visualStyle.channel, m_Zoom);
            p1.y += laneOffset;
            p2.y += laneOffset;
            const float handle = LinkBezierHandle(p1, p2);
            const ImU32 dragColor = graphStyle.enabled
                ? ImGui::ColorConvertFloat4ToU32(LinkColorVec(visualStyle, graphStyle))
                : LinkColorClassic(visualStyle, false);
            DrawBezierLinkStroke(
                drawList,
                p1,
                ImVec2(p1.x + handle, p1.y),
                ImVec2(p2.x - handle, p2.y),
                p2,
                dragColor,
                std::max(0.35f, 2.5f * NodeUiScaleFromZoom(m_Zoom)),
                visualStyle.dotted);
        }

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
        if (node && node->kind == EditorNodeGraph::NodeKind::HdrMerge) {
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
          editor->GetNodeGraph().FindNode(hoveredNodeId)->kind == EditorNodeGraph::NodeKind::HdrMerge)) &&
        hoveredNodeId > 0 &&
        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(hoveredNodeId);
        if (node) {
            if (node->kind == EditorNodeGraph::NodeKind::Output) {
                return;
            }
            TouchNodeFront(node->id);
            if (HasDedicatedComplexEditor(editor, *node)) {
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
        !m_NodeBrowserOpen &&
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
        !m_NodeBrowserOpen) {
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            CopySelectedNodes(editor);
        }
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            PasteNodes(editor);
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
    const char* message = "Channel Split?";
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
    GraphMouseOwner owner) const {
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

#include "ThirdParty/stb_image.h"
#include "Editor/LayerRegistry.h"

namespace {

bool InsertNewNodeOnExistingLink(
    EditorNodeGraphUI* ui,
    EditorModule* editor,
    const EditorNodeGraph::Link& link,
    int newNodeId);

nlohmann::json SerializeMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings) {
    return {
        { "value", settings.value },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "scale", settings.scale },
        { "centerX", settings.centerX },
        { "centerY", settings.centerY },
        { "radius", settings.radius },
        { "feather", settings.feather },
        { "invert", settings.invert }
    };
}

EditorNodeGraph::MaskGeneratorSettings DeserializeMaskSettings(const nlohmann::json& value) {
    EditorNodeGraph::MaskGeneratorSettings settings;
    if (!value.is_object()) return settings;
    settings.value = value.value("value", settings.value);
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.scale = value.value("scale", settings.scale);
    settings.centerX = value.value("centerX", settings.centerX);
    settings.centerY = value.value("centerY", settings.centerY);
    settings.radius = value.value("radius", settings.radius);
    settings.feather = value.value("feather", settings.feather);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings) {
    return {
        { "blackPoint", settings.blackPoint },
        { "whitePoint", settings.whitePoint },
        { "gamma", settings.gamma },
        { "threshold", settings.threshold },
        { "softness", settings.softness },
        { "invert", settings.invert }
    };
}

EditorNodeGraph::MaskUtilitySettings DeserializeMaskUtilitySettings(const nlohmann::json& value) {
    EditorNodeGraph::MaskUtilitySettings settings;
    if (!value.is_object()) return settings;
    settings.blackPoint = value.value("blackPoint", settings.blackPoint);
    settings.whitePoint = value.value("whitePoint", settings.whitePoint);
    settings.gamma = value.value("gamma", settings.gamma);
    settings.threshold = value.value("threshold", settings.threshold);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    return {
        { "low", settings.low },
        { "high", settings.high },
        { "softness", settings.softness },
        { "invert", settings.invert }
    };
}

EditorNodeGraph::ImageToMaskSettings DeserializeImageToMaskSettings(const nlohmann::json& value) {
    EditorNodeGraph::ImageToMaskSettings settings;
    if (!value.is_object()) return settings;
    settings.low = value.value("low", settings.low);
    settings.high = value.value("high", settings.high);
    settings.softness = value.value("softness", settings.softness);
    settings.invert = value.value("invert", settings.invert);
    return settings;
}

nlohmann::json SerializeImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings) {
    return {
        { "colorA", { settings.colorA[0], settings.colorA[1], settings.colorA[2], settings.colorA[3] } },
        { "colorB", { settings.colorB[0], settings.colorB[1], settings.colorB[2], settings.colorB[3] } },
        { "angle", settings.angle },
        { "offset", settings.offset },
        { "text", settings.text },
        { "fontSize", settings.fontSize }
    };
}

EditorNodeGraph::ImageGeneratorSettings DeserializeImageGeneratorSettings(const nlohmann::json& value) {
    EditorNodeGraph::ImageGeneratorSettings settings;
    if (!value.is_object()) return settings;
    const nlohmann::json colorA = value.value("colorA", nlohmann::json::array());
    const nlohmann::json colorB = value.value("colorB", nlohmann::json::array());
    for (int i = 0; i < 4; ++i) {
        if (colorA.is_array() && static_cast<int>(colorA.size()) > i) settings.colorA[i] = colorA[i].get<float>();
        if (colorB.is_array() && static_cast<int>(colorB.size()) > i) settings.colorB[i] = colorB[i].get<float>();
    }
    settings.angle = value.value("angle", settings.angle);
    settings.offset = value.value("offset", settings.offset);
    settings.text = value.value("text", settings.text);
    settings.fontSize = value.value("fontSize", settings.fontSize);
    return settings;
}

nlohmann::json SerializeRawDetailFusionSettings(const Raw::RawDetailFusionSettings& settings) {
    return {
        { "mode", static_cast<int>(settings.mode) },
        { "debugView", static_cast<int>(settings.debugView) },
        { "autoSafetyEnabled", settings.autoSafetyEnabled },
        { "overrideMinEv", settings.overrideMinEv },
        { "overrideMaxEv", settings.overrideMaxEv },
        { "overrideBaseEv", settings.overrideBaseEv },
        { "overrideNoiseProtection", settings.overrideNoiseProtection },
        { "overrideHighlightProtection", settings.overrideHighlightProtection },
        { "overrideShadowLiftLimit", settings.overrideShadowLiftLimit },
        { "overrideWellExposedTarget", settings.overrideWellExposedTarget },
        { "minEvBias", settings.minEvBias },
        { "maxEvBias", settings.maxEvBias },
        { "baseEvBias", settings.baseEvBias },
        { "noiseProtectionBias", settings.noiseProtectionBias },
        { "highlightProtectionBias", settings.highlightProtectionBias },
        { "shadowLiftLimitBias", settings.shadowLiftLimitBias },
        { "wellExposedTargetBias", settings.wellExposedTargetBias },
        { "minEv", settings.minEv },
        { "maxEv", settings.maxEv },
        { "baseEv", settings.baseEv },
        { "strength", settings.strength },
        { "sampleCount", settings.sampleCount },
        { "baseRadiusPercent", settings.baseRadiusPercent },
        { "highlightProtection", settings.highlightProtection },
        { "shadowLiftLimit", settings.shadowLiftLimit },
        { "noiseProtection", settings.noiseProtection },
        { "detailWeight", settings.detailWeight },
        { "wellExposedTarget", settings.wellExposedTarget },
        { "smoothGradientProtection", settings.smoothGradientProtection },
        { "textureSensitivity", settings.textureSensitivity },
        { "skyBias", settings.skyBias },
        { "invertMask", settings.invertMask },
        { "maskBlackPoint", settings.maskBlackPoint },
        { "maskWhitePoint", settings.maskWhitePoint },
        { "maskGamma", settings.maskGamma },
        { "smoothnessRadius", settings.smoothnessRadius },
        { "smoothAreaRadius", settings.smoothAreaRadius },
        { "edgeAwareness", settings.edgeAwareness },
        { "haloGuard", settings.haloGuard },
        { "maskDebandDither", settings.maskDebandDither },
        { "manualBlend", settings.manualBlend }
    };
}

Raw::RawDetailFusionSettings DeserializeRawDetailFusionSettings(const nlohmann::json& value) {
    Raw::RawDetailFusionSettings settings;
    if (!value.is_object()) return settings;
    settings.mode = static_cast<Raw::RawDetailFusionMode>(std::clamp(value.value("mode", static_cast<int>(settings.mode)), 0, 2));
    settings.debugView = static_cast<Raw::RawDetailFusionDebugView>(std::clamp(value.value("debugView", static_cast<int>(settings.debugView)), 0, 14));
    settings.autoSafetyEnabled = value.value("autoSafetyEnabled", settings.autoSafetyEnabled);
    settings.overrideMinEv = value.value("overrideMinEv", settings.overrideMinEv);
    settings.overrideMaxEv = value.value("overrideMaxEv", settings.overrideMaxEv);
    settings.overrideBaseEv = value.value("overrideBaseEv", settings.overrideBaseEv);
    settings.overrideNoiseProtection = value.value("overrideNoiseProtection", settings.overrideNoiseProtection);
    settings.overrideHighlightProtection = value.value("overrideHighlightProtection", settings.overrideHighlightProtection);
    settings.overrideShadowLiftLimit = value.value("overrideShadowLiftLimit", settings.overrideShadowLiftLimit);
    settings.overrideWellExposedTarget = value.value("overrideWellExposedTarget", settings.overrideWellExposedTarget);
    settings.minEvBias = value.value("minEvBias", settings.minEvBias);
    settings.maxEvBias = value.value("maxEvBias", settings.maxEvBias);
    settings.baseEvBias = value.value("baseEvBias", settings.baseEvBias);
    settings.noiseProtectionBias = value.value("noiseProtectionBias", settings.noiseProtectionBias);
    settings.highlightProtectionBias = value.value("highlightProtectionBias", settings.highlightProtectionBias);
    settings.shadowLiftLimitBias = value.value("shadowLiftLimitBias", settings.shadowLiftLimitBias);
    settings.wellExposedTargetBias = value.value("wellExposedTargetBias", settings.wellExposedTargetBias);
    settings.minEv = value.value("minEv", settings.minEv);
    settings.maxEv = value.value("maxEv", settings.maxEv);
    settings.baseEv = value.value("baseEv", settings.baseEv);
    settings.strength = value.value("strength", settings.strength);
    settings.sampleCount = value.value("sampleCount", settings.sampleCount);
    settings.baseRadiusPercent = value.value("baseRadiusPercent", settings.baseRadiusPercent);
    settings.highlightProtection = value.value("highlightProtection", settings.highlightProtection);
    settings.shadowLiftLimit = value.value("shadowLiftLimit", settings.shadowLiftLimit);
    settings.noiseProtection = value.value("noiseProtection", settings.noiseProtection);
    settings.detailWeight = value.value("detailWeight", settings.detailWeight);
    settings.wellExposedTarget = value.value("wellExposedTarget", settings.wellExposedTarget);
    settings.smoothGradientProtection = value.value("smoothGradientProtection", settings.smoothGradientProtection);
    settings.textureSensitivity = value.value("textureSensitivity", settings.textureSensitivity);
    settings.skyBias = value.value("skyBias", settings.skyBias);
    settings.invertMask = value.value("invertMask", settings.invertMask);
    settings.maskBlackPoint = value.value("maskBlackPoint", settings.maskBlackPoint);
    settings.maskWhitePoint = value.value("maskWhitePoint", settings.maskWhitePoint);
    settings.maskGamma = value.value("maskGamma", settings.maskGamma);
    settings.smoothnessRadius = value.value("smoothnessRadius", settings.smoothnessRadius);
    settings.smoothAreaRadius = value.value("smoothAreaRadius", settings.smoothAreaRadius);
    settings.edgeAwareness = value.value("edgeAwareness", settings.edgeAwareness);
    settings.haloGuard = value.value("haloGuard", settings.haloGuard);
    settings.maskDebandDither = value.value("maskDebandDither", settings.maskDebandDither);
    settings.manualBlend = value.value("manualBlend", settings.manualBlend);
    settings.minEv = std::clamp(settings.minEv, -2.5f, 0.5f);
    settings.maxEv = std::clamp(settings.maxEv, std::max(settings.minEv + 0.01f, 0.25f), 2.5f);
    settings.baseEv = std::clamp(settings.baseEv, -1.0f, 1.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.sampleCount = std::clamp(settings.sampleCount, 3, 33);
    settings.baseRadiusPercent = std::clamp(settings.baseRadiusPercent, 0.002f, 0.030f);
    settings.highlightProtection = std::clamp(settings.highlightProtection, 0.0f, 1.0f);
    settings.shadowLiftLimit = std::clamp(settings.shadowLiftLimit, 0.0f, 1.0f);
    settings.noiseProtection = std::clamp(settings.noiseProtection, 0.0f, 1.0f);
    settings.detailWeight = std::clamp(settings.detailWeight, 0.0f, 1.0f);
    settings.wellExposedTarget = std::clamp(settings.wellExposedTarget, 0.10f, 0.55f);
    settings.smoothGradientProtection = std::clamp(settings.smoothGradientProtection, 0.0f, 1.0f);
    settings.textureSensitivity = std::clamp(settings.textureSensitivity, 0.0f, 1.0f);
    settings.skyBias = std::clamp(settings.skyBias, 0.0f, 1.0f);
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.smoothnessRadius = std::clamp(settings.smoothnessRadius, 0, 16);
    settings.smoothAreaRadius = std::clamp(settings.smoothAreaRadius, 0, 32);
    settings.edgeAwareness = std::clamp(settings.edgeAwareness, 0.0f, 1.0f);
    settings.haloGuard = std::clamp(settings.haloGuard, 0.0f, 1.0f);
    settings.maskDebandDither = std::clamp(settings.maskDebandDither, 0.0f, 1.0f);
    settings.manualBlend = std::clamp(settings.manualBlend, 0.0f, 1.0f);
    return settings;
}

bool DecodePngBytesClipboard(const std::vector<unsigned char>& pngBytes, EditorNodeGraph::ImagePayload& payload) {
    if (pngBytes.empty()) {
        return false;
    }
    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(pngBytes.data(), static_cast<int>(pngBytes.size()), &width, &height, &channels, 4);
    if (!pixels || width <= 0 || height <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    payload.pngBytes = pngBytes;
    payload.pixels.assign(pixels, pixels + (width * height * 4));
    payload.width = width;
    payload.height = height;
    payload.channels = 4;
    payload.originalChannels = channels;
    stbi_image_free(pixels);
    return true;
}

EditorNodeGraph::Node MakeTreeOnlyNode(EditorNodeGraph::Node node) {
    node.image = {};
    node.rawSource = {};
    node.rawNeuralDenoise = {};
    node.rawDevelop = {};
    node.rawDetailAutoMask = {};
    node.rawDetailFusion = {};
    node.hdrMerge = {};
    node.maskSettings = {};
    node.maskUtilitySettings = {};
    node.imageToMaskSettings = {};
    node.imageGeneratorSettings = {};
    node.mixBlendMode = EditorNodeGraph::MixBlendMode::Normal;
    node.mixFactor = 0.5f;
    node.dataMathMode = EditorNodeGraph::DataMathMode::Clamp;
    node.dataMathSettings = {};
    node.outputEnabled = true;
    return node;
}

bool GroupIntersectsSelection(
    const EditorNodeGraph::NodeGroup& group,
    const EditorNodeGraph::Graph& graph,
    const std::unordered_set<int>& selectedNodeIds) {
    const float groupMinX = group.position.x;
    const float groupMinY = group.position.y;
    const float groupMaxX = group.position.x + group.size.x;
    const float groupMaxY = group.position.y + group.size.y;

    for (int nodeId : selectedNodeIds) {
        const EditorNodeGraph::Node* node = graph.FindNode(nodeId);
        if (!node) {
            continue;
        }
        if (node->position.x >= groupMinX && node->position.x <= groupMaxX &&
            node->position.y >= groupMinY && node->position.y <= groupMaxY) {
            return true;
        }
    }
    return false;
}

nlohmann::json BuildClipboardPayload(EditorModule* editor, const std::vector<int>& nodeIds, bool includeState, bool wholeGraph) {
    nlohmann::json payload = nlohmann::json::object();
    payload["format"] = "stack-node-graph";
    payload["version"] = 1;
    payload["scope"] = wholeGraph ? "fullGraph" : "selection";
    payload["mode"] = includeState ? "tree+state" : "tree";

    if (!editor) {
        return payload;
    }

    const EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const auto& layers = editor->GetLayers();
    std::unordered_set<int> includedNodeIds(nodeIds.begin(), nodeIds.end());

    std::unordered_map<int, int> oldLayerIndexToNew;
    nlohmann::json layerArray = nlohmann::json::array();
    EditorNodeGraph::Graph exportGraph;
    exportGraph.Clear();
    exportGraph.SetNextNodeId(1);
    exportGraph.SetNextGroupId(1);

    int maxNodeId = 0;
    for (int nodeId : nodeIds) {
        const EditorNodeGraph::Node* sourceNode = graph.FindNode(nodeId);
        if (!sourceNode) {
            continue;
        }

        EditorNodeGraph::Node nodeCopy = includeState ? *sourceNode : MakeTreeOnlyNode(*sourceNode);
        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Layer) {
            int remappedLayerIndex = -1;
            const auto it = oldLayerIndexToNew.find(sourceNode->layerIndex);
            if (it != oldLayerIndexToNew.end()) {
                remappedLayerIndex = it->second;
            } else if (sourceNode->layerIndex >= 0 && sourceNode->layerIndex < static_cast<int>(layers.size())) {
                remappedLayerIndex = static_cast<int>(layerArray.size());
                oldLayerIndexToNew[sourceNode->layerIndex] = remappedLayerIndex;
                std::shared_ptr<LayerBase> layerToSerialize = layers[sourceNode->layerIndex];
                if (!includeState) {
                    const std::string typeId = layerToSerialize->Serialize().value("type", std::string());
                    std::shared_ptr<LayerBase> defaultLayer = LayerRegistry::CreateLayerFromTypeId(typeId);
                    if (defaultLayer) {
                        defaultLayer->InitializeGL();
                        layerToSerialize = defaultLayer;
                    }
                }
                layerArray.push_back(layerToSerialize ? layerToSerialize->Serialize() : nlohmann::json::object());
            }
            nodeCopy.layerIndex = remappedLayerIndex;
        }

        exportGraph.GetNodes().push_back(std::move(nodeCopy));
        maxNodeId = std::max(maxNodeId, sourceNode->id);
    }

    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        if (includedNodeIds.count(link.fromNodeId) && includedNodeIds.count(link.toNodeId)) {
            exportGraph.GetLinks().push_back(link);
        }
    }

    for (const EditorNodeGraph::NodeGroup& group : graph.GetGroups()) {
        if (wholeGraph || GroupIntersectsSelection(group, graph, includedNodeIds)) {
            exportGraph.GetGroups().push_back(group);
        }
    }

    exportGraph.SetNextNodeId(std::max(maxNodeId + 1, 1));
    exportGraph.SetNextGroupId(static_cast<int>(exportGraph.GetGroups().size()) + 1);
    if (!nodeIds.empty()) {
        exportGraph.SelectNode(nodeIds.front());
    }

    payload["payload"] = EditorNodeGraph::SerializeGraphPayload(layerArray, exportGraph);
    return payload;
}

std::string BuildGraphText(const nlohmann::json& clipboardPayload) {
    std::ostringstream stream;
    stream << "STACK_NODE_GRAPH 1\n";
    stream << "scope: " << clipboardPayload.value("scope", std::string("selection")) << "\n";
    stream << "mode: " << clipboardPayload.value("mode", std::string("tree+state")) << "\n\n";
    stream << clipboardPayload.dump(2);
    return stream.str();
}

bool ParseGraphText(const std::string& text, nlohmann::json& outPayload, std::string& outError) {
    if (text.rfind("STACK_NODE_GRAPH 1", 0) != 0) {
        outError = "Clipboard does not contain Stack graph text.";
        return false;
    }

    const std::size_t jsonStart = text.find('{');
    if (jsonStart == std::string::npos) {
        outError = "Graph text is missing its payload block.";
        return false;
    }

    outPayload = nlohmann::json::parse(text.substr(jsonStart), nullptr, false);
    if (outPayload.is_discarded() || !outPayload.is_object()) {
        outError = "Graph text payload could not be parsed.";
        return false;
    }

    if (!outPayload.contains("payload") || !outPayload["payload"].is_object()) {
        outError = "Graph text payload is missing nodeGraph data.";
        return false;
    }

    return true;
}

bool BuildImportedLayers(
    const nlohmann::json& layerArray,
    std::vector<std::shared_ptr<LayerBase>>& outLayers,
    std::vector<std::string>& warnings) {
    outLayers.clear();
    if (!layerArray.is_array()) {
        return true;
    }

    for (const auto& layerData : layerArray) {
        const std::string type = layerData.value("type", "");
        std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayerFromTypeId(type);
        if (!newLayer) {
            warnings.push_back(type.empty() ? "Skipped a layer with no type." : ("Skipped unsupported layer type: " + type));
            outLayers.push_back(nullptr);
            continue;
        }
        newLayer->InitializeGL();
        newLayer->Deserialize(layerData);
        outLayers.push_back(newLayer);
    }
    return true;
}

} // namespace

bool EditorNodeGraphUI::PasteClipboardPayload(EditorModule* editor, const nlohmann::json& clipboardPayload, std::string* outSummary) {
    if (!editor || !clipboardPayload.is_object()) {
        return false;
    }

    const nlohmann::json serializerPayload = clipboardPayload.value("payload", nlohmann::json::object());
    if (!serializerPayload.is_object() || !serializerPayload.contains("nodeGraph")) {
        if (outSummary) *outSummary = "Graph payload is missing node data.";
        return false;
    }
    const nlohmann::json graphJson = serializerPayload.value("nodeGraph", nlohmann::json::object());
    const nlohmann::json serializedNodesJson = graphJson.value("nodes", nlohmann::json::array());
    std::unordered_set<int> serializedNodeIds;
    if (serializedNodesJson.is_array()) {
        for (const nlohmann::json& item : serializedNodesJson) {
            if (!item.is_object()) {
                continue;
            }
            const int nodeId = item.value("id", 0);
            if (nodeId > 0) {
                serializedNodeIds.insert(nodeId);
            }
        }
    }

    const nlohmann::json layerArray = EditorNodeGraph::ExtractLayerArray(serializerPayload);
    std::vector<std::shared_ptr<LayerBase>> importedLayers;
    std::vector<std::string> warnings;
    BuildImportedLayers(layerArray, importedLayers, warnings);

    EditorNodeGraph::Graph tempGraph;
    EditorNodeGraph::DeserializeGraphPayload(serializerPayload, tempGraph, static_cast<int>(importedLayers.size()), {}, 0, 0, 0);

    std::vector<int> syntheticOutputNodeIds;
    for (const EditorNodeGraph::Node& node : tempGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Output &&
            serializedNodeIds.find(node.id) == serializedNodeIds.end()) {
            syntheticOutputNodeIds.push_back(node.id);
        }
    }
    for (int nodeId : syntheticOutputNodeIds) {
        tempGraph.RemoveNode(nodeId);
    }

    const auto& nodes = tempGraph.GetNodes();
    if (nodes.empty()) {
        if (outSummary) *outSummary = "Graph payload did not contain any nodes.";
        return false;
    }

    m_ClipboardPasteCount++;
    const float offsetX = m_ClipboardPasteCount * 40.0f;
    const float offsetY = m_ClipboardPasteCount * 40.0f;

    const ImVec2 mousePos = ImGui::GetMousePos();
    const bool mouseInsideCanvas =
        mousePos.x >= m_CanvasMin.x && mousePos.x <= m_CanvasMax.x &&
        mousePos.y >= m_CanvasMin.y && mousePos.y <= m_CanvasMax.y;
    const bool useCursorPos = mouseInsideCanvas || m_HasLastGraphMousePos;
    EditorNodeGraph::Vec2 cursorGraphPos = m_LastGraphMousePos;
    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    for (const EditorNodeGraph::Node& node : nodes) {
        minX = std::min(minX, node.position.x);
        minY = std::min(minY, node.position.y);
    }
    if (useCursorPos) {
        if (mouseInsideCanvas) {
            cursorGraphPos = ScreenToGraph(ToGraphVec2(mousePos));
            m_LastGraphMousePos = cursorGraphPos;
            m_HasLastGraphMousePos = true;
        }
    }

    auto& targetGraph = editor->GetNodeGraph();
    auto& targetLayers = editor->GetLayers();
    std::unordered_map<int, int> layerIndexMap;
    for (int i = 0; i < static_cast<int>(importedLayers.size()); ++i) {
        if (!importedLayers[i]) {
            continue;
        }
        layerIndexMap[i] = static_cast<int>(targetLayers.size());
        targetLayers.push_back(importedLayers[i]);
    }

    std::unordered_map<int, int> oldNodeIdToNew;
    std::vector<int> pastedNodeIds;
    int importedNodeCount = 0;
    for (const EditorNodeGraph::Node& sourceNode : nodes) {
        EditorNodeGraph::Node nodeCopy = sourceNode;
        nodeCopy.id = targetGraph.GetNextNodeId();
        targetGraph.SetNextNodeId(nodeCopy.id + 1);

        if (useCursorPos && minX != std::numeric_limits<float>::max()) {
            nodeCopy.position.x = cursorGraphPos.x + (sourceNode.position.x - minX);
            nodeCopy.position.y = cursorGraphPos.y + (sourceNode.position.y - minY);
        } else {
            nodeCopy.position.x = sourceNode.position.x + offsetX;
            nodeCopy.position.y = sourceNode.position.y + offsetY;
        }

        if (nodeCopy.kind == EditorNodeGraph::NodeKind::Layer) {
            const auto layerIt = layerIndexMap.find(sourceNode.layerIndex);
            if (layerIt == layerIndexMap.end()) {
                warnings.push_back("Skipped a layer node because its layer state could not be created.");
                continue;
            }
            nodeCopy.layerIndex = layerIt->second;
        }

        targetGraph.GetNodes().push_back(nodeCopy);
        oldNodeIdToNew[sourceNode.id] = nodeCopy.id;
        pastedNodeIds.push_back(nodeCopy.id);
        importedNodeCount++;
    }

    int importedLinkCount = 0;
    int skippedLinkCount = 0;
    for (const EditorNodeGraph::Link& link : tempGraph.GetLinks()) {
        const auto fromIt = oldNodeIdToNew.find(link.fromNodeId);
        const auto toIt = oldNodeIdToNew.find(link.toNodeId);
        if (fromIt == oldNodeIdToNew.end() || toIt == oldNodeIdToNew.end()) {
            skippedLinkCount++;
            continue;
        }
        std::string errorMessage;
        if (targetGraph.TryConnectSockets(fromIt->second, link.fromSocketId, toIt->second, link.toSocketId, &errorMessage)) {
            importedLinkCount++;
        } else {
            skippedLinkCount++;
            if (!errorMessage.empty()) {
                warnings.push_back(errorMessage);
            }
        }
    }

    int importedGroupCount = 0;
    for (const EditorNodeGraph::NodeGroup& group : tempGraph.GetGroups()) {
        EditorNodeGraph::Vec2 position = group.position;
        if (useCursorPos && minX != std::numeric_limits<float>::max()) {
            position.x = cursorGraphPos.x + (group.position.x - minX);
            position.y = cursorGraphPos.y + (group.position.y - minY);
        } else {
            position.x += offsetX;
            position.y += offsetY;
        }
        if (targetGraph.AddGroup(group.title, position, group.size)) {
            importedGroupCount++;
        }
    }

    editor->RefreshGraphLayerMetadata();
    targetGraph.ClearSelection();
    for (int nodeId : pastedNodeIds) {
        targetGraph.SelectNode(nodeId, true);
    }
    editor->MarkRenderDirty();

    std::ostringstream summary;
    summary << "Imported " << importedNodeCount << " nodes, " << importedLinkCount << " links, and " << importedGroupCount << " groups";
    if (skippedLinkCount > 0) {
        summary << "; skipped " << skippedLinkCount << " invalid links";
    }
    if (!warnings.empty()) {
        summary << ".";
    }
    if (outSummary) {
        *outSummary = summary.str();
    }
    return true;
}

namespace {

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

} // namespace

void EditorNodeGraphUI::CopySelectedNodes(EditorModule* editor) {
    if (!editor) {
        return;
    }

    m_ClipboardPasteCount = 0;
    const auto& selectedNodeIds = editor->GetNodeGraph().GetSelectedNodeIds();
    if (selectedNodeIds.empty()) {
        m_Clipboard = nlohmann::json::object();
        return;
    }

    m_Clipboard = BuildClipboardPayload(editor, selectedNodeIds, true, false);
}

void EditorNodeGraphUI::PasteNodes(EditorModule* editor) {
    if (!editor || m_Clipboard.empty()) {
        return;
    }
    std::string summary;
    if (PasteClipboardPayload(editor, m_Clipboard, &summary)) {
        m_StatusMessage = summary;
    } else if (!summary.empty()) {
        m_StatusMessage = summary;
    }
}

void EditorNodeGraphUI::CopyGraphInfo(EditorModule* editor, bool wholeGraph, bool includeState) {
    if (!editor) {
        return;
    }

    std::vector<int> nodeIds;
    if (wholeGraph) {
        for (const EditorNodeGraph::Node& node : editor->GetNodeGraph().GetNodes()) {
            nodeIds.push_back(node.id);
        }
    } else {
        nodeIds = editor->GetNodeGraph().GetSelectedNodeIds();
    }

    if (nodeIds.empty()) {
        m_StatusMessage = wholeGraph ? "Graph is empty." : "Select at least one node to copy graph info.";
        return;
    }

    nlohmann::json payload = BuildClipboardPayload(editor, nodeIds, includeState, wholeGraph);
    const std::string text = BuildGraphText(payload);
    ImGui::SetClipboardText(text.c_str());
    m_Clipboard = payload;
    m_ClipboardPasteCount = 0;
    m_StatusMessage = wholeGraph
        ? (includeState ? "Whole graph copied with state." : "Whole graph copied as tree only.")
        : (includeState ? "Selected graph copied with state." : "Selected graph copied as tree only.");
}

void EditorNodeGraphUI::PasteGraphInfo(EditorModule* editor) {
    if (!editor) {
        return;
    }

    const char* clipboardText = ImGui::GetClipboardText();
    if (!clipboardText || clipboardText[0] == '\0') {
        m_StatusMessage = "Clipboard is empty.";
        return;
    }

    nlohmann::json payload;
    std::string error;
    if (!ParseGraphText(clipboardText, payload, error)) {
        m_StatusMessage = error;
        return;
    }

    std::string summary;
    if (PasteClipboardPayload(editor, payload, &summary)) {
        m_Clipboard = payload;
        m_StatusMessage = summary;
    } else {
        m_StatusMessage = summary.empty() ? "Graph text could not be imported." : summary;
    }
}

void EditorNodeGraphUI::DuplicateSelectedNodes(EditorModule* editor) {
    if (!editor) return;
    CopySelectedNodes(editor);
    PasteNodes(editor);
}
