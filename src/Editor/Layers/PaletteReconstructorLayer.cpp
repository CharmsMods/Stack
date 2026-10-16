#include "PaletteReconstructorLayer.h"
#include "Renderer/Operations/StylizeShaders.h"

#include "Renderer/FullscreenQuad.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <imgui.h>
#include "Utils/ImGuiExtras.h"



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
    m_ShaderProgram = GLHelpers::CreateShaderProgram(Stack::Renderer::Operations::StylizeVertex(), Stack::Renderer::Operations::PaletteReconstructorFragment());
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
    glUniform2f(glGetUniformLocation(m_ShaderProgram,"uSourceSize"),
        m_ExecutionSourceSize[0]>0 ? m_ExecutionSourceSize[0] : static_cast<float>(width),
        m_ExecutionSourceSize[1]>0 ? m_ExecutionSourceSize[1] : static_cast<float>(height));

    quad.Draw();
    glUseProgram(0);
}

void PaletteReconstructorLayer::RenderUI() {
    const char* smoothingTypes[] = { "Box", "Gaussian" };

    ImGuiExtras::NodeSliderFloat("Global Blend", "##GlobalBlend", &m_Blend, 0.0f, 100.0f, "%.0f");
    ImGuiExtras::NodeCombo("Smoothing Type", "##SmoothingType", &m_SmoothingType, smoothingTypes, IM_ARRAYSIZE(smoothingTypes));
    ImGuiExtras::NodeSliderFloat("Palette Smoothing", "##PaletteSmoothing", &m_Smoothing, 0.0f, 100.0f, "%.0f");
    ImGui::TextDisabled("Native adapts the web app's shared palette/extraction flow into a layer-local palette bank here.");
    ImGui::Text("Palette Colors: %d / %d", m_PaletteCount, kMaxPaletteColors);

    const float actionButtonWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::Button("Reset Default Palette", ImVec2(actionButtonWidth, 0.0f))) {
        ResetDefaultPalette();
    }
    if (ImGui::Button("Randomize Palette", ImVec2(actionButtonWidth, 0.0f))) {
        for (int index = 0; index < kMaxPaletteColors * 3; ++index) {
            m_Palette[index] = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        }
    }
    ImGui::BeginDisabled(m_PaletteCount >= kMaxPaletteColors);
    if (ImGui::Button("Add Color", ImVec2(actionButtonWidth, 0.0f))) {
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
            ImGuiExtras::NodeColorEdit3(label, "##PaletteColor", &m_Palette[index * 3]);
            ImGui::BeginDisabled(m_PaletteCount <= 2);
            const float removeButtonWidth = std::max(96.0f, ImGui::GetContentRegionAvail().x);
            if (ImGui::Button("Remove", ImVec2(removeButtonWidth, 0.0f))) {
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
    m_ExecutionSourceSize={};
    if (j.contains("executionSourceSize")) m_ExecutionSourceSize=j.at("executionSourceSize").get<std::array<float,2>>();
    if (j.contains("blend")) m_Blend = j["blend"];
    if (j.contains("smoothing")) m_Smoothing = j["smoothing"];
    if (j.contains("smoothingType")) m_SmoothingType = j["smoothingType"];
    if (j.contains("paletteCount")) m_PaletteCount = j["paletteCount"];

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
