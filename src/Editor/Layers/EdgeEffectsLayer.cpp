#include "EdgeEffectsLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_EdgeVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_EdgeFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uMode;
uniform float uStrength;
uniform float uTolerance;
uniform float uBackgroundSaturation;
uniform float uForegroundSaturation;
uniform float uBloom;
uniform float uSmooth;
uniform float uBlend;

float getLuma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec2 texel = 1.0 / max(uRes, vec2(1.0));
    float x = texel.x;
    float y = texel.y;

    float m00 = getLuma(texture(uInputTex, vUV + vec2(-x, -y)).rgb);
    float m01 = getLuma(texture(uInputTex, vUV + vec2( 0, -y)).rgb);
    float m02 = getLuma(texture(uInputTex, vUV + vec2( x, -y)).rgb);
    float m10 = getLuma(texture(uInputTex, vUV + vec2(-x,  0)).rgb);
    float m12 = getLuma(texture(uInputTex, vUV + vec2( x,  0)).rgb);
    float m20 = getLuma(texture(uInputTex, vUV + vec2(-x,  y)).rgb);
    float m21 = getLuma(texture(uInputTex, vUV + vec2( 0,  y)).rgb);
    float m22 = getLuma(texture(uInputTex, vUV + vec2( x,  y)).rgb);

    float gx = (m02 + 2.0 * m12 + m22) - (m00 + 2.0 * m10 + m20);
    float gy = (m00 + 2.0 * m01 + m02) - (m20 + 2.0 * m21 + m22);
    float edge = sqrt(gx * gx + gy * gy);

    edge = smoothstep(uTolerance / 100.0, (uTolerance + 10.0) / 100.0, edge) * (uStrength / 100.0);
    edge = clamp(edge, 0.0, 1.0);

    float spreadMask = edge;
    if (uBloom > 0.0) {
        float accumEdge = 0.0;
        float radius = uBloom;
        int taps = int(clamp(radius * 1.5, 16.0, 48.0));
        float tapLimit = float(taps);

        for (int i = 1; i <= 48; ++i) {
            if (i > taps) break;

            float fi = float(i);
            float r = sqrt(fi / tapLimit) * radius;
            float theta = fi * 2.39996323;
            vec2 offset = vec2(cos(theta), sin(theta)) * r * texel;

            float neighborLuma = getLuma(texture(uInputTex, vUV + offset).rgb);
            float neighborLumaX = getLuma(texture(uInputTex, vUV + offset + vec2(x, 0.0)).rgb);
            float neighborLumaY = getLuma(texture(uInputTex, vUV + offset + vec2(0.0, y)).rgb);
            float neighborEdge = abs(neighborLumaX - neighborLuma) + abs(neighborLumaY - neighborLuma);
            neighborEdge = smoothstep(uTolerance / 100.0, (uTolerance + 10.0) / 100.0, neighborEdge * 4.0) * (uStrength / 100.0);

            float falloff = mix(1.0, 1.0 - (r / max(radius, 0.0001)), clamp(uSmooth / 100.0, 0.0, 1.0));
            accumEdge += neighborEdge * max(falloff, 0.0);
        }

        float bloomEdge = clamp((accumEdge / sqrt(max(tapLimit, 1.0))) * 1.6, 0.0, 1.0);
        spreadMask = max(edge, bloomEdge);
    }

    vec4 source = texture(uInputTex, vUV);
    vec3 result = source.rgb;

    if (uMode == 0) {
        result = mix(source.rgb, vec3(1.0), spreadMask);
    } else {
        float luma = getLuma(source.rgb);
        vec3 grayscale = vec3(luma);
        vec3 background = mix(grayscale, source.rgb, uBackgroundSaturation / 100.0);
        vec3 foreground = mix(grayscale, source.rgb, uForegroundSaturation / 100.0);
        result = mix(background, foreground, spreadMask);
    }

    FragColor = vec4(mix(source.rgb, result, uBlend / 100.0), source.a);
}
)";

EdgeEffectsLayer::EdgeEffectsLayer() {}

EdgeEffectsLayer::~EdgeEffectsLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void EdgeEffectsLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_EdgeVert, s_EdgeFrag);
}

void EdgeEffectsLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uMode"), m_Mode);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uStrength"), m_Strength);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uTolerance"), m_Tolerance);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBackgroundSaturation"), m_BackgroundSaturation);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uForegroundSaturation"), m_ForegroundSaturation);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBloom"), m_BloomSpread);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSmooth"), m_BloomSmoothness);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlend"), m_Blend);

    quad.Draw();
    glUseProgram(0);
}

void EdgeEffectsLayer::RenderUI() {
    const char* modes[] = { "Edge Overlay", "Saturation Mask" };

    ImGui::SliderFloat("Blend", &m_Blend, 0.0f, 100.0f, "%.0f");
    ImGui::Combo("Mode", &m_Mode, modes, IM_ARRAYSIZE(modes));
    ImGui::SliderFloat("Strength", &m_Strength, 0.0f, 1000.0f, "%.0f");
    ImGui::SliderFloat("Tolerance", &m_Tolerance, 0.0f, 100.0f, "%.0f");

    if (m_Mode == 1) {
        ImGui::Separator();
        ImGui::SliderFloat("Foreground Sat", &m_ForegroundSaturation, 0.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Background Sat", &m_BackgroundSaturation, 0.0f, 200.0f, "%.0f");
        ImGui::SliderFloat("Bloom Spread", &m_BloomSpread, 0.0f, 50.0f, "%.0f");
        ImGui::SliderFloat("Bloom Smoothness", &m_BloomSmoothness, 0.0f, 100.0f, "%.0f");
    }
}

json EdgeEffectsLayer::Serialize() const {
    json j;
    j["type"] = "EdgeEffects";
    j["blend"] = m_Blend;
    j["mode"] = m_Mode;
    j["strength"] = m_Strength;
    j["tolerance"] = m_Tolerance;
    j["foregroundSaturation"] = m_ForegroundSaturation;
    j["backgroundSaturation"] = m_BackgroundSaturation;
    j["bloomSpread"] = m_BloomSpread;
    j["bloomSmoothness"] = m_BloomSmoothness;
    return j;
}

void EdgeEffectsLayer::Deserialize(const json& j) {
    if (j.contains("blend")) m_Blend = j["blend"];
    if (j.contains("edgeBlend")) m_Blend = j["edgeBlend"];
    if (j.contains("mode")) m_Mode = j["mode"];
    if (j.contains("edgeMode")) m_Mode = j["edgeMode"];
    if (j.contains("strength")) m_Strength = j["strength"];
    if (j.contains("edgeStrength")) m_Strength = j["edgeStrength"];
    if (j.contains("tolerance")) m_Tolerance = j["tolerance"];
    if (j.contains("edgeTolerance")) m_Tolerance = j["edgeTolerance"];
    if (j.contains("foregroundSaturation")) m_ForegroundSaturation = j["foregroundSaturation"];
    if (j.contains("edgeFgSat")) m_ForegroundSaturation = j["edgeFgSat"];
    if (j.contains("backgroundSaturation")) m_BackgroundSaturation = j["backgroundSaturation"];
    if (j.contains("edgeBgSat")) m_BackgroundSaturation = j["edgeBgSat"];
    if (j.contains("bloomSpread")) m_BloomSpread = j["bloomSpread"];
    if (j.contains("edgeBloom")) m_BloomSpread = j["edgeBloom"];
    if (j.contains("bloomSmoothness")) m_BloomSmoothness = j["bloomSmoothness"];
    if (j.contains("edgeSmooth")) m_BloomSmoothness = j["edgeSmooth"];
}
