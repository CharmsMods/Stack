#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "App/settings/AppearanceTheme.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <imgui.h>

namespace {

using NodeBrowserEntry = EditorNodeGraphDefinitions::NodeCatalogEntry;

const std::vector<NodeBrowserEntry>& CachedNodeBrowserEntries() {
    static const std::vector<NodeBrowserEntry> entries = EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
    return entries;
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

std::string EntryKey(const NodeBrowserEntry& entry) {
    return std::to_string(static_cast<int>(entry.kind)) + ":" + std::to_string(entry.value) + ":" + entry.label;
}

std::string EllipsizeText(const std::string& text, float maxWidth) {
    if (text.empty() || maxWidth <= 4.0f) {
        return {};
    }
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }

    const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    std::string clipped = text;
    while (!clipped.empty()) {
        clipped.pop_back();
        const std::string candidate = clipped + ellipsis;
        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth - ellipsisWidth * 0.1f) {
            return candidate;
        }
    }
    return ellipsis;
}

float AnimateToward(float current, float target, float rate, float dt) {
    const float t = 1.0f - std::exp(-std::max(0.0f, rate) * std::max(0.0f, dt));
    return current + (target - current) * std::clamp(t, 0.0f, 1.0f);
}

ImU32 ScaleAlpha(ImU32 color, float alpha) {
    ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
    rgba.w *= std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(rgba);
}

ImVec4 BlendColor(const ImVec4& a, const ImVec4& b, float t) {
    const float clampedT = std::clamp(t, 0.0f, 1.0f);
    return ImVec4(
        a.x + (b.x - a.x) * clampedT,
        a.y + (b.y - a.y) * clampedT,
        a.z + (b.z - a.z) * clampedT,
        a.w + (b.w - a.w) * clampedT);
}

ImU32 ColorWithAlpha(ImVec4 color, float alpha) {
    color.w = std::clamp(alpha, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

StackAppearance::GraphVisualMode CurrentGraphVisualMode(EditorModule* editor) {
    const StackAppearance::AppearanceManager* appearance = editor ? editor->GetAppearance() : nullptr;
    return appearance ? appearance->GetGraphVisualMode() : StackAppearance::GraphVisualMode::BlackNodes;
}

ImVec2 FitImageRect(const ImVec2& boxSize, const ImVec2& sourceSize) {
    if (boxSize.x <= 0.0f || boxSize.y <= 0.0f || sourceSize.x <= 0.0f || sourceSize.y <= 0.0f) {
        return ImVec2(0.0f, 0.0f);
    }

    const float scale = std::min(boxSize.x / sourceSize.x, boxSize.y / sourceSize.y);
    return ImVec2(
        std::max(1.0f, std::floor(sourceSize.x * scale)),
        std::max(1.0f, std::floor(sourceSize.y * scale)));
}

void DrawClippedTextLine(
    ImDrawList* drawList,
    const ImVec2& min,
    const ImVec2& max,
    const char* text,
    ImU32 color) {
    if (!drawList || !text || text[0] == '\0' || max.x <= min.x || max.y <= min.y) {
        return;
    }

    drawList->PushClipRect(min, max, true);
    drawList->AddText(min, color, text);
    drawList->PopClipRect();
}

bool RenderBrowserCard(
    const NodeBrowserEntry& entry,
    unsigned int texture,
    const ImVec2& textureSize,
    const char* statusText,
    bool pending,
    bool fallback,
    bool showPlaceholder,
    float alpha,
    bool interactive,
    ImU32 cardHoverFill,
    ImU32 titleColor,
    ImU32 footerColor,
    ImU32 overlayColor) {
    if (alpha <= 0.01f) {
        return false;
    }

    ImGui::PushID(EntryKey(entry).c_str());
    constexpr float kCardWidth = 138.0f;
    constexpr float kCardHeight = 154.0f;
    constexpr float kImageHeight = 104.0f;
    const ImVec2 size(kCardWidth, kCardHeight);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + size.x, min.y + size.y);
    ImGui::InvisibleButton("card", size);
    const bool hovered = interactive && ImGui::IsItemHovered();
    const bool pressed = interactive && ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const float drawAlpha = std::clamp(alpha, 0.0f, 1.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 imageMin(min.x + 6.0f, min.y + 4.0f);
    const ImVec2 imageMax(max.x - 6.0f, min.y + 4.0f + kImageHeight);

    if (texture != 0 && textureSize.x > 0.0f && textureSize.y > 0.0f) {
        const ImVec2 fitted = FitImageRect(
            ImVec2(imageMax.x - imageMin.x, imageMax.y - imageMin.y),
            textureSize);
        const ImVec2 imageDrawMin(
            imageMin.x + ((imageMax.x - imageMin.x) - fitted.x) * 0.5f,
            imageMin.y + ((imageMax.y - imageMin.y) - fitted.y) * 0.5f);
        const ImVec2 imageDrawMax(imageDrawMin.x + fitted.x, imageDrawMin.y + fitted.y);
        drawList->AddImage(
            (ImTextureID)(intptr_t)texture,
            imageDrawMin,
            imageDrawMax,
            ImVec2(0.0f, 1.0f),
            ImVec2(1.0f, 0.0f));
    } else if (showPlaceholder) {
        const char* placeholder = pending ? "Loading" : (fallback ? "Styled Card" : "Preview");
        const ImVec2 placeholderSize = ImGui::CalcTextSize(placeholder);
        drawList->AddText(
            ImVec2(
                imageMin.x + ((imageMax.x - imageMin.x) - placeholderSize.x) * 0.5f,
                imageMin.y + ((imageMax.y - imageMin.y) - placeholderSize.y) * 0.5f),
            overlayColor,
            placeholder);
    }

    if (pending) {
        drawList->AddRectFilled(imageMin, imageMax, ScaleAlpha(overlayColor, 0.10f * drawAlpha), 0.0f);
    }

    const std::string clippedTitle = EllipsizeText(entry.label, size.x - 12.0f);
    const ImVec2 titleMin(min.x + 6.0f, imageMax.y + 8.0f);
    const ImVec2 titleMax(max.x - 12.0f, titleMin.y + ImGui::GetTextLineHeight());
    DrawClippedTextLine(drawList, titleMin, titleMax, clippedTitle.c_str(), hovered ? titleColor : footerColor);

    if (statusText && statusText[0] != '\0') {
        const std::string clippedStatus = EllipsizeText(statusText, size.x - 12.0f);
        const ImVec2 footerMin(min.x + 6.0f, max.y - 20.0f);
        const ImVec2 footerMax(max.x - 12.0f, max.y - 8.0f);
        DrawClippedTextLine(drawList, footerMin, footerMax, clippedStatus.c_str(), footerColor);
    }

    if (hovered) {
        drawList->AddLine(
            ImVec2(titleMin.x, max.y - 3.0f),
            ImVec2(max.x - 10.0f, max.y - 3.0f),
            ScaleAlpha(cardHoverFill, 0.65f),
            1.0f);
    }

    ImGui::PopID();
    return pressed;
}


bool PrototypeHasCompatibleInput(
    EditorNodeGraph::Graph& compatibilityGraph,
    int& nextPrototypeNodeId,
    int fromNodeId,
    const std::string& fromSocketId,
    const NodeBrowserEntry& entry) {
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    EditorNodeGraph::Node testNode = prototype;
    testNode.id = nextPrototypeNodeId++;
    compatibilityGraph.GetNodes().push_back(testNode);

    bool compatible = false;
    for (const EditorNodeGraph::SocketDefinition& socket : compatibilityGraph.GetSockets(testNode, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (compatibilityGraph.CanConnectSockets(fromNodeId, fromSocketId, testNode.id, socket.id)) {
            compatible = true;
            break;
        }
    }
    compatibilityGraph.GetNodes().pop_back();
    return compatible;
}

bool PrototypeHasCompatibleOutput(
    EditorNodeGraph::Graph& compatibilityGraph,
    int& nextPrototypeNodeId,
    const NodeBrowserEntry& entry,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* to = compatibilityGraph.FindNode(toNodeId);
    if (!to) {
        return false;
    }
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    EditorNodeGraph::Node testNode = prototype;
    testNode.id = nextPrototypeNodeId++;
    compatibilityGraph.GetNodes().push_back(testNode);

    bool compatible = false;
    for (const EditorNodeGraph::SocketDefinition& socket : compatibilityGraph.GetSockets(testNode, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (compatibilityGraph.CanConnectSockets(testNode.id, socket.id, toNodeId, toSocketId)) {
            compatible = true;
            break;
        }
    }
    compatibilityGraph.GetNodes().pop_back();
    return compatible;
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
        case EditorNodeGraph::NodeKind::Lut:
            editor->AddLutNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            editor->AddRawNeuralDenoiseNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::Composite:
            break;
    }
    const std::vector<int>& selected = editor->GetNodeGraph().GetSelectedNodeIds();
    return selected.empty() ? -1 : selected.back();
}

} // namespace

void EditorNodeGraphUI::OpenNodeBrowser(NodeBrowserMode mode, const EditorNodeGraph::Vec2& graphPos) {
    m_NodeBrowserMode = mode;
    m_NodeBrowserGraphPos = graphPos;
    m_NodeBrowserScreenPos = GraphToScreen(graphPos);
    m_NodeBrowserOpen = true;
    m_NodeBrowserFocusSearch = true;
    m_NodeBrowserOpenedAt = ImGui::GetTime();
    m_NodeBrowserLastFrameTime = m_NodeBrowserOpenedAt;
    m_NodeBrowserSearchBuffer[0] = '\0';
    m_NodeBrowserEntryAlpha.clear();
}

void EditorNodeGraphUI::CloseNodeBrowser() {
    if (m_NodeBrowserOpen && m_PushedSourceNodeId > 0) {
        if (m_ActiveEditor) {
            EditorNodeGraph::Graph& graph = m_ActiveEditor->GetNodeGraph();
            for (int id : m_PushedNodeIds) {
                if (EditorNodeGraph::Node* dsNode = graph.FindNode(id)) {
                    dsNode->position.x -= m_PushDistance;
                    RefreshNodeLayoutCache(graph, *dsNode);
                }
            }
        }
    }
    m_PushedSourceNodeId = -1;
    m_PushDistance = 0.0f;
    m_PushedNodeIds.clear();

    m_NodeBrowserOpen = false;
    m_NodeBrowserFocusSearch = false;
    m_NodeBrowserDragFromNodeId = -1;
    m_NodeBrowserDragFromSocketId.clear();
    m_NodeBrowserDragToNodeId = -1;
    m_NodeBrowserDragToSocketId.clear();
    m_NodeBrowserEntryAlpha.clear();
}

void EditorNodeGraphUI::RenderNodeBrowser(EditorModule* editor) {
    // Old popup window rendering removed in favor of RenderNodesPanelDrawer
}

void EditorNodeGraphUI::RenderNodesPanelDrawer(
    EditorModule* editor,
    float panelWidth,
    float paneHeight,
    const ImVec2& workspacePos) {
    
    if (!editor) {
        return;
    }

    const bool isOpen = m_NodeBrowserOpen;

    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(std::clamp(now - m_NodeBrowserLastFrameTime, 0.0, 0.05));
    m_NodeBrowserLastFrameTime = now;
    
    // Overall fade alpha based on open/closed state
    const float targetAlpha = isOpen ? 1.0f : 0.0f;
    static float currentAlpha = 0.0f;
    if (!isOpen && currentAlpha < 0.01f) {
        currentAlpha = 0.0f;
    } else {
        currentAlpha += (targetAlpha - currentAlpha) * dt * 15.0f;
    }
    const float alpha = std::clamp(currentAlpha, 0.0f, 1.0f);

    const EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    EditorNodeGraph::Graph compatibilityGraph = graph;
    int nextPrototypeNodeId = std::max(1, graph.GetNextNodeId() + 1000);
    const std::vector<NodeBrowserEntry>& entries = CachedNodeBrowserEntries();
    const std::string search = m_NodeBrowserSearchBuffer;
    std::vector<const NodeBrowserEntry*> filtered;
    filtered.reserve(entries.size());
    
    for (const NodeBrowserEntry& entry : entries) {
        const bool matchesSearch =
            ContainsCaseInsensitive(entry.label, search) ||
            ContainsCaseInsensitive(entry.category, search);
        const bool compatibleOutput =
            m_NodeBrowserMode != NodeBrowserMode::ConnectFromOutput ||
            PrototypeHasCompatibleInput(compatibilityGraph, nextPrototypeNodeId, m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId, entry);
        const bool compatibleInput =
            m_NodeBrowserMode != NodeBrowserMode::ConnectFromInput ||
            PrototypeHasCompatibleOutput(compatibilityGraph, nextPrototypeNodeId, entry, m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId);
        const bool visible = isOpen && matchesSearch && compatibleOutput && compatibleInput;
        const std::string key = EntryKey(entry);
        float& entryAlpha = m_NodeBrowserEntryAlpha[key];
        entryAlpha = AnimateToward(entryAlpha, visible ? 1.0f : 0.0f, visible ? 18.0f : 14.0f, dt);
        if (visible) {
            filtered.push_back(&entry);
        }
    }

    for (auto it = m_NodeBrowserEntryAlpha.begin(); it != m_NodeBrowserEntryAlpha.end();) {
        if (it->second <= 0.01f) {
            it = m_NodeBrowserEntryAlpha.erase(it);
        } else {
            ++it;
        }
    }

    if (isOpen) {
        editor->EnsureNodeBrowserThumbnailCatalog();
    }

    // 1. Sliding panel background & content drawing
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 panelMin = workspacePos;
    
    float gradientWidth = std::min(60.0f, panelWidth);
    float solidWidth = panelWidth - gradientWidth;
    
    ImVec4 workspaceColor = editor->GetWorkspaceBaseColor();
    const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
    const bool isLightBg = luminance >= 0.5f;
    const StackAppearance::GraphVisualMode graphMode = CurrentGraphVisualMode(editor);
    const bool spotlightGraph = graphMode == StackAppearance::GraphVisualMode::SpotlightPrototype;
    const bool blackNodeGraph = graphMode == StackAppearance::GraphVisualMode::BlackNodes;
    const bool customGraph = graphMode != StackAppearance::GraphVisualMode::Classic;
    const StackAppearance::AppearanceManager* appearance = editor->GetAppearance();
    const bool wallpaperSurfaces = appearance && appearance->GetBackgroundImageEnabled();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        appearance ? appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
    const ImVec4 themeText = appearance ? appearance->GetWorkingTheme().colors[ImGuiCol_Text] : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 themeMuted = appearance ? appearance->GetWorkingTheme().colors[ImGuiCol_TextDisabled] : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImVec4 themeAccent = appearance ? appearance->GetWorkingTheme().colors[ImGuiCol_CheckMark] : ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    const ImVec4 spotlightTint = isLightBg
        ? ImVec4(0.86f, 0.96f, 1.0f, 1.0f)
        : ImVec4(0.00f, 0.11f, 0.14f, 1.0f);
    const ImVec4 blackTint(0.01f, 0.02f, 0.03f, 1.0f);

    ImVec4 colBgOpaqueVec = spotlightGraph
        ? BlendColor(workspaceColor, spotlightTint, isLightBg ? 0.18f : 0.34f)
        : (blackNodeGraph
            ? BlendColor(workspaceColor, blackTint, isLightBg ? 0.90f : 0.58f)
            : workspaceColor);
    if (wallpaperSurfaces) {
        colBgOpaqueVec = surfacePalette.drawerSurface;
        colBgOpaqueVec.w *= alpha;
    } else {
        colBgOpaqueVec.w = (spotlightGraph
            ? (isLightBg ? 0.88f : 0.84f)
            : (blackNodeGraph ? 0.96f : (isLightBg ? 0.94f : 0.92f))) * alpha;
    }
    const ImU32 colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);

    ImVec4 colBgTransVec = wallpaperSurfaces ? colBgOpaqueVec : workspaceColor;
    colBgTransVec.w = 0.0f;
    const ImU32 colBgTrans = ImGui::ColorConvertFloat4ToU32(colBgTransVec);

    const ImU32 colTitleText = blackNodeGraph
        ? ColorWithAlpha(ImVec4(0.94f, 0.96f, 0.98f, 1.0f), alpha)
        : spotlightGraph
        ? ColorWithAlpha(themeText, alpha)
        : ScaleAlpha(isLightBg ? IM_COL32(18, 24, 30, 255) : IM_COL32(255, 255, 255, 255), alpha);
    const ImU32 colPassiveText = blackNodeGraph
        ? ColorWithAlpha(BlendColor(ImVec4(0.68f, 0.72f, 0.76f, 1.0f), themeAccent, 0.08f), 0.90f * alpha)
        : spotlightGraph
        ? ColorWithAlpha(BlendColor(themeMuted, themeAccent, 0.10f), 0.86f * alpha)
        : ScaleAlpha(isLightBg ? IM_COL32(80, 95, 105, 220) : IM_COL32(140, 160, 170, 200), alpha);
    const ImU32 colNormalText = blackNodeGraph
        ? ColorWithAlpha(ImVec4(0.88f, 0.91f, 0.94f, 1.0f), 0.96f * alpha)
        : spotlightGraph
        ? ColorWithAlpha(BlendColor(themeText, themeMuted, 0.14f), 0.92f * alpha)
        : ScaleAlpha(isLightBg ? IM_COL32(40, 50, 60, 220) : IM_COL32(200, 210, 220, 200), alpha);
    const ImU32 colHoveredHeader = blackNodeGraph
        ? ColorWithAlpha(BlendColor(themeAccent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.18f), 0.18f * alpha)
        : spotlightGraph
        ? ColorWithAlpha(BlendColor(themeAccent, spotlightTint, 0.16f), 0.18f * alpha)
        : ScaleAlpha(isLightBg ? IM_COL32(16, 110, 190, 28) : IM_COL32(92, 178, 255, 30), alpha);
    const ImU32 colActiveHeader = blackNodeGraph
        ? ColorWithAlpha(BlendColor(themeAccent, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), 0.24f), 0.28f * alpha)
        : spotlightGraph
        ? ColorWithAlpha(BlendColor(themeAccent, themeText, 0.12f), 0.26f * alpha)
        : ScaleAlpha(isLightBg ? IM_COL32(16, 110, 190, 48) : IM_COL32(92, 178, 255, 50), alpha);
    const ImU32 colCardHoverFill = wallpaperSurfaces
        ? ColorWithAlpha(surfacePalette.controlSurfaceHovered, 0.98f * alpha)
        : (blackNodeGraph
            ? ColorWithAlpha(BlendColor(colBgOpaqueVec, themeAccent, 0.12f), 0.98f * alpha)
            : spotlightGraph
                ? ColorWithAlpha(BlendColor(colBgOpaqueVec, themeAccent, 0.14f), 0.94f * alpha)
                : ScaleAlpha(isLightBg ? IM_COL32(46, 62, 72, 224) : IM_COL32(24, 40, 48, 236), alpha));
    const ImU32 colCardOverlay = ColorWithAlpha(BlendColor(themeAccent, themeText, 0.22f), 0.72f * alpha);

    // Solid part
    if (solidWidth > 0.0f) {
        drawList->AddRectFilled(panelMin, ImVec2(workspacePos.x + solidWidth, workspacePos.y + paneHeight), colBgOpaque);
    }
    // Gradient feathered blend part
    drawList->AddRectFilledMultiColor(
        ImVec2(workspacePos.x + solidWidth, workspacePos.y),
        ImVec2(workspacePos.x + panelWidth, workspacePos.y + paneHeight),
        colBgOpaque, colBgTrans, colBgTrans, colBgOpaque
    );
    if (customGraph && alpha > 0.01f && !wallpaperSurfaces) {
        drawList->AddLine(
            ImVec2(workspacePos.x + 1.0f, workspacePos.y),
            ImVec2(workspacePos.x + 1.0f, workspacePos.y + paneHeight),
            ColorWithAlpha(BlendColor(themeAccent, themeText, 0.18f), 0.22f * alpha),
            1.0f);
    }

    bool closeBrowser = false;
    const bool closeAllowed =
        isOpen &&
        (now - m_NodeBrowserOpenedAt) > 0.12 &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
    if (closeAllowed) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (mousePos.x < workspacePos.x || mousePos.x > workspacePos.x + panelWidth ||
                mousePos.y < workspacePos.y || mousePos.y > workspacePos.y + paneHeight) {
                closeBrowser = true;
            }
        }
    }

    float contentWidth = panelWidth - 40.0f; // breathing room on right for the feathered edge
    if (contentWidth > 1.0f) {
        ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + 16.0f, workspacePos.y + 28.0f));
        // Added ImGuiWindowFlags_NoNav to prevent blue outline selection focus on child window
        ImGui::BeginChild("NodesPanelDrawerChild", ImVec2(contentWidth, paneHeight - 56.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav);

        ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
        ImGui::TextUnformatted(m_NodeBrowserMode == NodeBrowserMode::GeneralAdd ? "ADD NODE" : "COMPATIBLE NODES");
        ImGui::PopStyleColor();
        
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        if (isOpen && m_NodeBrowserFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_NodeBrowserFocusSearch = false;
        }

        const char* prompt = (m_NodeBrowserMode == NodeBrowserMode::GeneralAdd)
            ? "Search nodes"
            : "Search compatible nodes";

        // Compact custom input box theme
        const ImU32 searchBg = wallpaperSurfaces
            ? ColorWithAlpha(surfacePalette.controlSurface, alpha)
            : (blackNodeGraph
                ? ColorWithAlpha(BlendColor(colBgOpaqueVec, themeAccent, 0.05f), 0.94f * alpha)
                : spotlightGraph
                    ? ColorWithAlpha(BlendColor(colBgOpaqueVec, themeAccent, 0.08f), 0.82f * alpha)
                    : IM_COL32(34, 43, 48, 235));
        const ImU32 searchBgHovered = wallpaperSurfaces
            ? ColorWithAlpha(surfacePalette.controlSurfaceHovered, alpha)
            : (blackNodeGraph
                ? ColorWithAlpha(BlendColor(colBgOpaqueVec, themeAccent, 0.10f), 0.98f * alpha)
                : spotlightGraph
                    ? ColorWithAlpha(BlendColor(colBgOpaqueVec, themeAccent, 0.14f), 0.90f * alpha)
                    : IM_COL32(44, 57, 63, 235));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, searchBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, searchBgHovered);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, searchBgHovered);
        ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, colPassiveText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, customGraph ? 1.0f : 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 6.0f));
        if (customGraph) {
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                wallpaperSurfaces
                    ? ColorWithAlpha(surfacePalette.border, alpha)
                    : ColorWithAlpha(BlendColor(themeAccent, themeText, 0.18f), 0.28f * alpha));
        }
        
        ImGui::PushItemWidth(-1.0f);
        ImGuiInputTextFlags searchFlags = isOpen ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::InputTextWithHint("##NodesPanelSearch", prompt, m_NodeBrowserSearchBuffer, sizeof(m_NodeBrowserSearchBuffer), searchFlags);
        ImGui::PopItemWidth();
        
        ImGui::PopStyleVar(3);
        if (customGraph) {
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleColor(5);
        
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        ImGui::BeginChild("NodesPanelScrollingResults", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoNav);

        const NodeBrowserEntry* activatedEntry = nullptr;
        constexpr float kCardWidth = 138.0f;
        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const int columnCount = std::max(1, static_cast<int>(std::floor(availableWidth / kCardWidth)));

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 6.0f));
        if (ImGui::BeginTable("NodesPanelGrid", columnCount, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoSavedSettings)) {
            for (int column = 0; column < columnCount; ++column) {
                ImGui::TableSetupColumn(("NodePreviewCol" + std::to_string(column)).c_str(), ImGuiTableColumnFlags_WidthFixed, kCardWidth);
            }

            ImGuiListClipper clipper;
            const int rowCount = static_cast<int>((filtered.size() + static_cast<std::size_t>(columnCount) - 1u) / static_cast<std::size_t>(columnCount));
            clipper.Begin(rowCount, 154.0f);
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    ImGui::TableNextRow();
                    for (int column = 0; column < columnCount; ++column) {
                        const std::size_t index = static_cast<std::size_t>(row * columnCount + column);
                        ImGui::TableSetColumnIndex(column);
                        if (index >= filtered.size()) {
                            continue;
                        }

                        const NodeBrowserEntry* entry = filtered[index];
                        if (!entry) {
                            continue;
                        }

                        const std::string key = EntryKey(*entry);
                        const auto alphaIt = m_NodeBrowserEntryAlpha.find(key);
                        const float entryAlpha = alphaIt != m_NodeBrowserEntryAlpha.end() ? alphaIt->second : 0.0f;
                        ImVec2 textureSize(0.0f, 0.0f);
                        bool previewPending = false;
                        bool previewFallback = false;
                        unsigned int texture = 0;
                        if (entry->previewStrategy != EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview) {
                            texture = GetNodeBrowserThumbnailTexture(
                                editor,
                                entry->previewKey,
                                &textureSize,
                                &previewPending,
                                &previewFallback);
                        }
                        const char* detailText = "";
                        if (entry->previewStrategy == EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview) {
                            detailText = "RAW pipeline";
                        } else if (previewPending) {
                            detailText = "Generating";
                        }

                        const bool pressed = RenderBrowserCard(
                            *entry,
                            texture,
                            textureSize,
                            detailText,
                            previewPending,
                            previewFallback,
                            entry->previewStrategy != EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview,
                            entryAlpha * alpha,
                            isOpen,
                            colCardHoverFill,
                            colTitleText,
                            colPassiveText,
                            colCardOverlay);

                        if (isOpen && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            const NodeBrowserEntry* dragEntry = entry;
                            ImGui::SetDragDropPayload("ADD_NODE_DRAG_PAYLOAD", &dragEntry, sizeof(NodeBrowserEntry*));
                            ImGui::Text("Add %s", entry->label.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (pressed && isOpen) {
                            activatedEntry = entry;
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();

        if (filtered.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, colPassiveText);
            ImGui::TextUnformatted("No matching nodes");
            ImGui::PopStyleColor();
        }

        ImGui::EndChild(); // end NodesPanelScrollingResults

        if (isOpen && !activatedEntry &&
            (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) &&
            !filtered.empty()) {
            activatedEntry = filtered.front();
        }

        if (isOpen && activatedEntry) {
            const std::vector<int> selectedNodeIds = graph.GetSelectedNodeIds();
            const int previousSelectedNodeId = selectedNodeIds.size() == 1 ? selectedNodeIds.front() : -1;
            const EditorNodeGraph::Node* previousSelectedNode = previousSelectedNodeId > 0 ? graph.FindNode(previousSelectedNodeId) : nullptr;
            const std::string previousOutputSocket = previousSelectedNode ? graph.DefaultOutputSocket(*previousSelectedNode) : std::string();
            EditorNodeGraph::Link downstreamLinkCopy {};
            bool hasDownstreamLink = false;
            if (previousSelectedNode && !previousOutputSocket.empty()) {
                if (const EditorNodeGraph::Link* downstreamLink = graph.FindOutputLink(previousSelectedNodeId, previousOutputSocket)) {
                    downstreamLinkCopy = *downstreamLink;
                    hasDownstreamLink = true;
                }
            }

            const int newNodeId = AddNodeFromBrowserEntry(editor, *activatedEntry, m_NodeBrowserGraphPos);
            if (newNodeId > 0) {
                m_PushedSourceNodeId = -1;
                m_PushDistance = 0.0f;
                m_PushedNodeIds.clear();

                if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromOutput) {
                    EditorNodeGraphUI::ConnectOutputToBestInput(editor, m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId, newNodeId);
                } else if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromInput) {
                    EditorNodeGraphUI::ConnectBestOutputToInput(editor, newNodeId, m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId);
                } else if (previousSelectedNodeId > 0) {
                    bool inserted = false;
                    if (selectedNodeIds.size() == 1 && hasDownstreamLink && downstreamLinkCopy.fromNodeId == previousSelectedNodeId) {
                        if (editor->RemoveGraphLink(
                                downstreamLinkCopy.fromNodeId,
                                downstreamLinkCopy.fromSocketId,
                                downstreamLinkCopy.toNodeId,
                                downstreamLinkCopy.toSocketId)) {
                            const bool connectedFirst = EditorNodeGraphUI::ConnectOutputToBestInput(editor, previousSelectedNodeId, previousOutputSocket, newNodeId);
                            const bool connectedSecond = connectedFirst
                                ? EditorNodeGraphUI::ConnectBestOutputToInput(editor, newNodeId, downstreamLinkCopy.toNodeId, downstreamLinkCopy.toSocketId)
                                : false;
                            if (!connectedFirst || !connectedSecond) {
                                editor->RemoveGraphNode(newNodeId);
                                editor->ConnectGraphSockets(
                                    downstreamLinkCopy.fromNodeId,
                                    downstreamLinkCopy.fromSocketId,
                                    downstreamLinkCopy.toNodeId,
                                    downstreamLinkCopy.toSocketId,
                                    nullptr);
                            } else {
                                inserted = true;
                            }
                        }
                    }
                    if (!inserted && selectedNodeIds.size() == 1) {
                        EditorNodeGraphUI::ConnectOutputToBestInput(editor, previousSelectedNodeId, previousOutputSocket, newNodeId);
                    }
                }
            }
            CloseNodeBrowser();
        }

        if (isOpen && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeBrowser = true;
        }

        ImGui::EndChild(); // end NodesPanelDrawerChild
    }

    if (closeBrowser && isOpen) {
        CloseNodeBrowser();
    }
}
