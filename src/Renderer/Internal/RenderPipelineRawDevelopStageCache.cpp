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
    return key.find(":__rawDevelopmentPostLocalRange") != std::string_view::npos ||
           key.find(":__rawDevelopmentPostFinishTone") != std::string_view::npos ||
           key.find(":__rawDevelopmentPostColorWarp") != std::string_view::npos ||
           key.find(":__rawDevelopmentPostViewTransform") != std::string_view::npos ||
           key.find(":__rawDevelopmentPostOutputCrop") != std::string_view::npos;
}

bool IsTransientExposureDependentCacheKey(std::string_view key) {
    return key.find(":__rawDevelopmentPlacement") != std::string_view::npos ||
           IsManualRawSubstageCacheKey(key);
}

bool IsTransientExposureProtectedCacheKey(std::string_view key) {
    return key.find(":__rawDevelopmentNeutral") != std::string_view::npos ||
           key.find(":__rawDevelopmentRgbBase") != std::string_view::npos ||
           key.find(":__rawDevelopmentRawBase") != std::string_view::npos;
}

bool CacheKeyMatchesStage(
    std::string_view key,
    Stack::Renderer::RawDevelopmentCache::Stage stage) {
    using Stage = Stack::Renderer::RawDevelopmentCache::Stage;
    switch (stage) {
        case Stage::RawBase:
            return key.find(":__rawDevelopmentRawBase") != std::string_view::npos ||
                   key.find(":__rawDevelopmentRgbBase") != std::string_view::npos;
        case Stage::NeutralPlacement:
            return key.find(":__rawDevelopmentNeutral") != std::string_view::npos ||
                   key.find(":__rawDevelopmentRgbDenoise") != std::string_view::npos;
        case Stage::RawPlacement:
            return key.find(":__rawDevelopmentPlacement") != std::string_view::npos;
        case Stage::PostLocalRange:
            return key.find(":__rawDevelopmentPostLocalRange") != std::string_view::npos;
        case Stage::PostFinishTone:
            return key.find(":__rawDevelopmentPostFinishTone") != std::string_view::npos;
        case Stage::PostColorWarp:
            return key.find(":__rawDevelopmentPostColorWarp") != std::string_view::npos;
        case Stage::PostViewTransform:
            return key.find(":__rawDevelopmentPostViewTransform") != std::string_view::npos;
        case Stage::PostOutputCrop:
            return key.find(":__rawDevelopmentPostOutputCrop") != std::string_view::npos;
    }
    return false;
}

int RawDevelopStageCacheEvictionPriority(
    std::string_view key,
    std::size_t entryCount) {
    // Discard history before the current boundary, then downstream authored
    // stages before expensive sensor-domain bases. This preserves CFA and RGB
    // denoise reuse for Lift/zones/curve/color/view edits whenever the shared
    // memory budget can retain at least one native upstream texture.
    if (entryCount > 1u) return 40;
    if (IsManualRawSubstageCacheKey(key)) return 30;
    if (key.find(":__rawDevelopmentPlacement") != std::string_view::npos) {
        return 20;
    }
    if (key.find(":__rawDevelopmentNeutral") != std::string_view::npos ||
        key.find(":__rawDevelopmentRgbBase") != std::string_view::npos) {
        return 10;
    }
    if (key.find(":__rawDevelopmentRawBase") != std::string_view::npos) {
        return 0;
    }
    return 20;
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
    for (auto entryIt = entries.begin(); entryIt != entries.end(); ) {
        if (entryIt->fingerprint != fingerprint) {
            ++entryIt;
            continue;
        }
        if (entryIt->texture == 0 ||
            entryIt->width <= 0 ||
            entryIt->height <= 0 ||
            glIsTexture(entryIt->texture) != GL_TRUE) {
            // A cache entry can outlive the texture it refers to when a
            // render target is rebuilt or a shared GL resource is discarded.
            // Do not let a stale name pass the cache lookup and become the
            // next RAW presentation.
            DeleteRawDevelopStageCacheEntry(*entryIt);
            entryIt = entries.erase(entryIt);
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
    if (entries.empty()) {
        m_RawDevelopStageImageCache.erase(cacheIt);
    }
    return {};
}

void RenderPipeline::InvalidateRawDevelopStageCacheEntry(
    const std::string& key,
    std::size_t fingerprint) {
    const auto cacheIt = m_RawDevelopStageImageCache.find(key);
    if (cacheIt == m_RawDevelopStageImageCache.end()) {
        return;
    }
    auto& entries = cacheIt->second;
    for (auto entryIt = entries.begin(); entryIt != entries.end(); ) {
        if (fingerprint != 0 && entryIt->fingerprint != fingerprint) {
            ++entryIt;
            continue;
        }
        DeleteRawDevelopStageCacheEntry(*entryIt);
        entryIt = entries.erase(entryIt);
    }
    if (entries.empty()) {
        m_RawDevelopStageImageCache.erase(cacheIt);
    }
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
    return EstimateGraphTargetBytes(entry.width, entry.height);
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
    // A native denoise dependency and the newest preview can coexist even
    // when ordinary stage history is restricted to a single raster.
    if (std::any_of(entries.begin(), entries.end(), [](const auto& entry) { return entry.viewportNativeDependency; }))
        maxEntries = std::max(maxEntries, std::size_t{2});
    while (entries.size() > maxEntries) {
        auto victim = std::find_if(entries.rbegin(), entries.rend(), [](const auto& entry) { return !entry.viewportNativeDependency; });
        if (victim == entries.rend()) break;
        DeleteRawDevelopStageCacheEntry(*victim);
        entries.erase(std::next(victim).base());
    }
}

std::uint64_t RenderPipeline::TrimRawDevelopStageCacheToBudget(
    std::uint64_t currentTotalBytes,
    std::uint64_t maximumBytes,
    unsigned int protectedTexture) {
    while (currentTotalBytes > maximumBytes) {
        const std::string* victimKey = nullptr;
        std::uint64_t victimBytes = 0;
        int victimPriority = std::numeric_limits<int>::min();
        std::size_t victimIndex = 0;
        for (const auto& [cacheKey, entries] : m_RawDevelopStageImageCache) {
            if (entries.empty()) {
                continue;
            }
            if (m_RawDevelopmentGlobalExposureInteraction &&
                IsTransientExposureProtectedCacheKey(cacheKey)) {
                continue;
            }
            if (m_RawDevelopmentPreferredCacheInputStage.has_value() &&
                entries.size() == 1u &&
                RawDevelopStageCacheEntryBytes(entries.front()) <= m_RawDevelopStageCacheBudgetBytes &&
                CacheKeyMatchesStage(
                    cacheKey,
                    *m_RawDevelopmentPreferredCacheInputStage)) {
                // The active editor's immediate input is more useful than an
                // arbitrary authored boundary. Historical entries remain
                // evictable so preference never creates unbounded retention.
                continue;
            }
            for (std::size_t index = entries.size(); index-- > 0;) {
                const CachedGraphTexture& candidate = entries[index];
                if (candidate.texture == protectedTexture || candidate.viewportNativeDependency) continue;
                const std::uint64_t bytes = RawDevelopStageCacheEntryBytes(candidate);
                const int priority = RawDevelopStageCacheEvictionPriority(cacheKey, index > 0 ? 2u : 1u);
                if (priority > victimPriority ||
                    (priority == victimPriority && bytes > victimBytes)) {
                    victimKey = &cacheKey;
                    victimBytes = bytes;
                    victimPriority = priority;
                    victimIndex = index;
                }
            }
        }
        if (victimKey == nullptr || victimBytes == 0) {
            break;
        }
        auto victimIt = m_RawDevelopStageImageCache.find(*victimKey);
        if (victimIt == m_RawDevelopStageImageCache.end() || victimIt->second.empty()) {
            break;
        }
        CachedGraphTexture& stale = victimIt->second[victimIndex];
        DeleteRawDevelopStageCacheEntry(stale);
        victimIt->second.erase(victimIt->second.begin() + victimIndex);
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

bool RenderPipeline::StoreRawDevelopStageCacheEntry(
    const std::string& key,
    unsigned int texture,
    std::size_t fingerprint,
    bool takeTextureOwnership) {
    if (key.empty() || texture == 0 || fingerprint == 0 || m_Width <= 0 || m_Height <= 0) {
        return false;
    }
    if (m_RawDevelopmentGlobalExposureInteraction &&
        IsTransientExposureDependentCacheKey(key)) {
        // Pointer samples have distinct exposure fingerprints, so retaining
        // full-frame clones provides no useful reuse and can evict the neutral
        // placement that makes Global Exposure inexpensive. The settled
        // command restores ordinary cache population.
        return false;
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
        return false;
    }

    const std::uint64_t entryBytes =
        EstimateGraphTargetBytes(m_Width, m_Height);
    if (entryBytes == 0 || entryBytes > m_RawDevelopStageCacheBudgetBytes) {
        return false;
    }
    if (m_RawDevelopmentCachePrewarmActive &&
        (entryBytes > m_RawDevelopmentCachePrewarmByteBudget ||
         m_RawDevelopmentCachePrewarmStoredBytes >
             m_RawDevelopmentCachePrewarmByteBudget - entryBytes)) {
        // Hover work is speculative. Never let it allocate beyond its small
        // request budget, even when the normal RAW stage cache has room.
        return false;
    }
    const std::uint64_t preallocationBudget =
        m_RawDevelopStageCacheBudgetBytes - entryBytes;
    const std::uint64_t remainingBytes =
        TrimRawDevelopStageCacheToBudget(
            RawDevelopStageCacheTotalBytes(),
            preallocationBudget,
            texture);
    if (remainingBytes > preallocationBudget) {
        // The only remaining cache owner may be the texture being cloned.
        // Do not exceed the cache budget just to create another alias.
        return false;
    }

    Stack::Renderer::ScopedGLTexture copyTexture;
    unsigned int cachedTexture = texture;
    if (!takeTextureOwnership) {
        copyTexture.Reset(CloneTextureForRawDevelopStageCache(texture));
        if (!copyTexture) {
            return false;
        }
        cachedTexture = copyTexture.Get();
    }

    CachedGraphTexture newEntry;
    newEntry.texture = cachedTexture;
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
        return false;
    } catch (const std::length_error&) {
        if (insertedCacheKey) {
            m_RawDevelopStageImageCache.erase(cacheLocation);
        }
        return false;
    }
    if (!takeTextureOwnership) {
        copyTexture.Release();
    }
    if (m_RawDevelopmentCachePrewarmActive) {
        m_RawDevelopmentCachePrewarmStoredBytes += entryBytes;
    }

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
        m_RawDevelopStageCacheBudgetBytes,
        cachedTexture);
    return true;
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
