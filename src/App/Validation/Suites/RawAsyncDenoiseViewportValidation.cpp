#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/RawRgbDenoiseIdentity.h"
#include "Renderer/ScopedGLObjects.h"
#include <iostream>

bool RenderPipeline::ValidateRawNativeDenoiseHandoffForTesting() {
    using namespace Stack::Renderer::RawDenoise;
    auto recipe = Stack::RawRecipe::MakeDefaultRecipe("async-native-validation");
    recipe.rgbDenoise.enabled = true;
    recipe.rgbDenoise.method = Stack::RawRecipe::RawRgbDenoiseMethod::RestormerRealV1;
    recipe.rgbDenoise.luminanceNoise = 0.5f;
    recipe.rgbDenoise = Stack::RawRecipe::SanitizeRgbDenoiseRecipe(recipe.rgbDenoise);
    const auto native = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,0);
    const auto proxy = Stack::Renderer::RawDevelopmentCache::BuildStageFingerprints(recipe,16);
    const auto model = RestormerNeutralFingerprint(native.rawBase,recipe.rgbDenoise);
    SetGraphCacheBudget(1024*1024);
    SetPreviewMaxDimension(16);
    SetRawRgbDenoiseAsyncEnabled(true);
    m_Width = m_Height = 16;
    unsigned int texture = 0;
    glGenTextures(1,&texture);
    Stack::Renderer::ScopedGLTexture input(texture);
    glBindTexture(GL_TEXTURE_2D,input.Get());
    std::vector<float> pixels(16*16*4,0.25f);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16F,16,16,0,GL_RGBA,GL_FLOAT,pixels.data());
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    const std::string key = "1:__rawDevelopmentRgbDenoise";
    const auto render = [&] {
        return RenderRawDevelopmentRgbDenoise(input.Get(),recipe.rgbDenoise,recipe.technical.workingSpace,
            proxy.rawBase,{},key,64,64,&recipe);
    };
    const auto firstOwner = std::make_shared<RawRgbDenoiseState>();
    const auto secondOwner = std::make_shared<RawRgbDenoiseState>();
    SetRawRgbDenoiseState(firstOwner, true);
    // Simulate a pending native job without installing or launching a model.
    firstOwner->pending = true;
    firstOwner->modelFingerprint = model;
    firstOwner->cancel = std::make_shared<std::atomic<bool>>(false);
    Stack::Renderer::ScopedGLTexture provisional(render());
    const bool preserved = provisional && !firstOwner->cancel->load();

    // A second project with the same recipe must queue independently without
    // cancelling the admitted owner or allocating an inference input.
    SetRawRgbDenoiseState(secondOwner, false);
    Stack::Renderer::ScopedGLTexture deferred(render());
    const bool independent = deferred && secondOwner->deferred &&
        !secondOwner->pending && !secondOwner->future.valid() &&
        !secondOwner->cancel && IsRawRgbDenoiseAsyncPending() &&
        firstOwner->pending && !firstOwner->cancel->load();
    SetRawRgbDenoiseState(secondOwner, false);
    const bool remainsQueued = secondOwner->deferred;
    const std::string deferredKey = "deferred-denoise-validation";
    const bool cachedBeforeAdmission =
        StoreRawDevelopStageCacheEntry(deferredKey, input.Get(), 1);
    SetRawRgbDenoiseState(secondOwner, true);
    const bool admissionClearedProvisional = cachedBeforeAdmission &&
        !secondOwner->deferred && !IsRawRgbDenoiseAsyncPending() &&
        FindRawDevelopStageCacheEntry(deferredKey, 1).texture == 0;
    SetRawRgbDenoiseState(secondOwner, false);
    Stack::Renderer::ScopedGLTexture canceledDeferred(render());
    const bool cachedBeforeCancellation =
        StoreRawDevelopStageCacheEntry(deferredKey, input.Get(), 1);
    secondOwner->CancelAndWait();
    SetRawRgbDenoiseState({}, true);
    const bool cancellationClearedProvisional = canceledDeferred &&
        cachedBeforeCancellation &&
        FindRawDevelopStageCacheEntry(deferredKey, 1).texture == 0;
    {
        RenderPipeline temporary;
        temporary.SetRawRgbDenoiseState(firstOwner, false);
        temporary.Shutdown();
    }
    const bool teardownIsolated = firstOwner->pending &&
        !firstOwner->cancel->load();
    SetRawRgbDenoiseState(firstOwner, true);

    const auto originalSource = recipe.source.sourcePath;
    recipe.source.sourcePath = "different-source";
    Stack::Renderer::ScopedGLTexture changed(render());
    const bool replaced = m_RawRgbDenoiseState->cancel->load();
    recipe.source.sourcePath = originalSource;
    m_RawRgbDenoiseState->pending = false;
    m_RawRgbDenoiseState->cancel.reset();
    m_RestormerAppliedCacheFingerprint = RestormerApplicationFingerprint(model,recipe.technical.workingSpace,recipe.rgbDenoise);
    m_RestormerAppliedCacheWidth = m_RestormerAppliedCacheHeight = 64;
    m_RestormerAppliedCacheRgba = std::make_shared<const std::vector<float>>(64*64*4,0.5f);
    Stack::Renderer::ScopedGLTexture completed(render());
    const auto dependency = FindRawDevelopStageCacheEntry(key,native.neutralPlacement);
    std::vector<float> output(16*16*4);
    if (completed) {
        glBindTexture(GL_TEXTURE_2D,completed.Get());
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,output.data());
    }
    const bool reused = completed && dependency.viewportNativeDependency && dependency.width == 64 &&
        dependency.height == 64 && m_Width == 16 && m_Height == 16 &&
        std::all_of(output.begin(),output.end(),[](float value) { return std::abs(value-0.5f) < 0.001f; });
    std::cout << "RAW async native handoff: preserve job " << preserved << ", upstream cancellation "
        << replaced << ", native result reused by proxy " << reused
        << ", independent owner " << independent << ", deferred retained " << remainsQueued
        << ", admission clears provisional " << admissionClearedProvisional
        << ", cancellation clears provisional " << cancellationClearedProvisional
        << ", teardown isolated " << teardownIsolated << "\n";
    SetRawRgbDenoiseState({}, true);
    return preserved && replaced && reused && independent && remainsQueued &&
        admissionClearedProvisional && cancellationClearedProvisional && teardownIsolated;
}
