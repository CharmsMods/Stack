#include "EditorModule.h"

#include "Async/TaskSystem.h"
#include "NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <imgui.h>
#include <imgui_internal.h>

namespace {

namespace StackFormat = StackBinaryFormat;

constexpr ImVec4 kEditorWorkspaceBaseColor = ImVec4(0.016f, 0.231f, 0.274f, 1.0f);
struct DecodedImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 4;
};

bool DecodeImageFromFile(const std::string& path, DecodedImageData& outImage) {
    outImage = {};

    stbi_set_flip_vertically_on_load_thread(1);
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        return false;
    }

    outImage.width = width;
    outImage.height = height;
    outImage.channels = 4;
    outImage.pixels.assign(pixels, pixels + (width * height * 4));
    stbi_image_free(pixels);
    return true;
}

void PngWriteCallback(void* context, void* data, int size) {
    auto* bytes = static_cast<std::vector<unsigned char>*>(context);
    const auto* begin = static_cast<unsigned char*>(data);
    bytes->insert(bytes->end(), begin, begin + size);
}

std::vector<unsigned char> EncodePngBytes(const std::vector<unsigned char>& pixels, int width, int height, int channels) {
    std::vector<unsigned char> pngBytes;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return pngBytes;
    }

    const int safeChannels = std::max(1, channels);
    stbi_write_png_to_func(PngWriteCallback, &pngBytes, width, height, safeChannels, pixels.data(), width * safeChannels);
    return pngBytes;
}

std::string SanitizeProjectFileStem(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            result.push_back('_');
        }
    }

    while (!result.empty() && result.front() == '_') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == '_') {
        result.pop_back();
    }
    return result.empty() ? std::string("project") : result;
}

void ResizeNearestRgba(
    const std::vector<unsigned char>& srcPixels,
    int srcW,
    int srcH,
    int dstW,
    int dstH,
    std::vector<unsigned char>& dstPixels) {
    dstPixels.assign(static_cast<size_t>(dstW * dstH * 4), 0);
    if (srcPixels.empty() || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return;
    }

    for (int y = 0; y < dstH; ++y) {
        const int srcY = std::clamp(static_cast<int>((static_cast<float>(y) / static_cast<float>(dstH)) * srcH), 0, srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            const int srcX = std::clamp(static_cast<int>((static_cast<float>(x) / static_cast<float>(dstW)) * srcW), 0, srcW - 1);
            const size_t dstIndex = static_cast<size_t>((y * dstW + x) * 4);
            const size_t srcIndex = static_cast<size_t>((srcY * srcW + srcX) * 4);
            dstPixels[dstIndex + 0] = srcPixels[srcIndex + 0];
            dstPixels[dstIndex + 1] = srcPixels[srcIndex + 1];
            dstPixels[dstIndex + 2] = srcPixels[srcIndex + 2];
            dstPixels[dstIndex + 3] = srcPixels[srcIndex + 3];
        }
    }
}

std::vector<unsigned char> BuildThumbnailBytes(
    const std::vector<unsigned char>& rgbaPixels,
    int width,
    int height) {
    if (rgbaPixels.empty() || width <= 0 || height <= 0) {
        return {};
    }

    constexpr int kMaxEdge = 320;
    if (width <= kMaxEdge && height <= kMaxEdge) {
        return EncodePngBytes(rgbaPixels, width, height, 4);
    }

    const float scale = static_cast<float>(kMaxEdge) / static_cast<float>(std::max(width, height));
    const int thumbW = std::max(1, static_cast<int>(std::floor(static_cast<float>(width) * scale)));
    const int thumbH = std::max(1, static_cast<int>(std::floor(static_cast<float>(height) * scale)));
    std::vector<unsigned char> thumbPixels;
    ResizeNearestRgba(rgbaPixels, width, height, thumbW, thumbH, thumbPixels);
    return EncodePngBytes(thumbPixels, thumbW, thumbH, 4);
}

std::string BuildTimestampString() {
    std::time_t now = std::time(nullptr);
    std::tm timeInfo{};
#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    timeInfo = *std::localtime(&now);
#endif
    char buffer[64];
    if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeInfo) == 0) {
        return {};
    }
    return std::string(buffer);
}

std::string FileNameFromPath(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path.empty() ? std::string("Image") : path;
    }
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
}

template <typename T>
std::size_t HashValue(const T& value) {
    return std::hash<T>{}(value);
}

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
}

RenderMaskGeneratorKind ToRenderMaskKind(EditorNodeGraph::MaskGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskGeneratorKind::Solid: return RenderMaskGeneratorKind::Solid;
        case EditorNodeGraph::MaskGeneratorKind::LinearGradient: return RenderMaskGeneratorKind::LinearGradient;
        case EditorNodeGraph::MaskGeneratorKind::RadialGradient: return RenderMaskGeneratorKind::RadialGradient;
        case EditorNodeGraph::MaskGeneratorKind::Noise: return RenderMaskGeneratorKind::Noise;
    }
    return RenderMaskGeneratorKind::Solid;
}

RenderMaskSettings ToRenderMaskSettings(const EditorNodeGraph::MaskGeneratorSettings& settings) {
    RenderMaskSettings result;
    result.value = settings.value;
    result.angle = settings.angle;
    result.offset = settings.offset;
    result.scale = settings.scale;
    result.centerX = settings.centerX;
    result.centerY = settings.centerY;
    result.radius = settings.radius;
    result.feather = settings.feather;
    result.invert = settings.invert;
    return result;
}

RenderMixBlendMode ToRenderMixBlendMode(EditorNodeGraph::MixBlendMode mode) {
    switch (mode) {
        case EditorNodeGraph::MixBlendMode::Normal: return RenderMixBlendMode::Normal;
        case EditorNodeGraph::MixBlendMode::Add: return RenderMixBlendMode::Add;
        case EditorNodeGraph::MixBlendMode::Multiply: return RenderMixBlendMode::Multiply;
        case EditorNodeGraph::MixBlendMode::Screen: return RenderMixBlendMode::Screen;
        case EditorNodeGraph::MixBlendMode::AlphaOver: return RenderMixBlendMode::AlphaOver;
    }
    return RenderMixBlendMode::Normal;
}

RenderMaskUtilityKind ToRenderMaskUtilityKind(EditorNodeGraph::MaskUtilityKind kind) {
    switch (kind) {
        case EditorNodeGraph::MaskUtilityKind::Invert: return RenderMaskUtilityKind::Invert;
        case EditorNodeGraph::MaskUtilityKind::Levels: return RenderMaskUtilityKind::Levels;
        case EditorNodeGraph::MaskUtilityKind::Threshold: return RenderMaskUtilityKind::Threshold;
    }
    return RenderMaskUtilityKind::Invert;
}

RenderImageToMaskKind ToRenderImageToMaskKind(EditorNodeGraph::ImageToMaskKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageToMaskKind::Luminance: return RenderImageToMaskKind::Luminance;
    }
    return RenderImageToMaskKind::Luminance;
}

RenderImageGeneratorKind ToRenderImageGeneratorKind(EditorNodeGraph::ImageGeneratorKind kind) {
    switch (kind) {
        case EditorNodeGraph::ImageGeneratorKind::SolidColor: return RenderImageGeneratorKind::SolidColor;
        case EditorNodeGraph::ImageGeneratorKind::ColorGradient: return RenderImageGeneratorKind::ColorGradient;
        case EditorNodeGraph::ImageGeneratorKind::Square: return RenderImageGeneratorKind::Square;
        case EditorNodeGraph::ImageGeneratorKind::Circle: return RenderImageGeneratorKind::Circle;
    }
    return RenderImageGeneratorKind::SolidColor;
}

RenderMaskUtilitySettings ToRenderMaskUtilitySettings(const EditorNodeGraph::MaskUtilitySettings& settings) {
    RenderMaskUtilitySettings result;
    result.blackPoint = settings.blackPoint;
    result.whitePoint = settings.whitePoint;
    result.gamma = settings.gamma;
    result.threshold = settings.threshold;
    result.softness = settings.softness;
    result.invert = settings.invert;
    return result;
}

RenderImageToMaskSettings ToRenderImageToMaskSettings(const EditorNodeGraph::ImageToMaskSettings& settings) {
    RenderImageToMaskSettings result;
    result.low = settings.low;
    result.high = settings.high;
    result.softness = settings.softness;
    result.invert = settings.invert;
    return result;
}

RenderImageGeneratorSettings ToRenderImageGeneratorSettings(const EditorNodeGraph::ImageGeneratorSettings& settings) {
    RenderImageGeneratorSettings result;
    for (int i = 0; i < 4; ++i) {
        result.colorA[i] = settings.colorA[i];
        result.colorB[i] = settings.colorB[i];
    }
    result.angle = settings.angle;
    result.offset = settings.offset;
    return result;
}

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
        kind == EditorNodeGraph::NodeKind::MaskUtility ||
        kind == EditorNodeGraph::NodeKind::ImageToMask;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
        kind == EditorNodeGraph::NodeKind::ImageGenerator ||
        kind == EditorNodeGraph::NodeKind::Layer ||
        kind == EditorNodeGraph::NodeKind::Mix ||
        kind == EditorNodeGraph::NodeKind::Output;
}

bool ConnectionUsesImageAsRenderSource(
    const EditorNodeGraph::Graph& graph,
    int fromNodeId,
    const std::string& fromSocketId,
    int toNodeId,
    const std::string& toSocketId) {
    const EditorNodeGraph::Node* from = graph.FindNode(fromNodeId);
    const EditorNodeGraph::Node* to = graph.FindNode(toNodeId);
    if (!from || !to || from->kind != EditorNodeGraph::NodeKind::Image ||
        fromSocketId != EditorNodeGraph::kImageOutputSocketId) {
        return false;
    }

    if (to->kind == EditorNodeGraph::NodeKind::Layer && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Output && toSocketId == EditorNodeGraph::kImageInputSocketId) {
        return true;
    }
    if (to->kind == EditorNodeGraph::NodeKind::Mix &&
        (toSocketId == EditorNodeGraph::kMixInputASocketId ||
         toSocketId == EditorNodeGraph::kMixInputBSocketId)) {
        return true;
    }
    return false;
}

float SmoothStep(float edge0, float edge1, float x) {
    if (std::abs(edge1 - edge0) < 0.00001f) {
        return x < edge0 ? 0.0f : 1.0f;
    }
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float MaskPreviewValue(const EditorNodeGraph::Node& node, float u, float v) {
    const EditorNodeGraph::MaskGeneratorSettings& settings = node.maskSettings;
    float value = settings.value;
    if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::LinearGradient) {
        constexpr float kPi = 3.14159265358979323846f;
        const float radians = settings.angle * kPi / 180.0f;
        const float dx = std::cos(radians);
        const float dy = std::sin(radians);
        value = ((u - 0.5f) * dx + (v - 0.5f) * dy) * settings.scale + 0.5f + settings.offset;
        value = std::clamp(value, 0.0f, 1.0f);
    } else if (node.maskKind == EditorNodeGraph::MaskGeneratorKind::RadialGradient) {
        const float dx = u - settings.centerX;
        const float dy = v - settings.centerY;
        const float dist = std::sqrt(dx * dx + dy * dy);
        value = 1.0f - SmoothStep(std::max(0.0f, settings.radius - settings.feather), settings.radius + settings.feather, dist);
    }
    if (settings.invert) {
        value = 1.0f - value;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

std::vector<unsigned char> GenerateMaskPreviewPixels(const EditorNodeGraph::Node& node, int& outW, int& outH) {
    outW = 192;
    outH = 128;
    std::vector<unsigned char> pixels(static_cast<size_t>(outW * outH * 4), 255);
    for (int y = 0; y < outH; ++y) {
        const float v = outH > 1 ? static_cast<float>(y) / static_cast<float>(outH - 1) : 0.0f;
        for (int x = 0; x < outW; ++x) {
            const float u = outW > 1 ? static_cast<float>(x) / static_cast<float>(outW - 1) : 0.0f;
            const unsigned char gray = static_cast<unsigned char>(std::round(MaskPreviewValue(node, u, v) * 255.0f));
            const size_t dst = static_cast<size_t>(y * outW + x) * 4;
            pixels[dst + 0] = gray;
            pixels[dst + 1] = gray;
            pixels[dst + 2] = gray;
            pixels[dst + 3] = 255;
        }
    }
    return pixels;
}

} // namespace

EditorModule::EditorModule() {}

EditorModule::~EditorModule() {
    ClearCompositeSceneTextures();
}

void EditorModule::Initialize(GLFWwindow* sharedWindow) {
    std::vector<std::string> registryErrors;
    if (!LayerRegistry::ValidateRegistry(&registryErrors)) {
        for (const std::string& error : registryErrors) {
            fprintf(stderr, "LayerRegistry validation failed: %s\n", error.c_str());
        }
    }

    m_Pipeline.Initialize();
    m_CompositePreviewPipeline.Initialize();
    m_AdvancedNodeEditor.Initialize();
    m_Sidebar.Initialize();
    m_Viewport.Initialize();
    m_Scopes.Initialize();
    m_RenderWorkerAvailable = sharedWindow && m_RenderWorker.Initialize(sharedWindow);

    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_AdvancedEditorOpen = false;
    m_AdvancedEditorNodeId = -1;
    m_NodeGraph.ResetFromLayers(0, false);
    ClearCompositeRuntimeState();
    MarkRenderDirty();
}

void EditorModule::AddLayer(LayerType type) {
    std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayer(type);

    if (newLayer) {
        newLayer->InitializeGL();

        const char* defaultName = newLayer->GetDefaultName();
        int count = 0;
        for (const auto& existing : m_Layers) {
            if (strcmp(existing->GetDefaultName(), defaultName) == 0) {
                count++;
            }
        }

        if (count > 0) {
            char suffix[64];
            snprintf(suffix, sizeof(suffix), "%s (%d)", defaultName, count + 1);
            newLayer->SetInstanceName(suffix);
        }

        m_Layers.push_back(newLayer);
        SelectLayer(static_cast<int>(m_Layers.size()) - 1);
        m_FocusSelectedTabNextRender = true;
        MarkRenderDirty();
    }
}

void EditorModule::AddLayerNodeAt(LayerType type, EditorNodeGraph::Vec2 graphPosition) {
    const int layerIndex = static_cast<int>(m_Layers.size());
    AddLayer(type);
    if (static_cast<int>(m_Layers.size()) == layerIndex + 1) {
        m_NodeGraph.AddLayerNode(type, layerIndex, graphPosition);
        RefreshGraphLayerMetadata();
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(layerIndex)) {
            SelectGraphNode(node->id);
        }
        MarkRenderDirty();
    }
}

void EditorModule::RemoveLayer(int index) {
    if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
        if (const EditorNodeGraph::Node* boundNode = GetActiveAdvancedEditorNode()) {
            if (boundNode->kind == EditorNodeGraph::NodeKind::Layer && boundNode->layerIndex == index) {
                CloseAdvancedEditor();
            }
        }
        // TODO: Promote this to an undoable editor command when command history lands.
        m_Layers.erase(m_Layers.begin() + index);
        m_NodeGraph.RemoveLayerNode(index);
        RefreshGraphLayerMetadata();
        if (m_SelectedLayerIndex >= static_cast<int>(m_Layers.size())) {
            m_SelectedLayerIndex = static_cast<int>(m_Layers.size()) - 1;
        }
        MarkRenderDirty();
    }
}

void EditorModule::MoveLayer(int from, int to) {
    if (from == to) return;
    if (from < 0 || from >= static_cast<int>(m_Layers.size())) return;
    if (to < 0 || to >= static_cast<int>(m_Layers.size())) return;

    // TODO: Promote this to an undoable editor command when command history lands.
    if (from < to) {
        std::rotate(m_Layers.begin() + from, m_Layers.begin() + from + 1, m_Layers.begin() + to + 1);
    } else {
        std::rotate(m_Layers.begin() + to, m_Layers.begin() + from, m_Layers.begin() + from + 1);
    }

    if (m_SelectedLayerIndex == from) {
        m_SelectedLayerIndex = to;
    } else if (from < m_SelectedLayerIndex && to >= m_SelectedLayerIndex) {
        m_SelectedLayerIndex--;
    } else if (from > m_SelectedLayerIndex && to <= m_SelectedLayerIndex) {
        m_SelectedLayerIndex++;
    }

    RefreshGraphLayerMetadata();
    MarkRenderDirty();
}

void EditorModule::SetLayerVisible(int index, bool visible) {
    if (index < 0 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    // TODO: Promote this to an undoable editor command when command history lands.
    m_Layers[index]->SetVisible(visible);
    MarkRenderDirty();
}

void EditorModule::SelectLayer(int index) {
    if (index < -1 || index >= static_cast<int>(m_Layers.size())) {
        return;
    }

    m_SelectedLayerIndex = index;
}

void EditorModule::SelectGraphNode(int nodeId) {
    m_NodeGraph.SelectNode(nodeId);
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (node && node->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectLayer(node->layerIndex);
    } else {
        SelectLayer(-1);
    }
}

bool EditorModule::LayerSupportsAdvancedEditor(int layerIndex) const {
    return layerIndex >= 0 &&
        layerIndex < static_cast<int>(m_Layers.size()) &&
        m_Layers[layerIndex] &&
        m_Layers[layerIndex]->SupportsAdvancedEditor();
}

bool EditorModule::NodeSupportsAdvancedEditor(int nodeId) const {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    return node &&
        node->kind == EditorNodeGraph::NodeKind::Layer &&
        LayerSupportsAdvancedEditor(node->layerIndex);
}

void EditorModule::OpenAdvancedEditorForNode(int nodeId) {
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Layer || !LayerSupportsAdvancedEditor(node->layerIndex)) {
        return;
    }
    SelectGraphNode(nodeId);
    m_AdvancedEditorNodeId = nodeId;
    m_AdvancedEditorOpen = true;
    if (m_NodeGraph.FindNode(nodeId)) {
        m_NodeGraph.FindNode(nodeId)->expanded = false;
    }
    SetPickingColor(false);
}

void EditorModule::CloseAdvancedEditor() {
    m_AdvancedEditorOpen = false;
    m_AdvancedEditorNodeId = -1;
    SetPickingColor(false);
}

LayerBase* EditorModule::GetActiveAdvancedEditorLayer() {
    EditorNodeGraph::Node* node = GetActiveAdvancedEditorNode();
    if (!node || node->kind != EditorNodeGraph::NodeKind::Layer) {
        return nullptr;
    }
    return node->layerIndex >= 0 && node->layerIndex < static_cast<int>(m_Layers.size())
        ? m_Layers[node->layerIndex].get()
        : nullptr;
}

const LayerBase* EditorModule::GetActiveAdvancedEditorLayer() const {
    const EditorNodeGraph::Node* node = GetActiveAdvancedEditorNode();
    if (!node || node->kind != EditorNodeGraph::NodeKind::Layer) {
        return nullptr;
    }
    return node->layerIndex >= 0 && node->layerIndex < static_cast<int>(m_Layers.size())
        ? m_Layers[node->layerIndex].get()
        : nullptr;
}

EditorNodeGraph::Node* EditorModule::GetActiveAdvancedEditorNode() {
    if (!m_AdvancedEditorOpen || m_AdvancedEditorNodeId <= 0) {
        return nullptr;
    }
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_AdvancedEditorNodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Layer || !LayerSupportsAdvancedEditor(node->layerIndex)) {
        return nullptr;
    }
    return node;
}

const EditorNodeGraph::Node* EditorModule::GetActiveAdvancedEditorNode() const {
    if (!m_AdvancedEditorOpen || m_AdvancedEditorNodeId <= 0) {
        return nullptr;
    }
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_AdvancedEditorNodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Layer || !LayerSupportsAdvancedEditor(node->layerIndex)) {
        return nullptr;
    }
    return node;
}

void EditorModule::RenderAdvancedEditorOverlay(const ImVec2& workspacePos, const ImVec2& workspaceSize) {
    if (m_AdvancedEditorOpen && (!GetActiveAdvancedEditorNode() || !GetActiveAdvancedEditorLayer())) {
        CloseAdvancedEditor();
    }
    m_AdvancedNodeEditor.Render(this, workspacePos, workspaceSize);
}

void EditorModule::RenderAdvancedEditorPreview(const char* id, const ImVec2& desiredSize, bool allowColorPicking) {
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 regionSize(
        std::max(220.0f, desiredSize.x > 0.0f ? std::min(desiredSize.x, available.x) : available.x),
        std::max(180.0f, desiredSize.y > 0.0f ? desiredSize.y : available.y));

    ImGui::PushID(id ? id : "AdvancedEditorPreview");
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::BeginChild("PreviewRegion", regionSize, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 childMin = ImGui::GetCursorScreenPos();
    const ImVec2 childAvail = ImGui::GetContentRegionAvail();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const bool hasOutput = m_Pipeline.HasSourceImage() && m_NodeGraph.IsOutputConnected() && m_Pipeline.GetOutputTexture() != 0;
    if (!hasOutput) {
        const char* message = IsEditorRenderBusy()
            ? "Rendering preview..."
            : (m_NodeGraph.GetActiveImageNodeId() > 0
                ? "Connect the graph to the output to preview it here."
                : "Load an image and connect the graph to the output.");
        const ImVec2 textSize = ImGui::CalcTextSize(message);
        drawList->AddText(
            ImVec2(childMin.x + std::max(16.0f, (childAvail.x - textSize.x) * 0.5f),
                   childMin.y + std::max(24.0f, (childAvail.y - textSize.y) * 0.32f)),
            IM_COL32(185, 194, 201, 220),
            message);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        ImGui::PopID();
        return;
    }

    const int imgW = std::max(1, m_Pipeline.GetCanvasWidth());
    const int imgH = std::max(1, m_Pipeline.GetCanvasHeight());
    const float fitScale = std::min(childAvail.x / static_cast<float>(imgW), childAvail.y / static_cast<float>(imgH));
    const float displayScale = std::max(0.01f, fitScale);
    const ImVec2 imageSize(imgW * displayScale, imgH * displayScale);
    const ImVec2 imagePos(
        childMin.x + std::max(0.0f, (childAvail.x - imageSize.x) * 0.5f),
        childMin.y + std::max(0.0f, (childAvail.y - imageSize.y) * 0.5f));
    drawList->AddImage(
        (ImTextureID)(intptr_t)m_Pipeline.GetOutputTexture(),
        imagePos,
        ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
        ImVec2(0.0f, 1.0f),
        ImVec2(1.0f, 0.0f));

    if (allowColorPicking && IsPickingColor()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool overImage =
            mouse.x >= imagePos.x &&
            mouse.x <= imagePos.x + imageSize.x &&
            mouse.y >= imagePos.y &&
            mouse.y <= imagePos.y + imageSize.y;
        if (overImage) {
            drawList->AddRectFilled(
                imagePos,
                ImVec2(imagePos.x + imageSize.x, imagePos.y + imageSize.y),
                IM_COL32(255, 255, 255, 12));
            drawList->AddText(
                ImVec2(imagePos.x + 12.0f, imagePos.y + 12.0f),
                IM_COL32(255, 255, 255, 220),
                "Click to sample color");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const float u = std::clamp((mouse.x - imagePos.x) / std::max(1.0f, imageSize.x), 0.0f, 1.0f);
                const float v = std::clamp((mouse.y - imagePos.y) / std::max(1.0f, imageSize.y), 0.0f, 1.0f);
                const int px = std::clamp(static_cast<int>(u * static_cast<float>(imgW)), 0, imgW - 1);
                const int py = std::clamp(static_cast<int>(v * static_cast<float>(imgH)), 0, imgH - 1);
                const auto& sourcePixels = m_Pipeline.GetSourcePixelsRaw();
                const int channels = std::max(1, m_Pipeline.GetSourceChannels());
                const int flippedY = imgH - 1 - py;
                const size_t pixelIndex = static_cast<size_t>(flippedY * imgW + px) * static_cast<size_t>(channels);
                if (pixelIndex + 2 < sourcePixels.size()) {
                    OnColorPicked(
                        sourcePixels[pixelIndex + 0] / 255.0f,
                        sourcePixels[pixelIndex + 1] / 255.0f,
                        sourcePixels[pixelIndex + 2] / 255.0f);
                }
            }
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopID();
}

bool EditorModule::SelectAdjacentMainChainNode(int direction) {
    if (direction == 0) {
        return false;
    }

    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (selectedNodeId <= 0) {
        return false;
    }

    const int adjacentNodeId = m_NodeGraph.FindAdjacentMainChainNodeId(selectedNodeId, direction);
    if (adjacentNodeId <= 0 || adjacentNodeId == selectedNodeId) {
        return false;
    }

    SelectGraphNode(adjacentNodeId);
    return true;
}

void EditorModule::ApplyGraphLayerOrder() {
    const std::vector<int> order = m_NodeGraph.GetRenderLayerIndexPath();
    if (order.empty()) {
        return;
    }

    std::vector<int> uniqueOrder;
    for (int index : order) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size()) &&
            std::find(uniqueOrder.begin(), uniqueOrder.end(), index) == uniqueOrder.end()) {
            uniqueOrder.push_back(index);
        }
    }
    if (uniqueOrder.size() != m_Layers.size()) {
        return;
    }

    std::vector<std::shared_ptr<LayerBase>> reordered;
    reordered.reserve(m_Layers.size());
    for (int index : uniqueOrder) {
        reordered.push_back(m_Layers[index]);
    }
    m_Layers = std::move(reordered);

    for (int i = 0; i < static_cast<int>(uniqueOrder.size()); ++i) {
        if (EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(uniqueOrder[i])) {
            node->layerIndex = i;
        }
    }
    RefreshGraphLayerMetadata();
}

void EditorModule::PromptAddImageNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    const std::string path = FileDialogs::OpenImageFileDialog("Add Image Node");
    if (!path.empty()) {
        AddImageNodeFromFile(path, graphPosition);
    }
}

bool EditorModule::AddImageNodeFromFile(const std::string& path, EditorNodeGraph::Vec2 graphPosition) {
    DecodedImageData decoded;
    if (!DecodeImageFromFile(path, decoded) || decoded.pixels.empty()) {
        return false;
    }

    EditorNodeGraph::ImagePayload payload;
    payload.label = FileNameFromPath(path);
    payload.sourcePath = path;
    payload.pixels = std::move(decoded.pixels);
    payload.width = decoded.width;
    payload.height = decoded.height;
    payload.channels = decoded.channels;
    payload.pngBytes = EncodePngBytes(payload.pixels, payload.width, payload.height, payload.channels);

    EditorNodeGraph::Node* node = m_NodeGraph.AddImageNode(std::move(payload), graphPosition);
    if (node) {
        SelectGraphNode(node->id);
    }
    return node != nullptr;
}

bool EditorModule::ConnectGraphImageNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node || node->kind != EditorNodeGraph::NodeKind::Image || node->image.pixels.empty()) {
        return false;
    }

    LoadSourceFromPixels(node->image.pixels.data(), node->image.width, node->image.height, node->image.channels);
    m_NodeGraph.ConnectImageToOutput(nodeId);
    SelectGraphNode(nodeId);
    MarkRenderDirty();
    return true;
}

bool EditorModule::ConnectGraphNodes(int fromNodeId, int toNodeId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    const std::string fromSocket = from ? m_NodeGraph.DefaultOutputSocket(*from) : std::string();
    const EditorNodeGraph::Node* pendingTo = m_NodeGraph.FindNode(toNodeId);
    const std::string toSocket = pendingTo ? m_NodeGraph.DefaultInputSocket(*pendingTo) : std::string();
    if (!m_NodeGraph.TryConnect(fromNodeId, toNodeId, errorMessage)) {
        return false;
    }

    if (from && ConnectionUsesImageAsRenderSource(m_NodeGraph, fromNodeId, fromSocket, toNodeId, toSocket)) {
        LoadSourceFromPixels(from->image.pixels.data(), from->image.width, from->image.height, from->image.channels);
    }

    ApplyGraphLayerOrder();
    MarkRenderDirty();
    const EditorNodeGraph::Node* to = m_NodeGraph.FindNode(toNodeId);
    if (to && to->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectGraphNode(toNodeId);
    } else if (from) {
        SelectGraphNode(fromNodeId);
    }
    return true;
}

bool EditorModule::ConnectGraphSockets(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId, std::string* errorMessage) {
    EditorNodeGraph::Node* from = m_NodeGraph.FindNode(fromNodeId);
    if (from && from->kind == EditorNodeGraph::NodeKind::Image) {
        if (from->image.pixels.empty()) {
            if (errorMessage) *errorMessage = "Image node has no embedded pixels.";
            return false;
        }
    }

    if (!m_NodeGraph.TryConnectSockets(fromNodeId, fromSocketId, toNodeId, toSocketId, errorMessage)) {
        return false;
    }

    if (from && ConnectionUsesImageAsRenderSource(m_NodeGraph, fromNodeId, fromSocketId, toNodeId, toSocketId)) {
        LoadSourceFromPixels(from->image.pixels.data(), from->image.width, from->image.height, from->image.channels);
    }

    ApplyGraphLayerOrder();
    MarkRenderDirty();
    const EditorNodeGraph::Node* to = m_NodeGraph.FindNode(toNodeId);
    if (to && to->kind == EditorNodeGraph::NodeKind::Layer) {
        SelectGraphNode(toNodeId);
    } else if (from) {
        SelectGraphNode(fromNodeId);
    }
    return true;
}

bool EditorModule::RemoveGraphLink(int fromNodeId, int toNodeId) {
    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, toNodeId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::RemoveGraphLink(int fromNodeId, const std::string& fromSocketId, int toNodeId, const std::string& toSocketId) {
    const bool removed = m_NodeGraph.RemoveLink(fromNodeId, fromSocketId, toNodeId, toSocketId);
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphLink() {
    const bool removed = m_NodeGraph.RemoveSelectedLink();
    if (removed) {
        ApplyGraphLayerOrder();
        if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::RemoveGraphNode(int nodeId) {
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return false;
    }

    ClearGraphAutoFocusIfTrackedNode(nodeId);
    if (m_AdvancedEditorOpen && m_AdvancedEditorNodeId == nodeId) {
        CloseAdvancedEditor();
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer) {
        RemoveLayer(node->layerIndex);
        return true;
    }

    const bool removed = m_NodeGraph.RemoveNode(nodeId);
    if (removed && !m_NodeGraph.IsOutputConnected()) {
        m_Pipeline.ClearOutput();
    }
    if (removed) {
        MarkRenderDirty();
    }
    return removed;
}

bool EditorModule::DeleteSelectedGraphNodes() {
    std::vector<int> nodeIds = m_NodeGraph.GetSelectedNodeIds();
    if (nodeIds.empty()) {
        return false;
    }

    std::sort(nodeIds.begin(), nodeIds.end(), [this](int a, int b) {
        const EditorNodeGraph::Node* nodeA = m_NodeGraph.FindNode(a);
        const EditorNodeGraph::Node* nodeB = m_NodeGraph.FindNode(b);
        const int layerA = nodeA && nodeA->kind == EditorNodeGraph::NodeKind::Layer ? nodeA->layerIndex : -1;
        const int layerB = nodeB && nodeB->kind == EditorNodeGraph::NodeKind::Layer ? nodeB->layerIndex : -1;
        return layerA > layerB;
    });

    bool removedAny = false;
    for (int nodeId : nodeIds) {
        removedAny = RemoveGraphNode(nodeId) || removedAny;
    }
    m_NodeGraph.ClearSelection();
    RefreshGraphLayerMetadata();
    if (removedAny) {
        MarkRenderDirty();
    }
    return removedAny;
}

void EditorModule::AddScopeNodeAt(EditorNodeGraph::ScopeKind scopeKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddScopeNode(scopeKind, graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddMaskNodeAt(EditorNodeGraph::MaskGeneratorKind maskKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskGeneratorNode(maskKind, graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddMaskUtilityNodeAt(EditorNodeGraph::MaskUtilityKind utilityKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMaskUtilityNode(utilityKind, graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddImageToMaskNodeAt(EditorNodeGraph::ImageToMaskKind converterKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageToMaskNode(converterKind, graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddImageGeneratorNodeAt(EditorNodeGraph::ImageGeneratorKind generatorKind, EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddImageGeneratorNode(generatorKind, graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddMixNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddMixNode(graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddPreviewNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddPreviewNode(graphPosition)) {
        SelectGraphNode(node->id);
    }
}

void EditorModule::AddOutputNodeAt(EditorNodeGraph::Vec2 graphPosition) {
    if (EditorNodeGraph::Node* node = m_NodeGraph.AddOutputNode(graphPosition)) {
        SelectGraphNode(node->id);
        MarkRenderDirty();
    }
}

void EditorModule::AutoLayoutGraph() {
    m_NodeGraph.AutoLayout();
}

void EditorModule::DisconnectGraphOutput() {
    m_NodeGraph.DisconnectOutput();
    m_Pipeline.ClearOutput();
    MarkRenderDirty();
}

void EditorModule::RefreshGraphLayerMetadata() {
    m_NodeGraph.SyncLayerNodes(static_cast<int>(m_Layers.size()));

    for (int i = 0; i < static_cast<int>(m_Layers.size()); ++i) {
        EditorNodeGraph::Node* node = m_NodeGraph.FindNodeByLayerIndex(i);
        if (!node) {
            continue;
        }

        const nlohmann::json layerJson = m_Layers[i]->Serialize();
        const std::string typeId = layerJson.value("type", std::string());
        const LayerDescriptor* descriptor = LayerRegistry::FindDescriptorByTypeId(typeId);
        node->typeId = typeId;
        if (descriptor) {
            node->layerType = descriptor->type;
            node->title = descriptor->displayName;
        } else {
            node->title = m_Layers[i]->GetDefaultName();
        }
    }
}

std::vector<std::shared_ptr<LayerBase>> EditorModule::BuildGraphRenderLayers() const {
    std::vector<std::shared_ptr<LayerBase>> renderLayers;
    for (int index : m_NodeGraph.GetRenderLayerIndexPath()) {
        if (index >= 0 && index < static_cast<int>(m_Layers.size())) {
            renderLayers.push_back(m_Layers[index]);
        }
    }
    return renderLayers;
}

std::vector<RenderLayerStep> EditorModule::BuildGraphRenderSteps() const {
    std::vector<RenderLayerStep> steps;
    for (int nodeId : m_NodeGraph.GetRenderLayerNodePath()) {
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node || node->kind != EditorNodeGraph::NodeKind::Layer ||
            node->layerIndex < 0 || node->layerIndex >= static_cast<int>(m_Layers.size())) {
            continue;
        }

        RenderLayerStep step;
        step.layer = m_Layers[node->layerIndex];
        if (const EditorNodeGraph::Link* maskLink = m_NodeGraph.FindAnyInputLink(node->id, EditorNodeGraph::kMaskInputSocketId)) {
            const EditorNodeGraph::Node* maskNode = m_NodeGraph.FindNode(maskLink->fromNodeId);
            if (maskNode && maskNode->kind == EditorNodeGraph::NodeKind::MaskGenerator) {
                step.maskNodeId = maskNode->id;
            }
        }
        steps.push_back(std::move(step));
    }
    return steps;
}

std::vector<RenderMaskSource> EditorModule::BuildGraphRenderMasks() const {
    std::vector<int> usedMaskNodeIds;
    for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
        if (step.maskNodeId > 0 &&
            std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), step.maskNodeId) == usedMaskNodeIds.end()) {
            usedMaskNodeIds.push_back(step.maskNodeId);
        }
    }

    std::vector<RenderMaskSource> masks;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::MaskGenerator) {
            continue;
        }
        if (std::find(usedMaskNodeIds.begin(), usedMaskNodeIds.end(), node.id) == usedMaskNodeIds.end()) {
            continue;
        }

        RenderMaskSource mask;
        mask.nodeId = node.id;
        mask.kind = ToRenderMaskKind(node.maskKind);
        mask.settings = ToRenderMaskSettings(node.maskSettings);
        masks.push_back(mask);
    }
    return masks;
}

RenderGraphSnapshot EditorModule::BuildGraphSnapshot() const {
    RenderGraphSnapshot snapshot;
    snapshot.outputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        RenderGraphNode renderNode;
        renderNode.nodeId = node.id;
        switch (node.kind) {
            case EditorNodeGraph::NodeKind::Image:
                renderNode.kind = RenderGraphNodeKind::Image;
                renderNode.image.pixels = node.image.pixels;
                renderNode.image.width = node.image.width;
                renderNode.image.height = node.image.height;
                renderNode.image.channels = node.image.channels;
                break;
            case EditorNodeGraph::NodeKind::Layer:
                renderNode.kind = RenderGraphNodeKind::Layer;
                if (node.layerIndex >= 0 && node.layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node.layerIndex]) {
                    renderNode.layerJson = m_Layers[node.layerIndex]->Serialize();
                }
                break;
            case EditorNodeGraph::NodeKind::Output:
                renderNode.kind = RenderGraphNodeKind::Output;
                break;
            case EditorNodeGraph::NodeKind::MaskGenerator:
                renderNode.kind = RenderGraphNodeKind::MaskGenerator;
                renderNode.maskKind = ToRenderMaskKind(node.maskKind);
                renderNode.maskSettings = ToRenderMaskSettings(node.maskSettings);
                break;
            case EditorNodeGraph::NodeKind::MaskUtility:
                renderNode.kind = RenderGraphNodeKind::MaskUtility;
                renderNode.maskUtilityKind = ToRenderMaskUtilityKind(node.maskUtilityKind);
                renderNode.maskUtilitySettings = ToRenderMaskUtilitySettings(node.maskUtilitySettings);
                break;
            case EditorNodeGraph::NodeKind::ImageToMask:
                renderNode.kind = RenderGraphNodeKind::ImageToMask;
                renderNode.imageToMaskKind = ToRenderImageToMaskKind(node.imageToMaskKind);
                renderNode.imageToMaskSettings = ToRenderImageToMaskSettings(node.imageToMaskSettings);
                break;
            case EditorNodeGraph::NodeKind::ImageGenerator:
                renderNode.kind = RenderGraphNodeKind::ImageGenerator;
                renderNode.imageGeneratorKind = ToRenderImageGeneratorKind(node.imageGeneratorKind);
                renderNode.imageGeneratorSettings = ToRenderImageGeneratorSettings(node.imageGeneratorSettings);
                break;
            case EditorNodeGraph::NodeKind::Mix:
                renderNode.kind = RenderGraphNodeKind::Mix;
                renderNode.mixBlendMode = ToRenderMixBlendMode(node.mixBlendMode);
                renderNode.mixFactor = node.mixFactor;
                break;
            case EditorNodeGraph::NodeKind::Composite:
            case EditorNodeGraph::NodeKind::ExportBoundsSettings:
            case EditorNodeGraph::NodeKind::Scope:
            case EditorNodeGraph::NodeKind::Preview:
                continue;
        }
        snapshot.nodes.push_back(std::move(renderNode));
    }

    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (m_NodeGraph.GetLinkRole(link) == EditorNodeGraph::LinkRole::Scope) {
            continue;
        }
        snapshot.links.push_back(RenderGraphLink{
            link.fromNodeId,
            link.fromSocketId,
            link.toNodeId,
            link.toSocketId
        });
    }
    return snapshot;
}


void EditorModule::EnterSingleOutputPreviewMode() {
    ClearCompositeTransientInteractionState();
    m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
    m_CompositeExportBoundsEditMode = false;
    m_Viewport.ResetSinglePreviewState();
    m_HoverFade = 0.0f;

    const int previewOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    if (previewOutputNodeId <= 0) {
        m_Pipeline.ClearOutput();
        MarkRenderDirty();
        return;
    }

    RefreshCompletedChainCacheIfNeeded();
    const auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [previewOutputNodeId](const CachedCompositeChainState& chain) {
            return chain.info.outputNodeId == previewOutputNodeId;
        });
    if (chainIt == m_CachedCompletedChains.end()) {
        MarkRenderDirty();
        return;
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chainIt->info.sourceNodeId);
    if (sourceNode &&
        sourceNode->kind == EditorNodeGraph::NodeKind::Image &&
        !sourceNode->image.pixels.empty() &&
        sourceNode->image.width > 0 &&
        sourceNode->image.height > 0) {
        LoadSourceFromPixels(
            sourceNode->image.pixels.data(),
            sourceNode->image.width,
            sourceNode->image.height,
            sourceNode->image.channels);
        return;
    }

    int outW = 0;
    int outH = 0;
    (void)GetCompositePixelsForOutputNode(previewOutputNodeId, outW, outH);
    if (outW > 0 && outH > 0) {
        const std::vector<unsigned char> transparentPixels = BuildTransparentPixels(outW, outH);
        LoadSourceFromPixels(transparentPixels.data(), outW, outH, 4);
    } else {
        MarkRenderDirty();
    }
}

void EditorModule::HandleViewportModeTransition(ViewportMode previousMode, ViewportMode currentMode) {
    if (previousMode == currentMode) {
        return;
    }
    if (previousMode == ViewportMode::CompositeCanvas && currentMode == ViewportMode::SingleOutputPreview) {
        EnterSingleOutputPreviewMode();
    } else if (previousMode == ViewportMode::SingleOutputPreview && currentMode == ViewportMode::CompositeCanvas) {
        m_HoverFade = 0.0f;
        ClearCompositeTransientInteractionState();
    }
    m_LastViewportMode = currentMode;
}

std::size_t EditorModule::BuildCompositeChainFingerprint(const EditorNodeGraph::CompletedChainInfo& chain) const {
    std::size_t fingerprint = HashValue(chain.outputNodeId);
    HashCombine(fingerprint, HashValue(chain.terminalNodeId));
    HashCombine(fingerprint, HashValue(chain.sourceNodeId));
    for (int nodeId : chain.nodeIds) {
        HashCombine(fingerprint, HashValue(nodeId));
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            continue;
        }
        HashCombine(fingerprint, HashValue(static_cast<int>(node->kind)));
        switch (node->kind) {
            case EditorNodeGraph::NodeKind::Image:
                HashCombine(fingerprint, HashValue(node->image.width));
                HashCombine(fingerprint, HashValue(node->image.height));
                HashCombine(fingerprint, HashValue(node->image.channels));
                HashCombine(fingerprint, HashValue(node->title));
                break;
            case EditorNodeGraph::NodeKind::Layer:
                if (node->layerIndex >= 0 && node->layerIndex < static_cast<int>(m_Layers.size()) && m_Layers[node->layerIndex]) {
                    HashCombine(fingerprint, HashValue(m_Layers[node->layerIndex]->Serialize().dump()));
                }
                break;
            case EditorNodeGraph::NodeKind::Mix:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->mixBlendMode)));
                HashCombine(fingerprint, HashValue(node->mixFactor));
                break;
            case EditorNodeGraph::NodeKind::ImageGenerator:
                HashCombine(fingerprint, HashValue(static_cast<int>(node->imageGeneratorKind)));
                HashCombine(fingerprint, HashValue(node->imageGeneratorSettings.angle));
                HashCombine(fingerprint, HashValue(node->imageGeneratorSettings.offset));
                for (float channel : node->imageGeneratorSettings.colorA) {
                    HashCombine(fingerprint, HashValue(channel));
                }
                for (float channel : node->imageGeneratorSettings.colorB) {
                    HashCombine(fingerprint, HashValue(channel));
                }
                break;
            default:
                break;
        }
    }
    return fingerprint;
}

std::string EditorModule::BuildCompositeChainLabel(const EditorNodeGraph::CompletedChainInfo& chain) const {
    if (chain.outputNodeId <= 0) {
        return "Output";
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(chain.sourceNodeId);
    const EditorNodeGraph::Node* outputNode = m_NodeGraph.FindNode(chain.outputNodeId);
    const std::string sourceLabel = sourceNode && !sourceNode->title.empty()
        ? sourceNode->title
        : ("Source " + std::to_string(chain.sourceNodeId));
    const std::string outputLabel = outputNode && !outputNode->title.empty()
        ? outputNode->title
        : ("Output " + std::to_string(chain.outputNodeId));
    return sourceLabel + " -> " + outputLabel;
}

std::string EditorModule::BuildCompositeChainLabel(int outputNodeId) const {
    RefreshCompletedChainCacheIfNeeded();
    auto chainIt = std::find_if(
        m_CachedCompletedChains.begin(),
        m_CachedCompletedChains.end(),
        [outputNodeId](const CachedCompositeChainState& chain) { return chain.info.outputNodeId == outputNodeId; });
    if (chainIt == m_CachedCompletedChains.end()) {
        return "Output " + std::to_string(outputNodeId);
    }
    return chainIt->label.empty() ? BuildCompositeChainLabel(chainIt->info) : chainIt->label;
}

bool EditorModule::HasCompositeNode() const {
    return std::any_of(
        m_NodeGraph.GetNodes().begin(),
        m_NodeGraph.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) { return node.kind == EditorNodeGraph::NodeKind::Composite; });
}

void EditorModule::EnsureCompositeNode() {
    if (HasCompositeNode()) {
        return;
    }

    float anchorX = 300.0f;
    float anchorY = 280.0f;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            anchorX = std::max(anchorX, node.position.x - 240.0f);
            anchorY = std::max(anchorY, node.position.y + 180.0f);
        }
    }
    m_NodeGraph.AddCompositeNode(EditorNodeGraph::Vec2{ anchorX, anchorY });
}

bool EditorModule::HasExportBoundsSettingsNode() const {
    return std::any_of(
        m_NodeGraph.GetNodes().begin(),
        m_NodeGraph.GetNodes().end(),
        [](const EditorNodeGraph::Node& node) { return node.kind == EditorNodeGraph::NodeKind::ExportBoundsSettings; });
}

void EditorModule::EnsureExportBoundsSettingsNode() {
    if (HasExportBoundsSettingsNode()) {
        return;
    }

    float anchorX = 620.0f;
    float anchorY = 320.0f;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Composite) {
            anchorX = node.position.x + 320.0f;
            anchorY = node.position.y;
            break;
        }
        if (node.kind == EditorNodeGraph::NodeKind::Output) {
            anchorX = std::max(anchorX, node.position.x + 220.0f);
            anchorY = std::max(anchorY, node.position.y + 100.0f);
        }
    }
    m_NodeGraph.AddExportBoundsSettingsNode(EditorNodeGraph::Vec2{ anchorX, anchorY });
}

void EditorModule::RenderUI() {
    ConsumeRenderWorkerResults();
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetWorkspaceBaseColor());
    ImGui::BeginChild("StackEditorWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    const ImVec2 workspacePos = ImGui::GetCursorScreenPos();
    const ImVec2 workspaceSize = ImGui::GetContentRegionAvail();
    const bool advancedEditorOpen = IsAdvancedEditorOpen();
    const ImVec4 workspaceColor = GetWorkspaceBaseColor();
    const ImU32 workspaceColorU32 = ImGui::ColorConvertFloat4ToU32(workspaceColor);
    const ViewportMode viewportMode = GetViewportMode();
    HandleViewportModeTransition(m_LastViewportMode, viewportMode);
    const bool compositeViewportMode = viewportMode == ViewportMode::CompositeCanvas;
    EnsureCompositeSceneState(m_LastCompositeCanvasSize);
    ImGui::GetWindowDrawList()->AddRectFilled(
        workspacePos,
        ImVec2(workspacePos.x + workspaceSize.x, workspacePos.y + workspaceSize.y),
        workspaceColorU32);
    const float splitGap = 32.0f;
    const float minLeftWidth = 260.0f;
    const float minRightWidth = 420.0f;
    const float maxLeftWidth = std::max(minLeftWidth, workspaceSize.x - minRightWidth - splitGap);
    if (m_LeftPaneWidth <= 0.0f && !compositeViewportMode) {
        m_LeftPaneWidth = std::clamp(workspaceSize.x * (2.0f / 3.0f), minLeftWidth, maxLeftWidth);
    } else if (!compositeViewportMode) {
        m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, minLeftWidth, maxLeftWidth);
    }
    if (!advancedEditorOpen && CanConsumeEditorCommandKeys() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        TogglePartialSplitTargets(workspaceSize.x, minLeftWidth, maxLeftWidth, compositeViewportMode);
    }
    if (compositeViewportMode) {
        if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly && !m_DraggingSplitHandle && !m_SplitAutoAnimating) {
            m_LeftPaneWidth = workspaceSize.x;
        } else if (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly && !m_DraggingSplitHandle && !m_SplitAutoAnimating) {
            m_LeftPaneWidth = 0.0f;
        } else if (!m_DraggingSplitHandle && !m_SplitAutoAnimating) {
            m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, minLeftWidth, maxLeftWidth);
        } else {
            m_LeftPaneWidth = std::clamp(m_LeftPaneWidth, 0.0f, workspaceSize.x);
        }
    }
    const float effectiveSplitGap = compositeViewportMode
        ? splitGap * std::clamp(
            std::min(m_LeftPaneWidth, std::max(0.0f, workspaceSize.x - m_LeftPaneWidth)) / std::max(1.0f, splitGap),
            0.0f,
            1.0f)
        : splitGap;

    const float handleRadius = 7.0f;
    const ImVec2 handleCenter(
        std::clamp(
            workspacePos.x + m_LeftPaneWidth + (effectiveSplitGap * 0.5f),
            workspacePos.x + handleRadius + 2.0f,
            workspacePos.x + workspaceSize.x - handleRadius - 2.0f),
        workspacePos.y + 14.0f);
    bool handleHovered = false;
    if (m_SplitAutoAnimating) {
        const float t = static_cast<float>(std::clamp((ImGui::GetTime() - m_SplitAutoAnimStartTime) / 0.2, 0.0, 1.0));
        const float eased = 1.0f - std::pow(1.0f - t, 3.0f);
        m_LeftPaneWidth = m_SplitAutoAnimFrom + (m_SplitAutoAnimTo - m_SplitAutoAnimFrom) * eased;
        if (t >= 1.0f) {
            m_SplitAutoAnimating = false;
            if (compositeViewportMode) {
                m_CompositeEdgeSnapMode = m_SplitAutoAnimSnapMode;
            }
        }
    }

    ImDrawList* rootDrawList = ImGui::GetForegroundDrawList();
    const ImU32 handleFill = (m_DraggingSplitHandle || m_SplitHandlePressed)
        ? IM_COL32(92, 178, 255, 230)
        : (handleHovered ? IM_COL32(72, 148, 214, 214) : IM_COL32(52, 92, 120, 188));
    rootDrawList->AddCircleFilled(handleCenter, handleRadius, handleFill, 32);

    const float paneHeight = std::max(0.0f, workspaceSize.y);
    const float rightWidth = std::max(0.0f, workspaceSize.x - m_LeftPaneWidth - effectiveSplitGap);

    if (m_LeftPaneWidth > 1.0f) {
        ImGui::SetCursorScreenPos(workspacePos);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
        ImGui::BeginChild("EditorGraphPane", ImVec2(m_LeftPaneWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        m_Sidebar.Render(this);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    ImVec2 viewportPaneMin(workspacePos.x + m_LeftPaneWidth + effectiveSplitGap, workspacePos.y);
    ImVec2 viewportPaneMax(viewportPaneMin.x + rightWidth, workspacePos.y + paneHeight);
    bool viewportPaneRendered = false;
    if (rightWidth > 1.0f) {
        ImGui::SetCursorScreenPos(ImVec2(workspacePos.x + m_LeftPaneWidth + effectiveSplitGap, workspacePos.y));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, workspaceColor);
        ImGui::BeginChild("EditorViewportPane", ImVec2(rightWidth, paneHeight), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        m_Viewport.Render(this);
        ImGui::EndChild();
        viewportPaneMin = ImGui::GetItemRectMin();
        viewportPaneMax = ImGui::GetItemRectMax();
        viewportPaneRendered = true;
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }

    ImVec2 handleOverlayPos(handleCenter.x - 12.0f, handleCenter.y - 12.0f);
    ImVec2 handleOverlaySize(24.0f, 24.0f);
    if (compositeViewportMode && (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::ViewportOnly || m_LeftPaneWidth <= 2.0f)) {
        handleOverlayPos = ImVec2(workspacePos.x, workspacePos.y + 2.0f);
        handleOverlaySize = ImVec2(28.0f, std::min(38.0f, std::max(24.0f, paneHeight)));
    } else if (compositeViewportMode && (m_CompositeEdgeSnapMode == CompositeEdgeSnapMode::GraphOnly || m_LeftPaneWidth >= workspaceSize.x - 2.0f)) {
        handleOverlayPos = ImVec2(workspacePos.x + workspaceSize.x - 28.0f, workspacePos.y + 2.0f);
        handleOverlaySize = ImVec2(28.0f, std::min(38.0f, std::max(24.0f, paneHeight)));
    }

    ImGui::SetCursorScreenPos(handleOverlayPos);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("EditorSplitHandleOverlay", handleOverlaySize, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorScreenPos(ImVec2(handleCenter.x - 10.0f, handleCenter.y - 10.0f));
    ImGui::InvisibleButton("EditorSplitHandle", ImVec2(20.0f, 20.0f));
    handleHovered = ImGui::IsItemHovered();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    if (handleHovered || m_DraggingSplitHandle || m_SplitHandlePressed) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (!advancedEditorOpen && handleHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_SplitHandlePressed = true;
        m_SplitHandleMoved = false;
        m_SplitAutoAnimating = false;
    }
    if (!advancedEditorOpen && m_SplitHandlePressed && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (std::abs(ImGui::GetMouseDragDelta(ImGuiMouseButton_Left).x) > 2.0f) {
            m_DraggingSplitHandle = true;
            m_SplitHandleMoved = true;
            if (compositeViewportMode) {
                m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
            }
        }
    }

    const bool viewportPaneHovered = ImGui::IsMouseHoveringRect(viewportPaneMin, viewportPaneMax);
    const bool viewportSplitDragAllowed = !advancedEditorOpen && viewportPaneHovered && !IsPickingColor() && !compositeViewportMode;
    if ((viewportSplitDragAllowed || m_DraggingSplitHandle) && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (viewportSplitDragAllowed && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        m_DraggingSplitHandle = true;
    }
    if (advancedEditorOpen) {
        m_DraggingSplitHandle = false;
        m_SplitHandlePressed = false;
        m_SplitHandleMoved = false;
    } else if (m_DraggingSplitHandle) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (compositeViewportMode) {
                m_CompositeEdgeSnapMode = CompositeEdgeSnapMode::None;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, 0.0f, workspaceSize.x);
            } else {
                m_LeftPaneWidth = std::clamp(m_LeftPaneWidth + ImGui::GetIO().MouseDelta.x, minLeftWidth, maxLeftWidth);
            }
        } else {
            m_DraggingSplitHandle = false;
        }
    }
    if (m_SplitHandlePressed && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (!m_SplitHandleMoved) {
            const float leftTarget = compositeViewportMode ? 0.0f : minLeftWidth;
            const float rightTarget = compositeViewportMode ? workspaceSize.x : maxLeftWidth;
            const float centerThreshold = workspaceSize.x * 0.5f;
            const float currentCenter = m_LeftPaneWidth + effectiveSplitGap * 0.5f;
            m_SplitAutoAnimFrom = m_LeftPaneWidth;
            if (compositeViewportMode && m_LeftPaneWidth <= 2.0f) {
                m_SplitAutoAnimTo = rightTarget;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::GraphOnly;
            } else if (compositeViewportMode && m_LeftPaneWidth >= workspaceSize.x - 2.0f) {
                m_SplitAutoAnimTo = leftTarget;
                m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::ViewportOnly;
            } else {
                m_SplitAutoAnimTo = currentCenter < centerThreshold ? leftTarget : rightTarget;
                if (compositeViewportMode) {
                    m_SplitAutoAnimSnapMode = m_SplitAutoAnimTo <= 0.0f
                        ? CompositeEdgeSnapMode::ViewportOnly
                        : CompositeEdgeSnapMode::GraphOnly;
                } else {
                    m_SplitAutoAnimSnapMode = CompositeEdgeSnapMode::None;
                }
            }
            m_SplitAutoAnimStartTime = ImGui::GetTime();
            m_SplitAutoAnimating = true;
        }
        m_SplitHandlePressed = false;
        m_SplitHandleMoved = false;
    }

    RenderAdvancedEditorOverlay(workspacePos, workspaceSize);

    SubmitRenderIfReady();

    if (IsSourceLoadBusy()) {
        ImGuiExtras::RenderBusyOverlay("Loading source image...");
    } else if (IsExportBusy()) {
        ImGuiExtras::RenderBusyOverlay(GetExportStatusText().empty() ? "Exporting..." : GetExportStatusText().c_str());
    }

    ImGui::EndChild();
}

EditorModule::ViewportMode EditorModule::GetViewportMode() const {
    RefreshCompletedChainCacheIfNeeded();
    return GetCompletedChainCount() >= 2
        ? ViewportMode::CompositeCanvas
        : ViewportMode::SingleOutputPreview;
}

int EditorModule::GetCompletedChainCount() const {
    RefreshCompletedChainCacheIfNeeded();
    return static_cast<int>(m_CachedCompletedChains.size());
}

int EditorModule::GetConnectedOutputCount() const {
    RefreshCompletedChainCacheIfNeeded();
    return m_CachedConnectedOutputCount;
}

std::uint64_t EditorModule::GetPreviewNodeRevision(int previewNodeId) const {
    const EditorNodeGraph::Link* input =
        m_NodeGraph.FindAnyInputLink(previewNodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return 0;
    }
    return std::max<std::uint64_t>(1, GetNodeDirtyGeneration(input->fromNodeId));
}

const EditorModule::GraphPreviewPixels* EditorModule::GetCachedPreviewPixelsForNode(int previewNodeId) const {
    const auto it = m_PreviewPixelCache.find(previewNodeId);
    return it != m_PreviewPixelCache.end() ? &it->second : nullptr;
}

std::uint64_t EditorModule::GetScopeNodeRevision(int sourceNodeId) const {
    if (sourceNodeId <= 0) {
        m_ScopeDisplayedRevisions[sourceNodeId] = 0;
        return 0;
    }

    const std::uint64_t desiredRevision = GetNodeDirtyGeneration(sourceNodeId);
    std::uint64_t& displayedRevision = m_ScopeDisplayedRevisions[sourceNodeId];
    if (CanRefreshPreviewLikeNodes() && !HasPendingPreviewRefreshes()) {
        displayedRevision = desiredRevision;
    }
    return displayedRevision;
}

ImVec4 EditorModule::GetWorkspaceBaseColor() const {
    return kEditorWorkspaceBaseColor;
}

bool EditorModule::CanConsumeEditorCommandKeys() const {
    return !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive();
}
