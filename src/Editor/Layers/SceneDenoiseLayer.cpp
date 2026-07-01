#include "SceneDenoiseLayer.h"

#include "Editor/EditorModule.h"
#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cstdio>
#include <imgui.h>
#include <string>

namespace {

const char* kSceneDenoiseVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kSceneDenoiseFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform vec2 uRes;
uniform int uEnabled;
uniform int uRadius;
uniform float uLumaStrength;
uniform float uChromaStrength;
uniform float uEdgeProtection;
uniform float uChromaEdgeProtection;
uniform float uDetailPreservation;
uniform float uShadowBoost;
uniform float uHighlightProtection;
uniform float uBlend;
uniform int uAutoAnalyze;
uniform float uAutoAmount;
uniform int uPreviewMode;
uniform float uDifferenceAmount;
uniform float uSplitPosition;

vec3 rgbToYCgCo(vec3 rgb) {
    float y = dot(rgb, vec3(0.25, 0.5, 0.25));
    float co = rgb.r - rgb.b;
    float cg = rgb.g - 0.5 * (rgb.r + rgb.b);
    return vec3(y, cg, co);
}

vec3 yCgCoToRgb(vec3 ycc) {
    float tmp = ycc.x - ycc.y * 0.5;
    float g = ycc.x + ycc.y * 0.5;
    float r = tmp + ycc.z * 0.5;
    float b = tmp - ycc.z * 0.5;
    return vec3(r, g, b);
}

float sceneLuma(vec3 rgb) {
    return max(dot(max(rgb, vec3(0.0)), vec3(0.2126, 0.7152, 0.0722)), 0.0);
}

float log2Safe(float value) {
    return log(max(value, 0.000001)) * 1.44269504089;
}

void main() {
    vec4 original = texture(uInputTex, vUV);
    if (uEnabled == 0) {
        FragColor = original;
        return;
    }

    vec2 px = 1.0 / max(uRes, vec2(1.0));
    vec3 centerYcc = rgbToYCgCo(original.rgb);
    float centerLuma = sceneLuma(original.rgb);
    float centerLogLuma = log2Safe(centerLuma);
    float edgeProtection = clamp(uEdgeProtection, 0.0, 1.0);
    float chromaEdgeProtection = clamp(uChromaEdgeProtection, 0.0, 1.0);
    float detailProtection = clamp(uDetailPreservation, 0.0, 1.0);
    float edgeScaleEv = mix(1.40, 0.085, edgeProtection);
    float chromaScale = max(0.004, 0.035 + centerLuma * 0.075 + length(centerYcc.yz) * 0.18);
    float chromaEdgeScale = mix(5.0, 0.82, chromaEdgeProtection);
    float shadowMask = 1.0 - smoothstep(0.018, 0.28, centerLuma);
    float highlightMask = smoothstep(0.85, 4.0, centerLuma);
    float toneStrengthScale = (1.0 + clamp(uShadowBoost, 0.0, 1.0) * shadowMask * 0.85) *
        (1.0 - clamp(uHighlightProtection, 0.0, 1.0) * highlightMask * 0.78);
    int radius = clamp(uRadius, 1, 12);
    float radius2 = float(radius * radius) + 0.25;

    float ySum = 0.0;
    float yWeight = 0.0;
    vec2 cSum = vec2(0.0);
    float cWeight = 0.0;
    float localLogSum = 0.0;
    float localLogSqSum = 0.0;
    vec2 localChromaSum = vec2(0.0);
    float localChromaSqSum = 0.0;
    float localMomentWeight = 0.0;

    for (int yy = -12; yy <= 12; ++yy) {
        for (int xx = -12; xx <= 12; ++xx) {
            if (abs(xx) > radius || abs(yy) > radius) continue;
            vec2 offset = vec2(float(xx), float(yy));
            float dist2 = dot(offset, offset);
            if (dist2 > radius2) continue;

            vec3 sampleRgb = texture(uInputTex, vUV + offset * px).rgb;
            vec3 sampleYcc = rgbToYCgCo(sampleRgb);
            float sampleLogLuma = log2Safe(sceneLuma(sampleRgb));
            float yDeltaEv = abs(sampleLogLuma - centerLogLuma);
            float chromaDelta = length(sampleYcc.yz - centerYcc.yz) / chromaScale;
            float spatial = exp(-dist2 / max(1.0, float(radius * radius) * 0.48));
            float yRange = exp(-(yDeltaEv * yDeltaEv) / max(0.0001, edgeScaleEv * edgeScaleEv));
            float chromaRange = exp(-(chromaDelta * chromaDelta) / max(0.0001, chromaEdgeScale * chromaEdgeScale));
            float detailGate = mix(1.0, yRange, detailProtection);
            float yw = spatial * yRange * detailGate * mix(1.0, chromaRange, chromaEdgeProtection * 0.42);
            float cw = spatial * mix(1.0, yRange, 0.35 + edgeProtection * 0.45) * chromaRange;
            float analysisWeight = spatial * mix(1.0, yRange, 0.62) * mix(1.0, chromaRange, 0.42);
            ySum += sampleYcc.x * yw;
            yWeight += yw;
            cSum += sampleYcc.yz * cw;
            cWeight += cw;
            localLogSum += sampleLogLuma * analysisWeight;
            localLogSqSum += sampleLogLuma * sampleLogLuma * analysisWeight;
            localChromaSum += sampleYcc.yz * analysisWeight;
            localChromaSqSum += dot(sampleYcc.yz, sampleYcc.yz) * analysisWeight;
            localMomentWeight += analysisWeight;
        }
    }

    float filteredY = yWeight > 0.000001 ? ySum / yWeight : centerYcc.x;
    vec2 filteredC = cWeight > 0.000001 ? cSum / cWeight : centerYcc.yz;
    vec3 filteredYcc = centerYcc;
    float lumaAmount = clamp(uLumaStrength, 0.0, 1.0) * clamp(uBlend, 0.0, 1.0) * toneStrengthScale;
    float chromaAmount = clamp(uChromaStrength, 0.0, 1.0) * clamp(uBlend, 0.0, 1.0) *
        mix(toneStrengthScale, 1.0 + clamp(uShadowBoost, 0.0, 1.0) * shadowMask * 0.45, 0.55);
    vec3 leftRgb = texture(uInputTex, vUV + vec2(-px.x, 0.0)).rgb;
    vec3 rightRgb = texture(uInputTex, vUV + vec2(px.x, 0.0)).rgb;
    vec3 upRgb = texture(uInputTex, vUV + vec2(0.0, -px.y)).rgb;
    vec3 downRgb = texture(uInputTex, vUV + vec2(0.0, px.y)).rgb;
    float gradientEv = max(
        abs(log2Safe(sceneLuma(rightRgb)) - log2Safe(sceneLuma(leftRgb))),
        abs(log2Safe(sceneLuma(downRgb)) - log2Safe(sceneLuma(upRgb)))) * 0.5;
    vec2 chromaGradientA = rgbToYCgCo(rightRgb).yz - rgbToYCgCo(leftRgb).yz;
    vec2 chromaGradientB = rgbToYCgCo(downRgb).yz - rgbToYCgCo(upRgb).yz;
    float normalizedChromaGradient = max(length(chromaGradientA), length(chromaGradientB)) / chromaScale;
    float momentWeight = max(localMomentWeight, 0.000001);
    float meanLogLuma = localLogSum / momentWeight;
    float logStd = sqrt(max(0.0, localLogSqSum / momentWeight - meanLogLuma * meanLogLuma));
    vec2 meanChroma = localChromaSum / momentWeight;
    float chromaStd = sqrt(max(0.0, localChromaSqSum / momentWeight - dot(meanChroma, meanChroma))) / chromaScale;
    float lumaDetailMask = smoothstep(0.035, 0.24, gradientEv);
    float chromaDetailMask = smoothstep(0.45, 2.4, normalizedChromaGradient);
    float protectedDetailMask = clamp(max(lumaDetailMask, chromaDetailMask) * mix(0.55, 1.0, detailProtection), 0.0, 1.0);
    float flatConfidence = 1.0 - protectedDetailMask;
    float autoLumaNoise = smoothstep(0.012, 0.11, logStd) * flatConfidence;
    float autoChromaNoise = smoothstep(0.10, 0.92, chromaStd) * flatConfidence;
    float autoNeed = max(autoLumaNoise, autoChromaNoise);
    if (uAutoAnalyze != 0) {
        float autoAmount = clamp(uAutoAmount, 0.0, 1.5);
        float shadowNeed = shadowMask * (0.35 + autoNeed * 0.65);
        float highlightProtect = highlightMask * clamp(uHighlightProtection, 0.0, 1.0);
        float autoDetailProtect = protectedDetailMask * (0.35 + detailProtection * 0.65);
        float autoLumaBoost = autoAmount * (autoLumaNoise * 0.34 + shadowNeed * 0.30);
        float autoChromaBoost = autoAmount * (autoChromaNoise * 0.48 + shadowNeed * 0.25);
        lumaAmount = clamp(lumaAmount + autoLumaBoost, 0.0, 1.0);
        chromaAmount = clamp(chromaAmount + autoChromaBoost, 0.0, 1.0);
        lumaAmount *= 1.0 - autoDetailProtect * 0.52;
        chromaAmount *= 1.0 - max(autoDetailProtect * chromaEdgeProtection * 0.42, highlightProtect * 0.18);
    }
    filteredYcc.x = mix(centerYcc.x, filteredY, clamp(lumaAmount, 0.0, 1.0));
    filteredYcc.yz = mix(centerYcc.yz, filteredC, clamp(chromaAmount, 0.0, 1.0));
    vec3 denoisedRgb = yCgCoToRgb(filteredYcc);
    vec3 resultRgb = denoisedRgb;
    int previewMode = clamp(uPreviewMode, 0, 7);
    float differenceAmount = max(uDifferenceAmount, 0.0);
    if (previewMode == 1) {
        resultRgb = original.rgb;
    } else if (previewMode == 2) {
        resultRgb = abs(denoisedRgb - original.rgb) * differenceAmount;
    } else if (previewMode == 3) {
        float splitPosition = clamp(uSplitPosition, 0.02, 0.98);
        resultRgb = vUV.x < splitPosition ? original.rgb : denoisedRgb;
        float divider = 1.0 - smoothstep(0.0, px.x * 2.0, abs(vUV.x - splitPosition));
        resultRgb = mix(resultRgb, vec3(1.0), divider * 0.65);
    } else if (previewMode == 4) {
        float lumaDelta = abs(sceneLuma(denoisedRgb) - sceneLuma(original.rgb)) * differenceAmount;
        resultRgb = vec3(lumaDelta);
    } else if (previewMode == 5) {
        vec2 chromaDelta = rgbToYCgCo(denoisedRgb).yz - centerYcc.yz;
        resultRgb = vec3(length(chromaDelta) * differenceAmount);
    } else if (previewMode == 6) {
        resultRgb = vec3(autoChromaNoise, autoNeed, autoLumaNoise);
    } else if (previewMode == 7) {
        resultRgb = vec3(protectedDetailMask);
    }
    FragColor = vec4(resultRgb, original.a);
}
)";

enum SceneDenoisePreviewMode {
    kPreviewDenoised = 0,
    kPreviewOriginal,
    kPreviewDifference,
    kPreviewSplit,
    kPreviewLumaDifference,
    kPreviewChromaDifference,
    kPreviewNoiseMap,
    kPreviewProtectedDetail,
    kPreviewModeCount
};

struct SceneDenoisePresetDefinition {
    const char* name;
    const char* description;
    float autoAmount;
    int radius;
    float lumaStrength;
    float chromaStrength;
    float edgeProtection;
    float chromaEdgeProtection;
    float detailPreservation;
    float shadowBoost;
    float highlightProtection;
    float blend;
};

const SceneDenoisePresetDefinition kSceneDenoisePresets[] = {
    { "Subtle", "Low-strength smoothing for already clean images.", 0.25f, 3, 0.22f, 0.40f, 0.78f, 0.70f, 0.85f, 0.15f, 0.65f, 1.0f },
    { "Color Noise", "Targets chroma speckle while leaving luminance texture mostly intact.", 0.55f, 5, 0.15f, 0.82f, 0.72f, 0.62f, 0.85f, 0.35f, 0.60f, 1.0f },
    { "High ISO", "Balanced luma and chroma cleanup for noisy camera images.", 0.75f, 6, 0.50f, 0.82f, 0.70f, 0.58f, 0.74f, 0.55f, 0.55f, 1.0f },
    { "Shadow Cleanup", "More help in dark regions without washing out highlights.", 0.85f, 7, 0.48f, 0.88f, 0.68f, 0.55f, 0.70f, 0.85f, 0.70f, 1.0f },
    { "Texture Preserve", "Conservative pass for drawings, scans, and detail-heavy work.", 0.35f, 4, 0.28f, 0.58f, 0.84f, 0.76f, 0.92f, 0.25f, 0.72f, 1.0f },
    { "Strong Cleanup", "Heavy smoothing for difficult inputs; check detail loss with preview modes.", 0.95f, 9, 0.68f, 0.95f, 0.60f, 0.50f, 0.62f, 0.70f, 0.50f, 1.0f }
};

int NormalizePreviewMode(int mode) {
    return std::clamp(mode, 0, kPreviewModeCount - 1);
}

const char* PreviewModeLabel(int mode) {
    switch (NormalizePreviewMode(mode)) {
    case kPreviewOriginal: return "Original";
    case kPreviewDifference: return "Difference";
    case kPreviewSplit: return "Split View";
    case kPreviewLumaDifference: return "Luma Difference";
    case kPreviewChromaDifference: return "Chroma Difference";
    case kPreviewNoiseMap: return "Noise Map";
    case kPreviewProtectedDetail: return "Protected Detail";
    case kPreviewDenoised:
    default:
        return "Denoised";
    }
}

const char* PreviewModeToken(int mode) {
    switch (NormalizePreviewMode(mode)) {
    case kPreviewOriginal: return "original";
    case kPreviewDifference: return "difference";
    case kPreviewSplit: return "split";
    case kPreviewLumaDifference: return "lumaDifference";
    case kPreviewChromaDifference: return "chromaDifference";
    case kPreviewNoiseMap: return "noiseMap";
    case kPreviewProtectedDetail: return "protectedDetail";
    case kPreviewDenoised:
    default:
        return "denoised";
    }
}

int PreviewModeFromToken(const std::string& token) {
    if (token == "original") return kPreviewOriginal;
    if (token == "difference") return kPreviewDifference;
    if (token == "split" || token == "splitView") return kPreviewSplit;
    if (token == "lumaDifference") return kPreviewLumaDifference;
    if (token == "chromaDifference") return kPreviewChromaDifference;
    if (token == "noiseMap") return kPreviewNoiseMap;
    if (token == "protectedDetail") return kPreviewProtectedDetail;
    return kPreviewDenoised;
}

} // namespace

SceneDenoiseLayer::~SceneDenoiseLayer() {
    if (m_ShaderProgram) {
        glDeleteProgram(m_ShaderProgram);
    }
}

void SceneDenoiseLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kSceneDenoiseVert, kSceneDenoiseFrag);
}

void SceneDenoiseLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uRes"), static_cast<float>(width), static_cast<float>(height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uEnabled"), m_DenoiseEnabled ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uRadius"), std::clamp(m_Radius, 1, 12));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLumaStrength"), std::clamp(m_LumaStrength, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uChromaStrength"), std::clamp(m_ChromaStrength, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uEdgeProtection"), std::clamp(m_EdgeProtection, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uChromaEdgeProtection"), std::clamp(m_ChromaEdgeProtection, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uDetailPreservation"), std::clamp(m_DetailPreservation, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShadowBoost"), std::clamp(m_ShadowBoost, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uHighlightProtection"), std::clamp(m_HighlightProtection, 0.0f, 1.0f));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlend"), std::clamp(m_Blend, 0.0f, 1.0f));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uAutoAnalyze"), m_AutoAnalyze ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uAutoAmount"), std::clamp(m_AutoAmount, 0.0f, 1.5f));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPreviewMode"), NormalizePreviewMode(m_PreviewMode));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uDifferenceAmount"), std::max(0.0f, m_DifferenceAmount));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSplitPosition"), std::clamp(m_SplitPosition, 0.02f, 0.98f));
    quad.Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void SceneDenoiseLayer::RenderUI() {
    RenderUI(nullptr);
}

void SceneDenoiseLayer::RenderUI(EditorModule* editor) {
    (void)editor;
    RenderCompactSummary();
}

NodeSurfaceSpec SceneDenoiseLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 420.0f;
    spec.maxWidth = 520.0f;
    return spec;
}

void SceneDenoiseLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    const float controlWidth = std::max(220.0f, context.safeContentWidth);
    RenderControls(editor, controlWidth);
}

void SceneDenoiseLayer::ClampParameters() {
    m_Radius = std::clamp(m_Radius, 1, 12);
    m_LumaStrength = std::clamp(m_LumaStrength, 0.0f, 1.0f);
    m_ChromaStrength = std::clamp(m_ChromaStrength, 0.0f, 1.0f);
    m_EdgeProtection = std::clamp(m_EdgeProtection, 0.0f, 1.0f);
    m_ChromaEdgeProtection = std::clamp(m_ChromaEdgeProtection, 0.0f, 1.0f);
    m_DetailPreservation = std::clamp(m_DetailPreservation, 0.0f, 1.0f);
    m_ShadowBoost = std::clamp(m_ShadowBoost, 0.0f, 1.0f);
    m_HighlightProtection = std::clamp(m_HighlightProtection, 0.0f, 1.0f);
    m_Blend = std::clamp(m_Blend, 0.0f, 1.0f);
    m_AutoAmount = std::clamp(m_AutoAmount, 0.0f, 1.5f);
    m_PreviewMode = NormalizePreviewMode(m_PreviewMode);
    m_DifferenceAmount = std::clamp(m_DifferenceAmount, 0.0f, 8.0f);
    m_SplitPosition = std::clamp(m_SplitPosition, 0.02f, 0.98f);
}

void SceneDenoiseLayer::RenderCompactSummary() const {
    char lineA[96];
    std::snprintf(
        lineA,
        sizeof(lineA),
        "%s  R%d  %s",
        m_DenoiseEnabled ? "Enabled" : "Bypassed",
        std::clamp(m_Radius, 1, 12),
        m_AutoAnalyze ? "Auto" : PreviewModeLabel(m_PreviewMode));
    ImGui::TextDisabled("%s", lineA);

    char lineB[96];
    std::snprintf(
        lineB,
        sizeof(lineB),
        "L %.2f  C %.2f  Detail %.2f",
        std::clamp(m_LumaStrength, 0.0f, 1.0f),
        std::clamp(m_ChromaStrength, 0.0f, 1.0f),
        std::clamp(m_DetailPreservation, 0.0f, 1.0f));
    ImGui::TextDisabled("%s", lineB);
}

bool SceneDenoiseLayer::RenderControls(EditorModule* editor, float controlWidth) {
    bool changed = false;
    ImGui::TextWrapped("Classical scene-linear denoise for removing luma and chroma noise before display compression.");
    if (editor && editor->SelectedLayerInputContainsViewTransform()) {
        ImGui::TextWrapped("Warning: this node appears after View Transform. Denoise works best before display compression.");
    }

    const int oldRadius = m_Radius;
    const float oldLumaStrength = m_LumaStrength;
    const float oldChromaStrength = m_ChromaStrength;
    const float oldEdgeProtection = m_EdgeProtection;
    const float oldChromaEdgeProtection = m_ChromaEdgeProtection;
    const float oldDetailPreservation = m_DetailPreservation;
    const float oldShadowBoost = m_ShadowBoost;
    const float oldHighlightProtection = m_HighlightProtection;
    const float oldBlend = m_Blend;
    const bool oldAutoAnalyze = m_AutoAnalyze;
    const float oldAutoAmount = m_AutoAmount;
    const int oldPreviewMode = m_PreviewMode;
    const float oldDifferenceAmount = m_DifferenceAmount;
    const float oldSplitPosition = m_SplitPosition;

    changed |= RenderPresetSection(controlWidth);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    changed |= RenderAutoSection(controlWidth);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    changed |= RenderCoreSection(controlWidth);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    changed |= RenderProtectionSection(controlWidth);
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    changed |= RenderPreviewSection(controlWidth);

    ClampParameters();
    changed = changed ||
        oldRadius != m_Radius ||
        oldLumaStrength != m_LumaStrength ||
        oldChromaStrength != m_ChromaStrength ||
        oldEdgeProtection != m_EdgeProtection ||
        oldChromaEdgeProtection != m_ChromaEdgeProtection ||
        oldDetailPreservation != m_DetailPreservation ||
        oldShadowBoost != m_ShadowBoost ||
        oldHighlightProtection != m_HighlightProtection ||
        oldBlend != m_Blend ||
        oldAutoAnalyze != m_AutoAnalyze ||
        oldAutoAmount != m_AutoAmount ||
        oldPreviewMode != m_PreviewMode ||
        oldDifferenceAmount != m_DifferenceAmount ||
        oldSplitPosition != m_SplitPosition;
    if (changed && editor) {
        editor->MarkSelectedLayerRenderDirty();
    }
    return changed;
}

bool SceneDenoiseLayer::RenderPresetSection(float controlWidth) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("PRESETS", 4.0f);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::BeginCombo("##SceneDenoisePreset", "Choose preset...")) {
        for (int i = 0; i < IM_ARRAYSIZE(kSceneDenoisePresets); ++i) {
            if (ImGui::Selectable(kSceneDenoisePresets[i].name, false)) {
                ApplyPreset(i);
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", kSceneDenoisePresets[i].description);
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool SceneDenoiseLayer::RenderAutoSection(float controlWidth) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("AUTO ANALYSIS", 4.0f);
    changed |= ImGuiExtras::NodeCheckbox("Adaptive Strength", "##SceneDenoiseAutoAnalyze", &m_AutoAnalyze, controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Auto Amount", "##SceneDenoiseAutoAmount", &m_AutoAmount, 0.0f, 1.5f, "%.2f", controlWidth);
    ImGui::TextDisabled("Uses local noise and protected-detail estimates inside the shader.");
    return changed;
}

bool SceneDenoiseLayer::RenderCoreSection(float controlWidth) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("CORE", 4.0f);
    changed |= ImGuiExtras::NodeCheckbox("Enable", "##SceneDenoiseEnabled", &m_DenoiseEnabled, controlWidth);
    changed |= ImGuiExtras::NodeSliderInt("Radius", "##SceneDenoiseRadius", &m_Radius, 1, 12, "%d px", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Luminance Strength", "##SceneDenoiseLuma", &m_LumaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Chroma Strength", "##SceneDenoiseChroma", &m_ChromaStrength, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Blend", "##SceneDenoiseBlend", &m_Blend, 0.0f, 1.0f, "%.2f", controlWidth);
    return changed;
}

bool SceneDenoiseLayer::RenderProtectionSection(float controlWidth) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("PROTECTION", 4.0f);
    changed |= ImGuiExtras::NodeSliderFloat("Edge Protection", "##SceneDenoiseEdge", &m_EdgeProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Color Edge Protection", "##SceneDenoiseColorEdge", &m_ChromaEdgeProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Detail Preservation", "##SceneDenoiseDetail", &m_DetailPreservation, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Shadow Cleanup", "##SceneDenoiseShadowBoost", &m_ShadowBoost, 0.0f, 1.0f, "%.2f", controlWidth);
    changed |= ImGuiExtras::NodeSliderFloat("Highlight Protection", "##SceneDenoiseHighlightProtection", &m_HighlightProtection, 0.0f, 1.0f, "%.2f", controlWidth);
    return changed;
}

bool SceneDenoiseLayer::RenderPreviewSection(float controlWidth) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("PREVIEW", 4.0f);
    const char* previewLabels[] = {
        "Denoised",
        "Original",
        "Difference",
        "Split view",
        "Luma difference",
        "Chroma difference",
        "Noise map",
        "Protected detail"
    };
    changed |= ImGuiExtras::NodeCombo("Mode", "##SceneDenoisePreviewMode", &m_PreviewMode, previewLabels, IM_ARRAYSIZE(previewLabels), controlWidth);
    if (m_PreviewMode == kPreviewDifference || m_PreviewMode == kPreviewLumaDifference || m_PreviewMode == kPreviewChromaDifference) {
        changed |= ImGuiExtras::NodeSliderFloat("Difference Amount", "##SceneDenoiseDifferenceAmount", &m_DifferenceAmount, 0.0f, 8.0f, "%.2f", controlWidth);
    }
    if (m_PreviewMode == kPreviewSplit) {
        changed |= ImGuiExtras::NodeSliderFloat("Split Position", "##SceneDenoiseSplitPosition", &m_SplitPosition, 0.02f, 0.98f, "%.2f", controlWidth);
    }
    return changed;
}

void SceneDenoiseLayer::ApplyPreset(int presetIndex) {
    if (presetIndex < 0 || presetIndex >= IM_ARRAYSIZE(kSceneDenoisePresets)) {
        return;
    }
    const SceneDenoisePresetDefinition& preset = kSceneDenoisePresets[presetIndex];
    m_DenoiseEnabled = true;
    m_AutoAnalyze = true;
    m_AutoAmount = preset.autoAmount;
    m_Radius = preset.radius;
    m_LumaStrength = preset.lumaStrength;
    m_ChromaStrength = preset.chromaStrength;
    m_EdgeProtection = preset.edgeProtection;
    m_ChromaEdgeProtection = preset.chromaEdgeProtection;
    m_DetailPreservation = preset.detailPreservation;
    m_ShadowBoost = preset.shadowBoost;
    m_HighlightProtection = preset.highlightProtection;
    m_Blend = preset.blend;
    ClampParameters();
}

json SceneDenoiseLayer::Serialize() const {
    return {
        { "type", "SceneDenoise" },
        { "enabled", m_DenoiseEnabled },
        { "radius", m_Radius },
        { "lumaStrength", m_LumaStrength },
        { "chromaStrength", m_ChromaStrength },
        { "edgeProtection", m_EdgeProtection },
        { "chromaEdgeProtection", m_ChromaEdgeProtection },
        { "detailPreservation", m_DetailPreservation },
        { "shadowBoost", m_ShadowBoost },
        { "highlightProtection", m_HighlightProtection },
        { "blend", m_Blend },
        { "autoAnalyze", m_AutoAnalyze },
        { "autoAmount", m_AutoAmount },
        { "previewMode", PreviewModeToken(m_PreviewMode) },
        { "differenceAmount", m_DifferenceAmount },
        { "splitPosition", m_SplitPosition }
    };
}

void SceneDenoiseLayer::Deserialize(const json& j) {
    m_DenoiseEnabled = j.value("enabled", m_DenoiseEnabled);
    m_Radius = std::clamp(j.value("radius", m_Radius), 1, 12);
    m_LumaStrength = std::clamp(j.value("lumaStrength", m_LumaStrength), 0.0f, 1.0f);
    m_ChromaStrength = std::clamp(j.value("chromaStrength", m_ChromaStrength), 0.0f, 1.0f);
    m_EdgeProtection = std::clamp(j.value("edgeProtection", m_EdgeProtection), 0.0f, 1.0f);
    m_ChromaEdgeProtection = std::clamp(j.value("chromaEdgeProtection", m_ChromaEdgeProtection), 0.0f, 1.0f);
    m_DetailPreservation = std::clamp(j.value("detailPreservation", m_DetailPreservation), 0.0f, 1.0f);
    m_ShadowBoost = std::clamp(j.value("shadowBoost", m_ShadowBoost), 0.0f, 1.0f);
    m_HighlightProtection = std::clamp(j.value("highlightProtection", m_HighlightProtection), 0.0f, 1.0f);
    m_Blend = std::clamp(j.value("blend", m_Blend), 0.0f, 1.0f);
    m_AutoAnalyze = j.value("autoAnalyze", m_AutoAnalyze);
    m_AutoAmount = std::clamp(j.value("autoAmount", m_AutoAmount), 0.0f, 1.5f);
    if (auto it = j.find("previewMode"); it != j.end()) {
        if (it->is_string()) {
            m_PreviewMode = PreviewModeFromToken(it->get<std::string>());
        } else if (it->is_number_integer()) {
            m_PreviewMode = NormalizePreviewMode(it->get<int>());
        }
    }
    m_DifferenceAmount = std::clamp(j.value("differenceAmount", m_DifferenceAmount), 0.0f, 8.0f);
    m_SplitPosition = std::clamp(j.value("splitPosition", m_SplitPosition), 0.02f, 0.98f);
    ClampParameters();
}
