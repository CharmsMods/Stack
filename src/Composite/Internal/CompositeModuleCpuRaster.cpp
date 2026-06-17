#include "Composite/Internal/CompositeModuleInternal.h"

#include "Composite/CompositeModule.h"
#include "Composite/EmbeddedCompositeFont.h"
#include "Library/LibraryManager.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float kTextRasterZoomThresholdUp = 1.2f;
constexpr float kTextRasterZoomThresholdDown = 0.6f;
}

bool CompositeModule::EnsureDefaultTextFontLoaded() {
    if (!m_DefaultTextFontBytes.empty()) {
        return true;
    }
    if (m_DefaultTextFontLoadAttempted) {
        return false;
    }

    m_DefaultTextFontLoadAttempted = true;
    m_DefaultTextFontBytes.assign(
        EmbeddedCompositeFont::kRobotoMediumTtf,
        EmbeddedCompositeFont::kRobotoMediumTtf + EmbeddedCompositeFont::kRobotoMediumTtfSize);
    if (!m_DefaultTextFontBytes.empty()) {
        return true;
    }
    return ReadFileBytes(FindBundledCompositeFontPath(), m_DefaultTextFontBytes);
}

bool CompositeModule::RegenerateGeneratedLayerTexture(CompositeLayer& layer) {
    if (!IsGeneratedLayer(layer)) {
        return false;
    }

    std::vector<uint8_t> rgba;
    int width = std::max(1, layer.logicalW);
    int height = std::max(1, layer.logicalH);

    if (layer.kind == LayerKind::ShapeRect) {
        FillSolidRgba(rgba, width, height, layer.fillColor, false);
    } else if (layer.kind == LayerKind::ShapeCircle) {
        FillSolidRgba(rgba, width, height, layer.fillColor, true);
    } else {
        if (!EnsureDefaultTextFontLoaded()) {
            return false;
        }
        std::vector<uint8_t> baseRgba;
        int baseWidth = 0;
        int baseHeight = 0;
        if (!BuildTextRgba(
                m_DefaultTextFontBytes,
                layer.textContent,
                layer.textFontSize,
                layer.fillColor,
                1.0f,
                1.0f,
                baseRgba,
                baseWidth,
                baseHeight)) {
            return false;
        }
        layer.logicalW = baseWidth;
        layer.logicalH = baseHeight;

        const float qualityZoom = EffectiveTextRasterZoom(m_ViewZoom);
        const float renderStretchX = std::max(0.01f, layer.scaleX * qualityZoom);
        const float renderStretchY = std::max(0.01f, layer.scaleY * qualityZoom);
        if (std::abs(renderStretchX - 1.0f) <= 0.0001f && std::abs(renderStretchY - 1.0f) <= 0.0001f) {
            rgba = std::move(baseRgba);
            width = baseWidth;
            height = baseHeight;
        } else if (!BuildTextRgba(
                       m_DefaultTextFontBytes,
                       layer.textContent,
                       layer.textFontSize,
                       layer.fillColor,
                       renderStretchX,
                       renderStretchY,
                       rgba,
                       width,
                       height)) {
            return false;
        }
        layer.textRenderStretchX = renderStretchX;
        layer.textRenderStretchY = renderStretchY;
    }

    if (!rgba.empty()) {
        LibraryManager::FlipImageRowsInPlace(rgba, width, height, 4);
    }

    layer.rgba = std::move(rgba);
    layer.imgW = width;
    layer.imgH = height;
    if (layer.kind != LayerKind::Text) {
        layer.logicalW = width;
        layer.logicalH = height;
        layer.textRenderStretchX = 0.0f;
        layer.textRenderStretchY = 0.0f;
    }
    layer.originalSourcePng.clear();
    if (layer.tex != 0) {
        glDeleteTextures(1, &layer.tex);
        layer.tex = 0;
    }
    if (!layer.rgba.empty()) {
        layer.tex = GLHelpers::CreateTextureFromPixels(layer.rgba.data(), layer.imgW, layer.imgH, 4);
    }
    MarkStageDirty();
    return layer.tex != 0;
}

void CompositeModule::RefreshVisibleTextRasterQuality() {
    const float qualityZoom = EffectiveTextRasterZoom(m_ViewZoom);
    for (CompositeLayer& layer : m_Layers) {
        if (!layer.visible || layer.kind != LayerKind::Text) {
            continue;
        }

        const float targetStretchX = std::max(0.01f, layer.scaleX * qualityZoom);
        const float targetStretchY = std::max(0.01f, layer.scaleY * qualityZoom);
        auto needsReraster = [](const float currentStretch, const float targetStretch) {
            if (currentStretch <= 0.0f) {
                return true;
            }
            const float ratio = targetStretch / std::max(0.01f, currentStretch);
            return ratio >= kTextRasterZoomThresholdUp || ratio <= kTextRasterZoomThresholdDown;
        };

        if (needsReraster(layer.textRenderStretchX, targetStretchX) ||
            needsReraster(layer.textRenderStretchY, targetStretchY)) {
            RegenerateGeneratedLayerTexture(layer);
        }
    }
}

void CompositeModule::UpdateLayerData(
    const std::string& layerId,
    const std::string& projectJson,
    const std::vector<uint8_t>& previewPixels,
    const int w,
    const int h) {

    CompositeLayer* layer = FindLayerById(m_Layers, layerId);
    if (!layer || previewPixels.empty() || w <= 0 || h <= 0) {
        return;
    }

    const float previousWorldW = LayerWorldWidth(*layer);
    const float previousWorldH = LayerWorldHeight(*layer);
    const float centerX = layer->x + previousWorldW * 0.5f;
    const float centerY = layer->y + previousWorldH * 0.5f;

    std::vector<uint8_t> runtimePixels = previewPixels;
    LibraryManager::FlipImageRowsInPlace(runtimePixels, w, h, 4);

    layer->embeddedProjectJson = projectJson;
    layer->kind = LayerKind::EditorProject;
    layer->rgba = std::move(runtimePixels);
    layer->imgW = w;
    layer->imgH = h;
    layer->logicalW = w;
    layer->logicalH = h;

    const float logicalW = std::max(1.0f, static_cast<float>(layer->logicalW));
    const float logicalH = std::max(1.0f, static_cast<float>(layer->logicalH));
    const float fitScaleX = previousWorldW / logicalW;
    const float fitScaleY = previousWorldH / logicalH;
    if (std::isfinite(fitScaleX) && fitScaleX > 0.0f) {
        layer->scaleX = fitScaleX;
    }
    if (std::isfinite(fitScaleY) && fitScaleY > 0.0f) {
        layer->scaleY = fitScaleY;
    }

    layer->x = centerX - logicalW * layer->scaleX * 0.5f;
    layer->y = centerY - logicalH * layer->scaleY * 0.5f;

    if (layer->tex != 0) {
        glDeleteTextures(1, &layer->tex);
        layer->tex = 0;
    }
    layer->tex = GLHelpers::CreateTextureFromPixels(layer->rgba.data(), layer->imgW, layer->imgH, 4);
    MarkDocumentDirty();
}

void CompositeModule::ResizeNearest(
    const std::vector<uint8_t>& src,
    const int sw,
    const int sh,
    const int dw,
    const int dh,
    std::vector<uint8_t>& dst) {
    ResizeNearestRgba(src, sw, sh, dw, dh, dst);
}

void CompositeModule::EncodePng(const std::vector<uint8_t>& rgba, const int w, const int h, std::vector<uint8_t>& outPng) const {
    EncodePngBytes(rgba, w, h, outPng);
}

void CompositeModule::AddImageLayerFromFile(const std::string& path) {
    LayerFileLoadData data;
    if (!LoadImageLayerDataFromFile(path, data)) {
        return;
    }

    ApplyImportDataToNewLayer(data, m_CanvasW, m_CanvasH, m_Layers, m_SelectedId);
    QueueImportedExternalAssetMirror(path, data);
    MarkDocumentDirty();
}

void CompositeModule::AddProjectLayerFromFile(const std::string& path) {
    LayerFileLoadData data;
    if (!LoadEditorProjectLayerDataFromFile(path, m_LimitProjectResolution, data)) {
        return;
    }

    ApplyImportDataToNewLayer(data, m_CanvasW, m_CanvasH, m_Layers, m_SelectedId);
    MarkDocumentDirty();
}

bool CompositeModule::ReplaceSelectedLayerWithImageFile(const std::string& path) {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    LayerFileLoadData data;
    if (!LoadImageLayerDataFromFile(path, data)) {
        return false;
    }

    ApplyImportDataToExistingLayer(data, *selected);
    QueueImportedExternalAssetMirror(path, data);
    MarkDocumentDirty();
    return true;
}

bool CompositeModule::ReplaceSelectedLayerWithProjectFile(const std::string& path) {
    CompositeLayer* selected = GetSelectedLayer();
    if (!selected) {
        return false;
    }

    LayerFileLoadData data;
    if (!LoadEditorProjectLayerDataFromFile(path, m_LimitProjectResolution, data)) {
        return false;
    }

    ApplyImportDataToExistingLayer(data, *selected);
    MarkDocumentDirty();
    return true;
}
