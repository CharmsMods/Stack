#include "Renderer/RenderPipeline.h"
#include "Renderer/ScopedGLObjects.h"
#include "Renderer/ViewportTextureCopy.h"

RenderPipeline::CachedGraphTexture RenderPipeline::FindRawViewportDenoiseInput(
    const std::string& key, const Stack::RawRecipe::RawDevelopmentRecipe& recipe, bool& temporary) {
    temporary = false;
    if (recipe.rgbDenoise.diagnosticMode != Stack::RawRecipe::RawDenoiseDiagnosticMode::None)
        return FindRawDevelopStageCacheEntry(key,
            Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, m_PreviewMaxDimension).neutralPlacement);
    const auto nativeFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, 0).neutralPlacement;
    const auto requestedFingerprint = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe, m_PreviewMaxDimension).neutralPlacement;
    // Only the current upstream recipe owns a protected native dependency.
    // EV, curves, color and view are downstream of this fingerprint.
    if (auto found = m_RawDevelopStageImageCache.find(key); found != m_RawDevelopStageImageCache.end())
        for (auto& entry : found->second)
            if (entry.fingerprint != nativeFingerprint) entry.viewportNativeDependency = false;
    const auto native = FindRawDevelopStageCacheEntry(key, nativeFingerprint);
    auto requested = FindRawDevelopStageCacheEntry(key, requestedFingerprint);
    if (!native.texture || !native.viewportNativeDependency) return requested;
    ++m_LastGraphExecutionStats.rawNativeDenoiseReuses;
    if (m_PreviewMaxDimension == 0 || requested.viewportNativeDerived) return requested;
    // A preview produced before native preparation is replaced by a sample of
    // the completed native image. Changing resolution never runs denoise again.
    const double scale = std::min(1.0, double(m_PreviewMaxDimension) / std::max(native.width, native.height));
    const int width = std::max(1, int(std::lround(native.width * scale)));
    const int height = std::max(1, int(std::lround(native.height * scale)));
    Stack::Renderer::ScopedGLTexture copy(Stack::Renderer::CopyViewportTexture(
        native.texture, native.width, native.height, width, height));
    if (!copy) return {};
    m_Width = width;
    m_Height = height;
    InvalidateRawDevelopStageCacheEntry(key, requestedFingerprint);
    if (StoreRawDevelopStageCacheEntry(key, copy.Get(), requestedFingerprint, true)) {
        copy.Release();
        auto& entry = m_RawDevelopStageImageCache.at(key).front();
        entry.viewportNativeDerived = true;
        return entry;
    }
    requested = {};
    requested.texture = copy.Release();
    requested.width = width;
    requested.height = height;
    temporary = true;
    return requested;
}
