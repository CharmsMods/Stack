#include "GlareRaysLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_GlareRaysVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_GlareRaysFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform float uIntensity;
uniform float uRays;
uniform float uLength;
uniform float uBlur;

void main() {
    vec2 center = vec2(0.5);
    vec2 dir = vUV - center;
    float r = length(dir);
    float theta = atan(dir.y, dir.x);
    
    float sharpness = mix(40.0, 0.5, pow(clamp(uBlur, 0.0, 1.0), 0.7));
    
    float angularPattern = 0.0;
    for (float i = 0.0; i < 16.0; i++) {
        if (i >= uRays) break;
        float angle = i * 3.14159265 * 2.0 / uRays;
        float diff = abs(mod(theta - angle + 3.14159265, 3.14159265 * 2.0) - 3.14159265);
        angularPattern += exp(-diff * sharpness);
    }
    
    angularPattern *= (2.0 / max(1.0, uRays * 0.5));
    float radialFalloff = exp(-r * 4.0 / max(0.01, uLength));
    
    vec4 color = vec4(0.0);
    float totalWeight = 0.0;
    int samples = 24;
    
    for (int i = -samples; i <= samples; i++) {
        float t = float(i) / float(samples);
        vec2 sampleCoord = vUV + dir * t * uLength;
        float weight = (1.0 - abs(t)) * angularPattern * radialFalloff;
        color += texture(uInputTex, sampleCoord) * weight;
        totalWeight += weight;
    }
    
    if (totalWeight > 0.0) color /= totalWeight;
    
    vec4 original = texture(uInputTex, vUV);
    FragColor = original + color * uIntensity * 0.5;
}
)";

GlareRaysLayer::GlareRaysLayer() {}

GlareRaysLayer::~GlareRaysLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void GlareRaysLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_GlareRaysVert, s_GlareRaysFrag);
}

void GlareRaysLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uIntensity"), m_Intensity / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRays"), m_Rays);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLength"), m_Length / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlur"), m_Blur / 100.0f);

    quad.Draw();
    glUseProgram(0);
}

void GlareRaysLayer::RenderUI() {
    ImGui::SliderFloat("Intensity", &m_Intensity, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Ray Count", &m_Rays, 2.0f, 12.0f, "%.0f");
    ImGui::SliderFloat("Ray Length", &m_Length, 1.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Ray Softness", &m_Blur, 0.0f, 100.0f, "%.0f");
}

json GlareRaysLayer::Serialize() const {
    json j;
    j["type"] = "GlareRays";
    j["intensity"] = m_Intensity;
    j["rays"] = m_Rays;
    j["length"] = m_Length;
    j["softness"] = m_Blur;
    return j;
}

void GlareRaysLayer::Deserialize(const json& j) {
    if (j.contains("intensity")) m_Intensity = j["intensity"];
    if (j.contains("glareRaysIntensity")) m_Intensity = j["glareRaysIntensity"];
    if (j.contains("rays")) m_Rays = j["rays"];
    if (j.contains("glareRaysCount")) m_Rays = j["glareRaysCount"];
    if (j.contains("length")) m_Length = j["length"];
    if (j.contains("glareRaysLength")) m_Length = j["glareRaysLength"];
    if (j.contains("softness")) m_Blur = j["softness"];
    if (j.contains("glareRaysBlur")) m_Blur = j["glareRaysBlur"];
}
