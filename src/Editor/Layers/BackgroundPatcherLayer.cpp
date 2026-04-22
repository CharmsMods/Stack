#include "BackgroundPatcherLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_BgPatcherVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_BgPatcherFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uFloodMask;
uniform sampler2D uBrushMask;
uniform vec3 uTargetColor;
uniform float uTargetAlpha;
uniform float uTolerance;
uniform float uSmoothing;
uniform float uDefringe;
uniform float uEdgeShift;
uniform int uShowMask;
uniform int uKeepSelectedRange;
uniform int uAaEnabled;
uniform float uAntialias;
uniform int uNumProtected;
uniform int uUseFloodMask;
uniform int uUseBrushMask;
uniform int uPatchEnabled;
uniform vec2 uResolution;

float getMask(vec2 uv) {
    vec3 sampleRgb = texture(uInputTex, uv).rgb;
    float dist = distance(sampleRgb, uTargetColor);
    float selection = 1.0 - smoothstep(uTolerance, uTolerance + uSmoothing + 0.001, dist);

    if (uUseFloodMask == 1) {
        float floodValue = texture(uFloodMask, vec2(uv.x, 1.0 - uv.y)).r;
        if (floodValue < 0.5) selection = 0.0;
    }

    float removalMask = uKeepSelectedRange == 1 ? (1.0 - selection) : selection;

    if (uUseBrushMask == 1) {
        vec4 brushVal = texture(uBrushMask, vec2(uv.x, 1.0 - uv.y));
        removalMask = mix(removalMask, 1.0, brushVal.r);
        removalMask = mix(removalMask, 0.0, brushVal.g);
    }

    return clamp(removalMask, 0.0, 1.0);
}

void main() {
    vec2 texel = 1.0 / max(uResolution, vec2(1.0));
    vec4 color = texture(uInputTex, vUV);

    float centerMask = getMask(vUV);
    float mask = centerMask;

    if (uEdgeShift > 0.0) {
        float maxMask = mask;
        float averageMask = mask;
        float count = 1.0;
        int r = int(uEdgeShift);
        for (int y = -3; y <= 3; y += 1) {
            for (int x = -3; x <= 3; x += 1) {
                if (x == 0 && y == 0) continue;
                float radius = length(vec2(float(x), float(y)));
                if (radius <= uEdgeShift) {
                    float sampleMask = getMask(vUV + vec2(float(x), float(y)) * texel);
                    maxMask = max(maxMask, sampleMask);
                    averageMask += sampleMask;
                    count += 1.0;
                }
            }
        }
        averageMask /= count;
        mask = mix(centerMask, mix(maxMask, averageMask, 0.5), min(uEdgeShift, 1.0));
    }

    float newAlpha = mix(color.a, uTargetAlpha, mask);
    float removedAlpha = max(color.a - newAlpha, 0.0);
    float effectiveDefringe = uKeepSelectedRange == 1 ? 0.0 : uDefringe;
    vec3 defringedColor = clamp((color.rgb - uTargetColor * removedAlpha * effectiveDefringe) / max(1.0 - removedAlpha * effectiveDefringe, 0.0001), 0.0, 1.0);

    if (uShowMask == 1) {
        float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
        if (mask > 0.001) {
            FragColor = vec4(1.0, 1.0, 0.0, 1.0);
            return;
        }
        FragColor = vec4(vec3(luma), 1.0);
        return;
    }

    FragColor = vec4(defringedColor, newAlpha);
}
)";

BackgroundPatcherLayer::BackgroundPatcherLayer() {}

BackgroundPatcherLayer::~BackgroundPatcherLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
    if (m_FloodMask) glDeleteTextures(1, &m_FloodMask);
    if (m_BrushMask) glDeleteTextures(1, &m_BrushMask);
}

void BackgroundPatcherLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_BgPatcherVert, s_BgPatcherFrag);
    unsigned char white[] = { 255, 255, 255, 255 };
    m_FloodMask = GLHelpers::CreateTextureFromPixels(white, 1, 1, 4);
    m_BrushMask = GLHelpers::CreateTextureFromPixels(white, 1, 1, 4);
}

void BackgroundPatcherLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_FloodMask);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uFloodMask"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_BrushMask);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uBrushMask"), 2);

    glUniform3f(glGetUniformLocation(m_ShaderProgram, "uTargetColor"), m_TargetColor[0], m_TargetColor[1], m_TargetColor[2]);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uTargetAlpha"), m_TargetAlpha);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uTolerance"), m_Tolerance);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSmoothing"), m_Smoothing);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uDefringe"), m_Defringe);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uEdgeShift"), m_EdgeShift);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uShowMask"), m_ShowMask ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uKeepSelectedRange"), m_KeepSelected ? 1 : 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uResolution"), (float)width, (float)height);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uUseFloodMask"), 0); // Not implemented yet
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uUseBrushMask"), 0); // Not implemented yet

    quad.Draw();
    glUseProgram(0);
}

void BackgroundPatcherLayer::RenderUI() {
    ImGui::ColorEdit3("Target Color", m_TargetColor);
    ImGui::SliderFloat("Target Alpha", &m_TargetAlpha, 0.0f, 1.0f);
    ImGui::SliderFloat("Tolerance", &m_Tolerance, 0.0f, 1.0f);
    ImGui::SliderFloat("Smoothing", &m_Smoothing, 0.0f, 1.0f);
    ImGui::SliderFloat("Defringe", &m_Defringe, 0.0f, 1.0f);
    ImGui::SliderFloat("Edge Shift", &m_EdgeShift, 0.0f, 10.0f);
    ImGui::Checkbox("Keep Selected", &m_KeepSelected);
    ImGui::Checkbox("Show Selection Mask", &m_ShowMask);
}

json BackgroundPatcherLayer::Serialize() const {
    json j;
    j["type"] = "BackgroundPatcher";
    j["targetColor"] = { m_TargetColor[0], m_TargetColor[1], m_TargetColor[2] };
    j["targetAlpha"] = m_TargetAlpha;
    j["tolerance"] = m_Tolerance;
    j["smoothing"] = m_Smoothing;
    j["defringe"] = m_Defringe;
    j["edgeShift"] = m_EdgeShift;
    j["keepSelected"] = m_KeepSelected;
    j["showMask"] = m_ShowMask;
    return j;
}

void BackgroundPatcherLayer::Deserialize(const json& j) {
    if (j.contains("targetColor") && j["targetColor"].is_array() && j["targetColor"].size() == 3) {
        for (int i = 0; i < 3; ++i) m_TargetColor[i] = j["targetColor"][i];
    }
    if (j.contains("bgPatcherTargetColor")) {
        // Handle hex string if needed, but for now assuming RGB array in JSON for C++ parity
    }
    if (j.contains("targetAlpha")) m_TargetAlpha = j["targetAlpha"];
    if (j.contains("tolerance")) m_Tolerance = j["tolerance"];
    if (j.contains("smoothing")) m_Smoothing = j["smoothing"];
    if (j.contains("defringe")) m_Defringe = j["defringe"];
    if (j.contains("edgeShift")) m_EdgeShift = j["edgeShift"];
    if (j.contains("keepSelected")) m_KeepSelected = j["keepSelected"];
    if (j.contains("showMask")) m_ShowMask = j["showMask"];
}
