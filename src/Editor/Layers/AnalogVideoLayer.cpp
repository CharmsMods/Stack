#include "AnalogVideoLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>
#include <cmath>
#include <GLFW/glfw3.h>

static const char* s_AnalogVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_AnalogFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uTime;
uniform float uWobble;
uniform float uBleed;
uniform float uCurve;
uniform float uNoise;

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    vec2 uv = vUV;

    // 1. CRT Curvature
    vec2 cc = uv - 0.5;
    float r2 = cc.x*cc.x + cc.y*cc.y;
    uv = cc * (1.0 + uCurve * r2 * 2.0) + 0.5;

    // Border masking for curvature
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // 2. Tape Tracking Wobble
    float wobbleOffset = sin(uv.y * 20.0 + uTime * 5.0) * 0.005 + 
                         sin(uv.y * 50.0 - uTime * 15.0) * 0.002;
    uv.x += wobbleOffset * uWobble;

    // 3. Chromatic Bleed
    float bleedOffset = 0.005 * uBleed;
    float r = texture(uInputTex, vec2(uv.x + bleedOffset, uv.y)).r;
    float g = texture(uInputTex, uv).g;
    float b = texture(uInputTex, vec2(uv.x - bleedOffset, uv.y)).b;
    vec3 col = vec3(r, g, b);

    // 4. Scanline Noise
    float scanline = sin(uv.y * 800.0) * 0.04 * uNoise;
    float staticNoise = (rand(uv + mod(uTime, 10.0)) - 0.5) * 0.1 * uNoise;
    col += scanline + staticNoise;

    FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
)";

AnalogVideoLayer::AnalogVideoLayer() {}

AnalogVideoLayer::~AnalogVideoLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void AnalogVideoLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_AnalogVert, s_AnalogFrag);
}

void AnalogVideoLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uTime"), (float)glfwGetTime());
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uWobble"), m_Wobble / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBleed"), m_Bleed / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uCurve"), m_Curve / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uNoise"), m_Noise / 100.0f);

    quad.Draw();
    glUseProgram(0);
}

void AnalogVideoLayer::RenderUI() {
    ImGui::SliderFloat("Tape Wobble", &m_Wobble, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Color Bleed", &m_Bleed, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("CRT Curve", &m_Curve, 0.0f, 100.0f, "%.0f");
    ImGui::SliderFloat("Scanline Noise", &m_Noise, 0.0f, 100.0f, "%.0f");
}

json AnalogVideoLayer::Serialize() const {
    json j;
    j["type"] = "AnalogVideo";
    j["wobble"] = m_Wobble;
    j["bleed"] = m_Bleed;
    j["curve"] = m_Curve;
    j["noise"] = m_Noise;
    return j;
}

void AnalogVideoLayer::Deserialize(const json& j) {
    if (j.contains("wobble")) m_Wobble = j["wobble"];
    if (j.contains("analogWobble")) m_Wobble = j["analogWobble"];
    if (j.contains("bleed")) m_Bleed = j["bleed"];
    if (j.contains("analogBleed")) m_Bleed = j["analogBleed"];
    if (j.contains("curve")) m_Curve = j["curve"];
    if (j.contains("analogCurve")) m_Curve = j["analogCurve"];
    if (j.contains("noise")) m_Noise = j["noise"];
    if (j.contains("analogNoise")) m_Noise = j["analogNoise"];
}
