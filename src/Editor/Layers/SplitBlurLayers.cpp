#include "SplitBlurLayers.h"

#include "Renderer/FullscreenQuad.h"
#include "Renderer/GLStateGuards.h"
#include "Renderer/ScopedGLObjects.h"
#include "Utils/ImGuiExtras.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

const char* kBlurVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kBlurFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uOriginal;
uniform sampler2D uKernel;
uniform int uRadius;
uniform int uVertical;
uniform int uScalar;
uniform int uPaired;
uniform float uKernelSum;

void main() {
    ivec2 size = textureSize(uInputTex, 0);
    ivec2 p = clamp(ivec2(vUV * vec2(size)), ivec2(0), size - 1);
    ivec2 axis = uVertical != 0 ? ivec2(0,1) : ivec2(1,0);
    vec4 sum = vec4(0);
    if (uPaired != 0) {
        sum = texelFetch(uInputTex,p,0);
        for (int i=1; i<=uRadius; i+=2) {
            float a=texelFetch(uKernel,ivec2(i,0),0).r;
            float b=i<uRadius ? texelFetch(uKernel,ivec2(i+1,0),0).r : 0.0;
            float weight=a+b;
            if (weight<=0.0) continue;
            vec2 offset=vec2(axis)*(float(i)+b/weight);
            vec2 center=vec2(p)+vec2(0.5);
            sum += (texture(uInputTex,(center+offset)/vec2(size))+
                    texture(uInputTex,(center-offset)/vec2(size)))*weight;
        }
    } else {
        for (int i = -uRadius; i <= uRadius; ++i) {
            float weight = texelFetch(uKernel, ivec2(abs(i),0), 0).r;
            vec4 value = texelFetch(uInputTex, clamp(p + axis*i, ivec2(0), size-1), 0);
            // Premultiplication must precede filtering for transparent color.
            if (uVertical == 0 && uScalar == 0) value = vec4(value.rgb * value.a, value.a);
            sum += value * weight;
        }
    }
    if (uVertical == 0) FragColor = sum;
    else if (uScalar != 0) FragColor = vec4(vec3(sum.r / (uKernelSum*uKernelSum)), 1);
    else FragColor = vec4(sum.rgb / max(sum.a, 0.000001), texelFetch(uOriginal,p,0).a);
}
)";

} // namespace

SplitBlurLayerBase::~SplitBlurLayerBase() {
    if (m_KernelTexture) glDeleteTextures(1, &m_KernelTexture);
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SplitBlurLayerBase::InitializeGL() {
    if (!m_ShaderProgram) m_ShaderProgram = GLHelpers::CreateShaderProgram(kBlurVert, kBlurFrag);
}

void SplitBlurLayerBase::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    if (!m_ShaderProgram || !inputTexture || width <= 0 || height <= 0)
        throw std::runtime_error("Blur resources are unavailable.");
    const float amount = std::isfinite(m_Amount) ? std::max(.0001f,m_Amount) : 2.f;
    GLint maximum = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&maximum);
    if (amount >= maximum) throw std::runtime_error("Blur radius exceeds the supported kernel size.");
    if (!m_KernelTexture || m_KernelAmount != amount) {
        const int radius = static_cast<int>(std::max(1.f,amount));
        std::vector<float> weights(radius+1,1.f);
        double sum = 1;
        for(int i=1;i<=radius;++i) {
            if (GetBlurType()==1) weights[i]=float(std::exp(-double(i)*i/(.5*double(amount)*amount)));
            sum += 2*weights[i];
        }
        const auto kernel=GLHelpers::CreateTextureFromData(weights.data(),radius+1,1,GL_R32F,GL_RED,GL_FLOAT);
        if(!kernel) throw std::runtime_error("Could not allocate the blur kernel.");
        if(m_KernelTexture) glDeleteTextures(1,&m_KernelTexture);
        m_KernelTexture=kernel;m_KernelAmount=amount;m_KernelRadius=radius;m_KernelSum=float(sum);
    }
    const Stack::Renderer::GLState::FramebufferState destination(true);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,inputTexture);
    GLint redBits=0,greenBits=0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_RED_SIZE,&redBits);
    glGetTexLevelParameteriv(GL_TEXTURE_2D,0,GL_TEXTURE_GREEN_SIZE,&greenBits);
    const bool scalar=redBits>0 && greenBits==0;
    GLint minFilter=0,magFilter=0,wrapS=0,wrapT=0;
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,&minFilter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,&magFilter);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,&wrapS);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,&wrapT);
    const bool pairInput=scalar && minFilter==GL_LINEAR && magFilter==GL_LINEAR &&
        wrapS==GL_CLAMP_TO_EDGE && wrapT==GL_CLAMP_TO_EDGE;
    Stack::Renderer::ScopedGLTexture intermediate(GLHelpers::CreateStorageTexture(width,height,scalar?GL_R32F:GL_RGBA32F));
    const auto fbo=intermediate ? GLHelpers::CreateFBO(intermediate.Get()) : 0;
    if (!fbo) {destination.Restore(true);throw std::runtime_error("Could not allocate the blur intermediate.");}
    glUseProgram(m_ShaderProgram);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uOriginal"), 1);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uKernel"), 2);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uRadius"), m_KernelRadius);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uScalar"), scalar);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uKernelSum"), m_KernelSum);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,inputTexture);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,m_KernelTexture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,inputTexture);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,width,height);
    glUniform1i(glGetUniformLocation(m_ShaderProgram,"uVertical"),0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram,"uPaired"),pairInput);
    quad.Draw();
    destination.Restore(true);
    glBindTexture(GL_TEXTURE_2D,intermediate.Get());
    glUniform1i(glGetUniformLocation(m_ShaderProgram,"uVertical"),1);
    glUniform1i(glGetUniformLocation(m_ShaderProgram,"uPaired"),1);
    quad.Draw();
    glUseProgram(0);
    glDeleteFramebuffers(1,&fbo);
}

void SplitBlurLayerBase::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Blur Radius / Amount", "##BlurAmount", &m_Amount, 0.5f, 16.0f, "%.1f");
}

json SplitBlurLayerBase::Serialize() const {
    json j;
    j["type"] = GetTypeId();
    j["amount"] = m_Amount;
    return j;
}

void SplitBlurLayerBase::Deserialize(const json& j) {
    if (j.contains("amount")) {
        m_Amount = j["amount"];
    }
    if (j.contains("blurRadius")) {
        m_Amount = j["blurRadius"];
    }
}
