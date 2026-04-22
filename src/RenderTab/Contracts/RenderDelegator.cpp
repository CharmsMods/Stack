#include "RenderTab/Contracts/RenderDelegator.h"

#include "RenderTab/Contracts/AccumulationManager.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLLoader.h"

#include <algorithm>

namespace RenderContracts {

RenderDelegator::~RenderDelegator() {
    Shutdown();
}

bool RenderDelegator::RenderViewport(
    const CompiledScene& compiledScene,
    const RenderFoundation::Settings& settings,
    AccumulationManager& accumulationManager,
    int width,
    int height,
    int selectedObjectId,
    unsigned int& outTextureId,
    std::string& errorMessage) {

    errorMessage.clear();
    outTextureId = 0;
    if (!compiledScene.valid) {
        errorMessage = "The render scene snapshot is not compiled.";
        return false;
    }

    if (settings.viewMode == RenderFoundation::ViewMode::PathTrace) {
        return m_PathTraceRenderer.RenderFrame(
            compiledScene.pathTraceScene,
            settings,
            accumulationManager,
            width,
            height,
            outTextureId,
            errorMessage);
    }

    if (!m_RasterPreviewRenderer.RenderScenePreview(compiledScene.scene, compiledScene.camera, compiledScene.settings, width, height)) {
        errorMessage = m_RasterPreviewRenderer.GetLastError();
        return false;
    }

    if (!m_RasterPreviewRenderer.ComposeViewport(
            m_RasterPreviewRenderer.GetSceneColorTexture(),
            width,
            height,
            selectedObjectId)) {
        errorMessage = m_RasterPreviewRenderer.GetLastError();
        return false;
    }

    outTextureId = m_RasterPreviewRenderer.GetCompositedTexture();
    return outTextureId != 0;
}

bool RenderDelegator::CaptureViewportPixels(
    const CompiledScene& compiledScene,
    const RenderFoundation::Settings& settings,
    const AccumulationManager& accumulationManager,
    int width,
    int height,
    int selectedObjectId,
    std::vector<unsigned char>& outPixels,
    std::string& errorMessage) {

    if (settings.viewMode == RenderFoundation::ViewMode::PathTrace) {
        return m_PathTraceRenderer.CaptureVisiblePixels(width, height, accumulationManager, outPixels, errorMessage);
    }

    unsigned int textureId = 0;
    AccumulationManager dummyAccumulation;
    if (!RenderViewport(compiledScene, settings, dummyAccumulation, width, height, selectedObjectId, textureId, errorMessage)) {
        return false;
    }

    return CaptureTexturePixels(textureId, width, height, outPixels, errorMessage);
}

void RenderDelegator::Shutdown() {
    m_RasterPreviewRenderer.Shutdown();
    m_PathTraceRenderer.Shutdown();
}

bool RenderDelegator::CaptureTexturePixels(
    unsigned int textureId,
    int width,
    int height,
    std::vector<unsigned char>& outPixels,
    std::string& errorMessage) const {

    errorMessage.clear();
    outPixels.clear();
    if (textureId == 0 || width <= 0 || height <= 0) {
        errorMessage = "Viewport texture capture requested without a valid texture target.";
        return false;
    }

    outPixels.resize(static_cast<std::size_t>(width * height * 4), 0);
    const unsigned int captureFbo = GLHelpers::CreateFBO(textureId);
    if (captureFbo == 0) {
        errorMessage = "Failed to create a framebuffer for viewport capture.";
        outPixels.clear();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, captureFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, outPixels.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFbo);

    const std::size_t stride = static_cast<std::size_t>(width * 4);
    std::vector<unsigned char> flipped(outPixels.size(), 0);
    for (int y = 0; y < height; ++y) {
        const std::size_t sourceOffset = static_cast<std::size_t>(y) * stride;
        const std::size_t targetOffset = static_cast<std::size_t>(height - 1 - y) * stride;
        std::copy_n(outPixels.data() + sourceOffset, stride, flipped.data() + targetOffset);
    }
    outPixels.swap(flipped);
    return true;
}

} // namespace RenderContracts
