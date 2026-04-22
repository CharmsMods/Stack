#include "CorruptionLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_CorruptionVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_CorruptionFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uAlgorithm;
uniform float uResScale;

void main() {
    float blockSize = max(2.0, (100.0 - uResScale) / 5.0 + 1.0);
    vec4 col;
    
    if (uAlgorithm == 0) {
        vec2 blockPos = floor(vUV * uRes / blockSize) * blockSize / uRes;
        vec2 offset = (vUV - blockPos) * uRes / blockSize;
        offset = floor(offset * 2.0) / 2.0 * blockSize / uRes;
        col = texture(uInputTex, blockPos + offset);
        
        vec2 edgeDist = abs(fract(vUV * uRes / blockSize) - 0.5);
        float edge = smoothstep(0.3, 0.5, max(edgeDist.x, edgeDist.y));
        col.rgb = mix(col.rgb, col.rgb * 0.95, edge * 0.3);
    } else if (uAlgorithm == 1) {
        vec2 pixelPos = floor(vUV * uRes / blockSize) * blockSize / uRes;
        col = texture(uInputTex, pixelPos + (blockSize * 0.5) / uRes);
    } else {
        float bleedAmount = blockSize / uRes.x;
        vec4 left = texture(uInputTex, vUV - vec2(bleedAmount, 0.0));
        vec4 center = texture(uInputTex, vUV);
        vec4 right = texture(uInputTex, vUV + vec2(bleedAmount, 0.0));
        col.r = mix(center.r, right.r, 0.3);
        col.g = center.g;
        col.b = mix(center.b, left.b, 0.3);
        col.a = center.a;
    }
    
    FragColor = col;
}
)";

CorruptionLayer::CorruptionLayer() {}

CorruptionLayer::~CorruptionLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void CorruptionLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_CorruptionVert, s_CorruptionFrag);
}

void CorruptionLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uAlgorithm"), m_Algorithm);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uResScale"), m_ResScale);

    quad.Draw();
    glUseProgram(0);
}

void CorruptionLayer::RenderUI() {
    const char* algos[] = { "JPEG Blocks", "Pixelation", "Color Bleed" };
    ImGui::Combo("Algorithm", &m_Algorithm, algos, IM_ARRAYSIZE(algos));
    ImGui::SliderFloat("Quality Scale", &m_ResScale, 1.0f, 100.0f, "%.0f");
}

json CorruptionLayer::Serialize() const {
    json j;
    j["type"] = "Corruption";
    j["algorithm"] = m_Algorithm;
    j["resScale"] = m_ResScale;
    return j;
}

void CorruptionLayer::Deserialize(const json& j) {
    if (j.contains("algorithm")) m_Algorithm = j["algorithm"];
    if (j.contains("corruptionAlgo")) m_Algorithm = j["corruptionAlgo"];
    if (j.contains("resScale")) m_ResScale = j["resScale"];
    if (j.contains("corruptionScale")) m_ResScale = j["corruptionScale"];
}
