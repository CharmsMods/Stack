#include "CompressionLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <algorithm>
#include <imgui.h>

static const char* s_CompressionVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_CompressionFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uMethod;
uniform float uQuality;
uniform float uBlockSize;
uniform float uBlend;
uniform float uIterations;

void main() {
    vec4 original = texture(uInputTex, vUV);
    vec3 result = original.rgb;
    vec2 px = 1.0 / uRes;

    float qNorm = clamp(uQuality / 100.0, 0.01, 1.0);
    float qInv = 1.0 - qNorm;
    float iteratedInv = 1.0 - pow(1.0 - qInv, max(1.0, uIterations));

    if (uMethod == 0) {
        float blockSize = max(2.0, uBlockSize);
        vec2 blockCoord = floor(vUV * uRes / blockSize);
        vec2 blockUV = blockCoord * blockSize / uRes;
        vec2 blockCenter = blockUV + (blockSize * 0.5) * px;

        vec3 dcColor = texture(uInputTex, blockCenter).rgb;
        vec3 acColor = original.rgb;

        float quantStrength = iteratedInv * iteratedInv;
        float levels = mix(256.0, max(4.0, 8.0 * qNorm), quantStrength);
        vec3 quantized = floor(acColor * levels + 0.5) / max(levels, 1.0);
        result = mix(quantized, dcColor, quantStrength * 0.6);

        vec2 blockFract = fract(vUV * uRes / blockSize);
        vec2 edgeDist = abs(blockFract - 0.5);
        float edgeFactor = smoothstep(0.35, 0.5, max(edgeDist.x, edgeDist.y));
        vec3 ringing = result + (result - dcColor) * 0.15;
        result = mix(result, ringing, edgeFactor * quantStrength);
    } else if (uMethod == 1) {
        float chromaBlock = max(2.0, uBlockSize);
        float luma = dot(original.rgb, vec3(0.2126, 0.7152, 0.0722));
        vec2 chromaCoord = floor(vUV * uRes / chromaBlock);
        vec2 chromaUV = (chromaCoord + 0.5) * chromaBlock * px;
        vec3 chromaSample = texture(uInputTex, chromaUV).rgb;
        float chromaLuma = dot(chromaSample, vec3(0.2126, 0.7152, 0.0722));
        vec3 chromaDiff = chromaSample - vec3(chromaLuma);
        vec3 reconstructed = vec3(luma) + chromaDiff;
        result = mix(original.rgb, reconstructed, iteratedInv);
    } else {
        float blurRadius = iteratedInv * uBlockSize * 0.5;
        vec3 blurred = vec3(0.0);
        float totalWeight = 0.0;
        for (float dx = -2.0; dx <= 2.0; dx += 1.0) {
            for (float dy = -2.0; dy <= 2.0; dy += 1.0) {
                vec2 offset = vec2(dx, dy) * px * blurRadius;
                float weight = exp(-(dx * dx + dy * dy) / 8.0);
                blurred += texture(uInputTex, vUV + offset).rgb * weight;
                totalWeight += weight;
            }
        }
        blurred /= max(totalWeight, 0.0001);

        float bandLevels = mix(256.0, max(8.0, 32.0 * qNorm), iteratedInv);
        vec3 banded = floor(blurred * bandLevels + 0.5) / max(bandLevels, 1.0);
        result = mix(original.rgb, banded, iteratedInv * 0.8);
    }

    result = mix(original.rgb, result, clamp(uBlend, 0.0, 1.0));
    FragColor = vec4(clamp(result, 0.0, 1.0), original.a);
}
)";

CompressionLayer::CompressionLayer() {}

CompressionLayer::~CompressionLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void CompressionLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_CompressionVert, s_CompressionFrag);
}

void CompressionLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uMethod"), m_Method);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uQuality"), m_Quality);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlockSize"), m_BlockSize);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlend"), m_Blend / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIterations"), static_cast<float>(m_Iterations));

    quad.Draw();
    glUseProgram(0);
}

void CompressionLayer::RenderUI() {
    const char* methods[] = { "DCT Block", "Chroma Subsampling", "Wavelet" };
    ImGui::Combo("Method", &m_Method, methods, IM_ARRAYSIZE(methods));
    ImGui::SliderFloat("Quality", &m_Quality, 1.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Block Size", &m_BlockSize, 2.0f, 32.0f, "%.0f");
    ImGui::SliderFloat("Blend", &m_Blend, 0.0f, 100.0f, "%.0f");
    ImGui::SliderInt("Iterations", &m_Iterations, 1, 20);
    ImGui::TextDisabled("Native adapts repeated web passes here by folding iteration count into effect strength.");
}

json CompressionLayer::Serialize() const {
    json j;
    j["type"] = "Compression";
    j["method"] = m_Method;
    j["quality"] = m_Quality;
    j["blockSize"] = m_BlockSize;
    j["blend"] = m_Blend;
    j["iterations"] = m_Iterations;
    return j;
}

void CompressionLayer::Deserialize(const json& j) {
    if (j.contains("method")) m_Method = j["method"];
    if (j.contains("quality")) m_Quality = j["quality"];
    if (j.contains("blockSize")) m_BlockSize = j["blockSize"];
    if (j.contains("blend")) m_Blend = j["blend"];
    if (j.contains("iterations")) m_Iterations = j["iterations"];
}
