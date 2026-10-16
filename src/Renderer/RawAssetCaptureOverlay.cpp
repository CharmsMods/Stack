#include "Renderer/RawAssetCaptureOverlay.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"

namespace Stack::Renderer {
namespace {
constexpr const char* Vertex=R"GLSL(
#version 330 core
out vec2 vUV;
void main() {
    vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);
    vUV=p;gl_Position=vec4(p*2.0-1.0,0,1);
}
)GLSL";
constexpr const char* Fragment=R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 result;
uniform sampler2D uValues;
uniform vec3 uInnerMinimum,uInnerMaximum,uOuterMinimum,uOuterMaximum;
uniform int uChannels;
void main() {
    // CPU values use top-down rows. Output keeps standard FBO orientation.
    vec3 values=texture(uValues,vec2(vUV.x,1.0-vUV.y)).rgb;
    float coverage=1.0;
    for (int c=0;c<3;++c) {
        if ((c==0 && uChannels==1) || (c>0 && uChannels==0)) continue;
        if (isnan(values[c]) || isinf(values[c])) {coverage=0.0;break;}
        float channel=1.0;
        if (values[c]<uInnerMinimum[c]) {
            float width=uInnerMinimum[c]-uOuterMinimum[c];
            float t=width>0.0?clamp((values[c]-uOuterMinimum[c])/width,0.0,1.0):0.0;
            channel=t*t*(3.0-2.0*t);
        } else if (values[c]>uInnerMaximum[c]) {
            float width=uOuterMaximum[c]-uInnerMaximum[c];
            float t=width>0.0?clamp((uOuterMaximum[c]-values[c])/width,0.0,1.0):0.0;
            channel=t*t*(3.0-2.0*t);
        }
        coverage=min(coverage,channel);
    }
    result=vec4(vec3(coverage),1.0);
}
)GLSL";
struct RasterState {
    GLState::FramebufferState framebuffer{true};
    GLint program=0,vao=0,activeTexture=0,texture=0,sampler=0;
    GLboolean colorMask[4]{};
    const GLenum capabilities[7]{GL_BLEND,GL_SCISSOR_TEST,GL_DEPTH_TEST,GL_CULL_FACE,GL_STENCIL_TEST,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD};
    bool enabled[7]{};
    RasterState() {
        glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
        glGetIntegerv(GL_ACTIVE_TEXTURE,&activeTexture);glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D,&texture);glGetIntegerv(GL_SAMPLER_BINDING,&sampler);
        glGetBooleanv(GL_COLOR_WRITEMASK,colorMask);
        for (int i=0;i<7;++i) {enabled[i]=glIsEnabled(capabilities[i])==GL_TRUE;glDisable(capabilities[i]);}
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glBindSampler(0,0);
    }
    ~RasterState() {
        glUseProgram(program);glBindVertexArray(vao);
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture);glBindSampler(0,sampler);glActiveTexture(activeTexture);
        glColorMask(colorMask[0],colorMask[1],colorMask[2],colorMask[3]);
        for (int i=0;i<7;++i) if (enabled[i]) glEnable(capabilities[i]);else glDisable(capabilities[i]);
        framebuffer.Restore(true);
    }
};
} // namespace
void RawAssetCaptureOverlay::ClearInput() {
    if (m_Fbo) glDeleteFramebuffers(1,&m_Fbo);
    if (m_Source) glDeleteTextures(1,&m_Source);
    if (m_Output) glDeleteTextures(1,&m_Output);
    m_Source=m_Output=m_Fbo=0;m_Input.reset();
}
void RawAssetCaptureOverlay::Shutdown() {
    ClearInput();
    if (m_Program) glDeleteProgram(m_Program);
    if (m_Vao) glDeleteVertexArrays(1,&m_Vao);
    m_Program=m_Vao=0;
}
bool RawAssetCaptureOverlay::Render(const std::shared_ptr<const RawAssetCapture::PreviewField>& input,
    const RawAssetCapture::Qualifier& qualifier,std::string& error) {
    if (!input || input->width<=0 || input->height<=0 ||
        input->values.size()!=std::size_t(input->width)*input->height*3) {error="Capture feedback has no image values.";return false;}
    const RasterState saved;
    if (!m_Program) m_Program=GLHelpers::CreateShaderProgram(Vertex,Fragment,&error);
    if (!m_Program) return false;
    if (!m_Vao) glGenVertexArrays(1,&m_Vao);
    if (!m_Vao) {error="Could not prepare capture feedback.";return false;}
    if (input!=m_Input) {
        ClearInput();
        m_Source=GLHelpers::CreateTextureFromData(input->values.data(),input->width,input->height,GL_RGB32F,GL_RGB,GL_FLOAT);
        m_Output=GLHelpers::CreateTextureFromData(nullptr,input->width,input->height,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,GL_LINEAR);
        if (m_Output) {
            // A transformed input may not cover the entire displayed image.
            // Do not stretch its last coverage pixel into unavailable regions.
            glBindTexture(GL_TEXTURE_2D,m_Output);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_BORDER);
            const GLfloat border[4]{0,0,0,1};glTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_BORDER_COLOR,border);
        }
        if (m_Output) m_Fbo=GLHelpers::CreateFBO(m_Output);
        if (!m_Source || !m_Output || !m_Fbo) {ClearInput();error="Could not allocate capture feedback.";return false;}
        m_Input=input;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,m_Fbo);glViewport(0,0,input->width,input->height);
    glUseProgram(m_Program);glBindTexture(GL_TEXTURE_2D,m_Source);
    glUniform1i(glGetUniformLocation(m_Program,"uValues"),0);
    glUniform1i(glGetUniformLocation(m_Program,"uChannels"),static_cast<int>(qualifier.channels));
    glUniform3fv(glGetUniformLocation(m_Program,"uInnerMinimum"),1,qualifier.innerMinimum.data());
    glUniform3fv(glGetUniformLocation(m_Program,"uInnerMaximum"),1,qualifier.innerMaximum.data());
    glUniform3fv(glGetUniformLocation(m_Program,"uOuterMinimum"),1,qualifier.outerMinimum.data());
    glUniform3fv(glGetUniformLocation(m_Program,"uOuterMaximum"),1,qualifier.outerMaximum.data());
    glBindVertexArray(m_Vao);glDrawArrays(GL_TRIANGLES,0,3);
    if (glGetError()!=GL_NO_ERROR) {error="Could not draw capture feedback.";return false;}
    error.clear();return true;
}
} // namespace Stack::Renderer
