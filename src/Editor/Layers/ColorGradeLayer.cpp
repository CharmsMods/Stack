#include "ColorGradeLayer.h"
#include "Editor/EditorModule.h"
#include "Renderer/FullscreenQuad.h"
#include <imgui.h>
#include "Utils/ImGuiExtras.h"
#include <algorithm>
#include <cmath>

static const char* s_ColorGradeVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* s_ColorGradeFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uStrength;
uniform vec3 uShadows;
uniform vec3 uMidtones;
uniform vec3 uHighlights;

float getLuma(vec3 rgb) {
    return dot(rgb, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = color.rgb;
    float luma = getLuma(rgb);

    float shadowMask = 1.0 - smoothstep(0.0, 0.4, luma);
    float highlightMask = smoothstep(0.6, 1.0, luma);
    float midtoneMask = 1.0 - max(shadowMask, highlightMask);

    vec3 shadowOffset = (uShadows - vec3(getLuma(uShadows))) * 1.5;
    vec3 midtoneOffset = (uMidtones - vec3(getLuma(uMidtones))) * 1.5;
    vec3 highlightOffset = (uHighlights - vec3(getLuma(uHighlights))) * 1.5;

    vec3 graded = rgb;
    graded += shadowOffset * shadowMask;
    graded += midtoneOffset * midtoneMask;
    graded += (graded * highlightOffset) * highlightMask;

    rgb = mix(rgb, graded, clamp(uStrength, 0.0, 1.0));
    FragColor = vec4(clamp(rgb, 0.0, 1.0), color.a);
}
)";

ColorGradeLayer::ColorGradeLayer() {}

ColorGradeLayer::~ColorGradeLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ColorGradeLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(s_ColorGradeVert, s_ColorGradeFrag);
}

void ColorGradeLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;

    glUseProgram(m_ShaderProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);

    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uStrength"), m_Strength / 100.0f);
    glUniform3f(glGetUniformLocation(m_ShaderProgram, "uShadows"), m_Shadows[0], m_Shadows[1], m_Shadows[2]);
    glUniform3f(glGetUniformLocation(m_ShaderProgram, "uMidtones"), m_Midtones[0], m_Midtones[1], m_Midtones[2]);
    glUniform3f(glGetUniformLocation(m_ShaderProgram, "uHighlights"), m_Highlights[0], m_Highlights[1], m_Highlights[2]);

    quad.Draw();
    glUseProgram(0);
}

void ColorGradeLayer::RenderUI() {
    const ImGuiColorEditFlags flags =
        ImGuiColorEditFlags_Float |
        ImGuiColorEditFlags_DisplayRGB |
        ImGuiColorEditFlags_PickerHueWheel;

    ImGuiExtras::NodeSliderFloat("Strength", "##Strength", &m_Strength, 0.0f, 100.0f, "%.0f");
    ImGuiExtras::NodeColorEdit3("Shadows", "##Shadows", m_Shadows, flags);
    ImGuiExtras::NodeColorEdit3("Midtones", "##Midtones", m_Midtones, flags);
    ImGuiExtras::NodeColorEdit3("Highlights", "##Highlights", m_Highlights, flags);
}

NodeSurfaceSpec ColorGradeLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 448.0f;
    spec.maxWidth = 500.0f;
    spec.usesCanvasTool = false;
    return spec;
}

void ColorGradeLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    if (!editor) {
        return;
    }

    auto toneIsNeutral = [](const float color[3]) {
        return std::abs(color[0] - 1.0f) < 0.0005f &&
            std::abs(color[1] - 1.0f) < 0.0005f &&
            std::abs(color[2] - 1.0f) < 0.0005f;
    };
    auto resetTone = [](float color[3]) {
        color[0] = 1.0f;
        color[1] = 1.0f;
        color[2] = 1.0f;
    };

    const float layoutScale = std::max(0.001f, context.layoutScale);
    const float totalWidth = std::max(120.0f, context.safeContentWidth);
    const float logicalWidth = std::max(120.0f, context.logicalSafeContentWidth);
    const float itemGap = std::max(2.0f, context.itemGap);
    const float sectionGap = std::max(2.0f, context.sectionGap);
    const float inlineGap = 12.0f * layoutScale;
    const float footerGap = 10.0f * layoutScale;
    const float resetButtonWidth = 88.0f * layoutScale;
    const float wheelGap = 12.0f * layoutScale;

    ImGuiExtras::RichSectionLabel("Tone Balance");
    ImGui::SameLine(0.0f, footerGap);
    if (ImGuiExtras::RichFullWidthButton("Reset All", resetButtonWidth)) {
        m_Strength = 100.0f;
        resetTone(m_Shadows);
        resetTone(m_Midtones);
        resetTone(m_Highlights);
    }
    ImGui::Dummy(ImVec2(0.0f, std::max(1.0f, itemGap * 0.35f)));
    ImGui::TextDisabled("Pure white keeps a tonal range neutral.");
    if (context.focused) {
        ImGui::SameLine(0.0f, footerGap);
        ImGui::TextDisabled("Focused");
    }
    ImGui::Dummy(ImVec2(0.0f, sectionGap));

    ImGuiExtras::RichSectionLabel("Strength", std::max(1.0f, itemGap * 0.35f));
    const float valueLaneWidth = 62.0f * layoutScale;
    const float sliderWidth = std::max(48.0f, totalWidth - valueLaneWidth - inlineGap);
    ImGui::SetNextItemWidth(sliderWidth);
    ImGui::SliderFloat("##ColorGradeStrength", &m_Strength, 0.0f, 100.0f, "");
    ImGui::SameLine(0.0f, inlineGap);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.0f%%", m_Strength);

    ImGui::Dummy(ImVec2(0.0f, sectionGap));
    ImGuiExtras::RichSectionLabel("Tonal Ranges", std::max(1.0f, itemGap * 0.5f));

    const int wheelColumns = logicalWidth >= 280.0f ? 2 : 1;
    const float wheelWidth = wheelColumns > 1
        ? (totalWidth - wheelGap * static_cast<float>(wheelColumns - 1)) / static_cast<float>(wheelColumns)
        : totalWidth;

    struct ToneSlot {
        const char* label;
        float* color;
    };
    ToneSlot tones[] = {
        { "Shadows", m_Shadows },
        { "Midtones", m_Midtones },
        { "Highlights", m_Highlights }
    };

    for (int i = 0; i < 3; ++i) {
        if (i > 0 && (i % wheelColumns) != 0) {
            ImGui::SameLine(0.0f, wheelGap);
        } else if (i > 0) {
            ImGui::Dummy(ImVec2(0.0f, sectionGap));
        }

        ImGui::BeginGroup();
        ImGuiExtras::GradeWheel3(tones[i].label, tones[i].label, tones[i].color, wheelWidth);
        ImGui::Dummy(ImVec2(0.0f, std::max(1.0f, itemGap * 0.25f)));
        if (toneIsNeutral(tones[i].color)) {
            ImGui::TextDisabled("Neutral");
        } else {
            ImGui::TextDisabled("Active tint");
        }
        ImGui::EndGroup();
    }
}

json ColorGradeLayer::Serialize() const {
    json j;
    j["type"] = "ColorGrade";
    j["strength"] = m_Strength;
    j["shadows"] = {m_Shadows[0], m_Shadows[1], m_Shadows[2]};
    j["midtones"] = {m_Midtones[0], m_Midtones[1], m_Midtones[2]};
    j["highlights"] = {m_Highlights[0], m_Highlights[1], m_Highlights[2]};
    return j;
}

void ColorGradeLayer::Deserialize(const json& j) {
    if (j.contains("strength")) m_Strength = j["strength"];
    if (j.contains("shadows") && j["shadows"].is_array() && j["shadows"].size() == 3) {
        m_Shadows[0] = j["shadows"][0];
        m_Shadows[1] = j["shadows"][1];
        m_Shadows[2] = j["shadows"][2];
    }
    if (j.contains("midtones") && j["midtones"].is_array() && j["midtones"].size() == 3) {
        m_Midtones[0] = j["midtones"][0];
        m_Midtones[1] = j["midtones"][1];
        m_Midtones[2] = j["midtones"][2];
    }
    if (j.contains("highlights") && j["highlights"].is_array() && j["highlights"].size() == 3) {
        m_Highlights[0] = j["highlights"][0];
        m_Highlights[1] = j["highlights"][1];
        m_Highlights[2] = j["highlights"][2];
    }
}
