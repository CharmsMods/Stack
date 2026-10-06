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
    bool owned) {
    CachedGraphTexture replacement;
    replacement.viewportRegion = m_RawViewportAppliedRegion;
    replacement.texture = texture;
    replacement.fingerprint = fingerprint;
    replacement.width = m_Width;
    replacement.height = m_Height;
    replacement.owned = owned;
    replacement.bytes = owned
        ? EstimateGraphTargetBytes(m_Width, m_Height)
        : 0;
    replacement.lastUseSerial = ++m_GraphResourceUseSerial;

    try {
        const auto location = cache.try_emplace(key).first;
        auto& entry = location->second;
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

void RenderPipeline::DeleteFrequencyCacheEntry(CachedGraphFrequency& entry) {
    if (entry.owned && entry.resource.texture != 0 &&
        entry.resource.texture != m_SourceTexture &&
        entry.resource.texture != m_ExternalOutputTexture) {
        glDeleteTextures(1, &entry.resource.texture);
    }
    entry = {};
}

void RenderPipeline::DestroyFrequencyCache() {
    for (auto& [key, entry] : m_GraphFrequencyCache) {
        (void)key;
        DeleteFrequencyCacheEntry(entry);
    }
    m_GraphFrequencyCache.clear();
    m_GraphFrequencyAnalysisCache.clear();
}

bool RenderPipeline::StoreFrequencyCacheEntry(
    const std::string& key,
    const RenderFrequencyResource& resource,
    std::size_t fingerprint,
    bool owned) {
    CachedGraphFrequency replacement;
    try {
        replacement.resource = resource;
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    replacement.fingerprint = fingerprint;
    replacement.owned = owned;
    replacement.bytes = owned
        ? static_cast<std::uint64_t>(std::max(resource.paddedWidth, 0)) *
          static_cast<std::uint64_t>(std::max(resource.paddedHeight, 0)) *
          2u * sizeof(float)
        : 0;
    replacement.lastUseSerial = ++m_GraphResourceUseSerial;
    static_assert(
        std::is_nothrow_move_assignable_v<CachedGraphFrequency>,
        "Frequency-cache replacement must commit without throwing.");

    try {
        const auto location =
            m_GraphFrequencyCache.try_emplace(key).first;
        auto& entry = location->second;
        const unsigned int priorTexture = entry.resource.texture;
        const bool deletePrior =
            entry.owned &&
            priorTexture != 0 &&
            priorTexture != resource.texture;
        entry = std::move(replacement);
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

void RenderPipeline::PruneInactiveFrequencyCache(
    const GraphExecutionContext& executionContext) {
    for (auto it = m_GraphFrequencyCache.begin(); it != m_GraphFrequencyCache.end(); ) {
        if (!executionContext.IsActiveNode(ExtractNodeIdFromCacheKey(it->first))) {
            DeleteFrequencyCacheEntry(it->second);
            it = m_GraphFrequencyCache.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = m_GraphFrequencyAnalysisCache.begin();
         it != m_GraphFrequencyAnalysisCache.end(); ) {
        if (!executionContext.IsActiveNode(ExtractNodeIdFromCacheKey(it->first))) {
            it = m_GraphFrequencyAnalysisCache.erase(it);
        } else {
            ++it;
        }
    }
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
    addCache(m_GraphFrequencyCache);
    addCache(m_LutTextureCache);
    return total;
}

void RenderPipeline::TrimGraphPersistentCachesToBudget() {
    enum class CacheKind {
        None,
        Frequency,
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
        for (const auto& [key, entry] : m_GraphFrequencyCache) {
            consider(
                CacheKind::Frequency,
                key,
                entry.bytes,
                entry.lastUseSerial,
                false);
        }

        if (victimKey == nullptr) break;
        totalBytes = victimBytes >= totalBytes
            ? 0
            : totalBytes - victimBytes;
        switch (victimKind) {
            case CacheKind::Frequency: {
                const auto it = m_GraphFrequencyCache.find(*victimKey);
                if (it != m_GraphFrequencyCache.end()) {
                    DeleteFrequencyCacheEntry(it->second);
                    m_GraphFrequencyCache.erase(it);
                }
                break;
            }
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
