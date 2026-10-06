#include "Editor/EditorModule.h"
#include <algorithm>

const Stack::Renderer::RawImageBackdropFrame* EditorModule::GetRawImageBackdrop() const {
    return m_RawImageBackdrop.frame == ImGui::GetFrameCount() && !m_RawImageBackdrop.patches.empty()
        ? &m_RawImageBackdrop : nullptr;
}

void EditorModule::PublishRawImageBackdrop(ImVec2 imageMin, ImVec2 imageMax,
    ImVec2 viewMin, ImVec2 viewMax, bool rawStagePreview) {
    if (m_PermanentGalleryWorkspace || IsBracketingToolActive() ||
        ImGui::GetWindowViewport() != ImGui::GetMainViewport() ||
        m_RawImageBackdrop.frame == ImGui::GetFrameCount()) return;
    auto& frame = m_RawImageBackdrop;
    frame.frame = ImGui::GetFrameCount();
    frame.presentationFingerprint = m_RawViewportPresentationContent;
    frame.patches.clear();
    frame.completePhotoCoverage = false;
    frame.sharpDrawList = nullptr;
    frame.imageMinimum = imageMin; frame.imageMaximum = imageMax;
    const bool hasControls = m_RawActiveControlFrame == ImGui::GetFrameCount() &&
        GetRawActiveControlBounds(frame.controlsMinimum, frame.controlsMaximum);
    // Floating tools have no panel-sized blur. Their geometry still determines
    // first image contact and the separate Alt selector's temporary blur.
    frame.controlsAmount = 0.f;
    const ImVec2 size(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
    // Contact latches the target on. Fit changes the target to off without
    // discarding the current opacity, so both directions can finish their fade.
    auto& ui = m_RawWorkspaceLabUi;
    const bool atFit = ui.previewLayout.fitting && !ui.previewZoomAnimating;
    const bool contact = hasControls && m_RawWorkspaceLabEditingSurfaceReveal > .001f &&
        imageMin.x <= frame.controlsMaximum.x && imageMax.x >= frame.controlsMinimum.x &&
        imageMin.y <= frame.controlsMaximum.y && imageMax.y >= frame.controlsMinimum.y;
    const double now = ImGui::GetTime();
    const float t = ui.previewSurroundStarted >= 0.0 ? float(std::clamp(
        (now-ui.previewSurroundStarted-.150)/.900, 0.0, 1.0)) : 1.f;
    const float eased = t*t*t*(t*(6.f*t-15.f)+10.f);
    frame.extensionOpacity = std::clamp(ui.previewSurroundFrom +
        ((ui.previewSurroundTarget ? 1.f : 0.f)-ui.previewSurroundFrom)*eased, 0.f, 1.f);
    const bool target = !atFit && (contact || ui.previewSurroundTarget);
    if (target != ui.previewSurroundTarget) {
        ui.previewSurroundFrom = frame.extensionOpacity;
        ui.previewSurroundStarted = now;
        ui.previewSurroundTarget = target;
    }
    frame.extendImage = ui.previewSurroundTarget || frame.extensionOpacity > 0.f;
    frame.edgeOverlap = std::min(12.f, std::min(size.x, size.y)*.25f);
    // At fit there are no pixels beyond the ordinary image draw to compose.
    // Avoid the full-window copy and blur work until the photo leaves it.
    if (!frame.extendImage && imageMin.x >= viewMin.x && imageMin.y >= viewMin.y &&
        imageMax.x <= viewMax.x && imageMax.y <= viewMax.y) return;
    // The overview only fills gaps while a newly exposed area is rendering.
    // Accepted regional/tiled detail overlays it at its true image position.
    const bool regional = rawStagePreview && m_RawViewportPresentedRegion.Valid();
    const bool tiled = m_RawWorkspacePreviewOutputKind == RawWorkspacePreviewOutputKind::Tiled;
    const auto region = regional ? m_RawViewportPresentedRegion : Raw::ViewportRegion{};
    frame.nativeExtent = ImVec2(
        float(m_ViewportOutputExpectedNativeWidth > 0 ? m_ViewportOutputExpectedNativeWidth :
            region.Valid() ? region.fullWidth : m_RawWorkspacePresentationTexture.width),
        float(m_ViewportOutputExpectedNativeHeight > 0 ? m_ViewportOutputExpectedNativeHeight :
            region.Valid() ? region.fullHeight : m_RawWorkspacePresentationTexture.height));
    unsigned int fullTexture = (regional || tiled) ? m_RawViewportOverviewTexture.texture
                                                  : m_RawWorkspacePresentationTexture.texture;
    if ((regional || tiled) && m_RawViewportOverviewContent != m_RawViewportPresentationContent) fullTexture = 0;
    if (IsViewportTextureSafeForDrawing(fullTexture)) {
        frame.patches.push_back({fullTexture, imageMin, imageMax});
        frame.completePhotoCoverage = true;
    }
    if (tiled && HasViewportOutputTiles()) {
        const auto& tiles = GetViewportOutputTiles();
        if (!tiles.complete || tiles.fullWidth <= 0 || tiles.fullHeight <= 0) return;
        bool completeTiles = true;
        frame.nativeExtent = ImVec2(float(tiles.fullWidth), float(tiles.fullHeight));
        for (const auto& tile : tiles.tiles) {
            if (!tile.texture || tile.haloWidth <= 0 || tile.haloHeight <= 0) { completeTiles = false; continue; }
            const ImVec2 min(imageMin.x + size.x * tile.x / tiles.fullWidth,
                imageMax.y - size.y * (tile.y + tile.height) / tiles.fullHeight);
            const ImVec2 max(imageMin.x + size.x * (tile.x + tile.width) / tiles.fullWidth,
                imageMax.y - size.y * tile.y / tiles.fullHeight);
            frame.patches.push_back({tile.texture, min, max,
                ImVec2((tile.x - tile.haloX + .5f) / tile.haloWidth,
                    1.f - (tile.y - tile.haloY + tile.height - .5f) / tile.haloHeight),
                ImVec2((tile.x - tile.haloX + tile.width - .5f) / tile.haloWidth,
                    1.f - (tile.y - tile.haloY + .5f) / tile.haloHeight),
                ImVec4(tile.x == 0 ? 1.f : 0.f, tile.y == 0 ? 1.f : 0.f,
                    tile.x + tile.width == tiles.fullWidth ? 1.f : 0.f,
                    tile.y + tile.height == tiles.fullHeight ? 1.f : 0.f)});
            frame.patches.back().imageBounds = ImVec4(
                float(tile.x) / tiles.fullWidth, float(tile.y) / tiles.fullHeight,
                float(tile.x + tile.width) / tiles.fullWidth,
                float(tile.y + tile.height) / tiles.fullHeight);
        }
        frame.completePhotoCoverage = frame.completePhotoCoverage || (completeTiles && !tiles.tiles.empty());
    }
    const auto addRegion = [&](unsigned int texture, const Raw::ViewportRegion& region) {
        if (!IsViewportTextureSafeForDrawing(texture)) return;
        if (!region.Valid()) {
            frame.patches.push_back({texture, imageMin, imageMax});
            frame.completePhotoCoverage = true;
            return;
        }
        frame.patches.push_back({texture,
            ImVec2(imageMin.x + size.x * region.x / region.fullWidth,
                imageMin.y + size.y * region.y / region.fullHeight),
            ImVec2(imageMin.x + size.x * (region.x + region.width) / region.fullWidth,
                imageMin.y + size.y * (region.y + region.height) / region.fullHeight),
            ImVec2(0, 1), ImVec2(1, 0),
            ImVec4(region.x == 0 ? 1.f : 0.f,
                region.y + region.height == region.fullHeight ? 1.f : 0.f,
                region.x + region.width == region.fullWidth ? 1.f : 0.f,
                region.y == 0 ? 1.f : 0.f)});
        frame.patches.back().imageBounds = ImVec4(
            float(region.x) / region.fullWidth,
            1.f - float(region.y + region.height) / region.fullHeight,
            float(region.x + region.width) / region.fullWidth,
            1.f - float(region.y) / region.fullHeight);
        frame.completePhotoCoverage = frame.completePhotoCoverage || !region.Partial();
    };
    if (regional && !tiled) addRegion(m_RawWorkspacePresentationTexture.texture, m_RawViewportPresentedRegion);
    // Retained detail belongs to every current RAW presentation, including
    // graph-backed output. Regional placement still follows rawStagePreview.
    if (!tiled && m_RawViewportDetailContent &&
        m_RawViewportDetailContent == m_RawViewportPresentationContent)
        addRegion(m_RawViewportDetailTexture.texture, m_RawViewportDetailRegion);

    // finishImage publishes after the viewport has advanced or cancelled its
    // fade. Use that same blend everywhere, including beneath the controls.
    // The renderer only borrows these textures for this frame.
    if (!tiled && m_RawViewportFadeDuration > 0 && m_RawViewportFadeStarted >= 0 &&
        IsViewportTextureSafeForDrawing(m_RawViewportPreviousTexture.texture)) {
        const auto map = Raw::MapViewportFade(m_RawViewportFadeBase.region, m_RawViewportLastPresentation.region);
        if (map.valid) {
            for (auto& patch : frame.patches) {
                if (patch.texture != m_RawWorkspacePresentationTexture.texture) continue;
                patch.previousTexture = m_RawViewportPreviousTexture.texture;
                patch.previousOffset = ImVec2(float(map.x), float(map.y));
                patch.previousScale = ImVec2(float(map.width), float(map.height));
                patch.fadeAmount = float(std::clamp((ImGui::GetTime()-m_RawViewportFadeStarted) /
                    m_RawViewportFadeDuration, 0.0, 1.0));
                patch.encodedSrgb = m_RawViewportLastPresentation.encodedSrgb;
            }
        }
    }
}
