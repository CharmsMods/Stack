#include "ToneLayers.h"

#include "Renderer/FullscreenQuad.h"

#include <algorithm>
#include <array>
#include <iostream>

namespace {

const char* kToneVert = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kTonePassthroughFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

uniform sampler2D uInputTex;

void main() {
    vec4 color = texture(uInputTex, vUV);
    float outAlpha = color.a;
    if (outAlpha <= 0.0001 && max(color.r, max(color.g, color.b)) > 0.0001) {
        outAlpha = 1.0;
    }
    FragColor = vec4(color.rgb, outAlpha);
}
)";

const char* kToneMapperFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

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
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

uniform sampler2D uInputTex;
uniform sampler2D uCurveLut;
uniform int uMode;
uniform int uDomain;
uniform float uLogMinEv;
uniform float uLogMaxEv;
uniform float uMiddleGrey;
uniform float uToneAnchor;
uniform vec2 uTexelSize;
uniform int uLocalBaselineEnabled;
uniform float uLocalBaselineStrength;
uniform float uLocalShadowOpening;
uniform float uLocalHighlightCompression;
uniform float uLocalBaselineRadius;
uniform float uLocalEdgeProtection;
uniform float uLocalRangeProtection;
uniform float uFoundationRegionEv[5];
uniform float uFoundationBandWidth;
uniform int uFoundationPreserveHue;

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

float gaussian(float x, float center, float width) {
    float d = (x - center) / max(0.001, width);
    return exp(-0.5 * d * d);
}

void accumulateBaseSample(
    inout float weightedLuma,
    inout float totalWeight,
    vec2 uv,
    vec2 offset,
    float spatialWeight,
    float centerLuma) {
    vec2 sampleUv = clamp(uv + offset, vec2(0.0), vec2(1.0));
    float sampleLuma = lumaOf(max(vec3(0.0), texture(uInputTex, sampleUv).rgb));
    float centerEv = log2(max(centerLuma, 0.000001));
    float sampleEv = log2(max(sampleLuma, 0.000001));
    float edgeSigma = mix(2.8, 0.55, clamp(uLocalEdgeProtection, 0.0, 1.0));
    float edgeDelta = (sampleEv - centerEv) / max(0.10, edgeSigma);
    float edgeWeight = exp(-0.5 * edgeDelta * edgeDelta);
    float weight = spatialWeight * edgeWeight;
    weightedLuma += sampleLuma * weight;
    totalWeight += weight;
}

float localBaseLuma(vec2 uv, float centerLuma) {
    vec2 radius = max(vec2(1.0), uTexelSize * max(1.0, uLocalBaselineRadius));
    float weightedLuma = 0.0;
    float totalWeight = 0.0;
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(0.0), 1.00, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2( radius.x * 0.55, 0.0), 0.90, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(-radius.x * 0.55, 0.0), 0.90, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(0.0,  radius.y * 0.55), 0.90, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(0.0, -radius.y * 0.55), 0.90, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2( radius.x, 0.0), 0.70, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(-radius.x, 0.0), 0.70, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(0.0,  radius.y), 0.70, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(0.0, -radius.y), 0.70, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2( radius.x * 0.75,  radius.y * 0.75), 0.55, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(-radius.x * 0.75,  radius.y * 0.75), 0.55, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2( radius.x * 0.75, -radius.y * 0.75), 0.55, centerLuma);
    accumulateBaseSample(weightedLuma, totalWeight, uv, vec2(-radius.x * 0.75, -radius.y * 0.75), 0.55, centerLuma);
    return weightedLuma / max(0.0001, totalWeight);
}

float localBaselineGainEv(float baseLuma, float centerLuma) {
    float baseEv = log2(max(baseLuma, 0.000001) / max(uToneAnchor, 0.000001));
    float shadowWeight = 1.0 - smoothstep(-2.6, -0.12, baseEv);
    float highlightWeight = smoothstep(0.12, 2.8, baseEv);
    float shadowGain = max(0.0, -baseEv) * shadowWeight * max(0.0, uLocalShadowOpening);
    float highlightGain = max(0.0, baseEv) * highlightWeight * max(0.0, uLocalHighlightCompression);
    float gainEv = (shadowGain - highlightGain) * max(0.0, uLocalBaselineStrength);
    gainEv = clamp(gainEv, -4.5, 4.0);

    float nearBlack = 1.0 - smoothstep(0.003, 0.05, centerLuma);
    float nearClip = smoothstep(0.95, 3.5, centerLuma);
    float rangeProtection = clamp(uLocalRangeProtection, 0.0, 1.0);
    if (gainEv > 0.0) {
        gainEv *= mix(1.0, 0.58, rangeProtection * nearBlack);
    } else if (gainEv < 0.0) {
        gainEv *= mix(1.0, 0.55, rangeProtection * nearClip);
    }
    return gainEv;
}

float foundationGainEvFor(float luma) {
    float ev = log2(max(luma, 0.000001) / max(uToneAnchor, 0.000001));
    float width = max(0.35, uFoundationBandWidth);
    float w0 = gaussian(ev, -2.0 * width, width);
    float w1 = gaussian(ev, -1.0 * width, width);
    float w2 = gaussian(ev,  0.0, width);
    float w3 = gaussian(ev,  1.0 * width, width);
    float w4 = gaussian(ev,  2.0 * width, width);
    float sum = max(0.0001, w0 + w1 + w2 + w3 + w4);
    return (uFoundationRegionEv[0] * w0 +
            uFoundationRegionEv[1] * w1 +
            uFoundationRegionEv[2] * w2 +
            uFoundationRegionEv[3] * w3 +
            uFoundationRegionEv[4] * w4) / sum;
}

float toneResponse(float x) {
    float coord = sceneToCurveCoord(x);
    return curveCoordToScene(curve(coord));
}

void main() {
    vec4 color = texture(uInputTex, vUV);
    vec3 rgb = max(vec3(0.0), color.rgb);
    if (uLocalBaselineEnabled != 0) {
        float localOldLuma = lumaOf(rgb);
        float localAverage = localBaseLuma(vUV, localOldLuma);
        float localGainEv = localBaselineGainEv(localAverage, localOldLuma);
        float localNewLuma = localOldLuma * exp2(localGainEv);
        rgb = localOldLuma > 0.00001 ? rgb * (localNewLuma / localOldLuma) : vec3(localNewLuma);
    }
    float foundationOldLuma = lumaOf(rgb);
    float foundationNewLuma = foundationOldLuma * exp2(foundationGainEvFor(foundationOldLuma));
    if (uFoundationPreserveHue != 0) {
        rgb = foundationOldLuma > 0.00001 ? rgb * (foundationNewLuma / foundationOldLuma) : vec3(foundationNewLuma);
    } else {
        rgb *= exp2(foundationGainEvFor(foundationOldLuma));
    }
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
    float outAlpha = color.a;
    if (outAlpha <= 0.0001 && max(rgb.r, max(rgb.g, rgb.b)) > 0.0001) {
        outAlpha = 1.0;
    }
    FragColor = vec4(rgb, outAlpha);
}
)";

const char* kToneEqualizerFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

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
    float outAlpha = color.a;
    if (outAlpha <= 0.0001 && max(rgb.r, max(rgb.g, rgb.b)) > 0.0001) {
        outAlpha = 1.0;
    }
    FragColor = vec4(max(vec3(0.0), rgb), outAlpha);
}
)";

const char* kViewTransformFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

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
        float outAlpha = color.a;
        if (outAlpha <= 0.0001) {
            outAlpha = 1.0;
        }
        FragColor = vec4(falseColor(ev), outAlpha);
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
    float outAlpha = color.a;
    if (outAlpha <= 0.0001 && max(rgb.r, max(rgb.g, rgb.b)) > 0.0001) {
        outAlpha = 1.0;
    }
    FragColor = vec4(clamp(rgb, 0.0, 1.0), outAlpha);
}
)";

const char* kShadowsHighlightsFrag = R"(
#version 330 core
in vec2 vUV;
layout (location = 0) out vec4 FragColor;

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

unsigned int EnsureTonePassthroughProgram() {
    static unsigned int program = 0;
    if (program == 0) {
        program = GLHelpers::CreateShaderProgram(kToneVert, kTonePassthroughFrag);
    }
    return program;
}

void DrawTonePassthrough(unsigned int inputTexture, FullscreenQuad& quad) {
    const unsigned int program = EnsureTonePassthroughProgram();
    if (!program || !inputTexture) {
        return;
    }

    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(program, "uInputTex"), 0);
    quad.Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

} // namespace

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

ToneCurveLayer::~ToneCurveLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
    if (m_LutTexture) glDeleteTextures(1, &m_LutTexture);
}

void ToneCurveLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kToneCurveFrag);
}

void ToneCurveLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    if (m_ShaderProgram == 0) {
        InitializeGL();
    }
    GLint previousTexture0 = 0;
    GLint previousTexture1 = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture0);
    glActiveTexture(GL_TEXTURE1);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture1);
    glActiveTexture(GL_TEXTURE0);

    ClearPendingAutoRewriteFeedback();
    if (m_AutoCalibratePending) {
        const bool cachedStatsMatchInput =
            m_AutoSceneStatsValid &&
            m_AutoSceneAnalysisTexture == inputTexture &&
            m_AutoSceneAnalysisWidth == width &&
            m_AutoSceneAnalysisHeight == height;
        if (!cachedStatsMatchInput || m_AutoCalibrateForceReanalysis || !m_AutoSceneStatsValid) {
            UpdateAutoSceneAnalysis(inputTexture, width, height, m_AutoCalibrateForceReanalysis);
        }
        if (m_AutoSceneStatsValid) {
            const AutoToneIntent intent = SolveAutoToneIntent();
            const AutoAuthoredState authoredState = BuildAutoAuthoredStateFromIntent(intent);
            ApplyAuthoredStateForRender(authoredState);
            m_AutoCalibratePending = false;
            m_AutoCalibrateForceReanalysis = false;
            CapturePendingAutoRewriteFeedback();
        }
    }
    UpdateLut();
    if (m_ShaderProgram == 0 || m_LutTexture == 0) {
        DrawTonePassthrough(inputTexture, quad);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture1));
        glActiveTexture(GL_TEXTURE0);
        return;
    }
    const EffectiveLocalBaselineSettings localBaseline = ComputeEffectiveLocalBaselineSettings();
    const float toneAnchor = ComputeEffectiveToneAnchor();
    const float foundationBandWidth = ComputeEffectiveFoundationBandWidth();
    const std::array<float, 5> foundation = ComputeEffectiveFoundationRegionValues();
    while (glGetError() != GL_NO_ERROR) {}
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
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uToneAnchor"), toneAnchor);
    glUniform2f(glGetUniformLocation(m_ShaderProgram, "uTexelSize"),
        1.0f / std::max(1, width),
        1.0f / std::max(1, height));
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uLocalBaselineEnabled"), m_LocalBaselineEnabled ? 1 : 0);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLocalBaselineStrength"), localBaseline.strength);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLocalShadowOpening"), localBaseline.shadowOpening);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLocalHighlightCompression"), localBaseline.highlightCompression);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLocalBaselineRadius"), localBaseline.radius);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLocalEdgeProtection"), localBaseline.edgeProtection);
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uLocalRangeProtection"), localBaseline.rangeProtection);
    glUniform1fv(glGetUniformLocation(m_ShaderProgram, "uFoundationRegionEv"), 5, foundation.data());
    glUniform1f(glGetUniformLocation(m_ShaderProgram, "uFoundationBandWidth"), foundationBandWidth);
    glUniform1i(glGetUniformLocation(m_ShaderProgram, "uFoundationPreserveHue"), m_FoundationPreserveHue ? 1 : 0);
    quad.Draw();
    const GLenum drawError = glGetError();
    if (drawError != GL_NO_ERROR) {
        std::cerr << "[ToneCurve] Draw failed with GL error " << drawError
                  << "; passing input through.\n";
        DrawTonePassthrough(inputTexture, quad);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture0));
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture1));
    glActiveTexture(GL_TEXTURE0);
    glUseProgram(0);
}

void ToneCurveLayer::UpdateLut() {
    if (!m_LutDirty && m_LutTexture != 0) {
        return;
    }
    std::array<float, 256 * 4> lut {};
    for (std::size_t i = 0; i < lut.size(); ++i) {
        const std::size_t pixel = i / 4;
        const float value = Clamp01(EvaluateCombinedPointCurve(static_cast<float>(pixel) / 255.0f));
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

ViewTransformLayer::~ViewTransformLayer() {
    if (m_ShaderProgram) glDeleteProgram(m_ShaderProgram);
}

void ViewTransformLayer::InitializeGL() {
    m_ShaderProgram = GLHelpers::CreateShaderProgram(kToneVert, kViewTransformFrag);
}

void ViewTransformLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    (void)width;
    (void)height;
    if (m_ShaderProgram == 0) {
        InitializeGL();
    }
    if (m_ShaderProgram == 0) {
        DrawTonePassthrough(inputTexture, quad);
        return;
    }
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
