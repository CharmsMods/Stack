#include "Renderer/RenderPipeline.h"
#include "Renderer/Internal/SceneDetailShaders.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include <algorithm>
#include <cmath>

void RenderPipeline::EnsureRawSpatialPrograms() {
    namespace S=Stack::Renderer::SceneDetailShaders;
    if(!m_RawSpatialInitProgram)m_RawSpatialInitProgram=GLHelpers::CreateShaderProgram(S::Vertex,S::Initialize);
    if(!m_RawSpatialFilterProgram)m_RawSpatialFilterProgram=GLHelpers::CreateShaderProgram(S::Vertex,S::Filter);
    if(!m_RawSpatialApplyProgram)m_RawSpatialApplyProgram=GLHelpers::CreateShaderProgram(S::Vertex,S::Apply);
}

unsigned int RenderPipeline::RenderRawSpatialField(unsigned int input,float maximumScale,float edge,
    Raw::RawWorkingSpace space,int sourceWidth,int sourceHeight,const Stack::RawRecipe::DetailContrast* detail) {
    using namespace Stack::Renderer;
    using namespace Stack::RawRecipe;
    EnsureRawSpatialPrograms();
    if(!input||!m_RawSpatialInitProgram||!m_RawSpatialFilterProgram)return 0;
    // Three RG32F fields hold the base and accumulated EV residual. No image
    // readback or cross-stage reuse of denoise coefficients is involved.
    ScopedGLTexture current(GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RG32F));
    ScopedGLTexture horizontal(GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RG32F));
    ScopedGLTexture next(GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RG32F));
    ScopedGLTexture gains;
    if(!current||!horizontal||!next)return 0;
    const auto setLuma=[&](GLuint program){
        if(space==Raw::RawWorkingSpace::LinearRec2020D65)glUniform3f(glGetUniformLocation(program,"uLuma"),.2627002f,.6779981f,.0593017f);
        else glUniform3f(glGetUniformLocation(program,"uLuma"),.2126729f,.7151522f,.0721750f);
    };
    const auto bind=[](GLuint program,const char* name,GLuint texture,int unit){
        glActiveTexture(GL_TEXTURE0+unit);glBindTexture(GL_TEXTURE_2D,texture);glUniform1i(glGetUniformLocation(program,name),unit);
    };
    if(!RenderIntoGraphTargetTexture(current.Get(),[&](unsigned int){
        glUseProgram(m_RawSpatialInitProgram);bind(m_RawSpatialInitProgram,"uOriginal",input,0);setLuma(m_RawSpatialInitProgram);m_Quad.Draw();
    }))return 0;
    DetailContrast settings=detail?SanitizeDetailContrast(*detail):DetailContrast{};
    settings.maximumScale=std::clamp(maximumScale,2.f,1024.f);
    if(detail){
        gains.Reset(GLHelpers::CreateStorageTexture(kDetailBands,kDetailEvSamples,GL_R32F));
        if(!gains)return 0;
        const auto map=BuildDetailGainMap(settings);
        const GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
        const GLState::PixelUnpackState unpack;unpack.ConfigureTightCpuUpload();
        glBindTexture(GL_TEXTURE_2D,gains.Get());
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,kDetailBands,kDetailEvSamples,GL_RED,GL_FLOAT,map.data());
        unpack.Restore(); binding.Restore();
    }
    const float scaleX=float(m_Width)/std::max(1,sourceWidth>0?sourceWidth:m_Width);
    const float scaleY=float(m_Height)/std::max(1,sourceHeight>0?sourceHeight:m_Height);
    for(int band=0;band<kDetailBands;++band){
        if(m_ShouldCancelRender && m_ShouldCancelRender()) return 0;
        const float gap=DetailBandScale(settings,band)*.5f;
        // A proxy cannot represent bands finer than its sampling interval.
        if(gap*std::max(scaleX,scaleY)<.5f)continue;
        for(int direction=0;direction<2;++direction){
            const auto target=direction?next.Get():horizontal.Get();
            const auto source=direction?horizontal.Get():current.Get();
            if(!RenderIntoGraphTargetTexture(target,[&](unsigned int){
                const auto p=m_RawSpatialFilterProgram;glUseProgram(p);setLuma(p);
                bind(p,"uOriginal",input,0);bind(p,"uField",source,1);bind(p,"uPrevious",current.Get(),2);bind(p,"uGains",gains.Get(),3);
                glUniform2f(glGetUniformLocation(p,"uStep"),direction?0:gap*scaleX/m_Width,direction?gap*scaleY/m_Height:0);
                glUniform1f(glGetUniformLocation(p,"uEdge"),edge);
                glUniform1i(glGetUniformLocation(p,"uVertical"),direction);
                glUniform1i(glGetUniformLocation(p,"uDetail"),detail?1:0);
                glUniform1i(glGetUniformLocation(p,"uBand"),band);
                glUniform1i(glGetUniformLocation(p,"uTargetEnabled"),settings.targetEnabled?1:0);
                glUniform3f(glGetUniformLocation(p,"uTarget"),settings.targetEv,settings.targetHalfWidthEv,settings.targetFeatherEv);
                m_Quad.Draw();
            }))return 0;
        }
        std::swap(current,next);
    }
    glUseProgram(0);glActiveTexture(GL_TEXTURE0);
    return current.Release();
}

unsigned int RenderPipeline::RenderRawSceneDetail(unsigned int input,const Stack::RawRecipe::DetailContrast& value,
    Raw::RawWorkingSpace space,int sourceWidth,int sourceHeight) {
    using namespace Stack::Renderer;
    const auto settings=Stack::RawRecipe::SanitizeDetailContrast(value);
    if(!Stack::RawRecipe::IsDetailContrastActive(settings))return 0;
    ScopedGLTexture field(RenderRawSpatialField(input,settings.maximumScale,settings.edgeProtection,space,sourceWidth,sourceHeight,&settings));
    ScopedGLTexture result(GLHelpers::CreateStorageTexture(m_Width,m_Height,GL_RGBA32F));
    if(!field||!result||!m_RawSpatialApplyProgram)return 0;
    if(!RenderIntoGraphTargetTexture(result.Get(),[&](unsigned int){
        const auto p=m_RawSpatialApplyProgram;glUseProgram(p);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input);glUniform1i(glGetUniformLocation(p,"uOriginal"),0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,field.Get());glUniform1i(glGetUniformLocation(p,"uField"),1);
        if(space==Raw::RawWorkingSpace::LinearRec2020D65)glUniform3f(glGetUniformLocation(p,"uLuma"),.2627002f,.6779981f,.0593017f);
        else glUniform3f(glGetUniformLocation(p,"uLuma"),.2126729f,.7151522f,.0721750f);
        m_Quad.Draw();
    }))return 0;
    glUseProgram(0);glActiveTexture(GL_TEXTURE0);
    return result.Release();
}
