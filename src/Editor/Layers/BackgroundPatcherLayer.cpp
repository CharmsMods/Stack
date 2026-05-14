#include "BackgroundPatcherLayer.h"
#include "Renderer/FullscreenQuad.h"
#include "Editor/EditorModule.h"
#include <algorithm>
#include <imgui.h>
#include "Utils/ImGuiExtras.h"

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
uniform int uKeepSelectedRange;
uniform int uAaEnabled;
uniform float uAntialias;
uniform int uNumProtected;
uniform int uUseFloodMask;
uniform int uUseBrushMask;
uniform int uPatchEnabled;
uniform vec2 uResolution;
uniform int uShowDebugOverlay;

float getSelectionMask(vec2 uv) {
    vec3 sampleRgb = texture(uInputTex, uv).rgb;
    float dist = distance(sampleRgb, uTargetColor);
    return 1.0 - smoothstep(uTolerance, uTolerance + uSmoothing + 0.001, dist);
}

float getMask(vec2 uv) {
    float selection = getSelectionMask(uv);

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

    float centerSelectionMask = getSelectionMask(vUV);
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

    if (uShowDebugOverlay == 1) {
        float luma = dot(color.rgb, vec3(0.299, 0.587, 0.114));
        vec3 bw = vec3(luma);
        if (removedAlpha > 0.001) {
            if (centerSelectionMask > 0.001) {
                FragColor = vec4(1.0, 0.0, 1.0, 1.0);
            } else {
                FragColor = vec4(0.0, 1.0, 1.0, 1.0);
            }
        } else {
            FragColor = vec4(bw, 1.0);
        }
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
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uShowDebugOverlay"), m_ShowDebugOverlay ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uKeepSelectedRange"), m_KeepSelected ? 1 : 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uResolution"), (float)width, (float)height);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uUseFloodMask"), 0); // Not implemented yet
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uUseBrushMask"), 0); // Not implemented yet

    quad.Draw();
    glUseProgram(0);
}

void BackgroundPatcherLayer::RenderUI(EditorModule* editor) {
    if (!editor) return;

    ImGui::Text("SELECTION");
    ImGui::Spacing();

    bool isPicking = editor->IsPickingColor();
    if (isPicking) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
    }
    const float pickerButtonWidth = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    if (ImGui::Button(isPicking ? "Cancel Picking" : "Pick Color To Remove", ImVec2(pickerButtonWidth, 30.0f))) {
        if (isPicking) {
            editor->SetPickingColor(false);
        } else {
            editor->SetPickingColor(true, [this](float r, float g, float b) {
                m_TargetColor[0] = r;
                m_TargetColor[1] = g;
                m_TargetColor[2] = b;
            });
        }
    }
    if (isPicking) {
        ImGui::PopStyleColor(2);
    }

    ImGuiExtras::NodeColorEdit3(
        "Target Color",
        "##TargetColor",
        m_TargetColor,
        ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

    ImGui::Spacing();
    
    // Removed Area Opacity goes from 0-100. It translates to target alpha.
    float opacity = m_TargetAlpha * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Removed Area Opacity", "##RemovedAreaOpacity", &opacity, 0.0f, 100.0f, "%.0f %%")) {
        m_TargetAlpha = opacity / 100.0f;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("TOLERANCE & EDGES");
    ImGui::Spacing();

    float tol100 = m_Tolerance * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Color Tolerance", "##ColorTolerance", &tol100, 0.0f, 100.0f, "%.0f %%")) {
        m_Tolerance = tol100 / 100.0f;
    }

    // Draw the gradient bar
    ImGui::Spacing();
    ImGui::TextDisabled("RANGE OF COLORS CURRENTLY REMOVED");
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = 12.0f;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Gradient from picked color to transparent/faded
    ImU32 colLeft = ImGui::ColorConvertFloat4ToU32(ImVec4(m_TargetColor[0], m_TargetColor[1], m_TargetColor[2], 1.0f));
    ImU32 colRight = ImGui::ColorConvertFloat4ToU32(ImVec4(m_TargetColor[0], m_TargetColor[1], m_TargetColor[2], 0.0f));
    drawList->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + h), colLeft, colRight, colRight, colLeft);
    drawList->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(100, 100, 100, 255));
    ImGui::Dummy(ImVec2(w, h));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    float smoothing100 = m_Smoothing * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Edge Smoothing", "##EdgeSmoothing", &smoothing100, 0.0f, 100.0f, "%.0f %%")) {
        m_Smoothing = smoothing100 / 100.0f;
    }

    ImGuiExtras::NodeSliderFloat("Edge Shift", "##EdgeShift(px)", &m_EdgeShift, -10.0f, 10.0f, "%.0f px");

    float defringe100 = m_Defringe * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Defringe", "##Defringe", &defringe100, 0.0f, 100.0f, "%.0f %%")) {
        m_Defringe = defringe100 / 100.0f;
    }

    ImGui::Spacing();
    ImGuiExtras::NodeCheckbox("Keep Selected Range", "##KeepSelectedRangeOnly", &m_KeepSelected);
    ImGuiExtras::NodeCheckbox("Visualizer (Pink=In, Cyan=Edge)", "##RemovalVisualizer(Magenta=InRange,Cyan=EdgeBleed)", &m_ShowDebugOverlay);
}

void BackgroundPatcherLayer::RenderAdvancedEditor(EditorModule* editor) {
    if (!editor) {
        return;
    }

    const float leftWidth = 440.0f;
    const float columnGap = 30.0f;
    const float bodyHeight = std::max(200.0f, ImGui::GetContentRegionAvail().y);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(14.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("BackgroundRemoverControls", ImVec2(leftWidth, bodyHeight), false);
    const float controlWidth = std::max(260.0f, ImGui::GetContentRegionAvail().x - 4.0f);

    ImGui::TextDisabled("Selection");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    const bool isPicking = editor->IsPickingColor();
    if (isPicking) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.73f, 0.23f, 0.25f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.82f, 0.30f, 0.32f, 1.0f));
    }
    if (ImGui::Button(isPicking ? "Cancel Picking" : "Select Color", ImVec2(std::max(220.0f, ImGui::GetContentRegionAvail().x), 36.0f))) {
        if (isPicking) {
            editor->SetPickingColor(false);
        } else {
            editor->SetPickingColor(true, [this](float r, float g, float b) {
                m_TargetColor[0] = r;
                m_TargetColor[1] = g;
                m_TargetColor[2] = b;
            });
        }
    }
    if (isPicking) {
        ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextDisabled("Selected Color");
    const float swatchWidth = std::max(160.0f, std::min(220.0f, ImGui::GetContentRegionAvail().x * 0.58f));
    const float swatchHeight = 28.0f;
    const ImVec2 swatchMin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##AdvancedTargetColorSwatch", ImVec2(swatchWidth, swatchHeight));
    const ImVec2 swatchMax = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        swatchMin,
        swatchMax,
        ImGui::ColorConvertFloat4ToU32(ImVec4(m_TargetColor[0], m_TargetColor[1], m_TargetColor[2], 1.0f)),
        4.0f);
    drawList->AddRect(swatchMin, swatchMax, IM_COL32(255, 255, 255, 26), 4.0f);
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(
        "R %.0f  G %.0f  B %.0f",
        m_TargetColor[0] * 255.0f,
        m_TargetColor[1] * 255.0f,
        m_TargetColor[2] * 255.0f);

    float opacity = m_TargetAlpha * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Removed Area Opacity", "##AdvancedRemovedAreaOpacity", &opacity, 0.0f, 100.0f, "%.0f %%", controlWidth)) {
        m_TargetAlpha = opacity / 100.0f;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextDisabled("Tolerance & Edges");
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    float tolerancePercent = m_Tolerance * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Color Tolerance", "##AdvancedColorTolerance", &tolerancePercent, 0.0f, 100.0f, "%.0f %%", controlWidth)) {
        m_Tolerance = tolerancePercent / 100.0f;
    }

    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::TextDisabled("Removed Range");
    const ImVec2 gradientMin = ImGui::GetCursorScreenPos();
    const float gradientWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    const float gradientHeight = 12.0f;
    const ImU32 leftColor = ImGui::ColorConvertFloat4ToU32(ImVec4(m_TargetColor[0], m_TargetColor[1], m_TargetColor[2], 1.0f));
    const ImU32 rightColor = ImGui::ColorConvertFloat4ToU32(ImVec4(m_TargetColor[0], m_TargetColor[1], m_TargetColor[2], 0.0f));
    drawList->AddRectFilledMultiColor(
        gradientMin,
        ImVec2(gradientMin.x + gradientWidth, gradientMin.y + gradientHeight),
        leftColor,
        rightColor,
        rightColor,
        leftColor);
    ImGui::Dummy(ImVec2(gradientWidth, gradientHeight + 10.0f));

    float smoothingPercent = m_Smoothing * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Edge Smoothing", "##AdvancedEdgeSmoothing", &smoothingPercent, 0.0f, 100.0f, "%.0f %%", controlWidth)) {
        m_Smoothing = smoothingPercent / 100.0f;
    }

    ImGuiExtras::NodeSliderFloat("Edge Shift", "##AdvancedEdgeShift", &m_EdgeShift, -10.0f, 10.0f, "%.0f px", controlWidth);

    float defringePercent = m_Defringe * 100.0f;
    if (ImGuiExtras::NodeSliderFloat("Defringe", "##AdvancedDefringe", &defringePercent, 0.0f, 100.0f, "%.0f %%", controlWidth)) {
        m_Defringe = defringePercent / 100.0f;
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ImGui::TextDisabled("Options");
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGuiExtras::NodeCheckbox("Keep Selected Range", "##AdvancedKeepSelectedRange", &m_KeepSelected, controlWidth);
    ImGuiExtras::NodeCheckbox("Visualizer (Pink=In, Cyan=Edge)", "##AdvancedVisualizer", &m_ShowDebugOverlay, controlWidth);

    ImGui::EndChild();

    ImGui::SameLine(0.0f, columnGap);

    ImGui::BeginChild("BackgroundRemoverPreviewPane", ImVec2(0.0f, bodyHeight), false);
    ImGui::TextDisabled("Preview");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    editor->RenderAdvancedEditorPreview("BackgroundRemoverPreview", ImVec2(0.0f, std::max(260.0f, ImGui::GetContentRegionAvail().y - 4.0f)), true);
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
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
    j["showDebugOverlay"] = m_ShowDebugOverlay;
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
    if (j.contains("showDebugOverlay")) m_ShowDebugOverlay = j["showDebugOverlay"];
}
