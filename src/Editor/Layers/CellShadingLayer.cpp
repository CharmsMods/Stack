#include "CellShadingLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_CellShadingVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_CellShadingFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uLevels;
uniform float uBias;
uniform float uGamma;
uniform int uQuantMode;
uniform int uBandMap;
uniform int uEdgeMethod;
uniform float uEdgeStr;
uniform float uEdgeThick;
uniform float uColorPreserve;
uniform int uEdgeEnable;

vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

float getLuma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

float quantize(float value) {
    value = clamp(value + uBias, 0.0, 1.0);
    value = pow(value, max(0.001, uGamma));

    float levels = float(max(2, uLevels));
    float quantized = floor(value * levels) / max(1.0, levels - 1.0);

    if (uBandMap == 1) {
        quantized = smoothstep(0.0, 1.0, quantized);
    } else if (uBandMap == 2) {
        quantized = floor(value * levels) / levels;
    }

    return quantized;
}

float sobel(vec2 uv) {
    vec2 px = vec2(uEdgeThick) / uRes;
    float l00 = getLuma(texture(uInputTex, uv + vec2(-px.x, -px.y)).rgb);
    float l10 = getLuma(texture(uInputTex, uv + vec2(0.0, -px.y)).rgb);
    float l20 = getLuma(texture(uInputTex, uv + vec2(px.x, -px.y)).rgb);
    float l01 = getLuma(texture(uInputTex, uv + vec2(-px.x, 0.0)).rgb);
    float l21 = getLuma(texture(uInputTex, uv + vec2(px.x, 0.0)).rgb);
    float l02 = getLuma(texture(uInputTex, uv + vec2(-px.x, px.y)).rgb);
    float l12 = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float l22 = getLuma(texture(uInputTex, uv + vec2(px.x, px.y)).rgb);

    float gx = l00 + 2.0 * l01 + l02 - (l20 + 2.0 * l21 + l22);
    float gy = l00 + 2.0 * l10 + l20 - (l02 + 2.0 * l12 + l22);
    return sqrt(gx * gx + gy * gy);
}

float robertsCross(vec2 uv) {
    vec2 px = vec2(uEdgeThick) / uRes;
    float l00 = getLuma(texture(uInputTex, uv).rgb);
    float l10 = getLuma(texture(uInputTex, uv + vec2(px.x, 0.0)).rgb);
    float l01 = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float l11 = getLuma(texture(uInputTex, uv + px).rgb);
    float gx = l00 - l11;
    float gy = l10 - l01;
    return sqrt(gx * gx + gy * gy);
}

float laplacian(vec2 uv) {
    vec2 px = vec2(uEdgeThick) / uRes;
    float left = getLuma(texture(uInputTex, uv + vec2(-px.x, 0.0)).rgb);
    float right = getLuma(texture(uInputTex, uv + vec2(px.x, 0.0)).rgb);
    float up = getLuma(texture(uInputTex, uv + vec2(0.0, -px.y)).rgb);
    float down = getLuma(texture(uInputTex, uv + vec2(0.0, px.y)).rgb);
    float center = getLuma(texture(uInputTex, uv).rgb);
    return abs(left + right + up + down - 4.0 * center) * 2.0;
}

void main() {
    vec4 base = texture(uInputTex, vUV);
    vec3 color = base.rgb;
    vec3 result = color;

    if (uQuantMode == 0) {
        float q = quantize(getLuma(color));
        vec3 monochrome = vec3(q);
        vec3 hsv = rgb2hsv(color);
        hsv.z = q;
        vec3 preserved = hsv2rgb(hsv);
        result = mix(monochrome, preserved, clamp(uColorPreserve, 0.0, 1.0));
    } else if (uQuantMode == 1) {
        result.r = quantize(color.r);
        result.g = quantize(color.g);
        result.b = quantize(color.b);
    } else {
        vec3 hsv = rgb2hsv(color);
        hsv.z = quantize(hsv.z);
        result = hsv2rgb(hsv);
    }

    if (uEdgeEnable == 1) {
        float edge = 0.0;
        if (uEdgeMethod == 0) edge = sobel(vUV);
        else if (uEdgeMethod == 1) edge = robertsCross(vUV);
        else edge = laplacian(vUV);

        edge = smoothstep(0.1, 1.0, edge * 2.0);
        result = mix(result, vec3(0.0), edge * clamp(uEdgeStr, 0.0, 2.0));
    }

    FragColor = vec4(clamp(result, 0.0, 1.0), base.a);
}
)";

CellShadingLayer::CellShadingLayer() {}

CellShadingLayer::~CellShadingLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void CellShadingLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_CellShadingVert, s_CellShadingFrag);
}

void CellShadingLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uLevels"), m_Levels);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBias"), m_Bias);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uGamma"), m_Gamma);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uQuantMode"), m_QuantMode);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uBandMap"), m_BandMap);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uEdgeMethod"), m_EdgeMethod);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uEdgeStr"), m_EdgeStrength / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uEdgeThick"), m_EdgeThickness);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uColorPreserve"), m_ColorPreserve / 100.0f);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uEdgeEnable"), m_ShowEdges ? 1 : 0);

    quad.Draw();
    glUseProgram(0);
}

void CellShadingLayer::RenderUI() {
    const char* quantModes[] = { "Luminance", "Per-Channel", "HSV Value" };
    const char* bandMaps[] = { "Linear", "Smooth", "Hard" };
    const char* edgeMethods[] = { "Sobel", "Roberts Cross", "Laplacian" };

    ImGui::SliderInt("Shading Levels", &m_Levels, 2, 12);
    ImGui::SliderFloat("Contrast Bias", &m_Bias, -1.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Gamma", &m_Gamma, 0.1f, 3.0f, "%.2f");
    ImGui::Combo("Quantization Mode", &m_QuantMode, quantModes, IM_ARRAYSIZE(quantModes));
    ImGui::Combo("Band Mapping", &m_BandMap, bandMaps, IM_ARRAYSIZE(bandMaps));
    ImGui::Separator();
    ImGui::Combo("Edge Method", &m_EdgeMethod, edgeMethods, IM_ARRAYSIZE(edgeMethods));
    ImGui::SliderFloat("Edge Strength", &m_EdgeStrength, 0.0f, 200.0f, "%.0f");
    ImGui::SliderFloat("Edge Thickness", &m_EdgeThickness, 0.5f, 5.0f, "%.1f");
    ImGui::SliderFloat("Color Preserve", &m_ColorPreserve, 0.0f, 100.0f, "%.0f");
    ImGui::Checkbox("Show Edges", &m_ShowEdges);
}

json CellShadingLayer::Serialize() const {
    json j;
    j["type"] = "CellShading";
    j["levels"] = m_Levels;
    j["bias"] = m_Bias;
    j["gamma"] = m_Gamma;
    j["quantMode"] = m_QuantMode;
    j["bandMap"] = m_BandMap;
    j["edgeMethod"] = m_EdgeMethod;
    j["edgeStrength"] = m_EdgeStrength;
    j["edgeThickness"] = m_EdgeThickness;
    j["colorPreserve"] = m_ColorPreserve;
    j["showEdges"] = m_ShowEdges;
    return j;
}

void CellShadingLayer::Deserialize(const json& j) {
    if (j.contains("levels")) m_Levels = j["levels"];
    if (j.contains("bias")) m_Bias = j["bias"];
    if (j.contains("gamma")) m_Gamma = j["gamma"];
    if (j.contains("quantMode")) m_QuantMode = j["quantMode"];
    if (j.contains("bandMap")) m_BandMap = j["bandMap"];
    if (j.contains("edgeMethod")) m_EdgeMethod = j["edgeMethod"];
    if (j.contains("edgeStrength")) m_EdgeStrength = j["edgeStrength"];
    if (j.contains("edgeThickness")) m_EdgeThickness = j["edgeThickness"];
    if (j.contains("colorPreserve")) m_ColorPreserve = j["colorPreserve"];
    if (j.contains("showEdges")) m_ShowEdges = j["showEdges"];
}
