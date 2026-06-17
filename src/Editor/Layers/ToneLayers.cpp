#include "ToneLayers.h"

#include "Editor/EditorModule.h"
#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <imgui.h>
#include <iostream>
#include <limits>
#include <type_traits>

namespace {

const char* ToneCurveTargetScopeLabel(ToneCurveTargetScope scope) {
    switch (scope) {
        case ToneCurveTargetScope::ScopedMask: return "Scoped Tone + Mask";
        case ToneCurveTargetScope::Global:
        default: return "Global Tone";
    }
}

const char* ToneCurveScopeMaskActionLabel(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return "Add To Existing Mask";
        case ToneCurveScopeMaskAction::Subtract: return "Subtract From Existing Mask";
        case ToneCurveScopeMaskAction::Intersect: return "Intersect Existing Mask";
        case ToneCurveScopeMaskAction::NewMask:
        default: return "New Scoped Mask";
    }
}

const char* ToneCurveScopeMaskActionButtonLabel(ToneCurveScopeMaskAction action) {
    switch (action) {
        case ToneCurveScopeMaskAction::Add: return "Add Tone Scope To Mask";
        case ToneCurveScopeMaskAction::Subtract: return "Subtract Tone Scope From Mask";
        case ToneCurveScopeMaskAction::Intersect: return "Intersect Tone Scope With Mask";
        case ToneCurveScopeMaskAction::NewMask:
        default: return "Create New Scoped Mask";
    }
}

const char* ToneCurveTargetingModeLabel(ToneCurveTargetingMode mode) {
    switch (mode) {
        case ToneCurveTargetingMode::PointTarget: return "Point Target";
        case ToneCurveTargetingMode::RegionTarget:
        default: return "Region Target";
    }
}

const char* ToneFoundationRegionLabel(int index) {
    switch (index) {
        case 0: return "Shadows";
        case 1: return "Darks";
        case 2: return "Midtones";
        case 3: return "Lights";
        case 4: return "Highlights";
        default: return "Midtones";
    }
}

const char* ToneCurveAutoSceneProfileLabel(ToneCurveAutoSceneProfile profile) {
    switch (profile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy: return "Highlight-Heavy HDR";
        case ToneCurveAutoSceneProfile::ShadowHeavy: return "Shadow-Heavy";
        case ToneCurveAutoSceneProfile::Flat: return "Flat / Low Contrast";
        case ToneCurveAutoSceneProfile::NoisyLowLight: return "Noisy Low Light";
        case ToneCurveAutoSceneProfile::Balanced:
        default: return "Balanced";
    }
}

const char* ToneCurveAutoVariantLabel(ToneCurveAutoVariant variant) {
    switch (variant) {
        case ToneCurveAutoVariant::OpenShadows: return "Open Shadows";
        case ToneCurveAutoVariant::ProtectHighlights: return "Protect Highlights";
        case ToneCurveAutoVariant::MoreContrast: return "More Contrast";
        case ToneCurveAutoVariant::Recommended:
        default: return "Recommended";
    }
}

const char* ToneCurveGraphViewLabel(ToneCurveGraphView view) {
    switch (view) {
        case ToneCurveGraphView::Prepared: return "Prepared Graph";
        case ToneCurveGraphView::Finish:
        default: return "Finish Graph";
    }
}

float SafeLog2(float value) {
    return std::log2(std::max(value, 0.000001f));
}

float LerpFloat(float a, float b, float t) {
    return a + (b - a) * t;
}

float PercentileFromSorted(const std::vector<float>& sorted, float percentile) {
    if (sorted.empty()) {
        return 0.0f;
    }
    const float p = std::clamp(percentile, 0.0f, 1.0f);
    const float scaled = p * static_cast<float>(sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(scaled));
    const std::size_t hi = std::min<std::size_t>(lo + 1, sorted.size() - 1);
    const float t = scaled - static_cast<float>(lo);
    return sorted[lo] * (1.0f - t) + sorted[hi] * t;
}

std::size_t HashToneCurveJson(const json& value) {
    return std::hash<std::string>{}(value.dump());
}

std::vector<ToneCurvePoint> BuildLinearToneCurvePoints() {
    return {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
}

bool ToneCurvePointArraysEqual(const std::vector<ToneCurvePoint>& a, const std::vector<ToneCurvePoint>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i].x - b[i].x) > 0.0001f ||
            std::abs(a[i].y - b[i].y) > 0.0001f ||
            a[i].shape != b[i].shape) {
            return false;
        }
    }
    return true;
}

json SerializeToneCurvePointArray(const std::vector<ToneCurvePoint>& points) {
    json serialized = json::array();
    for (const ToneCurvePoint& point : points) {
        serialized.push_back({
            { "x", point.x },
            { "y", point.y },
            { "shape", static_cast<int>(point.shape) }
        });
    }
    return serialized;
}

std::vector<ToneCurvePoint> DeserializeToneCurvePointArray(const json& value, const std::vector<ToneCurvePoint>& fallback) {
    if (!value.is_array()) {
        return fallback;
    }

    std::vector<ToneCurvePoint> points;
    for (const json& item : value) {
        if (!item.is_object()) {
            continue;
        }
        ToneCurvePoint point;
        point.x = item.value("x", 0.0f);
        point.y = item.value("y", 0.0f);
        point.shape = static_cast<ToneCurveSegmentShape>(std::clamp(item.value("shape", 0), 0, 2));
        points.push_back(point);
    }
    return points.size() >= 2 ? points : fallback;
}

json SerializeToneCurveAutoAuthoredState(const ToneCurveLayer::AutoAuthoredState& state) {
    return json{
        { "localBaselineEnabled", state.localBaselineEnabled },
        { "localBaselineStrength", state.localBaseline.strength },
        { "localShadowOpening", state.localBaseline.shadowOpening },
        { "localHighlightCompression", state.localBaseline.highlightCompression },
        { "localBaselineRadius", state.localBaseline.radius },
        { "localEdgeProtection", state.localBaseline.edgeProtection },
        { "localRangeProtection", state.localBaseline.rangeProtection },
        { "middleGrey", state.middleGrey },
        { "logMinEv", state.logMinEv },
        { "logMaxEv", state.logMaxEv },
        { "targetAffectWidth", state.targetAffectWidth },
        { "targetShadowProtection", state.targetShadowProtection },
        { "targetHighlightProtection", state.targetHighlightProtection },
        { "foundationAdaptiveAssist", state.foundationAdaptiveAssist },
        { "foundationAssistStrength", state.foundationAssistStrength },
        { "foundationBandWidth", state.foundationBandWidth },
        { "foundationPreserveHue", state.foundationPreserveHue },
        { "foundationRegionEv", state.foundationRegionEv },
        { "points", SerializeToneCurvePointArray(state.points) }
    };
}

bool DeserializeToneCurveAutoAuthoredState(const json& value, ToneCurveLayer::AutoAuthoredState& outState) {
    if (!value.is_object()) {
        return false;
    }
    outState.localBaselineEnabled = value.value("localBaselineEnabled", outState.localBaselineEnabled);
    outState.localBaseline.strength = value.value("localBaselineStrength", outState.localBaseline.strength);
    outState.localBaseline.shadowOpening = value.value("localShadowOpening", outState.localBaseline.shadowOpening);
    outState.localBaseline.highlightCompression = value.value("localHighlightCompression", outState.localBaseline.highlightCompression);
    outState.localBaseline.radius = value.value("localBaselineRadius", outState.localBaseline.radius);
    outState.localBaseline.edgeProtection = value.value("localEdgeProtection", outState.localBaseline.edgeProtection);
    outState.localBaseline.rangeProtection = value.value("localRangeProtection", outState.localBaseline.rangeProtection);
    outState.middleGrey = value.value("middleGrey", outState.middleGrey);
    outState.logMinEv = value.value("logMinEv", outState.logMinEv);
    outState.logMaxEv = value.value("logMaxEv", outState.logMaxEv);
    outState.targetAffectWidth = value.value("targetAffectWidth", outState.targetAffectWidth);
    outState.targetShadowProtection = value.value("targetShadowProtection", outState.targetShadowProtection);
    outState.targetHighlightProtection = value.value("targetHighlightProtection", outState.targetHighlightProtection);
    outState.foundationAdaptiveAssist = value.value("foundationAdaptiveAssist", outState.foundationAdaptiveAssist);
    outState.foundationAssistStrength = value.value("foundationAssistStrength", outState.foundationAssistStrength);
    outState.foundationBandWidth = value.value("foundationBandWidth", outState.foundationBandWidth);
    outState.foundationPreserveHue = value.value("foundationPreserveHue", outState.foundationPreserveHue);
    if (value.contains("foundationRegionEv") && value["foundationRegionEv"].is_array()) {
        for (std::size_t i = 0; i < outState.foundationRegionEv.size() && i < value["foundationRegionEv"].size(); ++i) {
            outState.foundationRegionEv[i] = value["foundationRegionEv"][i].get<float>();
        }
    }
    outState.points = DeserializeToneCurvePointArray(value.value("points", json::array()), BuildLinearToneCurvePoints());
    return true;
}

template <typename T>
bool ApplyResettableSliderValue(T* value, T resetValue, bool changed, float epsilon = 0.0001f) {
    const ImGuiExtras::NodeControlState& state = ImGuiExtras::GetNodeControlState();
    if (!state.lastHovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        return changed;
    }
    if constexpr (std::is_floating_point_v<T>) {
        if (std::abs(*value - resetValue) <= epsilon) {
            return changed;
        }
    } else {
        if (*value == resetValue) {
            return changed;
        }
    }
    *value = resetValue;
    return true;
}

bool ResettableToneSliderFloat(
    const char* label,
    const char* id,
    float* value,
    float resetValue,
    float minValue,
    float maxValue,
    const char* format,
    float controlWidth) {
    const bool changed = ImGuiExtras::NodeSliderFloat(label, id, value, minValue, maxValue, format, controlWidth);
    return ApplyResettableSliderValue(value, resetValue, changed);
}

bool ResettableToneSliderInt(
    const char* label,
    const char* id,
    int* value,
    int resetValue,
    int minValue,
    int maxValue,
    const char* format,
    float controlWidth) {
    const bool changed = ImGuiExtras::NodeSliderInt(label, id, value, minValue, maxValue, format, controlWidth);
    return ApplyResettableSliderValue(value, resetValue, changed, 0.0f);
}

struct ScopedFramebufferState {
    GLint framebuffer = 0;
    GLint readFbo = 0;
    GLint drawFbo = 0;
    GLint readBuffer = 0;
    GLint drawBuffer = 0;
    GLint viewport[4] = { 0, 0, 0, 0 };

    ScopedFramebufferState() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        glGetIntegerv(GL_READ_BUFFER, &readBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &drawBuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
    }

    void Restore() const {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(framebuffer));
        glReadBuffer(static_cast<GLenum>(readBuffer));
        glDrawBuffer(static_cast<GLenum>(drawBuffer));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    }
};

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

constexpr int kToneCurveMaxPoints = 12;
constexpr float kToneCurveHitRadius = 22.0f;

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

const char* ToneCurveSegmentShapeLabel(ToneCurveSegmentShape shape) {
    switch (shape) {
        case ToneCurveSegmentShape::Smooth: return "Smooth";
        case ToneCurveSegmentShape::Linear: return "Linear";
        case ToneCurveSegmentShape::Hold: return "Hold";
    }
    return "Smooth";
}

const char* ToneCurveSamplingBasisLabel(ToneCurveSamplingBasis basis) {
    switch (basis) {
        case ToneCurveSamplingBasis::CurveInput: return "Curve Input";
        case ToneCurveSamplingBasis::FinalPreview: return "Final Preview";
    }
    return "Curve Input";
}

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

void ToneCurveLayer::SetAutoRewriteRenderContext(int nodeId, std::uint64_t requestRevision) {
    m_AutoRewriteNodeId = nodeId;
    m_AutoRewriteRequestRevision = requestRevision;
}

void ToneCurveLayer::SetDevelopScenePrepToneBudget(bool scenePrepApplied, float strength, float maxEvBias) {
    m_DevelopScenePrepToneBudgetActive = scenePrepApplied;
    m_DevelopScenePrepToneBudgetStrength = std::clamp(strength, 0.0f, 1.25f);
    m_DevelopScenePrepToneBudgetMaxEvBias = std::clamp(maxEvBias, -2.0f, 2.0f);
}

ToneCurveAutoRewriteFeedback ToneCurveLayer::TakePendingAutoRewriteFeedback() {
    ToneCurveAutoRewriteFeedback feedback = m_PendingAutoRewriteFeedback;
    ClearPendingAutoRewriteFeedback();
    return feedback;
}

void ToneCurveLayer::ApplyAutoRewriteFeedback(const ToneCurveAutoRewriteFeedback& feedback) {
    if (!feedback.valid) {
        return;
    }

    if (feedback.authoredLayerJson.is_object()) {
        Deserialize(feedback.authoredLayerJson);
    }

    m_AutoSceneStatsValid = feedback.statsValid;
    m_AutoSceneStats.valid = feedback.statsValid;
    m_AutoSceneStats.shadowPercentile = feedback.shadowPercentile;
    m_AutoSceneStats.midtonePercentile = feedback.midtonePercentile;
    m_AutoSceneStats.highlightPercentile = feedback.highlightPercentile;
    m_AutoSceneStats.clippingRatio = feedback.clippingRatio;
    m_AutoSceneStats.noiseRisk = feedback.noiseRisk;
    m_AutoSceneStats.highlightPressure = feedback.highlightPressure;
    m_AutoSceneStats.textureConfidence = feedback.textureConfidence;
    m_AutoSceneStats.hdrSpreadEv = feedback.hdrSpreadEv;
    m_AutoSceneStats.profile = static_cast<ToneCurveAutoSceneProfile>(std::clamp(
        feedback.sceneProfile,
        static_cast<int>(ToneCurveAutoSceneProfile::Balanced),
        static_cast<int>(ToneCurveAutoSceneProfile::NoisyLowLight)));

    m_AutoSceneShadowPercentile = feedback.shadowPercentile;
    m_AutoSceneMidtonePercentile = feedback.midtonePercentile;
    m_AutoSceneHighlightPercentile = feedback.highlightPercentile;
    m_AutoSceneClippingRatio = feedback.clippingRatio;
    m_AutoSceneNoiseRisk = feedback.noiseRisk;
    m_AutoSceneHighlightPressure = feedback.highlightPressure;
    m_AutoSceneTextureConfidence = feedback.textureConfidence;
    m_AutoSceneHdrSpreadEv = feedback.hdrSpreadEv;
    m_AutoSceneProfile = m_AutoSceneStats.profile;
    m_AutoRecommendedBaseEv = feedback.recommendedBaseEv;
    m_AutoRecommendedLocalStrength = feedback.recommendedLocalStrength;
    m_AutoRecommendedShadowOpening = feedback.recommendedShadowOpening;
    m_AutoRecommendedHighlightCompression = feedback.recommendedHighlightCompression;
    for (int i = 0; i < 5; ++i) {
        m_AutoRecommendedFoundationEv[static_cast<std::size_t>(i)] = feedback.recommendedFoundationEv[i];
    }
    m_AutoSceneStats.recommendedBaseEv = feedback.recommendedBaseEv;
    m_AutoSceneStats.recommendedLocalStrength = feedback.recommendedLocalStrength;
    m_AutoSceneStats.recommendedShadowOpening = feedback.recommendedShadowOpening;
    m_AutoSceneStats.recommendedHighlightCompression = feedback.recommendedHighlightCompression;
    for (int i = 0; i < 5; ++i) {
        m_AutoSceneStats.recommendedFoundationEv[static_cast<std::size_t>(i)] = feedback.recommendedFoundationEv[i];
    }
}

void ToneCurveLayer::RequestAutoCalibration(ToneCurveAutoVariant variant, bool forceReanalysis) {
    m_AutoCalibrateVariant = variant;
    m_AutoCalibratePending = true;
    m_AutoCalibrateForceReanalysis = forceReanalysis;
    ++m_AutoCalibrateRequestId;
    if (forceReanalysis) {
        m_AutoSceneAnalysisFramesUntilRefresh = 0;
    }
}

void ToneCurveLayer::RenderUI() {
    ImGui::TextDisabled("Double-click for curve controls.");
}

bool ToneCurveLayer::RenderDevelopBridgeControls(float controlWidth, bool showExtendedGuidance) {
    bool changed = false;
    const float buttonGap = 8.0f;
    const float buttonWidth = std::max(110.0f, (controlWidth - buttonGap) * 0.5f);
    if (ImGuiExtras::RichFullWidthButton("Auto Calibrate Finish", buttonWidth, 0.0f)) {
        RequestAutoCalibration(ToneCurveAutoVariant::Recommended, true);
        changed = true;
    }
    ImGui::SameLine(0.0f, buttonGap);
    if (ImGuiExtras::RichFullWidthButton("Reset Finish Curve", buttonWidth, 0.0f)) {
        m_Points = {
            ToneCurvePoint{ 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
            ToneCurvePoint{ 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
        };
        m_SelectedPoint = -1;
        m_DraggingPoint = -1;
        SanitizePoints();
        changed = true;
    }

    const bool autoStrengthChanged = ResettableToneSliderFloat(
        "Auto Strength",
        "##ToneCurveDevelopBridgeAutoStrength",
        &m_AutoSceneAssistStrength,
        0.78f,
        0.0f,
        2.4f,
        "%.2f",
        controlWidth);
    const bool autoDynamicRangeChanged = ResettableToneSliderFloat(
        "Auto Dynamic Range",
        "##ToneCurveDevelopBridgeDynamicRange",
        &m_AutoDynamicRange,
        1.0f,
        0.25f,
        3.00f,
        "%.2f",
        controlWidth);
    const bool autoContrastBiasChanged = ResettableToneSliderFloat(
        "Contrast Bias",
        "##ToneCurveDevelopBridgeContrastBias",
        &m_AutoContrastBias,
        0.0f,
        -1.25f,
        1.25f,
        "%.2f",
        controlWidth);
    const bool autoHighlightCharacterChanged = ResettableToneSliderFloat(
        "Highlight Character",
        "##ToneCurveDevelopBridgeHighlightCharacter",
        &m_AutoHighlightCharacter,
        0.0f,
        -1.25f,
        1.25f,
        "%.2f",
        controlWidth);

    bool guidanceChanged = autoStrengthChanged || autoDynamicRangeChanged || autoContrastBiasChanged || autoHighlightCharacterChanged;
    changed |= guidanceChanged;

    bool autoShadowBiasChanged = false;
    bool autoHighlightBiasChanged = false;
    if (showExtendedGuidance) {
        autoShadowBiasChanged = ResettableToneSliderFloat(
            "Shadow Lift",
            "##ToneCurveDevelopBridgeShadowBias",
            &m_AutoShadowBias,
            0.0f,
            -1.25f,
            1.25f,
            "%.2f",
            controlWidth);
        autoHighlightBiasChanged = ResettableToneSliderFloat(
            "Highlight Guard",
            "##ToneCurveDevelopBridgeHighlightBias",
            &m_AutoHighlightBias,
            0.0f,
            -1.25f,
            1.25f,
            "%.2f",
            controlWidth);
        guidanceChanged = guidanceChanged || autoShadowBiasChanged || autoHighlightBiasChanged;
        changed |= autoShadowBiasChanged || autoHighlightBiasChanged;
    }

    if (guidanceChanged) {
        RequestAutoCalibration(ToneCurveAutoVariant::Recommended, !m_AutoSceneStatsValid);
    }

    if (m_AutoCalibratePending) {
        ImGui::TextDisabled("Finish calibration queued for the next render.");
    }
    if (m_AutoSceneStatsValid) {
        ImGui::TextDisabled(
            "Finish profile: %s  (spread %.2f EV)",
            ToneCurveAutoSceneProfileLabel(m_AutoSceneProfile),
            m_AutoSceneHdrSpreadEv);
    } else {
        ImGui::TextDisabled("Run finish calibration to analyze the current prepared image before final curve edits.");
    }
    ImGui::TextDisabled("These are the same shared finish-guidance controls used by Develop Auto Calibrate.");
    ImGui::TextDisabled("Highlight Character: lower keeps more color/headroom, higher allows brighter / whiter highlight pop.");
    ImGui::TextDisabled("The finish curve stays user-authored on top of the prepared tone state.");
    return changed;
}

bool ToneCurveLayer::RenderDevelopFinishGraphPanel(float controlWidth, bool showDetails, bool allowPreparedEditing) {
    const std::vector<ToneCurvePoint> pointsBefore = m_Points;
    const std::vector<ToneCurvePoint> preparedPointsBefore = m_PreparedPoints;
    const ToneCurveDomain domainBefore = m_Domain;
    const ToneCurveGraphView previousView = m_ActiveGraphView;

    if (m_ActiveGraphView != ToneCurveGraphView::Prepared) {
        m_ActiveGraphView = ToneCurveGraphView::Finish;
    }
    RefreshProbeOutput();

    bool changed = false;
    const char* graphLabels[] = { "Final Graph", "Prepared Graph" };
    int graphView = m_ActiveGraphView == ToneCurveGraphView::Prepared ? 1 : 0;
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Graph", &graphView, graphLabels, 2)) {
        m_ActiveGraphView = graphView == 1 ? ToneCurveGraphView::Prepared : ToneCurveGraphView::Finish;
        m_SelectedPoint = -1;
        m_DraggingPoint = -1;
        m_ContextPoint = -1;
        changed = true;
    }
    changed |= ImGuiExtras::NodeCheckbox(
        "Overlay Final Curve",
        "##ToneCurveDevelopShowFinalOverlay",
        &m_ShowFinalCurve,
        controlWidth);
    ImGui::TextDisabled("Final Graph is the user-owned finish curve. Prepared Graph is the auto-authored curve feeding it.");
    const float graphSize = std::min(controlWidth, showDetails ? 320.0f : 280.0f);
    const bool preparedInspectOnly = m_ActiveGraphView == ToneCurveGraphView::Prepared && !allowPreparedEditing;
    if (preparedInspectOnly) {
        ImGui::BeginDisabled();
    }
    RenderCurveEditor(graphSize, graphSize);
    if (preparedInspectOnly) {
        ImGui::EndDisabled();
    }

    const char* domainLabel = m_Domain == ToneCurveDomain::LogScene ? "Log Scene" : "Scene Linear";
    ImGui::TextDisabled("%s Domain: %s", ToneCurveGraphViewLabel(m_ActiveGraphView), domainLabel);
    if (showDetails) {
        if (preparedInspectOnly) {
            ImGui::TextDisabled("Prepared Graph is inspect-only in normal Develop. Use Advanced if the prep curve itself needs editing.");
        } else {
            ImGui::TextDisabled("Right-click a point to delete it or confirm it is linear. Auto recalibration can refresh Prepared Graph.");
        }
    } else {
        ImGui::TextDisabled("Toggle between final taste edits and prepared auto graph inspection.");
    }

    const std::vector<ToneCurvePoint>& activePoints = EditablePoints();
    if (m_SelectedPoint >= 0 && m_SelectedPoint < static_cast<int>(activePoints.size())) {
        const ToneCurvePoint& point = activePoints[static_cast<std::size_t>(m_SelectedPoint)];
        ImGui::TextDisabled("Selected %s point: input %.3f  output %.3f", ToneCurveGraphViewLabel(m_ActiveGraphView), point.x, point.y);
    } else {
        ImGui::TextDisabled("%s points: %zu / %d", ToneCurveGraphViewLabel(m_ActiveGraphView), activePoints.size(), kToneCurveMaxPoints);
    }

    changed |= !ToneCurvePointArraysEqual(pointsBefore, m_Points);
    changed |= !ToneCurvePointArraysEqual(preparedPointsBefore, m_PreparedPoints);
    changed |= domainBefore != m_Domain;
    changed |= previousView != m_ActiveGraphView;
    return changed;
}

bool ToneCurveLayer::RenderDevelopPreparedControlsPanel(float controlWidth, bool showDetails) {
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();

    bool changed = false;
    ImGui::TextDisabled("Prepared tone guidance steers the algorithm-owned prep state before your Finish Graph.");
    ImGui::TextDisabled("Use this when automatic prep needs help opening shadows, holding bright detail, or staying more stable on difficult scenes.");
    ImGui::TextDisabled("Normal taste edits should still stay on Finish Graph.");

    changed |= ImGuiExtras::NodeCheckbox(
        "Automatic Local Baseline",
        "##ToneCurveDevelopPreparedLocalBaselineEnabled",
        &m_LocalBaselineEnabled,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Local Strength",
        "##ToneCurveDevelopPreparedLocalBaselineStrength",
        &m_LocalBaselineStrength,
        authoredResetState.localBaseline.strength,
        0.0f,
        1.6f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Shadow Opening",
        "##ToneCurveDevelopPreparedLocalShadowOpening",
        &m_LocalShadowOpening,
        authoredResetState.localBaseline.shadowOpening,
        0.0f,
        2.2f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Highlight Compression",
        "##ToneCurveDevelopPreparedLocalHighlightCompression",
        &m_LocalHighlightCompression,
        authoredResetState.localBaseline.highlightCompression,
        0.0f,
        2.2f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Edge Protection",
        "##ToneCurveDevelopPreparedLocalEdgeProtection",
        &m_LocalEdgeProtection,
        authoredResetState.localBaseline.edgeProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Range Protection",
        "##ToneCurveDevelopPreparedLocalRangeProtection",
        &m_LocalRangeProtection,
        authoredResetState.localBaseline.rangeProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Adaptive Assist",
        "##ToneCurveDevelopPreparedFoundationAdaptiveAssist",
        &m_FoundationAdaptiveAssist,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Assist Strength",
        "##ToneCurveDevelopPreparedFoundationAssistStrength",
        &m_FoundationAssistStrength,
        authoredResetState.foundationAssistStrength,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);

    if (showDetails) {
        ImGui::TextDisabled("Right-click any prepared control to reset it to the last auto-authored prepared recommendation.");
        ImGui::TextDisabled("Use the Develop manual controls when you want radius, foundation band shaping, middle grey, or on-image targeting.");
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopFoundationControlsPanel(float controlWidth, bool showDetails) {
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();

    bool changed = false;
    ImGui::TextDisabled("Foundation tone sets the broad global balance after local prep and before your finish-graph taste edits.");
    if (m_AutoSceneStatsValid && m_AutoSceneAssistStrength > 0.001f) {
        const float effectiveFoundationAssist = ComputeEffectiveFoundationAssistStrength();
        const float effectiveFoundationBandWidth = ComputeEffectiveFoundationBandWidth();
        ImGui::TextDisabled(
            "Last recommended foundation: %.2f %.2f %.2f %.2f %.2f EV",
            m_AutoRecommendedFoundationEv[0] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[1] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[2] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[3] * m_AutoSceneAssistStrength,
            m_AutoRecommendedFoundationEv[4] * m_AutoSceneAssistStrength);
        ImGui::TextDisabled(
            "Recommended shaping: strength %.2f  band %.2f EV",
            effectiveFoundationAssist,
            effectiveFoundationBandWidth);
    }

    changed |= ResettableToneSliderFloat(
        "Shadows",
        "##ToneCurveDevelopFoundationShadows",
        &m_FoundationShadows,
        authoredResetState.foundationRegionEv[0],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Darks",
        "##ToneCurveDevelopFoundationDarks",
        &m_FoundationDarks,
        authoredResetState.foundationRegionEv[1],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Midtones",
        "##ToneCurveDevelopFoundationMidtones",
        &m_FoundationMidtones,
        authoredResetState.foundationRegionEv[2],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Lights",
        "##ToneCurveDevelopFoundationLights",
        &m_FoundationLights,
        authoredResetState.foundationRegionEv[3],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Highlights",
        "##ToneCurveDevelopFoundationHighlights",
        &m_FoundationHighlights,
        authoredResetState.foundationRegionEv[4],
        -5.0f,
        5.0f,
        "%.2f EV",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Adaptive Assist",
        "##ToneCurveDevelopFoundationAdaptiveAssist",
        &m_FoundationAdaptiveAssist,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Assist Strength",
        "##ToneCurveDevelopFoundationAssistStrength",
        &m_FoundationAssistStrength,
        authoredResetState.foundationAssistStrength,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Middle Grey",
        "##ToneCurveDevelopFoundationMiddleGrey",
        &m_MiddleGrey,
        authoredResetState.middleGrey,
        0.01f,
        1.0f,
        "%.3f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Band Width",
        "##ToneCurveDevelopFoundationBandWidth",
        &m_FoundationBandWidth,
        authoredResetState.foundationBandWidth,
        0.5f,
        8.0f,
        "%.2f EV",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Preserve Hue",
        "##ToneCurveDevelopFoundationPreserveHue",
        &m_FoundationPreserveHue,
        controlWidth);

    if (showDetails) {
        ImGui::TextDisabled("Use these when auto gets the scene into the right zone but the broad tonal balance still needs a more deliberate push.");
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopTargetingPanel(EditorModule* editor, int nodeId, float controlWidth, bool showDetails) {
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();

    bool changed = false;
    ImGui::TextDisabled("On-image targeting samples the viewport and nudges this finish stage where the image actually lands.");
    ImGui::TextDisabled("Use it when broad auto plus graph edits are close, but a specific sampled region still needs to move.");

    const bool targetActive = editor && editor->IsCanvasToolActiveForNode(nodeId, EditorModule::CanvasToolKind::ToneCurveTarget);
    if (targetActive) {
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 128, 176, 215));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 146, 198, 235));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 156, 210, 255));
    }
    if (ImGuiExtras::RichFullWidthButton(targetActive ? "Stop On-Image Target" : "On-Image Target", controlWidth, 0.0f)) {
        if (editor) {
            if (targetActive) {
                editor->CancelCanvasTool();
            } else {
                editor->BeginToneCurveTargeting(
                    nodeId,
                    m_TargetingMode == ToneCurveTargetingMode::RegionTarget
                        ? "Drag in the main viewport to rebalance local baseline and nearby tonal regions."
                        : "Click and drag in the main viewport to move a point on the finish curve.");
            }
        }
    }
    if (targetActive) {
        ImGui::PopStyleColor(3);
    }

    const char* samplingLabels[] = { "Curve Input", "Final Preview" };
    int samplingBasis = static_cast<int>(m_SamplingBasis);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Sampling", &samplingBasis, samplingLabels, 2)) {
        m_SamplingBasis = static_cast<ToneCurveSamplingBasis>(std::clamp(samplingBasis, 0, 1));
        changed = true;
    }

    const char* targetingLabels[] = { "Region Target", "Point Target" };
    int targetingMode = static_cast<int>(m_TargetingMode);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo("Targeting Mode", &targetingMode, targetingLabels, 2)) {
        m_TargetingMode = static_cast<ToneCurveTargetingMode>(std::clamp(targetingMode, 0, 1));
        changed = true;
    }

    changed |= ResettableToneSliderFloat(
        "Affect Width",
        "##ToneCurveDevelopTargetAffectWidth",
        &m_TargetAffectWidth,
        authoredResetState.targetAffectWidth,
        0.02f,
        0.30f,
        "%.3f",
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Auto Anchor Protection",
        "##ToneCurveDevelopAutoAnchorProtection",
        &m_AutoAnchorProtection,
        controlWidth);
    changed |= ImGuiExtras::NodeCheckbox(
        "Protect Endpoints",
        "##ToneCurveDevelopProtectEndpointsDuringTargeting",
        &m_ProtectEndpointsDuringTargeting,
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Protect Shadows",
        "##ToneCurveDevelopTargetShadowProtection",
        &m_TargetShadowProtection,
        authoredResetState.targetShadowProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);
    changed |= ResettableToneSliderFloat(
        "Protect Highlights",
        "##ToneCurveDevelopTargetHighlightProtection",
        &m_TargetHighlightProtection,
        authoredResetState.targetHighlightProtection,
        0.0f,
        1.0f,
        "%.2f",
        controlWidth);

    if (m_ProbeValid) {
        ImGui::TextDisabled(
            "Sample %.3f -> %.3f  (%s)",
            m_ProbeInputX,
            m_ProbeOutputY,
            ToneCurveSamplingBasisLabel(m_ProbeSamplingBasis));
        if (m_TargetingMode == ToneCurveTargetingMode::RegionTarget) {
            const std::array<float, 5> regionWeights = ComputeFoundationTargetWeights(m_ProbeSceneValue);
            int dominantRegion = 0;
            float dominantWeight = regionWeights[0];
            for (int i = 1; i < 5; ++i) {
                if (regionWeights[static_cast<std::size_t>(i)] > dominantWeight) {
                    dominantWeight = regionWeights[static_cast<std::size_t>(i)];
                    dominantRegion = i;
                }
            }
            const char* regionNames[] = { "Shadows", "Darks", "Midtones", "Lights", "Highlights" };
            ImGui::TextDisabled(
                "Dominant region: %s  (weight %.2f)",
                regionNames[dominantRegion],
                dominantWeight);
        }
    } else {
        ImGui::TextDisabled("Hover the main viewport to inspect the current sampled tone before you drag.");
    }

    if (targetActive) {
        const char* statusText = editor && !editor->GetCanvasToolStatusText().empty()
            ? editor->GetCanvasToolStatusText().c_str()
            : (m_TargetingMode == ToneCurveTargetingMode::RegionTarget
                ? "Click and drag in the main viewport to rebalance local baseline and nearby tonal regions"
                : "Click and drag in the main viewport to adjust a point on the finish curve");
        ImGui::TextDisabled("%s", statusText);
    }

    if (showDetails) {
        ImGui::TextDisabled("Capture a hover seed here, then use Local Scope / Masking below when the same move should only affect part of the frame.");
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopScopedMaskPanel(EditorModule* editor, int nodeId, float controlWidth, bool showDetails) {
    return RenderScopedMaskPanel(editor, nodeId, controlWidth, showDetails, true);
}

bool ToneCurveLayer::RenderScopedMaskPanel(
    EditorModule* editor,
    int nodeId,
    float controlWidth,
    bool showDetails,
    bool embeddedInDevelop) {
    bool changed = false;
    ImGuiExtras::RichSectionLabel("Local Scope / Masking");
    const bool maskConnected = editor &&
        editor->GetNodeGraph().FindInputLink(nodeId, EditorNodeGraph::kMaskInputSocketId) != nullptr;
    ImGui::TextDisabled("Use this only when the same tonal move should apply to part of the frame, not the whole image.");
    if (embeddedInDevelop) {
        ImGui::TextDisabled("This creates or refines Develop's Finish Mask while keeping the actual finish processing inside the merged path.");
    }
    if (maskConnected) {
        ImGui::TextDisabled("Current area scope: masked region only.");
    } else {
        ImGui::TextDisabled("Current area scope: whole image. Capture a seed only when you want a local refinement.");
    }

    const char* actionLabels[] = {
        "New Scoped Mask",
        "Add To Existing Mask",
        "Subtract From Existing Mask",
        "Intersect Existing Mask"
    };
    int scopedAction = static_cast<int>(m_ScopedMaskAction);
    ImGui::SetNextItemWidth(controlWidth);
    if (ImGui::Combo(embeddedInDevelop ? "Mask Action##ToneCurveDevelopScopedMaskAction" : "Mask Action", &scopedAction, actionLabels, 4)) {
        m_ScopedMaskAction = static_cast<ToneCurveScopeMaskAction>(std::clamp(scopedAction, 0, 3));
        changed = true;
    }

    if (ImGuiExtras::RichFullWidthButton(
            "Capture Hover As Region Seed",
            std::min(controlWidth, controlWidth * 0.62f + (embeddedInDevelop ? controlWidth * 0.18f : 0.0f)),
            0.0f) &&
        m_ProbeValid) {
        CaptureSelectionSeedFromProbe();
        changed = true;
    }
    if (!m_ProbeValid) {
        ImGui::SameLine();
        ImGui::TextDisabled("Hover the viewport first.");
    }

    if (m_SelectionSeedValid) {
        ImGui::TextDisabled(
            "Seed tone %.3f  RGB %.3f %.3f %.3f",
            m_SelectionSeedSceneValue,
            m_SelectionSeedRgba[0],
            m_SelectionSeedRgba[1],
            m_SelectionSeedRgba[2]);
        changed |= ResettableToneSliderFloat(
            "Tone Similarity",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionToneSimilarity" : "##ToneCurveSelectionToneSimilarity",
            &m_SelectionToneSimilarity,
            0.12f,
            0.02f,
            0.35f,
            "%.3f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Color Similarity",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionColorSimilarity" : "##ToneCurveSelectionColorSimilarity",
            &m_SelectionColorSimilarity,
            0.18f,
            0.02f,
            0.50f,
            "%.3f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Area Radius",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionRegionRadius" : "##ToneCurveSelectionRegionRadius",
            &m_SelectionRegionRadius,
            0.35f,
            0.05f,
            1.0f,
            "%.2f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Region Feather",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionFeather" : "##ToneCurveSelectionFeather",
            &m_SelectionFeather,
            0.35f,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Edge Sensitivity",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionEdgeSensitivity" : "##ToneCurveSelectionEdgeSensitivity",
            &m_SelectionEdgeSensitivity,
            0.45f,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        changed |= ResettableToneSliderFloat(
            "Local Coherence",
            embeddedInDevelop ? "##ToneCurveDevelopSelectionLocalCoherence" : "##ToneCurveSelectionLocalCoherence",
            &m_SelectionLocalCoherence,
            0.45f,
            0.0f,
            1.0f,
            "%.2f",
            controlWidth);
        if (editor && ImGuiExtras::RichFullWidthButton(ToneCurveScopeMaskActionButtonLabel(m_ScopedMaskAction), controlWidth, 0.0f)) {
            const float low = CurveCoordToScene(std::max(0.0f, m_SelectionSeedInputX - std::max(m_TargetAffectWidth, m_SelectionToneSimilarity)));
            const float high = CurveCoordToScene(std::min(1.0f, m_SelectionSeedInputX + std::max(m_TargetAffectWidth, m_SelectionToneSimilarity)));
            const float softness = std::clamp(
                0.02f + 0.18f * m_SelectionFeather + 0.15f * m_SelectionColorSimilarity,
                0.0f,
                0.5f);
            editor->CreateToneCurveSelectionMask(
                nodeId,
                low,
                high,
                softness,
                m_SelectionSeedRgba,
                m_SelectionSeedSceneValue,
                m_SelectionSeedU,
                m_SelectionSeedV,
                m_SelectionToneSimilarity,
                m_SelectionColorSimilarity,
                m_SelectionRegionRadius,
                m_SelectionFeather,
                m_SelectionEdgeSensitivity,
                m_SelectionLocalCoherence,
                m_ScopedMaskAction);
        }
        if (ImGui::Button(embeddedInDevelop ? "Clear Region Seed##ToneCurveDevelopScopedMaskSeed" : "Clear Region Seed")) {
            ClearSelectionSeed();
            changed = true;
        }
        if (showDetails) {
            ImGui::TextDisabled("Repeated capture can add up to five tone/color samples to the tone scope mask, closer to Adobe-style range refinement.");
        }
    } else {
        ImGui::TextDisabled("No region seed captured yet.");
        if (showDetails && embeddedInDevelop) {
            ImGui::TextDisabled("Capture from the inline Develop targeting probe, then build or refine a Finish Mask without leaving the merged workflow.");
        }
    }

    return changed;
}

bool ToneCurveLayer::RenderDevelopPreparedGraphPreviewPanel(float controlWidth, bool showDetails) {
    const ToneCurveGraphView previousView = m_ActiveGraphView;
    m_ActiveGraphView = ToneCurveGraphView::Prepared;
    RefreshProbeOutput();

    ImGui::TextDisabled("Prepared Graph preview only. This is the algorithm-owned prep curve that feeds your Finish Graph.");
    ImGui::TextDisabled("Use this to understand what the automatic processing is doing, not as the main place for taste edits.");
    const float graphSize = std::min(controlWidth, showDetails ? 300.0f : 260.0f);
    RenderCurveEditor(graphSize, graphSize, false);
    if (showDetails) {
        ImGui::TextDisabled("Normal user edits should stay on Finish Graph while the prepared preview remains a read-only reference.");
    }

    m_ActiveGraphView = previousView;
    return false;
}

void ToneCurveLayer::NotifyUpstreamDevelopChanged() {
    if (!HasAutoPreparedState()) {
        return;
    }
    RequestAutoCalibration(ToneCurveAutoVariant::Recommended, true);
}

NodeSurfaceSpec ToneCurveLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 460.0f;
    spec.maxWidth = 540.0f;
    spec.usesCanvasTool = true;
    return spec;
}

void ToneCurveLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    bool changed = false;
    m_FreeEndpoints = true;
    RefreshProbeOutput();
    AutoAuthoredState authoredResetState = m_LastAutoAuthoredStateValid ? m_LastAutoAuthoredState : CaptureCurrentAutoAuthoredState();
    if (m_ActiveGraphView != ToneCurveGraphView::Finish) {
        m_ActiveGraphView = ToneCurveGraphView::Finish;
        m_SelectedPoint = -1;
        m_DraggingPoint = -1;
        m_ContextPoint = -1;
    }

    ImGuiExtras::RichSectionLabel("Tone Curve");
    const struct ModeButton { ToneCurveMode mode; const char* label; } modeButtons[] = {
        { ToneCurveMode::Luminance, "Y" },
        { ToneCurveMode::RGB, "RGB" },
        { ToneCurveMode::Red, "R" },
        { ToneCurveMode::Green, "G" },
        { ToneCurveMode::Blue, "B" }
    };
    const float modeGap = 6.0f;
    const float modeWidth = std::max(40.0f, (context.safeContentWidth - (modeGap * 4.0f)) / 5.0f);
    for (int i = 0; i < 5; ++i) {
        const bool selected = m_Mode == modeButtons[i].mode;
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(60, 128, 176, 215));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 146, 198, 235));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(80, 156, 210, 255));
        }
        if (ImGuiExtras::RichFullWidthButton(modeButtons[i].label, modeWidth, 0.0f)) {
            m_Mode = modeButtons[i].mode;
            changed = true;
        }
        if (selected) {
            ImGui::PopStyleColor(3);
        }
        if (i < 4) {
            ImGui::SameLine(0.0f, modeGap);
        }
    }

    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    const char* domainLabels[] = { "Scene Linear", "Log Scene" };
    int domain = static_cast<int>(m_Domain);
    ImGui::SetNextItemWidth(context.safeContentWidth);
    if (ImGui::Combo("Curve Domain", &domain, domainLabels, 2)) {
        m_Domain = static_cast<ToneCurveDomain>(std::clamp(domain, 0, 1));
        changed = true;
    }

    const float actionGap = 8.0f;
    const float actionWidth = std::max(110.0f, (context.safeContentWidth - actionGap) * 0.5f);
    if (ImGuiExtras::RichFullWidthButton("Reset Curve", actionWidth, 0.0f)) {
        ResetActiveCurveToLinear();
        changed = true;
    }
    ImGui::SameLine(0.0f, actionGap);
    if (ImGuiExtras::RichFullWidthButton("Reset Domain", actionWidth, 0.0f)) {
        m_Domain = ToneCurveDomain::Linear;
        m_LogMinEv = authoredResetState.logMinEv;
        m_LogMaxEv = authoredResetState.logMaxEv;
        changed = true;
    }
    ImGui::TextDisabled("Manual scene-referred finish curve. Usually place View Transform downstream.");

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("Curve");
    ImGui::TextDisabled("Right-click a point for point actions. This standalone node edits the finish curve directly.");
    const float graphSize = std::min(context.safeContentWidth, 390.0f * std::max(0.75f, context.layoutScale));
    RenderCurveEditor(graphSize, graphSize);
    changed |= m_LutDirty;

    ImGui::Dummy(ImVec2(0.0f, context.itemGap));
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_SelectedPoint >= 0 && m_SelectedPoint < static_cast<int>(editablePoints.size())) {
        const ToneCurvePoint& point = editablePoints[static_cast<std::size_t>(m_SelectedPoint)];
        ImGui::TextDisabled("Selected point: input %.3f  output %.3f", point.x, point.y);
        ImGui::SameLine();
        ImGui::TextDisabled("Segment: %s", ToneCurveSegmentShapeLabel(point.shape));
        ImGui::SameLine();
        ImGui::BeginDisabled(
            editablePoints.size() <= 2 ||
            m_SelectedPoint == 0 ||
            m_SelectedPoint == static_cast<int>(editablePoints.size()) - 1);
        if (ImGui::Button("Delete Point")) {
            DeleteSelectedPoint();
            changed = true;
        }
        ImGui::EndDisabled();
    } else {
        ImGui::TextDisabled("Finish Graph: %zu / %d points", editablePoints.size(), kToneCurveMaxPoints);
    }

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("On-Image Targeting");
    changed |= RenderDevelopTargetingPanel(editor, context.nodeId, context.safeContentWidth, false);
    if (context.canvasToolStatusText && context.canvasToolStatusText[0] != '\0') {
        ImGui::TextDisabled("%s", context.canvasToolStatusText);
    }

    ImGui::Dummy(ImVec2(0.0f, context.sectionGap));
    ImGuiExtras::RichSectionLabel("Curve Domain");
    ImGui::TextDisabled("Use Scene Linear for most manual RAW/HDR work. Switch to Log Scene for a broader tonal graph domain.");
    if (m_Domain == ToneCurveDomain::LogScene) {
        changed |= ResettableToneSliderFloat(
            "Graph Black EV",
            "##ToneCurveLogMinEv",
            &m_LogMinEv,
            authoredResetState.logMinEv,
            -20.0f,
            0.0f,
            "%.2f",
            context.safeContentWidth);
        changed |= ResettableToneSliderFloat(
            "Graph White EV",
            "##ToneCurveLogMaxEv",
            &m_LogMaxEv,
            authoredResetState.logMaxEv,
            0.0f,
            20.0f,
            "%.2f",
            context.safeContentWidth);
        if (m_LogMaxEv <= m_LogMinEv + 0.1f) {
            m_LogMaxEv = m_LogMinEv + 0.1f;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("0.0 - 1.0");
    }

    if (changed) {
        SanitizePoints();
        m_LutDirty = true;
    }
}

json ToneCurveLayer::Serialize() const {
    return json{
        { "type", "ToneCurve" },
        { "mode", static_cast<int>(m_Mode) },
        { "domain", static_cast<int>(m_Domain) },
        { "samplingBasis", static_cast<int>(m_SamplingBasis) },
        { "targetingMode", static_cast<int>(m_TargetingMode) },
        { "targetAffectWidth", m_TargetAffectWidth },
        { "autoAnchorProtection", m_AutoAnchorProtection },
        { "protectEndpointsDuringTargeting", m_ProtectEndpointsDuringTargeting },
        { "targetShadowProtection", m_TargetShadowProtection },
        { "targetHighlightProtection", m_TargetHighlightProtection },
        { "autoCalibratePending", m_AutoCalibratePending },
        { "autoCalibrateRequestId", m_AutoCalibrateRequestId },
        { "autoCalibrateVariant", static_cast<int>(m_AutoCalibrateVariant) },
        { "autoSceneAssistStrength", m_AutoSceneAssistStrength },
        { "autoDynamicRange", m_AutoDynamicRange },
        { "autoShadowBias", m_AutoShadowBias },
        { "autoHighlightBias", m_AutoHighlightBias },
        { "autoHighlightCharacter", m_AutoHighlightCharacter },
        { "autoContrastBias", m_AutoContrastBias },
        { "autoSceneStatsValid", m_AutoSceneStatsValid },
        { "autoSceneShadowPercentile", m_AutoSceneShadowPercentile },
        { "autoSceneMidtonePercentile", m_AutoSceneMidtonePercentile },
        { "autoSceneHighlightPercentile", m_AutoSceneHighlightPercentile },
        { "autoSceneClippingRatio", m_AutoSceneClippingRatio },
        { "autoSceneNoiseRisk", m_AutoSceneNoiseRisk },
        { "autoSceneHighlightPressure", m_AutoSceneHighlightPressure },
        { "autoSceneTextureConfidence", m_AutoSceneTextureConfidence },
        { "autoSceneHdrSpreadEv", m_AutoSceneHdrSpreadEv },
        { "autoSceneProfile", static_cast<int>(m_AutoSceneProfile) },
        { "autoRecommendedBaseEv", m_AutoRecommendedBaseEv },
        { "autoRecommendedLocalStrength", m_AutoRecommendedLocalStrength },
        { "autoRecommendedShadowOpening", m_AutoRecommendedShadowOpening },
        { "autoRecommendedHighlightCompression", m_AutoRecommendedHighlightCompression },
        { "autoRecommendedFoundationEv", m_AutoRecommendedFoundationEv },
        { "localBaselineEnabled", m_LocalBaselineEnabled },
        { "localBaselineStrength", m_LocalBaselineStrength },
        { "localShadowOpening", m_LocalShadowOpening },
        { "localHighlightCompression", m_LocalHighlightCompression },
        { "localBaselineRadius", m_LocalBaselineRadius },
        { "localEdgeProtection", m_LocalEdgeProtection },
        { "localRangeProtection", m_LocalRangeProtection },
        { "foundationShadows", m_FoundationShadows },
        { "foundationDarks", m_FoundationDarks },
        { "foundationMidtones", m_FoundationMidtones },
        { "foundationLights", m_FoundationLights },
        { "foundationHighlights", m_FoundationHighlights },
        { "foundationAdaptiveAssist", m_FoundationAdaptiveAssist },
        { "foundationAssistStrength", m_FoundationAssistStrength },
        { "foundationBandWidth", m_FoundationBandWidth },
        { "foundationPreserveHue", m_FoundationPreserveHue },
        { "targetScope", static_cast<int>(m_TargetScope) },
        { "scopedMaskAction", static_cast<int>(m_ScopedMaskAction) },
        { "selectionToneSimilarity", m_SelectionToneSimilarity },
        { "selectionColorSimilarity", m_SelectionColorSimilarity },
        { "selectionRegionRadius", m_SelectionRegionRadius },
        { "selectionFeather", m_SelectionFeather },
        { "selectionEdgeSensitivity", m_SelectionEdgeSensitivity },
        { "selectionLocalCoherence", m_SelectionLocalCoherence },
        { "preparedPoints", SerializeToneCurvePointArray(m_PreparedPoints) },
        { "points", SerializeToneCurvePointArray(m_Points) },
        { "freeEndpoints", m_FreeEndpoints },
        { "activeGraphView", static_cast<int>(m_ActiveGraphView) },
        { "logMinEv", m_LogMinEv },
        { "logMaxEv", m_LogMaxEv },
        { "middleGrey", m_MiddleGrey },
        { "lastAutoAuthoredStateValid", m_LastAutoAuthoredStateValid },
        { "lastAutoAuthoredState", m_LastAutoAuthoredStateValid ? SerializeToneCurveAutoAuthoredState(m_LastAutoAuthoredState) : json::object() }
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
    if (j.contains("samplingBasis")) {
        m_SamplingBasis = static_cast<ToneCurveSamplingBasis>(std::clamp(j["samplingBasis"].get<int>(), 0, 1));
    }
    if (j.contains("targetingMode")) {
        m_TargetingMode = static_cast<ToneCurveTargetingMode>(std::clamp(j["targetingMode"].get<int>(), 0, 1));
    } else {
        m_TargetingMode = ToneCurveTargetingMode::PointTarget;
    }
    m_TargetAffectWidth = j.value("targetAffectWidth", m_TargetAffectWidth);
    m_AutoAnchorProtection = j.value("autoAnchorProtection", m_AutoAnchorProtection);
    m_ProtectEndpointsDuringTargeting = j.value("protectEndpointsDuringTargeting", m_ProtectEndpointsDuringTargeting);
    m_TargetShadowProtection = j.value("targetShadowProtection", m_TargetShadowProtection);
    m_TargetHighlightProtection = j.value("targetHighlightProtection", m_TargetHighlightProtection);
    m_AutoCalibratePending = j.value("autoCalibratePending", false);
    m_AutoCalibrateForceReanalysis = false;
    m_AutoCalibrateRequestId = j.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0));
    if (j.contains("autoCalibrateVariant")) {
        m_AutoCalibrateVariant = static_cast<ToneCurveAutoVariant>(std::clamp(j["autoCalibrateVariant"].get<int>(), 0, 3));
    } else {
        m_AutoCalibrateVariant = ToneCurveAutoVariant::Recommended;
    }
    m_AutoDynamicRange = j.value("autoDynamicRange", m_AutoDynamicRange);
    m_AutoShadowBias = j.value("autoShadowBias", m_AutoShadowBias);
    m_AutoHighlightBias = j.value("autoHighlightBias", m_AutoHighlightBias);
    m_AutoHighlightCharacter = j.value("autoHighlightCharacter", m_AutoHighlightCharacter);
    m_AutoContrastBias = j.value("autoContrastBias", m_AutoContrastBias);
    m_AutoSceneStatsValid = j.value("autoSceneStatsValid", false);
    m_AutoSceneShadowPercentile = j.value("autoSceneShadowPercentile", m_AutoSceneShadowPercentile);
    m_AutoSceneMidtonePercentile = j.value("autoSceneMidtonePercentile", m_AutoSceneMidtonePercentile);
    m_AutoSceneHighlightPercentile = j.value("autoSceneHighlightPercentile", m_AutoSceneHighlightPercentile);
    m_AutoSceneClippingRatio = j.value("autoSceneClippingRatio", m_AutoSceneClippingRatio);
    m_AutoSceneNoiseRisk = j.value("autoSceneNoiseRisk", m_AutoSceneNoiseRisk);
    m_AutoSceneHighlightPressure = j.value("autoSceneHighlightPressure", m_AutoSceneHighlightPressure);
    m_AutoSceneTextureConfidence = j.value("autoSceneTextureConfidence", m_AutoSceneTextureConfidence);
    m_AutoSceneHdrSpreadEv = j.value("autoSceneHdrSpreadEv", m_AutoSceneHdrSpreadEv);
    if (j.contains("autoSceneProfile")) {
        m_AutoSceneProfile = static_cast<ToneCurveAutoSceneProfile>(std::clamp(
            j["autoSceneProfile"].get<int>(),
            static_cast<int>(ToneCurveAutoSceneProfile::Balanced),
            static_cast<int>(ToneCurveAutoSceneProfile::NoisyLowLight)));
    } else {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::Balanced;
    }
    m_AutoRecommendedBaseEv = j.value("autoRecommendedBaseEv", m_AutoRecommendedBaseEv);
    m_AutoRecommendedLocalStrength = j.value("autoRecommendedLocalStrength", m_AutoRecommendedLocalStrength);
    m_AutoRecommendedShadowOpening = j.value("autoRecommendedShadowOpening", m_AutoRecommendedShadowOpening);
    m_AutoRecommendedHighlightCompression = j.value("autoRecommendedHighlightCompression", m_AutoRecommendedHighlightCompression);
    if (j.contains("autoRecommendedFoundationEv") &&
        j["autoRecommendedFoundationEv"].is_array() &&
        j["autoRecommendedFoundationEv"].size() == m_AutoRecommendedFoundationEv.size()) {
        for (std::size_t i = 0; i < m_AutoRecommendedFoundationEv.size(); ++i) {
            m_AutoRecommendedFoundationEv[i] = j["autoRecommendedFoundationEv"][i].get<float>();
        }
    }
    m_AutoSceneStats.valid = m_AutoSceneStatsValid;
    m_AutoSceneStats.shadowPercentile = m_AutoSceneShadowPercentile;
    m_AutoSceneStats.midtonePercentile = m_AutoSceneMidtonePercentile;
    m_AutoSceneStats.highlightPercentile = m_AutoSceneHighlightPercentile;
    m_AutoSceneStats.clippingRatio = m_AutoSceneClippingRatio;
    m_AutoSceneStats.noiseRisk = m_AutoSceneNoiseRisk;
    m_AutoSceneStats.highlightPressure = m_AutoSceneHighlightPressure;
    m_AutoSceneStats.textureConfidence = m_AutoSceneTextureConfidence;
    m_AutoSceneStats.hdrSpreadEv = m_AutoSceneHdrSpreadEv;
    m_AutoSceneStats.profile = m_AutoSceneProfile;
    m_AutoSceneStats.recommendedBaseEv = m_AutoRecommendedBaseEv;
    m_AutoSceneStats.recommendedLocalStrength = m_AutoRecommendedLocalStrength;
    m_AutoSceneStats.recommendedShadowOpening = m_AutoRecommendedShadowOpening;
    m_AutoSceneStats.recommendedHighlightCompression = m_AutoRecommendedHighlightCompression;
    m_AutoSceneStats.recommendedFoundationEv = m_AutoRecommendedFoundationEv;
    m_AutoSceneAnalysisTexture = 0;
    m_AutoSceneAnalysisWidth = 0;
    m_AutoSceneAnalysisHeight = 0;
    m_AutoSceneAnalysisFramesUntilRefresh = 0;
    m_LastAutoAuthoredStateValid = j.value("lastAutoAuthoredStateValid", false);
    m_AutoSceneAssistStrength = j.value("autoSceneAssistStrength", m_AutoSceneAssistStrength);
    if (j.contains("localBaselineEnabled")) {
        m_LocalBaselineEnabled = j.value("localBaselineEnabled", m_LocalBaselineEnabled);
    } else {
        m_LocalBaselineEnabled = false;
    }
    m_LocalBaselineStrength = j.value("localBaselineStrength", m_LocalBaselineStrength);
    m_LocalShadowOpening = j.value("localShadowOpening", m_LocalShadowOpening);
    m_LocalHighlightCompression = j.value("localHighlightCompression", m_LocalHighlightCompression);
    m_LocalBaselineRadius = j.value("localBaselineRadius", m_LocalBaselineRadius);
    m_LocalEdgeProtection = j.value("localEdgeProtection", m_LocalEdgeProtection);
    m_LocalRangeProtection = j.value("localRangeProtection", m_LocalRangeProtection);
    m_FoundationShadows = j.value("foundationShadows", m_FoundationShadows);
    m_FoundationDarks = j.value("foundationDarks", m_FoundationDarks);
    m_FoundationMidtones = j.value("foundationMidtones", m_FoundationMidtones);
    m_FoundationLights = j.value("foundationLights", m_FoundationLights);
    m_FoundationHighlights = j.value("foundationHighlights", m_FoundationHighlights);
    m_FoundationAdaptiveAssist = j.value("foundationAdaptiveAssist", m_FoundationAdaptiveAssist);
    m_FoundationAssistStrength = j.value("foundationAssistStrength", m_FoundationAssistStrength);
    m_FoundationBandWidth = j.value("foundationBandWidth", m_FoundationBandWidth);
    m_FoundationPreserveHue = j.value("foundationPreserveHue", m_FoundationPreserveHue);
    if (j.contains("targetScope")) {
        m_TargetScope = static_cast<ToneCurveTargetScope>(std::clamp(j["targetScope"].get<int>(), 0, 1));
    }
    if (j.contains("scopedMaskAction")) {
        m_ScopedMaskAction = static_cast<ToneCurveScopeMaskAction>(std::clamp(j["scopedMaskAction"].get<int>(), 0, 3));
    }
    m_SelectionToneSimilarity = j.value("selectionToneSimilarity", m_SelectionToneSimilarity);
    m_SelectionColorSimilarity = j.value("selectionColorSimilarity", m_SelectionColorSimilarity);
    m_SelectionRegionRadius = j.value("selectionRegionRadius", m_SelectionRegionRadius);
    m_SelectionFeather = j.value("selectionFeather", m_SelectionFeather);
    m_SelectionEdgeSensitivity = j.value("selectionEdgeSensitivity", m_SelectionEdgeSensitivity);
    m_SelectionLocalCoherence = j.value("selectionLocalCoherence", m_SelectionLocalCoherence);
    m_PreparedPoints = DeserializeToneCurvePointArray(j.value("preparedPoints", json::array()), BuildLinearToneCurvePoints());
    m_Points = DeserializeToneCurvePointArray(j.value("points", json::array()), BuildLinearToneCurvePoints());
    if (!j.contains("points")) {
        m_Points = BuildLinearToneCurvePoints();
    }
    m_FreeEndpoints = true;
    if (j.contains("activeGraphView")) {
        m_ActiveGraphView = static_cast<ToneCurveGraphView>(std::clamp(j["activeGraphView"].get<int>(), 0, 1));
    } else {
        m_ActiveGraphView = ToneCurveGraphView::Finish;
    }
    m_LogMinEv = j.value("logMinEv", m_LogMinEv);
    m_LogMaxEv = j.value("logMaxEv", m_LogMaxEv);
    m_MiddleGrey = j.value("middleGrey", m_MiddleGrey);
    if (!m_LastAutoAuthoredStateValid ||
        !DeserializeToneCurveAutoAuthoredState(j.value("lastAutoAuthoredState", json::object()), m_LastAutoAuthoredState)) {
        m_LastAutoAuthoredStateValid = false;
        m_LastAutoAuthoredState = {};
    }
    SanitizePoints();
    m_LutDirty = true;
}

void ToneCurveLayer::ResetLinear() {
    m_PreparedPoints = BuildLinearToneCurvePoints();
    m_Points = BuildLinearToneCurvePoints();
    m_ActiveGraphView = ToneCurveGraphView::Finish;
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_ContextPoint = -1;
    m_LastAutoAuthoredStateValid = false;
    m_LastAutoAuthoredState = {};
    m_LutDirty = true;
}

void ToneCurveLayer::ResetActiveCurveToLinear() {
    EditablePoints() = BuildLinearToneCurvePoints();
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_ContextPoint = -1;
    SanitizePoints();
    m_LutDirty = true;
}

std::vector<ToneCurvePoint>& ToneCurveLayer::EditablePoints() {
    return m_ActiveGraphView == ToneCurveGraphView::Prepared ? m_PreparedPoints : m_Points;
}

const std::vector<ToneCurvePoint>& ToneCurveLayer::EditablePoints() const {
    return m_ActiveGraphView == ToneCurveGraphView::Prepared ? m_PreparedPoints : m_Points;
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
    EditablePoints() = {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 0.25f, 0.18f, ToneCurveSegmentShape::Linear },
        { 0.5f, 0.5f, ToneCurveSegmentShape::Linear },
        { 0.75f, 0.82f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
    m_SelectedPoint = -1;
    m_ContextPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplyFilmicShoulderPreset() {
    EditablePoints() = {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 0.18f, 0.16f, ToneCurveSegmentShape::Linear },
        { 0.5f, 0.56f, ToneCurveSegmentShape::Linear },
        { 0.82f, 0.9f, ToneCurveSegmentShape::Linear },
        { 1.0f, 0.98f, ToneCurveSegmentShape::Linear }
    };
    m_Domain = ToneCurveDomain::LogScene;
    m_SelectedPoint = -1;
    m_ContextPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::ApplyStrongSCurvePreset() {
    EditablePoints() = {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 0.18f, 0.08f, ToneCurveSegmentShape::Linear },
        { 0.42f, 0.36f, ToneCurveSegmentShape::Linear },
        { 0.62f, 0.72f, ToneCurveSegmentShape::Linear },
        { 0.86f, 0.95f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
    m_SelectedPoint = -1;
    m_ContextPoint = -1;
    m_LutDirty = true;
}

void ToneCurveLayer::SanitizePoints() {
    m_ActiveGraphView = static_cast<ToneCurveGraphView>(std::clamp(static_cast<int>(m_ActiveGraphView), 0, 1));
    m_TargetingMode = static_cast<ToneCurveTargetingMode>(std::clamp(static_cast<int>(m_TargetingMode), 0, 1));
    m_TargetAffectWidth = std::clamp(m_TargetAffectWidth, 0.02f, 0.30f);
    m_TargetShadowProtection = std::clamp(m_TargetShadowProtection, 0.0f, 1.0f);
    m_TargetHighlightProtection = std::clamp(m_TargetHighlightProtection, 0.0f, 1.0f);
    m_AutoSceneAssistStrength = std::clamp(m_AutoSceneAssistStrength, 0.0f, 2.4f);
    m_AutoDynamicRange = std::clamp(m_AutoDynamicRange, 0.25f, 3.0f);
    m_AutoShadowBias = std::clamp(m_AutoShadowBias, -1.25f, 1.25f);
    m_AutoHighlightBias = std::clamp(m_AutoHighlightBias, -1.25f, 1.25f);
    m_AutoHighlightCharacter = std::clamp(m_AutoHighlightCharacter, -1.25f, 1.25f);
    m_AutoContrastBias = std::clamp(m_AutoContrastBias, -1.25f, 1.25f);
    m_LocalBaselineStrength = std::clamp(m_LocalBaselineStrength, 0.0f, 1.6f);
    m_LocalShadowOpening = std::clamp(m_LocalShadowOpening, 0.0f, 2.2f);
    m_LocalHighlightCompression = std::clamp(m_LocalHighlightCompression, 0.0f, 2.2f);
    m_LocalBaselineRadius = std::clamp(m_LocalBaselineRadius, 8.0f, 220.0f);
    m_LocalEdgeProtection = std::clamp(m_LocalEdgeProtection, 0.0f, 1.0f);
    m_LocalRangeProtection = std::clamp(m_LocalRangeProtection, 0.0f, 1.0f);
    m_FoundationShadows = std::clamp(m_FoundationShadows, -5.0f, 5.0f);
    m_FoundationDarks = std::clamp(m_FoundationDarks, -5.0f, 5.0f);
    m_FoundationMidtones = std::clamp(m_FoundationMidtones, -5.0f, 5.0f);
    m_FoundationLights = std::clamp(m_FoundationLights, -5.0f, 5.0f);
    m_FoundationHighlights = std::clamp(m_FoundationHighlights, -5.0f, 5.0f);
    m_FoundationAssistStrength = std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f);
    m_FoundationBandWidth = std::clamp(m_FoundationBandWidth, 0.5f, 8.0f);
    m_MiddleGrey = std::clamp(m_MiddleGrey, 0.01f, 1.0f);
    m_TargetScope = static_cast<ToneCurveTargetScope>(std::clamp(static_cast<int>(m_TargetScope), 0, 1));
    m_ScopedMaskAction = static_cast<ToneCurveScopeMaskAction>(std::clamp(static_cast<int>(m_ScopedMaskAction), 0, 3));
    m_SelectionToneSimilarity = std::clamp(m_SelectionToneSimilarity, 0.02f, 0.35f);
    m_SelectionColorSimilarity = std::clamp(m_SelectionColorSimilarity, 0.02f, 0.50f);
    m_SelectionRegionRadius = std::clamp(m_SelectionRegionRadius, 0.05f, 1.0f);
    m_SelectionFeather = std::clamp(m_SelectionFeather, 0.0f, 1.0f);
    m_SelectionEdgeSensitivity = std::clamp(m_SelectionEdgeSensitivity, 0.0f, 1.0f);
    m_SelectionLocalCoherence = std::clamp(m_SelectionLocalCoherence, 0.0f, 1.0f);
    auto sanitizeCurve = [&](std::vector<ToneCurvePoint>& points) {
        for (ToneCurvePoint& point : points) {
            point.x = Clamp01(point.x);
            point.y = Clamp01(point.y);
            point.shape = ToneCurveSegmentShape::Linear;
        }
        std::sort(points.begin(), points.end(), [](const ToneCurvePoint& a, const ToneCurvePoint& b) {
            return a.x < b.x;
        });
        if (points.empty()) {
            points = BuildLinearToneCurvePoints();
        }
        if (points.size() == 1) {
            points.push_back({ 1.0f, points.front().y, points.front().shape });
        }
        if (points.size() > kToneCurveMaxPoints) {
            points.resize(kToneCurveMaxPoints);
        }
        if (!m_FreeEndpoints && points.size() >= 2) {
            points.front().x = 0.0f;
            points.back().x = 1.0f;
        }
    };
    sanitizeCurve(m_PreparedPoints);
    sanitizeCurve(m_Points);
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_SelectedPoint >= static_cast<int>(editablePoints.size())) {
        m_SelectedPoint = -1;
    }
    if (m_ContextPoint >= static_cast<int>(editablePoints.size())) {
        m_ContextPoint = -1;
    }
}

void ToneCurveLayer::AddPointAt(float x, float y) {
    std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (editablePoints.size() >= kToneCurveMaxPoints) {
        return;
    }
    editablePoints.push_back({ Clamp01(x), Clamp01(y), ToneCurveSegmentShape::Linear });
    SanitizePoints();
    const std::vector<ToneCurvePoint>& sanitizedPoints = EditablePoints();
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(sanitizedPoints.size()); ++i) {
        const float dx = sanitizedPoints[static_cast<std::size_t>(i)].x - x;
        const float dy = sanitizedPoints[static_cast<std::size_t>(i)].y - y;
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
    std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_SelectedPoint < 0 || m_SelectedPoint >= static_cast<int>(editablePoints.size()) || editablePoints.size() <= 2) {
        return;
    }
    if (m_SelectedPoint == 0 || m_SelectedPoint == static_cast<int>(editablePoints.size()) - 1) {
        return;
    }
    editablePoints.erase(editablePoints.begin() + m_SelectedPoint);
    m_SelectedPoint = -1;
    m_DraggingPoint = -1;
    m_ContextPoint = -1;
    SanitizePoints();
    m_LutDirty = true;
}

void ToneCurveLayer::MovePoint(int index, float x, float y) {
    std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (index < 0 || index >= static_cast<int>(editablePoints.size())) {
        return;
    }
    float nextX = Clamp01(x);
    if (!m_FreeEndpoints && index == 0) {
        nextX = 0.0f;
    } else if (!m_FreeEndpoints && index == static_cast<int>(editablePoints.size()) - 1) {
        nextX = 1.0f;
    }
    ToneCurvePoint& point = editablePoints[static_cast<std::size_t>(index)];
    point.x = nextX;
    point.y = Clamp01(y);
    SanitizePoints();
    const std::vector<ToneCurvePoint>& sanitizedPoints = EditablePoints();
    int bestIndex = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (int i = 0; i < static_cast<int>(sanitizedPoints.size()); ++i) {
        const float dx = sanitizedPoints[static_cast<std::size_t>(i)].x - nextX;
        const float dy = sanitizedPoints[static_cast<std::size_t>(i)].y - Clamp01(y);
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

float ToneCurveLayer::SceneToCurveCoord(float x) const {
    if (m_Domain == ToneCurveDomain::LogScene) {
        const float ev = std::log2(std::max(x, 0.000001f) / std::max(m_MiddleGrey, 0.000001f));
        return Clamp01((ev - m_LogMinEv) / std::max(0.0001f, m_LogMaxEv - m_LogMinEv));
    }
    return Clamp01(x);
}

float ToneCurveLayer::CurveCoordToScene(float coord) const {
    if (m_Domain == ToneCurveDomain::LogScene) {
        const float ev = m_LogMinEv + Clamp01(coord) * (m_LogMaxEv - m_LogMinEv);
        return std::max(m_MiddleGrey, 0.000001f) * std::exp2(ev);
    }
    return Clamp01(coord);
}

float ToneCurveLayer::ClampTargetInputX(float x) const {
    const float clamped = Clamp01(x);
    if (!m_ProtectEndpointsDuringTargeting) {
        return clamped;
    }
    const float affectWidth = ComputeEffectiveTargetAffectWidth();
    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    const float edgeGuard = std::clamp(
        affectWidth * (0.42f + 0.18f * std::max(shadowProtection, highlightProtection)),
        0.015f,
        0.18f);
    return std::clamp(clamped, edgeGuard, 1.0f - edgeGuard);
}

float ToneCurveLayer::ProbeSceneValueForSample(const std::array<float, 4>& rgba) const {
    const float r = std::max(0.0f, rgba[0]);
    const float g = std::max(0.0f, rgba[1]);
    const float b = std::max(0.0f, rgba[2]);
    switch (m_Mode) {
        case ToneCurveMode::Red: return r;
        case ToneCurveMode::Green: return g;
        case ToneCurveMode::Blue: return b;
        case ToneCurveMode::RGB:
        case ToneCurveMode::Luminance:
        default:
            return std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
    }
}

int ToneCurveLayer::FindNearbyPointByInput(float x, float tolerance) const {
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    const float clampedX = Clamp01(x);
    int bestIndex = -1;
    float bestDistance = std::max(0.0f, tolerance);
    for (int i = 0; i < static_cast<int>(editablePoints.size()); ++i) {
        const float distance = std::abs(editablePoints[static_cast<std::size_t>(i)].x - clampedX);
        if (distance <= bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

int ToneCurveLayer::EnsurePointAtInput(float x, float tolerance, bool avoidEndpoints) {
    const float clampedX = Clamp01(x);
    int pointIndex = FindNearbyPointByInput(clampedX, tolerance);
    if (avoidEndpoints && IsEndpointIndex(pointIndex)) {
        pointIndex = -1;
    }
    if (pointIndex < 0) {
        AddPointAt(clampedX, EvaluateCurve(clampedX));
        pointIndex = m_SelectedPoint;
        if (avoidEndpoints && IsEndpointIndex(pointIndex)) {
            pointIndex = -1;
        }
    } else {
        m_SelectedPoint = pointIndex;
        m_DraggingPoint = pointIndex;
    }
    return pointIndex;
}

bool ToneCurveLayer::IsEndpointIndex(int index) const {
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    return index == 0 || index == static_cast<int>(editablePoints.size()) - 1;
}

void ToneCurveLayer::EnsureTargetProtectionPoints(float centerX) {
    if (!m_AutoAnchorProtection) {
        return;
    }

    const float halfWidth = std::clamp(ComputeEffectiveTargetAffectWidth(), 0.02f, 0.35f);
    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    const float tolerance = std::max(0.0125f, halfWidth * 0.22f);
    const float leftDistance = halfWidth * (1.0f - 0.65f * shadowProtection);
    const float rightDistance = halfWidth * (1.0f - 0.65f * highlightProtection);
    const float leftX = Clamp01(centerX - leftDistance);
    const float rightX = Clamp01(centerX + rightDistance);
    const float edgeGuard = std::clamp(halfWidth * 0.35f, 0.01f, 0.08f);

    if (leftX > edgeGuard) {
        EnsurePointAtInput(leftX, tolerance, false);
    }
    if (rightX < 1.0f - edgeGuard) {
        EnsurePointAtInput(rightX, tolerance, false);
    }
    RefreshProbeOutput();
}

void ToneCurveLayer::CaptureSelectionSeedFromProbe() {
    if (!m_ProbeValid) {
        return;
    }
    m_SelectionSeedValid = true;
    m_SelectionSeedU = m_ProbeU;
    m_SelectionSeedV = m_ProbeV;
    m_SelectionSeedInputX = m_ProbeInputX;
    m_SelectionSeedSceneValue = m_ProbeSceneValue;
    m_SelectionSeedRgba = m_ProbeRgba;
}

void ToneCurveLayer::ClearSelectionSeed() {
    m_SelectionSeedValid = false;
    m_SelectionSeedU = 0.0f;
    m_SelectionSeedV = 0.0f;
    m_SelectionSeedInputX = 0.0f;
    m_SelectionSeedSceneValue = 0.0f;
    m_SelectionSeedRgba = { 0.0f, 0.0f, 0.0f, 1.0f };
}

void ToneCurveLayer::RefreshProbeOutput() {
    if (!m_ProbeValid) {
        return;
    }
    m_ProbeInputX = Clamp01(m_ProbeInputX);
    if (m_ProbeSamplingBasis == ToneCurveSamplingBasis::CurveInput) {
        const float localScene = ApplyApproximateLocalBaselineToSceneValue(m_ProbeSceneValue);
        const float foundationScene = ApplyFoundationToSceneValue(localScene);
        const float preparedCoord = EvaluatePreparedCurve(SceneToCurveCoord(foundationScene));
        const float preparedScene = CurveCoordToScene(preparedCoord);
        const float finishCoord = EvaluateFinishCurve(SceneToCurveCoord(preparedScene));
        const float finalScene = CurveCoordToScene(finishCoord);
        m_ProbeOutputY = Clamp01(SceneToCurveCoord(finalScene));
        return;
    }
    m_ProbeOutputY = Clamp01(EvaluateCombinedOutputCoord(m_ProbeInputX));
}

void ToneCurveLayer::ClearViewportProbe() {
    m_ProbeValid = false;
    m_ProbeOutputY = 0.0f;
    m_ProbeSceneValue = 0.0f;
}

ToneCurveLayer::ViewportInteractionState ToneCurveLayer::CaptureViewportInteractionState() const {
    ViewportInteractionState state;
    state.probeValid = m_ProbeValid;
    state.probeSamplingBasis = m_ProbeSamplingBasis;
    state.probeU = m_ProbeU;
    state.probeV = m_ProbeV;
    state.probeRgba = m_ProbeRgba;
    state.selectionSeedValid = m_SelectionSeedValid;
    state.selectionSeedU = m_SelectionSeedU;
    state.selectionSeedV = m_SelectionSeedV;
    state.selectionSeedInputX = m_SelectionSeedInputX;
    state.selectionSeedSceneValue = m_SelectionSeedSceneValue;
    state.selectionSeedRgba = m_SelectionSeedRgba;
    state.onImageDragPointIndex = m_OnImageDragPointIndex;
    state.onImageDragAnchorInputX = m_OnImageDragAnchorInputX;
    state.onImageDragAnchorOutputY = m_OnImageDragAnchorOutputY;
    return state;
}

void ToneCurveLayer::RestoreViewportInteractionState(const ViewportInteractionState& state) {
    ClearViewportProbe();
    ClearSelectionSeed();
    EndViewportTargetDrag();
    m_ProbeSamplingBasis = state.probeSamplingBasis;
    m_ProbeU = Clamp01(state.probeU);
    m_ProbeV = Clamp01(state.probeV);
    m_ProbeRgba = state.probeRgba;
    if (state.probeValid) {
        UpdateViewportProbe(state.probeSamplingBasis, state.probeU, state.probeV, state.probeRgba);
    }
    m_SelectionSeedValid = state.selectionSeedValid;
    m_SelectionSeedU = Clamp01(state.selectionSeedU);
    m_SelectionSeedV = Clamp01(state.selectionSeedV);
    m_SelectionSeedInputX = Clamp01(state.selectionSeedInputX);
    m_SelectionSeedSceneValue = std::max(0.0f, state.selectionSeedSceneValue);
    m_SelectionSeedRgba = state.selectionSeedRgba;
    m_OnImageDragPointIndex = state.onImageDragPointIndex;
    m_OnImageDragAnchorInputX = state.onImageDragAnchorInputX;
    m_OnImageDragAnchorOutputY = state.onImageDragAnchorOutputY;
}

void ToneCurveLayer::UpdateViewportProbe(
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    const std::array<float, 4>& rgba) {
    m_ProbeValid = true;
    m_ProbeSamplingBasis = basis;
    m_ProbeU = Clamp01(u);
    m_ProbeV = Clamp01(v);
    m_ProbeRgba = rgba;
    m_ProbeSceneValue = ProbeSceneValueForSample(rgba);
    float probeInputX = SceneToCurveCoord(m_ProbeSceneValue);
    if (basis == ToneCurveSamplingBasis::CurveInput) {
        const float localScene = ApplyApproximateLocalBaselineToSceneValue(m_ProbeSceneValue);
        const float foundationScene = ApplyFoundationToSceneValue(localScene);
        if (m_ActiveGraphView == ToneCurveGraphView::Prepared) {
            probeInputX = SceneToCurveCoord(foundationScene);
        } else {
            const float preparedCoord = EvaluatePreparedCurve(SceneToCurveCoord(foundationScene));
            probeInputX = SceneToCurveCoord(CurveCoordToScene(preparedCoord));
        }
    }
    m_ProbeInputX = probeInputX;
    RefreshProbeOutput();
}

bool ToneCurveLayer::BeginViewportTargetDrag(
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    const std::array<float, 4>& rgba) {
    UpdateViewportProbe(basis, u, v, rgba);
    if (!m_ProbeValid) {
        return false;
    }

    const float targetX = ClampTargetInputX(m_ProbeInputX);
    m_ProbeInputX = targetX;
    if (m_TargetingMode == ToneCurveTargetingMode::RegionTarget) {
        RefreshProbeOutput();
        return true;
    }
    EnsureTargetProtectionPoints(targetX);
    const float pointTolerance = std::max(0.015f, ComputeEffectiveTargetAffectWidth() * 0.28f);
    int pointIndex = EnsurePointAtInput(targetX, pointTolerance, m_ProtectEndpointsDuringTargeting);
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (pointIndex < 0 || pointIndex >= static_cast<int>(editablePoints.size())) {
        return false;
    }

    m_OnImageDragPointIndex = pointIndex;
    m_OnImageDragAnchorInputX = editablePoints[static_cast<std::size_t>(pointIndex)].x;
    m_OnImageDragAnchorOutputY = editablePoints[static_cast<std::size_t>(pointIndex)].y;
    return true;
}

void ToneCurveLayer::UpdateViewportTargetDrag(float deltaCurveY) {
    if (m_TargetingMode == ToneCurveTargetingMode::RegionTarget) {
        ApplyRegionTargetDelta(deltaCurveY);
        RefreshProbeOutput();
        m_LutDirty = true;
        return;
    }
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    if (m_OnImageDragPointIndex < 0 || m_OnImageDragPointIndex >= static_cast<int>(editablePoints.size())) {
        return;
    }
    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    float deltaScale = 1.0f;
    if (m_OnImageDragAnchorInputX < 0.35f) {
        const float shadowRegion = 1.0f - std::clamp(m_OnImageDragAnchorInputX / 0.35f, 0.0f, 1.0f);
        deltaScale *= 1.0f - 0.55f * shadowProtection * shadowRegion;
    }
    if (m_OnImageDragAnchorInputX > 0.65f) {
        const float highlightRegion = std::clamp((m_OnImageDragAnchorInputX - 0.65f) / 0.35f, 0.0f, 1.0f);
        deltaScale *= 1.0f - 0.55f * highlightProtection * highlightRegion;
    }
    const float nextY = Clamp01(editablePoints[static_cast<std::size_t>(m_OnImageDragPointIndex)].y + deltaCurveY * deltaScale);
    MovePoint(
        m_OnImageDragPointIndex,
        m_OnImageDragAnchorInputX,
        nextY);
    m_OnImageDragPointIndex = m_SelectedPoint;
    const std::vector<ToneCurvePoint>& refreshedPoints = EditablePoints();
    if (m_OnImageDragPointIndex >= 0 && m_OnImageDragPointIndex < static_cast<int>(refreshedPoints.size())) {
        m_OnImageDragAnchorOutputY = refreshedPoints[static_cast<std::size_t>(m_OnImageDragPointIndex)].y;
    }
    RefreshProbeOutput();
}

void ToneCurveLayer::EndViewportTargetDrag() {
    m_OnImageDragPointIndex = -1;
}

void ToneCurveLayer::RenderCurveEditor(float width, float height, bool allowEditing) {
    SanitizePoints();
    const std::vector<ToneCurvePoint>& editablePoints = EditablePoints();
    ImGui::PushID("ToneCurveGraph");
    ImGui::PushID(static_cast<int>(m_ActiveGraphView));
    ImGui::PushID(allowEditing ? "editable" : "readonly");
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const ImVec2 size(std::max(160.0f, width), std::max(160.0f, height));
    ImGui::InvisibleButton("canvas", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const CurveGraphRect rect { start, ImVec2(start.x + size.x, start.y + size.y) };
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImU32 bg = IM_COL32(16, 24, 28, 255);
    const ImU32 grid = IM_COL32(90, 115, 125, 58);
    const ImU32 diagonal = IM_COL32(150, 165, 170, 85);
    const ImU32 curveColor = IM_COL32(118, 190, 255, 245);
    const ImU32 supportingCurveColor = IM_COL32(244, 197, 95, 145);
    const ImU32 finalCurveColor = IM_COL32(102, 238, 191, 245);
    const ImU32 pointColor = IM_COL32(235, 240, 242, 255);
    const ImU32 selectedColor = IM_COL32(75, 175, 255, 255);
    const ImU32 probeGuideColor = IM_COL32(118, 190, 255, 108);
    const ImU32 probeRangeFillColor = IM_COL32(118, 190, 255, 26);
    const ImU32 probeInputColor = IM_COL32(235, 240, 242, 210);
    const ImU32 probeOutputColor = IM_COL32(102, 238, 191, 230);

    drawList->AddRectFilled(rect.min, rect.max, bg, 6.0f);
    for (int i = 1; i < 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x = rect.min.x + rect.Width() * t;
        const float y = rect.min.y + rect.Height() * t;
        drawList->AddLine(ImVec2(x, rect.min.y), ImVec2(x, rect.max.y), grid, 1.0f);
        drawList->AddLine(ImVec2(rect.min.x, y), ImVec2(rect.max.x, y), grid, 1.0f);
    }
    drawList->AddLine(ImVec2(rect.min.x, rect.max.y), ImVec2(rect.max.x, rect.min.y), diagonal, 1.0f);

    const auto inactiveCurveEval = [&](float x) {
        return m_ActiveGraphView == ToneCurveGraphView::Prepared
            ? EvaluateFinishCurve(x)
            : EvaluatePreparedCurve(x);
    };
    ImVec2 previous = CurveToScreen(rect, { 0.0f, inactiveCurveEval(0.0f) });
    for (int i = 1; i <= 96; ++i) {
        const float x = static_cast<float>(i) / 96.0f;
        const ImVec2 current = CurveToScreen(rect, { x, Clamp01(inactiveCurveEval(x)) });
        drawList->AddLine(previous, current, supportingCurveColor, 1.15f);
        previous = current;
    }

    previous = CurveToScreen(rect, { 0.0f, EvaluateCurve(0.0f) });
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
    if (m_ProbeValid) {
        RefreshProbeOutput();
        const float probeX = Clamp01(m_ProbeInputX);
        const float leftX = Clamp01(probeX - std::clamp(m_TargetAffectWidth, 0.0f, 0.35f));
        const float rightX = Clamp01(probeX + std::clamp(m_TargetAffectWidth, 0.0f, 0.35f));
        const ImVec2 rangeMin = CurveToScreen(rect, { leftX, 0.0f });
        const ImVec2 rangeMax = CurveToScreen(rect, { rightX, 1.0f });
        drawList->AddRectFilled(
            ImVec2(rangeMin.x, rect.min.y),
            ImVec2(rangeMax.x, rect.max.y),
            probeRangeFillColor,
            0.0f);
        const ImVec2 probeInput = CurveToScreen(rect, { probeX, probeX });
        const ImVec2 probeOutput = CurveToScreen(rect, { probeX, Clamp01(m_ProbeOutputY) });
        drawList->AddLine(
            ImVec2(probeInput.x, rect.min.y),
            ImVec2(probeInput.x, rect.max.y),
            probeGuideColor,
            1.0f);
        drawList->AddCircleFilled(probeInput, 5.0f, probeInputColor, 20);
        drawList->AddCircle(probeInput, 6.5f, IM_COL32(0, 0, 0, 120), 20, 1.0f);
        drawList->AddCircleFilled(probeOutput, 5.5f, probeOutputColor, 20);
        drawList->AddCircle(probeOutput, 7.0f, IM_COL32(0, 0, 0, 120), 20, 1.2f);
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoveredPoint = -1;
    float bestDist2 = kToneCurveHitRadius * kToneCurveHitRadius;
    for (int i = 0; i < static_cast<int>(editablePoints.size()); ++i) {
        const ImVec2 point = CurveToScreen(rect, editablePoints[static_cast<std::size_t>(i)]);
        const float dx = point.x - mouse.x;
        const float dy = point.y - mouse.y;
        const float dist2 = dx * dx + dy * dy;
        if (dist2 <= bestDist2) {
            bestDist2 = dist2;
            hoveredPoint = i;
        }
    }

    if (allowEditing && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hoveredPoint >= 0) {
            m_SelectedPoint = hoveredPoint;
            m_DraggingPoint = hoveredPoint;
        } else {
            const ToneCurvePoint point = ScreenToCurve(rect, mouse);
            AddPointAt(point.x, point.y);
            m_DraggingPoint = m_SelectedPoint;
        }
    }
    if (allowEditing && active && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && m_DraggingPoint >= 0) {
        const ToneCurvePoint point = ScreenToCurve(rect, mouse);
        MovePoint(m_DraggingPoint, point.x, point.y);
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_DraggingPoint = -1;
    }
    if (allowEditing && (hovered || active) && m_SelectedPoint >= 0 && !ImGui::GetIO().WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
        DeleteSelectedPoint();
    }

    if (allowEditing && hovered && hoveredPoint >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        m_SelectedPoint = hoveredPoint;
        m_ContextPoint = hoveredPoint;
        ImGui::OpenPopup("ToneCurvePointShapeMenu");
    }

    if (allowEditing && ImGui::BeginPopup("ToneCurvePointShapeMenu")) {
        std::vector<ToneCurvePoint>& editablePointsMutable = EditablePoints();
        if (m_ContextPoint >= 0 && m_ContextPoint < static_cast<int>(editablePointsMutable.size())) {
            ToneCurvePoint& point = editablePointsMutable[static_cast<std::size_t>(m_ContextPoint)];
            ImGui::TextDisabled("Point %.3f, %.3f", point.x, point.y);
            ImGui::Separator();

            const bool linearSelected = point.shape == ToneCurveSegmentShape::Linear;

            if (ImGui::MenuItem("Linear", nullptr, linearSelected)) {
                point.shape = ToneCurveSegmentShape::Linear;
                m_LutDirty = true;
            }
            ImGui::TextDisabled("Tone Curve now uses linear point connections only.");

            ImGui::Separator();
            ImGui::BeginDisabled(editablePointsMutable.size() <= 2 || m_ContextPoint == 0 || m_ContextPoint == static_cast<int>(editablePointsMutable.size()) - 1);
            if (ImGui::MenuItem("Delete Point")) {
                m_SelectedPoint = m_ContextPoint;
                DeleteSelectedPoint();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndPopup();
    }

    for (int i = 0; i < static_cast<int>(editablePoints.size()); ++i) {
        const ImVec2 point = CurveToScreen(rect, editablePoints[static_cast<std::size_t>(i)]);
        const bool selected = i == m_SelectedPoint;
        drawList->AddCircleFilled(point, selected ? 8.5f : 7.0f, selected ? selectedColor : pointColor, 24);
        drawList->AddCircle(point, selected ? 10.0f : 8.5f, IM_COL32(0, 0, 0, 145), 24, 1.35f);
    }
    ImGui::PopID();
    ImGui::PopID();
    ImGui::PopID();
}

float ToneCurveLayer::EvaluateCurve(float x) const {
    return EvaluateCurve(EditablePoints(), x);
}

float ToneCurveLayer::EvaluateCurve(const std::vector<ToneCurvePoint>& points, float x) const {
    if (points.empty()) return x;
    if (x <= points.front().x) return points.front().y;
    for (std::size_t i = 1; i < points.size(); ++i) {
        const ToneCurvePoint& a = points[i - 1];
        const ToneCurvePoint& b = points[i];
        if (x <= b.x) {
            const float t = (x - a.x) / std::max(0.0001f, b.x - a.x);
            switch (a.shape) {
                case ToneCurveSegmentShape::Linear:
                    return a.y + (b.y - a.y) * t;
                case ToneCurveSegmentShape::Hold:
                    return a.y;
                case ToneCurveSegmentShape::Smooth:
                default: {
                    const float smoothT = t * t * (3.0f - 2.0f * t);
                    return a.y + (b.y - a.y) * smoothT;
                }
            }
        }
    }
    return points.back().y;
}

float ToneCurveLayer::EvaluatePreparedCurve(float x) const {
    return EvaluateCurve(m_PreparedPoints, x);
}

float ToneCurveLayer::EvaluateFinishCurve(float x) const {
    return EvaluateCurve(m_Points, x);
}

float ToneCurveLayer::EvaluateCombinedPointCurve(float x) const {
    const float preparedCoord = Clamp01(EvaluatePreparedCurve(x));
    const float preparedScene = CurveCoordToScene(preparedCoord);
    const float finishInput = SceneToCurveCoord(preparedScene);
    return Clamp01(EvaluateFinishCurve(finishInput));
}

float ToneCurveLayer::EvaluateFinalCurve(float x) const {
    float y = Clamp01(EvaluateCombinedOutputCoord(x));
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

std::array<float, 5> ToneCurveLayer::GetFoundationRegionValues() const {
    return { m_FoundationShadows, m_FoundationDarks, m_FoundationMidtones, m_FoundationLights, m_FoundationHighlights };
}

void ToneCurveLayer::UpdateAutoSceneAnalysis(unsigned int inputTexture, int width, int height, bool forceRefresh) {
    if (!m_AutoCalibratePending || !inputTexture || width <= 0 || height <= 0) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        m_AutoSceneAnalysisTexture = 0;
        m_AutoSceneAnalysisWidth = 0;
        m_AutoSceneAnalysisHeight = 0;
        m_AutoSceneAnalysisFramesUntilRefresh = 0;
        return;
    }

    const ScopedFramebufferState savedState;

    const bool sameInput =
        m_AutoSceneStatsValid &&
        m_AutoSceneAnalysisTexture == inputTexture &&
        m_AutoSceneAnalysisWidth == width &&
        m_AutoSceneAnalysisHeight == height;
    if (sameInput && m_AutoSceneAnalysisFramesUntilRefresh > 0 && !forceRefresh) {
        --m_AutoSceneAnalysisFramesUntilRefresh;
        return;
    }

    const int statsWidth = std::clamp(width / 32, 64, 160);
    const int statsHeight = std::clamp(height / 32, 36, 120);
    const unsigned int statsTexture = GLHelpers::CreateEmptyTexture(statsWidth, statsHeight);
    const unsigned int sourceFbo = GLHelpers::CreateFBO(inputTexture);
    const unsigned int statsFbo = GLHelpers::CreateFBO(statsTexture);
    if (!statsTexture || !sourceFbo || !statsFbo) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        savedState.Restore();
        if (sourceFbo) glDeleteFramebuffers(1, &sourceFbo);
        if (statsFbo) glDeleteFramebuffers(1, &statsFbo);
        if (statsTexture) glDeleteTextures(1, &statsTexture);
        return;
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, statsFbo);
    const GLenum sourceStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    const GLenum statsStatus = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (sourceStatus != GL_FRAMEBUFFER_COMPLETE || statsStatus != GL_FRAMEBUFFER_COMPLETE) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        savedState.Restore();
        glDeleteFramebuffers(1, &sourceFbo);
        glDeleteFramebuffers(1, &statsFbo);
        glDeleteTextures(1, &statsTexture);
        return;
    }

    glBlitFramebuffer(0, 0, width, height, 0, 0, statsWidth, statsHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    std::vector<float> pixels(static_cast<std::size_t>(statsWidth) * static_cast<std::size_t>(statsHeight) * 4u, 0.0f);
    glBindFramebuffer(GL_FRAMEBUFFER, statsFbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, statsWidth, statsHeight);
    while (glGetError() != GL_NO_ERROR) {}
    glReadPixels(0, 0, statsWidth, statsHeight, GL_RGBA, GL_FLOAT, pixels.data());
    const bool readbackOk = glGetError() == GL_NO_ERROR;

    savedState.Restore();
    glDeleteFramebuffers(1, &sourceFbo);
    glDeleteFramebuffers(1, &statsFbo);
    glDeleteTextures(1, &statsTexture);
    if (!readbackOk) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        return;
    }

    std::vector<float> lumas;
    lumas.reserve(static_cast<std::size_t>(statsWidth * statsHeight));
    std::vector<float> lumGrid(static_cast<std::size_t>(statsWidth * statsHeight), 0.0f);
    float clipped = 0.0f;
    float saturated = 0.0f;
    for (int y = 0; y < statsHeight; ++y) {
        for (int x = 0; x < statsWidth; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * statsWidth + x);
            const std::size_t pixelIndex = idx * 4u;
            const float r = std::max(0.0f, pixels[pixelIndex + 0]);
            const float g = std::max(0.0f, pixels[pixelIndex + 1]);
            const float b = std::max(0.0f, pixels[pixelIndex + 2]);
            const float maxChannel = std::max(r, std::max(g, b));
            const float minChannel = std::min(r, std::min(g, b));
            const float saturation = maxChannel > 0.00003f ? (maxChannel - minChannel) / maxChannel : 0.0f;
            const float lum = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
            lumGrid[idx] = lum;
            if (!std::isfinite(lum) || lum <= 0.0f) {
                continue;
            }
            lumas.push_back(lum);
            clipped += maxChannel > 1.0f ? 1.0f : 0.0f;
            saturated += (maxChannel > 0.35f && saturation > 0.58f) ? 1.0f : 0.0f;
        }
    }

    if (lumas.size() < 16) {
        m_AutoSceneStatsValid = false;
        m_AutoSceneStats.valid = false;
        return;
    }

    float textureSum = 0.0f;
    float darkTextureSum = 0.0f;
    float darkCount = 0.0f;
    for (int y = 0; y < statsHeight; ++y) {
        for (int x = 0; x < statsWidth; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * statsWidth + x);
            const int left = std::max(0, x - 1);
            const int right = std::min(statsWidth - 1, x + 1);
            const int up = std::max(0, y - 1);
            const int down = std::min(statsHeight - 1, y + 1);
            const float centerLum = std::max(0.000001f, lumGrid[idx]);
            const float gx = std::abs(SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(y * statsWidth + right)])) -
                                      SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(y * statsWidth + left)])));
            const float gy = std::abs(SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(down * statsWidth + x)])) -
                                      SafeLog2(std::max(0.000001f, lumGrid[static_cast<std::size_t>(up * statsWidth + x)])));
            const float textureProxy = Clamp01((gx + gy) * 0.5f);
            textureSum += textureProxy;
            if (centerLum < 0.08f) {
                darkTextureSum += textureProxy;
                darkCount += 1.0f;
            }
        }
    }

    std::sort(lumas.begin(), lumas.end());
    const float count = static_cast<float>(lumas.size());
    m_AutoSceneShadowPercentile = std::max(PercentileFromSorted(lumas, 0.10f), 0.00001f);
    m_AutoSceneMidtonePercentile = std::max(PercentileFromSorted(lumas, 0.50f), 0.00001f);
    m_AutoSceneHighlightPercentile = std::max(PercentileFromSorted(lumas, 0.98f), 0.00001f);
    const float p01 = std::max(PercentileFromSorted(lumas, 0.01f), 0.000001f);
    const float p05 = std::max(PercentileFromSorted(lumas, 0.05f), 0.000001f);
    const float p25 = std::max(PercentileFromSorted(lumas, 0.25f), 0.000001f);
    const float p40 = std::max(PercentileFromSorted(lumas, 0.40f), 0.000001f);
    const float p60 = std::max(PercentileFromSorted(lumas, 0.60f), 0.000001f);
    const float p75 = std::max(PercentileFromSorted(lumas, 0.75f), 0.000001f);
    const float p90 = std::max(PercentileFromSorted(lumas, 0.90f), 0.000001f);
    m_AutoSceneClippingRatio = Clamp01(clipped / count);
    const float saturationRatio = Clamp01(saturated / count);
    m_AutoSceneTextureConfidence = Clamp01(textureSum / static_cast<float>(statsWidth * statsHeight));
    const float darkTexture = darkCount > 0.0f ? darkTextureSum / darkCount : m_AutoSceneTextureConfidence;
    const float estimatedNoiseFloor = std::clamp(
        std::max(p01, p05 * 0.52f) * (1.0f + darkTexture * 2.85f),
        0.00003f,
        0.10f);

    m_AutoSceneNoiseRisk = Clamp01((estimatedNoiseFloor * 7.0f) / std::max(m_AutoSceneShadowPercentile, 0.00003f));
    const float noisySaturationPressureGuard =
        Clamp01((m_AutoSceneNoiseRisk - 0.55f) / 0.40f);
    const float noisySceneLinearClipGuard =
        Clamp01((m_AutoSceneNoiseRisk - 0.55f) / 0.40f) *
        Clamp01((0.20f - m_AutoSceneClippingRatio) / 0.20f);
    const float clippingHighlightPressure =
        m_AutoSceneClippingRatio * 8.0f * (1.0f - 0.85f * noisySceneLinearClipGuard);
    const float saturationHighlightPressure =
        saturationRatio * 2.5f * (1.0f - 0.70f * noisySaturationPressureGuard);
    m_AutoSceneHighlightPressure = Clamp01(
        std::max(clippingHighlightPressure, saturationHighlightPressure) +
        std::max(0.0f, SafeLog2(m_AutoSceneHighlightPercentile / 0.85f)) * 0.30f);
    m_AutoSceneHdrSpreadEv = std::clamp(
        SafeLog2(std::max(m_AutoSceneHighlightPercentile, 0.0001f) / std::max(m_AutoSceneShadowPercentile, 0.0005f)),
        0.0f,
        16.0f);
    const float sceneKey = std::exp2(
        SafeLog2(p25) * 0.10f +
        SafeLog2(p40) * 0.25f +
        SafeLog2(m_AutoSceneMidtonePercentile) * 0.35f +
        SafeLog2(p60) * 0.20f +
        SafeLog2(p75) * 0.10f);

    const float shadowTarget = LerpFloat(0.18f, 0.32f, 1.0f - m_AutoSceneNoiseRisk);
    float shadowLiftEv = SafeLog2(shadowTarget / std::max(m_AutoSceneShadowPercentile, estimatedNoiseFloor * 8.0f));
    shadowLiftEv *= LerpFloat(0.95f, 0.50f, m_AutoSceneNoiseRisk);
    shadowLiftEv = std::clamp(shadowLiftEv, 0.0f, 2.8f);

    const float broadHighlight = std::max(p90, std::sqrt(std::max(p75 * m_AutoSceneHighlightPercentile, 0.000001f)));
    const float highlightTarget = LerpFloat(0.84f, 0.60f, m_AutoSceneHighlightPressure);
    const float highlightCompressEv = std::clamp(
        SafeLog2(highlightTarget / std::max(broadHighlight, 0.0001f)) - m_AutoSceneHighlightPressure * 0.22f,
        -3.2f,
        0.0f);

    const float lowKeyBias = Clamp01(SafeLog2(0.23f / std::max(sceneKey, 0.000001f)) / 1.6f);
    const float highKeyBias = Clamp01(SafeLog2(std::max(sceneKey, 0.000001f) / 0.34f) / 1.4f);
    const float midtoneTarget = std::clamp(
        0.29f +
            lowKeyBias * 0.06f -
            highKeyBias * 0.05f -
            m_AutoSceneHighlightPressure * 0.05f -
            m_AutoSceneNoiseRisk * 0.04f,
        0.23f,
        0.40f);
    float baseEv = SafeLog2(midtoneTarget / std::max(sceneKey, 0.000001f));
    baseEv *= LerpFloat(1.00f, 0.82f, std::max(m_AutoSceneHighlightPressure, m_AutoSceneNoiseRisk));
    baseEv = std::clamp(baseEv, -1.3f, 1.3f);

    const float shadowPressure = Clamp01(shadowLiftEv / 2.5f);
    const float keyMatch = 1.0f - Clamp01(std::abs(SafeLog2(std::max(sceneKey, 0.000001f) / std::max(midtoneTarget, 0.000001f))) / 0.80f);
    const float highlightStability =
        1.0f - Clamp01(std::max(0.0f, m_AutoSceneHighlightPressure - 0.34f) * 1.35f + m_AutoSceneClippingRatio * 5.0f);
    const float wellExposedStability = Clamp01(
        keyMatch *
        (1.0f - shadowPressure * 0.72f) *
        (1.0f - m_AutoSceneNoiseRisk * 0.35f) *
        (0.55f + highlightStability * 0.45f));
    baseEv *= 1.0f - 0.45f * wellExposedStability;
    if (m_AutoSceneNoiseRisk > 0.72f && m_AutoSceneShadowPercentile < 0.08f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::NoisyLowLight;
    } else if (m_AutoSceneHdrSpreadEv < 2.2f && m_AutoSceneHighlightPressure < 0.22f && m_AutoSceneNoiseRisk < 0.35f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::Flat;
    } else if (m_AutoSceneHighlightPressure > 0.58f && m_AutoSceneHdrSpreadEv > 3.4f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::HighlightHeavy;
    } else if (shadowPressure > 0.55f && m_AutoSceneHighlightPressure < 0.45f) {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::ShadowHeavy;
    } else {
        m_AutoSceneProfile = ToneCurveAutoSceneProfile::Balanced;
    }
    if (baseEv < 0.0f) {
        float negativeBaseEvPermission = std::clamp(
            m_AutoSceneHighlightPressure * 0.75f +
            m_AutoSceneClippingRatio * 5.5f +
            shadowPressure * 0.18f,
            0.12f,
            1.0f);
        if (m_AutoSceneProfile == ToneCurveAutoSceneProfile::Flat ||
            m_AutoSceneProfile == ToneCurveAutoSceneProfile::Balanced) {
            negativeBaseEvPermission *= 0.65f;
        }
        negativeBaseEvPermission *= 1.0f - 0.25f * wellExposedStability;
        baseEv *= negativeBaseEvPermission;
    }
    m_AutoRecommendedBaseEv = baseEv;

    float localStrengthBias = 0.0f;
    float shadowOpeningBias = 0.0f;
    float highlightCompressionBias = 0.0f;
    float baseEvBias = 0.0f;
    float foundationScale = 1.0f;
    float shadowsEvBias = 0.0f;
    float darksEvBias = 0.0f;
    float midtonesEvBias = 0.0f;
    float lightsEvBias = 0.0f;
    float highlightsEvBias = 0.0f;
    switch (m_AutoSceneProfile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy:
            localStrengthBias = 0.04f;
            shadowOpeningBias = 0.06f;
            highlightCompressionBias = 0.20f;
            baseEvBias = -0.12f;
            shadowsEvBias = 0.08f;
            darksEvBias = 0.08f;
            lightsEvBias = -0.14f;
            highlightsEvBias = -0.22f;
            break;
        case ToneCurveAutoSceneProfile::ShadowHeavy:
            localStrengthBias = 0.05f;
            shadowOpeningBias = 0.18f;
            highlightCompressionBias = -0.04f;
            baseEvBias = 0.10f;
            shadowsEvBias = 0.18f;
            darksEvBias = 0.14f;
            midtonesEvBias = 0.06f;
            break;
        case ToneCurveAutoSceneProfile::Flat:
            localStrengthBias = -0.18f;
            shadowOpeningBias = -0.14f;
            highlightCompressionBias = -0.14f;
            baseEvBias *= 0.0f;
            foundationScale = 0.52f;
            break;
        case ToneCurveAutoSceneProfile::NoisyLowLight:
            localStrengthBias = -0.16f;
            shadowOpeningBias = -0.14f;
            highlightCompressionBias = 0.04f;
            baseEvBias = 0.04f;
            foundationScale = 0.68f;
            shadowsEvBias = -0.08f;
            darksEvBias = -0.04f;
            break;
        case ToneCurveAutoSceneProfile::Balanced:
        default:
            break;
    }

    m_AutoRecommendedLocalStrength = std::clamp(
        0.82f + shadowPressure * 0.28f + m_AutoSceneHighlightPressure * 0.24f + localStrengthBias - (1.0f - m_AutoSceneTextureConfidence) * 0.10f,
        0.45f,
        1.45f);
    m_AutoRecommendedShadowOpening = std::clamp(
        0.82f + shadowPressure * 0.78f + m_AutoSceneHighlightPressure * 0.22f + shadowOpeningBias - m_AutoSceneNoiseRisk * 0.12f,
        0.45f,
        2.2f);
    m_AutoRecommendedHighlightCompression = std::clamp(
        0.86f + m_AutoSceneHighlightPressure * 0.88f + shadowPressure * 0.16f + highlightCompressionBias,
        0.45f,
        2.2f);

    baseEv = std::clamp(baseEv + baseEvBias, -1.5f, 1.5f);
    m_AutoRecommendedFoundationEv[0] = std::clamp((shadowLiftEv * 0.72f + shadowsEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[1] = std::clamp((shadowLiftEv * 0.48f + baseEv * 0.40f + darksEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[2] = std::clamp((baseEv * 0.72f + midtonesEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[3] = std::clamp((highlightCompressEv * 0.40f + baseEv * 0.18f + lightsEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoRecommendedFoundationEv[4] = std::clamp((highlightCompressEv * 0.78f + highlightsEvBias) * foundationScale, -5.0f, 5.0f);
    m_AutoSceneStatsValid = true;
    m_AutoSceneStats.valid = true;
    m_AutoSceneStats.shadowPercentile = m_AutoSceneShadowPercentile;
    m_AutoSceneStats.midtonePercentile = m_AutoSceneMidtonePercentile;
    m_AutoSceneStats.highlightPercentile = m_AutoSceneHighlightPercentile;
    m_AutoSceneStats.clippingRatio = m_AutoSceneClippingRatio;
    m_AutoSceneStats.noiseRisk = m_AutoSceneNoiseRisk;
    m_AutoSceneStats.highlightPressure = m_AutoSceneHighlightPressure;
    m_AutoSceneStats.textureConfidence = m_AutoSceneTextureConfidence;
    m_AutoSceneStats.hdrSpreadEv = m_AutoSceneHdrSpreadEv;
    m_AutoSceneStats.profile = m_AutoSceneProfile;
    m_AutoSceneStats.recommendedBaseEv = m_AutoRecommendedBaseEv;
    m_AutoSceneStats.recommendedLocalStrength = m_AutoRecommendedLocalStrength;
    m_AutoSceneStats.recommendedShadowOpening = m_AutoRecommendedShadowOpening;
    m_AutoSceneStats.recommendedHighlightCompression = m_AutoRecommendedHighlightCompression;
    m_AutoSceneStats.recommendedFoundationEv = m_AutoRecommendedFoundationEv;
    m_AutoSceneAnalysisTexture = inputTexture;
    m_AutoSceneAnalysisWidth = width;
    m_AutoSceneAnalysisHeight = height;
    m_AutoSceneAnalysisFramesUntilRefresh = 7;
}

ToneCurveLayer::AutoToneIntent ToneCurveLayer::SolveAutoToneIntent() const {
    AutoToneIntent intent;
    const AutoSceneStats& stats = m_AutoSceneStats;
    if (!stats.valid) {
        intent.localBaseline = ComputeEffectiveLocalBaselineSettings();
        intent.localBaselineEnabled = m_LocalBaselineEnabled;
        intent.middleGrey = std::clamp(m_MiddleGrey, 0.01f, 1.0f);
        intent.logMinEv = m_LogMinEv;
        intent.logMaxEv = m_LogMaxEv;
        intent.targetAffectWidth = std::clamp(m_TargetAffectWidth, 0.02f, 0.30f);
        intent.targetShadowProtection = std::clamp(m_TargetShadowProtection, 0.0f, 1.0f);
        intent.targetHighlightProtection = std::clamp(m_TargetHighlightProtection, 0.0f, 1.0f);
        intent.foundationAdaptiveAssist = m_FoundationAdaptiveAssist;
        intent.foundationAssistStrength = std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f);
        intent.foundationBandWidth = std::clamp(m_FoundationBandWidth, 0.5f, 8.0f);
        intent.foundationPreserveHue = m_FoundationPreserveHue;
        intent.foundationRegionEv = GetFoundationRegionValues();
        return intent;
    }

    EffectiveLocalBaselineSettings canonicalLocalBaseline;
    canonicalLocalBaseline.strength = 0.0f;
    canonicalLocalBaseline.shadowOpening = 0.0f;
    canonicalLocalBaseline.highlightCompression = 0.0f;
    canonicalLocalBaseline.radius = 72.0f;
    canonicalLocalBaseline.edgeProtection = 0.65f;
    canonicalLocalBaseline.rangeProtection = 0.45f;
    constexpr float kCanonicalMiddleGrey = 0.18f;
    constexpr float kCanonicalFoundationBandWidth = 2.50f;
    constexpr float kCanonicalTargetAffectWidth = 0.08f;
    constexpr float kCanonicalTargetProtection = 0.65f;
    constexpr std::array<float, 5> kCanonicalFoundationRegionEv { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };

    const float autoAssist = std::clamp(m_AutoSceneAssistStrength, 0.0f, 2.4f);
    const float baseBlend = std::clamp(autoAssist, 0.0f, 1.0f);
    const float extraAutoBoost = std::max(0.0f, autoAssist - 1.0f);
    const float autoBiasScale = baseBlend + extraAutoBoost * 0.55f;
    const float dynamicRangeControl = std::clamp(m_AutoDynamicRange, 0.25f, 3.0f);
    const float autoTuningPresence = std::clamp(baseBlend + extraAutoBoost * 0.45f, 0.0f, 1.65f);
    const float rawDynamicRangeDelta = dynamicRangeControl - 1.0f;
    const float dynamicRangeDelta =
        std::copysign(std::pow(std::abs(rawDynamicRangeDelta), 0.92f), rawDynamicRangeDelta) * autoTuningPresence;
    const float shadowBias = std::clamp(m_AutoShadowBias, -1.25f, 1.25f) * autoTuningPresence;
    const float highlightBias = std::clamp(m_AutoHighlightBias, -1.25f, 1.25f) * autoTuningPresence;
    const float highlightCharacter = std::clamp(m_AutoHighlightCharacter, -1.25f, 1.25f) * autoTuningPresence;
    const float contrastBias = std::clamp(m_AutoContrastBias, -1.25f, 1.25f) * autoTuningPresence;
    const float liftNeed = Clamp01(std::max(0.0f, stats.recommendedBaseEv) / 1.25f);
    const float highlightGuard = std::clamp(1.0f - stats.highlightPressure * 0.55f, 0.30f, 1.0f);
    const float noiseGuard = std::clamp(1.0f - stats.noiseRisk * 0.60f, 0.25f, 1.0f);
    const float positiveHighlightCharacter = std::max(0.0f, highlightCharacter);
    const float negativeHighlightCharacter = std::max(0.0f, -highlightCharacter);
    intent.localBaseline.strength = std::clamp(
        LerpFloat(canonicalLocalBaseline.strength, stats.recommendedLocalStrength, baseBlend),
        0.0f,
        1.6f);
    intent.localBaseline.shadowOpening = std::clamp(
        LerpFloat(canonicalLocalBaseline.shadowOpening, stats.recommendedShadowOpening, baseBlend),
        0.0f,
        2.2f);
    intent.localBaseline.highlightCompression = std::clamp(
        LerpFloat(canonicalLocalBaseline.highlightCompression, stats.recommendedHighlightCompression, baseBlend),
        0.0f,
        2.2f);

    const float textureComplexity = std::clamp(stats.textureConfidence, 0.0f, 1.0f);
    const float flatness = 1.0f - textureComplexity;
    const float shadowPressure = Clamp01(
        (std::max(0.0f, stats.recommendedFoundationEv[0]) +
         std::max(0.0f, stats.recommendedFoundationEv[1]) * 0.70f +
         std::max(0.0f, stats.recommendedBaseEv) * 1.05f) / 2.55f);
    const float brightDetailPressure = Clamp01(stats.highlightPressure * 0.78f + std::clamp(stats.clippingRatio * 22.0f, 0.0f, 1.0f) * 0.22f);
    const float underBrightBroadHighlight = Clamp01(
        SafeLog2(0.68f / std::max(stats.highlightPercentile, 0.0001f)) / 1.45f) *
        Clamp01((0.55f - stats.highlightPressure) / 0.55f) *
        Clamp01((stats.hdrSpreadEv - 3.2f) / 2.0f) *
        Clamp01((stats.midtonePercentile - 0.08f) / 0.12f) *
        (1.0f - stats.noiseRisk * 0.45f);
    const float sceneContrastRestore = underBrightBroadHighlight * autoTuningPresence;
    const float highlightPunchNeed = Clamp01((shadowPressure * 0.72f + liftNeed * 0.28f) * brightDetailPressure);
    const float stableExposureGuard = Clamp01(
        (1.0f - Clamp01(std::abs(stats.recommendedBaseEv) / 0.60f)) *
        (1.0f - shadowPressure * 0.85f) *
        (1.0f - stats.noiseRisk * 0.35f) *
        (1.0f - std::max(0.0f, stats.highlightPressure - 0.35f) * 1.20f) *
        (1.0f - stats.clippingRatio * 4.0f));
    const float neutralHighlightGuard = stableExposureGuard * (1.0f - brightDetailPressure);
    const float stableStrengthNeutrality =
        stableExposureGuard * Clamp01((autoAssist - 0.55f) / 1.25f);
    const float noisyLowLightToneOverlapGuard =
        stats.profile == ToneCurveAutoSceneProfile::NoisyLowLight
            ? Clamp01((stats.noiseRisk - 0.48f) / 0.42f) *
                Clamp01((0.34f - stats.highlightPressure) / 0.34f) *
                Clamp01((stats.hdrSpreadEv - 2.2f) / 2.4f) *
                (0.65f + 0.35f * liftNeed) *
                autoTuningPresence
            : 0.0f;
    const float scenePrepLiftToneBudgetGuard =
        m_DevelopScenePrepToneBudgetActive && stats.profile == ToneCurveAutoSceneProfile::NoisyLowLight
            ? Clamp01((m_DevelopScenePrepToneBudgetMaxEvBias - 0.18f) / 0.72f) *
                Clamp01((m_DevelopScenePrepToneBudgetStrength - 0.48f) / 0.36f) *
                Clamp01((stats.noiseRisk - 0.48f) / 0.38f) *
                Clamp01((stats.hdrSpreadEv - 2.0f) / 1.8f) *
                (0.70f + 0.30f * liftNeed) *
                autoTuningPresence
            : 0.0f;
    const float scenePrepToneOverlapGuard =
        std::max(noisyLowLightToneOverlapGuard, scenePrepLiftToneBudgetGuard);
    const float flattenRisk = std::clamp(
        flatness * 0.34f +
        stats.noiseRisk * 0.42f +
        std::clamp(0.55f - stats.highlightPressure, 0.0f, 0.55f) * 0.18f,
        0.0f,
        1.0f);
    intent.localBaseline.strength = std::clamp(
        intent.localBaseline.strength * (1.0f - 0.22f * flattenRisk),
        0.0f,
        1.6f);
    if (sceneContrastRestore > 0.0001f) {
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength * (1.0f - 0.10f * sceneContrastRestore),
            0.0f,
            1.6f);
    }
    intent.localBaseline.shadowOpening = std::clamp(
        intent.localBaseline.shadowOpening * (1.0f - 0.12f * stats.noiseRisk),
        0.0f,
        2.2f);
    intent.localBaseline.highlightCompression = std::clamp(
        intent.localBaseline.highlightCompression * (1.0f - 0.08f * flatness) + stats.highlightPressure * 0.08f * autoAssist,
        0.0f,
        2.2f);
    if (highlightPunchNeed > 0.0001f) {
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + highlightPunchNeed * (0.08f + 0.10f * brightDetailPressure),
            0.0f,
            2.2f);
    }
    intent.localBaseline.radius = std::clamp(
        LerpFloat(
            canonicalLocalBaseline.radius,
            std::clamp(
                104.0f - textureComplexity * 40.0f + stats.highlightPressure * 20.0f + stats.noiseRisk * 14.0f,
                28.0f,
                180.0f),
            baseBlend),
        8.0f,
        220.0f);
    intent.localBaseline.edgeProtection = std::clamp(
        LerpFloat(
            canonicalLocalBaseline.edgeProtection,
            std::clamp(
                0.48f + flatness * 0.18f + stats.highlightPressure * 0.12f + stats.noiseRisk * 0.10f,
                0.15f,
                1.0f),
            baseBlend),
        0.0f,
        1.0f);
    intent.localBaseline.rangeProtection = std::clamp(
        LerpFloat(
            canonicalLocalBaseline.rangeProtection,
            std::clamp(
                0.34f + stats.noiseRisk * 0.42f + stats.highlightPressure * 0.16f + flatness * 0.08f,
                0.10f,
                1.0f),
            baseBlend),
        0.0f,
        1.0f);
    if (highlightPunchNeed > 0.0001f) {
        intent.localBaseline.edgeProtection = std::clamp(
            intent.localBaseline.edgeProtection + highlightPunchNeed * (0.14f + 0.10f * brightDetailPressure),
            0.0f,
            1.0f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + highlightPunchNeed * (0.08f + 0.08f * brightDetailPressure),
            0.0f,
            1.0f);
    }
    intent.localBaselineEnabled =
        intent.localBaseline.strength > 0.001f ||
        intent.localBaseline.shadowOpening > 0.001f ||
        intent.localBaseline.highlightCompression > 0.001f;

    if (extraAutoBoost > 0.0001f) {
        const float shadowLiftBoost = extraAutoBoost * liftNeed * highlightGuard * noiseGuard;
        intent.localBaselineEnabled = true;
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength + shadowLiftBoost * (0.12f + shadowPressure * 0.22f),
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening + shadowLiftBoost * (0.22f + shadowPressure * 0.34f),
            0.0f,
            2.2f);
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + extraAutoBoost * stats.highlightPressure * 0.08f,
            0.0f,
            2.2f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + extraAutoBoost * 0.06f,
            0.0f,
            1.0f);
    }

    if (std::abs(shadowBias) > 0.0001f) {
        const float positiveShadowBias = std::max(0.0f, shadowBias);
        const float negativeShadowBias = std::max(0.0f, -shadowBias);
        intent.localBaselineEnabled = true;
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength + positiveShadowBias * 0.10f * noiseGuard - negativeShadowBias * 0.08f,
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening + positiveShadowBias * (0.22f + 0.12f * liftNeed) * noiseGuard -
                negativeShadowBias * 0.18f,
            0.0f,
            2.2f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + positiveShadowBias * 0.05f,
            0.0f,
            1.0f);
    }
    if (std::abs(highlightBias) > 0.0001f) {
        const float positiveHighlightBias = std::max(0.0f, highlightBias);
        const float negativeHighlightBias = std::max(0.0f, -highlightBias);
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + positiveHighlightBias * (0.18f + stats.highlightPressure * 0.14f) -
                negativeHighlightBias * 0.16f,
            0.0f,
            2.2f);
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection + positiveHighlightBias * 0.08f - negativeHighlightBias * 0.06f,
            0.0f,
            1.0f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression - positiveHighlightCharacter * (0.05f + 0.05f * brightDetailPressure),
            0.0f,
            2.2f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection - positiveHighlightCharacter * 0.04f,
            0.0f,
            1.0f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.localBaselineEnabled = true;
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression + negativeHighlightCharacter * (0.09f + 0.09f * brightDetailPressure),
            0.0f,
            2.2f);
        intent.localBaseline.edgeProtection = std::clamp(
            intent.localBaseline.edgeProtection + negativeHighlightCharacter * 0.05f,
            0.0f,
            1.0f);
        intent.localBaseline.rangeProtection = std::clamp(
            intent.localBaseline.rangeProtection + negativeHighlightCharacter * 0.05f,
            0.0f,
            1.0f);
    }

    float anchorEvBias = std::clamp(stats.recommendedBaseEv, -1.25f, 1.25f) *
        (0.72f + 0.16f * baseBlend + 0.18f * extraAutoBoost * liftNeed);
    switch (stats.profile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy:
            anchorEvBias -= 0.16f * autoBiasScale * (1.0f - 0.60f * stableExposureGuard);
            break;
        case ToneCurveAutoSceneProfile::ShadowHeavy:
            anchorEvBias += 0.12f * autoBiasScale;
            break;
        case ToneCurveAutoSceneProfile::Flat:
            anchorEvBias *= 0.72f;
            break;
        case ToneCurveAutoSceneProfile::NoisyLowLight:
            anchorEvBias += 0.08f * autoBiasScale;
            break;
        case ToneCurveAutoSceneProfile::Balanced:
        default:
            break;
    }
    const float highlightRestraint = 1.0f - 0.10f * stats.highlightPressure * (1.0f - 0.55f * stableExposureGuard);
    const float noiseLiftGuard = 1.0f - 0.06f * stats.noiseRisk;
    intent.middleGrey = std::clamp(
        kCanonicalMiddleGrey * std::exp2(anchorEvBias) * highlightRestraint * noiseLiftGuard,
        0.02f,
        0.50f);
    if (extraAutoBoost > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.24f * extraAutoBoost * liftNeed * highlightGuard * noiseGuard),
            0.02f,
            0.50f);
    }
    if (shadowBias > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.08f * shadowBias * liftNeed * noiseGuard),
            0.02f,
            0.50f);
    } else if (shadowBias < -0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.05f * shadowBias),
            0.02f,
            0.50f);
    }
    if (highlightBias > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(-0.04f * highlightBias),
            0.02f,
            0.50f);
    }
    if (highlightPunchNeed > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(-0.08f * highlightPunchNeed),
            0.02f,
            0.50f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(-0.06f * positiveHighlightCharacter * (0.70f + shadowPressure * 0.30f)),
            0.02f,
            0.50f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.middleGrey = std::clamp(
            intent.middleGrey * std::exp2(0.05f * negativeHighlightCharacter * (0.55f + brightDetailPressure * 0.45f)),
            0.02f,
            0.50f);
    }

    float shadowEv = std::clamp(
        SafeLog2(intent.middleGrey / std::max(stats.shadowPercentile, 0.0005f)) + 1.55f + stats.noiseRisk * 0.48f,
        4.5f,
        12.5f);
    float highlightEv = std::clamp(
        SafeLog2(std::max(stats.highlightPercentile, 0.0001f) / intent.middleGrey) + 1.95f + stats.highlightPressure * 0.92f,
        3.5f,
        9.5f);
    shadowEv = std::clamp(
        shadowEv +
            dynamicRangeDelta * (0.85f + liftNeed * 0.55f) * noiseGuard +
            extraAutoBoost * liftNeed * 0.32f * noiseGuard,
        4.5f,
        12.8f);
    highlightEv = std::clamp(
        highlightEv +
            dynamicRangeDelta * (0.78f + stats.highlightPressure * 0.42f) * (0.75f + highlightGuard * 0.25f) +
            extraAutoBoost * liftNeed * 0.18f,
        3.5f,
        10.5f);
    intent.logMinEv = std::clamp(-shadowEv, -14.0f, -2.0f);
    intent.logMaxEv = std::clamp(highlightEv, 2.0f, 10.5f);
    if (intent.logMaxEv - intent.logMinEv < 6.0f) {
        intent.logMaxEv = intent.logMinEv + 6.0f;
    }
    intent.logMinEv = std::clamp(intent.logMinEv - std::max(0.0f, shadowBias) * 0.28f * noiseGuard, -14.5f, -2.0f);
    intent.logMaxEv = std::clamp(intent.logMaxEv + std::max(0.0f, highlightBias) * 0.26f, 2.0f, 10.8f);
    if (highlightPunchNeed > 0.0001f) {
        intent.logMaxEv = std::clamp(intent.logMaxEv + 0.18f * highlightPunchNeed, 2.0f, 10.8f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.logMaxEv = std::clamp(
            intent.logMaxEv - positiveHighlightCharacter * (0.12f + 0.08f * brightDetailPressure),
            2.0f,
            10.8f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.logMaxEv = std::clamp(
            intent.logMaxEv + negativeHighlightCharacter * (0.18f + 0.10f * brightDetailPressure),
            2.0f,
            10.8f);
    }
    if (intent.logMaxEv - intent.logMinEv < 6.0f) {
        intent.logMaxEv = intent.logMinEv + 6.0f;
    }
    if (stableStrengthNeutrality > 0.0001f) {
        intent.logMaxEv = std::clamp(
            intent.logMaxEv + 0.42f * stableStrengthNeutrality,
            2.0f,
            10.8f);
    }

    const float hdrSpread = std::clamp(stats.hdrSpreadEv / 8.0f, 0.0f, 1.0f);
    intent.foundationBandWidth = std::clamp(
        kCanonicalFoundationBandWidth +
            (std::clamp(2.1f + hdrSpread * 1.45f + flatness * 0.42f - stats.noiseRisk * 0.14f, 1.4f, 5.2f) -
             kCanonicalFoundationBandWidth) * baseBlend,
        0.5f,
        8.0f);
    intent.foundationBandWidth = std::clamp(
        intent.foundationBandWidth * (1.0f + dynamicRangeDelta * 0.16f),
        0.5f,
        8.0f);
    intent.targetAffectWidth = std::clamp(
        kCanonicalTargetAffectWidth +
            (std::clamp(
                kCanonicalTargetAffectWidth +
                    std::clamp((intent.foundationBandWidth - kCanonicalFoundationBandWidth) / 3.0f, -1.0f, 1.0f) * 0.05f +
                    stats.highlightPressure * 0.03f + stats.noiseRisk * 0.015f,
                0.035f,
                0.26f) - kCanonicalTargetAffectWidth) * baseBlend,
        0.02f,
        0.30f);
    intent.targetAffectWidth = std::clamp(
        intent.targetAffectWidth * (1.0f - dynamicRangeDelta * 0.10f),
        0.02f,
        0.30f);
    intent.targetShadowProtection = std::clamp(
        kCanonicalTargetProtection +
            (std::clamp(0.58f + stats.noiseRisk * 0.26f + flatness * 0.08f, 0.0f, 1.0f) -
             kCanonicalTargetProtection) * baseBlend,
        0.0f,
        1.0f);
    intent.targetHighlightProtection = std::clamp(
        kCanonicalTargetProtection +
            (std::clamp(0.56f + stats.highlightPressure * 0.28f + stats.clippingRatio * 0.10f, 0.0f, 1.0f) -
             kCanonicalTargetProtection) * baseBlend,
        0.0f,
        1.0f);
    intent.targetShadowProtection = std::clamp(
        intent.targetShadowProtection + extraAutoBoost * 0.05f + std::max(0.0f, -dynamicRangeDelta) * 0.04f,
        0.0f,
        1.0f);
    intent.targetHighlightProtection = std::clamp(
        intent.targetHighlightProtection + extraAutoBoost * 0.08f + std::max(0.0f, dynamicRangeDelta) * 0.06f,
        0.0f,
        1.0f);
    intent.targetShadowProtection = std::clamp(
        intent.targetShadowProtection + std::max(0.0f, shadowBias) * 0.05f,
        0.0f,
        1.0f);
    intent.targetHighlightProtection = std::clamp(
        intent.targetHighlightProtection + std::max(0.0f, highlightBias) * 0.10f - std::max(0.0f, -highlightBias) * 0.08f,
        0.0f,
        1.0f);
    if (positiveHighlightCharacter > 0.0001f) {
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection - positiveHighlightCharacter * 0.12f,
            0.0f,
            1.0f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection + negativeHighlightCharacter * 0.14f,
            0.0f,
            1.0f);
    }
    intent.targetAffectWidth = std::clamp(
        intent.targetAffectWidth * (1.0f - contrastBias * 0.05f),
        0.02f,
        0.30f);
    if (neutralHighlightGuard > 0.0001f) {
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression * (1.0f - 0.12f * neutralHighlightGuard),
            0.0f,
            2.2f);
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection - 0.08f * neutralHighlightGuard,
            0.0f,
            1.0f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection - 0.18f * stableStrengthNeutrality,
            0.0f,
            1.0f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength * (1.0f - 0.35f * stableStrengthNeutrality),
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening * (1.0f - 0.28f * stableStrengthNeutrality),
            0.0f,
            2.2f);
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression * (1.0f - 0.40f * stableStrengthNeutrality),
            0.0f,
            2.2f);
    }
    if (scenePrepToneOverlapGuard > 0.0001f) {
        intent.localBaseline.strength = std::clamp(
            intent.localBaseline.strength * (1.0f - 0.28f * scenePrepToneOverlapGuard),
            0.0f,
            1.6f);
        intent.localBaseline.shadowOpening = std::clamp(
            intent.localBaseline.shadowOpening * (1.0f - 0.18f * scenePrepToneOverlapGuard),
            0.0f,
            2.2f);
        intent.localBaseline.highlightCompression = std::clamp(
            intent.localBaseline.highlightCompression * (1.0f - 0.62f * scenePrepToneOverlapGuard),
            0.0f,
            2.2f);
        intent.targetHighlightProtection = std::clamp(
            intent.targetHighlightProtection * (1.0f - 0.18f * scenePrepToneOverlapGuard),
            0.0f,
            1.0f);
        intent.targetAffectWidth = std::clamp(
            intent.targetAffectWidth * (1.0f - 0.10f * scenePrepToneOverlapGuard),
            0.02f,
            0.30f);
    }

    intent.foundationAdaptiveAssist = false;
    intent.foundationAssistStrength = 0.0f;
    intent.foundationPreserveHue = true;
    std::array<float, 5> authoredFoundation = kCanonicalFoundationRegionEv;
    for (int i = 0; i < 5; ++i) {
        authoredFoundation[static_cast<std::size_t>(i)] =
            std::clamp(
                LerpFloat(
                    authoredFoundation[static_cast<std::size_t>(i)],
                    stats.recommendedFoundationEv[static_cast<std::size_t>(i)],
                    baseBlend),
                -5.0f,
                5.0f);
    }

    const float assistBlend = std::clamp(0.35f + stats.highlightPressure * 0.18f + flatness * 0.12f, 0.0f, 0.72f) *
        (baseBlend + extraAutoBoost * 0.25f);
    if (assistBlend > 0.0001f) {
        std::array<float, 5> smoothed = authoredFoundation;
        const float sigma = std::clamp(0.95f + intent.foundationBandWidth * 0.16f, 0.95f, 1.80f);
        for (int i = 0; i < 5; ++i) {
            float weightedSum = 0.0f;
            float weightTotal = 0.0f;
            for (int j = 0; j < 5; ++j) {
                const float d = static_cast<float>(i - j);
                const float weight = std::exp(-0.5f * (d * d) / std::max(0.05f, sigma * sigma));
                weightedSum += authoredFoundation[static_cast<std::size_t>(j)] * weight;
                weightTotal += weight;
            }
            smoothed[static_cast<std::size_t>(i)] = weightedSum / std::max(0.0001f, weightTotal);
        }
        for (int i = 0; i < 5; ++i) {
            authoredFoundation[static_cast<std::size_t>(i)] = std::clamp(
                LerpFloat(authoredFoundation[static_cast<std::size_t>(i)], smoothed[static_cast<std::size_t>(i)], assistBlend),
                -5.0f,
                5.0f);
        }
    }
    if (dynamicRangeDelta > 0.0001f) {
        const float liftRollback = dynamicRangeDelta * (0.20f + liftNeed * 0.30f);
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - liftRollback * (0.70f + stats.noiseRisk * 0.15f), -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - liftRollback * 0.48f, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] - dynamicRangeDelta * liftNeed * 0.06f, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + dynamicRangeDelta * 0.04f * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + dynamicRangeDelta * 0.08f * highlightGuard, -5.0f, 5.0f);
    } else if (dynamicRangeDelta < -0.0001f) {
        const float flatter = -dynamicRangeDelta;
        authoredFoundation[0] = std::clamp(authoredFoundation[0] + flatter * 0.18f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] + flatter * 0.12f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - flatter * 0.06f, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - flatter * 0.10f, -5.0f, 5.0f);
    }
    if (extraAutoBoost > 0.0001f) {
        const float shadowRecover = extraAutoBoost * liftNeed * highlightGuard * noiseGuard;
        authoredFoundation[0] = std::clamp(authoredFoundation[0] + 0.22f * shadowRecover, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] + 0.16f * shadowRecover, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] + 0.08f * shadowRecover, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - 0.02f * extraAutoBoost * stats.highlightPressure, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - 0.05f * extraAutoBoost * stats.highlightPressure, -5.0f, 5.0f);
    }
    if (std::abs(shadowBias) > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] + shadowBias * 0.24f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] + shadowBias * 0.16f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] + shadowBias * 0.05f * liftNeed, -5.0f, 5.0f);
    }
    if (std::abs(highlightBias) > 0.0001f) {
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - highlightBias * 0.10f, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - highlightBias * 0.22f, -5.0f, 5.0f);
    }
    if (highlightPunchNeed > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - 0.10f * highlightPunchNeed * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - 0.06f * highlightPunchNeed * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.05f * highlightPunchNeed * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.10f * highlightPunchNeed * highlightGuard, -5.0f, 5.0f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - 0.05f * positiveHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - 0.03f * positiveHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.08f * positiveHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.18f * positiveHighlightCharacter, -5.0f, 5.0f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        authoredFoundation[3] = std::clamp(authoredFoundation[3] - 0.06f * negativeHighlightCharacter, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] - 0.14f * negativeHighlightCharacter, -5.0f, 5.0f);
    }
    if (std::abs(contrastBias) > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - contrastBias * 0.16f, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - contrastBias * 0.12f, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + contrastBias * 0.08f * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + contrastBias * 0.16f * highlightGuard, -5.0f, 5.0f);
        intent.foundationBandWidth = std::clamp(intent.foundationBandWidth * (1.0f - contrastBias * 0.06f), 0.5f, 8.0f);
    }
    if (sceneContrastRestore > 0.0001f) {
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - sceneContrastRestore * 0.10f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - sceneContrastRestore * 0.06f * noiseGuard, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + sceneContrastRestore * 0.04f * highlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + sceneContrastRestore * 0.08f * highlightGuard, -5.0f, 5.0f);
        intent.foundationBandWidth = std::clamp(intent.foundationBandWidth * (1.0f - sceneContrastRestore * 0.03f), 0.5f, 8.0f);
    }
    if (neutralHighlightGuard > 0.0001f) {
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.03f * neutralHighlightGuard, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.06f * neutralHighlightGuard, -5.0f, 5.0f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        for (float& value : authoredFoundation) {
            value = LerpFloat(value, 0.0f, 0.50f * stableStrengthNeutrality);
        }
        authoredFoundation[3] = std::clamp(authoredFoundation[3] + 0.08f * stableStrengthNeutrality, -5.0f, 5.0f);
        authoredFoundation[4] = std::clamp(authoredFoundation[4] + 0.14f * stableStrengthNeutrality, -5.0f, 5.0f);
    }
    if (scenePrepToneOverlapGuard > 0.0001f) {
        const float lowLightContrast = scenePrepToneOverlapGuard * (0.65f + 0.35f * liftNeed);
        authoredFoundation[0] = std::clamp(authoredFoundation[0] - 0.18f * lowLightContrast, -5.0f, 5.0f);
        authoredFoundation[1] = std::clamp(authoredFoundation[1] - 0.10f * lowLightContrast, -5.0f, 5.0f);
        authoredFoundation[2] = std::clamp(authoredFoundation[2] + 0.04f * lowLightContrast, -5.0f, 5.0f);
        authoredFoundation[3] = std::clamp(
            std::max(authoredFoundation[3] + 0.10f * lowLightContrast, -0.06f * (1.0f - scenePrepToneOverlapGuard)),
            -5.0f,
            5.0f);
        authoredFoundation[4] = std::clamp(
            std::max(authoredFoundation[4] + 0.18f * lowLightContrast, -0.08f * (1.0f - scenePrepToneOverlapGuard)),
            -5.0f,
            5.0f);
    }
    intent.foundationRegionEv = authoredFoundation;

    std::array<float, 5> residualProfile { 0.08f, 0.04f, 0.00f, -0.04f, -0.10f };
    switch (stats.profile) {
        case ToneCurveAutoSceneProfile::HighlightHeavy:
            residualProfile = { 0.10f, 0.06f, -0.01f, -0.10f, -0.22f };
            break;
        case ToneCurveAutoSceneProfile::ShadowHeavy:
            residualProfile = { 0.14f, 0.08f, 0.02f, -0.03f, -0.12f };
            break;
        case ToneCurveAutoSceneProfile::Flat:
            residualProfile = { 0.04f, 0.02f, 0.00f, -0.02f, -0.05f };
            break;
        case ToneCurveAutoSceneProfile::NoisyLowLight:
            residualProfile = { 0.02f, 0.01f, 0.00f, -0.03f, -0.08f };
            break;
        case ToneCurveAutoSceneProfile::Balanced:
        default:
            break;
    }
    const float residualScale = std::clamp(
        (0.35f + stats.highlightPressure * 0.30f + (1.0f - stats.noiseRisk) * 0.10f) *
            (0.78f + dynamicRangeControl * 0.22f) +
            extraAutoBoost * 0.04f,
        0.16f,
        0.92f) * (baseBlend + extraAutoBoost * 0.35f);
    for (int i = 0; i < 5; ++i) {
        intent.pointResidualEv[static_cast<std::size_t>(i)] =
            std::clamp(residualProfile[static_cast<std::size_t>(i)] * residualScale, -0.28f, 0.22f);
    }
    if (dynamicRangeDelta > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - 0.10f * dynamicRangeDelta, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - 0.06f * dynamicRangeDelta, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.05f * dynamicRangeDelta, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.10f * dynamicRangeDelta, -0.28f, 0.22f);
    } else if (dynamicRangeDelta < -0.0001f) {
        const float flatter = -dynamicRangeDelta;
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + 0.06f * flatter * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + 0.04f * flatter * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.04f * flatter, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.07f * flatter, -0.28f, 0.22f);
    }
    if (extraAutoBoost > 0.0001f) {
        const float shadowCurveBoost = extraAutoBoost * liftNeed * noiseGuard;
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + 0.04f * shadowCurveBoost, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + 0.03f * shadowCurveBoost, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.02f * extraAutoBoost * stats.highlightPressure, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.03f * extraAutoBoost * stats.highlightPressure, -0.28f, 0.22f);
    }
    if (std::abs(shadowBias) > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + shadowBias * 0.05f * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + shadowBias * 0.03f * noiseGuard, -0.28f, 0.22f);
    }
    if (std::abs(highlightBias) > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - highlightBias * 0.04f, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - highlightBias * 0.08f, -0.28f, 0.22f);
    }
    if (highlightPunchNeed > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.02f * highlightPunchNeed, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.04f * highlightPunchNeed, -0.28f, 0.22f);
    }
    if (positiveHighlightCharacter > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.04f * positiveHighlightCharacter, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.08f * positiveHighlightCharacter, -0.28f, 0.22f);
    } else if (negativeHighlightCharacter > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.03f * negativeHighlightCharacter, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.06f * negativeHighlightCharacter, -0.28f, 0.22f);
    }
    if (std::abs(contrastBias) > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - contrastBias * 0.05f, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - contrastBias * 0.03f, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + contrastBias * 0.03f, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + contrastBias * 0.06f, -0.28f, 0.22f);
    }
    if (sceneContrastRestore > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - sceneContrastRestore * 0.030f * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - sceneContrastRestore * 0.020f * noiseGuard, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + sceneContrastRestore * 0.020f * highlightGuard, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + sceneContrastRestore * 0.040f * highlightGuard, -0.28f, 0.22f);
    }
    if (neutralHighlightGuard > 0.0001f) {
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.01f * neutralHighlightGuard, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.02f * neutralHighlightGuard, -0.28f, 0.22f);
    }
    if (stableStrengthNeutrality > 0.0001f) {
        for (float& residual : intent.pointResidualEv) {
            residual = std::clamp(residual * (1.0f - 0.65f * stableStrengthNeutrality), -0.28f, 0.22f);
        }
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.020f * stableStrengthNeutrality, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.040f * stableStrengthNeutrality, -0.28f, 0.22f);
    }
    if (scenePrepToneOverlapGuard > 0.0001f) {
        intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] - 0.035f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] - 0.020f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[2] = std::clamp(intent.pointResidualEv[2] + 0.010f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] + 0.040f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
        intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] + 0.070f * scenePrepToneOverlapGuard, -0.28f, 0.22f);
    }

    switch (m_AutoCalibrateVariant) {
        case ToneCurveAutoVariant::OpenShadows: {
            const float shadowBoost = 1.0f - stats.noiseRisk * 0.55f;
            intent.localBaselineEnabled = true;
            intent.localBaseline.strength = std::clamp(intent.localBaseline.strength + 0.06f * shadowBoost, 0.0f, 1.6f);
            intent.localBaseline.shadowOpening = std::clamp(intent.localBaseline.shadowOpening + 0.20f * shadowBoost, 0.0f, 2.2f);
            intent.foundationRegionEv[0] = std::clamp(intent.foundationRegionEv[0] + 0.18f * shadowBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[1] = std::clamp(intent.foundationRegionEv[1] + 0.12f * shadowBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[2] = std::clamp(intent.foundationRegionEv[2] + 0.04f * shadowBoost, -5.0f, 5.0f);
            intent.pointResidualEv[0] = std::clamp(intent.pointResidualEv[0] + 0.02f * shadowBoost, -0.28f, 0.22f);
            intent.pointResidualEv[1] = std::clamp(intent.pointResidualEv[1] + 0.02f * shadowBoost, -0.28f, 0.22f);
            break;
        }
        case ToneCurveAutoVariant::ProtectHighlights: {
            const float highlightProtect = 0.70f + stats.highlightPressure * 0.30f;
            intent.middleGrey = std::clamp(intent.middleGrey * std::exp2(-0.10f * highlightProtect), 0.02f, 0.50f);
            intent.logMaxEv = std::clamp(intent.logMaxEv + 0.25f * highlightProtect, 2.0f, 10.0f);
            intent.localBaselineEnabled = true;
            intent.localBaseline.highlightCompression = std::clamp(intent.localBaseline.highlightCompression + 0.16f * highlightProtect, 0.0f, 2.2f);
            intent.foundationRegionEv[3] = std::clamp(intent.foundationRegionEv[3] - 0.08f * highlightProtect, -5.0f, 5.0f);
            intent.foundationRegionEv[4] = std::clamp(intent.foundationRegionEv[4] - 0.18f * highlightProtect, -5.0f, 5.0f);
            intent.pointResidualEv[3] = std::clamp(intent.pointResidualEv[3] - 0.03f * highlightProtect, -0.28f, 0.22f);
            intent.pointResidualEv[4] = std::clamp(intent.pointResidualEv[4] - 0.05f * highlightProtect, -0.28f, 0.22f);
            break;
        }
        case ToneCurveAutoVariant::MoreContrast: {
            const float contrastBoost = 0.75f + (1.0f - flatness) * 0.25f;
            intent.localBaseline.strength = std::clamp(intent.localBaseline.strength * (1.0f - 0.10f * contrastBoost), 0.0f, 1.6f);
            intent.foundationBandWidth = std::clamp(intent.foundationBandWidth * (1.0f - 0.06f * contrastBoost), 0.5f, 8.0f);
            intent.foundationRegionEv[0] = std::clamp(intent.foundationRegionEv[0] - 0.12f * contrastBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[1] = std::clamp(intent.foundationRegionEv[1] - 0.08f * contrastBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[3] = std::clamp(intent.foundationRegionEv[3] + 0.04f * contrastBoost, -5.0f, 5.0f);
            intent.foundationRegionEv[4] = std::clamp(intent.foundationRegionEv[4] + 0.08f * contrastBoost, -5.0f, 5.0f);
            for (float& residual : intent.pointResidualEv) {
                residual = std::clamp(residual * (1.0f + 0.16f * contrastBoost), -0.28f, 0.22f);
            }
            break;
        }
        case ToneCurveAutoVariant::Recommended:
        default:
            break;
    }

    return intent;
}

ToneCurveLayer::AutoAuthoredState ToneCurveLayer::BuildAutoAuthoredStateFromIntent(const AutoToneIntent& intent) const {
    AutoAuthoredState state;
    state.localBaselineEnabled = intent.localBaselineEnabled;
    state.localBaseline = intent.localBaseline;
    state.middleGrey = intent.middleGrey;
    state.logMinEv = intent.logMinEv;
    state.logMaxEv = intent.logMaxEv;
    state.targetAffectWidth = intent.targetAffectWidth;
    state.targetShadowProtection = intent.targetShadowProtection;
    state.targetHighlightProtection = intent.targetHighlightProtection;
    state.foundationAdaptiveAssist = intent.foundationAdaptiveAssist;
    state.foundationAssistStrength = intent.foundationAssistStrength;
    state.foundationBandWidth = intent.foundationBandWidth;
    state.foundationPreserveHue = intent.foundationPreserveHue;
    state.foundationRegionEv = intent.foundationRegionEv;

    auto sceneToCurveCoord = [&](float sceneValue) {
        if (m_Domain == ToneCurveDomain::LogScene) {
            const float ev = std::log2(std::max(sceneValue, 0.000001f) / std::max(state.middleGrey, 0.000001f));
            return Clamp01((ev - state.logMinEv) / std::max(0.0001f, state.logMaxEv - state.logMinEv));
        }
        return Clamp01(sceneValue);
    };

    auto applyLocalBaseline = [&](float sceneValue) {
        if (!state.localBaselineEnabled) {
            return std::max(sceneValue, 0.000001f);
        }
        const float safeSceneValue = std::max(sceneValue, 0.000001f);
        const float baseEv = std::log2(safeSceneValue / std::max(state.middleGrey, 0.000001f));
        const float shadowWeight = 1.0f - std::clamp((baseEv + 0.12f) / 2.48f, 0.0f, 1.0f);
        const float highlightWeight = std::clamp((baseEv - 0.12f) / 2.68f, 0.0f, 1.0f);
        const float shadowGain = std::max(0.0f, -baseEv) * shadowWeight * std::max(0.0f, state.localBaseline.shadowOpening);
        const float highlightGain = std::max(0.0f, baseEv) * highlightWeight * std::max(0.0f, state.localBaseline.highlightCompression);
        float gainEv = (shadowGain - highlightGain) * std::max(0.0f, state.localBaseline.strength);
        gainEv = std::clamp(gainEv, -4.5f, 4.0f);
        const float nearBlack = 1.0f - std::clamp((safeSceneValue - 0.003f) / 0.047f, 0.0f, 1.0f);
        const float nearClip = std::clamp((safeSceneValue - 0.95f) / 2.55f, 0.0f, 1.0f);
        if (gainEv > 0.0f) {
            gainEv *= 1.0f - 0.30f * state.localBaseline.rangeProtection * nearBlack;
        } else if (gainEv < 0.0f) {
            gainEv *= 1.0f - 0.32f * state.localBaseline.rangeProtection * nearClip;
        }
        return safeSceneValue * std::exp2(gainEv);
    };

    constexpr float kInternalMinSpacing = 0.045f;
    constexpr float kInternalMinYStep = 0.010f;
    const std::array<float, 5> evOffsets {
        -2.0f * state.foundationBandWidth,
        -1.0f * state.foundationBandWidth,
         0.0f,
         1.0f * state.foundationBandWidth,
         2.0f * state.foundationBandWidth
    };

    state.points.reserve(7);
    state.points.push_back({ 0.0f, 0.0f, ToneCurveSegmentShape::Linear });
    float previousX = 0.0f;
    float previousY = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const float sceneInput = std::max(state.middleGrey * std::exp2(evOffsets[static_cast<std::size_t>(i)]), 0.000001f);
        const float localScene = applyLocalBaseline(sceneInput);
        const float foundationScene = ApplyFoundationToSceneValue(
            localScene,
            state.middleGrey,
            state.foundationBandWidth,
            state.foundationRegionEv);
        const float baseCoord = sceneToCurveCoord(foundationScene);
        const float residualScene = std::max(
            foundationScene * std::exp2(intent.pointResidualEv[static_cast<std::size_t>(i)]),
            0.000001f);
        float x = sceneToCurveCoord(sceneInput);
        float y = sceneToCurveCoord(residualScene);
        y = std::clamp(y, baseCoord - 0.085f, baseCoord + 0.085f);
        const float minX = kInternalMinSpacing * static_cast<float>(i + 1);
        const float maxX = 1.0f - kInternalMinSpacing * static_cast<float>(5 - i);
        x = std::clamp(std::max(x, previousX + kInternalMinSpacing), minX, maxX);
        const float minY = previousY + kInternalMinYStep;
        const float maxY = 1.0f - kInternalMinYStep * static_cast<float>(5 - i);
        y = std::clamp(std::max(y, minY), minY, maxY);
        state.points.push_back({ x, y, ToneCurveSegmentShape::Linear });
        previousX = x;
        previousY = y;
    }
    state.points.push_back({ 1.0f, 1.0f, ToneCurveSegmentShape::Linear });
    return state;
}

ToneCurveLayer::AutoAuthoredState ToneCurveLayer::CaptureCurrentAutoAuthoredState() const {
    AutoAuthoredState state;
    state.localBaselineEnabled = m_LocalBaselineEnabled;
    state.localBaseline.strength = m_LocalBaselineStrength;
    state.localBaseline.shadowOpening = m_LocalShadowOpening;
    state.localBaseline.highlightCompression = m_LocalHighlightCompression;
    state.localBaseline.radius = m_LocalBaselineRadius;
    state.localBaseline.edgeProtection = m_LocalEdgeProtection;
    state.localBaseline.rangeProtection = m_LocalRangeProtection;
    state.middleGrey = m_MiddleGrey;
    state.logMinEv = m_LogMinEv;
    state.logMaxEv = m_LogMaxEv;
    state.targetAffectWidth = m_TargetAffectWidth;
    state.targetShadowProtection = m_TargetShadowProtection;
    state.targetHighlightProtection = m_TargetHighlightProtection;
    state.foundationAdaptiveAssist = m_FoundationAdaptiveAssist;
    state.foundationAssistStrength = m_FoundationAssistStrength;
    state.foundationBandWidth = m_FoundationBandWidth;
    state.foundationPreserveHue = m_FoundationPreserveHue;
    state.foundationRegionEv = GetFoundationRegionValues();
    state.points = m_PreparedPoints;
    return state;
}

ToneCurveLayer::AutoAuthoredState ToneCurveLayer::ApplyUserAdjustmentsToAutoAuthoredState(const AutoAuthoredState& state) const {
    if (!m_LastAutoAuthoredStateValid) {
        return state;
    }

    const AutoAuthoredState current = CaptureCurrentAutoAuthoredState();
    const AutoAuthoredState& authoredBase = m_LastAutoAuthoredState;
    AutoAuthoredState adjusted = state;

    auto applyDelta = [](float authoredValue, float currentValue, float previousAuthoredValue) {
        return authoredValue + (currentValue - previousAuthoredValue);
    };

    adjusted.localBaselineEnabled =
        current.localBaselineEnabled != authoredBase.localBaselineEnabled
            ? current.localBaselineEnabled
            : state.localBaselineEnabled;
    adjusted.localBaseline.strength = applyDelta(state.localBaseline.strength, current.localBaseline.strength, authoredBase.localBaseline.strength);
    adjusted.localBaseline.shadowOpening = applyDelta(state.localBaseline.shadowOpening, current.localBaseline.shadowOpening, authoredBase.localBaseline.shadowOpening);
    adjusted.localBaseline.highlightCompression = applyDelta(state.localBaseline.highlightCompression, current.localBaseline.highlightCompression, authoredBase.localBaseline.highlightCompression);
    adjusted.localBaseline.radius = applyDelta(state.localBaseline.radius, current.localBaseline.radius, authoredBase.localBaseline.radius);
    adjusted.localBaseline.edgeProtection = applyDelta(state.localBaseline.edgeProtection, current.localBaseline.edgeProtection, authoredBase.localBaseline.edgeProtection);
    adjusted.localBaseline.rangeProtection = applyDelta(state.localBaseline.rangeProtection, current.localBaseline.rangeProtection, authoredBase.localBaseline.rangeProtection);
    adjusted.middleGrey = applyDelta(state.middleGrey, current.middleGrey, authoredBase.middleGrey);
    adjusted.logMinEv = applyDelta(state.logMinEv, current.logMinEv, authoredBase.logMinEv);
    adjusted.logMaxEv = applyDelta(state.logMaxEv, current.logMaxEv, authoredBase.logMaxEv);
    adjusted.targetAffectWidth = applyDelta(state.targetAffectWidth, current.targetAffectWidth, authoredBase.targetAffectWidth);
    adjusted.targetShadowProtection = applyDelta(state.targetShadowProtection, current.targetShadowProtection, authoredBase.targetShadowProtection);
    adjusted.targetHighlightProtection = applyDelta(state.targetHighlightProtection, current.targetHighlightProtection, authoredBase.targetHighlightProtection);
    adjusted.foundationAdaptiveAssist =
        current.foundationAdaptiveAssist != authoredBase.foundationAdaptiveAssist
            ? current.foundationAdaptiveAssist
            : state.foundationAdaptiveAssist;
    adjusted.foundationAssistStrength = applyDelta(state.foundationAssistStrength, current.foundationAssistStrength, authoredBase.foundationAssistStrength);
    adjusted.foundationBandWidth = applyDelta(state.foundationBandWidth, current.foundationBandWidth, authoredBase.foundationBandWidth);
    adjusted.foundationPreserveHue =
        current.foundationPreserveHue != authoredBase.foundationPreserveHue
            ? current.foundationPreserveHue
            : state.foundationPreserveHue;
    for (std::size_t i = 0; i < adjusted.foundationRegionEv.size(); ++i) {
        adjusted.foundationRegionEv[i] = applyDelta(
            state.foundationRegionEv[i],
            current.foundationRegionEv[i],
            authoredBase.foundationRegionEv[i]);
    }
    return adjusted;
}

void ToneCurveLayer::ApplyAuthoredStateForRender(const AutoAuthoredState& state) {
    const AutoAuthoredState effectiveState = ApplyUserAdjustmentsToAutoAuthoredState(state);
    m_LocalBaselineEnabled = effectiveState.localBaselineEnabled;
    m_LocalBaselineStrength = effectiveState.localBaseline.strength;
    m_LocalShadowOpening = effectiveState.localBaseline.shadowOpening;
    m_LocalHighlightCompression = effectiveState.localBaseline.highlightCompression;
    m_LocalBaselineRadius = effectiveState.localBaseline.radius;
    m_LocalEdgeProtection = effectiveState.localBaseline.edgeProtection;
    m_LocalRangeProtection = effectiveState.localBaseline.rangeProtection;
    m_MiddleGrey = effectiveState.middleGrey;
    m_LogMinEv = effectiveState.logMinEv;
    m_LogMaxEv = effectiveState.logMaxEv;
    m_TargetAffectWidth = effectiveState.targetAffectWidth;
    m_TargetShadowProtection = effectiveState.targetShadowProtection;
    m_TargetHighlightProtection = effectiveState.targetHighlightProtection;
    m_FoundationAdaptiveAssist = effectiveState.foundationAdaptiveAssist;
    m_FoundationAssistStrength = effectiveState.foundationAssistStrength;
    m_FoundationBandWidth = effectiveState.foundationBandWidth;
    m_FoundationPreserveHue = effectiveState.foundationPreserveHue;
    m_FoundationShadows = effectiveState.foundationRegionEv[0];
    m_FoundationDarks = effectiveState.foundationRegionEv[1];
    m_FoundationMidtones = effectiveState.foundationRegionEv[2];
    m_FoundationLights = effectiveState.foundationRegionEv[3];
    m_FoundationHighlights = effectiveState.foundationRegionEv[4];
    m_PreparedPoints = state.points;
    m_ActiveGraphView = ToneCurveGraphView::Finish;
    m_LastAutoAuthoredState = state;
    m_LastAutoAuthoredStateValid = true;
    SanitizePoints();
    m_LutDirty = true;
}

void ToneCurveLayer::CapturePendingAutoRewriteFeedback() {
    if (!m_AutoSceneStatsValid ||
        m_AutoRewriteNodeId <= 0 ||
        m_AutoRewriteRequestRevision == 0) {
        return;
    }

    m_PendingAutoRewriteFeedback.valid = true;
    m_PendingAutoRewriteFeedback.nodeId = m_AutoRewriteNodeId;
    m_PendingAutoRewriteFeedback.requestRevision = m_AutoRewriteRequestRevision;
    m_PendingAutoRewriteFeedback.authoredLayerJson = Serialize();
    m_PendingAutoRewriteFeedback.authoredStateHash = HashToneCurveJson(m_PendingAutoRewriteFeedback.authoredLayerJson);
    m_PendingAutoRewriteFeedback.statsValid = m_AutoSceneStatsValid;
    m_PendingAutoRewriteFeedback.shadowPercentile = m_AutoSceneShadowPercentile;
    m_PendingAutoRewriteFeedback.midtonePercentile = m_AutoSceneMidtonePercentile;
    m_PendingAutoRewriteFeedback.highlightPercentile = m_AutoSceneHighlightPercentile;
    m_PendingAutoRewriteFeedback.clippingRatio = m_AutoSceneClippingRatio;
    m_PendingAutoRewriteFeedback.noiseRisk = m_AutoSceneNoiseRisk;
    m_PendingAutoRewriteFeedback.highlightPressure = m_AutoSceneHighlightPressure;
    m_PendingAutoRewriteFeedback.textureConfidence = m_AutoSceneTextureConfidence;
    m_PendingAutoRewriteFeedback.hdrSpreadEv = m_AutoSceneHdrSpreadEv;
    m_PendingAutoRewriteFeedback.sceneProfile = static_cast<int>(m_AutoSceneProfile);
    m_PendingAutoRewriteFeedback.recommendedBaseEv = m_AutoRecommendedBaseEv;
    m_PendingAutoRewriteFeedback.recommendedLocalStrength = m_AutoRecommendedLocalStrength;
    m_PendingAutoRewriteFeedback.recommendedShadowOpening = m_AutoRecommendedShadowOpening;
    m_PendingAutoRewriteFeedback.recommendedHighlightCompression = m_AutoRecommendedHighlightCompression;
    for (int i = 0; i < 5; ++i) {
        m_PendingAutoRewriteFeedback.recommendedFoundationEv[i] = m_AutoRecommendedFoundationEv[static_cast<std::size_t>(i)];
    }
}

void ToneCurveLayer::ClearPendingAutoRewriteFeedback() {
    m_PendingAutoRewriteFeedback = {};
}

ToneCurveLayer::EffectiveLocalBaselineSettings ToneCurveLayer::ComputeEffectiveLocalBaselineSettings() const {
    EffectiveLocalBaselineSettings settings;
    settings.strength = m_LocalBaselineStrength;
    settings.shadowOpening = m_LocalShadowOpening;
    settings.highlightCompression = m_LocalHighlightCompression;
    settings.radius = m_LocalBaselineRadius;
    settings.edgeProtection = m_LocalEdgeProtection;
    settings.rangeProtection = m_LocalRangeProtection;
    return settings;
}

float ToneCurveLayer::ComputeEffectiveToneAnchor() const {
    return std::clamp(m_MiddleGrey, 0.01f, 1.0f);
}

float ToneCurveLayer::ComputeEffectiveFoundationAssistStrength() const {
    return std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f);
}

float ToneCurveLayer::ComputeEffectiveFoundationBandWidth() const {
    return std::clamp(m_FoundationBandWidth, 0.5f, 8.0f);
}

float ToneCurveLayer::ComputeEffectiveTargetAffectWidth() const {
    return std::clamp(m_TargetAffectWidth, 0.02f, 0.30f);
}

float ToneCurveLayer::ComputeEffectiveTargetShadowProtection() const {
    return std::clamp(m_TargetShadowProtection, 0.0f, 1.0f);
}

float ToneCurveLayer::ComputeEffectiveTargetHighlightProtection() const {
    return std::clamp(m_TargetHighlightProtection, 0.0f, 1.0f);
}

std::array<float, 5> ToneCurveLayer::ComputeEffectiveFoundationRegionValues() const {
    std::array<float, 5> base = GetFoundationRegionValues();
    const float assist = ComputeEffectiveFoundationAssistStrength();
    if (!m_FoundationAdaptiveAssist || assist <= 0.0001f) {
        return base;
    }

    std::array<float, 5> effective = base;
    const float sigma = 0.90f + 1.30f * assist;
    const float assistBlend = 0.72f * assist;
    for (int i = 0; i < 5; ++i) {
        float weightedSum = 0.0f;
        float weightTotal = 0.0f;
        for (int j = 0; j < 5; ++j) {
            const float d = static_cast<float>(i - j);
            const float weight = std::exp(-0.5f * (d * d) / std::max(0.05f, sigma * sigma));
            weightedSum += base[static_cast<std::size_t>(j)] * weight;
            weightTotal += weight;
        }
        const float smoothed = weightedSum / std::max(0.0001f, weightTotal);
        effective[static_cast<std::size_t>(i)] =
            base[static_cast<std::size_t>(i)] +
            (smoothed - base[static_cast<std::size_t>(i)]) * assistBlend;
    }

    const float shadowLiftIntent =
        std::max(0.0f, base[0]) * 0.72f +
        std::max(0.0f, base[1]) * 0.52f +
        std::max(0.0f, base[2]) * 0.16f;
    const float highlightCompressionIntent =
        std::max(0.0f, -base[4]) * 0.74f +
        std::max(0.0f, -base[3]) * 0.54f +
        std::max(0.0f, -base[2]) * 0.14f;
    const float highlightLiftIntent =
        std::max(0.0f, base[4]) * 0.52f +
        std::max(0.0f, base[3]) * 0.34f;
    const float shadowCompressionIntent =
        std::max(0.0f, -base[0]) * 0.50f +
        std::max(0.0f, -base[1]) * 0.30f;

    effective[0] += highlightCompressionIntent * 0.40f * assist;
    effective[1] += highlightCompressionIntent * 0.32f * assist;
    effective[2] += highlightCompressionIntent * 0.16f * assist;
    effective[3] -= shadowLiftIntent * 0.14f * assist;
    effective[4] -= shadowLiftIntent * 0.24f * assist;

    effective[0] -= highlightLiftIntent * 0.12f * assist;
    effective[1] -= highlightLiftIntent * 0.08f * assist;
    effective[3] += shadowCompressionIntent * 0.06f * assist;
    effective[4] += shadowCompressionIntent * 0.10f * assist;

    const float midtoneDrift = effective[2] - base[2];
    const float midtoneRecenter = 0.82f * assist;
    for (float& value : effective) {
        value = std::clamp(value - midtoneDrift * midtoneRecenter, -5.0f, 5.0f);
    }

    const float mean = (effective[0] + effective[1] + effective[2] + effective[3] + effective[4]) / 5.0f;
    const float recenter = 0.06f * assist;
    for (float& value : effective) {
        value = std::clamp(value - mean * recenter, -5.0f, 5.0f);
    }
    return effective;
}

std::array<float, 5> ToneCurveLayer::ComputeFoundationTargetWeights(float sceneValue) const {
    const float safeMiddleGrey = std::max(ComputeEffectiveToneAnchor(), 0.000001f);
    const float ev = std::log2(std::max(sceneValue, 0.000001f) / safeMiddleGrey);
    const float width = std::max(0.35f, ComputeEffectiveFoundationBandWidth());
    const float affectWidth = ComputeEffectiveTargetAffectWidth();
    const float sampleWidth = std::max(0.20f, width * (0.42f + 1.8f * affectWidth));
    std::array<float, 5> weights {};
    float sum = 0.0f;
    for (int i = 0; i < 5; ++i) {
        const float center = static_cast<float>(i - 2) * width;
        const float delta = (ev - center) / sampleWidth;
        const float weight = std::exp(-0.5f * delta * delta);
        weights[static_cast<std::size_t>(i)] = weight;
        sum += weight;
    }
    if (sum <= 0.0001f) {
        weights = { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
        return weights;
    }
    for (float& value : weights) {
        value /= sum;
    }
    return weights;
}

float ToneCurveLayer::ComputeApproximateLocalBaselineGainEv(float sceneValue) const {
    if (!m_LocalBaselineEnabled) {
        return 0.0f;
    }
    const EffectiveLocalBaselineSettings localBaseline = ComputeEffectiveLocalBaselineSettings();
    const std::array<float, 5> base = GetFoundationRegionValues();
    const float assist = (m_FoundationAdaptiveAssist ? std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f) : 0.0f);
    const float shadowLiftIntent =
        std::max(0.0f, base[0]) * 0.70f +
        std::max(0.0f, base[1]) * 0.45f +
        std::max(0.0f, base[2]) * 0.12f;
    const float highlightCompressionIntent =
        std::max(0.0f, -base[4]) * 0.70f +
        std::max(0.0f, -base[3]) * 0.45f +
        std::max(0.0f, -base[2]) * 0.10f;
    const float effectiveStrength = std::clamp(
        localBaseline.strength +
            (shadowLiftIntent + highlightCompressionIntent) * 0.10f * assist,
        0.0f,
        1.6f);
    const float effectiveShadowOpening = std::clamp(
        localBaseline.shadowOpening +
            highlightCompressionIntent * 0.22f * assist,
        0.0f,
        2.2f);
    const float effectiveHighlightCompression = std::clamp(
        localBaseline.highlightCompression +
            shadowLiftIntent * 0.22f * assist,
        0.0f,
        2.2f);

    const float safeMiddleGrey = std::max(ComputeEffectiveToneAnchor(), 0.000001f);
    const float safeSceneValue = std::max(sceneValue, 0.000001f);
    const float baseEv = std::log2(safeSceneValue / safeMiddleGrey);
    const float shadowWeight = 1.0f - std::clamp((baseEv + 0.12f) / 2.48f, 0.0f, 1.0f);
    const float highlightWeight = std::clamp((baseEv - 0.12f) / 2.68f, 0.0f, 1.0f);
    const float shadowGain = std::max(0.0f, -baseEv) * shadowWeight * std::max(0.0f, effectiveShadowOpening);
    const float highlightGain = std::max(0.0f, baseEv) * highlightWeight * std::max(0.0f, effectiveHighlightCompression);
    float gainEv = (shadowGain - highlightGain) * std::max(0.0f, effectiveStrength);
    gainEv = std::clamp(gainEv, -4.5f, 4.0f);

    const float nearBlack = 1.0f - std::clamp((safeSceneValue - 0.003f) / 0.047f, 0.0f, 1.0f);
    const float nearClip = std::clamp((safeSceneValue - 0.95f) / 2.55f, 0.0f, 1.0f);
    if (gainEv > 0.0f) {
        gainEv *= 1.0f - 0.30f * localBaseline.rangeProtection * nearBlack;
    } else if (gainEv < 0.0f) {
        gainEv *= 1.0f - 0.32f * localBaseline.rangeProtection * nearClip;
    }
    return gainEv;
}

float ToneCurveLayer::ApplyApproximateLocalBaselineToSceneValue(float sceneValue) const {
    const float safeSceneValue = std::max(sceneValue, 0.000001f);
    return safeSceneValue * std::exp2(ComputeApproximateLocalBaselineGainEv(safeSceneValue));
}

float ToneCurveLayer::ApplyFoundationToSceneValue(
    float sceneValue,
    float middleGrey,
    float bandWidth,
    const std::array<float, 5>& foundationRegionEv) const {
    const float safeSceneValue = std::max(sceneValue, 0.000001f);
    const float safeMiddleGrey = std::max(middleGrey, 0.000001f);
    const float ev = std::log2(safeSceneValue / safeMiddleGrey);
    const float width = std::max(0.35f, bandWidth);
    float weights[5];
    weights[0] = std::exp(-0.5f * std::pow((ev - (-2.0f * width)) / width, 2.0f));
    weights[1] = std::exp(-0.5f * std::pow((ev - (-1.0f * width)) / width, 2.0f));
    weights[2] = std::exp(-0.5f * std::pow((ev -   0.0f)          / width, 2.0f));
    weights[3] = std::exp(-0.5f * std::pow((ev - ( 1.0f * width)) / width, 2.0f));
    weights[4] = std::exp(-0.5f * std::pow((ev - ( 2.0f * width)) / width, 2.0f));
    float weightSum = weights[0] + weights[1] + weights[2] + weights[3] + weights[4];
    weightSum = std::max(0.0001f, weightSum);
    const float gainEv =
        (foundationRegionEv[0] * weights[0] +
         foundationRegionEv[1] * weights[1] +
         foundationRegionEv[2] * weights[2] +
         foundationRegionEv[3] * weights[3] +
         foundationRegionEv[4] * weights[4]) / weightSum;
    return safeSceneValue * std::exp2(gainEv);
}

float ToneCurveLayer::ApplyFoundationToSceneValue(float sceneValue) const {
    return ApplyFoundationToSceneValue(
        sceneValue,
        ComputeEffectiveToneAnchor(),
        ComputeEffectiveFoundationBandWidth(),
        ComputeEffectiveFoundationRegionValues());
}

float ToneCurveLayer::EvaluateCombinedOutputCoord(float inputCoord) const {
    const float sceneInput = CurveCoordToScene(inputCoord);
    const float localScene = ApplyApproximateLocalBaselineToSceneValue(sceneInput);
    const float foundationScene = ApplyFoundationToSceneValue(localScene);
    const float pointCurveCoord = EvaluateCombinedPointCurve(SceneToCurveCoord(foundationScene));
    const float sceneOutput = CurveCoordToScene(pointCurveCoord);
    return SceneToCurveCoord(sceneOutput);
}

void ToneCurveLayer::ApplyRegionTargetDelta(float deltaCurveY) {
    if (!m_ProbeValid) {
        return;
    }

    const float shadowProtection = ComputeEffectiveTargetShadowProtection();
    const float highlightProtection = ComputeEffectiveTargetHighlightProtection();
    float deltaScale = 1.0f;
    if (m_ProbeInputX < 0.35f) {
        const float shadowRegion = 1.0f - std::clamp(m_ProbeInputX / 0.35f, 0.0f, 1.0f);
        deltaScale *= 1.0f - 0.45f * shadowProtection * shadowRegion;
    }
    if (m_ProbeInputX > 0.65f) {
        const float highlightRegion = std::clamp((m_ProbeInputX - 0.65f) / 0.35f, 0.0f, 1.0f);
        deltaScale *= 1.0f - 0.45f * highlightProtection * highlightRegion;
    }

    float deltaEv = deltaCurveY * 8.8f * deltaScale;
    std::array<float, 5> weights = ComputeFoundationTargetWeights(m_ProbeSceneValue);
    if (m_AutoSceneStatsValid) {
        const float shadowIntent = weights[0] + weights[1] * 0.75f;
        const float highlightIntent = weights[4] + weights[3] * 0.75f;
        if (deltaCurveY > 0.0f) {
            deltaEv *= 1.0f + shadowIntent * (0.30f - 0.18f * m_AutoSceneNoiseRisk);
        } else if (deltaCurveY < 0.0f) {
            deltaEv *= 1.0f + highlightIntent * (0.34f + 0.22f * m_AutoSceneHighlightPressure);
        }
    }
    if (m_FoundationAdaptiveAssist && m_FoundationAssistStrength > 0.0001f) {
        std::array<float, 5> broadened = weights;
        for (int i = 0; i < 5; ++i) {
            float neighborMix = weights[static_cast<std::size_t>(i)] * 0.55f;
            if (i > 0) neighborMix += weights[static_cast<std::size_t>(i - 1)] * 0.25f;
            if (i + 1 < 5) neighborMix += weights[static_cast<std::size_t>(i + 1)] * 0.25f;
            if (i > 1) neighborMix += weights[static_cast<std::size_t>(i - 2)] * 0.08f;
            if (i + 2 < 5) neighborMix += weights[static_cast<std::size_t>(i + 2)] * 0.08f;
            broadened[static_cast<std::size_t>(i)] = neighborMix;
        }
        float broadenedSum = 0.0f;
        for (float weight : broadened) {
            broadenedSum += weight;
        }
        broadenedSum = std::max(0.0001f, broadenedSum);
        for (float& weight : broadened) {
            weight /= broadenedSum;
        }
        const float mixAmount = 0.55f * std::clamp(m_FoundationAssistStrength, 0.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
            weights[static_cast<std::size_t>(i)] =
                weights[static_cast<std::size_t>(i)] +
                (broadened[static_cast<std::size_t>(i)] - weights[static_cast<std::size_t>(i)]) * mixAmount;
        }
    }

    if (m_LocalBaselineEnabled) {
        const float shadowIntent = std::clamp(weights[0] + weights[1] * 0.85f + weights[2] * 0.20f, 0.0f, 1.0f);
        const float highlightIntent = std::clamp(weights[4] + weights[3] * 0.85f + weights[2] * 0.20f, 0.0f, 1.0f);
        if (deltaCurveY > 0.0f && shadowIntent > 0.01f) {
            m_LocalShadowOpening = std::clamp(
                m_LocalShadowOpening + deltaCurveY * (0.90f + 0.70f * m_LocalBaselineStrength) * shadowIntent,
                0.0f,
                2.2f);
            m_LocalBaselineStrength = std::clamp(
                m_LocalBaselineStrength + deltaCurveY * 0.22f * shadowIntent,
                0.0f,
                1.6f);
        } else if (deltaCurveY < 0.0f && highlightIntent > 0.01f) {
            const float highlightDelta = -deltaCurveY;
            m_LocalHighlightCompression = std::clamp(
                m_LocalHighlightCompression + highlightDelta * (0.90f + 0.70f * m_LocalBaselineStrength) * highlightIntent,
                0.0f,
                2.2f);
            m_LocalBaselineStrength = std::clamp(
                m_LocalBaselineStrength + highlightDelta * 0.18f * highlightIntent,
                0.0f,
                1.6f);
        }
    }

    m_FoundationShadows = std::clamp(m_FoundationShadows + deltaEv * weights[0], -5.0f, 5.0f);
    m_FoundationDarks = std::clamp(m_FoundationDarks + deltaEv * weights[1], -5.0f, 5.0f);
    m_FoundationMidtones = std::clamp(m_FoundationMidtones + deltaEv * weights[2], -5.0f, 5.0f);
    m_FoundationLights = std::clamp(m_FoundationLights + deltaEv * weights[3], -5.0f, 5.0f);
    m_FoundationHighlights = std::clamp(m_FoundationHighlights + deltaEv * weights[4], -5.0f, 5.0f);
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
