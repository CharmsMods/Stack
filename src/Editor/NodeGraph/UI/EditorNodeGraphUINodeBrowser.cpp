#include "Editor/NodeGraph/EditorNodeGraphUI.h"

#include "App/AppHeaderStyle.h"
#include "App/settings/AppearanceTheme.h"
#include "Editor/EditorModule.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Graph/GraphDocumentRules.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <unordered_set>
#include <imgui.h>

namespace {

using NodeBrowserEntry = EditorNodeGraphDefinitions::NodeCatalogEntry;

const std::vector<NodeBrowserEntry>& CachedNodeBrowserEntries() {
    static const std::vector<NodeBrowserEntry> entries = EditorNodeGraphDefinitions::BuildRegisteredNodeCatalogEntries();
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
    return appearance ? appearance->GetGraphVisualMode() : StackAppearance::GraphVisualMode::Classic;
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
    float& hoverAnim,
    float dt,
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
    const bool pressed = ImGui::InvisibleButton("card", size);
    const bool activated = interactive && pressed;
    const bool hovered = interactive && ImGui::IsItemHovered();
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float drawAlpha = std::clamp(alpha, 0.0f, 1.0f);
    hoverAnim = AnimateToward(hoverAnim, hovered ? 1.0f : 0.0f, hovered ? 22.0f : 16.0f, dt);
    const float lift = hoverAnim * 2.6f;
    const float glowAlpha = drawAlpha * hoverAnim;
    const ImVec2 renderMin(min.x, min.y - lift);
    const ImVec2 renderMax(max.x, max.y - lift);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 imageMin(renderMin.x + 6.0f, renderMin.y + 4.0f);
    const ImVec2 imageMax(renderMax.x - 6.0f, renderMin.y + 4.0f + kImageHeight);

    if (hoverAnim > 0.001f) {
        drawList->AddRectFilled(
            renderMin,
            renderMax,
            ScaleAlpha(cardHoverFill, 0.10f * glowAlpha),
            7.0f);
    }

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
    const ImVec2 titleMin(renderMin.x + 6.0f, imageMax.y + 8.0f);
    const ImVec2 titleMax(renderMax.x - 12.0f, titleMin.y + ImGui::GetTextLineHeight());
    DrawClippedTextLine(drawList, titleMin, titleMax, clippedTitle.c_str(), hoverAnim > 0.001f ? titleColor : footerColor);

    if (statusText && statusText[0] != '\0') {
        const std::string clippedStatus = EllipsizeText(statusText, size.x - 12.0f);
        const ImVec2 footerMin(renderMin.x + 6.0f, renderMax.y - 20.0f);
        const ImVec2 footerMax(renderMax.x - 12.0f, renderMax.y - 8.0f);
        DrawClippedTextLine(drawList, footerMin, footerMax, clippedStatus.c_str(), footerColor);
    }

    if (hoverAnim > 0.001f) {
        const float lineInset = 10.0f + (1.0f - hoverAnim) * 8.0f;
        drawList->AddLine(
            ImVec2(titleMin.x + lineInset, renderMax.y - 3.0f),
            ImVec2(renderMax.x - lineInset, renderMax.y - 3.0f),
            ScaleAlpha(cardHoverFill, 0.45f + hoverAnim * 0.30f),
            1.0f + hoverAnim * 0.3f);
    }

    ImGui::PopID();
    return activated;
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
        case EditorNodeGraph::NodeKind::Value: {
            EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
            editor->AddValueNodeAt(std::move(prototype.value.value), graphPos);
            break;
        }
        case EditorNodeGraph::NodeKind::FieldMean:
            editor->AddFieldMeanNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Reformat:
            editor->AddReformatNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawOperation:
            editor->AddRawOperationNodeAt(static_cast<Stack::RawRecipe::GraphOperationKind>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::TechnicalImage:
            editor->AddTechnicalImageNodeAt(
                static_cast<Stack::NodeMath::TechnicalImageOperation>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::Compound:
            editor->AddCompoundTemplateNodeAt(static_cast<std::size_t>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::FrequencyFilter:
            editor->AddFrequencyFilterNodeAt(
                static_cast<EditorNodeGraph::FrequencyFilterMode>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::FrequencyResponse:
            editor->AddFrequencyResponseNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::FrequencyFft:
            editor->AddFrequencyFftNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::FrequencyIfft:
            editor->AddFrequencyIfftNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::SpectrumView:
            editor->AddSpectrumViewNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ApplyFrequencyResponse:
            editor->AddApplyFrequencyResponseNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::CombineSpectra:
            editor->AddCombineSpectraNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::SpectrumSeparate:
            editor->AddSpectrumSeparateNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::SpectrumRecombine:
            editor->AddSpectrumRecombineNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::FrequencyMask:
            editor->AddFrequencyMaskNodeAt(static_cast<EditorNodeGraph::FrequencyMaskShape>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::SpectrumMath:
            editor->AddSpectrumMathNodeAt(static_cast<EditorNodeGraph::SpectrumMathMode>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::MagnitudePhase:
            editor->AddMagnitudePhaseNodeAt(static_cast<EditorNodeGraph::MagnitudePhaseMode>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::SpectrumAnalyzer:
            editor->AddSpectrumAnalyzerNodeAt(static_cast<EditorNodeGraph::SpectrumAnalyzerMode>(entry.value), graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelSplit:
            editor->AddChannelSplitNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ChannelCombine:
            editor->AddChannelCombineNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::ConstantChannel:
            editor->AddConstantChannelNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawDevelopment:
            editor->AddRawDevelopmentNodeAt(graphPos);
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
        case EditorNodeGraph::NodeKind::Mfsr:
            editor->AddMfsrNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Lut:
            editor->AddLutNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::RawNeuralDenoise:
            editor->AddRawNeuralDenoiseNodeAt(graphPos);
            break;
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::RawSource:
        case EditorNodeGraph::NodeKind::RawProjectFrame:
        case EditorNodeGraph::NodeKind::MultiFrameDenoise:
        case EditorNodeGraph::NodeKind::RawProjectSourceSet:
        case EditorNodeGraph::NodeKind::Composite:
            break;
    }
    const std::vector<int>& selected = editor->GetNodeGraph().GetSelectedNodeIds();
    return selected.empty() ? -1 : selected.back();
}

} // namespace

void EditorNodeGraphUI::SetCatalogHost(const ImVec2& position, const ImVec2& size,
    bool expanded, float visibleWidth) {
    const bool enabled = size.x > 0.0f && size.y > 0.0f;
    const bool wasDocked = IsCatalogDocked();
    if (!enabled && HasCatalogHost()) CloseNodeBrowser();
    if (wasDocked && enabled && !expanded) {
        // Explicit collapse closes both the docked view and any insertion
        // operation. Tab may reopen it temporarily after the panel closes.
        CloseNodeBrowser();
        m_NodeBrowserFocusSearch = false;
        m_NodeBrowserSearchFocused = false;
    }
    if (wasDocked != (enabled && expanded) || size.x != m_CatalogHostSize.x)
        m_NodeBrowserRestoreScroll = true;
    m_CatalogHostPosition = position;
    m_CatalogHostSize = enabled ? size : ImVec2();
    m_CatalogHostExpanded = enabled && expanded;
    m_CatalogHostVisibleWidth = enabled
        ? std::clamp(visibleWidth < 0.0f ? (expanded ? size.x : 0.0f) : visibleWidth, 0.0f, size.x)
        : 0.0f;
}

void EditorNodeGraphUI::OpenNodeBrowser(NodeBrowserMode mode, const EditorNodeGraph::Vec2& graphPos) {
    m_NodeBrowserMode = mode;
    m_NodeBrowserGraphPos = graphPos;
    m_NodeBrowserScreenPos = GraphToScreen(graphPos);
    m_DrawerMode = DrawerMode::NodeBrowser;
    m_NodeBrowserFocusSearch = true;
    m_NodeBrowserThumbnailCatalogEnsured = false;
    m_NodeBrowserFilterCacheValid = false;
    m_NodeBrowserOpenedAt = ImGui::GetTime();
    m_NodeBrowserLastFrameTime = m_NodeBrowserOpenedAt;
    if (!IsCatalogDocked()) m_NodeBrowserDrawerAlpha = 0.0f;
    if (!HasCatalogHost()) {
        m_NodeBrowserStablePanelWidth = 0.0f;
        m_NodeBrowserSearchBuffer[0] = '\0';
        m_NodeBrowserScrollY = 0.0f;
    }
    m_NodeBrowserRestoreScroll = true;
    m_NodeBrowserFilteredEntryIndices.clear();
    m_NodeBrowserEntryAlpha.clear();
    m_NodeBrowserCardHoverAnim.clear();
}

void EditorNodeGraphUI::CloseNodeBrowser() {
    if (m_DrawerMode == DrawerMode::NodeBrowser && m_PushedSourceNodeId > 0) {
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

    if (m_DrawerMode == DrawerMode::NodeBrowser) {
        m_DrawerMode = DrawerMode::None;
    }
    m_NodeBrowserFocusSearch = false;
    m_NodeBrowserSearchFocused = false;
    m_NodeBrowserMode = NodeBrowserMode::GeneralAdd;
    m_NodeBrowserFilterCacheValid = false;
    m_NodeBrowserDragFromNodeId = -1;
    m_NodeBrowserDragFromSocketId.clear();
    m_NodeBrowserDragToNodeId = -1;
    m_NodeBrowserDragToSocketId.clear();
    m_NodeBrowserFilteredEntryIndices.clear();
    if (!IsCatalogDocked()) {
        m_NodeBrowserThumbnailCatalogEnsured = false;
        m_NodeBrowserEntryAlpha.clear();
        m_NodeBrowserCardHoverAnim.clear();
    }
}

void EditorNodeGraphUI::RenderNodeBrowser(EditorModule* editor) {
    // Old popup window rendering removed in favor of RenderNodesPanelDrawer
}

void EditorNodeGraphUI::CloseTransientDrawers() {
    CloseNodeBrowser();
    m_DrawerMode = DrawerMode::None;
}

void EditorNodeGraphUI::RenderNodesPanelDrawer(
    EditorModule* editor,
    float panelWidth,
    float targetPanelWidth,
    float paneHeight,
    const ImVec2& workspacePos) {
    if (!editor) {
        return;
    }

    const bool hosted = HasCatalogHost();
    const bool operationOpen = IsNodeBrowserOpen();
    const bool docked = IsCatalogDocked() ||
        (hosted && !operationOpen && m_CatalogHostVisibleWidth > 0.5f);
    const bool isOpen = operationOpen || docked;
    if (hosted) {
        targetPanelWidth = m_CatalogHostSize.x;
        panelWidth = docked ? m_CatalogHostVisibleWidth : m_CatalogHostSize.x;
    }
    const int frameCount = ImGui::GetFrameCount();
    if (m_NodeBrowserTextureUploadBudgetFrame != frameCount) {
        m_NodeBrowserTextureUploadBudgetFrame = frameCount;
        m_NodeBrowserTextureUploadsThisFrame = 0;
    }

    const double now = ImGui::GetTime();
    const float dt = static_cast<float>(std::clamp(now - m_NodeBrowserLastFrameTime, 0.0, 0.05));
    m_NodeBrowserLastFrameTime = now;

    const float targetAlpha = isOpen ? 1.0f : 0.0f;
    if (docked || (!isOpen && m_NodeBrowserDrawerAlpha < 0.01f)) {
        m_NodeBrowserDrawerAlpha = 0.0f;
    } else {
        m_NodeBrowserDrawerAlpha += (targetAlpha - m_NodeBrowserDrawerAlpha) * dt * 15.0f;
    }
    const float alpha = docked ? 1.0f : std::clamp(m_NodeBrowserDrawerAlpha, 0.0f, 1.0f);
    if (isOpen && targetPanelWidth > 1.0f) {
        m_NodeBrowserStablePanelWidth = targetPanelWidth;
    } else {
        m_NodeBrowserStablePanelWidth = std::max(m_NodeBrowserStablePanelWidth, panelWidth);
    }
    const float layoutPanelWidth = std::max(panelWidth, m_NodeBrowserStablePanelWidth);

    const EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const std::vector<NodeBrowserEntry>& entries = CachedNodeBrowserEntries();
    const std::string search = m_NodeBrowserSearchBuffer;
    std::vector<const NodeBrowserEntry*> filtered;
    if (isOpen) {
        const std::uint64_t graphRevision = graph.GetStructureRevision();
        const bool filterCacheDirty =
            !m_NodeBrowserFilterCacheValid ||
            m_NodeBrowserFilterCacheGraphRevision != graphRevision ||
            m_NodeBrowserFilterCacheSearch != search ||
            m_NodeBrowserFilterCacheMode != m_NodeBrowserMode ||
            m_NodeBrowserFilterCacheDragFromNodeId != m_NodeBrowserDragFromNodeId ||
            m_NodeBrowserFilterCacheDragFromSocketId != m_NodeBrowserDragFromSocketId ||
            m_NodeBrowserFilterCacheDragToNodeId != m_NodeBrowserDragToNodeId ||
            m_NodeBrowserFilterCacheDragToSocketId != m_NodeBrowserDragToSocketId;
        if (filterCacheDirty) {
            m_NodeBrowserFilteredEntryIndices.clear();
            m_NodeBrowserFilteredEntryIndices.reserve(entries.size());

            std::optional<std::pair<int, std::string>> endpoint;
            if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromOutput)
                endpoint = {{m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId}};
            else if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromInput)
                endpoint = {{m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId}};
            const auto available = editor->GetGraphEditorContext().queryAvailableNodes(endpoint);
            std::unordered_set<std::string> availableKeys;
            for (const auto& entry : available) availableKeys.insert(EntryKey(entry));

            for (std::size_t index = 0; index < entries.size(); ++index) {
                const NodeBrowserEntry& entry = entries[index];
                const bool matchesSearch =
                    ContainsCaseInsensitive(entry.label, search) ||
                    ContainsCaseInsensitive(entry.category, search) ||
                    ContainsCaseInsensitive(entry.searchAliases, search);
                if (!matchesSearch) {
                    continue;
                }

                if (!availableKeys.count(EntryKey(entry))) continue;

                m_NodeBrowserFilteredEntryIndices.push_back(index);
            }

            m_NodeBrowserFilterCacheValid = true;
            m_NodeBrowserFilterCacheGraphRevision = graphRevision;
            m_NodeBrowserFilterCacheSearch = search;
            m_NodeBrowserFilterCacheMode = m_NodeBrowserMode;
            m_NodeBrowserFilterCacheDragFromNodeId = m_NodeBrowserDragFromNodeId;
            m_NodeBrowserFilterCacheDragFromSocketId = m_NodeBrowserDragFromSocketId;
            m_NodeBrowserFilterCacheDragToNodeId = m_NodeBrowserDragToNodeId;
            m_NodeBrowserFilterCacheDragToSocketId = m_NodeBrowserDragToSocketId;
        }

        filtered.reserve(m_NodeBrowserFilteredEntryIndices.size());
        for (std::size_t index : m_NodeBrowserFilteredEntryIndices) {
            if (index < entries.size()) {
                filtered.push_back(&entries[index]);
            }
        }
    }

    std::unordered_set<std::string> visibleKeys;
    visibleKeys.reserve(filtered.size());
    for (const NodeBrowserEntry* entry : filtered) {
        if (!entry) {
            continue;
        }
        const std::string key = EntryKey(*entry);
        visibleKeys.insert(key);
        float& entryAlpha = m_NodeBrowserEntryAlpha[key];
        entryAlpha = AnimateToward(entryAlpha, 1.0f, 18.0f, dt);
    }

    for (auto it = m_NodeBrowserEntryAlpha.begin(); it != m_NodeBrowserEntryAlpha.end();) {
        if (visibleKeys.find(it->first) == visibleKeys.end()) {
            it->second = AnimateToward(it->second, 0.0f, 14.0f, dt);
        }
        if (it->second <= 0.01f) {
            it = m_NodeBrowserEntryAlpha.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_NodeBrowserCardHoverAnim.begin(); it != m_NodeBrowserCardHoverAnim.end();) {
        if (m_NodeBrowserEntryAlpha.find(it->first) == m_NodeBrowserEntryAlpha.end()) {
            it = m_NodeBrowserCardHoverAnim.erase(it);
        } else {
            ++it;
        }
    }

    if (isOpen && !m_NodeBrowserThumbnailCatalogEnsured) {
        editor->EnsureNodeBrowserThumbnailCatalog();
        m_NodeBrowserThumbnailCatalogEnsured = true;
    }

    // 1. Sliding panel background & content drawing
    ImVec4 workspaceColor = editor->GetWorkspaceBaseColor();
    const float luminance = 0.2126f * workspaceColor.x + 0.7152f * workspaceColor.y + 0.0722f * workspaceColor.z;
    const bool isLightBg = luminance >= 0.5f;
    const StackAppearance::GraphVisualMode graphMode = CurrentGraphVisualMode(editor);
    const bool spotlightGraph = graphMode == StackAppearance::GraphVisualMode::SpotlightPrototype;
    const bool blackNodeGraph = graphMode == StackAppearance::GraphVisualMode::BlackNodes;
    const bool customGraph = graphMode != StackAppearance::GraphVisualMode::Classic;
    const StackAppearance::AppearanceManager* appearance = editor->GetAppearance();
    const bool wallpaperSurfaces = appearance && appearance->GetSeamlessSurfaceStylingEnabled();
    const StackAppearance::RuntimeSurfacePalette surfacePalette =
        appearance ? appearance->GetRuntimeSurfacePalette() : StackAppearance::RuntimeSurfacePalette{};
    const ImVec4 themeText = appearance ? appearance->GetWorkingTheme().colors[ImGuiCol_Text] : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 themeMuted = appearance ? appearance->GetWorkingTheme().colors[ImGuiCol_TextDisabled] : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const ImVec4 themeAccent = appearance ? appearance->GetWorkingTheme().colors[ImGuiCol_CheckMark] : ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    ImVec4 colBgOpaqueVec = docked ? surfacePalette.panelSurface : surfacePalette.drawerSurface;
    const float backgroundOpacity = hosted && docked
        ? Stack::Header::ResolveSectionTintOpacity(m_CatalogHostVisibleWidth / m_CatalogHostSize.x)
        : 1.0f;
    colBgOpaqueVec.w = alpha * backgroundOpacity;
    const ImU32 colBgOpaque = ImGui::ColorConvertFloat4ToU32(colBgOpaqueVec);
    const ImU32 colBgTrans = ColorWithAlpha(colBgOpaqueVec,0);
    const ImU32 colTitleText = ColorWithAlpha(themeText,alpha);
    const ImU32 colPassiveText = ColorWithAlpha(themeMuted,alpha);
    const ImU32 colNormalText = ColorWithAlpha(themeText,alpha);
    const ImU32 colHoveredHeader = ColorWithAlpha(surfacePalette.controlSurfaceHovered,alpha);
    const ImU32 colActiveHeader = ColorWithAlpha(surfacePalette.controlSurfaceActive,alpha);
    const ImU32 colCardHoverFill = colHoveredHeader;

    const ImU32 colCardOverlay = ColorWithAlpha(themeAccent,0.72f*alpha);
    const ImGuiViewport* rootViewport = ImGui::GetMainViewport();
    const ImVec2 overlayPos = hosted ? m_CatalogHostPosition : rootViewport ? rootViewport->Pos : workspacePos;
    const ImVec2 overlaySize = rootViewport ? rootViewport->Size : ImVec2(std::max(layoutPanelWidth,panelWidth),paneHeight);
    const float overlayHeight = hosted ? m_CatalogHostSize.y : std::max(paneHeight,overlaySize.y);
    const float drawerWidth = std::max(0.0f, hosted && !docked ? panelWidth * alpha : panelWidth);
    const float featherWidth = hosted ? 0.0f : std::min(92.0f,std::max(0.0f,drawerWidth*0.22f));
    const float solidWidth = std::max(0.0f,drawerWidth-featherWidth);
    const float horizontalPadding = 18.0f;
    const float topPadding = hosted ? 20.0f : 74.0f;
    const float bottomPadding = 18.0f;
    const float contentWidth = std::max(1.0f,
        (hosted ? m_CatalogHostSize.x : drawerWidth - featherWidth) - horizontalPadding * 2.0f + (hosted ? 0.0f : 8.0f));

    bool closeBrowser = false;
    const bool closeAllowed =
        operationOpen && !docked &&
        (now - m_NodeBrowserOpenedAt) > 0.12 &&
        !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f);
    if (closeAllowed) {
        const ImVec2 mousePos = ImGui::GetIO().MousePos;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (mousePos.x < overlayPos.x || mousePos.x > overlayPos.x + drawerWidth ||
                mousePos.y < overlayPos.y || mousePos.y > overlayPos.y + overlayHeight) {
                closeBrowser = true;
            }
        }
    }

    if (drawerWidth > 1.0f || alpha > 0.01f) {
        const bool interactiveOverlay = isOpen && (!docked ||
            (m_CatalogHostExpanded && m_CatalogHostVisibleWidth >= m_CatalogHostSize.x - .5f));
        ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(std::max(1.0f, drawerWidth), overlayHeight), ImGuiCond_Always);
        if (rootViewport) {
            ImGui::SetNextWindowViewport(rootViewport->ID);
        }
        if (interactiveOverlay && m_NodeBrowserFocusSearch) {
            ImGui::SetNextWindowFocus();
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
        ImGuiWindowFlags overlayFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoFocusOnAppearing;
        if (!interactiveOverlay) {
            overlayFlags |= ImGuiWindowFlags_NoInputs;
        }
        const std::string windowName = "##NodeBrowserDrawerOverlay_" +
            std::to_string(reinterpret_cast<std::uintptr_t>(this));
        ImGui::Begin(windowName.c_str(), nullptr, overlayFlags);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(4);
        ImGui::BeginDisabled(!interactiveOverlay);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 panelMin = ImGui::GetWindowPos();
        const ImVec2 panelMax(panelMin.x + drawerWidth, panelMin.y + overlayHeight);
        if (solidWidth > 0.0f) {
            drawList->AddRectFilled(
                panelMin,
                ImVec2(panelMin.x + solidWidth, panelMax.y),
                colBgOpaque);
        }
        if (drawerWidth > 0.0f) {
            drawList->AddRectFilledMultiColor(
                ImVec2(panelMin.x + solidWidth, panelMin.y),
                panelMax,
                colBgOpaque,
                colBgTrans,
                colBgTrans,
                colBgOpaque);
        }

        const float reveal = alpha * alpha * (3.0f - 2.0f * alpha);
        const float revealOffsetY = (1.0f - reveal) * 10.0f;
        const float scrollFadeHeight = 34.0f;
        ImGui::SetCursorPos(ImVec2(horizontalPadding, topPadding + revealOffsetY));
        if (hosted) {
            ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
            ImGui::TextUnformatted("Node library");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
        }

        if (interactiveOverlay && m_NodeBrowserFocusSearch) {
            ImGui::SetKeyboardFocusHere();
            m_NodeBrowserFocusSearch = false;
        }

        const char* prompt = (m_NodeBrowserMode == NodeBrowserMode::GeneralAdd)
            ? "Search nodes"
            : "Search compatible nodes";
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
        const ImU32 scrollbarGrab = wallpaperSurfaces
            ? ColorWithAlpha(surfacePalette.controlSurfaceHovered, 0.78f * alpha)
            : ColorWithAlpha(BlendColor(themeAccent, themeText, 0.14f), 0.44f * alpha);
        const ImU32 scrollbarGrabHovered = wallpaperSurfaces
            ? ColorWithAlpha(surfacePalette.controlSurfaceActive, 0.90f * alpha)
            : ColorWithAlpha(BlendColor(themeAccent, themeText, 0.10f), 0.62f * alpha);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, searchBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, searchBgHovered);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, searchBgHovered);
        ImGui::PushStyleColor(ImGuiCol_Text, colTitleText);
        ImGui::PushStyleColor(ImGuiCol_TextDisabled, colPassiveText);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
        ImGui::PushItemWidth(contentWidth);
        ImGuiInputTextFlags searchFlags = isOpen ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_ReadOnly;
        ImGui::InputTextWithHint("##NodesPanelSearch", prompt, m_NodeBrowserSearchBuffer, sizeof(m_NodeBrowserSearchBuffer), searchFlags);
        m_NodeBrowserSearchFocused = ImGui::IsItemActive();
        ImGui::PopItemWidth();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);

        ImGui::Dummy(ImVec2(0.0f, 16.0f));

        ImVec2 resultsMin(0.0f, 0.0f);
        ImVec2 resultsMax(0.0f, 0.0f);
        const NodeBrowserEntry* activatedEntry = nullptr;
        const float resultsHeight = std::max(1.0f, overlayHeight - ImGui::GetCursorPosY() - bottomPadding);

        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, scrollbarGrab);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, scrollbarGrabHovered);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, scrollbarGrabHovered);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 999.0f);
        ImGui::BeginChild("NodesPanelScrollingResults", ImVec2(contentWidth, resultsHeight), false, ImGuiWindowFlags_NoNav);
        if (m_NodeBrowserRestoreScroll) {
            ImGui::SetScrollY(m_NodeBrowserScrollY);
            m_NodeBrowserRestoreScroll = false;
        }
        resultsMin = ImGui::GetWindowPos();
        resultsMax = ImVec2(resultsMin.x + ImGui::GetWindowSize().x, resultsMin.y + ImGui::GetWindowSize().y);

        constexpr float kCardWidth = 138.0f;
        const int columnCount = std::max(1, static_cast<int>(std::floor(contentWidth / kCardWidth)));

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 6.0f));
        if (ImGui::BeginTable("NodesPanelGrid", columnCount, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoPadInnerX | ImGuiTableFlags_NoSavedSettings, ImVec2(contentWidth, 0.0f))) {
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

                        float& hoverAnim = m_NodeBrowserCardHoverAnim[key];
                        const bool pressed = RenderBrowserCard(
                            *entry,
                            texture,
                            textureSize,
                            detailText,
                            previewPending,
                            previewFallback,
                            entry->previewStrategy != EditorNodeGraphDefinitions::NodeCatalogPreviewStrategy::NoPreview,
                            entryAlpha * alpha,
                            interactiveOverlay,
                            hoverAnim,
                            dt,
                            colCardHoverFill,
                            colTitleText,
                            colPassiveText,
                            colCardOverlay);

                        if (interactiveOverlay && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                            const NodeBrowserEntry* dragEntry = entry;
                            ImGui::SetDragDropPayload("ADD_NODE_DRAG_PAYLOAD", &dragEntry, sizeof(NodeBrowserEntry*));
                            ImGui::Text("Add %s", entry->label.c_str());
                            ImGui::EndDragDropSource();
                        }

                        if (pressed && interactiveOverlay) {
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

        m_NodeBrowserScrollY = ImGui::GetScrollY();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(5);

        if (resultsMax.y > resultsMin.y) {
            const float fadeHeight = std::min(scrollFadeHeight, std::max(0.0f, (resultsMax.y - resultsMin.y) * 0.14f));
            const ImU32 fadeOpaque = ColorWithAlpha(colBgOpaqueVec, std::min(1.0f, colBgOpaqueVec.w * 1.08f));
            drawList->AddRectFilledMultiColor(
                resultsMin,
                ImVec2(resultsMax.x, resultsMin.y + fadeHeight),
                fadeOpaque,
                fadeOpaque,
                colBgTrans,
                colBgTrans);
            drawList->AddRectFilledMultiColor(
                ImVec2(resultsMin.x, resultsMax.y - fadeHeight),
                resultsMax,
                colBgTrans,
                colBgTrans,
                fadeOpaque,
                fadeOpaque);
        }

        const bool submitPressed =
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
        const bool catalogFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        if (interactiveOverlay && catalogFocused && !activatedEntry && submitPressed) {
            if (!filtered.empty()) {
                activatedEntry = filtered.front();
            } else {
                // Single-line inputs release their active focus on Enter. With
                // nothing to activate, keep the user in the search workflow.
                m_NodeBrowserFocusSearch = true;
            }
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

            if (docked && !operationOpen) {
                m_NodeBrowserGraphPos = previousSelectedNode
                    ? EditorNodeGraph::Vec2{previousSelectedNode->position.x + 340.0f, previousSelectedNode->position.y}
                    : m_HasLastGraphMousePos ? m_LastGraphMousePos
                    : ScreenToGraph(EditorNodeGraph::Vec2{
                        (m_CanvasMin.x + m_CanvasMax.x) * 0.5f,
                        (m_CanvasMin.y + m_CanvasMax.y) * 0.5f});
            }
            const int newNodeId = AddNodeFromBrowserEntry(editor, *activatedEntry, m_NodeBrowserGraphPos);
            if (newNodeId > 0) {
                if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromOutput) {
                    EditorNodeGraphUI::ConnectOutputToBestInput(editor, m_NodeBrowserDragFromNodeId, m_NodeBrowserDragFromSocketId, newNodeId);
                } else if (m_NodeBrowserMode == NodeBrowserMode::ConnectFromInput) {
                    EditorNodeGraphUI::ConnectBestOutputToInput(editor, newNodeId, m_NodeBrowserDragToNodeId, m_NodeBrowserDragToSocketId);
                }
            }

            CloseNodeBrowser();
        }

        if (interactiveOverlay && (catalogFocused || operationOpen) && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            closeBrowser = true;
        }

        ImGui::EndDisabled();
        ImGui::End();
    }

    if (closeBrowser && isOpen) {
        CloseNodeBrowser();
    }
}
