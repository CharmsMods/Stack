#include "ToneLayers.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

std::vector<ToneCurvePoint> BuildLinearToneCurvePoints() {
    return {
        { 0.0f, 0.0f, ToneCurveSegmentShape::Linear },
        { 1.0f, 1.0f, ToneCurveSegmentShape::Linear }
    };
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

} // namespace

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