#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

void RenderPipeline::DestroyRawDevelopStageCache() {
    for (auto& [key, entries] : m_RawDevelopStageImageCache) {
        (void)key;
        for (CachedGraphTexture& entry : entries) {
            if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
                glDeleteTextures(1, &entry.texture);
            }
        }
    }
    m_RawDevelopStageImageCache.clear();
}

void RenderPipeline::PruneInactiveRawDevelopStageCache(const GraphExecutionContext& executionContext) {
    for (auto it = m_RawDevelopStageImageCache.begin(); it != m_RawDevelopStageImageCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            for (CachedGraphTexture& entry : it->second) {
                DeleteRawDevelopStageCacheEntry(entry);
            }
            it = m_RawDevelopStageImageCache.erase(it);
        } else {
            ++it;
        }
    }
}

RenderPipeline::CachedGraphTexture RenderPipeline::FindRawDevelopStageCacheEntry(const std::string& key, std::size_t fingerprint) {
    if (fingerprint == 0) {
        return {};
    }
    auto cacheIt = m_RawDevelopStageImageCache.find(key);
    if (cacheIt == m_RawDevelopStageImageCache.end()) {
        return {};
    }
    auto& entries = cacheIt->second;
    for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
        if (entryIt->fingerprint != fingerprint || entryIt->texture == 0) {
            continue;
        }
        const CachedGraphTexture hit = *entryIt;
        if (entryIt != entries.begin()) {
            entries.erase(entryIt);
            entries.insert(entries.begin(), hit);
        }
        return hit;
    }
    return {};
}

unsigned int RenderPipeline::CloneTextureForRawDevelopStageCache(unsigned int sourceTexture) {
    return CloneTextureForGraphCache(sourceTexture, m_Width, m_Height);
}

void RenderPipeline::DeleteRawDevelopStageCacheEntry(RenderPipeline::CachedGraphTexture& entry) {
    if (entry.owned && entry.texture != 0 && entry.texture != m_SourceTexture && entry.texture != m_ExternalOutputTexture) {
        glDeleteTextures(1, &entry.texture);
    }
    entry.texture = 0;
    entry.owned = false;
}

std::uint64_t RenderPipeline::RawDevelopStageCacheEntryBytes(const RenderPipeline::CachedGraphTexture& entry) const {
    if (!entry.owned || entry.texture == 0) {
        return 0;
    }
    return EstimateRawDevelopStageCacheTextureBytes(entry.width, entry.height);
}

std::uint64_t RenderPipeline::RawDevelopStageCacheTotalBytes() const {
    std::uint64_t total = 0;
    for (const auto& [cacheKey, entries] : m_RawDevelopStageImageCache) {
        (void)cacheKey;
        for (const CachedGraphTexture& entry : entries) {
            const std::uint64_t bytes = RawDevelopStageCacheEntryBytes(entry);
            if (total > std::numeric_limits<std::uint64_t>::max() - bytes) {
                return std::numeric_limits<std::uint64_t>::max();
            }
            total += bytes;
        }
    }
    return total;
}

void RenderPipeline::TrimRawDevelopStageCacheVector(std::vector<RenderPipeline::CachedGraphTexture>& entries, std::size_t maxEntries) {
    while (entries.size() > maxEntries) {
        CachedGraphTexture& stale = entries.back();
        DeleteRawDevelopStageCacheEntry(stale);
        entries.pop_back();
    }
}

void RenderPipeline::TrimRawDevelopStageCacheToBudget(std::uint64_t currentTotalBytes) {
    while (currentTotalBytes > kRawDevelopStageCacheSoftByteBudget) {
        std::string victimKey;
        std::uint64_t victimBytes = 0;
        for (const auto& [cacheKey, entries] : m_RawDevelopStageImageCache) {
            if (entries.empty()) {
                continue;
            }
            const std::uint64_t bytes = RawDevelopStageCacheEntryBytes(entries.back());
            if (bytes > victimBytes) {
                victimKey = cacheKey;
                victimBytes = bytes;
            }
        }
        if (victimKey.empty() || victimBytes == 0) {
            break;
        }
        auto victimIt = m_RawDevelopStageImageCache.find(victimKey);
        if (victimIt == m_RawDevelopStageImageCache.end() || victimIt->second.empty()) {
            break;
        }
        CachedGraphTexture& stale = victimIt->second.back();
        DeleteRawDevelopStageCacheEntry(stale);
        victimIt->second.pop_back();
        currentTotalBytes =
            currentTotalBytes > victimBytes
                ? currentTotalBytes - victimBytes
                : 0;
        if (victimIt->second.empty()) {
            m_RawDevelopStageImageCache.erase(victimIt);
        }
    }
}

void RenderPipeline::StoreRawDevelopStageCacheEntry(const std::string& key, unsigned int texture, std::size_t fingerprint) {
    if (key.empty() || texture == 0 || fingerprint == 0 || m_Width <= 0 || m_Height <= 0) {
        return;
    }

    const std::size_t maxEntriesForDimensions = ResolveRawDevelopStageCacheMaxEntries(m_Width, m_Height);
    if (maxEntriesForDimensions == 0) {
        return;
    }

    unsigned int copyTexture = CloneTextureForRawDevelopStageCache(texture);
    if (copyTexture == 0) {
        return;
    }

    CachedGraphTexture newEntry;
    newEntry.texture = copyTexture;
    newEntry.fingerprint = fingerprint;
    newEntry.width = m_Width;
    newEntry.height = m_Height;
    newEntry.owned = true;

    auto& entries = m_RawDevelopStageImageCache[key];
    for (auto entryIt = entries.begin(); entryIt != entries.end(); ++entryIt) {
        if (entryIt->fingerprint != fingerprint) {
            continue;
        }
        DeleteRawDevelopStageCacheEntry(*entryIt);
        entries.erase(entryIt);
        break;
    }

    entries.insert(entries.begin(), newEntry);
    // Large RAWs can make each RGBA16F boundary snapshot hundreds of MB.
    // Keep reuse generous for small files, but trade cache hits for stability on large candidate runs.
    TrimRawDevelopStageCacheVector(entries, maxEntriesForDimensions);
    TrimRawDevelopStageCacheToBudget(RawDevelopStageCacheTotalBytes());
}

std::uint64_t RenderPipeline::EstimateRawDevelopStageCacheTextureBytesForValidation(int width, int height) {
    return EstimateRawDevelopStageCacheTextureBytes(width, height);
}

std::size_t RenderPipeline::ResolveRawDevelopStageCacheMaxEntriesForValidation(int width, int height) {
    return ResolveRawDevelopStageCacheMaxEntries(width, height);
}

bool RenderPipeline::ShouldCacheRawDevelopStageTextureForValidation(int width, int height) {
    return ResolveRawDevelopStageCacheMaxEntries(width, height) > 0;
}
