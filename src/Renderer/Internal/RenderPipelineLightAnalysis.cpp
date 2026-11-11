#include "Renderer/RenderPipeline.h"
#include "Renderer/ScopedGLObjects.h"
#include <stdexcept>

void RenderPipeline::ClearLightAnalysis() {
    for(auto& entry:m_LightAnalysisCache) {
        if(entry.guide)glDeleteTextures(1,&entry.guide);
        entry={};
    }
    m_LightRangeAnalysis.Clear();
}

RenderPipeline::LightAnalysisEntry& RenderPipeline::PrepareLightAnalysis(unsigned int input,
    const Stack::RawRecipe::RawLocalRangeRecipe& recipe,Raw::RawWorkingSpace space,
    std::size_t fingerprint,int nativeWidth,int nativeHeight) {
    using namespace Stack::Renderer::RawDevelopmentCache;
    auto key=fingerprint;
    HashTypedValue(key,m_Width);HashTypedValue(key,m_Height);
    HashTypedValue(key,space);HashTypedValue(key,recipe.middleGrey);
    LightAnalysisEntry* result=nullptr;
    if(fingerprint)for(auto& entry:m_LightAnalysisCache)if(entry.key!=0 && entry.key==key){result=&entry;break;}
    if(!result) {
        auto& entry=m_LightAnalysisCache[m_LightAnalysisNext++%m_LightAnalysisCache.size()];
        if(entry.guide)glDeleteTextures(1,&entry.guide);
        entry={};
        if(m_ShouldCancelRender && m_ShouldCancelRender())throw std::runtime_error("Light analysis cancelled.");
        // Free retained image fields before the histogram/guide working set grows.
        for(auto& other:m_LightAnalysisCache)if(other.guide) {
            glDeleteTextures(1,&other.guide);other.guide=0;other.guideKey=0;other.guideBytes=0;
        }
        entry.range=m_LightRangeAnalysis.Measure(input,m_Width,m_Height,
            space==Raw::RawWorkingSpace::LinearRec2020D65,recipe.middleGrey);
        if(m_ShouldCancelRender && m_ShouldCancelRender())throw std::runtime_error("Light analysis cancelled.");
        entry.key=fingerprint?key:0;result=&entry;
    }
    auto guideKey=key;
    HashTypedValue(guideKey,recipe.smoothness);HashTypedValue(guideKey,recipe.edgeProtection);
    HashTypedValue(guideKey,nativeWidth);HashTypedValue(guideKey,nativeHeight);
    if(recipe.smoothness<=.0001f && result->guide) {
        glDeleteTextures(1,&result->guide);result->guide=0;result->guideKey=0;result->guideBytes=0;
    }
    if(recipe.smoothness>.0001f && (!result->guide || result->guideKey!=guideKey)) {
        // Retain only one full-resolution field; scalar fits remain cheap and
        // bounded across nodes. This avoids four native-size guide allocations.
        for(auto& entry:m_LightAnalysisCache)if(&entry!=result && entry.guide) {
            glDeleteTextures(1,&entry.guide);entry.guide=0;entry.guideKey=0;entry.guideBytes=0;
        }
        Stack::Renderer::ScopedGLTexture replacement(RenderRawSpatialField(input,
            16.f+240.f*recipe.smoothness,recipe.edgeProtection,space,nativeWidth,nativeHeight));
        if(!replacement)throw std::runtime_error("Light brightness guide unavailable.");
        if(result->guide)glDeleteTextures(1,&result->guide);
        result->guide=replacement.Release();result->guideKey=guideKey;
        result->guideBytes=std::uint64_t(m_Width)*m_Height*8;
    }
    return *result;
}
