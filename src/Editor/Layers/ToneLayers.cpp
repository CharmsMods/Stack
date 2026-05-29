#include "ToneLayers.h"

#include "Editor/EditorModule.h"
#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <limits>

namespace {

const char* kToneVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kToneMapperFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uExposure;
uniform float uShoulder;
uniform float uToe;
uniform float uContrast;
uniform float uWhitePoint;
uniform float uBlackPoint;
uniform int uPreserveHue;

float lumaOf(vec3 rgb) {
    return max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
}

float toneMapLuma(float x) {
    x = max(0.0, x - uBlackPoint);
    x *= exp2(uExposure);
    x = pow(max(0.0, x), max(0.05, uContrast));
    float toeLift = max(0.0, uToe);
    x = (x + toeLift * x / (x + 0.18)) / (1.0 + toeLift);
    float shoulder = max(0.001, uShoulder);
    float whitePoint = max(0.001, uWhitePoint);
    float mapped = x / (x + shoulder);
    float whiteMapped = whitePoint / (whitePoint + shoulder);
    return clamp(mapped / max(0.001, whiteMapped), 0.0, 1.0);
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = max(vec3(0.0), color.rgb);
    float oldLuma = lumaOf(rgb);
    float newLuma = toneMapLuma(oldLuma);
    if (uPreserveHue != 0) {
        rgb = oldLuma > 0.00001 ? rgb * (newLuma / oldLuma) : vec3(newLuma);
    } else {
        rgb.r = toneMapLuma(rgb.r);
        rgb.g = toneMapLuma(rgb.g);
        rgb.b = toneMapLuma(rgb.b);
    }
    FragColor = vec4(max(vec3(0.0), rgb), color.a);
}
)";

const char* kToneCurveFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uCurveLut;
uniform int uMode;
uniform int uDomain;
uniform float uLogMinEv;
uniform float uLogMaxEv;
uniform float uMiddleGrey;

float lumaOf(vec3 rgb) {
    return max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
}

float curve(float x) {
    return texture(uCurveLut, vec2(clamp(x, 0.0, 1.0), 0.5)).r;
}

float sceneToCurveCoord(float x) {
    if (uDomain == 1) {
        float ev = log2(max(x, 0.000001) / max(uMiddleGrey, 0.000001));
        return clamp((ev - uLogMinEv) / max(0.0001, uLogMaxEv - uLogMinEv), 0.0, 1.0);
    }
    return clamp(x, 0.0, 1.0);
}

float curveCoordToScene(float coord) {
    if (uDomain == 1) {
        float ev = mix(uLogMinEv, uLogMaxEv, clamp(coord, 0.0, 1.0));
        return max(uMiddleGrey, 0.000001) * exp2(ev);
    }
    return coord;
}

float toneResponse(float x) {
    float coord = sceneToCurveCoord(x);
    return curveCoordToScene(curve(coord));
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = color.rgb;
    if (uMode == 0) {
        float oldLuma = lumaOf(rgb);
        float newLuma = toneResponse(oldLuma);
        float gain = newLuma / max(oldLuma, 0.000001);
        rgb = rgb * gain;
    } else if (uMode == 1) {
        rgb = vec3(toneResponse(rgb.r), toneResponse(rgb.g), toneResponse(rgb.b));
    } else if (uMode == 2) {
        rgb.r = toneResponse(rgb.r);
    } else if (uMode == 3) {
        rgb.g = toneResponse(rgb.g);
    } else if (uMode == 4) {
        rgb.b = toneResponse(rgb.b);
    }
    FragColor = vec4(rgb, color.a);
}
)";

const char* kToneEqualizerFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uShadowsEv;
uniform float uDarksEv;
uniform float uMidtonesEv;
uniform float uLightsEv;
uniform float uHighlightsEv;
uniform float uMiddleGrey;
uniform float uRange;
uniform int uPreserveHue;

float lumaOf(vec3 rgb) {
    return max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
}

float gaussian(float x, float center, float width) {
    float d = (x - center) / max(0.001, width);
    return exp(-0.5 * d * d);
}

float gainEvFor(float ev) {
    float width = max(0.25, uRange);
    float w0 = gaussian(ev, -2.0 * width, width);
    float w1 = gaussian(ev, -1.0 * width, width);
    float w2 = gaussian(ev,  0.0, width);
    float w3 = gaussian(ev,  1.0 * width, width);
    float w4 = gaussian(ev,  2.0 * width, width);
    float sum = max(0.0001, w0 + w1 + w2 + w3 + w4);
    return (uShadowsEv * w0 + uDarksEv * w1 + uMidtonesEv * w2 + uLightsEv * w3 + uHighlightsEv * w4) / sum;
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = max(vec3(0.0), color.rgb);
    float oldLuma = lumaOf(rgb);
    float ev = log2(max(0.000001, oldLuma) / max(0.000001, uMiddleGrey));
    float newLuma = oldLuma * exp2(gainEvFor(ev));
    if (uPreserveHue != 0) {
        rgb = oldLuma > 0.00001 ? rgb * (newLuma / oldLuma) : vec3(newLuma);
    } else {
        rgb *= exp2(gainEvFor(ev));
    }
    FragColor = vec4(max(vec3(0.0), rgb), color.a);
}
)";

const char* kViewTransformFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uExposure;
uniform float uBlackEv;
uniform float uWhiteEv;
uniform float uMiddleGrey;
uniform float uShoulder;
uniform float uToe;
uniform float uContrast;
uniform float uSaturation;
uniform int uPreserveHue;
uniform int uDebugFalseColor;

float lumaOf(vec3 rgb) {
    return max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
}

vec3 falseColor(float ev) {
    if (ev < -4.0) return vec3(0.02, 0.05, 0.24);
    if (ev < -2.0) return vec3(0.0, 0.34, 0.85);
    if (ev < 0.0) return vec3(0.0, 0.65, 0.45);
    if (ev < 2.0) return vec3(0.82, 0.72, 0.18);
    if (ev < 4.0) return vec3(1.0, 0.42, 0.08);
    return vec3(1.0, 0.06, 0.04);
}

float filmicCurve(float x) {
    float black = uMiddleGrey * exp2(uBlackEv);
    float white = uMiddleGrey * exp2(uWhiteEv);
    x = max(0.0, x * exp2(uExposure) - black);
    float norm = x / max(0.000001, white - black);
    norm = pow(max(0.0, norm), max(0.05, uContrast));
    float toe = clamp(uToe, 0.0, 1.0);
    norm = mix(norm, (norm + toe * norm / (norm + 0.18)) / (1.0 + toe), toe);
    float shoulder = max(0.001, uShoulder);
    float mapped = norm / (norm + shoulder);
    float whiteMapped = 1.0 / (1.0 + shoulder);
    return clamp(mapped / max(0.0001, whiteMapped), 0.0, 1.0);
}

vec3 compressDisplayGamut(vec3 rgb) {
    float luma = clamp(lumaOf(rgb), 0.0, 1.0);
    float maxChannel = max(rgb.r, max(rgb.g, rgb.b));
    float minChannel = min(rgb.r, min(rgb.g, rgb.b));
    float amount = 0.0;
    if (maxChannel > 1.0) {
        amount = max(amount, (maxChannel - 1.0) / max(0.00001, maxChannel - luma));
    }
    if (minChannel < 0.0) {
        amount = max(amount, (0.0 - minChannel) / max(0.00001, luma - minChannel));
    }
    float highlightChroma = smoothstep(0.68, 1.0, luma) * smoothstep(0.92, 1.35, maxChannel);
    amount = clamp(max(amount, highlightChroma * 0.45), 0.0, 1.0);
    return mix(rgb, vec3(luma), amount);
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = max(vec3(0.0), color.rgb);
    float oldLuma = lumaOf(rgb);
    if (uDebugFalseColor != 0) {
        float ev = log2(max(0.000001, oldLuma) / max(0.000001, uMiddleGrey));
        FragColor = vec4(falseColor(ev), color.a);
        return;
    }
    float newLuma = filmicCurve(oldLuma);
    if (uPreserveHue != 0) {
        rgb = oldLuma > 0.00001 ? rgb * (newLuma / oldLuma) : vec3(newLuma);
    } else {
        rgb = vec3(filmicCurve(rgb.r), filmicCurve(rgb.g), filmicCurve(rgb.b));
    }
    rgb = compressDisplayGamut(rgb);
    float luma = lumaOf(rgb);
    rgb = mix(vec3(luma), rgb, max(0.0, uSaturation));
    rgb = compressDisplayGamut(rgb);
    FragColor = vec4(clamp(rgb, 0.0, 1.0), color.a);
}
)";

const char* kShadowsHighlightsFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uInputTex;
uniform float uShadows;
uniform float uHighlights;
uniform float uWhites;
uniform float uBlacks;
uniform float uMidtoneContrast;

float lumaOf(vec3 rgb) {
    return max(0.0, dot(rgb, vec3(0.2126, 0.7152, 0.0722)));
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = max(vec3(0.0), color.rgb);
    float oldLuma = lumaOf(rgb);
    float luma = oldLuma;

    float shadowMask = 1.0 - smoothstep(0.0, 0.55, oldLuma);
    float highlightMask = smoothstep(0.45, 1.0, oldLuma);
    luma += uShadows * shadowMask * (1.0 - exp(-max(0.0, 1.0 - oldLuma) * 2.0));
    luma -= uHighlights * highlightMask * oldLuma * 0.75;
    luma += uWhites * smoothstep(0.72, 1.0, oldLuma) * 0.5;
    luma += uBlacks * (1.0 - smoothstep(0.0, 0.28, oldLuma)) * 0.5;
    luma = (luma - 0.5) * (1.0 + uMidtoneContrast) + 0.5;
    luma = max(0.0, luma);

    rgb = oldLuma > 0.00001 ? rgb * (luma / oldLuma) : vec3(luma);
    FragColor = vec4(max(vec3(0.0), rgb), color.a);
}
)";

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

constexpr int kToneCurveMaxPoints = 12;
constexpr float kToneCurveHitRadius = 22.0f;

struct CurveGraphRect {
    ImVec2 min;
    ImVec2 max;

    float Width() const { return max.x - min.x; }
    float Height() const { return max.y - min.y; }
};

ImVec2 CurveToScreen(const CurveGraphRect& graphRect, const ToneCurvePoint& point) {
    return ImVec2(
        graphRect.min.x + point.x * graphRect.Width(),
        graphRect.max.y - point.y * graphRect.Height());
}

ToneCurvePoint ScreenToCurve(const CurveGraphRect& graphRect, const ImVec2& screen) {
    return {
        Clamp01((screen.x - graphRect.min.x) / std::max(1.0f, graphRect.Width())),
        Clamp01((graphRect.max.y - screen.y) / std::max(1.0f, graphRect.Height()))
    };
}

void RenderToneMapperControls(
    float& exposure,
    float& shoulder,
    float& toe,
    float& contrast,
    float& whitePoint,
    float& blackPoint,
    bool& preserveHue,
    float controlWidth) {
    ImGuiExtras::NodeSliderFloat("Exposure Comp", "##ToneMapExposure", &exposure, -5.0f, 5.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Highlight Rolloff", "##ToneMapShoulder", &shoulder, 0.05f, 4.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Shadow Lift / Toe", "##ToneMapToe", &toe, 0.0f, 1.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Contrast", "##ToneMapContrast", &contrast, 0.25f, 2.5f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("White Point", "##ToneMapWhite", &whitePoint, 0.5f, 16.0f, "%.2f", controlWidth);
    ImGuiExtras::NodeSliderFloat("Black Point", "##ToneMapBlack", &blackPoint, 0.0f, 1.0f, "%.3f", controlWidth);
    ImGuiExtras::NodeCheckbox("Preserve Hue", "##ToneMapPreserveHue", &preserveHue, controlWidth);
}

} // namespace

ToneMapperLayer::ToneMapperLayer() = default;

ToneMapperLayer::~ToneMapperLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ToneMapperLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kToneMapperFrag);
}

void ToneMapperLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uExposure"), m_Exposure);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShoulder"), m_Shoulder);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uToe"), m_Toe);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uContrast"), m_Contrast);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uWhitePoint"), m_WhitePoint);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlackPoint"), m_BlackPoint);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPreserveHue"), m_PreserveHue ? 1 : 0);
    quad.Draw();
    glUseProgram(0);
}

void ToneMapperLayer::RenderUI() {
    ImGui::TextDisabled("Double-click for tone controls.");
}

NodeSurfaceSpec ToneMapperLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 430.0f;
    spec.maxWidth = 500.0f;
    return spec;
}

void ToneMapperLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    (void)editor;
    ImGuiExtras::RichSectionLabel("Tone Mapper / Filmic");
    ImGui::TextDisabled("Compress scene-linear range while preserving hue.");
    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    RenderToneMapperControls(
        m_Exposure,
        m_Shoulder,
        m_Toe,
        m_Contrast,
        m_WhitePoint,
        m_BlackPoint,
        m_PreserveHue,
        context.safeContentWidth);
}

json ToneMapperLayer::Serialize() const {
    return json{
        { "type", "ToneMapper" },
        { "exposure", m_Exposure },
        { "shoulder", m_Shoulder },
        { "toe", m_Toe },
        { "contrast", m_Contrast },
        { "whitePoint", m_WhitePoint },
        { "blackPoint", m_BlackPoint },
        { "preserveHue", m_PreserveHue }
    };
}

void ToneMapperLayer::Deserialize(const json& j) {
    if (j.contains("exposure")) m_Exposure = j["exposure"];
    if (j.contains("shoulder")) m_Shoulder = j["shoulder"];
    if (j.contains("toe")) m_Toe = j["toe"];
    if (j.contains("contrast")) m_Contrast = j["contrast"];
    if (j.contains("whitePoint")) m_WhitePoint = j["whitePoint"];
    if (j.contains("blackPoint")) m_BlackPoint = j["blackPoint"];
    if (j.contains("preserveHue")) m_PreserveHue = j["preserveHue"];
}

ToneCurveLayer::ToneCurveLayer() {
    ResetLinear();
}

ToneCurveLayer::~ToneCurveLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
    if (m_LutTexture) glDeleteTextures(1, &m_LutTexture);
}

void ToneCurveLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kToneCurveFrag);
}

void ToneCurveLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;
    UpdateLut();
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_LutTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uCurveLut"), 1);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uMode"), static_cast<int>(m_Mode));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uDomain"), static_cast<int>(m_Domain));
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLogMinEv"), m_LogMinEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLogMaxEv"), m_LogMaxEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uMiddleGrey"), m_MiddleGrey);
    quad.Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void ToneCurveLayer::RenderUI() {
    ImGui::TextDisabled("Double-click for curve controls.");
}

NodeSurfaceSpec ToneCurveLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 460.0f;
    spec.maxWidth = 540.0f;
    return spec;
}

void ToneCurveLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    (void)editor;
    bool changed = false;
    ImGuiExtras::RichSectionLabel("Tone Curve");
    const char* modeLabels[] = { "Luminance", "RGB", "Red", "Green", "Blue" };
    int mode = static_cast<int>(m_Mode);
    ImGui::SetNextItemWidth(context.safeContentWidth);
    if (ImGui::Combo("Mode", &mode, modeLabels, 5)) {
        m_Mode = static_cast<ToneCurveMode>(std::clamp(mode, 0, 4));
        changed = true;
    }
    const char* domainLabels[] = { "Scene Linear", "Log Scene" };
    int domain = static_cast<int>(m_Domain);
    ImGui::SetNextItemWidth(context.safeContentWidth);
    if (ImGui::Combo("Curve Domain", &domain, domainLabels, 2)) {
        m_Domain = static_cast<ToneCurveDomain>(std::clamp(domain, 0, 1));
        changed = true;
    }
    ImGui::TextDisabled("Scene Linear edits literal values; Log Scene edits stops around middle grey.");
    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    if (ImGuiExtras::RichFullWidthButton("Linear", context.safeContentWidth * 0.31f)) {
        ResetLinear();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGuiExtras::RichFullWidthButton("Soft Contrast", context.safeContentWidth * 0.31f)) {
        ApplySoftContrastPreset();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGuiExtras::RichFullWidthButton("Highlight Roll-Off", context.safeContentWidth * 0.31f)) {
        ApplyFilmicShoulderPreset();
        changed = true;
    }
    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    if (ImGuiExtras::RichFullWidthButton("Strong S-Curve", context.safeContentWidth * 0.48f)) {
        ApplyStrongSCurvePreset();
        changed = true;
    }
    ImGui::SameLine();
    if (ImGuiExtras::RichFullWidthButton("Reset Curve", context.safeContentWidth * 0.48f)) {
        ResetLinear();
        changed = true;
    }

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("Curve Graph");
    ImGui::TextDisabled("Click to add points. Drag freely; crossed points reorder.");
    changed |= ImGuiExtras::NodeCheckbox("Free Endpoints", "##ToneCurveFreeEndpoints", &m_FreeEndpoints, context.safeContentWidth);
    const float graphSize = std::min(context.safeContentWidth, 390.0f * std::max(0.75f, context.layoutScale));
    RenderCurveEditor(graphSize, graphSize);
    changed = changed || m_LutDirty;

    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    if (m_SelectedPoint >= 0 && m_SelectedPoint < static_cast<int>(m_Points.size())) {
        const ToneCurvePoint& point = m_Points[static_cast<std::size_t>(m_SelectedPoint)];
        ImGui::TextDisabled("Selected: input %.3f  output %.3f", point.x, point.y);
        ImGui::SameLine();
        ImGui::BeginDisabled(m_Points.size() <= 2 || m_SelectedPoint == 0 || m_SelectedPoint == static_cast<int>(m_Points.size()) - 1);
        if (ImGui::Button("Delete Point")) {
            DeleteSelectedPoint();
            changed = true;
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("%zu / %d points", m_Points.size(), kToneCurveMaxPoints);
    }

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("Curve Domain");
    if (m_Domain == ToneCurveDomain::LogScene) {
        changed |= ImGuiExtras::NodeSliderFloat("Middle Grey", "##ToneCurveMiddleGrey", &m_MiddleGrey, 0.01f, 1.0f, "%.3f", context.safeContentWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Graph Black EV", "##ToneCurveLogMinEv", &m_LogMinEv, -20.0f, 0.0f, "%.2f", context.safeContentWidth);
        changed |= ImGuiExtras::NodeSliderFloat("Graph White EV", "##ToneCurveLogMaxEv", &m_LogMaxEv, 0.0f, 20.0f, "%.2f", context.safeContentWidth);
        if (m_LogMaxEv <= m_LogMinEv + 0.1f) {
            m_LogMaxEv = m_LogMinEv + 0.1f;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("Scene Linear maps 0-1 graph positions directly to scene values.");
    }
    ImGui::TextDisabled("Outputs unclamped scene-linear RGB. Use View Transform for display compression.");

    if (changed) {
        SanitizePoints();
        m_LutDirty = true;
    }
}

json ToneCurveLayer::Serialize() const {
    json points = json::array();
    for (const ToneCurvePoint& point : m_Points) {
        points.push_back({ { "x", point.x }, { "y", point.y } });
    }
    return json{
        { "type", "ToneCurve" },
        { "mode", static_cast<int>(m_Mode) },
        { "domain", static_cast<int>(m_Domain) },
        { "points", points },
        { "freeEndpoints", m_FreeEndpoints },
        { "logMinEv", m_LogMinEv },
        { "logMaxEv", m_LogMaxEv },
        { "middleGrey", m_MiddleGrey }
    };
}

void ToneCurveLayer::Deserialize(const json& j) {
    const std::string type = j.value("type", std::string("ToneCurve"));
    if (type != "ToneCurve") {
        ResetLinear();
        return;
    }
    if (j.contains("mode")) {
        m_Mode = static_cast<ToneCurveMode>(std::clamp(j["mode"].get<int>(), 0, 4));
    }
    if (j.contains("domain")) {
        m_Domain = static_cast<ToneCurveDomain>(std::clamp(j["domain"].get<int>(), 0, 1));
    }
    if (j.contains("points") && j["points"].is_array()) {
        m_Points.clear();
        for (const json& item : j["points"]) {
            if (!item.is_object()) continue;
            m_Points.push_back({ item.value("x", 0.0f), item.value("y", 0.0f) });
        }
        if (m_Points.size() < 2) {
            ResetLinear();
        }
        SanitizePoints();
        m_LutDirty = true;
    }
    m_FreeEndpoints = j.value("freeEndpoints", m_FreeEndpoints);
    m_LogMinEv = j.value("logMinEv", m_LogMinEv);
    m_LogMaxEv = j.value("logMaxEv", m_LogMaxEv);
    m_MiddleGrey = j.value("middleGrey", m_MiddleGrey);
    m_LutDirty = true;
}

void ToneCurveLayer::ResetLinear() {
    m_Points = { { 0.0f, 0.0f }, { 1.0f, 1.0f } };
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ResetToneShape() {
    m_Shoulder = 0.55f;
    m_Toe = 0.18f;
    m_Contrast = 1.0f;
    m_LutDirty = true;
}

void ToneCurveLayer::ResetDynamicRange() {
    m_Shadows = 0.0f;
    m_Highlights = 0.0f;
    m_Whites = 0.0f;
    m_Blacks = 0.0f;
    m_MidtoneContrast = 0.0f;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplySoftContrastPreset() {
    m_Points = { { 0.0f, 0.0f }, { 0.25f, 0.18f }, { 0.5f, 0.5f }, { 0.75f, 0.82f }, { 1.0f, 1.0f } };
    m_SelectedPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplyFilmicShoulderPreset() {
    m_Points = { { 0.0f, 0.0f }, { 0.18f, 0.16f }, { 0.5f, 0.56f }, { 0.82f, 0.9f }, { 1.0f, 0.98f } };
    m_Domain = ToneCurveDomain::LogScene;
    m_SelectedPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplyStrongSCurvePreset() {
    m_Points = { { 0.0f, 0.0f }, { 0.18f, 0.08f }, { 0.42f, 0.36f }, { 0.62f, 0.72f }, { 0.86f, 0.95f }, { 1.0f, 1.0f } };
    m_SelectedPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::SanitizePoints() {
    for (ToneCurvePoint& point : m_Points) {
        point.x = Clamp01(point.x);
        point.y = Clamp01(point.y);
    }
    std::sort(m_Points.begin(), m_Points.end(), [](const ToneCurvePoint& a, const ToneCurvePoint& b) {
        return a.x < b.x;
    });
    if (m_Points.empty()) {
        ResetLinear();
        return;
    }
    if (m_Points.size() == 1) {
        m_Points.push_back({ 1.0f, m_Points.front().y });
    }
    if (m_Points.size() > kToneCurveMaxPoints) {
        m_Points.resize(kToneCurveMaxPoints);
    }
    if (!m_FreeEndpoints && m_Points.size() >= 2) {
        m_Points.front().x = 0.0f;
        m_Points.back().x = 1.0f;
    }
    if (m_SelectedPoint >= static_cast<int>(m_Points.size())) {
        m_SelectedPoint = -1;
    }
}

void ToneCurveLayer::AddPointAt(float x, float y) {
    if (m_Points.size() >= kToneCurveMaxPoints) {
        return;
    }
    m_Points.push_back({ Clamp01(x), Clamp01(y) });
    SanitizePoints();
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(m_Points.size()); ++i) {
        const float dx = m_Points[static_cast<std::size_t>(i)].x - x;
        const float dy = m_Points[static_cast<std::size_t>(i)].y - y;
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    m_SelectedPoint = bestIndex;
    m_LutDirty = true;
}

void ToneCurveLayer::DeleteSelectedPoint() {
    if (m_SelectedPoint < 0 || m_SelectedPoint >= static_cast<int>(m_Points.size()) || m_Points.size() <= 2) {
        return;
    }
    if (m_SelectedPoint == 0 || m_SelectedPoint == static_cast<int>(m_Points.size()) - 1) {
        return;
    }
    m_Points.erase(m_Points.begin() + m_SelectedPoint);
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    SanitizePoints();
    m_LutDirty = true;
}

void ToneCurveLayer::MovePoint(int index, float x, float y) {
    if (index < 0 || index >= static_cast<int>(m_Points.size())) {
        return;
    }
    float nextX = Clamp01(x);
    if (!m_FreeEndpoints && index == 0) {
        nextX = 0.0f;
    } else if (!m_FreeEndpoints && index == static_cast<int>(m_Points.size()) - 1) {
        nextX = 1.0f;
    }
    m_Points[static_cast<std::size_t>(index)] = { nextX, Clamp01(y) };
    SanitizePoints();
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(m_Points.size()); ++i) {
        const float dx = m_Points[static_cast<std::size_t>(i)].x - nextX;
        const float dy = m_Points[static_cast<std::size_t>(i)].y - Clamp01(y);
        const float distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    m_SelectedPoint = bestIndex;
    m_DraggingPoint = bestIndex;
    m_LutDirty = true;
}

void ToneCurveLayer::RenderCurveEditor(float width, float height) {
    SanitizePoints();
    ImGui::PushID("ToneCurveGraph");
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(160.0f, width), std::max(160.0f, height));
    ImGui::InvisibleButton("canvas", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const CurveGraphRect rect { start, ImVec2(start.x + size.x, start.y + size.y) };
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 bg = IM_COL32(16, 24, 28, 255);
    const ImU32 border = IM_COL32(110, 135, 145, 180);
    const ImU32 grid = IM_COL32(90, 115, 125, 58);
    const ImU32 diagonal = IM_COL32(150, 165, 170, 85);
    const ImU32 curveColor = IM_COL32(118, 190, 255, 245);
    const ImU32 finalCurveColor = IM_COL32(102, 238, 191, 245);
    const ImU32 pointColor = IM_COL32(235, 240, 242, 255);
    const ImU32 selectedColor = IM_COL32(75, 175, 255, 255);

    drawList->AddRectFilled(rect.min, rect.max, bg, 6.0f);
    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = rect.min.x + rect.Width() * t;
        const float y = rect.min.y + rect.Height() * t;
        drawList->AddLine(ImVec2(x, rect.min.y), ImVec2(x, rect.max.y), grid, 1.0f);
        drawList->AddLine(ImVec2(rect.min.x, y), ImVec2(rect.max.x, y), grid, 1.0f);
    }
    drawList->AddLine(ImVec2(rect.min.x, rect.max.y), ImVec2(rect.max.x, rect.min.y), diagonal, 1.0f);

    ImVec2 previous = CurveToScreen(rect, { 0.0f, EvaluateCurve(0.0f) });
    for (int i = 1; i <= 96; ++i) {
        const float x = static_cast<float>(i) / 96.0f;
        const ImVec2 current = CurveToScreen(rect, { x, Clamp01(EvaluateCurve(x)) });
        drawList->AddLine(previous, current, curveColor, 2.25f);
        previous = current;
    }
    if (m_ShowFinalCurve) {
        previous = CurveToScreen(rect, { 0.0f, Clamp01(EvaluateFinalCurve(0.0f)) });
        for (int i = 1; i <= 96; ++i) {
            const float x = static_cast<float>(i) / 96.0f;
            const ImVec2 current = CurveToScreen(rect, { x, Clamp01(EvaluateFinalCurve(x)) });
            drawList->AddLine(previous, current, finalCurveColor, 1.65f);
            previous = current;
        }
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoveredPoint = -1;
    float bestDist2 = kToneCurveHitRadius * kToneCurveHitRadius;
    for (int i = 0; i < static_cast<int>(m_Points.size()); ++i) {
        const ImVec2 point = CurveToScreen(rect, m_Points[static_cast<std::size_t>(i)]);
        const float dx = point.x - mouse.x;
        const float dy = point.y - mouse.y;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 <= bestDist2) {
            bestDist2 = dist2;
            hoveredPoint = i;
        }
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredPoint >= 0) {
            m_SelectedPoint = hoveredPoint;
            m_DraggingPoint = hoveredPoint;
        } else {
            const ToneCurvePoint point = ScreenToCurve(rect, mouse);
            AddPointAt(point.x, point.y);
            m_DraggingPoint = m_SelectedPoint;
        }
    }
    if (active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_DraggingPoint >= 0) {
        const ToneCurvePoint point = ScreenToCurve(rect, mouse);
        MovePoint(m_DraggingPoint, point.x, point.y);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_DraggingPoint = -1;
    }

    for (int i = 0; i < static_cast<int>(m_Points.size()); ++i) {
        const ImVec2 point = CurveToScreen(rect, m_Points[static_cast<std::size_t>(i)]);
        const bool selected = i == m_SelectedPoint;
        drawList->AddCircleFilled(point, selected ? 8.5f : 7.0f, selected ? selectedColor : pointColor, 24);
        drawList->AddCircle(point, selected ? 10.0f : 8.5f, IM_COL32(0, 0, 0, 145), 24, 1.35f);
    }

    drawList->AddRect(rect.min, rect.max, border, 6.0f, 0, 1.2f);
    ImGui::PopID();
}

float ToneCurveLayer::EvaluateCurve(float x) const {
    if (m_Points.empty()) return x;
    if (x <= m_Points.front().x) return m_Points.front().y;
    for (std::size_t i = 1; i < m_Points.size(); ++i) {
        const ToneCurvePoint& a = m_Points[i - 1];
        const ToneCurvePoint& b = m_Points[i];
        if (x <= b.x) {
            const float t = (x - a.x) / std::max(0.0001f, b.x - a.x);
            const float smoothT = t * t * (3.0f - 2.0f * t);
            return a.y + (b.y - a.y) * smoothT;
        }
    }
    return m_Points.back().y;
}

float ToneCurveLayer::EvaluateFinalCurve(float x) const {
    float y = Clamp01(EvaluateCurve(x));
    if (m_EnableFilmic) {
        y = std::pow(std::max(0.0f, y), std::max(0.05f, m_Contrast));
        const float toe = Clamp01(m_Toe);
        if (toe > 0.0001f) {
            const float lifted = (y + toe * y / (y + 0.18f)) / (1.0f + toe);
            y = y + (lifted - y) * toe;
        }
        const float shoulder = std::max(0.001f, m_Shoulder);
        const float whitePoint = std::max(0.001f, m_WhitePoint);
        const float scene = y * whitePoint;
        const float mapped = scene / (scene + shoulder);
        const float whiteMapped = whitePoint / (whitePoint + shoulder);
        y = mapped / std::max(0.0001f, whiteMapped);
    }
    if (m_EnableDynamicRange) {
        const float shadowMask = 1.0f - std::clamp((x - 0.0f) / 0.55f, 0.0f, 1.0f);
        const float highlightMask = std::clamp((x - 0.45f) / 0.55f, 0.0f, 1.0f);
        y += m_Shadows * shadowMask * (1.0f - std::exp(-std::max(0.0f, 1.0f - y) * 2.0f));
        y -= m_Highlights * highlightMask * y * 0.75f;
        y += m_Whites * std::clamp((x - 0.72f) / 0.28f, 0.0f, 1.0f) * 0.5f;
        y += m_Blacks * (1.0f - std::clamp(x / 0.28f, 0.0f, 1.0f)) * 0.5f;
        y = (y - 0.5f) * (1.0f + m_MidtoneContrast) + 0.5f;
    }
    return Clamp01(y);
}

void ToneCurveLayer::UpdateLut() {
    if (!m_LutDirty && m_LutTexture != 0) {
        return;
    }
    std::array<float, 256 * 4> lut {};
    for (std::size_t i = 0; i < lut.size(); ++i) {
        const std::size_t pixel = i / 4;
        const float value = Clamp01(EvaluateCurve(static_cast<float>(pixel) / 255.0f));
        lut[i] = value;
    }
    if (m_LutTexture == 0) {
        glGenTextures(1, &m_LutTexture);
    }
    glBindTexture(GL_TEXTURE_2D, m_LutTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 256, 1, 0, GL_RGBA, GL_FLOAT, lut.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_LutDirty = false;
}

ToneEqualizerLayer::ToneEqualizerLayer() = default;

ToneEqualizerLayer::~ToneEqualizerLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ToneEqualizerLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kToneEqualizerFrag);
}

void ToneEqualizerLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShadowsEv"), m_ShadowsEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uDarksEv"), m_DarksEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uMidtonesEv"), m_MidtonesEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLightsEv"), m_LightsEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uHighlightsEv"), m_HighlightsEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uMiddleGrey"), m_MiddleGrey);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uRange"), m_Range);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPreserveHue"), m_PreserveHue ? 1 : 0);
    quad.Draw();
    glUseProgram(0);
}

void ToneEqualizerLayer::RenderUI() {
    ImGui::TextDisabled("Double-click for dynamic exposure controls.");
}

NodeSurfaceSpec ToneEqualizerLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 430.0f;
    spec.maxWidth = 520.0f;
    return spec;
}

void ToneEqualizerLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    (void)editor;
    ImGuiExtras::RichSectionLabel("Tone Equalizer / Dynamic Exposure");
    ImGui::TextDisabled("Scene-linear EV gain by luminance range. Output remains unclamped.");
    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::NodeSliderFloat("Shadows EV", "##ToneEqShadows", &m_ShadowsEv, -4.0f, 4.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Darks EV", "##ToneEqDarks", &m_DarksEv, -4.0f, 4.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Midtones EV", "##ToneEqMidtones", &m_MidtonesEv, -4.0f, 4.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Lights EV", "##ToneEqLights", &m_LightsEv, -4.0f, 4.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Highlights EV", "##ToneEqHighlights", &m_HighlightsEv, -4.0f, 4.0f, "%.2f", context.safeContentWidth);
    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::NodeSliderFloat("Middle Grey", "##ToneEqMiddleGrey", &m_MiddleGrey, 0.01f, 1.0f, "%.3f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Band Width", "##ToneEqRange", &m_Range, 0.5f, 8.0f, "%.2f EV", context.safeContentWidth);
    ImGuiExtras::NodeCheckbox("Preserve Hue", "##ToneEqPreserveHue", &m_PreserveHue, context.safeContentWidth);
}

json ToneEqualizerLayer::Serialize() const {
    return json{
        { "type", "ToneEqualizer" },
        { "shadowsEv", m_ShadowsEv },
        { "darksEv", m_DarksEv },
        { "midtonesEv", m_MidtonesEv },
        { "lightsEv", m_LightsEv },
        { "highlightsEv", m_HighlightsEv },
        { "middleGrey", m_MiddleGrey },
        { "range", m_Range },
        { "preserveHue", m_PreserveHue }
    };
}

void ToneEqualizerLayer::Deserialize(const json& j) {
    m_ShadowsEv = j.value("shadowsEv", m_ShadowsEv);
    m_DarksEv = j.value("darksEv", m_DarksEv);
    m_MidtonesEv = j.value("midtonesEv", m_MidtonesEv);
    m_LightsEv = j.value("lightsEv", m_LightsEv);
    m_HighlightsEv = j.value("highlightsEv", m_HighlightsEv);
    m_MiddleGrey = j.value("middleGrey", m_MiddleGrey);
    m_Range = j.value("range", m_Range);
    m_PreserveHue = j.value("preserveHue", m_PreserveHue);
}

ViewTransformLayer::ViewTransformLayer() = default;

ViewTransformLayer::~ViewTransformLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ViewTransformLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kViewTransformFrag);
}

void ViewTransformLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uExposure"), m_Exposure);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlackEv"), m_BlackEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uWhiteEv"), m_WhiteEv);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uMiddleGrey"), m_MiddleGrey);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShoulder"), m_Shoulder);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uToe"), m_Toe);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uContrast"), m_Contrast);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uSaturation"), m_Saturation);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uPreserveHue"), m_PreserveHue ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uDebugFalseColor"), m_DebugFalseColor ? 1 : 0);
    quad.Draw();
    glUseProgram(0);
}

void ViewTransformLayer::RenderUI() {
    ImGui::TextDisabled("Double-click for display transform controls.");
}

NodeSurfaceSpec ViewTransformLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 430.0f;
    spec.maxWidth = 520.0f;
    return spec;
}

void ViewTransformLayer::ResetDisplayDefaults() {
    m_Exposure = 0.0f;
    m_BlackEv = -8.0f;
    m_WhiteEv = 4.0f;
    m_MiddleGrey = 0.18f;
    m_Shoulder = 0.45f;
    m_Toe = 0.18f;
    m_Contrast = 1.0f;
    m_Saturation = 1.0f;
    m_PreserveHue = true;
    m_DebugFalseColor = false;
}

void ViewTransformLayer::StoreProbeStats(const RenderTextureStats& stats) {
    m_LastProbeValid = stats.valid;
    m_LastMinRgb = stats.minRgb;
    m_LastMaxRgb = stats.maxRgb;
    m_LastMinLuma = stats.minLuma;
    m_LastMaxLuma = stats.maxLuma;
    m_LastP01Luma = stats.p01Luma;
    m_LastP50Luma = stats.p50Luma;
    m_LastP99Luma = stats.p99Luma;
    m_LastHdrPixelPercent = stats.hdrPixelPercent;
    m_LastDisplayEdgePercent = stats.displayClipPercent;
}

void ViewTransformLayer::ApplyAutoFromStats(const RenderTextureStats& stats) {
    if (!stats.valid) {
        m_LastProbeValid = false;
        return;
    }
    StoreProbeStats(stats);
    const float middle = std::clamp(stats.p50Luma > 0.000001f ? stats.p50Luma : 0.18f, 0.01f, 1.0f);
    auto evFor = [&](float value) {
        return std::log2(std::max(value, 0.000001f) / std::max(middle, 0.000001f));
    };
    m_Exposure = 0.0f;
    m_MiddleGrey = middle;
    m_BlackEv = std::clamp(std::floor(evFor(std::max(stats.p01Luma, 0.000001f)) - 0.5f), -16.0f, -0.5f);
    m_WhiteEv = std::clamp(std::ceil(evFor(std::max(stats.p99Luma, middle)) + 0.5f), 1.0f, 16.0f);
    m_Shoulder = stats.hdrPixelPercent > 1.0f ? 0.75f : 0.45f;
    m_Toe = 0.18f;
    m_Contrast = 1.0f;
    m_Saturation = 1.0f;
    m_PreserveHue = true;
}

void ViewTransformLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    ImGuiExtras::RichSectionLabel("View Transform / Display Render");
    ImGui::TextDisabled("Final scene-to-display mapping. This is the intentional display compression stage.");
    ImGui::TextDisabled("EV values are stops relative to Middle Grey.");
    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    if (ImGui::Button("Auto From Current Frame", ImVec2(context.safeContentWidth, 0.0f))) {
        RenderTextureStats stats;
        if (editor && editor->ProbeViewTransformInputStats(context.nodeId, stats)) {
            ApplyAutoFromStats(stats);
        } else {
            m_LastProbeValid = false;
        }
    }
    const float halfWidth = std::max(70.0f, context.safeContentWidth * 0.48f);
    if (ImGui::Button("Analyze Input", ImVec2(halfWidth, 0.0f))) {
        RenderTextureStats stats;
        if (editor && editor->ProbeViewTransformInputStats(context.nodeId, stats)) {
            StoreProbeStats(stats);
        } else {
            m_LastProbeValid = false;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset Defaults", ImVec2(halfWidth, 0.0f))) {
        ResetDisplayDefaults();
    }
    if (m_LastProbeValid) {
        ImGui::Dummy(ImVec2(0.0f, context.itemGap));
        ImGui::TextDisabled("Input RGB %.3f to %.3f", m_LastMinRgb, m_LastMaxRgb);
        ImGui::TextDisabled("Luma p01 %.4f  p50 %.4f  p99 %.4f", m_LastP01Luma, m_LastP50Luma, m_LastP99Luma);
        ImGui::TextDisabled("HDR > 1.0: %.1f%%   Display-edge pixels: %.1f%%", m_LastHdrPixelPercent, m_LastDisplayEdgePercent);
    }
    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::NodeSliderFloat("Exposure", "##ViewExposure", &m_Exposure, -8.0f, 8.0f, "%.2f stops", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Black EV", "##ViewBlackEv", &m_BlackEv, -16.0f, 0.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("White EV", "##ViewWhiteEv", &m_WhiteEv, 0.0f, 16.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Middle Grey", "##ViewMiddleGrey", &m_MiddleGrey, 0.01f, 1.0f, "%.3f", context.safeContentWidth);
    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::NodeSliderFloat("Highlight Shoulder", "##ViewShoulder", &m_Shoulder, 0.05f, 4.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Shadow Toe", "##ViewToe", &m_Toe, 0.0f, 1.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Contrast", "##ViewContrast", &m_Contrast, 0.25f, 2.5f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeSliderFloat("Saturation", "##ViewSaturation", &m_Saturation, 0.0f, 2.0f, "%.2f", context.safeContentWidth);
    ImGuiExtras::NodeCheckbox("Preserve Hue", "##ViewPreserveHue", &m_PreserveHue, context.safeContentWidth);
    ImGuiExtras::NodeCheckbox("EV False Color", "##ViewFalseColor", &m_DebugFalseColor, context.safeContentWidth);
    ImGui::TextDisabled("False color: blue < -4, cyan -4..-2, green -2..0, yellow 0..2, orange 2..4, red > 4 EV.");
}

json ViewTransformLayer::Serialize() const {
    return json{
        { "type", "ViewTransform" },
        { "exposure", m_Exposure },
        { "blackEv", m_BlackEv },
        { "whiteEv", m_WhiteEv },
        { "middleGrey", m_MiddleGrey },
        { "shoulder", m_Shoulder },
        { "toe", m_Toe },
        { "contrast", m_Contrast },
        { "saturation", m_Saturation },
        { "preserveHue", m_PreserveHue },
        { "debugFalseColor", m_DebugFalseColor }
    };
}

void ViewTransformLayer::Deserialize(const json& j) {
    m_Exposure = j.value("exposure", m_Exposure);
    m_BlackEv = j.value("blackEv", m_BlackEv);
    m_WhiteEv = j.value("whiteEv", m_WhiteEv);
    m_MiddleGrey = j.value("middleGrey", m_MiddleGrey);
    m_Shoulder = j.value("shoulder", m_Shoulder);
    m_Toe = j.value("toe", m_Toe);
    m_Contrast = j.value("contrast", m_Contrast);
    m_Saturation = j.value("saturation", m_Saturation);
    m_PreserveHue = j.value("preserveHue", m_PreserveHue);
    m_DebugFalseColor = j.value("debugFalseColor", m_DebugFalseColor);
}

ShadowsHighlightsLayer::ShadowsHighlightsLayer() = default;

ShadowsHighlightsLayer::~ShadowsHighlightsLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ShadowsHighlightsLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kShadowsHighlightsFrag);
}

void ShadowsHighlightsLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;
    glUseProgram(m_ShaderProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uInputTex"), 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uShadows"), m_Shadows);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uHighlights"), m_Highlights);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uWhites"), m_Whites);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uBlacks"), m_Blacks);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uMidtoneContrast"), m_MidtoneContrast);
    quad.Draw();
    glUseProgram(0);
}

void ShadowsHighlightsLayer::RenderUI() {
    ImGuiExtras::NodeSliderFloat("Shadows", "##ShadowsLift", &m_Shadows, -1.0f, 1.0f, "%.2f");
    ImGuiExtras::NodeSliderFloat("Highlights", "##HighlightsRecover", &m_Highlights, -1.0f, 1.0f, "%.2f");
    ImGuiExtras::NodeSliderFloat("Whites", "##Whites", &m_Whites, -1.0f, 1.0f, "%.2f");
    ImGuiExtras::NodeSliderFloat("Blacks", "##Blacks", &m_Blacks, -1.0f, 1.0f, "%.2f");
    ImGuiExtras::NodeSliderFloat("Midtone Contrast", "##MidtoneContrast", &m_MidtoneContrast, -1.0f, 1.0f, "%.2f");
}

json ShadowsHighlightsLayer::Serialize() const {
    return json{
        { "type", "ShadowsHighlights" },
        { "shadows", m_Shadows },
        { "highlights", m_Highlights },
        { "whites", m_Whites },
        { "blacks", m_Blacks },
        { "midtoneContrast", m_MidtoneContrast }
    };
}

void ShadowsHighlightsLayer::Deserialize(const json& j) {
    if (j.contains("shadows")) m_Shadows = j["shadows"];
    if (j.contains("highlights")) m_Highlights = j["highlights"];
    if (j.contains("whites")) m_Whites = j["whites"];
    if (j.contains("blacks")) m_Blacks = j["blacks"];
    if (j.contains("midtoneContrast")) m_MidtoneContrast = j["midtoneContrast"];
}
