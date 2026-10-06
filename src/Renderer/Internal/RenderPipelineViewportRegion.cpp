#include "Renderer/RenderPipeline.h"
#include "Renderer/ScopedGLObjects.h"
#include <stdexcept>

void RenderPipeline::ApplyRawViewportRegion(GraphNodeRenderResult& result,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe, Raw::ViewportStage stage,
    std::size_t upstreamFingerprint, int nodeId) {
    if (recipe.requireFullSpatialInput || !m_RawViewportRequest.visible.Partial() || m_RawViewportAppliedRegion.Valid() ||
        m_RawDevelopmentAnalysisEnabled || m_RawDevelopmentViewportValidationEnabled ||
        m_RawDevelopmentGraphScopeStage != RawDevelopmentGraphScopeStage::None ||
        static_cast<int>(stage) < static_cast<int>(Raw::ViewportFirstRegionalStage(recipe))) return;
    const int inputWidth = m_Width, inputHeight = m_Height;
    const auto& crop = recipe.cropRotation;
    const int cropLeft = crop.cropEnabled ? std::clamp(int(std::floor(crop.cropX * inputWidth)), 0, inputWidth - 1) : 0;
    const int cropTop = crop.cropEnabled ? std::clamp(int(std::floor(crop.cropY * inputHeight)), 0, inputHeight - 1) : 0;
    const int cropRight = crop.cropEnabled ? std::clamp(int(std::ceil((crop.cropX + crop.cropWidth) * inputWidth)), cropLeft + 1, inputWidth) : inputWidth;
    const int cropBottom = crop.cropEnabled ? std::clamp(int(std::ceil((crop.cropY + crop.cropHeight) * inputHeight)), cropTop + 1, inputHeight) : inputHeight;
    const auto region = Raw::ScaleViewportRegion(m_RawViewportRequest.visible, cropRight - cropLeft, cropBottom - cropTop);
    if (!region.Valid() || !result.texture) return;
    Stack::Renderer::RawDevelopmentCache::MixJsonHash(upstreamFingerprint, region.Fingerprint());
    const std::string cacheKey = std::to_string(nodeId) + ":__rawViewportInput:" + std::to_string(static_cast<int>(stage));
    const auto cached = FindRawDevelopStageCacheEntry(cacheKey, upstreamFingerprint);
    if (cached.texture) {
        if (result.owned && result.texture != cached.texture) glDeleteTextures(1, &result.texture);
        result.texture = cached.texture;
        result.owned = false;
        m_Width = cached.width;
        m_Height = cached.height;
        m_RawViewportAppliedRegion = region;
        return;
    }
    m_Width = region.width;
    m_Height = region.height;
    Stack::Renderer::ScopedGLTexture target(CreateGraphRenderTargetTexture());
    const int left = cropLeft + region.x;
    const int bottom = inputHeight - cropTop - region.y - region.height;
    bool complete = false;
    const bool rendered = target && RenderIntoGraphTargetTexture(target.Get(), [&](unsigned int) {
        GLint previousRead = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
        GLuint readFbo = 0;
        glGenFramebuffers(1, &readFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, result.texture, 0);
        complete = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        if (complete) glBlitFramebuffer(left, bottom, left + region.width, bottom + region.height,
            0, 0, region.width, region.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousRead);
        glDeleteFramebuffers(1, &readFbo);
    });
    if (!rendered || !complete) {
        m_Width = inputWidth;
        m_Height = inputHeight;
        throw std::runtime_error("RAW viewport region allocation failed");
    }
    if (result.owned) glDeleteTextures(1, &result.texture);
    result.texture = target.Release();
    result.owned = !StoreRawDevelopStageCacheEntry(cacheKey, result.texture, upstreamFingerprint, true);
    m_RawViewportAppliedRegion = region;
}
