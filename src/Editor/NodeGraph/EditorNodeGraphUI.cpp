#include "EditorNodeGraphUI.h"

#include "Editor/EditorModule.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include <algorithm>
#include <cctype>
#include <cmath>
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

const char* NodeKindLabel(EditorNodeGraph::NodeKind kind) {
    switch (kind) {
        case EditorNodeGraph::NodeKind::Image: return "Image";
        case EditorNodeGraph::NodeKind::Layer: return "Layer";
        case EditorNodeGraph::NodeKind::Output: return "Output";
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

size_t HashImagePayload(const EditorNodeGraph::Node& node) {
    size_t hash = 1469598103934665603ull;
    auto mix = [&](size_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };

    mix(static_cast<size_t>(node.image.width));
    mix(static_cast<size_t>(node.image.height));
    mix(static_cast<size_t>(std::max(1, node.image.channels)));
    mix(node.image.pixels.size());
    if (!node.image.pixels.empty()) {
        mix(node.image.pixels.front());
        mix(node.image.pixels[node.image.pixels.size() / 2]);
        mix(node.image.pixels.back());
    }
    return hash;
}

} // namespace

EditorNodeGraphUI::~EditorNodeGraphUI() {
    for (auto& item : m_ImagePreviewTextures) {
        unsigned int texture = item.second;
        if (texture) {
            glDeleteTextures(1, &texture);
        }
    }
    for (auto& item : m_GraphPreviewTextures) {
        unsigned int texture = item.second;
        if (texture) {
            glDeleteTextures(1, &texture);
        }
    }
}

void EditorNodeGraphUI::Initialize() {}

void EditorNodeGraphUI::Render(EditorModule* editor) {
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    m_NodeContentActive = false;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_NodeControlCapture = false;
    }

    ImGui::TextDisabled("Drag pins to connect. Right-click graph space to add images, layers, or scopes.");
    ImGui::Separator();

    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 canvasSize = ImVec2(std::max(320.0f, available.x), std::max(320.0f, available.y));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("EditorNodeGraphCanvas", canvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const ImVec2 canvasMin = ImGui::GetItemRectMin();
    const ImVec2 canvasMax = ImGui::GetItemRectMax();
    m_CanvasOrigin = ToGraphVec2(canvasMin);
    m_CanvasMin = ToGraphVec2(canvasMin);
    m_CanvasMax = ToGraphVec2(canvasMax);
    editor->SetGraphDropTargetRect(canvasMin.x, canvasMin.y, canvasMax.x, canvasMax.y);
    editor->SetGraphViewTransform(canvasMin.x, canvasMin.y, m_Pan.x, m_Pan.y, m_Zoom);
    const bool graphHovered = ImGui::IsMouseHoveringRect(canvasMin, canvasMax);
    if (graphHovered && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::GetIO().MouseWheel != 0.0f) {
        ZoomAtMouse(ImGui::GetIO().MouseWheel);
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasMin, canvasMax, true);
    drawList->AddRectFilled(canvasMin, canvasMax, IM_COL32(24, 25, 28, 255));

    const float gridStep = std::max(8.0f, 32.0f * m_Zoom);
    const ImU32 gridColor = IM_COL32(55, 58, 64, 120);
    for (float x = std::fmod(m_Pan.x, gridStep); x < canvasSize.x; x += gridStep) {
        drawList->AddLine(ImVec2(canvasMin.x + x, canvasMin.y), ImVec2(canvasMin.x + x, canvasMax.y), gridColor);
    }
    for (float y = std::fmod(m_Pan.y, gridStep); y < canvasSize.y; y += gridStep) {
        drawList->AddLine(ImVec2(canvasMin.x, canvasMin.y + y), ImVec2(canvasMax.x, canvasMin.y + y), gridColor);
    }

    if (graphHovered && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_Pan.x += delta.x;
        m_Pan.y += delta.y;
    }
    ClampPanToContent(graph);

    RenderLinks(graph);

    for (EditorNodeGraph::Node& node : graph.GetNodes()) {
        RenderNode(editor, node);
    }

    RenderInteraction(editor, graph);
    RenderValidationStatus(graph);
    RenderContextMenu(editor);
    drawList->PopClipRect();
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::ScreenToGraph(const EditorNodeGraph::Vec2& screen) const {
    return EditorNodeGraph::Vec2{
        (screen.x - m_CanvasOrigin.x - m_Pan.x) / m_Zoom,
        (screen.y - m_CanvasOrigin.y - m_Pan.y) / m_Zoom
    };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::GraphToScreen(const EditorNodeGraph::Vec2& graph) const {
    return EditorNodeGraph::Vec2{
        m_CanvasOrigin.x + m_Pan.x + graph.x * m_Zoom,
        m_CanvasOrigin.y + m_Pan.y + graph.y * m_Zoom
    };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeSize(const EditorNodeGraph::Node& node) const {
    if (node.kind == EditorNodeGraph::NodeKind::Layer && node.expanded) {
        return EditorNodeGraph::Vec2{ 330.0f, 380.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::Image) {
        return EditorNodeGraph::Vec2{ 210.0f, 104.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::Output) {
        return EditorNodeGraph::Vec2{ 190.0f, 82.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::Scope) {
        return EditorNodeGraph::Vec2{ 280.0f, 260.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator) {
        if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::Solid) {
            return EditorNodeGraph::Vec2{ 260.0f, 126.0f };
        }
        if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
            return EditorNodeGraph::Vec2{ 270.0f, 210.0f };
        }
        return EditorNodeGraph::Vec2{ 270.0f, 246.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::Mix) {
        return EditorNodeGraph::Vec2{ 250.0f, node.expanded ? 170.0f : 96.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::Preview) {
        return EditorNodeGraph::Vec2{ 240.0f, 178.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::MaskUtility) {
        return EditorNodeGraph::Vec2{ 270.0f, node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert ? 96.0f : 178.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::ImageToMask) {
        return EditorNodeGraph::Vec2{ 270.0f, 180.0f };
    }
    if (node.kind == EditorNodeGraph::NodeKind::ImageGenerator) {
        return EditorNodeGraph::Vec2{ 270.0f, node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::SolidColor ? 126.0f : 178.0f };
    }
    return EditorNodeGraph::Vec2{ 210.0f, 82.0f };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::NodeScreenSize(const EditorNodeGraph::Node& node) const {
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    return EditorNodeGraph::Vec2{ size.x * m_Zoom, size.y * m_Zoom };
}

void EditorNodeGraphUI::ZoomAtMouse(float wheel) {
    const EditorNodeGraph::Vec2 mouseScreen = ToGraphVec2(ImGui::GetMousePos());
    const EditorNodeGraph::Vec2 before = ScreenToGraph(mouseScreen);
    const float oldZoom = m_Zoom;
    m_Zoom = std::clamp(m_Zoom * (wheel > 0.0f ? 1.12f : 1.0f / 1.12f), 0.35f, 2.5f);
    if (std::abs(m_Zoom - oldZoom) < 0.0001f) {
        return;
    }
    m_Pan.x = mouseScreen.x - m_CanvasOrigin.x - before.x * m_Zoom;
    m_Pan.y = mouseScreen.y - m_CanvasOrigin.y - before.y * m_Zoom;
}

void EditorNodeGraphUI::ClampPanToContent(const EditorNodeGraph::Graph& graph) {
    if (graph.GetNodes().empty()) {
        return;
    }

    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    bool first = true;
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        const EditorNodeGraph::Vec2 size = NodeSize(node);
        if (first) {
            minX = node.position.x;
            minY = node.position.y;
            maxX = node.position.x + size.x;
            maxY = node.position.y + size.y;
            first = false;
        } else {
            minX = std::min(minX, node.position.x);
            minY = std::min(minY, node.position.y);
            maxX = std::max(maxX, node.position.x + size.x);
            maxY = std::max(maxY, node.position.y + size.y);
        }
    }

    const float margin = 360.0f;
    const float canvasW = std::max(1.0f, m_CanvasMax.x - m_CanvasMin.x);
    const float canvasH = std::max(1.0f, m_CanvasMax.y - m_CanvasMin.y);
    const float minPanX = canvasW - (maxX + margin) * m_Zoom;
    const float maxPanX = (-minX + margin) * m_Zoom;
    const float minPanY = canvasH - (maxY + margin) * m_Zoom;
    const float maxPanY = (-minY + margin) * m_Zoom;
    m_Pan.x = std::clamp(m_Pan.x, std::min(minPanX, maxPanX), std::max(minPanX, maxPanX));
    m_Pan.y = std::clamp(m_Pan.y, std::min(minPanY, maxPanY), std::max(minPanY, maxPanY));
}

void EditorNodeGraphUI::RenderContextMenu(EditorModule* editor) {
    if (!ImGui::BeginPopup("EditorNodeGraphContextMenu")) {
        return;
    }

    if (m_ContextTarget == ContextTarget::Node) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(m_ContextNodeId);
        if (node) {
            ImGui::TextDisabled("%s", node->title.c_str());
            if (node->kind != EditorNodeGraph::NodeKind::Output && ImGui::MenuItem("Delete Node")) {
                editor->RemoveGraphNode(node->id);
            }
            if (node->kind == EditorNodeGraph::NodeKind::Layer && ImGui::MenuItem(node->expanded ? "Collapse" : "Expand")) {
                node->expanded = !node->expanded;
            }
            if (node->kind == EditorNodeGraph::NodeKind::Output) {
                ImGui::BeginDisabled(!editor->GetNodeGraph().IsOutputConnected() || editor->IsExportBusy());
                if (ImGui::MenuItem("Export")) {
                    const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
                    if (!path.empty()) {
                        editor->RequestExportImage(path);
                    }
                }
                ImGui::EndDisabled();
            }
        }
        ImGui::EndPopup();
        return;
    }

    if (m_ContextTarget == ContextTarget::Link) {
        if (ImGui::MenuItem("Delete Link")) {
            editor->RemoveGraphLink(m_ContextLink.fromNodeId, m_ContextLink.fromSocketId, m_ContextLink.toNodeId, m_ContextLink.toSocketId);
        }
        ImGui::EndPopup();
        return;
    }

    if (ImGui::BeginMenu("Add")) {
        if (ImGui::MenuItem("Add Image")) {
            editor->PromptAddImageNodeAt(m_ContextGraphPos);
        }

        if (ImGui::BeginMenu("Add Node")) {
            ImGui::SetNextItemWidth(240.0f);
            if (ImGui::IsWindowAppearing()) {
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##LayerSearch", "Search layers", m_SearchBuffer, sizeof(m_SearchBuffer));
            ImGui::Separator();

            std::string currentCategory;
            const std::string search = m_SearchBuffer;
            for (const LayerDescriptor& descriptor : LayerRegistry::GetAllDescriptors()) {
                const std::string label = descriptor.displayName ? descriptor.displayName : "";
                const std::string category = descriptor.categoryName ? descriptor.categoryName : "";
                if (!ContainsCaseInsensitive(label, search) && !ContainsCaseInsensitive(category, search)) {
                    continue;
                }

                if (category != currentCategory) {
                    currentCategory = category;
                    ImGui::TextDisabled("%s", currentCategory.c_str());
                }

                if (ImGui::MenuItem(label.c_str())) {
                    editor->AddLayerNodeAt(descriptor.type, m_ContextGraphPos);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scopes")) {
            if (ImGui::MenuItem("Histogram")) {
                editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::Histogram, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Vectorscope")) {
                editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::Vectorscope, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("RGB Parade")) {
                editor->AddScopeNodeAt(EditorNodeGraph::ScopeKind::RGBParade, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Analysis")) {
            if (ImGui::MenuItem("Preview")) {
                editor->AddPreviewNodeAt(m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mask")) {
            if (ImGui::MenuItem("Solid Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::Solid, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Linear Gradient Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::LinearGradient, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Radial Gradient Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::RadialGradient, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Noise Mask")) {
                editor->AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind::Noise, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Mask Utility")) {
            if (ImGui::MenuItem("Invert Mask")) {
                editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Invert, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Levels Mask")) {
                editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Levels, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Threshold Mask")) {
                editor->AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind::Threshold, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Generator")) {
            if (ImGui::MenuItem("Luminance Mask")) {
                editor->AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind::Luminance, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Solid Color Image")) {
                editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::SolidColor, m_ContextGraphPos);
            }
            if (ImGui::MenuItem("Color Gradient Image")) {
                editor->AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind::ColorGradient, m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Merge")) {
            if (ImGui::MenuItem("Mix")) {
                editor->AddMixNodeAt(m_ContextGraphPos);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Auto Layout")) {
            editor->AutoLayoutGraph();
        }
        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}

void EditorNodeGraphUI::RenderNode(EditorModule* editor, EditorNodeGraph::Node& node) {
    EditorNodeGraph::Graph& graph = editor->GetNodeGraph();
    const EditorNodeGraph::Vec2 nodeSize = NodeScreenSize(node);
    const EditorNodeGraph::Vec2 nodeScreenPos = GraphToScreen(node.position);
    const ImVec2 min = ToImVec2(nodeScreenPos);
    const ImVec2 max = ImVec2(min.x + nodeSize.x, min.y + nodeSize.y);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const bool selected = graph.IsNodeSelected(node.id);
    ImU32 fill = IM_COL32(40, 42, 48, 245);
    if (node.kind == EditorNodeGraph::NodeKind::Image) fill = IM_COL32(42, 54, 64, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::Output) fill = IM_COL32(58, 48, 42, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::Scope) fill = IM_COL32(38, 48, 44, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator) fill = IM_COL32(40, 56, 50, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::Mix) fill = IM_COL32(52, 45, 62, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::Preview) fill = IM_COL32(45, 52, 62, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::MaskUtility) fill = IM_COL32(38, 58, 48, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::ImageToMask) fill = IM_COL32(38, 54, 58, 245);
    else if (node.kind == EditorNodeGraph::NodeKind::ImageGenerator) fill = IM_COL32(58, 52, 38, 245);
    drawList->AddRectFilled(min, max, fill, 7.0f);
    drawList->AddRect(min, max, selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(95, 100, 112, 255), 7.0f, 0, selected ? 2.5f : 1.0f);

    const bool hovered = ImGui::IsMouseHoveringRect(min, max);
    ImGuiContext* imguiContext = ImGui::GetCurrentContext();
    const ImGuiID activeIdBeforeNode = imguiContext ? imguiContext->ActiveId : 0;

    for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
        const bool isInput = socket.direction == EditorNodeGraph::SocketDirection::Input;
        const EditorNodeGraph::Vec2 pin = isInput ? InputPinScreenPos(node, socket.id) : OutputPinScreenPos(node, socket.id);
        const bool hoveredSocket = isInput
            ? (m_HoveredInputNodeId == node.id && m_HoveredInputSocketId == socket.id)
            : (m_HoveredOutputNodeId == node.id && m_HoveredOutputSocketId == socket.id);
        const ImU32 baseColor = socket.type == EditorNodeGraph::SocketType::Mask
            ? IM_COL32(130, 240, 170, 255)
            : (isInput ? IM_COL32(145, 180, 255, 255) : IM_COL32(145, 255, 190, 255));
        drawList->AddCircleFilled(ToImVec2(pin), 6.0f, hoveredSocket ? IM_COL32(255, 255, 255, 255) : baseColor);
        const ImVec2 textSize = ImGui::CalcTextSize(socket.label.c_str());
        const ImVec2 labelPos = isInput
            ? ImVec2(pin.x + 10.0f, pin.y - textSize.y * 0.5f)
            : ImVec2(pin.x - 10.0f - textSize.x, pin.y - textSize.y * 0.5f);
        drawList->AddText(labelPos, IM_COL32(215, 220, 230, 220), socket.label.c_str());
    }

    ImGui::SetCursorScreenPos(ImVec2(min.x + 12.0f, min.y + 10.0f));
    ImGui::PushID(node.id);
    drawList->PushClipRect(ImVec2(min.x + 8.0f, min.y + 8.0f), ImVec2(max.x - 8.0f, max.y - 8.0f), true);
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", NodeKindLabel(node.kind));

    if (node.kind == EditorNodeGraph::NodeKind::Layer) {
        ImGui::Text("%d. %.28s%s", node.layerIndex + 1, node.title.c_str(), node.title.size() > 28 ? "..." : "");
        if (node.expanded) {
            ImGui::Separator();
            auto& layers = editor->GetLayers();
            if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(layers.size())) {
                ImGui::BeginChild("LayerSettings", ImVec2(nodeSize.x - 28.0f, nodeSize.y - 92.0f), false);
                layers[node.layerIndex]->RenderUI(editor);
                ImGui::EndChild();
            }
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Image) {
        ImGui::Text("%.28s%s", node.title.c_str(), node.title.size() > 28 ? "..." : "");
        unsigned int texture = GetImagePreviewTexture(node);
        if (texture != 0) {
            ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(64.0f, 48.0f), ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SameLine();
            ImGui::BeginGroup();
        }
        if (node.image.width > 0 && node.image.height > 0) {
            ImGui::TextDisabled("%d x %d", node.image.width, node.image.height);
        }
        const bool active = graph.GetActiveImageNodeId() == node.id;
        ImGui::TextDisabled("%s", active ? "Active image" : "Unconnected image");
        if (texture != 0) {
            ImGui::EndGroup();
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Output) {
        ImGui::Text("%s", node.title.c_str());
        ImGui::TextDisabled(graph.IsOutputConnected() ? "Connected" : "Not connected");
        ImGui::BeginDisabled(!graph.IsOutputConnected() || editor->IsExportBusy());
        if (ImGui::Button("Export")) {
            const std::string path = FileDialogs::SavePngFileDialog("Export Rendered Image", "rendered_output.png");
            if (!path.empty()) {
                editor->RequestExportImage(path);
            }
        }
        ImGui::EndDisabled();
    } else if (node.kind == EditorNodeGraph::NodeKind::Scope) {
        ImGui::Text("%s", ScopeLabel(node.scopeKind));
        const EditorNodeGraph::Link* input = graph.FindScopeInputLink(node.id);
        if (input) {
            const EditorNodeGraph::Node* from = graph.FindNode(input->fromNodeId);
            ImGui::TextDisabled("Input: %s", from ? from->title.c_str() : "Missing");
        } else {
            ImGui::TextDisabled("No input");
        }
        ImGui::Separator();
        ImGui::BeginChild("ScopeNodeView", ImVec2(nodeSize.x - 24.0f, nodeSize.y - 72.0f), false);
        editor->RenderGraphScopeNode(node.scopeKind, input ? input->fromNodeId : -1);
        ImGui::EndChild();
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskGenerator) {
        ImGui::Text("%s", MaskLabel(node.maskKind));
        bool changed = false;
        const float controlWidth = std::max(120.0f, nodeSize.x - 34.0f);
        auto captureIfActive = [&]() {
            if (ImGui::IsItemActive()) {
                m_NodeContentActive = true;
                m_NodeControlCapture = true;
            }
        };
        if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::Solid) {
            ImGui::TextDisabled("Value");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##SolidValue", &node.maskSettings.value, 0.0f, 1.0f);
            captureIfActive();
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
            ImGui::TextDisabled("Angle");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##LinearAngle", &node.maskSettings.angle, -180.0f, 180.0f);
            captureIfActive();
            ImGui::TextDisabled("Offset");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##LinearOffset", &node.maskSettings.offset, -1.0f, 1.0f);
            captureIfActive();
            ImGui::TextDisabled("Scale");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##LinearScale", &node.maskSettings.scale, 0.1f, 4.0f);
            captureIfActive();
            changed |= ImGui::Checkbox("Invert", &node.maskSettings.invert);
            captureIfActive();
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient) {
            ImGui::TextDisabled("Center X");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##RadialCenterX", &node.maskSettings.centerX, 0.0f, 1.0f);
            captureIfActive();
            ImGui::TextDisabled("Center Y");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##RadialCenterY", &node.maskSettings.centerY, 0.0f, 1.0f);
            captureIfActive();
            ImGui::TextDisabled("Radius");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##RadialRadius", &node.maskSettings.radius, 0.01f, 1.5f);
            captureIfActive();
            ImGui::TextDisabled("Feather");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##RadialFeather", &node.maskSettings.feather, 0.001f, 1.0f);
            captureIfActive();
            changed |= ImGui::Checkbox("Invert", &node.maskSettings.invert);
            captureIfActive();
        } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::Noise) {
            ImGui::TextDisabled("Scale");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##NoiseScale", &node.maskSettings.scale, 0.05f, 8.0f);
            captureIfActive();
            ImGui::TextDisabled("Contrast");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##NoiseContrast", &node.maskSettings.value, 0.0f, 2.0f);
            captureIfActive();
            ImGui::TextDisabled("Seed");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##NoiseSeed", &node.maskSettings.offset, 0.0f, 100.0f);
            captureIfActive();
            changed |= ImGui::Checkbox("Invert", &node.maskSettings.invert);
            captureIfActive();
        }
        if (changed) {
            editor->MarkRenderDirty();
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::MaskUtility) {
        ImGui::Text("%s", MaskUtilityLabel(node.maskUtilityKind));
        bool changed = false;
        const float controlWidth = std::max(120.0f, nodeSize.x - 34.0f);
        auto captureIfActive = [&]() {
            if (ImGui::IsItemActive()) {
                m_NodeContentActive = true;
                m_NodeControlCapture = true;
            }
        };
        if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Invert) {
            ImGui::TextDisabled("Mask values are inverted.");
        } else if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Levels) {
            ImGui::TextDisabled("Black");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##LevelsBlack", &node.maskUtilitySettings.blackPoint, 0.0f, 1.0f);
            captureIfActive();
            ImGui::TextDisabled("White");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##LevelsWhite", &node.maskUtilitySettings.whitePoint, 0.0f, 1.0f);
            captureIfActive();
            ImGui::TextDisabled("Gamma");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##LevelsGamma", &node.maskUtilitySettings.gamma, 0.1f, 4.0f);
            captureIfActive();
            changed |= ImGui::Checkbox("Invert", &node.maskUtilitySettings.invert);
            captureIfActive();
        } else if (node.maskUtilityKind == EditorNodeGraph::MaskUtilityKind::Threshold) {
            ImGui::TextDisabled("Threshold");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##Threshold", &node.maskUtilitySettings.threshold, 0.0f, 1.0f);
            captureIfActive();
            ImGui::TextDisabled("Softness");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##ThresholdSoftness", &node.maskUtilitySettings.softness, 0.0f, 0.5f);
            captureIfActive();
            changed |= ImGui::Checkbox("Invert", &node.maskUtilitySettings.invert);
            captureIfActive();
        }
        if (changed) {
            editor->MarkRenderDirty();
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::ImageToMask) {
        ImGui::Text("Luminance Mask");
        bool changed = false;
        const float controlWidth = std::max(120.0f, nodeSize.x - 34.0f);
        auto captureIfActive = [&]() {
            if (ImGui::IsItemActive()) {
                m_NodeContentActive = true;
                m_NodeControlCapture = true;
            }
        };
        ImGui::TextDisabled("Low");
        ImGui::SetNextItemWidth(controlWidth);
        changed |= ImGui::SliderFloat("##LumLow", &node.imageToMaskSettings.low, 0.0f, 1.0f);
        captureIfActive();
        ImGui::TextDisabled("High");
        ImGui::SetNextItemWidth(controlWidth);
        changed |= ImGui::SliderFloat("##LumHigh", &node.imageToMaskSettings.high, 0.0f, 1.0f);
        captureIfActive();
        ImGui::TextDisabled("Softness");
        ImGui::SetNextItemWidth(controlWidth);
        changed |= ImGui::SliderFloat("##LumSoftness", &node.imageToMaskSettings.softness, 0.0f, 0.5f);
        captureIfActive();
        changed |= ImGui::Checkbox("Invert", &node.imageToMaskSettings.invert);
        captureIfActive();
        if (changed) {
            editor->MarkRenderDirty();
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::ImageGenerator) {
        ImGui::Text("%s", ImageGeneratorLabel(node.imageGeneratorKind));
        bool changed = false;
        const float controlWidth = std::max(120.0f, nodeSize.x - 34.0f);
        ImGui::SetNextItemWidth(controlWidth);
        changed |= ImGui::ColorEdit4("A", node.imageGeneratorSettings.colorA, ImGuiColorEditFlags_NoInputs);
        if (ImGui::IsItemActive()) { m_NodeContentActive = true; m_NodeControlCapture = true; }
        if (node.imageGeneratorKind == EditorNodeGraph::ImageGeneratorKind::ColorGradient) {
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::ColorEdit4("B", node.imageGeneratorSettings.colorB, ImGuiColorEditFlags_NoInputs);
            if (ImGui::IsItemActive()) { m_NodeContentActive = true; m_NodeControlCapture = true; }
            ImGui::TextDisabled("Angle");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##GradientAngle", &node.imageGeneratorSettings.angle, -180.0f, 180.0f);
            if (ImGui::IsItemActive()) { m_NodeContentActive = true; m_NodeControlCapture = true; }
            ImGui::TextDisabled("Offset");
            ImGui::SetNextItemWidth(controlWidth);
            changed |= ImGui::SliderFloat("##GradientOffset", &node.imageGeneratorSettings.offset, -1.0f, 1.0f);
            if (ImGui::IsItemActive()) { m_NodeContentActive = true; m_NodeControlCapture = true; }
        }
        if (changed) {
            editor->MarkRenderDirty();
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Mix) {
        ImGui::Text("%s", node.title.empty() ? "Mix" : node.title.c_str());
        ImGui::TextDisabled("Blend: %s", MixBlendLabel(node.mixBlendMode));
        bool changed = false;
        if (node.expanded) {
            ImGui::Separator();
            int mode = static_cast<int>(node.mixBlendMode);
            const char* modes[] = { "Normal / Lerp", "Add", "Multiply", "Screen", "Alpha Over" };
            ImGui::SetNextItemWidth(std::max(120.0f, nodeSize.x - 34.0f));
            if (ImGui::Combo("##MixBlendMode", &mode, modes, IM_ARRAYSIZE(modes))) {
                node.mixBlendMode = static_cast<EditorNodeGraph::MixBlendMode>(mode);
                changed = true;
            }
            if (ImGui::IsItemActive()) {
                m_NodeContentActive = true;
                m_NodeControlCapture = true;
            }
            ImGui::TextDisabled("Factor");
            ImGui::SetNextItemWidth(std::max(120.0f, nodeSize.x - 34.0f));
            changed |= ImGui::SliderFloat("##MixFactor", &node.mixFactor, 0.0f, 1.0f);
            if (ImGui::IsItemActive()) {
                m_NodeContentActive = true;
                m_NodeControlCapture = true;
            }
        }
        if (changed) {
            editor->MarkRenderDirty();
        }
    } else if (node.kind == EditorNodeGraph::NodeKind::Preview) {
        ImGui::Text("%s", node.title.empty() ? "Preview" : node.title.c_str());
        const EditorNodeGraph::Link* input = graph.FindAnyInputLink(node.id, EditorNodeGraph::kPreviewInputSocketId);
        if (input) {
            const EditorNodeGraph::Node* from = graph.FindNode(input->fromNodeId);
            ImGui::TextDisabled("Input: %s", from ? from->title.c_str() : "Missing");
            unsigned int texture = GetGraphPreviewTexture(editor, node);
            if (texture != 0) {
                ImGui::Image((ImTextureID)(intptr_t)texture, ImVec2(std::max(96.0f, nodeSize.x - 28.0f), 92.0f), ImVec2(0, 1), ImVec2(1, 0));
            } else {
                ImGui::TextDisabled("Preview unavailable");
            }
        } else {
            ImGui::TextDisabled("No input");
            ImGui::Dummy(ImVec2(std::max(96.0f, nodeSize.x - 28.0f), 92.0f));
        }
    }
    ImGui::EndGroup();
    const ImGuiID activeIdAfterNode = imguiContext ? imguiContext->ActiveId : 0;
    if (hovered && activeIdAfterNode != 0 && activeIdAfterNode != activeIdBeforeNode) {
        m_NodeContentActive = true;
        m_NodeControlCapture = true;
    }
    drawList->PopClipRect();
    ImGui::PopID();
}

void EditorNodeGraphUI::RenderLinks(const EditorNodeGraph::Graph& graph) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
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
            selected ? 4.5f : 3.0f);
    }
}

void EditorNodeGraphUI::RenderInteraction(EditorModule* editor, const EditorNodeGraph::Graph& graph) {
    const EditorNodeGraph::Vec2 mouse = ToGraphVec2(ImGui::GetMousePos());
    const SocketHit hoveredInput = FindInputPinAt(graph, mouse);
    const SocketHit hoveredOutput = FindOutputPinAt(graph, mouse);
    m_HoveredInputNodeId = hoveredInput.nodeId;
    m_HoveredInputSocketId = hoveredInput.socketId;
    m_HoveredOutputNodeId = hoveredOutput.nodeId;
    m_HoveredOutputSocketId = hoveredOutput.socketId;
    const int hoveredNodeId = FindNodeAt(graph, mouse);
    const EditorNodeGraph::Link hoveredLink = FindLinkAt(graph, mouse);
    const bool hoveringLink = hoveredLink.fromNodeId > 0 && hoveredLink.toNodeId > 0;
    const bool graphHovered = ImGui::IsMouseHoveringRect(ToImVec2(m_CanvasMin), ToImVec2(m_CanvasMax));
    const bool additiveSelect = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
    const bool anyPopupOpen = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);

    if (hoveredOutput.IsValid() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DragOutputNodeId = m_HoveredOutputNodeId;
        m_DragOutputSocketId = m_HoveredOutputSocketId;
    }
    if (hoveredInput.IsValid() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DragInputNodeId = m_HoveredInputNodeId;
        m_DragInputSocketId = m_HoveredInputSocketId;
    }

    if (graphHovered && !anyPopupOpen && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_ContextGraphPos = ScreenToGraph(ToGraphVec2(ImGui::GetMousePos()));
        m_ContextTarget = ContextTarget::Canvas;
        m_ContextNodeId = -1;
        m_ContextLink = {};
        if (hoveringLink) {
            m_ContextTarget = ContextTarget::Link;
            m_ContextLink = hoveredLink;
            editor->GetNodeGraph().SelectLink(hoveredLink.fromNodeId, hoveredLink.fromSocketId, hoveredLink.toNodeId, hoveredLink.toSocketId);
        } else if (hoveredNodeId > 0) {
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
            drawList->AddBezierCubic(p1, ImVec2(p1.x + handle, p1.y), ImVec2(p2.x - handle, p2.y), p2, IM_COL32(255, 255, 255, 210), 2.5f);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredInput.IsValid()) {
                std::string error;
                if (!editor->ConnectGraphSockets(m_DragOutputNodeId, m_DragOutputSocketId, hoveredInput.nodeId, hoveredInput.socketId, &error)) {
                    m_StatusMessage = error;
                } else {
                    m_StatusMessage.clear();
                }
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
            drawList->AddBezierCubic(p1, ImVec2(p1.x + handle, p1.y), ImVec2(p2.x - handle, p2.y), p2, IM_COL32(255, 255, 255, 210), 2.5f);
        }

        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hoveredOutput.IsValid()) {
                std::string error;
                if (!editor->ConnectGraphSockets(hoveredOutput.nodeId, hoveredOutput.socketId, m_DragInputNodeId, m_DragInputSocketId, &error)) {
                    m_StatusMessage = error;
                } else {
                    m_StatusMessage.clear();
                }
            }
            m_DragInputNodeId = -1;
            m_DragInputSocketId.clear();
        }
        return;
    }

    if (hoveringLink && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (editor->RemoveGraphLink(hoveredLink.fromNodeId, hoveredLink.fromSocketId, hoveredLink.toNodeId, hoveredLink.toSocketId)) {
            m_StatusMessage = "Link removed.";
        }
        return;
    }

    if (hoveringLink && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        editor->GetNodeGraph().SelectLink(hoveredLink.fromNodeId, hoveredLink.fromSocketId, hoveredLink.toNodeId, hoveredLink.toSocketId);
        return;
    }

    if (hoveredNodeId > 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !m_NodeControlCapture) {
        EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(hoveredNodeId);
        if (node && (node->kind == EditorNodeGraph::NodeKind::Layer || node->kind == EditorNodeGraph::NodeKind::Mix)) {
            node->expanded = !node->expanded;
            editor->SelectGraphNode(node->id);
            return;
        }
    }

    if (graphHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoveredNodeId <= 0 && !hoveringLink &&
        m_HoveredInputNodeId <= 0 && m_HoveredOutputNodeId <= 0 && !m_NodeControlCapture) {
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
            editor->GetNodeGraph().SelectNodesInRect(m_BoxSelectStart, m_BoxSelectCurrent, additiveSelect);
            m_BoxSelecting = false;
        }
        return;
    }

    if (hoveredNodeId > 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_NodeControlCapture) {
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

    if (m_DragNodeId > 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !m_NodeControlCapture) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        for (int nodeId : editor->GetNodeGraph().GetSelectedNodeIds()) {
            if (EditorNodeGraph::Node* node = editor->GetNodeGraph().FindNode(nodeId)) {
                node->position.x += delta.x / m_Zoom;
                node->position.y += delta.y / m_Zoom;
            }
        }
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_DragNodeId = -1;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (editor->DeleteSelectedGraphLink()) {
            m_StatusMessage = "Link deleted.";
        } else if (editor->DeleteSelectedGraphNodes()) {
            m_StatusMessage = "Node deleted.";
        }
    }
}

void EditorNodeGraphUI::RenderValidationStatus(const EditorNodeGraph::Graph& graph) {
    const EditorNodeGraph::ValidationResult validation = graph.Validate();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec2 pos(m_CanvasOrigin.x + 12.0f, m_CanvasOrigin.y + 12.0f);
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
    drawList->AddText(pos, color, text.c_str());
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::InputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    const EditorNodeGraph::Vec2 screenSize{ size.x * m_Zoom, size.y * m_Zoom };
    const EditorNodeGraph::Vec2 pos = GraphToScreen(node.position);
    const float y = socketId == EditorNodeGraph::kMaskInputSocketId
        ? pos.y + screenSize.y * 0.68f
        : (socketId == EditorNodeGraph::kMixInputASocketId ? pos.y + screenSize.y * 0.34f
        : (socketId == EditorNodeGraph::kMixInputBSocketId ? pos.y + screenSize.y * 0.52f
        : (socketId == EditorNodeGraph::kMixFactorSocketId ? pos.y + screenSize.y * 0.72f
        : pos.y + screenSize.y * 0.5f)));
    return EditorNodeGraph::Vec2{ pos.x, y };
}

EditorNodeGraph::Vec2 EditorNodeGraphUI::OutputPinScreenPos(const EditorNodeGraph::Node& node, const std::string& socketId) const {
    (void)socketId;
    const EditorNodeGraph::Vec2 size = NodeSize(node);
    const EditorNodeGraph::Vec2 screenSize{ size.x * m_Zoom, size.y * m_Zoom };
    const EditorNodeGraph::Vec2 pos = GraphToScreen(node.position);
    return EditorNodeGraph::Vec2{ pos.x + screenSize.x, pos.y + screenSize.y * 0.5f };
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindInputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Input) {
                continue;
            }
            const EditorNodeGraph::Vec2 pin = InputPinScreenPos(node, socket.id);
            const float dx = pin.x - screenPos.x;
            const float dy = pin.y - screenPos.y;
            if ((dx * dx + dy * dy) <= 100.0f) {
                return SocketHit{ node.id, socket.id };
            }
        }
    }
    return {};
}

EditorNodeGraphUI::SocketHit EditorNodeGraphUI::FindOutputPinAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    for (const EditorNodeGraph::Node& node : graph.GetNodes()) {
        for (const EditorNodeGraph::SocketDefinition& socket : graph.GetSockets(node, true)) {
            if (socket.direction != EditorNodeGraph::SocketDirection::Output) {
                continue;
            }
            const EditorNodeGraph::Vec2 pin = OutputPinScreenPos(node, socket.id);
            const float dx = pin.x - screenPos.x;
            const float dy = pin.y - screenPos.y;
            if ((dx * dx + dy * dy) <= 100.0f) {
                return SocketHit{ node.id, socket.id };
            }
        }
    }
    return {};
}

int EditorNodeGraphUI::FindNodeAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    const auto& nodes = graph.GetNodes();
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const EditorNodeGraph::Vec2 pos = GraphToScreen(it->position);
        const EditorNodeGraph::Vec2 size = NodeScreenSize(*it);
        if (screenPos.x >= pos.x && screenPos.x <= pos.x + size.x &&
            screenPos.y >= pos.y && screenPos.y <= pos.y + size.y) {
            return it->id;
        }
    }
    return -1;
}

EditorNodeGraph::Link EditorNodeGraphUI::FindLinkAt(const EditorNodeGraph::Graph& graph, const EditorNodeGraph::Vec2& screenPos) const {
    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {
        const EditorNodeGraph::Node* from = graph.FindNode(link.fromNodeId);
        const EditorNodeGraph::Node* to = graph.FindNode(link.toNodeId);
        if (!from || !to) {
            continue;
        }
        if (IsPointNearLink(screenPos, OutputPinScreenPos(*from, link.fromSocketId), InputPinScreenPos(*to, link.toSocketId))) {
            return link;
        }
    }
    return {};
}

bool EditorNodeGraphUI::IsPointNearLink(const EditorNodeGraph::Vec2& point, const EditorNodeGraph::Vec2& a, const EditorNodeGraph::Vec2& b) const {
    return DistancePointToSegment(ToImVec2(point), ToImVec2(a), ToImVec2(b)) <= 8.0f;
}

void EditorNodeGraphUI::DrawClippedText(ImDrawList* drawList, const ImVec2& min, const ImVec2& max, const char* text, ImU32 color) const {
    drawList->PushClipRect(min, max, true);
    drawList->AddText(min, color, text);
    drawList->PopClipRect();
}

unsigned int EditorNodeGraphUI::GetImagePreviewTexture(const EditorNodeGraph::Node& node) {
    if (node.kind != EditorNodeGraph::NodeKind::Image || node.image.pixels.empty() || node.image.width <= 0 || node.image.height <= 0) {
        return 0;
    }
    const size_t fingerprint = HashImagePayload(node);
    auto it = m_ImagePreviewTextures.find(node.id);
    auto fingerprintIt = m_ImagePreviewFingerprints.find(node.id);
    if (it != m_ImagePreviewTextures.end() &&
        fingerprintIt != m_ImagePreviewFingerprints.end() &&
        fingerprintIt->second == fingerprint) {
        return it->second;
    }
    if (it != m_ImagePreviewTextures.end() && it->second != 0) {
        unsigned int oldTexture = it->second;
        glDeleteTextures(1, &oldTexture);
        m_ImagePreviewTextures.erase(it);
    }
    const unsigned int texture = GLHelpers::CreateTextureFromPixels(node.image.pixels.data(), node.image.width, node.image.height, node.image.channels);
    m_ImagePreviewTextures[node.id] = texture;
    m_ImagePreviewFingerprints[node.id] = fingerprint;
    return texture;
}

unsigned int EditorNodeGraphUI::UploadPreviewTexture(int nodeId, const std::vector<unsigned char>& pixels, int width, int height) {
    if (pixels.empty() || width <= 0 || height <= 0) {
        return 0;
    }

    auto existing = m_GraphPreviewTextures.find(nodeId);
    if (existing != m_GraphPreviewTextures.end() && existing->second != 0) {
        unsigned int oldTexture = existing->second;
        glDeleteTextures(1, &oldTexture);
        m_GraphPreviewTextures.erase(existing);
    }

    const unsigned int texture = GLHelpers::CreateTextureFromPixels(pixels.data(), width, height, 4);
    if (texture != 0) {
        m_GraphPreviewTextures[nodeId] = texture;
    }
    return texture;
}

unsigned int EditorNodeGraphUI::GetGraphPreviewTexture(EditorModule* editor, const EditorNodeGraph::Node& node) {
    if (!editor || node.kind != EditorNodeGraph::NodeKind::Preview) {
        return 0;
    }

    const double now = ImGui::GetTime();
    auto textureIt = m_GraphPreviewTextures.find(node.id);
    auto timeIt = m_GraphPreviewRefreshTimes.find(node.id);
    if (textureIt != m_GraphPreviewTextures.end() &&
        timeIt != m_GraphPreviewRefreshTimes.end() &&
        now - timeIt->second < 0.35) {
        return textureIt->second;
    }

    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels = editor->GetPreviewPixelsForNode(node.id, width, height);
    m_GraphPreviewRefreshTimes[node.id] = now;
    if (pixels.empty()) {
        if (textureIt != m_GraphPreviewTextures.end() && textureIt->second != 0) {
            unsigned int oldTexture = textureIt->second;
            glDeleteTextures(1, &oldTexture);
            m_GraphPreviewTextures.erase(textureIt);
        }
        return 0;
    }

    return UploadPreviewTexture(node.id, pixels, width, height);
}
