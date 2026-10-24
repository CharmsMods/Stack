#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "NodeMath/PointwiseIR.h"

#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <type_traits>
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
    entry.bytes = 0;
    entry.lastUseSerial = 0;
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

    unsigned int copyTexture = m_GraphFloat32Targets
        ? GLHelpers::CreateStorageTexture(width, height, GL_RGBA32F)
        : GLHelpers::CreateEmptyTexture(width, height);
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

bool RenderPipeline::StoreGraphCacheEntry(
    std::unordered_map<std::string, CachedGraphTexture>& cache,
    const std::string& key,
    unsigned int texture,
    std::size_t fingerprint,
    bool owned,
    bool denoisePending) {
    CachedGraphTexture replacement;
    replacement.viewportRegion = m_RawViewportAppliedRegion;
    replacement.graphGeometry = m_CurrentGraphImageGeometry;
    replacement.texture = texture;
    replacement.fingerprint = fingerprint;
    replacement.width = m_Width;
    replacement.height = m_Height;
    replacement.owned = owned;
    replacement.denoisePending = denoisePending;
    replacement.bytes = owned
        ? EstimateGraphTargetBytes(m_Width, m_Height)
        : 0;
    if(owned && texture) {
        const Stack::Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
        glBindTexture(GL_TEXTURE_2D,texture);
        GLint bits=0;
        for(const auto component:{GL_TEXTURE_RED_SIZE,GL_TEXTURE_GREEN_SIZE,GL_TEXTURE_BLUE_SIZE,GL_TEXTURE_ALPHA_SIZE}) {
            GLint size=0;glGetTexLevelParameteriv(GL_TEXTURE_2D,0,component,&size);bits+=size;
        }
        binding.Restore();
        if(bits>0) replacement.bytes=std::uint64_t(m_Width)*m_Height*((bits+7)/8);
    }
    replacement.lastUseSerial = ++m_GraphResourceUseSerial;

    try {
        const auto location = cache.try_emplace(key).first;
        auto& entry = location->second;
        // A thumbnail, editing preview and native render can all use the same
        // immutable source. Keep the other extent within the existing LRU
        // budget instead of making each request evict the previous one.
        if(entry.owned && entry.texture && entry.texture!=texture &&
            (entry.width!=replacement.width || entry.height!=replacement.height ||
             entry.graphGeometry.resident != replacement.graphGeometry.resident)) {
            const auto variant=key+"@"+std::to_string(entry.fingerprint);
            ReleaseGraphCacheEntry(cache,variant);
            cache.emplace(variant,entry);
            entry={};
        }
        const unsigned int priorTexture = entry.texture;
        const bool deletePrior =
            entry.owned &&
            priorTexture != 0 &&
            priorTexture != texture &&
            priorTexture != m_SourceTexture &&
            priorTexture != m_ExternalOutputTexture;
        entry = replacement;
        if (deletePrior) {
            glDeleteTextures(1, &priorTexture);
        }
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    return true;
}

void RenderPipeline::TouchGraphCacheEntry(CachedGraphTexture& entry) {
    entry.lastUseSerial = ++m_GraphResourceUseSerial;
}

RenderPipeline::CachedGraphTexture* RenderPipeline::FindGraphCacheEntry(
    std::unordered_map<std::string,CachedGraphTexture>& cache,const std::string& key,std::size_t fingerprint) {
    const auto current=cache.find(key);
    if(current!=cache.end() && current->second.fingerprint==fingerprint && current->second.texture)
        return &current->second;
    const auto savedKey=key+"@"+std::to_string(fingerprint);
    const auto saved=cache.find(savedKey);
    if(saved==cache.end() || !saved->second.texture) return nullptr;
    // Keep the unsuffixed entry as the currently requested representation for
    // analysis/readback callers. Every texture still has exactly one owner.
    const auto restored=saved->second;
    auto& entry=cache.try_emplace(key).first->second;
    if(entry.owned && entry.texture) {
        const auto variant=key+"@"+std::to_string(entry.fingerprint);
        auto& archived=cache.try_emplace(variant).first->second;
        DeleteGraphCacheEntry(archived);
        archived=entry;
    }
    entry=restored;
    cache.erase(savedKey);
    return &entry;
}









std::uint64_t RenderPipeline::GraphPersistentCacheBytes() const {
    std::uint64_t total = 0;
    const auto addCache = [&](const auto& cache) {
        for (const auto& [key, entry] : cache) {
            (void)key;
            if (entry.bytes > std::numeric_limits<std::uint64_t>::max() - total) {
                total = std::numeric_limits<std::uint64_t>::max();
                return;
            }
            total += entry.bytes;
        }
    };
    addCache(m_GraphImageCache);
    addCache(m_GraphMaskCache);
    addCache(m_LutTextureCache);
    return total;
}

void RenderPipeline::TrimGraphPersistentCachesToBudget() {
    enum class CacheKind {
        None,
        Image,
        Lut,
        Mask
    };

    std::uint64_t totalBytes = GraphPersistentCacheBytes();
    while (totalBytes > m_GraphPersistentCacheBudgetBytes) {
        CacheKind victimKind = CacheKind::None;
        const std::string* victimKey = nullptr;
        std::uint64_t victimBytes = 0;
        std::uint64_t victimSerial = 0;

        const auto consider = [&](
                                  CacheKind kind,
                                  const std::string& key,
                                  std::uint64_t bytes,
                                  std::uint64_t serial,
                                  bool protectedResource) {
            if (protectedResource || bytes == 0) return;
            const bool earlier =
                victimKey == nullptr ||
                serial < victimSerial ||
                (serial == victimSerial &&
                 (kind < victimKind ||
                  (kind == victimKind && key < *victimKey)));
            if (!earlier) return;
            victimKind = kind;
            victimKey = &key;
            victimBytes = bytes;
            victimSerial = serial;
        };

        const auto considerTextureCache = [&](
                                              const auto& cache,
                                              CacheKind kind) {
            for (const auto& [key, entry] : cache) {
                consider(
                    kind,
                    key,
                    entry.bytes,
                    entry.lastUseSerial,
                    entry.texture == m_OutputTexture ||
                        entry.texture == m_GraphSourceTexture ||
                        entry.texture == m_SourceTexture ||
                        entry.texture == m_ExternalOutputTexture);
            }
        };
        considerTextureCache(m_GraphImageCache, CacheKind::Image);
        considerTextureCache(m_GraphMaskCache, CacheKind::Mask);
        considerTextureCache(m_LutTextureCache, CacheKind::Lut);


        if (victimKey == nullptr) break;
        totalBytes = victimBytes >= totalBytes
            ? 0
            : totalBytes - victimBytes;
        switch (victimKind) {
            case CacheKind::Image:
                ReleaseGraphCacheEntry(m_GraphImageCache, *victimKey);
                break;
            case CacheKind::Lut:
                ClearLutTextureKey(*victimKey);
                break;
            case CacheKind::Mask:
                ReleaseGraphCacheEntry(m_GraphMaskCache, *victimKey);
                break;
            case CacheKind::None:
                break;
        }
        ++m_LastGraphExecutionStats.persistentCacheEvictions;
    }
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

std::uint64_t RenderPipeline::GetGraphResidentCacheBytes() const {
    const auto add = [](std::uint64_t a, std::uint64_t b) {
        return b > std::numeric_limits<std::uint64_t>::max() - a
            ? std::numeric_limits<std::uint64_t>::max() : a + b;
    };
    return add(add(GraphPersistentCacheBytes(), RawDevelopStageCacheTotalBytes()),
        GraphTransientTargetBytes());
}
