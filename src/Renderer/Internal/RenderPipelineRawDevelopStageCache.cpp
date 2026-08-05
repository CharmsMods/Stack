#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RenderPipelineGraphExecutionHelpers.h"
#include "Renderer/ScopedGLObjects.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace Stack::Renderer::GraphExecution;

namespace {

constexpr std::size_t kManualRawSubstageHistoryLimit = 2;

bool IsManualRawSubstageCacheKey(std::string_view key) {
    return key.find(":__rawDevelopmentPostLocalExposure") != std::string_view::npos ||
           key.find(":__rawDevelopmentPostLocalRange") != std::string_view::npos ||
           key.find(":__rawDevelopmentPostFinishTone") != std::string_view::npos;
}

} // namespace

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
    for (auto it = m_RawDevelopmentRecipeLayerCache.begin();
         it != m_RawDevelopmentRecipeLayerCache.end(); ) {
        const int nodeId = ExtractNodeIdFromCacheKey(it->first);
        if (!executionContext.IsActiveNode(nodeId)) {
            // Layer destruction releases its GL program on this render context.
            it = m_RawDevelopmentRecipeLayerCache.erase(it);
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
            std::rotate(
                entries.begin(),
                entryIt,
                std::next(entryIt));
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

std::uint64_t RenderPipeline::TrimRawDevelopStageCacheToBudget(
    std::uint64_t currentTotalBytes,
    std::uint64_t maximumBytes,
    unsigned int protectedTexture) {
    while (currentTotalBytes > maximumBytes) {
        const std::string* victimKey = nullptr;
        std::uint64_t victimBytes = 0;
        for (const auto& [cacheKey, entries] : m_RawDevelopStageImageCache) {
            if (entries.empty()) {
                continue;
            }
            const CachedGraphTexture& candidate = entries.back();
            if (candidate.texture == protectedTexture) {
                continue;
            }
            const std::uint64_t bytes = RawDevelopStageCacheEntryBytes(candidate);
            if (bytes > victimBytes) {
                victimKey = &cacheKey;
                victimBytes = bytes;
            }
        }
        if (victimKey == nullptr || victimBytes == 0) {
            break;
        }
        auto victimIt = m_RawDevelopStageImageCache.find(*victimKey);
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
    return currentTotalBytes;
}

void RenderPipeline::StoreRawDevelopStageCacheEntry(const std::string& key, unsigned int texture, std::size_t fingerprint) {
    if (key.empty() || texture == 0 || fingerprint == 0 || m_Width <= 0 || m_Height <= 0) {
        return;
    }

    std::size_t maxEntriesForDimensions = ResolveRawDevelopStageCacheMaxEntries(m_Width, m_Height);
    if (IsManualRawSubstageCacheKey(key)) {
        // Slider churn only needs the current and immediately previous manual
        // boundary. Keep the broader history policy for candidate comparisons.
        maxEntriesForDimensions = std::min(
            maxEntriesForDimensions,
            kManualRawSubstageHistoryLimit);
    }
    if (maxEntriesForDimensions == 0) {
        return;
    }

    const std::uint64_t entryBytes =
        EstimateRawDevelopStageCacheTextureBytes(m_Width, m_Height);
    if (entryBytes == 0 || entryBytes > kRawDevelopStageCacheSoftByteBudget) {
        return;
    }
    const std::uint64_t preallocationBudget =
        kRawDevelopStageCacheSoftByteBudget - entryBytes;
    const std::uint64_t remainingBytes =
        TrimRawDevelopStageCacheToBudget(
            RawDevelopStageCacheTotalBytes(),
            preallocationBudget,
            texture);
    if (remainingBytes > preallocationBudget) {
        // The only remaining cache owner may be the texture being cloned.
        // Do not exceed the cache budget just to create another alias.
        return;
    }

    Stack::Renderer::ScopedGLTexture copyTexture(
        CloneTextureForRawDevelopStageCache(texture));
    if (!copyTexture) {
        return;
    }

    CachedGraphTexture newEntry;
    newEntry.texture = copyTexture.Get();
    newEntry.fingerprint = fingerprint;
    newEntry.width = m_Width;
    newEntry.height = m_Height;
    newEntry.owned = true;

    decltype(m_RawDevelopStageImageCache)::iterator cacheLocation;
    bool insertedCacheKey = false;
    try {
        const auto inserted =
            m_RawDevelopStageImageCache.try_emplace(key);
        cacheLocation = inserted.first;
        insertedCacheKey = inserted.second;
        cacheLocation->second.insert(
            cacheLocation->second.begin(),
            newEntry);
    } catch (const std::bad_alloc&) {
        if (insertedCacheKey) {
            m_RawDevelopStageImageCache.erase(cacheLocation);
        }
        return;
    } catch (const std::length_error&) {
        if (insertedCacheKey) {
            m_RawDevelopStageImageCache.erase(cacheLocation);
        }
        return;
    }
    copyTexture.Release();

    auto& entries = cacheLocation->second;
    for (auto entryIt = std::next(entries.begin());
         entryIt != entries.end();
         ++entryIt) {
        if (entryIt->fingerprint == fingerprint) {
            DeleteRawDevelopStageCacheEntry(*entryIt);
            entries.erase(entryIt);
            break;
        }
    }
    // Large RAWs can make each RGBA16F boundary snapshot hundreds of MB.
    // Keep reuse generous for small files, but trade cache hits for stability on large candidate runs.
    TrimRawDevelopStageCacheVector(entries, maxEntriesForDimensions);
    (void)TrimRawDevelopStageCacheToBudget(
        RawDevelopStageCacheTotalBytes(),
        kRawDevelopStageCacheSoftByteBudget);
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
