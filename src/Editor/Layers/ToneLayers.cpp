#include "ToneLayers.h"

#include "Editor/EditorModule.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace {

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

ToneEqualizerLayer::ToneEqualizerLayer() = default;

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
