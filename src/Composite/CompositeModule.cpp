#include "Composite/CompositeModule.h"
#include <iostream>
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include "Utils/FileDialogs.h"
#include "Library/LibraryManager.h"
#include "Editor/EditorModule.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <imgui.h>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace {

using json = StackBinaryFormat::json;

constexpr int kCompositeFormatVersion = 1;

void png_write_vec(void* context, void* data, int size) {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* bytes = static_cast<const unsigned char*>(data);
    vec->insert(vec->end(), bytes, bytes + size);
}

std::string NewLayerId() {
    static int s_counter = 0;
    ++s_counter;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "layer-%d-%ld", s_counter, static_cast<long>(std::time(nullptr)));
    return std::string(buf);
}

void BlendSourceOver(
    std::vector<uint8_t>& canvas,
    int canvasW,
    int canvasH,
    int dstX,
    int dstY,
    const std::vector<uint8_t>& src,
    int srcW,
    int srcH,
    float opacity) {

    if (src.empty() || srcW <= 0 || srcH <= 0 || canvas.empty() || canvasW <= 0 || canvasH <= 0) return;

    const float op = std::max(0.0f, std::min(1.0f, opacity));
    for (int sy = 0; sy < srcH; ++sy) {
        const int dy = dstY + sy;
        if (dy < 0 || dy >= canvasH) continue;
        for (int sx = 0; sx < srcW; ++sx) {
            const int dx = dstX + sx;
            if (dx < 0 || dx >= canvasW) continue;
            const std::size_t di = (static_cast<std::size_t>(dy) * static_cast<std::size_t>(canvasW) + static_cast<std::size_t>(dx)) * 4;
            const std::size_t si = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(srcW) + static_cast<std::size_t>(sx)) * 4;
            const float sr = src[si + 0] / 255.0f;
            const float sg = src[si + 1] / 255.0f;
            const float sb = src[si + 2] / 255.0f;
            const float sa = (src[si + 3] / 255.0f) * op;
            const float dr = canvas[di + 0] / 255.0f;
            const float dg = canvas[di + 1] / 255.0f;
            const float db = canvas[di + 2] / 255.0f;
            const float da = canvas[di + 3] / 255.0f;
            const float outA = sa + da * (1.0f - sa);
            if (outA <= 1e-5f) {
                canvas[di + 0] = canvas[di + 1] = canvas[di + 2] = canvas[di + 3] = 0;
                continue;
            }
            const float inv = 1.0f / outA;
            canvas[di + 0] = static_cast<unsigned char>(std::clamp((sr * sa + dr * da * (1.0f - sa)) * inv * 255.0f, 0.0f, 255.0f));
            canvas[di + 1] = static_cast<unsigned char>(std::clamp((sg * sa + dg * da * (1.0f - sa)) * inv * 255.0f, 0.0f, 255.0f));
            canvas[di + 2] = static_cast<unsigned char>(std::clamp((sb * sa + db * da * (1.0f - sa)) * inv * 255.0f, 0.0f, 255.0f));
            canvas[di + 3] = static_cast<unsigned char>(std::clamp(outA * 255.0f, 0.0f, 255.0f));
        }
    }
}

void ResizeNearest(
    const std::vector<uint8_t>& src,
    int sw,
    int sh,
    int dw,
    int dh,
    std::vector<uint8_t>& out) {

    out.resize(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4);
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, static_cast<int>(static_cast<float>(y) * static_cast<float>(sh) / static_cast<float>(dh)));
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(sw - 1, static_cast<int>(static_cast<float>(x) * static_cast<float>(sw) / static_cast<float>(dw)));
            const std::size_t si = (static_cast<std::size_t>(sy) * static_cast<std::size_t>(sw) + static_cast<std::size_t>(sx)) * 4;
            const std::size_t di = (static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)) * 4;
            out[di + 0] = src[si + 0];
            out[di + 1] = src[si + 1];
            out[di + 2] = src[si + 2];
            out[di + 3] = src[si + 3];
        }
    }
}

} // namespace

CompositeModule::CompositeModule() = default;
CompositeModule::~CompositeModule() { Shutdown(); }

void CompositeModule::Initialize() {
    if (m_Initialized) return;
    m_Initialized = true;
}

void CompositeModule::Shutdown() {
    ClearLayersGpu();
    m_Layers.clear();
    m_Initialized = false;
}

void CompositeModule::ClearLayersGpu() {
    for (CompositeLayer& layer : m_Layers) {
        if (layer.tex != 0) {
            glDeleteTextures(1, &layer.tex);
            layer.tex = 0;
        }
    }
}

void CompositeModule::SyncLayerTextures() {
    for (CompositeLayer& layer : m_Layers) {
        if (layer.tex != 0 || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) continue;
        layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
    }
}

void CompositeModule::NewProject() {
    ClearLayersGpu();
    m_Layers.clear();
    m_SelectedId.clear();
    m_ViewZoom = 1.0f;
    m_ViewPanX = 0.0f;
    m_ViewPanY = 0.0f;
    m_ProjectName = "Untitled Composite";
    m_ProjectFileName.clear();
    m_Dirty = false;
}

CompositeLayer* CompositeModule::GetSelectedLayer() {
    if (m_SelectedId.empty()) return nullptr;
    for (auto& L : m_Layers) {
        if (L.id == m_SelectedId) return &L;
    }
    return nullptr;
}

void CompositeModule::UpdateLayerData(const std::string& layerId, const std::string& projectJson, const std::vector<uint8_t>& previewPixels, int w, int h) {
    for (auto& L : m_Layers) {
        if (L.id == layerId) {
            L.embeddedProjectJson = projectJson;
            L.rgba = previewPixels;
            L.imgW = w;
            L.imgH = h;
            if (L.tex != 0) glDeleteTextures(1, &L.tex);
            L.tex = GLHelpers::CreateTextureFromPixels(L.rgba.data(), L.imgW, L.imgH, 4);
            L.kind = LayerKind::EditorProject;
            m_Dirty = true;
            break;
        }
    }
}

void CompositeModule::AddProjectLayer(const std::string& name, const std::string& projectJson, const std::vector<uint8_t>& previewPixels, int texW, int texH, int logicalW, int logicalH) {
    CompositeLayer L;
    L.id = NewLayerId();
    L.name = name;
    L.kind = LayerKind::EditorProject;
    L.embeddedProjectJson = projectJson;
    L.rgba = previewPixels;
    L.imgW = texW;
    L.imgH = texH;
    L.logicalW = logicalW;
    L.logicalH = logicalH;
    L.visible = true;
    L.opacity = 1.0f;
    L.scale = 1.0f;
    L.rotation = 0.0f;
    
    // Z-order: top
    int maxZ = -1;
    for (const auto& existing : m_Layers) maxZ = std::max(maxZ, existing.z);
    L.z = maxZ + 1;
    
    // Scale and Center (using logical dimensions)
    const float targetW = m_CanvasW * 0.8f;
    const float targetH = m_CanvasH * 0.8f;
    if (logicalW > 0 && logicalH > 0) {
        L.scale = std::min({ 1.0f, targetW / static_cast<float>(logicalW), targetH / static_cast<float>(logicalH) });
    }
    L.x = - (static_cast<float>(logicalW) * L.scale) * 0.5f;
    L.y = - (static_cast<float>(logicalH) * L.scale) * 0.5f;

    L.tex = GLHelpers::CreateTextureFromPixels(L.rgba.data(), texW, texH, 4);
    if (L.tex == 0) {
        std::cerr << "[Composite] Failed to create GL texture for layer: " << name << " (" << texW << "x" << texH << "). Texture may be too large for GPU.\n";
    }
    
    m_Layers.push_back(std::move(L));
    m_SelectedId = m_Layers.back().id;
    m_Dirty = true;
}

bool CompositeModule::HasLayers() const {
    return !m_Layers.empty();
}

void CompositeModule::ResizeNearest(const std::vector<uint8_t>& src, int sw, int sh, int dw, int dh, std::vector<uint8_t>& dst) {
    dst.resize(dw * dh * 4);
    for (int y = 0; y < dh; y++) {
        int sy = (y * sh) / dh;
        for (int x = 0; x < dw; x++) {
            int sx = (x * sw) / dw;
            int srcIdx = (sy * sw + sx) * 4;
            int dstIdx = (y * dw + x) * 4;
            dst[dstIdx + 0] = src[srcIdx + 0];
            dst[dstIdx + 1] = src[srcIdx + 1];
            dst[dstIdx + 2] = src[srcIdx + 2];
            dst[dstIdx + 3] = src[srcIdx + 3];
        }
    }
}

void CompositeModule::AddProjectLayerFromFile(const std::string& path) {
    StackBinaryFormat::ProjectLoadOptions options;
    options.includeThumbnail = false;
    options.includeSourceImage = true;
    options.includePipelineData = true;

    StackBinaryFormat::ProjectDocument document;
    if (!StackBinaryFormat::ReadProjectFile(path, document, options)) {
        return;
    }

    // Must be an editor project to embed as a layer
    if (document.metadata.projectKind != StackBinaryFormat::kEditorProjectKind) {
        return;
    }

    // Re-render the project to get a high-fidelity preview
    EditorModule previewEditor;
    previewEditor.Initialize();

    int srcW = 0, srcH = 0, srcC = 4;
    std::vector<uint8_t> srcPixels;
    if (!LibraryManager::DecodeImageBytes(document.sourceImageBytes, srcPixels, srcW, srcH, srcC)) {
        return;
    }

    int pW = srcW;
    int pH = srcH;
    std::vector<uint8_t> pPixels = srcPixels;

    // Apply performance limit if enabled
    if (m_LimitProjectResolution && !pPixels.empty()) {
        const int limit = 4096;
        if (pW > limit || pH > limit) {
            float s = (float)limit / (float)std::max(pW, pH);
            int newW = std::max(1, (int)(pW * s));
            int newH = std::max(1, (int)(pH * s));
            std::vector<uint8_t> resized;
            ResizeNearest(pPixels, pW, pH, newW, newH, resized);
            pPixels = std::move(resized);
            pW = newW;
            pH = newH;
        }
    }

    previewEditor.LoadSourceFromPixels(pPixels.data(), pW, pH, srcC);
    previewEditor.DeserializePipeline(document.pipelineData);
    previewEditor.GetPipeline().Execute(previewEditor.GetLayers());

    // Force GPU to finish before we try to read back the pixels
    glFinish();

    int renderedW = 0, renderedH = 0;
    auto renderedPixels = previewEditor.GetPipeline().GetOutputPixels(renderedW, renderedH);
    if (renderedPixels.empty()) {
        std::cerr << "[Composite] Failed to render project preview for: " << document.metadata.projectName << "\n";
        return;
    }

    AddProjectLayer(document.metadata.projectName, document.pipelineData.dump(), renderedPixels, renderedW, renderedH, srcW, srcH);
    
    // Store the source image for the bridge
    if (!m_Layers.empty() && m_Layers.back().id == m_SelectedId) {
        m_Layers.back().sourceImagePng = std::move(document.sourceImageBytes);
    }
}

void CompositeModule::AddImageLayerFromFile(const std::string& path) {
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!data || w <= 0 || h <= 0) {
        if (data) stbi_image_free(data);
        return;
    }

    CompositeLayer layer;
    layer.id = NewLayerId();
    const std::size_t slash = path.find_last_of("/\\");
    layer.name = slash == std::string::npos ? path : path.substr(slash + 1);
    layer.imgW = w;
    layer.imgH = h;
    layer.logicalW = w;
    layer.logicalH = h;
    layer.rgba.assign(data, data + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    stbi_image_free(data);

    int maxZ = -1;
    for (const CompositeLayer& L : m_Layers) maxZ = std::max(maxZ, L.z);
    layer.z = maxZ + 1;

    // Scale to fit (80% of canvas)
    const float targetW = m_CanvasW * 0.8f;
    const float targetH = m_CanvasH * 0.8f;
    float s = 1.0f;
    if (w > 0 && h > 0) {
        float sw = targetW / static_cast<float>(w);
        float sh = targetH / static_cast<float>(h);
        s = std::min(sw, sh);
        if (s > 1.0f) s = 1.0f; // Don't upscale past 1:1 by default
    }
    layer.scale = s;
    
    // Center it
    layer.x = - (static_cast<float>(w) * s) * 0.5f;
    layer.y = - (static_cast<float>(h) * s) * 0.5f;

    if (layer.tex != 0) glDeleteTextures(1, &layer.tex);
    layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);

    m_Layers.push_back(std::move(layer));
    m_SelectedId = m_Layers.back().id;
    m_Dirty = true;
}

void CompositeModule::RemoveSelectedLayers() {
    if (m_SelectedId.empty()) return;
    ClearLayersGpu();
    m_Layers.erase(
        std::remove_if(
            m_Layers.begin(),
            m_Layers.end(),
            [this](const CompositeLayer& L) { return L.id == m_SelectedId; }),
        m_Layers.end());
    m_SelectedId.clear();
    SyncLayerTextures();
    m_Dirty = true;
}

void CompositeModule::RasterizeCompositeRgba(std::vector<uint8_t>& outRgba, int& outW, int& outH) const {
    outRgba.clear();
    outW = 0;
    outH = 0;

    std::vector<const CompositeLayer*> visible;
    visible.reserve(m_Layers.size());
    for (const CompositeLayer& L : m_Layers) {
        if (!L.visible || L.rgba.empty() || L.imgW <= 0 || L.imgH <= 0) continue;
        visible.push_back(&L);
    }
    if (visible.empty()) return;

    std::sort(visible.begin(), visible.end(), [](const CompositeLayer* a, const CompositeLayer* b) { return a->z < b->z; });

    float minX = 1e9f;
    float minY = 1e9f;
    float maxX = -1e9f;
    float maxY = -1e9f;
    for (const CompositeLayer* L : visible) {
        const int dw = std::max(1, static_cast<int>(std::ceil(static_cast<float>(L->logicalW) * L->scale)));
        const int dh = std::max(1, static_cast<int>(std::ceil(static_cast<float>(L->logicalH) * L->scale)));
        minX = std::min(minX, L->x);
        minY = std::min(minY, L->y);
        maxX = std::max(maxX, L->x + static_cast<float>(dw));
        maxY = std::max(maxY, L->y + static_cast<float>(dh));
    }

    const int originX = static_cast<int>(std::floor(minX));
    const int originY = static_cast<int>(std::floor(minY));
    outW = std::max(1, static_cast<int>(std::ceil(maxX - static_cast<float>(originX))));
    outH = std::max(1, static_cast<int>(std::ceil(maxY - static_cast<float>(originY))));

    outRgba.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4, 0);

    for (const CompositeLayer* L : visible) {
        const int dw = std::max(1, static_cast<int>(std::ceil(static_cast<float>(L->logicalW) * L->scale)));
        const int dh = std::max(1, static_cast<int>(std::ceil(static_cast<float>(L->logicalH) * L->scale)));
        std::vector<uint8_t> scaled;
        ResizeNearest(L->rgba, L->imgW, L->imgH, dw, dh, scaled);
        const int dstX = static_cast<int>(std::floor(L->x)) - originX;
        const int dstY = static_cast<int>(std::floor(L->y)) - originY;
        BlendSourceOver(outRgba, outW, outH, dstX, dstY, scaled, dw, dh, L->opacity);
    }
}

void CompositeModule::EncodePng(const std::vector<uint8_t>& rgba, int w, int h, std::vector<uint8_t>& outPng) const {
    outPng.clear();
    if (rgba.empty() || w <= 0 || h <= 0) return;
    stbi_write_png_to_func(png_write_vec, &outPng, w, h, 4, rgba.data(), w * 4);
}

bool CompositeModule::ApplyLibraryProject(const StackBinaryFormat::ProjectDocument& document) {
    if (document.metadata.projectKind != StackBinaryFormat::kCompositeProjectKind) return false;

    ClearLayersGpu();
    m_Layers.clear();
    m_SelectedId.clear();

    const json& root = document.pipelineData;
    if (!root.is_object()) return false;

    const json& view = root.value("view", json::object());
    m_ViewZoom = std::max(0.05f, view.value("zoom", 1.0f));
    m_ViewPanX = view.value("panX", 0.0f);
    m_ViewPanY = view.value("panY", 0.0f);
    m_ShowChecker = view.value("showChecker", true);
    m_SelectedId = root.value("selectionLayerId", std::string());

    const json& layersJson = root.value("layers", json::array());
    for (const auto& item : layersJson) {
        if (!item.is_object()) continue;
        
        std::string kindStr = item.value("kind", "image");
        LayerKind kind = LayerKind::Image;
        if (kindStr == "editor-project") kind = LayerKind::EditorProject;
        
        CompositeLayer layer;
        layer.id = item.value("id", NewLayerId());
        layer.kind = kind;
        layer.name = item.value("name", std::string("Layer"));
        layer.x = item.value("x", 0.0f);
        layer.y = item.value("y", 0.0f);
        layer.scale = std::max(0.0001f, item.value("scale", 1.0f));
        layer.rotation = item.value("rotation", 0.0f);
        layer.opacity = std::clamp(item.value("opacity", 1.0f), 0.0f, 1.0f);
        layer.z = item.value("z", 0);
        layer.visible = item.value("visible", true);

        if (kind == LayerKind::EditorProject) {
            layer.embeddedProjectJson = item.value("projectJson", std::string("{}"));
            if (item.contains("sourcePng") && item["sourcePng"].is_binary()) {
                layer.sourceImagePng = item["sourcePng"].get_binary();
            }
        }

        if (item.contains("imagePng") && item["imagePng"].is_binary()) {
            const auto& bin = item["imagePng"].get_binary();
            int w = 0, h = 0, ch = 0;
            unsigned char* pix = stbi_load_from_memory(bin.data(), static_cast<int>(bin.size()), &w, &h, &ch, 4);
            if (pix && w > 0 && h > 0) {
                layer.imgW = w;
                layer.imgH = h;
                layer.logicalW = item.value("logicalW", w);
                layer.logicalH = item.value("logicalH", h);
                layer.rgba.assign(pix, pix + static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
                stbi_image_free(pix);
                layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
            } else {
                if (pix) stbi_image_free(pix);
            }
        }
        
        m_Layers.push_back(std::move(layer));
    }

    m_ProjectName = document.metadata.projectName;
    m_Dirty = false;
    return true;
}

bool CompositeModule::BuildProjectDocumentForSave(const std::string& displayName, StackBinaryFormat::ProjectDocument& out) const {
    std::vector<uint8_t> rgba;
    int rw = 0, rh = 0;
    RasterizeCompositeRgba(rgba, rw, rh);
    if (rgba.empty() || rw <= 0 || rh <= 0) return false;

    std::vector<uint8_t> fullPng;
    EncodePng(rgba, rw, rh, fullPng);
    if (fullPng.empty()) return false;

    std::vector<uint8_t> thumbRgba;
    int tw = rw;
    int th = rh;
    const int maxEdge = 320;
    if (rw > maxEdge || rh > maxEdge) {
        const float s = static_cast<float>(maxEdge) / static_cast<float>(std::max(rw, rh));
        tw = std::max(1, static_cast<int>(std::floor(static_cast<float>(rw) * s)));
        th = std::max(1, static_cast<int>(std::floor(static_cast<float>(rh) * s)));
        ResizeNearest(rgba, rw, rh, tw, th, thumbRgba);
    } else {
        thumbRgba = rgba;
    }
    std::vector<uint8_t> thumbPng;
    EncodePng(thumbRgba, tw, th, thumbPng);

    json layersJson = json::array();
    for (const CompositeLayer& L : m_Layers) {
        if (L.rgba.empty()) continue;
        std::vector<uint8_t> png;
        EncodePng(L.rgba, L.imgW, L.imgH, png);
        if (png.empty()) continue;

        json layerObj = json::object();
        layerObj["id"] = L.id;
        layerObj["kind"] = (L.kind == LayerKind::EditorProject) ? "editor-project" : "image";
        layerObj["name"] = L.name;
        layerObj["x"] = L.x;
        layerObj["y"] = L.y;
        layerObj["z"] = L.z;
        layerObj["scale"] = L.scale;
        layerObj["rotation"] = L.rotation;
        layerObj["opacity"] = L.opacity;
        layerObj["visible"] = L.visible;
        layerObj["logicalW"] = L.logicalW;
        layerObj["logicalH"] = L.logicalH;
        layerObj["imagePng"] = json::binary(std::move(png));
        
        if (L.kind == LayerKind::EditorProject) {
            layerObj["projectJson"] = L.embeddedProjectJson;
            layerObj["sourcePng"] = json::binary(L.sourceImagePng);
        }

        layersJson.push_back(std::move(layerObj));
    }

    json root = json::object();
    root["compositeVersion"] = kCompositeFormatVersion;
    root["layers"] = std::move(layersJson);
    json viewObj = json::object();
    viewObj["zoom"] = m_ViewZoom;
    viewObj["panX"] = m_ViewPanX;
    viewObj["panY"] = m_ViewPanY;
    viewObj["showChecker"] = m_ShowChecker;
    root["view"] = std::move(viewObj);
    root["selectionLayerId"] = m_SelectedId;

    out.metadata.projectKind = StackBinaryFormat::kCompositeProjectKind;
    out.metadata.projectName = displayName.empty() ? m_ProjectName : displayName;
    out.metadata.timestamp.clear();
    out.metadata.sourceWidth = rw;
    out.metadata.sourceHeight = rh;
    out.thumbnailBytes = std::move(thumbPng);
    out.sourceImageBytes = std::move(fullPng);
    out.pipelineData = std::move(root);
    return true;
}

void CompositeModule::RenderUI() {
    if (!m_Initialized) Initialize();

    // Use a horizontal layout for Sidebar and Canvas
    const float sidebarWidth = 300.0f;
    
    ImGui::BeginGroup();
    {
        // --- Sidebar ---
        ImGui::BeginChild("CompositeSidebar", ImVec2(sidebarWidth, 0), true);
        
        if (ImGui::CollapsingHeader("Project", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("New Project", ImVec2(-1, 0))) {
                NewProject();
            }
            if (ImGui::Button("Add Image Layer", ImVec2(-1, 0))) {
                const std::string path = FileDialogs::OpenImageFileDialog("Add image to composite");
                if (!path.empty()) AddImageLayerFromFile(path);
            }
            if (ImGui::Button("Add Project Layer", ImVec2(-1, 0))) {
                const std::string path = FileDialogs::OpenProjectFileDialog("Add project to composite");
                if (!path.empty()) AddProjectLayerFromFile(path);
            }
            if (ImGui::Button("Add From Library", ImVec2(-1, 0))) {
                m_ShowLibraryPicker = true;
            }
            
            ImGui::BeginDisabled(!HasLayers());
            if (ImGui::Button("Save To Library", ImVec2(-1, 0))) {
                ImGui::OpenPopup("SaveCompositeToLibrary");
            }
            ImGui::EndDisabled();
            
            ImGui::Text("Project: %s", m_ProjectName.c_str());
        }

        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::SliderFloat("Zoom", &m_ViewZoom, 0.05f, 16.0f, "%.2f");
            if (ImGui::Button("Reset View")) {
                m_ViewPanX = 0.0f;
                m_ViewPanY = 0.0f;
                m_ViewZoom = 1.0f;
            }
            ImGui::Checkbox("Show Checker", &m_ShowChecker);
            ImGui::Checkbox("Limit Project resolution to 4k", &m_LimitProjectResolution);
            
            ImGui::Separator();
            ImGui::Checkbox("Enable Snapping", &m_SnapEnabled);
            if (m_SnapEnabled) {
                ImGui::DragFloat("Grid Size", &m_GridSize, 1.0f, 1.0f, 500.0f, "%.0f px");
                ImGui::DragFloat("Rotate Step", &m_RotateSnapStep, 1.0f, 1.0f, 180.0f, "%.0f deg");
                ImGui::DragFloat("Scale Step", &m_ScaleSnapStep, 0.01f, 0.01f, 1.0f, "%.2f");
            }
        }

        if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginChild("LayerListInternal", ImVec2(0, 300), true);
            
            // We want the list to show top-to-bottom (z descending)
            std::vector<CompositeLayer*> sorted;
            sorted.reserve(m_Layers.size());
            for (CompositeLayer& L : m_Layers) sorted.push_back(&L);
            std::sort(sorted.begin(), sorted.end(), [](const CompositeLayer* a, const CompositeLayer* b) { 
                return a->z > b->z; 
            });

            for (int n = 0; n < (int)sorted.size(); n++) {
                CompositeLayer* L = sorted[n];
                ImGui::PushID(L->id.c_str());
                
                bool isSelected = (L->id == m_SelectedId);
                std::string label = L->name;
                if (L->kind == LayerKind::EditorProject) label += " [Project]";
                
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_AllowOverlap)) {
                    m_SelectedId = L->id;
                }

                // Drag and Drop Source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                    ImGui::SetDragDropPayload("COMPOSITE_LAYER_MOVE", &n, sizeof(int));
                    ImGui::Text("Moving %s", L->name.c_str());
                    ImGui::EndDragDropSource();
                }

                // Drag and Drop Target
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("COMPOSITE_LAYER_MOVE")) {
                        int sourceIdx = *(const int*)payload->Data;
                        int targetIdx = n;

                        if (sourceIdx != targetIdx) {
                            // Reorder logic:
                            // We are working with the 'sorted' view (z descending).
                            // If we move source to target, we need to update 'z' values.
                            // The easiest way: Swap their Z values or shift them.
                            
                            // Let's just collect ALL layers in current list order, 
                            // modify the list, then re-assign Z values from bottom up.
                            CompositeLayer* movingItem = sorted[sourceIdx];
                            sorted.erase(sorted.begin() + sourceIdx);
                            sorted.insert(sorted.begin() + targetIdx, movingItem);

                            // Re-assign Z values: bottom item gets 0, top item gets N-1
                            for (int i = 0; i < (int)sorted.size(); i++) {
                                sorted[i]->z = (int)sorted.size() - 1 - i;
                            }
                            m_Dirty = true;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20.0f);
                if (ImGui::Checkbox("##vis", &L->visible)) {
                    m_Dirty = true;
                }
                
                ImGui::PopID();
            }
            ImGui::EndChild();
            
            ImGui::BeginDisabled(m_SelectedId.empty());
            if (ImGui::Button("Remove Selected", ImVec2(-1, 0))) {
                RemoveSelectedLayers();
            }
            ImGui::EndDisabled();
        }

        // --- Selected Layer Properties ---
        CompositeLayer* sel = nullptr;
        for (CompositeLayer& L : m_Layers) {
            if (L.id == m_SelectedId) {
                sel = &L;
                break;
            }
        }

        if (sel) {
            if (ImGui::CollapsingHeader("Selected Layer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Name: %s", sel->name.c_str());
                ImGui::Text("Size: %d x %d", sel->logicalW, sel->logicalH);
                
                if (ImGui::DragFloat2("Position", &sel->x, 1.0f)) m_Dirty = true;
                if (ImGui::SliderFloat("Opacity", &sel->opacity, 0.0f, 1.0f)) m_Dirty = true;
                
                ImGui::BeginGroup();
                if (ImGui::Button("Reset Scale")) { sel->scale = 1.0f; m_Dirty = true; }
                ImGui::SameLine();
                if (ImGui::Button("Reset Rotation")) { sel->rotation = 0.0f; m_Dirty = true; }
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::TextDisabled("Use on-canvas handles to");
                ImGui::TextDisabled("adjust Scale and Rotation.");
            }
        }

        ImGui::EndChild();
    }
    ImGui::EndGroup();

    ImGui::SameLine();

    // --- Canvas Area ---
    ImGui::BeginGroup();
    {
        ImGui::BeginChild("CompositeCanvasArea", ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove);
        
        const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
        const ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        m_CanvasW = canvasSize.x;
        m_CanvasH = canvasSize.y;
        const ImVec2 canvasMax(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 1. Draw Background
        if (m_ShowChecker) {
            const float cs = 16.0f * m_ViewZoom;
            // Draw checker only within canvas bounds
            for (float y = canvasPos.y; y < canvasMax.y; y += cs) {
                for (float x = canvasPos.x; x < canvasMax.x; x += cs) {
                    const int cx = static_cast<int>(std::floor((x - canvasPos.x) / cs));
                    const int cy = static_cast<int>(std::floor((y - canvasPos.y) / cs));
                    const ImU32 col = ((cx + cy) & 1) ? IM_COL32(40, 42, 48, 255) : IM_COL32(28, 30, 34, 255);
                    drawList->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cs, canvasMax.x), std::min(y + cs, canvasMax.y)), col);
                }
            }
        } else {
            drawList->AddRectFilled(canvasPos, canvasMax, IM_COL32(18, 19, 22, 255));
        }

        SyncLayerTextures();

        // 2. Resolve Layers to draw (sorted by Z)
        std::vector<CompositeLayer*> drawOrder;
        drawOrder.reserve(m_Layers.size());
        for (CompositeLayer& L : m_Layers) drawOrder.push_back(&L);
        std::sort(drawOrder.begin(), drawOrder.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
            return a->z < b->z;
        });

        // 3. Draw Layers
        const ImVec2 center(canvasPos.x + canvasSize.x * 0.5f, canvasPos.y + canvasSize.y * 0.5f);
        for (CompositeLayer* L : drawOrder) {
            if (!L->visible || L->tex == 0) continue;
            
            const float dw = static_cast<float>(L->logicalW) * L->scale * m_ViewZoom;
            const float dh = static_cast<float>(L->logicalH) * L->scale * m_ViewZoom;
            
            const float rad = L->rotation;
            const float cosR = std::cos(rad);
            const float sinR = std::sin(rad);
            
            // Pivot is at layer center? Web guide says x,y is top-left of unrotated box.
            // Let's stick to top-left pivot for now as in current code, or move to center pivot.
            // Current code: p0 = center + pan + x,y. p1 = p0 + dw,dh.
            // Usually rotation is around some pivot. Let's do center pivot.
            const float worldX = center.x + m_ViewPanX + L->x * m_ViewZoom;
            const float worldY = center.y + m_ViewPanY + L->y * m_ViewZoom;
            
            // Corners relative to top-left (0,0)
            ImVec2 q[4] = {
                { 0.0f, 0.0f },
                { dw, 0.0f },
                { dw, dh },
                { 0.0f, dh }
            };
            
            // Pivot at center of the layer
            const float pivotX = dw * 0.5f;
            const float pivotY = dh * 0.5f;
            
            ImVec2 screenPoints[4];
            for (int i = 0; i < 4; ++i) {
                // translate to pivot
                float tx = q[i].x - pivotX;
                float ty = q[i].y - pivotY;
                // rotate
                float rx = tx * cosR - ty * sinR;
                float ry = tx * sinR + ty * cosR;
                // translate back + to world position
                screenPoints[i].x = worldX + pivotX + rx;
                screenPoints[i].y = worldY + pivotY + ry;
            }
            
            // OpenGL flipped textures: (0,1) is top-left, (1,0) is bottom-right.
            // In AddImageQuad: uv0 (top-left), uv1 (top-right), uv2 (bottom-right), uv3 (bottom-left)
            drawList->AddImageQuad(
                (ImTextureID)(intptr_t)L->tex,
                screenPoints[0], screenPoints[1], screenPoints[2], screenPoints[3],
                ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0),
                IM_COL32(255, 255, 255, static_cast<int>(std::clamp(L->opacity, 0.0f, 1.0f) * 255.0f))
            );
            
            if (L->id == m_SelectedId) {
                drawList->AddPolyline(screenPoints, 4, IM_COL32(255, 170, 0, 255), true, 2.0f);
                
                // Draw Handles
                const ImU32 handleCol = IM_COL32(255, 255, 255, 255);
                const ImU32 handleOutlineCol = IM_COL32(0, 0, 0, 255);
                const float hs = 5.0f; // handle half-size
                
                // Corners
                for (int i = 0; i < 4; i++) {
                    drawList->AddCircleFilled(screenPoints[i], hs, handleCol);
                    drawList->AddCircle(screenPoints[i], hs, handleOutlineCol);
                }
                
                // Rotation handle (line from top center)
                const ImVec2 topCenter = { (screenPoints[0].x + screenPoints[1].x) * 0.5f, (screenPoints[0].y + screenPoints[1].y) * 0.5f };
                const float rotHandleDist = 25.0f;
                // Direction of top (rotated)
                const float upX = -sinR;
                const float upY = -cosR;
                const ImVec2 rotHandlePos = { topCenter.x + upX * rotHandleDist, topCenter.y + upY * rotHandleDist };
                
                drawList->AddLine(topCenter, rotHandlePos, IM_COL32(255, 170, 0, 255), 2.0f);
                drawList->AddCircleFilled(rotHandlePos, hs + 1.0f, IM_COL32(240, 190, 110, 255));
                drawList->AddCircle(rotHandlePos, hs + 1.0f, handleOutlineCol);
            }
        }

        // Handle interaction logic
        ImGui::SetCursorScreenPos(canvasPos);
        ImGui::InvisibleButton("composite_stage_interact", canvasSize);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const ImVec2 mousePos = ImGui::GetMousePos();

        CompositeLayer* selLayer = nullptr;
        for (CompositeLayer& L : m_Layers) if (L.id == m_SelectedId) { selLayer = &L; break; }

        if (ImGui::IsMouseClicked(0) && hovered) {
            m_ActiveHandle = HandleType::None;
            if (selLayer) {
                // Check handles first
                const float dw = static_cast<float>(selLayer->imgW) * selLayer->scale * m_ViewZoom;
                const float dh = static_cast<float>(selLayer->imgH) * selLayer->scale * m_ViewZoom;
                const float worldX = center.x + m_ViewPanX + selLayer->x * m_ViewZoom;
                const float worldY = center.y + m_ViewPanY + selLayer->y * m_ViewZoom;
                const float rad = selLayer->rotation;
                const float cosR = std::cos(rad);
                const float sinR = std::sin(rad);
                const float pivotX = dw * 0.5f;
                const float pivotY = dh * 0.5f;
                const float cx = worldX + pivotX;
                const float cy = worldY + pivotY;

                auto get_p = [&](float x, float y) {
                    float tx = x - pivotX; float ty = y - pivotY;
                    return ImVec2(cx + (tx * cosR - ty * sinR), cy + (tx * sinR + ty * cosR));
                };

                const ImVec2 p[4] = { get_p(0,0), get_p(dw,0), get_p(dw,dh), get_p(0,dh) };
                const float threshold = 12.0f;

                // Rotation handle
                const ImVec2 topCenter = { (p[0].x + p[1].x) * 0.5f, (p[0].y + p[1].y) * 0.5f };
                const ImVec2 rotHandlePos = { topCenter.x - sinR * 25.0f, topCenter.y - cosR * 25.0f };
                
                if (std::hypot(mousePos.x - rotHandlePos.x, mousePos.y - rotHandlePos.y) < threshold) {
                    m_ActiveHandle = HandleType::Rotate;
                } else if (std::hypot(mousePos.x - p[1].x, mousePos.y - p[1].y) < threshold) {
                    m_ActiveHandle = HandleType::ScaleTR;
                } else if (std::hypot(mousePos.x - p[0].x, mousePos.y - p[0].y) < threshold) {
                    m_ActiveHandle = HandleType::ScaleTL;
                } else if (std::hypot(mousePos.x - p[2].x, mousePos.y - p[2].y) < threshold) {
                    m_ActiveHandle = HandleType::ScaleBR;
                } else if (std::hypot(mousePos.x - p[3].x, mousePos.y - p[3].y) < threshold) {
                    m_ActiveHandle = HandleType::ScaleBL;
                }

                if (m_ActiveHandle != HandleType::None) {
                    m_StartScale = selLayer->scale;
                    m_StartRotation = selLayer->rotation;
                    m_StartX = selLayer->x;
                    m_StartY = selLayer->y;
                    m_StartMouseAngle = std::atan2(mousePos.y - cy, mousePos.x - cx) - selLayer->rotation;
                    m_StartMouseDist = std::hypot(mousePos.x - cx, mousePos.y - cy);
                } else {
                    // Check if clicking the body of the layer to move it
                    float dx = mousePos.x - cx; float dy = mousePos.y - cy;
                    float ux = dx * cosR + dy * sinR; float uy = -dx * sinR + dy * cosR;
                    if (ux >= -pivotX && ux <= pivotX && uy >= -pivotY && uy <= pivotY) {
                        m_ActiveHandle = HandleType::Move;
                        m_StartX = selLayer->x;
                        m_StartY = selLayer->y;
                    }
                }
            }
            
            // If still None, maybe clicking another layer
            if (m_ActiveHandle == HandleType::None) {
                // ... (existing selection logic)
                bool found = false;
                std::vector<CompositeLayer*> drawOrder;
                for (CompositeLayer& L : m_Layers) drawOrder.push_back(&L);
                std::sort(drawOrder.begin(), drawOrder.end(), [](const CompositeLayer* a, const CompositeLayer* b) { return a->z < b->z; });
                for (auto it = drawOrder.rbegin(); it != drawOrder.rend(); ++it) {
                    CompositeLayer* L = *it; if (!L->visible) continue;
                    const float dw = static_cast<float>(L->imgW) * L->scale * m_ViewZoom;
                    const float dh = static_cast<float>(L->imgH) * L->scale * m_ViewZoom;
                    const float worldX = center.x + m_ViewPanX + L->x * m_ViewZoom;
                    const float worldY = center.y + m_ViewPanY + L->y * m_ViewZoom;
                    const float rad = L->rotation;
                    const float cx = worldX + dw * 0.5f; const float cy = worldY + dh * 0.5f;
                    float dx = mousePos.x - cx; float dy = mousePos.y - cy;
                    float ux = dx * std::cos(rad) + dy * std::sin(rad); 
                    float uy = -dx * std::sin(rad) + dy * std::cos(rad);
                    if (ux >= -dw*0.5f && ux <= dw*0.5f && uy >= -dh*0.5f && uy <= dh*0.5f) {
                        m_SelectedId = L->id; found = true; m_ActiveHandle = HandleType::Move; 
                        m_StartX = L->x; m_StartY = L->y; // SET START POS HERE TOO
                        break;
                    }
                }
                if (!found) m_SelectedId.clear();
            }
        }

        // Re-fetch selected layer pointer in case it changed during the click logic above
        selLayer = nullptr;
        for (CompositeLayer& L : m_Layers) if (L.id == m_SelectedId) { selLayer = &L; break; }

        if (active && m_ActiveHandle != HandleType::None && selLayer) {
            const float dw = static_cast<float>(selLayer->imgW) * selLayer->scale * m_ViewZoom;
            const float dh = static_cast<float>(selLayer->imgH) * selLayer->scale * m_ViewZoom;
            const float cx = center.x + m_ViewPanX + (selLayer->x + static_cast<float>(selLayer->imgW) * selLayer->scale * 0.5f) * m_ViewZoom;
            const float cy = center.y + m_ViewPanY + (selLayer->y + static_cast<float>(selLayer->imgH) * selLayer->scale * 0.5f) * m_ViewZoom;

            if (m_ActiveHandle == HandleType::Move) {
                ImVec2 delta = ImGui::GetMouseDragDelta(0);
                float targetX = m_StartX + delta.x / std::max(0.001f, m_ViewZoom);
                float targetY = m_StartY + delta.y / std::max(0.001f, m_ViewZoom);
                
                if (m_SnapEnabled) {
                    targetX = std::round(targetX / m_GridSize) * m_GridSize;
                    targetY = std::round(targetY / m_GridSize) * m_GridSize;
                }
                
                selLayer->x = targetX;
                selLayer->y = targetY;
            } else if (m_ActiveHandle == HandleType::Rotate) {
                float targetRot = std::atan2(mousePos.y - cy, mousePos.x - cx) - m_StartMouseAngle;
                if (m_SnapEnabled) {
                    float deg = targetRot * 180.0f / 3.14159265f;
                    deg = std::round(deg / m_RotateSnapStep) * m_RotateSnapStep;
                    targetRot = deg * 3.14159265f / 180.0f;
                }
                selLayer->rotation = targetRot;
            } else {
                // Scaling
                float currentDist = std::hypot(mousePos.x - cx, mousePos.y - cy);
                float ratio = currentDist / std::max(1.0f, m_StartMouseDist);
                float targetScale = m_StartScale * ratio;
                if (m_SnapEnabled) {
                    targetScale = std::round(targetScale / m_ScaleSnapStep) * m_ScaleSnapStep;
                }
                selLayer->scale = std::clamp(targetScale, 0.01f, 32.0f);
                
                // Adjust x,y to keep it centered
                float oldW = static_cast<float>(selLayer->imgW) * m_StartScale;
                float oldH = static_cast<float>(selLayer->imgH) * m_StartScale;
                float newW = static_cast<float>(selLayer->imgW) * selLayer->scale;
                float newH = static_cast<float>(selLayer->imgH) * selLayer->scale;
                selLayer->x = m_StartX + (oldW - newW) * 0.5f;
                selLayer->y = m_StartY + (oldH - newH) * 0.5f;
            }
            m_Dirty = true;
        }

        if (ImGui::IsMouseReleased(0)) {
            m_ActiveHandle = HandleType::None;
        }

        if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
            m_ViewPanX += ImGui::GetIO().MouseDelta.x;
            m_ViewPanY += ImGui::GetIO().MouseDelta.y;
        }
        
        if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
            m_ViewZoom = std::clamp(m_ViewZoom * (1.0f + ImGui::GetIO().MouseWheel * 0.1f), 0.05f, 32.0f);
        }

        if (hovered && ImGui::IsKeyPressed(ImGuiKey_Delete) && !m_SelectedId.empty()) {
            RemoveSelectedLayers();
        }

        ImGui::EndChild();
    }
    ImGui::EndGroup();

    RenderLibraryPicker();

    // --- Popups ---
    if (ImGui::BeginPopupModal("SaveCompositeToLibrary", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char nameBuf[256] = "Composite Project";
        if (ImGui::IsWindowAppearing()) {
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_ProjectName.c_str());
        }
        ImGui::InputText("Project name", nameBuf, sizeof(nameBuf));
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            m_ProjectName = nameBuf;
            ImGui::CloseCurrentPopup();
            LibraryManager::Get().RequestSaveCompositeProject(nameBuf, this, m_ProjectFileName);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
    }
}

void CompositeModule::RenderLibraryPicker() {
    if (!m_ShowLibraryPicker) return;

    ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Select Library Project", &m_ShowLibraryPicker, ImGuiWindowFlags_NoCollapse)) {
        
        const auto& projects = LibraryManager::Get().GetProjects();
        if (projects.empty()) {
            ImGui::TextDisabled("No projects found in the Library.");
        } else {
            ImGui::Text("Click a project to add it as a layer:");
            ImGui::Separator();
            
            if (ImGui::BeginChild("PickerList")) {
                const float thumbSize = 80.0f;
                const float padding = 10.0f;
                
                for (const auto& project : projects) {
                    if (project->projectKind != StackBinaryFormat::kEditorProjectKind) continue;

                    ImGui::PushID(project->fileName.c_str());
                    
                    ImGui::BeginGroup();
                    
                    // Thumbnail
                    if (project->thumbnailTex != 0) {
                        float aspect = (project->sourceWidth > 0 && project->sourceHeight > 0) ? (float)project->sourceWidth / (float)project->sourceHeight : 1.0f;
                        float tw = thumbSize;
                        float th = thumbSize;
                        if (aspect > 1.0f) th = thumbSize / aspect;
                        else tw = thumbSize * aspect;
                        
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (thumbSize - th) * 0.5f);
                        ImGui::Image((ImTextureID)(intptr_t)project->thumbnailTex, ImVec2(tw, th), ImVec2(0, 1), ImVec2(1, 0));
                    } else {
                        ImGui::Dummy(ImVec2(thumbSize, thumbSize));
                        ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), IM_COL32_WHITE);
                    }
                    
                    ImGui::SameLine(thumbSize + padding);
                    
                    // Info
                    ImGui::BeginGroup();
                    ImGui::Text("%s", project->projectName.c_str());
                    ImGui::TextDisabled("%s", project->timestamp.c_str());
                    if (ImGui::Button("Add to Canvas")) {
                        std::filesystem::path fullPath = LibraryManager::Get().GetLibraryPath() / project->fileName;
                        AddProjectLayerFromFile(fullPath.string());
                        m_ShowLibraryPicker = false;
                    }
                    ImGui::EndGroup();
                    
                    ImGui::EndGroup();
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }
}
