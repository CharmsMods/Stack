#include "FilmGrainRenderer.h"
#include "FilmGrainShaders.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/FullscreenQuad.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include "Raw/RawDevelopmentRecipe.h"

namespace Stack::Renderer::FilmGrain {
namespace {
struct FieldPass {
    GLState::FramebufferState framebuffer{true};
    GLboolean blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST);
    GLint box[4]{};
    FieldPass(){glGetIntegerv(GL_SCISSOR_BOX,box);}
    ~FieldPass(){framebuffer.Restore(true);glScissor(box[0],box[1],box[2],box[3]);if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);}
};
struct Bindings {
    GLint active=0,textures[3]{},samplers[3]{};
    Bindings(){
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);
        for(int i=0;i<3;++i){
            glActiveTexture(GL_TEXTURE0+i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&textures[i]);
            glGetIntegerv(GL_SAMPLER_BINDING,&samplers[i]);
            glBindSampler(i,0);
        }
        glActiveTexture(GL_TEXTURE0);
    }
    ~Bindings(){
        for(int i=0;i<3;++i){
            glActiveTexture(GL_TEXTURE0+i);
            glBindTexture(GL_TEXTURE_2D,textures[i]);
            glBindSampler(i,samplers[i]);
        }
        glActiveTexture(active);
    }
};
}
Renderer::~Renderer(){ReleaseField();if(m_FieldProgram)glDeleteProgram(m_FieldProgram);
    if(m_CompositeProgram)glDeleteProgram(m_CompositeProgram);
    if(m_ResponseTexture)glDeleteTextures(1,&m_ResponseTexture);}
void Renderer::ReleaseField(){if(m_FieldFbo)glDeleteFramebuffers(1,&m_FieldFbo);
    if(m_FieldTexture)glDeleteTextures(1,&m_FieldTexture);m_FieldFbo=m_FieldTexture=0;m_Valid=false;}

void Renderer::PrepareResponseCurve(const Parameters& p) {
    if(m_ResponseTexture && SameResponseCurve(m_ResponseCurve,p.responseCurve))return;
    const auto segments=p.responseCurve.size()-1;
    if(segments>size_t(std::numeric_limits<int>::max()/2))throw std::runtime_error("Film grain curve exceeds GPU addressable storage.");
    const int texels=int(segments)*2,width=std::min(256,texels),height=int((size_t(texels)+width-1)/width);
    GLint maximumEdge=0;glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximumEdge);
    if(width>maximumEdge || height>maximumEdge)throw std::runtime_error("Film grain curve exceeds this GPU's texture storage capacity.");
    std::vector<float> pixels(size_t(width)*height*4,0.f);
    const auto prepared=RawRecipe::BuildRawBezierSegments(p.responseCurve);
    for(size_t i=0;i<segments;++i) {
        const auto& s=prepared[i];
        const std::array<float,8> values{s.left.x,s.left.y,s.right.x,s.right.y,
            s.leftHandleX,s.leftHandleY,s.rightHandleX,s.rightHandleY};
        std::copy(values.begin(),values.end(),pixels.begin()+i*8);
    }
    const auto texture=GLHelpers::CreateTextureFromData(pixels.data(),width,height,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_NEAREST);
    if(!texture)throw std::runtime_error("Film grain response-curve allocation failed.");
    if(m_ResponseTexture)glDeleteTextures(1,&m_ResponseTexture);
    m_ResponseTexture=texture;m_ResponseSegments=int(segments);m_ResponseCurve=p.responseCurve;
}

void Renderer::Render(unsigned int inputTexture,int width,int height,FullscreenQuad& quad,
    const Parameters& parameters,const Input& input){
    if(!inputTexture || width<=0 || height<=0)throw std::runtime_error("Film grain received an invalid image.");
    Parameters p=parameters;p.Sanitize();
    if(p.encoding==0 && input.encodingHint==-2 && p.densityDeviation>0.f && p.mix>0.f && !input.scalar)
        throw std::runtime_error("Film grain needs linear or sRGB input. Convert the declared transfer or select an explicit input encoding.");
    const int encoding=input.scalar ? 0 : p.encoding==0 ? std::max(0,input.encodingHint) : p.encoding-1;
    Bindings bindings;
    if(!m_FieldProgram)m_FieldProgram=GLHelpers::CreateShaderProgram(Shaders::Vertex,Shaders::Field);
    if(!m_CompositeProgram)m_CompositeProgram=GLHelpers::CreateShaderProgram(Shaders::Vertex,Shaders::Composite);
    if(!m_FieldProgram || !m_CompositeProgram)throw std::runtime_error("Film grain shader compilation failed.");
    const int nativeWidth=input.nativeWidth>0 ? input.nativeWidth : width;
    const int nativeHeight=input.nativeHeight>0 ? input.nativeHeight : height;
    const float pixelsPerMicron=float(nativeWidth)/(p.filmWidthMm*1000.f);
    const float radius=p.radiusMicrons*pixelsPerMicron,blur=p.opticalBlurMicrons*pixelsPerMicron;
    const float moment=float(ClippedRadiusSecondMoment(radius,p.radiusVariation,p.maximumRadiusFactor));
    const float spanX=input.sampleSpan[0]>0.f ? input.sampleSpan[0] : float(nativeWidth);
    const float spanY=input.sampleSpan[1]>0.f ? input.sampleSpan[1] : float(nativeHeight);
    const float footprintX=spanX/width,footprintY=spanY/height;
    const std::array<float,17> key{p.radiusMicrons,p.radiusVariation,p.maximumRadiusFactor,p.filmWidthMm,
        p.opticalBlurMicrons,p.coverage,p.channelSize[0],p.channelSize[1],p.channelSize[2],
        float(p.samples),float(nativeWidth),float(nativeHeight),float(input.scalar),
        input.sampleOrigin[0],input.sampleOrigin[1],spanX,spanY};
    if(p.densityDeviation>0.f && p.mix>0.f && (!m_Valid || key!=m_FieldKey || p.seed!=m_Seed || p.frame!=m_Frame ||
        width!=m_Width || height!=m_Height)){
        FieldPass pass;glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);
        if(!m_FieldTexture || width!=m_Width || height!=m_Height){
            ReleaseField();
            m_FieldTexture=GLHelpers::CreateTextureFromData(nullptr,width,height,GL_RGBA16F,GL_RGBA,GL_FLOAT,GL_LINEAR);
            m_FieldFbo=m_FieldTexture ? GLHelpers::CreateFBO(m_FieldTexture) : 0;
            if(!m_FieldFbo){ReleaseField();throw std::runtime_error("Film grain field allocation failed.");}
            m_Width=width;m_Height=height;
        }
        m_Valid=false;
        glBindFramebuffer(GL_FRAMEBUFFER,m_FieldFbo);glViewport(0,0,width,height);glUseProgram(m_FieldProgram);
        const auto loc=[&](const char* name){return glGetUniformLocation(m_FieldProgram,name);};
        glUniform2f(loc("nativeSize"),float(nativeWidth),float(nativeHeight));
        glUniform2f(loc("sampleOrigin"),input.sampleOrigin[0],input.sampleOrigin[1]);
        glUniform2f(loc("sampleSpan"),spanX,spanY);
        glUniform2f(loc("footprint"),footprintX,footprintY);
        glUniform1f(loc("radius"),radius);glUniform1f(loc("variation"),p.radiusVariation);
        glUniform1f(loc("maximumFactor"),p.maximumRadiusFactor);glUniform1f(loc("coverage"),p.coverage);
        glUniform1f(loc("opticalBlur"),blur);glUniform1f(loc("secondMoment"),moment);
        glUniform3fv(loc("channelSize"),1,p.channelSize.data());
        glUniform1i(loc("seed"),p.seed);glUniform1i(loc("frameIndex"),p.frame);
        glUniform1i(loc("samples"),p.samples);
        glUniform1i(loc("scalarField"),input.scalar);
        // Keep individual GPU commands bounded on Windows. Scissoring does
        // not restart UVs, particle coordinates or the optical sample sequence.
        glEnable(GL_SCISSOR_TEST);
        for(int y=0;y<height;y+=64)for(int x=0;x<width;x+=256){
            if(input.shouldCancel && input.shouldCancel())throw std::runtime_error("Render superseded by a newer snapshot.");
            glScissor(x,y,std::min(256,width-x),std::min(64,height-y));quad.Draw();
        }
        if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Film grain field rendering failed.");
        m_FieldKey=key;m_Seed=p.seed;m_Frame=p.frame;m_Valid=true;
    }
    PrepareResponseCurve(p);
    glUseProgram(m_CompositeProgram);
    const auto loc=[&](const char* name){return glGetUniformLocation(m_CompositeProgram,name);};
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,m_FieldTexture);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,m_ResponseTexture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,inputTexture);
    glUniform1i(loc("image"),0);glUniform1i(loc("grain"),1);
    glUniform1i(loc("responseCurve"),2);glUniform1i(loc("responseSegments"),m_ResponseSegments);
    const bool curveActive=std::any_of(p.responseCurve.begin(),p.responseCurve.end(),[](const auto& point){
        return point.y!=1.f/3.f || (point.incoming.manual && point.incoming.offsetY!=0.f) ||
            (point.outgoing.manual && point.outgoing.offsetY!=0.f);
    });
    glUniform1i(loc("responseActive"),curveActive);
    glUniform1f(loc("deviation"),p.densityDeviation);glUniform1f(loc("amount"),p.mix);
    glUniform1f(loc("colorAmount"),p.colorAmount);glUniform1f(loc("correlation"),p.channelCorrelation);
    glUniform3fv(loc("channelStrength"),1,p.channelStrength.data());glUniform3fv(loc("components"),1,input.components.data());
    glUniform3fv(loc("luma"),1,input.luma.data());
    std::array<float,4> variance{};
    for(int i=0;i<4;++i){const float size=i==0 ? 1.f : p.channelSize[i-1];
        const float area=4.f*3.14159265359f*moment*size*size;
        variance[i]=area/(footprintX*footprintY+12.f*blur*blur+area);}
    glUniform4f(loc("fieldVariance"),variance[0],variance[1],variance[2],variance[3]);
    glUniform1f(loc("shadows"),p.shadows);glUniform1f(loc("midtones"),p.midtones);glUniform1f(loc("highlights"),p.highlights);
    glUniform1f(loc("pivot"),p.tonalPivot);glUniform1f(loc("exposureFloor"),p.exposureFloor);
    glUniform1i(loc("encoding"),encoding);glUniform1i(loc("outputMode"),p.output);
    glUniform1i(loc("scalar"),input.scalar);glUniform1i(loc("premultiplied"),input.premultiplied);
    glUniform1i(loc("compensateMean"),p.compensateMean);
    quad.Draw();glUseProgram(0);
}
}
