#include "PaletteReconstructorLayer.h"

#include "Renderer/FullscreenQuad.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>

static const char* s_PaletteVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_PaletteFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec3 uPalette[16];
uniform int uPaletteSize;
uniform float uBlend;
uniform float uSmoothing;
uniform int uSmoothingType;
uniform vec2 uRes;

vec3 getWorkingColor(vec2 uv) {
    vec3 base = texture(uInputTex, uv).rgb;
    if (uSmoothing <= 0.0) {
        return base;
    }

    vec2 texel = 1.0 / max(uRes, vec2(1.0));
    float radius = uSmoothing / 10.0;
    vec3 sum = vec3(0.0);
    float totalWeight = 0.0;

    for (float y = -1.0; y <= 1.0; y += 1.0) {
        for (float x = -1.0; x <= 1.0; x += 1.0) {
            vec2 offset = vec2(x, y) * texel * radius;
            float weight = 1.0;
            if (uSmoothingType == 1) {
                float distSq = x * x + y * y;
                weight = exp(-distSq);
            }

            sum += texture(uInputTex, uv + offset).rgb * weight;
            totalWeight += weight;
        }
    }

    return sum / max(totalWeight, 0.0001);
}

void main() {
    vec4 source = texture(uInputTex, vUV);
    if (uPaletteSize <= 0) {
        FragColor = source;
        return;
    }

    vec3 working = getWorkingColor(vUV);
    vec3 bestColor = uPalette[0];
    float minDistance = 1e10;

    for (int i = 0; i < 16; ++i) {
        if (i >= uPaletteSize) break;
        float distanceToColor = distance(working, uPalette[i]);
        if (distanceToColor < minDistance) {
            minDistance = distanceToColor;
            bestColor = uPalette[i];
        }
    }

    vec3 result = mix(working, bestColor, clamp(uBlend, 0.0, 1.0));
    FragColor = vec4(clamp(result, 0.0, 1.0), source.a);
}
)";

PaletteReconstructorLayer::PaletteReconstructorLayer() {
    ResetDefaultPalette();
}

PaletteReconstructorLayer::~PaletteReconstructorLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void PaletteReconstructorLayer::ResetDefaultPalette() {
    m_Palette = {};

    const float defaults[kMaxPaletteColors][3] = {
        {0.07f, 0.07f, 0.07f},
        {0.96f, 0.97f, 0.98f},
        {1.00f, 0.54f, 0.00f},
        {0.00f, 0.82f, 1.00f},
        {0.93f, 0.21f, 0.28f},
        {0.19f, 0.74f, 0.38f},
        {0.54f, 0.33f, 0.85f},
        {0.98f, 0.86f, 0.25f},
        {0.14f, 0.23f, 0.45f},
        {0.90f, 0.47f, 0.70f},
        {0.24f, 0.62f, 0.60f},
        {0.58f, 0.36f, 0.19f},
        {0.37f, 0.38f, 0.40f},
        {0.67f, 0.71f, 0.74f},
        {0.96f, 0.74f, 0.62f},
        {0.28f, 0.16f, 0.32f}
    };

    for (int index = 0; index < kMaxPaletteColors; ++index) {
        m_Palette[index * 3 + 0] = defaults[index][0];
        m_Palette[index * 3 + 1] = defaults[index][1];
        m_Palette[index * 3 + 2] = defaults[index][2];
    }
}

void PaletteReconstructorLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_PaletteVert, s_PaletteFrag);
}

void PaletteReconstructorLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform3fv(glGetUniformLocation(m_ShaderProgram, "uPalette"), kMaxPaletteColors, m_Palette.data());
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPaletteSize"), std::max(0, std::min(kMaxPaletteColors, m_PaletteCount)));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlend"), m_Blend / 100.0f);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSmoothing"), m_Smoothing);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uSmoothingType"), m_SmoothingType);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));

    quad.Draw();
    glUseProgram(0);
}

void PaletteReconstructorLayer::RenderUI() {
    const char* smoothingTypes[] = { "Box", "Gaussian" };

    ImGui::SliderFloat("Global Blend", &m_Blend, 0.0f, 100.0f, "%.0f");
    ImGui::Combo("Smoothing Type", &m_SmoothingType, smoothingTypes, IM_ARRAYSIZE(smoothingTypes));
    ImGui::SliderFloat("Palette Smoothing", &m_Smoothing, 0.0f, 100.0f, "%.0f");
    ImGui::TextDisabled("Native adapts the web app's shared palette/extraction flow into a layer-local palette bank here.");
    ImGui::Text("Palette Colors: %d / %d", m_PaletteCount, kMaxPaletteColors);

    if (ImGui::Button("Reset Default Palette")) {
        ResetDefaultPalette();
    }
    ImGui::SameLine();
    if (ImGui::Button("Randomize Palette")) {
        for (int index = 0; index < kMaxPaletteColors * 3; ++index) {
            m_Palette[index] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(m_PaletteCount >= kMaxPaletteColors);
    if (ImGui::Button("Add Color")) {
        const int baseIndex = m_PaletteCount * 3;
        m_Palette[baseIndex + 0] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        m_Palette[baseIndex + 1] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        m_Palette[baseIndex + 2] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        ++m_PaletteCount;
    }
    ImGui::EndDisabled();

    if (ImGui::TreeNode("Palette Colors")) {
        for (int index = 0; index < m_PaletteCount; ++index) {
            char label[32];
            snprintf(label, sizeof(label), "Color %d", index + 1);
            ImGui::PushID(index);
            ImGui::ColorEdit3(label, &m_Palette[index * 3]);
            ImGui::SameLine();
            ImGui::BeginDisabled(m_PaletteCount <= 2);
            if (ImGui::Button("Remove")) {
                for (int moveIndex = index; moveIndex < m_PaletteCount - 1; ++moveIndex) {
                    m_Palette[moveIndex * 3 + 0] = m_Palette[(moveIndex + 1) * 3 + 0];
                    m_Palette[moveIndex * 3 + 1] = m_Palette[(moveIndex + 1) * 3 + 1];
                    m_Palette[moveIndex * 3 + 2] = m_Palette[(moveIndex + 1) * 3 + 2];
                }

                const int lastIndex = (m_PaletteCount - 1) * 3;
                m_Palette[lastIndex + 0] = 0.0f;
                m_Palette[lastIndex + 1] = 0.0f;
                m_Palette[lastIndex + 2] = 0.0f;
                --m_PaletteCount;
                ImGui::EndDisabled();
                ImGui::PopID();
                break;
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}

json PaletteReconstructorLayer::Serialize() const {
    json j;
    j["type"] = "PaletteReconstructor";
    j["blend"] = m_Blend;
    j["smoothing"] = m_Smoothing;
    j["smoothingType"] = m_SmoothingType;
    j["paletteCount"] = m_PaletteCount;
    j["extractCount"] = m_PaletteCount;

    json palette = json::array();
    for (int index = 0; index < m_PaletteCount; ++index) {
        palette.push_back({
            m_Palette[index * 3 + 0],
            m_Palette[index * 3 + 1],
            m_Palette[index * 3 + 2]
        });
    }
    j["palette"] = palette;
    return j;
}

void PaletteReconstructorLayer::Deserialize(const json& j) {
    if (j.contains("blend")) m_Blend = j["blend"];
    if (j.contains("paletteBlend")) m_Blend = j["paletteBlend"];
    if (j.contains("smoothing")) m_Smoothing = j["smoothing"];
    if (j.contains("paletteSmoothing")) m_Smoothing = j["paletteSmoothing"];
    if (j.contains("smoothingType")) m_SmoothingType = j["smoothingType"];
    if (j.contains("paletteSmoothingType")) m_SmoothingType = j["paletteSmoothingType"];
    if (j.contains("paletteCount")) m_PaletteCount = j["paletteCount"];
    if (j.contains("extractCount")) m_PaletteCount = j["extractCount"];

    m_PaletteCount = std::max(2, std::min(kMaxPaletteColors, m_PaletteCount));

    if (j.contains("palette") && j["palette"].is_array()) {
        const auto& palette = j["palette"];
        for (int index = 0; index < m_PaletteCount && index < static_cast<int>(palette.size()); ++index) {
            if (!palette[index].is_array() || palette[index].size() < 3) continue;
            m_Palette[index * 3 + 0] = palette[index][0];
            m_Palette[index * 3 + 1] = palette[index][1];
            m_Palette[index * 3 + 2] = palette[index][2];
        }
    }
}
