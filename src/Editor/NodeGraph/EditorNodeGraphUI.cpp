#include "EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "EditorNodeGraphDefinitions.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>
#include "Renderer/GLLoader.h"
#include <string>

namespace {

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

float NodeUiScaleFromZoom(float zoom) {
    return std::clamp(zoom, 0.16f, 2.5f);
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

using NodeBrowserEntry = EditorNodeGraphDefinitions::NodeCatalogEntry;

NodeFamily FamilyForNode(const EditorNodeGraph::Node& node) {
    switch (node.kind) {
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Output:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::ExportBoundsSettings:
            return NodeFamily::Gray;
        case EditorNodeGraph::NodeKind::Layer:
            return NodeFamily::Layer;
        case EditorNodeGraph::NodeKind::Preview:
            return NodeFamily::Preview;
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::ImageToMask:
            return NodeFamily::Mask;
        case EditorNodeGraph::NodeKind::Scope:
            return NodeFamily::Scope;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            return NodeFamily::Generator;
        case EditorNodeGraph::NodeKind::Mix:
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

std::vector<NodeBrowserEntry> BuildNodeBrowserEntries() {
    return EditorNodeGraphDefinitions::BuildNodeCatalogEntries();
}

bool ConnectOutputToBestInput(EditorModule* editor, int fromNodeId, const std::string& fromSocketId, int toNodeId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!to) {
        return false;
    }
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*to, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        std::string error;
        if (editor->ConnectGraphSockets(fromNodeId, fromSocketId, toNodeId, socket.id, &error)) {
            return true;
        }
    }
    return false;
}

bool ConnectBestOutputToInput(EditorModule* editor, int fromNodeId, int toNodeId, const std::string& toSocketId) {
    if (!editor) {
        return false;
    }
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    if (!from) {
        return false;
    }
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(*from, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        std::string error;
        if (editor->ConnectGraphSockets(fromNodeId, socket.id, toNodeId, toSocketId, &error)) {
            return true;
        }
    }
    return false;
}

bool PrototypeHasCompatibleInput(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    const NodeBrowserEntry& entry) {
    EditorNodeGraph::SocketDefinition fromSocket;
    if (!graph.FindSocket(fromNodeId, fromSocketId, &fromSocket)) {
        return false;
    }
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(prototype, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
            continue;
        }
        if (fromSocket.type == EditorNodeGraph::SocketType::Image) {
            if (prototype.kind == EditorNodeGraph::NodeKind::Layer && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Mix &&
                (socket.id == EditorNodeGraph::kMixInputASocketId || socket.id == EditorNodeGraph::kMixInputBSocketId)) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::ImageToMask && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Output && socket.id == EditorNodeGraph::kImageInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Preview && socket.id == EditorNodeGraph::kPreviewInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Scope && socket.id == EditorNodeGraph::kScopeInputSocketId) return true;
        } else if (fromSocket.type == EditorNodeGraph::SocketType::Mask && fromSocketId == EditorNodeGraph::kMaskOutputSocketId) {
            if (prototype.kind == EditorNodeGraph::NodeKind::Layer && socket.id == EditorNodeGraph::kMaskInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Mix && socket.id == EditorNodeGraph::kMixFactorSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::MaskUtility && socket.id == EditorNodeGraph::kMaskInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Preview && socket.id == EditorNodeGraph::kPreviewInputSocketId) return true;
            if (prototype.kind == EditorNodeGraph::NodeKind::Scope && socket.id == EditorNodeGraph::kScopeInputSocketId) return true;
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
    EditorNodeGraph::SocketDefinition toSocket;
    if (!graph.FindSocket(toNodeId, toSocketId, &toSocket)) {
        return false;
    }
    const EditorNodeGraph::Node prototype = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(prototype, true)) {
        if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
            continue;
        }
        if (toSocket.type == EditorNodeGraph::SocketType::Image && socket.type == EditorNodeGraph::SocketType::Image) {
            return true;
        }
        if (toSocket.type == EditorNodeGraph::SocketType::Mask && socket.type == EditorNodeGraph::SocketType::Mask) {
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
        case EditorNodeGraph::NodeKind::Image:
        case EditorNodeGraph::NodeKind::Composite:
        case EditorNodeGraph::NodeKind::ExportBoundsSettings:
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
    if (node.kind == EditorNodeGraph::NodeKind::ExportBoundsSettings) {
        metrics.width = 318.0f;
        metrics.contentLaneWidth = 238.0f;
        metrics.previewWidth = 238.0f;
        metrics.previewHeight = 168.0f;
        metrics.minExpandedHeight = 404.0f;
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

float PinRadiusForZoom(float zoom) {
    return std::max(1.8f, 5.3f * NodeUiScaleFromZoom(zoom));
}

ImU32 ColorWithAlpha(const ImVec4& color, float alpha) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, std::clamp(alpha, 0.0f, 1.0f)));
}

float NodeControlWidthForScale(float logicalWidth, float uiScale) {
    return std::max(18.0f, logicalWidth * uiScale);
}

ImVec2 NodePreviewSizeForScale(const NodeLayoutMetrics& metrics, float uiScale) {
    return ImVec2(std::max(22.0f, metrics.previewWidth * uiScale), std::max(18.0f, metrics.previewHeight * uiScale));
}

ImU32 TypedSocketColor(EditorNodeGraph::SocketType type, const NodeFamilyStyle& familyStyle) {
    const ImVec4 imageBase(0.60f, 0.69f, 0.79f, 1.0f);
    const ImVec4 maskBase(0.71f, 0.64f, 0.82f, 1.0f);
    const ImVec4 analysisBase(0.76f, 0.68f, 0.57f, 1.0f);
    const ImVec4 valueBase(0.66f, 0.73f, 0.73f, 1.0f);
    ImVec4 base = imageBase;
    switch (type) {
        case EditorNodeGraph::SocketType::Image: base = imageBase; break;
        case EditorNodeGraph::SocketType::Mask: base = maskBase; break;
        case EditorNodeGraph::SocketType::Analysis: base = analysisBase; break;
        case EditorNodeGraph::SocketType::Value: base = valueBase; break;
    }
    return ImGui::ColorConvertFloat4ToU32(BlendColor(base, familyStyle.accent, 0.38f));
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
        case EditorNodeGraph::NodeKind::ExportBoundsSettings:
            return headerBlock + sectionGap + row + gap + row + gap + row + gap + row + gap + colorRow + gap + row + gap + row + gap + row + bottomPadding;
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
        case EditorNodeGraph::NodeKind::ImageToMask:
            return headerBlock + sectionGap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + row + gap + sliderRow + gap + checkboxRow + bottomPadding;
        case EditorNodeGraph::NodeKind::ImageGenerator:
            if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::SolidColor ||
                node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Square ||
                node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::Circle) {
                return headerBlock + sectionGap + row + gap + colorRow + bottomPadding;
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
        case EditorNodeGraph::NodeKind::ExportBoundsSettings:
        case EditorNodeGraph::NodeKind::Scope:
        case EditorNodeGraph::NodeKind::MaskGenerator:
        case EditorNodeGraph::NodeKind::Mix:
        case EditorNodeGraph::NodeKind::Preview:
        case EditorNodeGraph::NodeKind::MaskUtility:
        case EditorNodeGraph::NodeKind::ImageToMask:
        case EditorNodeGraph::NodeKind::ImageGenerator:
            return true;
    }
    return false;
}

bool ShouldShowKindLabel(const EditorNodeGraph::Node& node) {
    return node.expanded &&
        node.kind != EditorNodeGraph::NodeKind::Composite &&
        node.kind != EditorNodeGraph::NodeKind::ExportBoundsSettings;
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
        case EditorNodeGraph::NodeKind::Layer: return "Layer";
        case EditorNodeGraph::NodeKind::Output: return "Output";
        case EditorNodeGraph::NodeKind::Composite: return "Composite";
        case EditorNodeGraph::NodeKind::ExportBoundsSettings: return "Export";
        case EditorNodeGraph::NodeKind::Scope: return "Scope";
        case EditorNodeGraph::NodeKind::MaskGenerator: return "Mask";
        case EditorNodeGraph::NodeKind::Mix: return "Merge";
        case EditorNodeGraph::NodeKind::Preview: return "Preview";
        case EditorNodeGraph::NodeKind::MaskUtility: return "Mask Utility";
        case EditorNodeGraph::NodeKind::ImageToMask: return "Image To Mask";
        case EditorNodeGraph::NodeKind::ImageGenerator: return "Generator";
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
        case EditorNodeGraph::MaskUtilityKind::Levels: return "Levels Mask";
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
    }
    return "Generated Image";
}

const char* MixBlendLabel(EditorNodeGraph::MixBlendMode mode) {
    switch (mode) {
        case EditorNodeGraph::MixBlendMode::Normal: return "Normal / Lerp";
        case EditorNodeGraph::MixBlendMode::Add: return "Add";
        case EditorNodeGraph::MixBlendMode::Multiply: return "Multiply";
        case EditorNodeGraph::MixBlendMode::Screen: return "Screen";
        case EditorNodeGraph::MixBlendMode::AlphaOver: return "Alpha Over";
    }
    return "Normal / Lerp";
}

float DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float wx = p.x - a.x;
    const float wy = p.y - a.y;
    const float lenSq = vx * vx + vy * vy;
    const float t = lenSq > 0.0f ? std::max(0.0f, std::min(1.0f, (wx * vx + wy * vy) / lenSq)) : 0.0f;
    const float px = a.x + t * vx;
    const float py = a.y + t * vy;
    const float dx = p.x - px;
    const float dy = p.y - py;
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

void EditorNodeGraphUI::Initialize() {}

bool EditorNodeGraphUI::IsGraphCanvasHovered() const {
    return ImGui::IsMouseHoveringRect(ToImVec2(m_CanvasMin), ToImVec2(m_CanvasMax), false);
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
            const bool overRealWidget = context && (context->HoveredId != 0 || context->ActiveId != 0);
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
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    m_NodeContentActive = false;
    m_NodeContentHovered = false;
    m_GraphInteractionBlocked = false;
    m_MouseOwner = GraphMouseOwner::None;
    m_LastNodeControlId = 0;
    m_NodeLayoutCache.clear();

    const bool minimalGuide = graph.GetNodes().size() <= 1;
    if (minimalGuide) {
        ImGui::TextDisabled("Drag to connect. Right-click to add.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
    }

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize = ImVec2(std::max(320.0f, available.x), std::max(320.0f, available.y));
    ImGui::Dummy(canvasSize);

    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    m_CanvasOrigin = ToGraphVec2(canvasMin);
    m_CanvasMin = ToGraphVec2(canvasMin);
    m_CanvasMax = ToGraphVec2(canvasMax);
    editor->ApplyGraphAutoFocusFrame(canvasSize.x, canvasSize.y, m_Pan.x, m_Pan.y, m_Zoom);
    editor->SetGraphDropTargetRect(canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y);
    editor->SetGraphViewTransform(canvasMin.x, canvasMin.y, m_Pan.x, m_Pan.y, m_Zoom);
    const bool graphHovered = IsGraphCanvasHovered();
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Layer && editor->LayerSupportsAdvancedEditor(node.layerIndex)) {
            node.expanded = false;
        }
    }
    if ((graphHovered || ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) &&
        !editor->IsAdvancedEditorOpen() &&
        !m_NodeBrowserOpen &&
        editor->CanConsumeEditorCommandKeys() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) &&
        ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
        OpenNodeBrowser(NodeBrowserMode::GeneralAdd, ScreenToGraph(ToGraphVec2(ImGui::GetMousePos())));
    }
    if (graphHovered && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::GetIO().MouseWheel != 0.0f) {
        editor->CancelGraphAutoFocusTracking();
        ZoomAtMouse(ImGui::GetIO().MouseWheel);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin, canvasMax, true);
    const ImVec4 workspaceBg = editor->GetWorkspaceBaseColor();
    drawList->AddRectFilled(canvasMin, canvasMax, ImGui::ColorConvertFloat4ToU32(workspaceBg));

    const float gridStep = std::max(8.0f, 32.0f * m_Zoom);
    const ImVec4 separatorColor = ImGui::GetStyleColorVec4(ImGuiCol_Separator);
    const ImU32 gridColor = ImGui::ColorConvertFloat4ToU32(ImVec4(separatorColor.x, separatorColor.y, separatorColor.z, 0.12f));
    for (float x = std::fmod(m_Pan.x, gridStep); x < canvasSize.x; x += gridStep) {
        drawList->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), gridColor);
    }
    for (float y = std::fmod(m_Pan.y, gridStep); y < canvasSize.y; y += gridStep) {
        drawList->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), gridColor);
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
    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        RenderNode(editor, node);
    }
    graphSplitter.SetCurrentChannel(drawList, 0);
    RenderLinks(graph);
    graphSplitter.Merge(drawList);

    RenderInteraction(editor, graph);
    RenderValidationStatus(graph);
    const int debugHoveredNodeId = IsGraphCanvasHovered() ? FindNodeAt(graph, ToGraphVec2(ImGui::GetMousePos())) : -1;
    RenderInteractionDebugOverlay(graph, debugHoveredNodeId, m_MouseOwner);
    RenderContextMenu(editor);
    RenderNodeBrowser(editor);

    const float seamFadeFloor = std::min(56.0f, std::max(8.0f, canvasSize.x - 2.0f));
    const float seamFade = std::clamp(canvasSize.x * 0.26f, seamFadeFloor, 160.0f);
    const float edgeFade = std::min(56.0f, std::min(canvasSize.x, canvasSize.y) * 0.10f);
    const ImU32 fadeSoft = ColorWithAlpha(workspaceBg, 0.62f);
    const ImU32 fadeStrong = ColorWithAlpha(workspaceBg, 1.0f);
    const ImU32 fadeClear = ColorWithAlpha(workspaceBg, 0.0f);
    if (edgeFade > 1.0f) {
        drawList->AddRectFilledMultiColor(canvasMin, ImVec2(canvasMin.x + edgeFade, canvasMax.y), fadeSoft, fadeClear, fadeClear, fadeSoft);
        drawList->AddRectFilledMultiColor(canvasMin, ImVec2(canvasMax.x, canvasMin.y + edgeFade), fadeSoft, fadeSoft, fadeClear, fadeClear);
        drawList->AddRectFilledMultiColor(ImVec2(canvasMin.x, canvasMax.y - edgeFade), canvasMax, fadeClear, fadeClear, fadeSoft, fadeSoft);
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
    const float uiScale = NodeUiScaleFromZoom(m_Zoom);
    const float pinRadius = PinRadiusForZoom(m_Zoom);
    const float headerInsetX = metrics.headerInsetX * uiScale;
    const float headerInsetY = metrics.headerInsetY * uiScale;
    const float bodyInsetBottom = metrics.bodyInsetBottom * uiScale;
    const float sectionGap = metrics.sectionGap * uiScale;
    const float laneInset = std::max(
        14.0f * uiScale,
        ((metrics.width - metrics.contentLaneWidth) * 0.5f - metrics.headerInsetX) * uiScale);

    const EditorNodeGraph::Vec2 safePosition = ClampGraphPosition(node.position);
    const EditorNodeGraph::Vec2 nodeScreenPos = GraphToScreen(safePosition);
    const EditorNodeGraph::Vec2 nodeSize = NodeScreenSize(node);
    const ImVec2 frameMin = ToImVec2(nodeScreenPos);
    const ImVec2 frameMax(frameMin.x + nodeSize.x, frameMin.y + nodeSize.y);

    const bool showKindLabel = ShouldShowKindLabel(node);
    const float kindLabelBlock = showKindLabel ? (metrics.kindLabelHeight * uiScale) + (2.0f * uiScale) : 0.0f;
    const float titleBlock = metrics.titleHeight * uiScale;
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
    const NodeLayoutMetrics metrics = MetricsForNode(node);
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
    const NodeLayoutMetrics metrics = MetricsForNode(node);
    const NodeFamilyStyle& familyStyle = StyleForFamily(FamilyForNode(node));
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
    const bool expanded = node.expanded;
    const float uiScale = NodeUiScaleFromZoom(m_Zoom);
    const float controlWidth = std::max(24.0f, layout->contentRect.max.x - layout->contentRect.min.x);
    const ImVec2 previewSize = NodePreviewSizeForScale(metrics, uiScale);
    const float pinRadius = PinRadiusForZoom(m_Zoom);
    const float frameRounding = std::max(4.0f, 8.0f * uiScale);
    const float borderThickness = selected ? std::max(1.4f, 2.0f * uiScale) : std::max(0.95f, 1.15f * uiScale);
    const float headerY = metrics.headerInsetY * uiScale;
    const float sectionGap = metrics.sectionGap * uiScale;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 fillColor = expanded
        ? familyStyle.fill
        : BlendColor(familyStyle.fill, ImVec4(0.10f, 0.12f, 0.13f, familyStyle.fill.w), 0.18f);
    const ImVec4 borderColor = selected
        ? BlendColor(familyStyle.accent, ImVec4(0.94f, 0.96f, 0.98f, 1.0f), 0.42f)
        : familyStyle.border;
    drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(fillColor), frameRounding);
    drawList->AddRect(min, max, ImGui::ColorConvertFloat4ToU32(borderColor), frameRounding, 0, borderThickness);
    const float accentHeight = std::max(2.0f, 3.0f * uiScale);
    drawList->AddRectFilled(
        ImVec2(min.x, min.y),
        ImVec2(max.x, min.y + accentHeight),
        ColorWithAlpha(familyStyle.accent, expanded ? 0.42f : 0.28f),
        frameRounding,
        ImDrawFlags_RoundCornersTop);

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
        const ImU32 baseColor = TypedSocketColor(socket.type, familyStyle);
        drawList->AddCircleFilled(pin, pinRadius, hoveredSocket ? IM_COL32(255, 255, 255, 255) : baseColor);
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
            case EditorNodeGraph::NodeKind::MaskUtility:
                return MaskUtilityLabel(node.maskUtilityKind);
            case EditorNodeGraph::NodeKind::ImageToMask:
                return "Luminance Mask";
            case EditorNodeGraph::NodeKind::ImageGenerator:
                return ImageGeneratorLabel(node.imageGeneratorKind);
            case EditorNodeGraph::NodeKind::Scope:
                return ScopeLabel(node.scopeKind);
            case EditorNodeGraph::NodeKind::Mix:
                return node.title.empty() ? "Mix" : node.title;
            case EditorNodeGraph::NodeKind::Preview:
                return node.title.empty() ? "Preview" : node.title;
            case EditorNodeGraph::NodeKind::Image:
            case EditorNodeGraph::NodeKind::Output:
            case EditorNodeGraph::NodeKind::Composite:
            case EditorNodeGraph::NodeKind::ExportBoundsSettings:
                return node.title.empty() ? NodeKindLabel(node.kind) : node.title;
        }
        return node.title.empty() ? NodeKindLabel(node.kind) : node.title;
    }();

    const ImVec4 frameBg = BlendColor(familyStyle.fill, ImVec4(0.12f, 0.14f, 0.15f, 1.0f), 0.34f);
    const ImVec4 frameBgHovered = BlendColor(frameBg, familyStyle.accent, 0.12f);
    const ImVec4 frameBgActive = BlendColor(frameBg, familyStyle.accent, 0.22f);
    const ImVec4 buttonBg = BlendColor(familyStyle.fill, familyStyle.accent, 0.18f);
    const ImVec4 buttonBgHovered = BlendColor(buttonBg, familyStyle.accent, 0.20f);
    const ImVec4 buttonBgActive = BlendColor(buttonBg, familyStyle.accent, 0.32f);
    const ImVec4 textMuted = BlendColor(familyStyle.mutedText, familyStyle.text, 0.18f);

    ImGui::PushID(node.id);
    const float titleFontSize = ImGui::GetFontSize() * uiScale;
    const ImVec2 headerTextMin(layout->contentRect.min.x, min.y + headerY);
    ImVec2 headerCursor = headerTextMin;
    if (ShouldShowKindLabel(node)) {
        const float kindFontSize = std::max(8.0f, ImGui::GetFontSize() * uiScale * 0.86f);
        drawList->AddText(
            ImGui::GetFont(),
            kindFontSize,
            headerCursor,
            ColorWithAlpha(textMuted, 0.96f),
            NodeKindLabel(node.kind));
        headerCursor.y += kindFontSize + (2.0f * uiScale);
    }
    const std::string displayTitle = EllipsizeLabel(nodePrimaryTitle, std::max(36.0f, controlWidth - (6.0f * uiScale)));
    drawList->AddText(
        ImGui::GetFont(),
        titleFontSize,
        headerCursor,
        ImGui::ColorConvertFloat4ToU32(familyStyle.text),
        displayTitle.c_str());

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(std::max(2.0f, 6.0f * uiScale), std::max(1.0f, 2.5f * uiScale)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(std::max(2.0f, metrics.itemGap * uiScale * 0.75f), std::max(2.0f, metrics.itemGap * uiScale * 0.58f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(std::max(2.0f, 4.0f * uiScale), std::max(1.0f, 2.0f * uiScale)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, std::max(2.0f, 5.0f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 999.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize, std::max(2.5f, 6.5f * uiScale));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, std::max(0.8f, 1.0f * uiScale));
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
    ImGui::SetWindowFontScale(uiScale);
    ImGui::PushItemWidth(controlWidth);
    ImGuiExtras::ResetNodeControlState();
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
            const nlohmann::json before = layers[node.layerIndex]->Serialize();
            layers[node.layerIndex]->RenderUI(editor);
            const nlohmann::json after = layers[node.layerIndex]->Serialize();
            if (before != after) {
                editor->MarkRenderDirty(node.id);
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
            ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        } else {
            ImGui::Dummy(previewSize);
            ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        }
        if (node.image.width > 0 && node.image.height > 0) {
            ImGui::TextDisabled("%d x %d", node.image.width, node.image.height);
        }
        ImGui::TextDisabled("%s", graph.GetActiveImageNodeId() == node.id ? "Active image" : "Unconnected image");
    } else if (node.kind == EditorNodeGraph::NodeKind::Output) {
        ImGui::TextDisabled("%s", graph.IsOutputConnected() ? "Connected to output chain" : "Output is not connected");
        ImGui::Dummy(ImVec2(0.0f, metrics.itemGap * uiScale * 0.55f));
        ImGui::BeginDisabled(!graph.IsOutputConnected() || editor->IsExportBusy());
        if (ImGui::Button("Export", ImVec2(controlWidth, 0.0f))) {
            const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
            if (!path.empty()) {
                editor->RequestExportImage(path);
            }
        }
        ImGui::EndDisabled();
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
    } else if (node.kind == EditorNodeGraph::NodeKind::ExportBoundsSettings) {
        auto& settings = editor->GetMutableCompositeExportSettings();
        const bool autoBounds = settings.boundsMode == EditorModule::CompositeExportBoundsMode::Auto;
        const bool hasCompositeContent = !editor->GetCompositeSceneItems().empty();
        ImGui::BeginDisabled(!hasCompositeContent);
        if (ImGui::RadioButton("Auto Bounds", autoBounds)) {
            settings.boundsMode = EditorModule::CompositeExportBoundsMode::Auto;
            editor->SyncCompositeExportResolutionFromWidth();
        }
        if (ImGui::RadioButton("Custom Bounds", !autoBounds)) {
            settings.boundsMode = EditorModule::CompositeExportBoundsMode::Custom;
            if (settings.aspectPreset == EditorModule::CompositeExportAspectPreset::Custom) {
                editor->UpdateCompositeCustomExportAspectFromBounds();
            } else {
                editor->SyncCompositeExportResolutionFromWidth();
            }
        }

        static const EditorModule::CompositeExportAspectPreset presets[] = {
            EditorModule::CompositeExportAspectPreset::Ratio1x1,
            EditorModule::CompositeExportAspectPreset::Ratio4x3,
            EditorModule::CompositeExportAspectPreset::Ratio3x2,
            EditorModule::CompositeExportAspectPreset::Ratio16x9,
            EditorModule::CompositeExportAspectPreset::Ratio9x16,
            EditorModule::CompositeExportAspectPreset::Ratio2x3,
            EditorModule::CompositeExportAspectPreset::Ratio5x4,
            EditorModule::CompositeExportAspectPreset::Ratio21x9,
            EditorModule::CompositeExportAspectPreset::Custom
        };
        ImGui::SetNextItemWidth(controlWidth);
        if (ImGui::BeginCombo("Aspect Ratio", ExportAspectPresetLabel(settings.aspectPreset))) {
            for (const auto preset : presets) {
                const bool selected = preset == settings.aspectPreset;
                if (ImGui::Selectable(ExportAspectPresetLabel(preset), selected)) {
                    settings.aspectPreset = preset;
                    if (settings.aspectPreset == EditorModule::CompositeExportAspectPreset::Custom) {
                        editor->UpdateCompositeCustomExportAspectFromBounds();
                    } else if (settings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom) {
                        const float ratio = editor->GetCurrentCompositeExportAspectRatio();
                        const float centerY = settings.customY + settings.customHeight * 0.5f;
                        settings.customHeight = std::max(1.0f, settings.customWidth / std::max(0.0001f, ratio));
                        settings.customY = centerY - settings.customHeight * 0.5f;
                    }
                    editor->SyncCompositeExportResolutionFromWidth();
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (settings.boundsMode == EditorModule::CompositeExportBoundsMode::Custom) {
            ImGui::TextDisabled("Toggle boundary edit with F.");
            ImGui::Text("Bounds: %.1f x %.1f", settings.customWidth, settings.customHeight);
            ImGui::TextDisabled("Origin: %.1f, %.1f", settings.customX, settings.customY);
            if (ImGui::Button("Use View As Export", ImVec2(controlWidth, 0.0f))) {
                editor->UseCompositeViewAsExportBounds(editor->GetLastCompositeCanvasSize());
            }
        } else {
            ImGui::TextDisabled("Auto bounds follow the visible composite objects.");
        }

        drawInlineSeparator();
        if (ImGui::RadioButton("Transparent Background", settings.backgroundMode == EditorModule::CompositeExportBackgroundMode::Transparent)) {
            settings.backgroundMode = EditorModule::CompositeExportBackgroundMode::Transparent;
        }
        if (ImGui::RadioButton("Solid Background", settings.backgroundMode == EditorModule::CompositeExportBackgroundMode::Solid)) {
            settings.backgroundMode = EditorModule::CompositeExportBackgroundMode::Solid;
        }
        if (settings.backgroundMode == EditorModule::CompositeExportBackgroundMode::Solid) {
            float color[3] = {
                settings.backgroundColor[0],
                settings.backgroundColor[1],
                settings.backgroundColor[2]
            };
            if (ImGuiExtras::NodeColorEdit3("Background Color", "##BackgroundColor", color, 0, controlWidth)) {
                settings.backgroundColor[0] = std::clamp(color[0], 0.0f, 1.0f);
                settings.backgroundColor[1] = std::clamp(color[1], 0.0f, 1.0f);
                settings.backgroundColor[2] = std::clamp(color[2], 0.0f, 1.0f);
                settings.backgroundColor[3] = 1.0f;
            }
            captureIfActive();
        }

        drawInlineSeparator();
        const bool widthChanged = ImGuiExtras::NodeInputInt("Output Width", "##OutputWidth", &settings.outputWidth, 1, 100, controlWidth);
        captureIfActive();
        const bool heightChanged = ImGuiExtras::NodeInputInt("Output Height", "##OutputHeight", &settings.outputHeight, 1, 100, controlWidth);
        captureIfActive();
        if (widthChanged) {
            editor->SyncCompositeExportResolutionFromWidth();
        } else if (heightChanged) {
            editor->SyncCompositeExportResolutionFromHeight();
        }

        if (ImGui::Button("Export PNG", ImVec2(controlWidth, 0.0f))) {
            const std::string path = FileDialogs::SavePngFileDialog("Export Composite Image", "composite_output.png");
            if (!path.empty()) {
                editor->RequestExportImage(path);
            }
        }
        ImGui::EndDisabled();
        if (!hasCompositeContent) {
            ImGui::TextDisabled("Add at least two completed chains to export the composite.");
        }
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
        changed |= renderSlider("Low", "##LumLow", &node.imageToMaskSettings.low, 0.0f, 1.0f);
        changed |= renderSlider("High", "##LumHigh", &node.imageToMaskSettings.high, 0.0f, 1.0f);
        changed |= renderSlider("Softness", "##LumSoftness", &node.imageToMaskSettings.softness, 0.0f, 0.5f);
        changed |= ImGuiExtras::NodeCheckbox("Invert", "##ImageToMaskInvert", &node.imageToMaskSettings.invert, controlWidth);
        captureIfActive();
        if (changed) {
            editor->MarkRenderDirty(node.id);
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::ImageGenerator) {
        bool changed = false;
        changed |= ImGuiExtras::NodeColorEdit4("A", "##GeneratorColorA", node.imageGeneratorSettings.colorA, ImGuiColorEditFlags_NoInputs, controlWidth);
        captureIfActive();
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
        const char* modes[] = { "Normal / Lerp", "Add", "Multiply", "Screen", "Alpha Over" };
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
            } else {
                ImGui::Dummy(previewSize);
                ImGui::TextDisabled("Preview unavailable");
            }
        } else {
            ImGui::TextDisabled("No input");
            ImGui::Dummy(previewSize);
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
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleColor(15);
    ImGui::PopStyleVar(7);
    ImGui::PopID();
}

void EditorNodeGraphUI::RenderLinks(const EditorNodeGraph::Graph& graph) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float thicknessScale = std::max(0.7f, NodeUiScaleFromZoom(m_Zoom));
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        const EditorNodeGraph::Node* from = graph.FindNode(link.fromNodeId);
        const EditorNodeGraph::Node* to = graph.FindNode(link.toNodeId);
        if (!from || !to) {
            continue;
        }

        const ImVec2 p1 = ToImVec2(OutputPinScreenPos(*from, link.fromSocketId));
        const ImVec2 p2 = ToImVec2(InputPinScreenPos(*to, link.toSocketId));
        const float handle = std::max(60.0f, (p2.x - p1.x) * 0.45f);
        const bool selected = graph.GetSelectedLink() &&
            graph.GetSelectedLink()->fromNodeId == link.fromNodeId &&
            graph.GetSelectedLink()->fromSocketId == link.fromSocketId &&
            graph.GetSelectedLink()->toNodeId == link.toNodeId &&
            graph.GetSelectedLink()->toSocketId == link.toSocketId;
        const bool scopeLink = graph.GetLinkRole(link) == EditorNodeGraph::LinkRole::Scope;
        const bool maskLink = link.fromSocketId == EditorNodeGraph::kMaskOutputSocketId ||
            link.toSocketId == EditorNodeGraph::kMaskInputSocketId ||
            link.toSocketId == EditorNodeGraph::kMixFactorSocketId;
        drawList->AddBezierCubic(
            p1,
            ImVec2(p1.x + handle, p1.y),
            ImVec2(p2.x - handle, p2.y),
            p2,
            selected ? IM_COL32(255, 255, 255, 255) : (maskLink ? IM_COL32(130, 230, 170, 230) : (scopeLink ? IM_COL32(130, 230, 170, 230) : IM_COL32(120, 170, 255, 230))),
            (selected ? 4.5f : 3.0f) * thicknessScale);
    }
}

void EditorNodeGraphUI::RenderInteraction(EditorModule* editor, const EditorNodeGraph::Graph& graph) {
    if (editor->IsAdvancedEditorOpen()) {
        m_NodeContentActive = false;
        m_NodeContentHovered = false;
        m_GraphInteractionBlocked = true;
        m_MouseOwner = GraphMouseOwner::Popup;
        m_DragNodeId = -1;
        m_BoxSelecting = false;
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
    const bool additiveSelect = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
    if (ImGui::GetIO().KeyCtrl && ImGui::GetIO().KeyAlt && ImGui::IsKeyPressed(ImGuiKey_G, false)) {
        m_DebugInteractionOverlay = !m_DebugInteractionOverlay;
    }

    if (m_NodeBrowserOpen) {
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

    if (graphHovered && !anyPopupOpen && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        editor->CancelGraphAutoFocusTracking();
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_Pan.x += delta.x;
        m_Pan.y += delta.y;
        return;
    }

    if (m_MouseOwner == GraphMouseOwner::OutputPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DragOutputNodeId = m_HoveredOutputNodeId;
        m_DragOutputSocketId = m_HoveredOutputSocketId;
    }
    if (m_MouseOwner == GraphMouseOwner::InputPin && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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
            const ImVec2 p1 = ToImVec2(OutputPinScreenPos(*from, m_DragOutputSocketId));
            const ImVec2 p2 = ImGui::GetMousePos();
            const float handle = std::max(60.0f, (p2.x - p1.x) * 0.45f);
            drawList->AddBezierCubic(p1, ImVec2(p1.x + handle, p1.y), ImVec2(p2.x - handle, p2.y), p2, IM_COL32(255, 255, 255, 210), std::max(1.2f, 2.5f * NodeUiScaleFromZoom(m_Zoom)));
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredInput.IsValid()) {
                std::string error;
                if (!editor->ConnectGraphSockets(m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId, &error)) {
                    m_StatusMessage = error;
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
            const ImVec2 p1 = ImGui::GetMousePos();
            const ImVec2 p2 = ToImVec2(InputPinScreenPos(*to, m_DragInputSocketId));
            const float handle = std::max(60.0f, (p2.x - p1.x) * 0.45f);
            drawList->AddBezierCubic(p1, ImVec2(p1.x + handle, p1.y), ImVec2(p2.x - handle, p2.y), p2, IM_COL32(255, 255, 255, 210), std::max(1.2f, 2.5f * NodeUiScaleFromZoom(m_Zoom)));
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredOutput.IsValid()) {
                std::string error;
                if (!editor->ConnectGraphSockets(hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId, &error)) {
                    m_StatusMessage = error;
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

    if (m_MouseOwner == GraphMouseOwner::NodeHeader && hoveredNodeId > 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(hoveredNodeId);
        if (node) {
            if (node->kind == EditorNodeGraph::NodeKind::Layer && editor->LayerSupportsAdvancedEditor(node->layerIndex)) {
                editor->OpenAdvancedEditorForNode(node->id);
                return;
            }
            const bool expanding = !node->expanded;
            node->expanded = !node->expanded;
            editor->SelectGraphNode(node->id);
            if (expanding) {
                editor->RequestGraphNodeAutoFocus(node->id, node->position, NodeSize(*node), m_Pan.x, m_Pan.y, m_Zoom);
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
            const float left = std::min(m_BoxSelectStart.x, m_BoxSelectCurrent.x);
            const float right = std::max(m_BoxSelectStart.x, m_BoxSelectCurrent.x);
            const float top = std::min(m_BoxSelectStart.y, m_BoxSelectCurrent.y);
            const float bottom = std::max(m_BoxSelectStart.y, m_BoxSelectCurrent.y);
            for (const EditorNodeGraph::Node& node : mutableGraph.GetNodes()) {
                const EditorNodeGraph::Vec2 size = NodeSize(node);
                const bool overlaps =
                    node.position.x <= right &&
                    node.position.x + size.x >= left &&
                    node.position.y <= bottom &&
                    node.position.y + size.y >= top;
                if (overlaps && !mutableGraph.IsNodeSelected(node.id)) {
                    mutableGraph.SelectNode(node.id, true);
                }
            }
            m_BoxSelecting = false;
        }
        return;
    }

    if (ownerIsNode && hoveredNodeId > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
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

    const bool graphWindowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
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
                    NodeSize(*selectedNode),
                    m_Pan.x,
                    m_Pan.y,
                    m_Zoom);
            }
            m_StatusMessage.clear();
        }
    }

    if (editor->CanConsumeEditorCommandKeys() &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        if (editor->DeleteSelectedGraphLink()) {
            m_StatusMessage = "Link deleted.";
        } else if (editor->DeleteSelectedGraphNodes()) {
            m_StatusMessage = "Node deleted.";
        }
    }
}

void EditorNodeGraphUI::RenderValidationStatus(const EditorNodeGraph::Graph& graph) {
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
