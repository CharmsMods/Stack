#include "DenoisingLayer.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>

static const char* s_DenoisingVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_DenoisingFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uMode;
uniform int uSearchRadius;
uniform int uPatchRadius;
uniform float uH;
uniform float uStrength;

float patchDistance(vec2 p1, vec2 p2, int pRad) {
    float dist = 0.0;
    float count = 0.0;
    vec2 px = 1.0 / uRes;
    int step = (pRad >= 3) ? 2 : 1;
    for (int dx = -pRad; dx <= pRad; dx += step) {
        for (int dy = -pRad; dy <= pRad; dy += step) {
            vec2 off = vec2(float(dx), float(dy));
            vec3 c1 = texture(uInputTex, p1 + off * px).rgb;
            vec3 c2 = texture(uInputTex, p2 + off * px).rgb;
            vec3 d = c1 - c2;
            dist += dot(d, d);
            count += 1.0;
        }
    }
    return dist / count;
}

void main() {
    vec4 original = texture(uInputTex, vUV);
    vec2 px = 1.0 / uRes;
    vec3 result = vec3(0.0);

    if (uMode == 0) {
        float totalWeight = 0.0;
        float h2 = uH * uH;
        int step = (uSearchRadius >= 10) ? 3 : ((uSearchRadius >= 5) ? 2 : 1);
        for (int sx = -uSearchRadius; sx <= uSearchRadius; sx += step) {
            for (int sy = -uSearchRadius; sy <= uSearchRadius; sy += step) {
                vec2 offset = vec2(float(sx), float(sy));
                vec2 neighborUV = vUV + offset * px;
                float d = patchDistance(vUV, neighborUV, uPatchRadius);
                float w = exp(-d / h2);
                result += texture(uInputTex, neighborUV).rgb * w;
                totalWeight += w;
            }
        }
        result /= totalWeight;
    }
    else if (uMode == 1) {
        vec3 v[9];
        v[0] = texture(uInputTex, vUV + vec2(-px.x, -px.y)).rgb;
        v[1] = texture(uInputTex, vUV + vec2( 0.0,  -px.y)).rgb;
        v[2] = texture(uInputTex, vUV + vec2( px.x, -px.y)).rgb;
        v[3] = texture(uInputTex, vUV + vec2(-px.x,  0.0)).rgb;
        v[4] = texture(uInputTex, vUV + vec2( 0.0,   0.0)).rgb;
        v[5] = texture(uInputTex, vUV + vec2( px.x,  0.0)).rgb;
        v[6] = texture(uInputTex, vUV + vec2(-px.x,  px.y)).rgb;
        v[7] = texture(uInputTex, vUV + vec2( 0.0,   px.y)).rgb;
        v[8] = texture(uInputTex, vUV + vec2( px.x,  px.y)).rgb;
        float l[9];
        for(int i=0; i<9; i++) l[i] = dot(v[i], vec3(0.2126, 0.7152, 0.0722));
        for(int i=0; i<5; i++) {
            for(int j=i+1; j<9; j++) {
                if(l[i] > l[j]) {
                    float tempL = l[i]; l[i] = l[j]; l[j] = tempL;
                    vec3 tempV = v[i]; v[i] = v[j]; v[j] = tempV;
                }
            }
        }
        result = v[4];
    }
    else {
        float count = 0.0;
        for (int dx = -uSearchRadius; dx <= uSearchRadius; dx++) {
            for (int dy = -uSearchRadius; dy <= uSearchRadius; dy++) {
                vec2 uv = vUV + vec2(float(dx), float(dy)) * px;
                result += texture(uInputTex, uv).rgb;
                count += 1.0;
            }
        }
        result /= count;
    }

    FragColor = vec4(mix(original.rgb, result, uStrength), original.a);
}
)";

DenoisingLayer::DenoisingLayer() {}

DenoisingLayer::~DenoisingLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void DenoisingLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_DenoisingVert, s_DenoisingFrag);
}

void DenoisingLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), (float)width, (float)height);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uMode"), m_Mode);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uSearchRadius"), m_SearchRadius);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPatchRadius"), m_PatchRadius);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uH"), m_H);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uStrength"), m_Strength / 100.0f);

    quad.Draw();
    glUseProgram(0);
}

void DenoisingLayer::RenderUI() {
    const char* modes[] = { "Non-Local Means", "Median", "Mean (Box)" };
    ImGui::Combo("Algorithm", &m_Mode, modes, IM_ARRAYSIZE(modes));

    ImGui::SliderInt("Search Radius", &m_SearchRadius, 1, 15);
    if (m_Mode == 0) {
        ImGui::SliderInt("Patch Radius", &m_PatchRadius, 1, 5);
        ImGui::SliderFloat("Filter Strength (h)", &m_H, 0.01f, 2.0f);
    }
    ImGui::SliderFloat("Blend Strength", &m_Strength, 0.0f, 100.0f, "%.0f%%");
}

json DenoisingLayer::Serialize() const {
    json j;
    j["type"] = "Denoising";
    j["mode"] = m_Mode;
    j["searchRadius"] = m_SearchRadius;
    j["patchRadius"] = m_PatchRadius;
    j["h"] = m_H;
    j["strength"] = m_Strength;
    return j;
}

void DenoisingLayer::Deserialize(const json& j) {
    if (j.contains("mode")) m_Mode = j["mode"];
    if (j.contains("denoiseMode")) m_Mode = j["denoiseMode"];
    if (j.contains("searchRadius")) m_SearchRadius = j["searchRadius"];
    if (j.contains("denoiseSearchRadius")) m_SearchRadius = j["denoiseSearchRadius"];
    if (j.contains("patchRadius")) m_PatchRadius = j["patchRadius"];
    if (j.contains("denoisePatchRadius")) m_PatchRadius = j["denoisePatchRadius"];
    if (j.contains("h")) m_H = j["h"];
    if (j.contains("denoiseH")) m_H = j["denoiseH"];
    if (j.contains("strength")) m_Strength = j["strength"];
    if (j.contains("denoiseBlend")) m_Strength = j["denoiseBlend"];
}
