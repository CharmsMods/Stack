#include "Renderer/ViewportTextureCopy.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/RenderPipeline.h"

void RenderPipeline::PrepareRawCalibrationPass(std::optional<Raw::ViewportStage> changingStage) {
    // Keep decoded sources, shader programs and source uploads. Re-evaluate
    // processing instead of timing lookups of identical completed images.
    InvalidateGraphCaches(false);
    // A curve edit rebuilds its LUT; an EV edit reuses it. Benchmark the
    // preparation work belonging to the actual edited module only.
    for (auto& [key, cached] : m_RawDevelopmentRecipeLayerCache) {
        if (!changingStage) continue;
        const auto* type = Raw::kViewportModules[static_cast<std::size_t>(*changingStage)].reconfiguredLayerType;
        if (type && cached.type == type) cached.appliedPayload = nullptr;
    }
    for (auto& [id, raw] : m_RawPipelines) {
        (void)id;
        raw.InvalidateProcessingOutputs();
    }
}

std::array<std::size_t, Raw::kViewportStageCount> RenderPipeline::RawViewportCachedStages(
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe, int edge) const {
    std::array<std::size_t, Raw::kViewportStageCount> result {};
    const auto expected = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, edge);
    for (const auto& [key, entries] : m_RawDevelopStageImageCache) {
        for (const auto& entry : entries) if (entry.texture && entry.width > 0 && entry.height > 0) {
            for (std::size_t i = 0; i < result.size(); ++i)
                if (entry.fingerprint == expected.For(static_cast<Raw::ViewportStage>(i))) result[i] = entry.fingerprint;
        }
    }
    return result;
}

bool RenderPipeline::SeedViewportDependency(const RenderPipeline& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe, int edge, int inputEdge) {
    // Adopt source precision before storing the copy. Switching on the first
    // graph execution would otherwise discard the dependency just installed.
    if (source.m_GraphFloat32Targets) EnsureGraphFloat32Targets();
    const auto expected = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, edge).neutralPlacement;
    const auto sourceFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,inputEdge < 0 ? edge : inputEdge).neutralPlacement;
    for (const auto& [key, entries] : source.m_RawDevelopStageImageCache) {
        if (key.find(":__rawDevelopmentRgbDenoise") == std::string::npos) continue;
        for (const auto& entry : entries) {
            if (entry.fingerprint != sourceFingerprint || !entry.texture || entry.width <= 0 || entry.height <= 0) continue;
            m_Width = entry.width;
            m_Height = entry.height;
            // Copy into the private owner's budget. No speculative result is
            // installed in the foreground cache and no external job is started.
            if (inputEdge >= 0 && inputEdge != edge) {
                const double scale = edge > 0 ? std::min(1.0,double(edge)/std::max(entry.width,entry.height)) : 1.0;
                m_Width = std::max(1,int(std::lround(entry.width*scale)));
                m_Height = std::max(1,int(std::lround(entry.height*scale)));
                Stack::Renderer::ScopedGLTexture copy(Stack::Renderer::CopyViewportTexture(
                    entry.texture,entry.width,entry.height,m_Width,m_Height));
                if (!copy || !StoreRawDevelopStageCacheEntry(key,copy.Get(),expected,true)) return false;
                copy.Release();
                return true;
            }
            return StoreRawDevelopStageCacheEntry(key, entry.texture, expected);
        }
    }
    return false;
}
