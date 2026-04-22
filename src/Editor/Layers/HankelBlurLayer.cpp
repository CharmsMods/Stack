#include "HankelBlurLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_HankelVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_HankelFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform float uRadius;
uniform float uQuality;
uniform float uIntensity;

float besselJ0(float x) {
    float ax = abs(x);
    if (ax < 8.0) {
        float y = x * x;
        float ans1 = 57568490574.0 + y * (-13362590354.0 + y * (651619640.7
            + y * (-11214424.18 + y * (77392.33017 + y * (-184.9052456)))));
        float ans2 = 57568490411.0 + y * (1029532985.0 + y * (9494680.718
            + y * (59272.64853 + y * (267.8532712 + y * 1.0))));
        return ans1 / ans2;
    } else {
        float z = 8.0 / ax;
        float y = z * z;
        float xx = ax - 0.785398164;
        float ans1 = 1.0 + y * (-0.1098628627e-2 + y * (0.2734510407e-4
            + y * (-0.2073370639e-5 + y * 0.2093887211e-6)));
        float ans2 = -0.1562499995e-1 + y * (0.1430488765e-3
            + y * (-0.6911147651e-5 + y * (0.7621095161e-6 - y * 0.934935152e-7)));
        return sqrt(0.636619772 / ax) * (cos(xx) * ans1 - z * sin(xx) * ans2);
    }
}

void main() {
    vec2 texelSize = 1.0 / uRes;
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    int samples = int(uQuality);
    
    for (int i = 0; i < 32; i++) {
        if (i >= samples) break;
        for (int j = 0; j < 32; j++) {
            if (j >= samples) break;
            
            float r = (float(i) / float(samples)) * uRadius;
            float theta = (float(j) / float(samples)) * 6.283185307;
            
            vec2 offset = vec2(cos(theta), sin(theta)) * r * texelSize;
            float weight = besselJ0(r * 2.0);
            weight = abs(weight) + 0.01;
            
            color += texture(uInputTex, vUV + offset) * weight;
            totalWeight += weight;
        }
    }
    
    vec4 blurred = color / max(0.001, totalWeight);
    vec4 original = texture(uInputTex, vUV);
    FragColor = mix(original, blurred, uIntensity);
}
)";

HankelBlurLayer::HankelBlurLayer() {}

HankelBlurLayer::~HankelBlurLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void HankelBlurLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_HankelVert, s_HankelFrag);
}

void HankelBlurLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRadius"), m_Radius);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uQuality"), m_Quality);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIntensity"), m_Intensity);

    quad.Draw();
    glUseProgram(0);
}

void HankelBlurLayer::RenderUI() {
    ImGui::SliderFloat("Blur Radius", &m_Radius, 0.0f, 30.0f, "%.1f");
    ImGui::SliderFloat("Quality (Samples)", &m_Quality, 2.0f, 16.0f, "%.0f");
    ImGui::SliderFloat("Intensity", &m_Intensity, 0.0f, 1.0f);
}

json HankelBlurLayer::Serialize() const {
    json j;
    j["type"] = "HankelBlur";
    j["radius"] = m_Radius;
    j["quality"] = m_Quality;
    j["intensity"] = m_Intensity;
    return j;
}

void HankelBlurLayer::Deserialize(const json& j) {
    if (j.contains("radius")) m_Radius = j["radius"];
    if (j.contains("hankelBlurRadius")) m_Radius = j["hankelBlurRadius"];
    if (j.contains("quality")) m_Quality = j["quality"];
    if (j.contains("hankelBlurQuality")) m_Quality = j["hankelBlurQuality"];
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("hankelBlurIntensity")) m_Intensity = j["hankelBlurIntensity"];
}
