#include "Composite/Internal/CompositeModuleInternal.h"

#include "Composite/CompositeModule.h"
#include "Renderer/GLHelpers.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#include <algorithm>
#include <cmath>

using json = StackBinaryFormat::json;

bool CompositeModule::BuildExportRaster(
    std::vector<uint8_t>& outRgba,
    int& outW,
    int& outH,
    const bool useExportSettings) const {

    outRgba.clear();
    outW = 0;
    outH = 0;

    std::vector<const CompositeLayer*> layers;
    layers.reserve(m_Layers.size());
    for (const CompositeLayer& layer : m_Layers) {
        if (!layer.visible || layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
            continue;
        }
        layers.push_back(&layer);
    }

    if (layers.empty()) {
        return false;
    }

    std::sort(layers.begin(), layers.end(), [](const CompositeLayer* a, const CompositeLayer* b) {
        return a->z < b->z;
    });

    FloatRect worldBounds;
    if (useExportSettings && m_ExportSettings.boundsMode == CompositeExportBoundsMode::Custom) {
        worldBounds = {
            m_ExportSettings.customX,
            m_ExportSettings.customY,
            m_ExportSettings.customWidth,
            m_ExportSettings.customHeight
        };
        if (!IsRectValid(worldBounds)) {
            return false;
        }

        outW = std::max(1, m_ExportSettings.outputWidth);
        outH = std::max(
            1,
            static_cast<int>(std::round(static_cast<float>(outW) / std::max(0.0001f, RectAspectRatio(worldBounds)))));
    } else {
        if (!ComputeAutoBounds(m_Layers, layers, worldBounds)) {
            return false;
        }

        if (useExportSettings) {
            outW = std::max(1, m_ExportSettings.outputWidth);
            outH = std::max(
                1,
                static_cast<int>(std::round(static_cast<float>(outW) / std::max(0.0001f, RectAspectRatio(worldBounds)))));
        } else {
            outW = std::max(1, static_cast<int>(std::ceil(worldBounds.width)));
            outH = std::max(1, static_cast<int>(std::ceil(worldBounds.height)));
        }
    }

    const CompositeExportBackgroundMode backgroundMode = useExportSettings
        ? m_ExportSettings.backgroundMode
        : CompositeExportBackgroundMode::Transparent;
    const std::array<float, 4> backgroundColor = useExportSettings
        ? m_ExportSettings.backgroundColor
        : std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f };

    RasterizeLayersToTopLeftRgba(
        m_Layers,
        layers,
        worldBounds,
        outW,
        outH,
        backgroundMode,
        backgroundColor,
        outRgba);

    return !outRgba.empty();
}

bool CompositeModule::ExportCurrentPng(const std::string& path) const {
    if (path.empty()) {
        return false;
    }

    std::vector<uint8_t> rgba;
    int width = 0;
    int height = 0;
    if (!BuildExportRaster(rgba, width, height, true) || rgba.empty()) {
        return false;
    }

    return stbi_write_png(path.c_str(), width, height, 4, rgba.data(), width * 4) != 0;
}

bool CompositeModule::ApplyLibraryProject(const StackBinaryFormat::ProjectDocument& document) {
    if (document.metadata.projectKind != StackBinaryFormat::kCompositeProjectKind) {
        return false;
    }

    ClearLayersGpu();
    ClearStagePreviewGpu();
    m_Layers.clear();
    m_SelectedId.clear();

    const json& root = document.pipelineData;
    if (!root.is_object()) {
        return false;
    }

    const json& view = root.value("view", json::object());
    m_ViewZoom = std::max(0.05f, view.value("zoom", 1.0f));
    m_ViewPanX = view.value("panX", 0.0f);
    m_ViewPanY = view.value("panY", 0.0f);
    m_ShowChecker = view.value("showChecker", true);
    m_LimitProjectResolution = view.value("limitProjectResolution", true);
    m_SnapEnabled = view.value("snapEnabled", false);
    m_SnapToObjects = view.value("snapToObjects", true);
    m_SnapToCenters = view.value("snapToCenters", true);
    m_SnapToCanvasCenter = view.value("snapToCanvasCenter", true);
    m_SnapToExportBounds = view.value("snapToExportBounds", false);
    m_SnapToSpacing = view.value("snapToSpacing", true);
    m_LastNonZeroGridSize = 24.0f;
    m_LastNonZeroRotateSnapStep = 15.0f;
    m_LastNonZeroScaleSnapStep = 0.1f;
    m_GridSize = std::max(0.0f, view.value("gridSize", 24.0f));
    m_RotateSnapStep = std::clamp(view.value("rotateSnapStep", 15.0f), 0.0f, 180.0f);
    m_ScaleSnapStep = std::clamp(view.value("scaleSnapStep", 0.1f), 0.0f, 1.0f);
    RememberSnapStepDefaults();
    m_SelectedId = root.value("selectionLayerId", std::string());

    const json& exportJson = root.value("export", json::object());
    m_ExportSettings.boundsMode = ExportBoundsModeFromToken(exportJson.value("boundsMode", std::string("auto")));
    m_ExportSettings.backgroundMode = ExportBackgroundModeFromToken(exportJson.value("backgroundMode", std::string("transparent")));
    m_ExportSettings.customX = exportJson.value("x", m_ExportSettings.customX);
    m_ExportSettings.customY = exportJson.value("y", m_ExportSettings.customY);
    m_ExportSettings.customWidth = exportJson.value("width", m_ExportSettings.customWidth);
    m_ExportSettings.customHeight = exportJson.value("height", m_ExportSettings.customHeight);
    if (exportJson.contains("aspectPreset")) {
        m_ExportSettings.aspectPreset = ExportAspectPresetFromToken(exportJson.value("aspectPreset", std::string("1:1")));
    } else {
        m_ExportSettings.aspectPreset = CompositeExportAspectPreset::Custom;
    }
    m_ExportSettings.customAspectRatio = exportJson.value("customAspectRatio", m_ExportSettings.customAspectRatio);
    m_ExportSettings.outputWidth = std::max(1, exportJson.value("outputWidth", m_ExportSettings.outputWidth));
    m_ExportSettings.outputHeight = std::max(1, exportJson.value("outputHeight", m_ExportSettings.outputHeight));
    if (!std::isfinite(m_ExportSettings.customAspectRatio) || m_ExportSettings.customAspectRatio <= 0.0001f) {
        m_ExportSettings.customAspectRatio =
            static_cast<float>(m_ExportSettings.outputWidth) / static_cast<float>(std::max(1, m_ExportSettings.outputHeight));
    }
    if (exportJson.contains("backgroundColor") && exportJson["backgroundColor"].is_array() && exportJson["backgroundColor"].size() >= 3) {
        for (int channel = 0; channel < 3; ++channel) {
            m_ExportSettings.backgroundColor[channel] = Clamp01(exportJson["backgroundColor"][channel].get<float>());
        }
        if (exportJson["backgroundColor"].size() >= 4) {
            m_ExportSettings.backgroundColor[3] = Clamp01(exportJson["backgroundColor"][3].get<float>());
        }
    }

    const json& workspaceJson = root.value("workspace", json::object());
    const json& panelsJson = workspaceJson.value("panels", json::object());
    m_ShowLayersWindow = panelsJson.value("layers", true);
    m_ShowSelectedWindow = panelsJson.value("selected", true);
    m_ShowViewWindow = panelsJson.value("view", true);
    m_ShowExportWindow = panelsJson.value("export", true);
    m_WorkspaceLayoutIni = workspaceJson.value("layoutIni", std::string());
    m_PendingWorkspaceLayoutLoad = !m_WorkspaceLayoutIni.empty();
    m_PendingWorkspaceLayoutReset = m_WorkspaceLayoutIni.empty();
    m_SuspendWorkspaceLayoutDirtyTracking = true;
    m_RightMousePressedOnCanvas = false;
    m_ActiveExportHandle = ExportHandleType::None;
    m_ExportPanelActive = false;

    const json& layersJson = root.value("layers", json::array());
    for (const auto& item : layersJson) {
        if (!item.is_object()) {
            continue;
        }

        CompositeLayer layer;
        layer.id = item.value("id", NewLayerId());
        layer.name = item.value("name", std::string("Layer"));
        layer.kind = LayerKindFromToken(item.value("kind", std::string("image")));
        layer.x = item.value("x", 0.0f);
        layer.y = item.value("y", 0.0f);
        const float legacyScale = std::max(0.0001f, item.value("scale", 1.0f));
        layer.scaleX = std::max(0.0001f, item.value("scaleX", legacyScale));
        layer.scaleY = std::max(0.0001f, item.value("scaleY", legacyScale));
        layer.preserveAspectRatio = item.value("preserveAspectRatio", layer.kind == LayerKind::Text);
        layer.rotation = item.value("rotation", 0.0f);
        layer.opacity = std::clamp(item.value("opacity", 1.0f), 0.0f, 1.0f);
        layer.visible = item.value("visible", true);
        layer.locked = item.value("locked", false);
        layer.flipX = item.value("flipX", false);
        layer.flipY = item.value("flipY", false);
        layer.blendMode = BlendModeFromToken(item.value("blendMode", std::string("normal")));
        layer.z = item.value("z", 0);
        layer.parentId = item.value("parentId", std::string());
        layer.logicalW = item.value("logicalW", 0);
        layer.logicalH = item.value("logicalH", 0);
        if (item.contains("fillColor") && item["fillColor"].is_array() && item["fillColor"].size() >= 3) {
            for (int channel = 0; channel < 3; ++channel) {
                layer.fillColor[channel] = Clamp01(item["fillColor"][channel].get<float>());
            }
            if (item["fillColor"].size() >= 4) {
                layer.fillColor[3] = Clamp01(item["fillColor"][3].get<float>());
            }
        }
        layer.textContent = item.value("textContent", std::string("Text"));
        layer.textFontSize = std::max(1.0f, item.value("textFontSize", 72.0f));
        layer.embeddedProjectJson = item.value("projectJson", json::array().dump());
        layer.linkedProjectFileName = item.value("linkedProjectFileName", std::string());
        layer.linkedProjectName = item.value("linkedProjectName", std::string());
        layer.generatedFromImage = item.value("generatedFromImage", false);

        if (item.contains("sourcePng") && item["sourcePng"].is_binary()) {
            layer.originalSourcePng = item["sourcePng"].get_binary();
        }

        if (item.contains("imagePng") && item["imagePng"].is_binary()) {
            const auto& binary = item["imagePng"].get_binary();
            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load_thread(1);
            unsigned char* pixels = stbi_load_from_memory(binary.data(), static_cast<int>(binary.size()), &width, &height, &channels, 4);
            if (pixels && width > 0 && height > 0) {
                layer.imgW = width;
                layer.imgH = height;
                if (layer.logicalW <= 0) layer.logicalW = width;
                if (layer.logicalH <= 0) layer.logicalH = height;
                layer.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
                stbi_image_free(pixels);
                layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);

                if (layer.originalSourcePng.empty()) {
                    BuildTopLeftPngFromBottomLeftRgba(layer.rgba, layer.imgW, layer.imgH, layer.originalSourcePng);
                }
            } else if (pixels) {
                stbi_image_free(pixels);
            }
        }

        if ((layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) && IsGeneratedLayer(layer)) {
            RegenerateGeneratedLayerTexture(layer);
        }

        m_Layers.push_back(std::move(layer));
    }

    if (m_ExportSettings.aspectPreset == CompositeExportAspectPreset::Custom) {
        UpdateCustomExportAspectFromBounds();
    }
    SyncExportResolutionFromWidth();

    m_ProjectName = document.metadata.projectName;
    m_PendingOpenInEditorRequest = false;
    m_Dirty = false;
    m_StagePreviewDirty = true;
    m_LastStagePreviewZoom = -1.0f;
    return true;
}

bool CompositeModule::BuildProjectDocumentForSave(const std::string& displayName, StackBinaryFormat::ProjectDocument& outDocument) const {
    std::vector<uint8_t> rgba;
    int rasterW = 0;
    int rasterH = 0;
    if (!BuildExportRaster(rgba, rasterW, rasterH, false) || rgba.empty()) {
        return false;
    }

    std::vector<uint8_t> fullPng;
    EncodePng(rgba, rasterW, rasterH, fullPng);
    if (fullPng.empty()) {
        return false;
    }

    std::vector<uint8_t> thumbRgba;
    int thumbW = rasterW;
    int thumbH = rasterH;
    const int maxEdge = 320;
    if (rasterW > maxEdge || rasterH > maxEdge) {
        const float scale = static_cast<float>(maxEdge) / static_cast<float>(std::max(rasterW, rasterH));
        thumbW = std::max(1, static_cast<int>(std::floor(static_cast<float>(rasterW) * scale)));
        thumbH = std::max(1, static_cast<int>(std::floor(static_cast<float>(rasterH) * scale)));
        ResizeNearest(rgba, rasterW, rasterH, thumbW, thumbH, thumbRgba);
    } else {
        thumbRgba = rgba;
    }

    std::vector<uint8_t> thumbPng;
    EncodePng(thumbRgba, thumbW, thumbH, thumbPng);

    json layersJson = json::array();
    for (const CompositeLayer& layer : m_Layers) {
        if (layer.rgba.empty() || layer.imgW <= 0 || layer.imgH <= 0) {
            continue;
        }

        std::vector<uint8_t> layerPreviewPng;
        if (!BuildTopLeftPngFromBottomLeftRgba(layer.rgba, layer.imgW, layer.imgH, layerPreviewPng)) {
            continue;
        }

        json layerJson = json::object();
        layerJson["id"] = layer.id;
        layerJson["kind"] = LayerKindToToken(layer.kind);
        layerJson["name"] = layer.name;
        layerJson["x"] = layer.x;
        layerJson["y"] = layer.y;
        layerJson["z"] = layer.z;
        layerJson["parentId"] = layer.parentId;
        layerJson["scaleX"] = layer.scaleX;
        layerJson["scaleY"] = layer.scaleY;
        layerJson["preserveAspectRatio"] = layer.preserveAspectRatio;
        layerJson["rotation"] = layer.rotation;
        layerJson["opacity"] = layer.opacity;
        layerJson["visible"] = layer.visible;
        layerJson["locked"] = layer.locked;
        layerJson["flipX"] = layer.flipX;
        layerJson["flipY"] = layer.flipY;
        layerJson["blendMode"] = BlendModeToToken(layer.blendMode);
        layerJson["fillColor"] = json::array({
            layer.fillColor[0],
            layer.fillColor[1],
            layer.fillColor[2],
            layer.fillColor[3]
        });
        layerJson["textContent"] = layer.textContent;
        layerJson["textFontSize"] = layer.textFontSize;
        layerJson["logicalW"] = layer.logicalW;
        layerJson["logicalH"] = layer.logicalH;
        layerJson["imagePng"] = json::binary(std::move(layerPreviewPng));
        layerJson["linkedProjectFileName"] = layer.linkedProjectFileName;
        layerJson["linkedProjectName"] = layer.linkedProjectName;
        layerJson["generatedFromImage"] = layer.generatedFromImage;
        if (!layer.originalSourcePng.empty()) {
            layerJson["sourcePng"] = json::binary(layer.originalSourcePng);
        }
        if (layer.kind == LayerKind::EditorProject) {
            layerJson["projectJson"] = layer.embeddedProjectJson;
        }

        layersJson.push_back(std::move(layerJson));
    }

    json root = json::object();
    root["compositeVersion"] = kCompositeFormatVersion;
    root["layers"] = std::move(layersJson);
    root["selectionLayerId"] = m_SelectedId;

    json viewJson = json::object();
    viewJson["zoom"] = m_ViewZoom;
    viewJson["panX"] = m_ViewPanX;
    viewJson["panY"] = m_ViewPanY;
    viewJson["showChecker"] = m_ShowChecker;
    viewJson["limitProjectResolution"] = m_LimitProjectResolution;
    viewJson["snapEnabled"] = m_SnapEnabled;
    viewJson["snapToObjects"] = m_SnapToObjects;
    viewJson["snapToCenters"] = m_SnapToCenters;
    viewJson["snapToCanvasCenter"] = m_SnapToCanvasCenter;
    viewJson["snapToExportBounds"] = m_SnapToExportBounds;
    viewJson["snapToSpacing"] = m_SnapToSpacing;
    viewJson["gridSize"] = m_GridSize;
    viewJson["rotateSnapStep"] = m_RotateSnapStep;
    viewJson["scaleSnapStep"] = m_ScaleSnapStep;
    root["view"] = std::move(viewJson);

    json exportJson = json::object();
    exportJson["boundsMode"] = ExportBoundsModeToToken(m_ExportSettings.boundsMode);
    exportJson["backgroundMode"] = ExportBackgroundModeToToken(m_ExportSettings.backgroundMode);
    exportJson["backgroundColor"] = json::array({
        m_ExportSettings.backgroundColor[0],
        m_ExportSettings.backgroundColor[1],
        m_ExportSettings.backgroundColor[2],
        m_ExportSettings.backgroundColor[3]
    });
    exportJson["x"] = m_ExportSettings.customX;
    exportJson["y"] = m_ExportSettings.customY;
    exportJson["width"] = m_ExportSettings.customWidth;
    exportJson["height"] = m_ExportSettings.customHeight;
    exportJson["aspectPreset"] = ExportAspectPresetToToken(m_ExportSettings.aspectPreset);
    exportJson["customAspectRatio"] = m_ExportSettings.customAspectRatio;
    exportJson["outputWidth"] = m_ExportSettings.outputWidth;
    exportJson["outputHeight"] = m_ExportSettings.outputHeight;
    root["export"] = std::move(exportJson);

    json workspaceJson = json::object();
    json panelsJson = json::object();
    panelsJson["layers"] = m_ShowLayersWindow;
    panelsJson["selected"] = m_ShowSelectedWindow;
    panelsJson["view"] = m_ShowViewWindow;
    panelsJson["export"] = m_ShowExportWindow;
    workspaceJson["panels"] = std::move(panelsJson);
    workspaceJson["layoutIni"] = m_WorkspaceLayoutIni;
    root["workspace"] = std::move(workspaceJson);

    outDocument.metadata.projectKind = StackBinaryFormat::kCompositeProjectKind;
    outDocument.metadata.projectName = displayName.empty() ? m_ProjectName : displayName;
    outDocument.metadata.timestamp.clear();
    outDocument.metadata.sourceWidth = rasterW;
    outDocument.metadata.sourceHeight = rasterH;
    outDocument.thumbnailBytes = std::move(thumbPng);
    outDocument.sourceImageBytes = std::move(fullPng);
    outDocument.pipelineData = std::move(root);
    return true;
}
