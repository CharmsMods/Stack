#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

#include <string>
#include <unordered_map>

using namespace Stack::Renderer::GraphExecution;

void RenderPipeline::DeleteGraphCacheEntry(RenderPipeline::CachedGraphTexture& entry) {
    if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = 0;
    entry.owned = false;
    entry.width = 0;
    entry.height = 0;
    entry.fingerprint = 0;
}

void RenderPipeline::DestroyGraphCache(std::unordered_map<std::string, CachedGraphTexture>& cache) {
    for (auto& [key, entry] : cache) {
        (void)key;
        DeleteGraphCacheEntry(entry);
    }
    cache.clear();
}

void RenderPipeline::ReleaseGraphCacheEntry(
    std::unordered_map<std::string, CachedGraphTexture>& cache,
    const std::string& key) {
    auto it = cache.find(key);
    if (it == cache.end()) {
        return;
    }
    DeleteGraphCacheEntry(it->second);
    cache.erase(it);
}

unsigned int RenderPipeline::CloneTextureForGraphCache(unsigned int sourceTexture, int width, int height) {
    if (sourceTexture == 0 || width <= 0 || height <= 0) {
        return 0;
    }

    unsigned int copyTexture = GLHelpers::CreateEmptyTexture(width, height);
    if (copyTexture == 0) {
        return 0;
    }

    const ScopedFramebufferState savedState(true);
    unsigned int sourceFbo = GLHelpers::CreateFBO(sourceTexture);
    unsigned int copyFbo = GLHelpers::CreateFBO(copyTexture);
    if (sourceFbo == 0 || copyFbo == 0) {
        if (sourceFbo != 0) glDeleteFramebuffers(1, &sourceFbo);
        if (copyFbo != 0) glDeleteFramebuffers(1, &copyFbo);
        glDeleteTextures(1, &copyTexture);
        savedState.Restore(true);
        return 0;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, copyFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(
        0, 0, width, height,
        0, 0, width, height,
        GL_COLOR_BUFFER_BIT,
        GL_NEAREST);

    const GLenum copyError = glGetError();
    savedState.Restore(true);
    glDeleteFramebuffers(1, &sourceFbo);
    glDeleteFramebuffers(1, &copyFbo);
    if (copyError != GL_NO_ERROR) {
        glDeleteTextures(1, &copyTexture);
        return 0;
    }
    return copyTexture;
}

void RenderPipeline::StoreGraphCacheEntry(
    std::unordered_map<std::string, CachedGraphTexture>& cache,
    const std::string& key,
    unsigned int texture,
    std::size_t fingerprint,
    bool owned) {
    auto& entry = cache[key];
    if (entry.owned && entry.texture != 0 && entry.texture != texture && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = texture;
    entry.fingerprint = fingerprint;
    entry.width = m_Width;
    entry.height = m_Height;
    entry.owned = owned;
}

void RenderPipeline::PruneInactiveGraphCache(
    std::unordered_map<std::string, CachedGraphTexture>& cache,
    const GraphExecutionContext& executionContext) {
    for (auto it = cache.begin(); it != cache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            DeleteGraphCacheEntry(it->second);
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}
