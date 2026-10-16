#include "FilmGrainPreview.h"
#include "FilmGrainShaders.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>
#include <stdexcept>

namespace Stack::Renderer::FilmGrain {
namespace {
struct PreviewPass {
    GLState::FramebufferState framebuffer{true};
    GLboolean blend=glIsEnabled(GL_BLEND),scissor=glIsEnabled(GL_SCISSOR_TEST),depth=glIsEnabled(GL_DEPTH_TEST);
    GLint program=0,vao=0,buffer=0,active=0,texture=0,sampler=0;
    PreviewPass(){
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&buffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
        glBindSampler(0,0);glDisable(GL_BLEND);glDisable(GL_SCISSOR_TEST);glDisable(GL_DEPTH_TEST);
    }
    ~PreviewPass(){
        framebuffer.Restore(true);glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(0,sampler);glActiveTexture(active);
        if(blend)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
        if(depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
    }
};
constexpr const char* Display=R"(
#version 330 core
in vec2 uv;out vec4 result;uniform sampler2D image;uniform bool scalar;
void main(){vec3 x=texture(image,uv).rgb;if(scalar)x=vec3(x.r);
    x=mix(x*12.92,1.055*pow(max(x,vec3(0)),vec3(1./2.4))-.055,step(vec3(.0031308),x));
    result=vec4(x,1);}
)";
}
ReferencePreview::~ReferencePreview(){
    for(auto texture:{m_Source,m_Output,m_DisplayTexture})if(texture)glDeleteTextures(1,&texture);
    for(auto fbo:{m_OutputFbo,m_DisplayFbo})if(fbo)glDeleteFramebuffers(1,&fbo);
    if(m_DisplayProgram)glDeleteProgram(m_DisplayProgram);
}
void ReferencePreview::Render(const Parameters& parameters,Input input,float tone){
    PreviewPass pass;
    constexpr int edge=128;
    if(!m_Initialized){
        m_Quad.Initialize();
        m_Output=GLHelpers::CreateTextureFromData(nullptr,edge,edge,GL_RGBA16F,GL_RGBA,GL_FLOAT,GL_LINEAR);
        m_DisplayTexture=GLHelpers::CreateTextureFromData(nullptr,edge,edge,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,GL_LINEAR);
        m_OutputFbo=m_Output?GLHelpers::CreateFBO(m_Output):0;
        m_DisplayFbo=m_DisplayTexture?GLHelpers::CreateFBO(m_DisplayTexture):0;
        m_DisplayProgram=GLHelpers::CreateShaderProgram(Shaders::Vertex,Display);
        m_Initialized=true;
    }
    if(!m_OutputFbo || !m_DisplayFbo || !m_DisplayProgram)throw std::runtime_error("Grain reference preview is unavailable.");
    tone=std::clamp(tone,0.f,1.f);
    const float gray[]{tone,tone,tone,1.f};
    if(!m_Source)m_Source=GLHelpers::CreateTextureFromData(gray,1,1,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_NEAREST);
    else {
        GLState::PixelUnpackState unpack;unpack.ConfigureTightCpuUpload();
        glBindTexture(GL_TEXTURE_2D,m_Source);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,1,1,GL_RGBA,GL_FLOAT,gray);unpack.Restore();
    }
    if(!m_Source)throw std::runtime_error("Grain reference source allocation failed.");
    if(input.nativeWidth<=0)input.nativeWidth=6000;
    if(input.nativeHeight<=0)input.nativeHeight=4000;
    input.sampleSpan={edge/8.f,edge/8.f};
    input.sampleOrigin={input.nativeWidth*.5f-input.sampleSpan[0]*.5f,input.nativeHeight*.5f-input.sampleSpan[1]*.5f};
    input.premultiplied=false;input.encodingHint=0;input.shouldCancel={};
    auto p=parameters;p.encoding=1; // The reference is explicitly linear, independent of the photograph's transfer.
    glBindFramebuffer(GL_FRAMEBUFFER,m_OutputFbo);glViewport(0,0,edge,edge);
    m_Renderer.Render(m_Source,edge,edge,m_Quad,p,input);
    glBindFramebuffer(GL_FRAMEBUFFER,m_DisplayFbo);glUseProgram(m_DisplayProgram);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,m_Output);glBindSampler(0,0);
    glUniform1i(glGetUniformLocation(m_DisplayProgram,"image"),0);
    glUniform1i(glGetUniformLocation(m_DisplayProgram,"scalar"),input.scalar);
    m_Quad.Draw();
    if(glGetError()!=GL_NO_ERROR)throw std::runtime_error("Grain reference display rendering failed.");
}
}
