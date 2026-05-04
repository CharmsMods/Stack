#include "EditorModule.h"

#include "Async/TaskSystem.h"
#include "NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
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

std::string FileNameFromPath(const std::string& path) {
    try {
        return std::filesystem::path(path).filename().string();
    } catch (...) {
        return path.empty() ? std::string("Image") : path;
    }
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

EditorModule::~EditorModule() {}

void EditorModule::Initialize(GLFWwindow* sharedWindow) {
    std::vector<std::string> registryErrors;
    if (!LayerRegistry::ValidateRegistry(&registryErrors)) {
        for (const std::string& error : registryErrors) {
            fprintf(stderr, "LayerRegistry validation failed: %s\n", error.c_str());
        }
    }

    m_Pipeline.Initialize();
    m_Sidebar.Initialize();
    m_Viewport.Initialize();
    m_Scopes.Initialize();
    m_RenderWorkerAvailable = sharedWindow && m_RenderWorker.Initialize(sharedWindow);

    m_Layers.clear();
    m_SelectedLayerIndex = -1;
    m_NodeGraph.ResetFromLayers(0, false);
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
    if (!node || node->kind == EditorNodeGraph::NodeKind::Output) {
        return false;
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

void EditorModule::AutoLayoutGraph() {
    m_NodeGraph.AutoLayout();
}

void EditorModule::DisconnectGraphOutput() {
    m_NodeGraph.DisconnectOutput();
    m_Pipeline.ClearOutput();
    MarkRenderDirty();
}

void EditorModule::SetGraphDropTargetRect(float minX, float minY, float maxX, float maxY) {
    m_GraphDropMinX = minX;
    m_GraphDropMinY = minY;
    m_GraphDropMaxX = maxX;
    m_GraphDropMaxY = maxY;
}

void EditorModule::SetGraphViewTransform(float originX, float originY, float panX, float panY, float zoom) {
    m_GraphViewOriginX = originX;
    m_GraphViewOriginY = originY;
    m_GraphViewPanX = panX;
    m_GraphViewPanY = panY;
    m_GraphViewZoom = std::max(0.01f, zoom);
}

bool EditorModule::IsScreenPointOverGraph(float x, float y) const {
    return x >= m_GraphDropMinX && x <= m_GraphDropMaxX && y >= m_GraphDropMinY && y <= m_GraphDropMaxY;
}

bool EditorModule::HandleGraphFileDrop(const std::string& path, float screenX, float screenY) {
    if (!IsScreenPointOverGraph(screenX, screenY)) {
        return false;
    }

    const float safeZoom = std::max(0.01f, m_GraphViewZoom);
    const EditorNodeGraph::Vec2 graphPosition{
        (screenX - m_GraphViewOriginX - m_GraphViewPanX) / safeZoom - 40.0f,
        (screenY - m_GraphViewOriginY - m_GraphViewPanY) / safeZoom - 40.0f
    };
    return AddImageNodeFromFile(path, graphPosition);
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

std::vector<unsigned char> EditorModule::GetScopePixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return {};
    }

    if (!IsImageOutputNode(node->kind) && !IsMaskOutputNode(node->kind)) {
        return {};
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (node->kind == EditorNodeGraph::NodeKind::Image && !node->image.pixels.empty() &&
        node->image.width > 0 && node->image.height > 0) {
        sourcePixels = node->image.pixels;
        sourceW = node->image.width;
        sourceH = node->image.height;
        sourceCh = std::max(1, node->image.channels);
    } else {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& graphNode : m_NodeGraph.GetNodes()) {
            if (graphNode.kind == EditorNodeGraph::NodeKind::Image && !graphNode.image.pixels.empty() &&
                graphNode.image.width > 0 && graphNode.image.height > 0) {
                sourcePixels = graphNode.image.pixels;
                sourceW = graphNode.image.width;
                sourceH = graphNode.image.height;
                sourceCh = std::max(1, graphNode.image.channels);
                break;
            }
        }
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    if (IsMaskOutputNode(node->kind) || node->kind == EditorNodeGraph::NodeKind::Output) {
        snapshot.outputNodeId = nodeId;
    } else {
        const int syntheticOutputId = -200000 - nodeId;
        RenderGraphNode outputNode;
        outputNode.nodeId = syntheticOutputId;
        outputNode.kind = RenderGraphNodeKind::Output;
        snapshot.nodes.push_back(std::move(outputNode));
        snapshot.links.push_back(RenderGraphLink{
            nodeId,
            EditorNodeGraph::kImageOutputSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
        snapshot.outputNodeId = syntheticOutputId;
    }

    RenderPipeline scopePipeline;
    scopePipeline.Initialize();
    scopePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, sourceCh);
    scopePipeline.ExecuteGraph(snapshot);
    return scopePipeline.GetScopesPixels(outW, outH);
}

std::vector<unsigned char> EditorModule::GetPreviewPixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* previewNode = m_NodeGraph.FindNode(nodeId);
    if (!previewNode || previewNode->kind != EditorNodeGraph::NodeKind::Preview) {
        return {};
    }

    const EditorNodeGraph::Link* input = m_NodeGraph.FindAnyInputLink(nodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return {};
    }

    EditorNodeGraph::SocketDefinition sourceSocket;
    if (!m_NodeGraph.FindSocket(input->fromNodeId, input->fromSocketId, &sourceSocket)) {
        return {};
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(input->fromNodeId);
    if (!sourceNode) {
        return {};
    }

    if (sourceSocket.type != EditorNodeGraph::SocketType::Image &&
        sourceSocket.type != EditorNodeGraph::SocketType::Mask) {
        return {};
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (sourceNode->kind == EditorNodeGraph::NodeKind::Image && !sourceNode->image.pixels.empty() &&
        sourceNode->image.width > 0 && sourceNode->image.height > 0) {
        sourcePixels = sourceNode->image.pixels;
        sourceW = sourceNode->image.width;
        sourceH = sourceNode->image.height;
        sourceCh = std::max(1, sourceNode->image.channels);
    } else {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Image && !node.image.pixels.empty() &&
                node.image.width > 0 && node.image.height > 0) {
                sourcePixels = node.image.pixels;
                sourceW = node.image.width;
                sourceH = node.image.height;
                sourceCh = std::max(1, node.image.channels);
                break;
            }
        }
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -100000 - nodeId;
    if (sourceSocket.type == EditorNodeGraph::SocketType::Mask) {
        snapshot.outputNodeId = input->fromNodeId;
    } else {
        RenderGraphNode outputNode;
        outputNode.nodeId = syntheticOutputId;
        outputNode.kind = RenderGraphNodeKind::Output;
        snapshot.nodes.push_back(std::move(outputNode));
        snapshot.links.push_back(RenderGraphLink{
            input->fromNodeId,
            input->fromSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
        snapshot.outputNodeId = syntheticOutputId;
    }

    RenderPipeline previewPipeline;
    previewPipeline.Initialize();
    previewPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    previewPipeline.ExecuteGraph(snapshot);
    return previewPipeline.GetScopesPixels(outW, outH);
}

void EditorModule::RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId) {
    m_Scopes.RenderScopeNode(this, scopeKind, sourceNodeId);
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
    snapshot.outputNodeId = m_NodeGraph.GetOutputNodeId();

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

void EditorModule::MarkRenderDirty() {
    m_RenderDirty = true;
    m_LastRenderDirtyTime = ImGui::GetTime();
}

EditorRenderWorker::Snapshot EditorModule::BuildRenderSnapshot(std::uint64_t generation) const {
    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.outputConnected = m_NodeGraph.IsOutputConnected();
    snapshot.sourcePixels = m_Pipeline.GetSourcePixelsRaw();
    snapshot.width = m_Pipeline.GetCanvasWidth();
    snapshot.height = m_Pipeline.GetCanvasHeight();
    snapshot.channels = m_Pipeline.GetSourceChannels();
    snapshot.graph = BuildGraphSnapshot();
    snapshot.masks = BuildGraphRenderMasks();
    for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
        if (step.layer) {
            nlohmann::json item = nlohmann::json::object();
            item["layer"] = step.layer->Serialize();
            item["maskNodeId"] = step.maskNodeId;
            snapshot.layerSteps.push_back(std::move(item));
            snapshot.layers.push_back(step.layer->Serialize());
        }
    }
    return snapshot;
}

std::string EditorModule::BuildRenderSignature() const {
    nlohmann::json signature = nlohmann::json::object();
    signature["connected"] = m_NodeGraph.IsOutputConnected();
    signature["activeImage"] = m_NodeGraph.GetActiveImageNodeId();
    signature["sourceW"] = m_Pipeline.GetCanvasWidth();
    signature["sourceH"] = m_Pipeline.GetCanvasHeight();
    signature["sourceSize"] = m_Pipeline.GetSourcePixelsRaw().size();
    signature["steps"] = nlohmann::json::array();
    for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
        if (step.layer) {
            signature["steps"].push_back({
                { "layer", step.layer->Serialize() },
                { "maskNodeId", step.maskNodeId }
            });
        }
    }
    signature["masks"] = nlohmann::json::array();
    for (const RenderMaskSource& mask : BuildGraphRenderMasks()) {
        signature["masks"].push_back({
            { "nodeId", mask.nodeId },
            { "kind", static_cast<int>(mask.kind) },
            { "value", mask.settings.value },
            { "angle", mask.settings.angle },
            { "offset", mask.settings.offset },
            { "scale", mask.settings.scale },
            { "centerX", mask.settings.centerX },
            { "centerY", mask.settings.centerY },
            { "radius", mask.settings.radius },
            { "feather", mask.settings.feather },
            { "invert", mask.settings.invert }
        });
    }
    signature["graphNodes"] = nlohmann::json::array();
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind == EditorNodeGraph::NodeKind::Scope ||
            node.kind == EditorNodeGraph::NodeKind::Preview) {
            continue;
        }
        nlohmann::json item = {
            { "id", node.id },
            { "kind", static_cast<int>(node.kind) },
            { "layerIndex", node.layerIndex },
            { "mixMode", static_cast<int>(node.mixBlendMode) },
            { "mixFactor", node.mixFactor },
            { "maskKind", static_cast<int>(node.maskKind) },
            { "maskValue", node.maskSettings.value },
            { "maskAngle", node.maskSettings.angle },
            { "maskOffset", node.maskSettings.offset },
            { "maskScale", node.maskSettings.scale },
            { "maskCenterX", node.maskSettings.centerX },
            { "maskCenterY", node.maskSettings.centerY },
            { "maskRadius", node.maskSettings.radius },
            { "maskFeather", node.maskSettings.feather },
            { "maskInvert", node.maskSettings.invert },
            { "maskUtilityKind", static_cast<int>(node.maskUtilityKind) },
            { "utilityBlack", node.maskUtilitySettings.blackPoint },
            { "utilityWhite", node.maskUtilitySettings.whitePoint },
            { "utilityGamma", node.maskUtilitySettings.gamma },
            { "utilityThreshold", node.maskUtilitySettings.threshold },
            { "utilitySoftness", node.maskUtilitySettings.softness },
            { "utilityInvert", node.maskUtilitySettings.invert },
            { "imageToMaskKind", static_cast<int>(node.imageToMaskKind) },
            { "lumLow", node.imageToMaskSettings.low },
            { "lumHigh", node.imageToMaskSettings.high },
            { "lumSoftness", node.imageToMaskSettings.softness },
            { "lumInvert", node.imageToMaskSettings.invert },
            { "imageGeneratorKind", static_cast<int>(node.imageGeneratorKind) },
            { "imageGeneratorAngle", node.imageGeneratorSettings.angle },
            { "imageGeneratorOffset", node.imageGeneratorSettings.offset }
        };
        nlohmann::json colorA = nlohmann::json::array();
        nlohmann::json colorB = nlohmann::json::array();
        for (int i = 0; i < 4; ++i) {
            colorA.push_back(node.imageGeneratorSettings.colorA[i]);
            colorB.push_back(node.imageGeneratorSettings.colorB[i]);
        }
        item["imageGeneratorColorA"] = std::move(colorA);
        item["imageGeneratorColorB"] = std::move(colorB);
        if (node.kind == EditorNodeGraph::NodeKind::Image) {
            item["imageSize"] = node.image.pixels.size();
            item["imageW"] = node.image.width;
            item["imageH"] = node.image.height;
        }
        signature["graphNodes"].push_back(std::move(item));
    }
    signature["graphLinks"] = nlohmann::json::array();
    for (const EditorNodeGraph::Link& link : m_NodeGraph.GetLinks()) {
        if (m_NodeGraph.GetLinkRole(link) == EditorNodeGraph::LinkRole::Scope) {
            continue;
        }
        signature["graphLinks"].push_back({
            { "from", link.fromNodeId },
            { "fromSocket", link.fromSocketId },
            { "to", link.toNodeId },
            { "toSocket", link.toSocketId }
        });
    }
    return signature.dump();
}

void EditorModule::ConsumeRenderWorkerResults() {
    EditorRenderWorker::Result result;
    while (m_RenderWorker.TryConsumeCompleted(result)) {
        if (result.generation < m_RenderGeneration) {
            continue;
        }
        m_RenderPending = false;
        if (result.success && !result.pixels.empty()) {
            m_Pipeline.UploadOutputFromPixels(result.pixels.data(), result.width, result.height, 4);
            m_LastCompletedRenderGeneration = result.generation;
        } else if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        }
    }
}

void EditorModule::SubmitRenderIfReady() {
    const std::string signature = BuildRenderSignature();
    if (signature != m_LastRenderSignature) {
        m_LastRenderSignature = signature;
        MarkRenderDirty();
    }

    if (!m_RenderDirty || m_RenderPending) {
        return;
    }
    if (ImGui::GetTime() - m_LastRenderDirtyTime < 0.02) {
        return;
    }

    ++m_RenderGeneration;
    m_RenderDirty = false;
    if (!m_NodeGraph.IsOutputConnected()) {
        m_Pipeline.ClearOutput();
        m_RenderPending = false;
        return;
    }

    if (m_RenderWorkerAvailable) {
        m_RenderPending = true;
        m_RenderWorker.Submit(BuildRenderSnapshot(m_RenderGeneration));
    } else {
        m_Pipeline.ExecuteGraph(BuildGraphSnapshot());
        m_LastCompletedRenderGeneration = m_RenderGeneration;
    }
}

void EditorModule::RenderUI() {
    ConsumeRenderWorkerResults();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
                           | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("StackEditorWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleVar();

    ImGuiID editorDockId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(editorDockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    static bool first = true;
    if (first) {
        first = false;
        ImGui::DockBuilderRemoveNode(editorDockId);
        ImGui::DockBuilderAddNode(editorDockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(editorDockId, ImGui::GetWindowSize());

        ImGuiID left = 0;
        ImGuiID main = editorDockId;
        left = ImGui::DockBuilderSplitNode(main, ImGuiDir_Left, 0.25f, nullptr, &main);

        ImGui::DockBuilderDockWindow("Inspector Panel Sidebar", left);
        ImGui::DockBuilderDockWindow("Canvas Viewport", main);
        ImGui::DockBuilderFinish(editorDockId);
    }

    m_Sidebar.Render(this);
    m_Viewport.Render(this);
    SubmitRenderIfReady();

    if (IsSourceLoadBusy()) {
        ImGuiExtras::RenderBusyOverlay("Loading source image...");
    } else if (IsExportBusy()) {
        ImGuiExtras::RenderBusyOverlay("Exporting image...");
    } else if (Async::IsBusy(LibraryManager::Get().GetSaveTaskState())) {
        ImGuiExtras::RenderBusyOverlay(LibraryManager::Get().GetSaveStatusText().c_str());
    }

    ImGui::EndChild();
}

nlohmann::json EditorModule::SerializePipeline() {
    json serialized = json::array();
    for (auto& layer : m_Layers) {
        serialized.push_back(layer->Serialize());
    }
    return EditorNodeGraph::SerializeGraphPayload(serialized, m_NodeGraph);
}

void EditorModule::DeserializePipeline(const nlohmann::json& serialized) {
    m_Layers.clear();
    const nlohmann::json layers = EditorNodeGraph::ExtractLayerArray(serialized);
    if (!layers.is_array()) return;

    for (const auto& layerData : layers) {
        std::string type = layerData.value("type", "");
        std::shared_ptr<LayerBase> newLayer = LayerRegistry::CreateLayerFromTypeId(type);

        if (newLayer) {
            newLayer->InitializeGL();
            newLayer->Deserialize(layerData);
            m_Layers.push_back(newLayer);
        }
    }

    m_SelectedLayerIndex = m_Layers.empty() ? -1 : 0;
    int sourceWidth = 0;
    int sourceHeight = 0;
    const std::vector<unsigned char>& sourcePixels = m_Pipeline.GetSourcePixelsRaw();
    if (!sourcePixels.empty()) {
        sourceWidth = m_Pipeline.GetCanvasWidth();
        sourceHeight = m_Pipeline.GetCanvasHeight();
    }

    EditorNodeGraph::DeserializeGraphPayload(
        serialized,
        m_NodeGraph,
        static_cast<int>(m_Layers.size()),
        sourcePixels,
        sourceWidth,
        sourceHeight,
        m_Pipeline.GetSourceChannels());
    RefreshGraphLayerMetadata();

    const int activeImageNodeId = m_NodeGraph.GetActiveImageNodeId();
    if (activeImageNodeId > 0) {
        if (EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(activeImageNodeId)) {
            if (imageNode->kind == EditorNodeGraph::NodeKind::Image && !imageNode->image.pixels.empty()) {
                LoadSourceFromPixels(imageNode->image.pixels.data(), imageNode->image.width, imageNode->image.height, imageNode->image.channels);
            }
        }
        ApplyGraphLayerOrder();
    } else {
        m_Pipeline.ClearOutput();
    }
    MarkRenderDirty();
}

void EditorModule::LoadSourceFromPixels(const unsigned char* data, int w, int h, int ch) {
    m_Pipeline.LoadSourceFromPixels(data, w, h, ch);
    MarkRenderDirty();
}

bool EditorModule::ApplyLoadedProject(const LoadedProjectData& projectData) {
    if (projectData.sourcePixels.empty() || projectData.width <= 0 || projectData.height <= 0) {
        return false;
    }

    LoadSourceFromPixels(projectData.sourcePixels.data(), projectData.width, projectData.height, projectData.channels);
    DeserializePipeline(projectData.pipelineData);
    SetCurrentProjectName(projectData.projectName);
    SetCurrentProjectFileName(projectData.projectFileName);
    return true;
}

void EditorModule::RequestLoadSourceImage(const std::string& path) {
    if (path.empty()) {
        return;
    }

    ++m_SourceLoadGeneration;
    const std::uint64_t generation = m_SourceLoadGeneration;
    m_SourceLoadTaskState = Async::TaskState::Queued;
    m_SourceLoadStatusText = "Loading source image in the background...";

    Async::TaskSystem::Get().Submit([this, generation, path]() {
        DecodedImageData decoded;
        const bool success = DecodeImageFromFile(path, decoded);

        Async::TaskSystem::Get().PostToMain([this, generation, path, decoded = std::move(decoded), success]() mutable {
            if (generation != m_SourceLoadGeneration) {
                return;
            }

            if (!success || decoded.pixels.empty()) {
                m_SourceLoadTaskState = Async::TaskState::Failed;
                m_SourceLoadStatusText = "Failed to load the selected source image.";
                return;
            }

            m_SourceLoadTaskState = Async::TaskState::Applying;
            m_SourceLoadStatusText = "Applying source image to the editor...";

            LoadSourceFromPixels(decoded.pixels.data(), decoded.width, decoded.height, decoded.channels);
            EditorNodeGraph::ImagePayload payload;
            payload.label = FileNameFromPath(path);
            payload.sourcePath = path;
            payload.width = decoded.width;
            payload.height = decoded.height;
            payload.channels = decoded.channels;
            payload.pngBytes = EncodePngBytes(decoded.pixels, decoded.width, decoded.height, decoded.channels);
            payload.pixels = std::move(decoded.pixels);

            EditorNodeGraph::Node* imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
            if (!imageNode || imageNode->kind != EditorNodeGraph::NodeKind::Image) {
                m_NodeGraph.ResetFromLayers(static_cast<int>(m_Layers.size()), true);
                imageNode = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId());
            } else if (!m_NodeGraph.IsOutputConnected()) {
                m_NodeGraph.RebuildLinks();
            }

            if (imageNode) {
                imageNode->title = payload.label.empty() ? "Image" : payload.label;
                imageNode->image = std::move(payload);
                m_NodeGraph.SetActiveImageNodeId(imageNode->id);
            }
            SetCurrentProjectName("");
            SetCurrentProjectFileName("");

            m_SourceLoadTaskState = Async::TaskState::Idle;
            m_SourceLoadStatusText = "Source image loaded.";
        });
    });
}

bool EditorModule::ExportImage(const std::string& path) {
    return RequestExportImage(path);
}

bool EditorModule::RequestExportImage(const std::string& path) {
    if (path.empty() || !m_Pipeline.HasSourceImage()) {
        return false;
    }

    if (Async::IsBusy(m_ExportTaskState)) {
        return false;
    }

    m_ExportTaskState = Async::TaskState::Applying;
    m_ExportStatusText = "Capturing the rendered image...";

    m_Pipeline.ExecuteGraph(BuildGraphSnapshot());

    int width = 0;
    int height = 0;
    auto pixels = m_Pipeline.GetOutputPixels(width, height);
    if (pixels.empty()) {
        m_ExportTaskState = Async::TaskState::Failed;
        m_ExportStatusText = "Failed to capture the rendered image.";
        return false;
    }

    if (!m_CurrentProjectName.empty()) {
        LibraryManager::Get().RequestSaveProject(m_CurrentProjectName, this, m_CurrentProjectFileName);
    }

    ++m_ExportGeneration;
    const std::uint64_t generation = m_ExportGeneration;
    m_ExportTaskState = Async::TaskState::Running;
    m_ExportStatusText = "Writing PNG export in the background...";

    Async::TaskSystem::Get().Submit([this, generation, path, width, height, pixels = std::move(pixels)]() mutable {
        bool success = false;

        try {
            const std::filesystem::path destination(path);
            if (destination.has_parent_path()) {
                std::filesystem::create_directories(destination.parent_path());
            }

            success = stbi_write_png(
                destination.string().c_str(),
                width,
                height,
                4,
                pixels.data(),
                width * 4) != 0;
        } catch (...) {
            success = false;
        }

        Async::TaskSystem::Get().PostToMain([this, generation, success]() {
            if (generation != m_ExportGeneration) {
                return;
            }

            if (success) {
                m_ExportTaskState = Async::TaskState::Idle;
                m_ExportStatusText = "Rendered image exported.";
            } else {
                m_ExportTaskState = Async::TaskState::Failed;
                m_ExportStatusText = "Failed to write the exported PNG.";
            }
        });
    });

    return true;
}
