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
    for (auto& kernel : m_Kernels) if (kernel.texture) glDeleteTextures(1, &kernel.texture);
    if (m_ScratchFramebuffer) glDeleteFramebuffers(1, &m_ScratchFramebuffer);
    if (m_ScratchTexture) glDeleteTextures(1, &m_ScratchTexture);
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SplitBlurLayerBase::InitializeGL() {
    if (m_ShaderProgram) return;
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kBlurVert, kBlurFrag);
    if (!m_ShaderProgram) return;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_MaximumTextureSize);
    const char* names[] = {"uInputTex", "uOriginal", "uKernel", "uRadius", "uVertical",
        "uScalar", "uPaired", "uKernelSum"};
    for (std::size_t i = 0; i < std::size(names); ++i)
        m_UniformLocations[i] = glGetUniformLocation(m_ShaderProgram, names[i]);
}

void SplitBlurLayerBase::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    if (!m_ShaderProgram || !inputTexture || width <= 0 || height <= 0)
        throw std::runtime_error("Blur resources are unavailable.");
    if (width > m_MaximumTextureSize || height > m_MaximumTextureSize)
        throw std::runtime_error("Blur dimensions exceed the supported texture size.");
    const float amount = std::isfinite(m_Amount) ? std::max(0.f,m_Amount) : 2.f;
    if ((m_SigmaMode ? std::ceil(3.0 * amount) : std::max(1.0f,amount)) >= m_MaximumTextureSize)
        throw std::runtime_error("Blur radius exceeds the supported kernel size.");
    auto found = std::find_if(m_Kernels.begin(), m_Kernels.end(), [&](const Kernel& entry) {
        return entry.texture && entry.amount == amount && entry.sigmaMode == m_SigmaMode;
    });
    if (found == m_Kernels.end()) {
        const int radius = m_SigmaMode ? static_cast<int>(std::ceil(3.f*amount)) : static_cast<int>(std::max(1.f,amount));
        std::vector<float> weights(radius+1,1.f);
        double sum = 1;
        for(int i=1;i<=radius;++i) {
            if (GetBlurType()==1) weights[i]=float(std::exp(-double(i)*i/((m_SigmaMode ? 2. : .5)*double(amount)*amount)));
            sum += 2*weights[i];
        }
        const auto kernel=GLHelpers::CreateTextureFromData(weights.data(),radius+1,1,GL_R32F,GL_RED,GL_FLOAT);
        if(!kernel) throw std::runtime_error("Could not allocate the blur kernel.");
        found = std::min_element(m_Kernels.begin(), m_Kernels.end(), [](const Kernel& a,const Kernel& b) {
            return a.lastUse < b.lastUse;
        });
        if (found->texture) glDeleteTextures(1,&found->texture);
        *found = {kernel,amount,m_SigmaMode,radius,float(sum),0};
        ++m_ResourceStats.kernelUploads;
    }
    found->lastUse = ++m_KernelUseSerial;
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
    const auto scratchBytes = std::uint64_t(width) * std::uint64_t(height) * (scalar ? 4u : 16u);
    constexpr std::uint64_t retainedScratchLimit = 64ull * 1024 * 1024;
    const bool retainScratch = scratchBytes <= retainedScratchLimit;
    if (m_ScratchTexture && (!retainScratch || m_ScratchWidth != width || m_ScratchHeight != height || m_ScratchScalar != scalar)) {
        glDeleteFramebuffers(1,&m_ScratchFramebuffer);m_ScratchFramebuffer=0;
        glDeleteTextures(1,&m_ScratchTexture);m_ScratchTexture=0;
        m_ResourceStats.retainedScratchBytes=0;
    }
    Stack::Renderer::ScopedGLTexture temporary;
    unsigned int intermediate=m_ScratchTexture, fbo=m_ScratchFramebuffer;
    if (!intermediate) {
        temporary.Reset(GLHelpers::CreateStorageTexture(width,height,scalar?GL_R32F:GL_RGBA32F));
        intermediate=temporary.Get();
        fbo=intermediate ? GLHelpers::CreateFBO(intermediate) : 0;
        ++m_ResourceStats.scratchAllocations;
        if (fbo && retainScratch) {
            m_ScratchTexture=temporary.Release();m_ScratchFramebuffer=fbo;
            m_ScratchWidth=width;m_ScratchHeight=height;m_ScratchScalar=scalar;
            m_ResourceStats.retainedScratchBytes=scratchBytes;
        }
    } else ++m_ResourceStats.scratchReuses;
    if (!fbo) {destination.Restore(true);throw std::runtime_error("Could not allocate the blur intermediate.");}
    glUseProgram(m_ShaderProgram);
    glUniform1i(m_UniformLocations[0], 0);
    glUniform1i(m_UniformLocations[1], 1);
    glUniform1i(m_UniformLocations[2], 2);
    glUniform1i(m_UniformLocations[3], found->radius);
    glUniform1i(m_UniformLocations[5], scalar);
    glUniform1f(m_UniformLocations[7], found->sum);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,inputTexture);
    glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,found->texture);
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,inputTexture);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,width,height);
    glUniform1i(m_UniformLocations[4],0);
    glUniform1i(m_UniformLocations[6],pairInput);
    quad.Draw();
    destination.Restore(true);
    glBindTexture(GL_TEXTURE_2D,intermediate);
    glUniform1i(m_UniformLocations[4],1);
    glUniform1i(m_UniformLocations[6],1);
    quad.Draw();
    glUseProgram(0);
    if (!retainScratch) glDeleteFramebuffers(1,&fbo);
}

void SplitBlurLayerBase::RenderUI() {
    if (m_SigmaMode && m_SigmaFraction>=0) {
        float percentage=m_SigmaFraction*100.f;
        if(ImGuiExtras::NodeSliderFloat("Sigma (% of full image shorter side)","##BlurSigmaFraction",&percentage,0.f,25.f,"%.3f%%"))m_SigmaFraction=percentage/100.f;
        return;
    }
    ImGuiExtras::NodeSliderFloat(m_SigmaMode ? "Gaussian sigma (sample pixels)" : "Blur Radius / Amount", "##BlurAmount", &m_Amount, m_SigmaMode ? 0.f : 0.5f, 64.0f, "%.3f");
}

json SplitBlurLayerBase::Serialize() const {
    json j;
    j["type"] = GetTypeId();
    j["amount"] = m_Amount;
    if (m_SigmaMode) j["sigmaMode"] = true;
    if (m_SigmaFraction>=0) j["sigmaFraction"] = m_SigmaFraction;
    return j;
}

void SplitBlurLayerBase::Deserialize(const json& j) {
    m_SigmaMode = j.value("sigmaMode", false);
    m_SigmaFraction = j.value("sigmaFraction", -1.f);
    if (j.contains("amount")) {
        m_Amount = j["amount"];
    }
    if (j.contains("blurRadius")) {
        m_Amount = j["blurRadius"];
    }
}
