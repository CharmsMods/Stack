#include "Raw/RawAutoStartPoint.h"

#include "Raw/RawAutoBase.h"
#include "Raw/RawImageAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace Stack::RawAutoStartPoint {
namespace {

constexpr float kStartingPointRawExposureConfidenceGate = 0.70f;
constexpr float kStartingPointRawExposureCautiousConfidenceGate = 0.50f;
constexpr float kStartingPointRawExposureTinyLiftConfidenceGate = 0.35f;
constexpr float kStartingPointRawExposureDownwardConfidenceGate = 0.35f;
constexpr float kStartingPointRawExposureOneClickCapEv = 0.50f;
constexpr float kStartingPointRawExposureCautiousCapEv = 0.25f;
constexpr float kStartingPointRawExposureTinyLiftCapEv = 0.15f;
constexpr float kStartingPointRawExposureDownwardCapEv = 0.25f;
constexpr float kStartingPointRawExposureMinMoveEv = 0.15f;
constexpr float kStartingPointLocalRangeStrictConfidenceGate = 0.85f;
constexpr float kStartingPointLocalRangeCautiousConfidenceGate = 0.70f;
constexpr float kStartingPointLocalRangeStrictMaxAbsDeltaEv = 0.60f;
constexpr float kStartingPointLocalRangeCautiousMaxAbsDeltaEv = 0.60f;
constexpr float kStartingPointLocalRangeContextConfidenceGate = 0.50f;
constexpr float kStartingPointLocalRangeContextMaxAbsDeltaEv = 0.45f;
constexpr float kStartingPointDarkRevealGate = 0.20f;
constexpr float kStartingPointDarkRevealLocalConfidenceGate = 0.45f;
constexpr float kStartingPointDarkRevealLocalMaxAbsDeltaEv = 1.15f;
constexpr float kStartingPointDarkRevealFinishToneStrengthLimit = 0.35f;
constexpr float kStartingPointFinishToneStrictStrengthLimit = 0.12f;
constexpr float kStartingPointFinishToneCautiousStrengthLimit = 0.20f;

nlohmann::json JsonFloat(float value) {
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json();
}

nlohmann::json JsonStringOrNull(const std::string& value) {
    return value.empty() ? nlohmann::json() : nlohmann::json(value);
}

float JsonNumberOr(const nlohmann::json& object, const char* key, float fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    return it != object.end() && it->is_number()
        ? it->get<float>()
        : fallback;
}

int JsonIntOr(const nlohmann::json& object, const char* key, int fallback) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto it = object.find(key);
    return it != object.end() && it->is_number()
        ? static_cast<int>(std::round(it->get<float>()))
        : fallback;
}

nlohmann::json SerializeFloat3(const std::array<float, 3>& values) {
    return nlohmann::json::array({
        JsonFloat(values[0]),
        JsonFloat(values[1]),
        JsonFloat(values[2])
    });
}

nlohmann::json SerializeStringVector(const std::vector<std::string>& values) {
    nlohmann::json result = nlohmann::json::array();
    for (const std::string& value : values) {
        result.push_back(value);
    }
    return result;
}

nlohmann::json SerializePercentiles(const RawAutoStartPointPercentiles& stats) {
    return nlohmann::json {
        { "valid", stats.valid },
        { "p01", JsonFloat(stats.p01) },
        { "p05", JsonFloat(stats.p05) },
        { "p10", JsonFloat(stats.p10) },
        { "p25", JsonFloat(stats.p25) },
        { "p50", JsonFloat(stats.p50) },
        { "p75", JsonFloat(stats.p75) },
        { "p90", JsonFloat(stats.p90) },
        { "p95", JsonFloat(stats.p95) },
        { "p99", JsonFloat(stats.p99) },
        { "p999", JsonFloat(stats.p999) }
    };
}

nlohmann::json SerializeEvBuckets(const std::vector<RawAutoStartPointEvBucketValue>& buckets) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointEvBucketValue& bucket : buckets) {
        result.push_back({
            { "lowEv", JsonFloat(bucket.lowEv) },
            { "highEv", JsonFloat(bucket.highEv) },
            { "value", JsonFloat(bucket.value) },
            { "label", JsonStringOrNull(bucket.label) }
        });
    }
    return result;
}

nlohmann::json SerializeToneBucketSpatialSummaries(
    const std::vector<RawAutoStartPointToneBucketSpatialSummary>& summaries) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointToneBucketSpatialSummary& summary : summaries) {
        result.push_back({
            { "bucketId", JsonStringOrNull(summary.bucketId) },
            { "areaFraction", JsonFloat(summary.areaFraction) },
            { "centerWeight", JsonFloat(summary.centerWeight) },
            { "topBandWeight", JsonFloat(summary.topBandWeight) },
            { "medianEv", JsonFloat(summary.medianEv) }
        });
    }
    return result;
}

nlohmann::json SerializeNeutralSampleSummary(
    const RawAutoStartPointNeutralSampleSummary& summary) {
    return nlohmann::json {
        { "valid", summary.valid },
        { "sampleCount", summary.sampleCount },
        { "eligibleFraction", JsonFloat(summary.eligibleFraction) },
        { "medianChroma", JsonFloat(summary.medianChroma) },
        { "gainDistanceEv", JsonFloat(summary.gainDistanceEv) },
        { "estimatedGains", SerializeFloat3(summary.estimatedGains) },
        { "method", JsonStringOrNull(summary.method) },
        { "statusMessage", JsonStringOrNull(summary.statusMessage) }
    };
}

nlohmann::json SerializeRawSafetyStats(
    const RawAutoStartPointRawSafetyStats& stats) {
    return nlohmann::json {
        { "valid", stats.valid },
        { "activeValidFraction", JsonFloat(stats.activeValidFraction) },
        { "blackLevelSource", JsonStringOrNull(stats.blackLevelSource) },
        { "whiteLevelSource", JsonStringOrNull(stats.whiteLevelSource) },
        { "maskedBlackMean", JsonFloat(stats.maskedBlackMean) },
        { "maskedBlackStd", JsonFloat(stats.maskedBlackStd) },
        { "blackDriftWarning", stats.blackDriftWarning },
        { "lensShadingConfidence", JsonFloat(stats.lensShadingConfidence) },
        { "cornerFalloffEv", JsonFloat(stats.cornerFalloffEv) },
        { "perChannelClippedFraction", SerializeFloat3(stats.perChannelClippedFraction) },
        { "perChannelNearClippedFraction", SerializeFloat3(stats.perChannelNearClippedFraction) },
        { "perChannelP999", SerializeFloat3(stats.perChannelP999) },
        { "rawWhiteP999", JsonFloat(stats.rawWhiteP999) },
        { "linearResponseLimit", JsonFloat(stats.linearResponseLimit) },
        { "headroomEv", JsonFloat(stats.headroomEv) },
        { "wbScaledHeadroomEv", JsonFloat(stats.wbScaledHeadroomEv) },
        { "singleChannelClipFraction", JsonFloat(stats.singleChannelClipFraction) },
        { "multiChannelClipFraction", JsonFloat(stats.multiChannelClipFraction) },
        { "fullClipFraction", JsonFloat(stats.fullClipFraction) },
        { "highlightRecoverabilityScore", JsonFloat(stats.highlightRecoverabilityScore) },
        { "hotPixelFraction", JsonFloat(stats.hotPixelFraction) },
        { "noiseProfileAvailable", stats.noiseProfileAvailable },
        { "shadowSnrByEvBucket", SerializeEvBuckets(stats.shadowSnrByEvBucket) },
        { "baselineExposureEv", JsonFloat(stats.baselineExposureEv) },
        { "colorMatrixConfidence", JsonFloat(stats.colorMatrixConfidence) },
        { "asShotWbAvailable", stats.asShotWbAvailable },
        { "statusMessage", JsonStringOrNull(stats.statusMessage) }
    };
}

nlohmann::json SerializeSceneStageStats(
    const RawAutoStartPointSceneStageStats& stats) {
    return nlohmann::json {
        { "valid", stats.valid },
        { "stage", StageStableString(stats.stage) },
        { "status", StageStatusStableString(stats.status) },
        { "validPixelFraction", JsonFloat(stats.validPixelFraction) },
        { "lumaPercentiles", SerializePercentiles(stats.lumaPercentiles) },
        { "evPercentiles", SerializePercentiles(stats.evPercentiles) },
        { "logAverageY", JsonFloat(stats.logAverageY) },
        { "midSpreadEv", JsonFloat(stats.midSpreadEv) },
        { "wideSpreadEv", JsonFloat(stats.wideSpreadEv) },
        { "shadowMassFraction", JsonFloat(stats.shadowMassFraction) },
        { "highlightMassFraction", JsonFloat(stats.highlightMassFraction) },
        { "centerMedianEv", JsonFloat(stats.centerMedianEv) },
        { "topBandMedianEv", JsonFloat(stats.topBandMedianEv) },
        { "gradientWeightedPercentiles", SerializePercentiles(stats.gradientWeightedPercentiles) },
        { "toneBucketSpatialSummaries", SerializeToneBucketSpatialSummaries(stats.toneBucketSpatialSummaries) },
        { "neutralSampleSummary", SerializeNeutralSampleSummary(stats.neutralSampleSummary) },
        { "negativeChannelFraction", JsonFloat(stats.negativeChannelFraction) },
        { "wideGamutPressure", JsonFloat(stats.wideGamutPressure) },
        { "matrixGamutConfidence", JsonFloat(stats.matrixGamutConfidence) },
        { "localMaskHaloRisk", JsonFloat(stats.localMaskHaloRisk) },
        { "noiseAfterGainByEvBucket", SerializeEvBuckets(stats.noiseAfterGainByEvBucket) },
        { "statusMessage", JsonStringOrNull(stats.statusMessage) }
    };
}

nlohmann::json SerializeDisplayStageStats(
    const RawAutoStartPointDisplayStageStats& stats) {
    return nlohmann::json {
        { "valid", stats.valid },
        { "status", StageStatusStableString(stats.status) },
        { "transferFamily", JsonStringOrNull(stats.transferFamily) },
        { "displayClipHighFraction", JsonFloat(stats.displayClipHighFraction) },
        { "displayClipLowFraction", JsonFloat(stats.displayClipLowFraction) },
        { "displayLinearP05", JsonFloat(stats.displayLinearP05) },
        { "displayLinearP50", JsonFloat(stats.displayLinearP50) },
        { "displayLinearP95", JsonFloat(stats.displayLinearP95) },
        { "displayP05", JsonFloat(stats.displayP05) },
        { "displayP50", JsonFloat(stats.displayP50) },
        { "displayP95", JsonFloat(stats.displayP95) },
        { "displaySpread", JsonFloat(stats.displaySpread) },
        { "readabilityScore", JsonFloat(stats.readabilityScore) },
        { "metricsAreLinearDisplay", stats.metricsAreLinearDisplay },
        { "statusMessage", JsonStringOrNull(stats.statusMessage) }
    };
}

nlohmann::json SerializeStageDiagnostics(
    const RawAutoStartPointStageDiagnostics& diagnostics) {
    return nlohmann::json {
        { "stage", StageStableString(diagnostics.stage) },
        { "label", StageLabel(diagnostics.stage) },
        { "status", StageStatusStableString(diagnostics.status) },
        { "confidence01", JsonFloat(diagnostics.confidence01) },
        { "rawSafety", SerializeRawSafetyStats(diagnostics.rawSafety) },
        { "scene", SerializeSceneStageStats(diagnostics.scene) },
        { "display", SerializeDisplayStageStats(diagnostics.display) },
        { "warnings", SerializeStringVector(diagnostics.warnings) },
        { "statusMessage", JsonStringOrNull(diagnostics.statusMessage) }
    };
}

nlohmann::json SerializeStageDiagnosticsVector(
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointStageDiagnostics& item : diagnostics) {
        result.push_back(SerializeStageDiagnostics(item));
    }
    return result;
}

nlohmann::json SerializeScoreTerm(
    const RawAutoStartPointScoreTerm& term) {
    return nlohmann::json {
        { "id", JsonStringOrNull(term.id) },
        { "label", JsonStringOrNull(term.label) },
        { "value01", JsonFloat(term.value01) },
        { "weight", JsonFloat(term.weight) },
        { "weightedValue", JsonFloat(term.weightedValue) },
        { "rationale", JsonStringOrNull(term.rationale) }
    };
}

nlohmann::json SerializePenaltyTerm(
    const RawAutoStartPointPenaltyTerm& term) {
    return nlohmann::json {
        { "id", JsonStringOrNull(term.id) },
        { "label", JsonStringOrNull(term.label) },
        { "value", JsonFloat(term.value) },
        { "weight", JsonFloat(term.weight) },
        { "weightedValue", JsonFloat(term.weightedValue) },
        { "rationale", JsonStringOrNull(term.rationale) }
    };
}

nlohmann::json SerializeScoreTerms(
    const std::vector<RawAutoStartPointScoreTerm>& terms) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointScoreTerm& term : terms) {
        result.push_back(SerializeScoreTerm(term));
    }
    return result;
}

nlohmann::json SerializePenaltyTerms(
    const std::vector<RawAutoStartPointPenaltyTerm>& terms) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointPenaltyTerm& term : terms) {
        result.push_back(SerializePenaltyTerm(term));
    }
    return result;
}

nlohmann::json SerializeSubscores(
    const RawAutoStartPointCandidateSubscores& subscores) {
    return nlohmann::json {
        { "valid", subscores.valid },
        { "rawSafetyScore", JsonFloat(subscores.rawSafetyScore) },
        { "scenePlacementScore", JsonFloat(subscores.scenePlacementScore) },
        { "localConflictScore", JsonFloat(subscores.localConflictScore) },
        { "toneShapeScore", JsonFloat(subscores.toneShapeScore) },
        { "displayReadabilityScore", JsonFloat(subscores.displayReadabilityScore) },
        { "editConservatismScore", JsonFloat(subscores.editConservatismScore) },
        { "noisePenalty", JsonFloat(subscores.noisePenalty) },
        { "colorConstancyPenalty", JsonFloat(subscores.colorConstancyPenalty) },
        { "hiddenCompensationPenalty", JsonFloat(subscores.hiddenCompensationPenalty) }
    };
}

nlohmann::json SerializeCandidateScore(
    const RawAutoStartPointCandidateScore& score) {
    return nlohmann::json {
        { "valid", score.valid },
        { "subscores", SerializeSubscores(score.subscores) },
        { "terms", SerializeScoreTerms(score.terms) },
        { "penalties", SerializePenaltyTerms(score.penalties) },
        { "totalScore", JsonFloat(score.totalScore) },
        { "summary", JsonStringOrNull(score.summary) }
    };
}

nlohmann::json SerializeControls(
    const std::vector<RawAutoStartPointControl>& controls) {
    nlohmann::json result = nlohmann::json::array();
    for (RawAutoStartPointControl control : controls) {
        result.push_back({
            { "id", ControlStableString(control) },
            { "label", ControlLabel(control) }
        });
    }
    return result;
}

nlohmann::json SerializeVisibleRecipeEdits(
    const RawAutoStartPointVisibleRecipeEdits& edits) {
    return nlohmann::json {
        { "rawExposure", {
            { "valid", edits.rawExposureEvValid },
            { "recipeField", "preToneExposureEv" },
            { "preToneExposureEv", JsonFloat(edits.preToneExposureEv) }
        } },
        { "whiteBalance", {
            { "valid", edits.whiteBalanceValid },
            { "recipeField", "whiteBalance" },
            { "policy", JsonStringOrNull(edits.whiteBalancePolicy) },
            { "multipliers", SerializeFloat3(edits.whiteBalanceMultipliers) }
        } },
        { "localRange", {
            { "valid", edits.localRangeValid },
            { "recipeField", "localRange" },
            { "pointCount", edits.localRangePointCount },
            { "summary", JsonStringOrNull(edits.localRangeSummary) }
        } },
        { "finishTone", {
            { "valid", edits.finishToneValid },
            { "recipeField", "finishTone.layerJson" },
            { "neutral", edits.finishToneNeutral },
            { "summary", JsonStringOrNull(edits.finishToneSummary) }
        } },
        { "displayFit", {
            { "valid", edits.displayFitValid },
            { "recipeField", "viewTransform.layerJson" },
            { "summary", JsonStringOrNull(edits.viewTransformSummary) }
        } },
        { "touchedControls", SerializeControls(edits.touchedControls) }
    };
}

nlohmann::json SerializeCandidateRenderRequest(
    const RawAutoStartPointCandidateRenderRequest& request) {
    nlohmann::json value {
        { "valid", request.valid },
        { "id", JsonStringOrNull(request.id) },
        { "stage", StageStableString(request.stage) },
        { "stageLabel", StageLabel(request.stage) },
        { "replacesStatus", StageStatusStableString(request.replacesStatus) },
        { "hasRecipe", request.hasRecipe },
        { "expectedControls", SerializeControls(request.expectedControls) },
        { "reason", JsonStringOrNull(request.reason) }
    };
    value["recipe"] = request.hasRecipe
        ? Stack::RawRecipe::SerializeRecipe(request.recipe)
        : nlohmann::json();
    return value;
}

nlohmann::json SerializeCandidateRenderRequests(
    const std::vector<RawAutoStartPointCandidateRenderRequest>& requests) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointCandidateRenderRequest& request : requests) {
        result.push_back(SerializeCandidateRenderRequest(request));
    }
    return result;
}

nlohmann::json SerializeCandidate(
    const RawAutoStartPointCandidate& candidate) {
    nlohmann::json value {
        { "valid", candidate.valid },
        { "kind", CandidateKindStableString(candidate.kind) },
        { "label", CandidateKindLabel(candidate.kind) },
        { "id", JsonStringOrNull(candidate.id) },
        { "displayLabel", JsonStringOrNull(candidate.label) },
        { "summary", JsonStringOrNull(candidate.summary) },
        { "hasRecipe", candidate.hasRecipe },
        { "visibleEdits", SerializeVisibleRecipeEdits(candidate.visibleEdits) },
        { "stageDiagnostics", SerializeStageDiagnosticsVector(candidate.stageDiagnostics) },
        { "renderRequests", SerializeCandidateRenderRequests(candidate.renderRequests) },
        { "score", SerializeCandidateScore(candidate.score) },
        { "warnings", SerializeStringVector(candidate.warnings) }
    };
    value["recipe"] = candidate.hasRecipe
        ? Stack::RawRecipe::SerializeRecipe(candidate.recipe)
        : nlohmann::json();
    return value;
}

nlohmann::json SerializeCandidates(
    const std::vector<RawAutoStartPointCandidate>& candidates) {
    nlohmann::json result = nlohmann::json::array();
    for (const RawAutoStartPointCandidate& candidate : candidates) {
        result.push_back(SerializeCandidate(candidate));
    }
    return result;
}

float Saturate01(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

float SmoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = Saturate01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float Closeness01(float errorAbs, float goodAbs, float badAbs) {
    return 1.0f - SmoothStep(goodAbs, badAbs, std::abs(errorAbs));
}

float LowIsGood01(float value, float goodMax, float badMin) {
    return 1.0f - SmoothStep(goodMax, badMin, value);
}

float HighIsGood01(float value, float badMax, float goodMin) {
    return SmoothStep(badMax, goodMin, value);
}

std::string FormatFixed(float value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string FormatScore(float value) {
    return FormatFixed(value, 1);
}

std::string FormatPercent(float value01) {
    return FormatFixed(Saturate01(value01) * 100.0f, 0) + "%";
}

std::string FormatScoreComponentSummary(const RawAutoStartPointCandidateScore& score) {
    if (!score.valid || !score.subscores.valid) {
        return "Unavailable";
    }
    const RawAutoStartPointCandidateSubscores& subscores = score.subscores;
    return "Raw " + FormatPercent(subscores.rawSafetyScore) +
        ", Scene " + FormatPercent(subscores.scenePlacementScore) +
        ", Display " + FormatPercent(subscores.displayReadabilityScore);
}

std::string FormatScoreComponentDetail(const RawAutoStartPointCandidateScore& score) {
    if (!score.valid || !score.subscores.valid) {
        return "Score components are unavailable for this dry-run candidate.";
    }
    const RawAutoStartPointCandidateSubscores& subscores = score.subscores;
    std::ostringstream out;
    out << "Existing normalized score components: "
        << "Raw Safety " << FormatPercent(subscores.rawSafetyScore)
        << ", Scene Placement " << FormatPercent(subscores.scenePlacementScore)
        << ", Local Conflict " << FormatPercent(subscores.localConflictScore)
        << ", Tone Shape " << FormatPercent(subscores.toneShapeScore)
        << ", Display Readability " << FormatPercent(subscores.displayReadabilityScore)
        << ", Edit Conservatism " << FormatPercent(subscores.editConservatismScore)
        << ". Existing penalties: Noise " << FormatPercent(subscores.noisePenalty)
        << ", Color Constancy " << FormatPercent(subscores.colorConstancyPenalty)
        << ", Hidden Compensation " << FormatPercent(subscores.hiddenCompensationPenalty)
        << ". Evidence: ";
    bool wroteEvidence = false;
    for (const RawAutoStartPointScoreTerm& term : score.terms) {
        if (term.rationale.empty()) {
            continue;
        }
        if (wroteEvidence) {
            out << " ";
        }
        wroteEvidence = true;
        out << term.label << ": " << term.rationale;
    }
    if (!wroteEvidence) {
        out << "No score-term rationale was recorded.";
    }
    out << " This diagnostic line does not change candidate scoring or apply recipe values.";
    return out.str();
}

std::string CandidateDisplayLabel(const RawAutoStartPointCandidate& candidate) {
    return candidate.label.empty()
        ? CandidateKindLabel(candidate.kind)
        : candidate.label;
}

std::string FormatTouchedControls(
    const RawAutoStartPointVisibleRecipeEdits& visibleEdits) {
    std::string touchedControls;
    for (RawAutoStartPointControl control : visibleEdits.touchedControls) {
        if (!touchedControls.empty()) {
            touchedControls += ", ";
        }
        touchedControls += ControlLabel(control);
    }
    return touchedControls.empty() ? "None" : touchedControls;
}

std::string FormatControlList(
    const std::vector<RawAutoStartPointControl>& controls) {
    std::string result;
    for (RawAutoStartPointControl control : controls) {
        if (!result.empty()) {
            result += ", ";
        }
        result += ControlLabel(control);
    }
    return result.empty() ? "None" : result;
}

std::string FormatCandidateRenderRequestValue(
    const std::vector<RawAutoStartPointCandidateRenderRequest>& requests) {
    if (requests.empty()) {
        return "None";
    }
    std::string value;
    for (const RawAutoStartPointCandidateRenderRequest& request : requests) {
        if (!value.empty()) {
            value += ", ";
        }
        value += StageLabel(request.stage);
    }
    return value;
}

std::string FormatCandidateRenderRequestDetail(
    const std::vector<RawAutoStartPointCandidateRenderRequest>& requests) {
    if (requests.empty()) {
        return "No pending per-candidate renders are required by this dry-run candidate.";
    }
    std::string detail =
        "Requested per-candidate renders are diagnostic only; they do not render hidden output or apply recipe values. ";
    for (const RawAutoStartPointCandidateRenderRequest& request : requests) {
        detail += StageLabel(request.stage);
        detail += " replaces ";
        detail += StageStatusStableString(request.replacesStatus);
        detail += " evidence";
        if (!request.expectedControls.empty()) {
            detail += " after ";
            detail += FormatControlList(request.expectedControls);
        }
        if (!request.reason.empty()) {
            detail += ": ";
            detail += request.reason;
        }
        detail += " ";
    }
    return detail;
}

std::string FormatCandidateScoreOrderValue(
    const std::vector<RawAutoStartPointCandidate>& candidates) {
    std::vector<const RawAutoStartPointCandidate*> scoredCandidates;
    std::vector<std::string> unscoredLabels;
    for (const RawAutoStartPointCandidate& candidate : candidates) {
        if (candidate.score.valid) {
            scoredCandidates.push_back(&candidate);
        } else {
            unscoredLabels.push_back(CandidateDisplayLabel(candidate));
        }
    }
    std::stable_sort(
        scoredCandidates.begin(),
        scoredCandidates.end(),
        [](const RawAutoStartPointCandidate* a, const RawAutoStartPointCandidate* b) {
            return a->score.totalScore > b->score.totalScore;
        });

    std::ostringstream out;
    bool firstScoredCandidate = true;
    for (const RawAutoStartPointCandidate* candidate : scoredCandidates) {
        if (!firstScoredCandidate) {
            out << " > ";
        }
        firstScoredCandidate = false;
        out << CandidateDisplayLabel(*candidate)
            << " " << FormatScore(candidate->score.totalScore);
    }
    if (!unscoredLabels.empty()) {
        if (!firstScoredCandidate) {
            out << "; ";
        }
        out << "Unscored: ";
        for (std::size_t i = 0; i < unscoredLabels.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << unscoredLabels[i];
        }
    }
    const std::string value = out.str();
    return value.empty() ? "Unavailable" : value;
}

std::string FormatSignedEv(float value) {
    std::ostringstream out;
    out << (value >= 0.0f ? "+" : "") << std::fixed << std::setprecision(2) << value << " EV";
    return out.str();
}

std::string JoinCommaSummaryParts(const std::vector<std::string>& parts) {
    std::string summary;
    for (const std::string& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += part;
    }
    return summary;
}

void AppendUniqueSummary(std::vector<std::string>& summaries, std::string summary) {
    if (summary.empty()) {
        return;
    }
    if (std::find(summaries.begin(), summaries.end(), summary) != summaries.end()) {
        return;
    }
    summaries.push_back(std::move(summary));
}

std::string FormatViewFitSummary(const Stack::RawAutoBase::ViewTransformFit& fit) {
    if (!fit.valid) {
        return "Display Fit unavailable until current-frame stats are available.";
    }
    std::ostringstream out;
    out << "middle grey " << std::fixed << std::setprecision(3) << fit.middleGrey
        << ", black " << std::setprecision(2) << fit.blackEv << " EV"
        << ", white +" << fit.whiteEv << " EV"
        << ", shoulder " << fit.shoulder
        << ", toe " << fit.toe;
    return out.str();
}

std::string FormatViewTransformLayerSummary(
    const nlohmann::json& viewTransform,
    const char* prefix) {
    Stack::RawAutoBase::ViewTransformFit fit;
    fit.valid = viewTransform.is_object();
    fit.middleGrey = JsonNumberOr(viewTransform, "middleGrey", 0.18f);
    fit.blackEv = JsonNumberOr(viewTransform, "blackEv", -8.0f);
    fit.whiteEv = JsonNumberOr(viewTransform, "whiteEv", 4.0f);
    fit.shoulder = JsonNumberOr(viewTransform, "shoulder", 0.45f);
    fit.toe = JsonNumberOr(viewTransform, "toe", 0.18f);

    const std::string values = FormatViewFitSummary(fit);
    if (prefix == nullptr || prefix[0] == '\0') {
        return values;
    }
    return std::string(prefix) + ": " + values;
}

std::vector<RawAutoStartPointStageDiagnostics> ExtractStageDiagnostics(
    const RawAutoStartPointDiagnostics& diagnostics) {
    for (const RawAutoStartPointCandidate& candidate : diagnostics.candidates) {
        if (!candidate.stageDiagnostics.empty()) {
            return candidate.stageDiagnostics;
        }
    }
    return {};
}

const RawAutoStartPointStageDiagnostics* FindStageDiagnostics(
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics,
    RawAutoStartPointStage stage) {
    for (const RawAutoStartPointStageDiagnostics& item : diagnostics) {
        if (item.stage == stage) {
            return &item;
        }
    }
    return nullptr;
}

const RawAutoStartPointSceneStageStats* FindSceneStats(
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics,
    RawAutoStartPointStage stage) {
    const RawAutoStartPointStageDiagnostics* item = FindStageDiagnostics(diagnostics, stage);
    if (item == nullptr || !item->scene.valid) {
        return nullptr;
    }
    return &item->scene;
}

const RawAutoStartPointDisplayStageStats* FindDisplayStats(
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics) {
    const RawAutoStartPointStageDiagnostics* item =
        FindStageDiagnostics(diagnostics, RawAutoStartPointStage::DisplayCandidate);
    if (item == nullptr || !item->display.valid) {
        return nullptr;
    }
    return &item->display;
}

float DarkRevealScoreFromEv(float p05Ev, float p50Ev) {
    float score = 0.0f;
    if (std::isfinite(p50Ev)) {
        score = std::max(score, SmoothStep(4.0f, 6.5f, -p50Ev));
    }
    if (std::isfinite(p05Ev)) {
        score = std::max(score, SmoothStep(6.0f, 9.0f, -p05Ev));
    }
    return Saturate01(score);
}

float DarkRevealScoreFromSceneStats(const RawAutoStartPointSceneStageStats& stats) {
    if (stats.evPercentiles.valid) {
        return DarkRevealScoreFromEv(stats.evPercentiles.p05, stats.evPercentiles.p50);
    }
    return DarkRevealScoreFromEv(0.0f, stats.centerMedianEv);
}

float ComputeDarkRevealScore(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics) {
    if (const RawAutoStartPointSceneStageStats* scene =
            FindSceneStats(diagnostics, RawAutoStartPointStage::FinishToneCandidate)) {
        return DarkRevealScoreFromSceneStats(*scene);
    }

    float score = 0.0f;
    if (analysis.valid && analysis.currentFrameStats.valid) {
        score = std::max(score, DarkRevealScoreFromEv(
            analysis.currentFrameStats.p05Ev,
            analysis.currentFrameStats.p50Ev));
    }
    return Saturate01(score);
}

bool StageComplete(
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics,
    RawAutoStartPointStage stage) {
    const RawAutoStartPointStageDiagnostics* item = FindStageDiagnostics(diagnostics, stage);
    return item != nullptr && item->status == RawAutoStartPointStageStatus::Complete;
}

bool StageProjected(
    const std::vector<RawAutoStartPointStageDiagnostics>& diagnostics,
    RawAutoStartPointStage stage) {
    const RawAutoStartPointStageDiagnostics* item = FindStageDiagnostics(diagnostics, stage);
    return item != nullptr && item->status == RawAutoStartPointStageStatus::Projected;
}

bool AnyCandidateStageComplete(
    const std::vector<RawAutoStartPointCandidate>& candidates,
    RawAutoStartPointStage stage) {
    return std::any_of(
        candidates.begin(),
        candidates.end(),
        [stage](const RawAutoStartPointCandidate& candidate) {
            return StageComplete(candidate.stageDiagnostics, stage);
        });
}

bool IsRenderedCandidateStage(const RawAutoStartPointStageDiagnostics& stage) {
    return stage.statusMessage.find("Rendered ") != std::string::npos ||
        stage.scene.statusMessage.find("Rendered ") != std::string::npos ||
        stage.display.statusMessage.find("Rendered ") != std::string::npos;
}

bool CandidateHasRenderedCompleteStage(
    const RawAutoStartPointCandidate& candidate,
    RawAutoStartPointStage stage) {
    const RawAutoStartPointStageDiagnostics* diagnostics =
        FindStageDiagnostics(candidate.stageDiagnostics, stage);
    return diagnostics != nullptr &&
        diagnostics->status == RawAutoStartPointStageStatus::Complete &&
        IsRenderedCandidateStage(*diagnostics);
}

bool RecipeFloatMatches(float a, float b, float tolerance = 0.001f) {
    return std::isfinite(a) && std::isfinite(b) && std::abs(a - b) < tolerance;
}

bool WhiteBalanceStateMatches(
    const Stack::RawRecipe::RawWhiteBalanceRecipe& a,
    const Stack::RawRecipe::RawWhiteBalanceRecipe& b) {
    if (a.mode != b.mode || a.hasMultipliers != b.hasMultipliers) {
        return false;
    }
    if (a.hasMultipliers) {
        for (std::size_t i = 0; i < a.multipliers.size(); ++i) {
            if (!RecipeFloatMatches(a.multipliers[i], b.multipliers[i])) {
                return false;
            }
        }
    }
    return true;
}

bool VisibleUpstreamRecipeMatches(
    const Stack::RawRecipe::RawDevelopmentRecipe& a,
    const Stack::RawRecipe::RawDevelopmentRecipe& b) {
    const bool sameSource =
        a.source.relativePathKey == b.source.relativePathKey &&
        a.source.sourcePath == b.source.sourcePath &&
        a.source.fingerprint == b.source.fingerprint;
    return sameSource &&
        RecipeFloatMatches(a.preToneExposureEv, b.preToneExposureEv) &&
        WhiteBalanceStateMatches(a.whiteBalance, b.whiteBalance) &&
        Stack::RawRecipe::LocalRangeStateHash(a) == Stack::RawRecipe::LocalRangeStateHash(b) &&
        a.finishTone.layerJson == b.finishTone.layerJson;
}

bool RenderRequestRecipeStillMatches(
    const RawAutoStartPointCandidateRenderRequest& current,
    const RawAutoStartPointCandidateRenderRequest& rendered) {
    if (!current.hasRecipe || !rendered.hasRecipe) {
        return false;
    }
    return current.replacesStatus == rendered.replacesStatus &&
        current.expectedControls == rendered.expectedControls &&
        VisibleUpstreamRecipeMatches(current.recipe, rendered.recipe);
}

std::string RenderRequestMismatchReason(
    const RawAutoStartPointCandidateRenderRequest& current,
    const RawAutoStartPointCandidateRenderRequest& rendered) {
    std::vector<std::string> parts;
    if (!current.hasRecipe || !rendered.hasRecipe) {
        parts.push_back("visible recipe missing");
    } else if (!VisibleUpstreamRecipeMatches(current.recipe, rendered.recipe)) {
        parts.push_back("visible upstream recipe changed");
    }
    if (current.replacesStatus != rendered.replacesStatus) {
        parts.push_back(
            std::string("replaced-stage status changed from ") +
            StageStatusStableString(current.replacesStatus) +
            " to " + StageStatusStableString(rendered.replacesStatus));
    }
    if (current.expectedControls != rendered.expectedControls) {
        parts.push_back(
            "expected visible controls changed from " +
            FormatControlList(current.expectedControls) +
            " to " + FormatControlList(rendered.expectedControls));
    }
    const std::string reason = JoinCommaSummaryParts(parts);
    return reason.empty() ? "request identity changed" : reason;
}

bool ComputeRawExposureDeltaFromSceneStats(
    const RawAutoStartPointSceneStageStats& stats,
    float& outDeltaEv) {
    if (!stats.valid || !stats.evPercentiles.valid) {
        return false;
    }
    const float whiteAnchorEv = std::isfinite(stats.evPercentiles.p999)
        ? stats.evPercentiles.p999
        : stats.evPercentiles.p99;
    if (!std::isfinite(stats.evPercentiles.p50) || !std::isfinite(whiteAnchorEv)) {
        return false;
    }
    constexpr float targetMedianRelativeToWhiteEv = -2.7f;
    outDeltaEv = std::clamp(
        whiteAnchorEv + targetMedianRelativeToWhiteEv - stats.evPercentiles.p50,
        -0.5f,
        1.0f);
    return true;
}

const RawAutoStartPointRawSafetyStats* FindCompleteRawSafetyStats(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages) {
    const RawAutoStartPointStageDiagnostics* rawTechnical =
        FindStageDiagnostics(stages, RawAutoStartPointStage::RawTechnical);
    if (rawTechnical == nullptr ||
        rawTechnical->status != RawAutoStartPointStageStatus::Complete ||
        !rawTechnical->rawSafety.valid) {
        return nullptr;
    }
    return &rawTechnical->rawSafety;
}

void ShiftLumaPercentiles(RawAutoStartPointPercentiles& stats, float scale) {
    if (!stats.valid || !std::isfinite(scale)) {
        return;
    }
    stats.p01 *= scale;
    stats.p05 *= scale;
    stats.p10 *= scale;
    stats.p25 *= scale;
    stats.p50 *= scale;
    stats.p75 *= scale;
    stats.p90 *= scale;
    stats.p95 *= scale;
    stats.p99 *= scale;
    stats.p999 *= scale;
}

void ShiftEvPercentiles(RawAutoStartPointPercentiles& stats, float deltaEv) {
    if (!stats.valid || !std::isfinite(deltaEv)) {
        return;
    }
    stats.p01 += deltaEv;
    stats.p05 += deltaEv;
    stats.p10 += deltaEv;
    stats.p25 += deltaEv;
    stats.p50 += deltaEv;
    stats.p75 += deltaEv;
    stats.p90 += deltaEv;
    stats.p95 += deltaEv;
    stats.p99 += deltaEv;
    stats.p999 += deltaEv;
}

RawAutoStartPointSceneStageStats ProjectSceneExposureToStage(
    RawAutoStartPointSceneStageStats scene,
    RawAutoStartPointStage targetStage,
    float deltaEv,
    std::string statusMessage) {
    scene.stage = targetStage;
    scene.status = RawAutoStartPointStageStatus::Projected;
    const float scale = std::isfinite(deltaEv) ? std::exp2(deltaEv) : 1.0f;
    ShiftLumaPercentiles(scene.lumaPercentiles, scale);
    ShiftEvPercentiles(scene.evPercentiles, deltaEv);
    ShiftLumaPercentiles(scene.gradientWeightedPercentiles, scale);
    scene.logAverageY *= scale;
    if (std::isfinite(scene.centerMedianEv)) {
        scene.centerMedianEv += deltaEv;
    }
    if (std::isfinite(scene.topBandMedianEv)) {
        scene.topBandMedianEv += deltaEv;
    }
    for (RawAutoStartPointToneBucketSpatialSummary& summary :
         scene.toneBucketSpatialSummaries) {
        if (std::isfinite(summary.medianEv)) {
            summary.medianEv += deltaEv;
        }
    }
    scene.statusMessage = std::move(statusMessage);
    return scene;
}

RawAutoStartPointSceneStageStats ProjectSceneExposure(
    RawAutoStartPointSceneStageStats scene,
    float deltaEv) {
    scene = ProjectSceneExposureToStage(
        std::move(scene),
        RawAutoStartPointStage::RawPlacement,
        deltaEv,
        "Projected Raw Placement stage from existing staged evidence and the Base RAW Exposure candidate; not a rendered candidate readback.");
    return scene;
}

bool BuildProjectedBaseRawPlacementStage(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    float currentExposureEv,
    float targetExposureEv,
    RawAutoStartPointStageDiagnostics& outStage) {
    if (!std::isfinite(currentExposureEv) || !std::isfinite(targetExposureEv) ||
        std::abs(targetExposureEv - currentExposureEv) < 0.001f) {
        return false;
    }

    const RawAutoStartPointStageDiagnostics* sourceStage =
        FindStageDiagnostics(stages, RawAutoStartPointStage::RawPlacement);
    float projectionDeltaEv = targetExposureEv - currentExposureEv;
    std::string sourceLabel = "current Raw Placement";
    if (sourceStage == nullptr ||
        sourceStage->status != RawAutoStartPointStageStatus::Complete ||
        !sourceStage->scene.valid) {
        sourceStage = FindStageDiagnostics(stages, RawAutoStartPointStage::NeutralScene);
        projectionDeltaEv = targetExposureEv;
        sourceLabel = "Neutral Scene";
    }
    if (sourceStage == nullptr ||
        sourceStage->status != RawAutoStartPointStageStatus::Complete ||
        !sourceStage->scene.valid) {
        return false;
    }

    outStage = *sourceStage;
    outStage.stage = RawAutoStartPointStage::RawPlacement;
    outStage.status = RawAutoStartPointStageStatus::Projected;
    outStage.confidence01 = std::min(outStage.confidence01, 0.75f);
    outStage.scene = ProjectSceneExposure(sourceStage->scene, projectionDeltaEv);
    outStage.scene.stage = RawAutoStartPointStage::RawPlacement;
    outStage.scene.status = RawAutoStartPointStageStatus::Projected;
    outStage.warnings.push_back(
        "Projected Raw Placement is candidate-specific but not rendered; per-candidate render validation is still required.");
    std::ostringstream status;
    status << "Projected Base Raw Placement from " << sourceLabel
           << " by " << FormatSignedEv(projectionDeltaEv)
           << " for the proposed visible RAW Exposure value.";
    outStage.statusMessage = status.str();
    outStage.scene.statusMessage = outStage.statusMessage;
    return true;
}

bool BuildProjectedBaseFinishToneCandidateStage(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    float currentExposureEv,
    float targetExposureEv,
    RawAutoStartPointStageDiagnostics& outStage) {
    if (!std::isfinite(currentExposureEv) || !std::isfinite(targetExposureEv) ||
        std::abs(targetExposureEv - currentExposureEv) < 0.001f) {
        return false;
    }

    const RawAutoStartPointStageDiagnostics* sourceStage =
        FindStageDiagnostics(stages, RawAutoStartPointStage::FinishToneCandidate);
    if (sourceStage == nullptr ||
        sourceStage->status != RawAutoStartPointStageStatus::Complete ||
        !sourceStage->scene.valid) {
        return false;
    }

    const float projectionDeltaEv = targetExposureEv - currentExposureEv;
    outStage = *sourceStage;
    outStage.status = RawAutoStartPointStageStatus::Projected;
    outStage.confidence01 = std::min(outStage.confidence01, 0.70f);
    std::ostringstream status;
    status << "Projected Base Finish Tone Candidate from current pre-display tone by "
           << FormatSignedEv(projectionDeltaEv)
           << " for the proposed visible RAW Exposure value.";
    outStage.statusMessage = status.str();
    outStage.scene = ProjectSceneExposureToStage(
        sourceStage->scene,
        RawAutoStartPointStage::FinishToneCandidate,
        projectionDeltaEv,
        outStage.statusMessage);
    outStage.warnings.push_back(
        "Projected Finish Tone Candidate is candidate-specific but not rendered; per-candidate render validation is still required.");
    return true;
}

RawAutoStartPointStageDiagnostics BuildPendingBaseDisplayCandidateStage(
    float currentExposureEv,
    float targetExposureEv) {
    RawAutoStartPointStageDiagnostics stage;
    stage.stage = RawAutoStartPointStage::DisplayCandidate;
    stage.status = RawAutoStartPointStageStatus::Pending;
    stage.confidence01 = 0.0f;
    stage.display.status = RawAutoStartPointStageStatus::Pending;
    const float deltaEv = std::isfinite(currentExposureEv) && std::isfinite(targetExposureEv)
        ? targetExposureEv - currentExposureEv
        : 0.0f;
    std::ostringstream status;
    status << "Base Display Candidate pending: View Transform / Display Fit must be rendered after the proposed upstream Starting Point values";
    if (std::abs(deltaEv) >= 0.001f) {
        status << " including RAW Exposure " << FormatSignedEv(deltaEv);
    }
    status << ".";
    stage.statusMessage = status.str();
    stage.display.statusMessage = stage.statusMessage;
    stage.warnings.push_back(
        "Base display readability is pending until the post-edit Display Fit render completes.");
    return stage;
}

RawAutoStartPointStageDiagnostics BuildPendingBaseLocalCandidateStage(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    RawAutoStartPointStageDiagnostics stage;
    stage.stage = RawAutoStartPointStage::LocalCandidate;
    stage.status = RawAutoStartPointStageStatus::Pending;
    stage.confidence01 = 0.0f;
    stage.scene.stage = RawAutoStartPointStage::LocalCandidate;
    stage.scene.status = RawAutoStartPointStageStatus::Pending;
    std::ostringstream status;
    status << "Base Local Candidate pending: render the proposed one-click Local Range point "
           << FormatSignedEv(suggestion.deltaEv)
           << " at scene " << FormatSignedEv(suggestion.targetEv)
           << " after the proposed upstream Starting Point values.";
    stage.statusMessage = status.str();
    stage.scene.statusMessage = stage.statusMessage;
    stage.warnings.push_back(
        "Base Local Range graph authoring waits for rendered post-upstream Local Candidate evidence.");
    return stage;
}

RawAutoStartPointCandidateRenderRequest BuildCandidateRenderRequest(
    RawAutoStartPointCandidateKind candidateKind,
    RawAutoStartPointStage stage,
    RawAutoStartPointStageStatus replacesStatus,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::vector<RawAutoStartPointControl> expectedControls,
    std::string reason) {
    RawAutoStartPointCandidateRenderRequest request;
    request.valid = true;
    request.id = std::string(CandidateKindStableString(candidateKind)) +
        "-" + StageStableString(stage) + "-render";
    request.stage = stage;
    request.replacesStatus = replacesStatus;
    request.hasRecipe = true;
    request.recipe = recipe;
    request.expectedControls = std::move(expectedControls);
    request.reason = std::move(reason);
    return request;
}

void ReplaceStageDiagnostics(
    std::vector<RawAutoStartPointStageDiagnostics>& stages,
    RawAutoStartPointStageDiagnostics stage) {
    const auto it = std::find_if(
        stages.begin(),
        stages.end(),
        [&](const RawAutoStartPointStageDiagnostics& item) {
            return item.stage == stage.stage;
        });
    if (it != stages.end()) {
        *it = std::move(stage);
    } else {
        stages.push_back(std::move(stage));
    }
}

Stack::RawAutoBase::RawExposureRecommendation BuildStartingPointRawExposureRecommendation(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAutoBase::RawExposureRecommendation& baseRecommendation) {
    Stack::RawAutoBase::RawExposureRecommendation recommendation = baseRecommendation;
    if (!recommendation.valid || !std::isfinite(recommendation.suggestedEv)) {
        return recommendation;
    }
    const bool initiallyBlockedByHighlightRisk = recommendation.blockedByHighlightRisk;

    const float requestedDeltaEv = recommendation.suggestedEv - currentRecipe.preToneExposureEv;
    if (!std::isfinite(requestedDeltaEv) ||
        std::abs(requestedDeltaEv) < kStartingPointRawExposureMinMoveEv) {
        return recommendation;
    }

    const auto applyCappedVisibleMove = [&](
        float deltaEv,
        float capEv,
        std::string rationale,
        std::string cappedRationale) {
        const float safeCapEv = std::max(kStartingPointRawExposureMinMoveEv, capEv);
        const float cappedDeltaEv = std::clamp(
            deltaEv,
            -safeCapEv,
            safeCapEv);
        if (!std::isfinite(cappedDeltaEv) ||
            std::abs(cappedDeltaEv) < kStartingPointRawExposureMinMoveEv) {
            return false;
        }
        const bool capped = std::abs(cappedDeltaEv - deltaEv) > 0.001f;
        recommendation.deltaEv = cappedDeltaEv;
        recommendation.suggestedEv = currentRecipe.preToneExposureEv + cappedDeltaEv;
        recommendation.autoApplyAllowed = true;
        recommendation.blockedByHighlightRisk = false;
        recommendation.rationale =
            capped && !cappedRationale.empty()
                ? cappedRationale
                : rationale;
        return true;
    };

    const RawAutoStartPointSceneStageStats* neutralScene =
        FindSceneStats(stages, RawAutoStartPointStage::NeutralScene);
    const RawAutoStartPointRawSafetyStats* rawSafety = FindCompleteRawSafetyStats(stages);
    if (neutralScene == nullptr || rawSafety == nullptr) {
        if (initiallyBlockedByHighlightRisk) {
            return recommendation;
        }
        if (!recommendation.autoApplyAllowed) {
            if (recommendation.confidence >= kStartingPointRawExposureConfidenceGate) {
                applyCappedVisibleMove(
                    requestedDeltaEv,
                    kStartingPointRawExposureOneClickCapEv,
                    "Starting Point applied RAW Exposure from conservative exposure evidence before staged Neutral Scene / Raw Technical evidence was available.",
                    "Starting Point applied a capped RAW Exposure move from conservative exposure evidence before staged Neutral Scene / Raw Technical evidence was available; larger RAW Exposure move remains manual.");
            } else if (recommendation.confidence >= kStartingPointRawExposureCautiousConfidenceGate) {
                applyCappedVisibleMove(
                    requestedDeltaEv,
                    kStartingPointRawExposureCautiousCapEv,
                    "Starting Point applied a cautious RAW Exposure nudge from medium-confidence exposure evidence before staged Neutral Scene / Raw Technical evidence was available.",
                    "Starting Point applied a smaller cautious RAW Exposure nudge from medium-confidence exposure evidence before staged Neutral Scene / Raw Technical evidence was available; larger RAW Exposure move remains manual.");
            } else if (requestedDeltaEv > 0.0f &&
                recommendation.confidence >= kStartingPointRawExposureTinyLiftConfidenceGate) {
                applyCappedVisibleMove(
                    requestedDeltaEv,
                    kStartingPointRawExposureTinyLiftCapEv,
                    "Starting Point applied a tiny positive RAW Exposure nudge from lower-confidence exposure evidence before staged Neutral Scene / Raw Technical evidence was available.",
                    "Starting Point applied a tiny positive RAW Exposure nudge from lower-confidence exposure evidence before staged Neutral Scene / Raw Technical evidence was available; larger RAW Exposure lift remains manual.");
            } else if (requestedDeltaEv < 0.0f &&
                recommendation.confidence >= kStartingPointRawExposureDownwardConfidenceGate) {
                applyCappedVisibleMove(
                    requestedDeltaEv,
                    kStartingPointRawExposureDownwardCapEv,
                    "Starting Point applied a small downward RAW Exposure nudge from lower-confidence exposure evidence before staged Neutral Scene / Raw Technical evidence was available.",
                    "Starting Point applied a small downward RAW Exposure nudge from lower-confidence exposure evidence before staged Neutral Scene / Raw Technical evidence was available; larger RAW Exposure lowering remains manual.");
            }
        }
        return recommendation;
    }

    float neutralDeltaEv = 0.0f;
    if (!ComputeRawExposureDeltaFromSceneStats(*neutralScene, neutralDeltaEv)) {
        return recommendation;
    }

    const bool neutralSuggestsNoMove = std::abs(neutralDeltaEv) < kStartingPointRawExposureMinMoveEv;
    const bool neutralDisagrees =
        neutralSuggestsNoMove ||
        (requestedDeltaEv * neutralDeltaEv < 0.0f &&
         std::abs(neutralDeltaEv) >= kStartingPointRawExposureMinMoveEv);
    if (neutralDisagrees) {
        recommendation.autoApplyAllowed = false;
        recommendation.rationale =
            "RAW Exposure withheld because Neutral Scene placement disagrees with the current-frame suggestion.";
        return recommendation;
    }

    float stagedDeltaEv = std::clamp(
        requestedDeltaEv,
        -kStartingPointRawExposureOneClickCapEv,
        kStartingPointRawExposureOneClickCapEv);
    const bool cappedToOneClick = std::abs(stagedDeltaEv - requestedDeltaEv) > 0.001f;
    if (stagedDeltaEv > 0.0f) {
        const float positiveHeadroomEv = std::isfinite(rawSafety->wbScaledHeadroomEv)
            ? std::max(0.0f, rawSafety->wbScaledHeadroomEv)
            : 0.0f;
        stagedDeltaEv = std::min(stagedDeltaEv, positiveHeadroomEv);
        if (stagedDeltaEv < kStartingPointRawExposureMinMoveEv) {
            recommendation.autoApplyAllowed = false;
            recommendation.rationale =
                "RAW Exposure lift withheld because Raw Technical WB-scaled headroom is below the minimum visible one-click move.";
            return recommendation;
        }
    }

    if (recommendation.autoApplyAllowed) {
        if (std::abs(stagedDeltaEv - requestedDeltaEv) > 0.001f) {
            recommendation.deltaEv = stagedDeltaEv;
            recommendation.suggestedEv = currentRecipe.preToneExposureEv + stagedDeltaEv;
            recommendation.rationale =
                "RAW Exposure suggestion was reduced by Raw Technical headroom before applying the visible one-click value.";
        }
        return recommendation;
    }

    const bool highlightRiskLift =
        initiallyBlockedByHighlightRisk && stagedDeltaEv > 0.0f;
    const std::string highlightRiskRationale =
        "Staged Starting Point applied RAW Exposure despite display highlight risk because Neutral Scene placement agreed and Raw Technical headroom was available.";
    const std::string highlightRiskCappedRationale =
        "Staged Starting Point capped RAW Exposure under highlight risk using Neutral Scene placement and Raw Technical headroom; larger lift remains manual.";

    if (recommendation.confidence >= kStartingPointRawExposureConfidenceGate) {
        applyCappedVisibleMove(
            stagedDeltaEv,
            kStartingPointRawExposureOneClickCapEv,
            highlightRiskLift
                ? highlightRiskRationale
                : (cappedToOneClick
                ? "Staged Starting Point capped a larger RAW Exposure suggestion to the visible one-click limit using Neutral Scene placement and Raw Technical headroom; larger lift remains manual."
                : "Staged Starting Point applied RAW Exposure using Neutral Scene placement and Raw Technical headroom."),
            highlightRiskLift
                ? highlightRiskCappedRationale
                : "Staged Starting Point capped a larger RAW Exposure suggestion to the visible one-click limit using Neutral Scene placement and Raw Technical headroom; larger lift remains manual.");
    } else if (recommendation.confidence >= kStartingPointRawExposureCautiousConfidenceGate) {
        applyCappedVisibleMove(
            stagedDeltaEv,
            kStartingPointRawExposureCautiousCapEv,
            highlightRiskLift
                ? "Staged Starting Point applied a cautious RAW Exposure nudge despite highlight risk because Neutral Scene placement agreed and Raw Technical headroom was available."
                : "Staged Starting Point applied a cautious RAW Exposure nudge from medium-confidence Neutral Scene placement and Raw Technical headroom.",
            highlightRiskLift
                ? "Staged Starting Point applied a smaller cautious RAW Exposure nudge under highlight risk; larger lift remains manual."
                : "Staged Starting Point applied a smaller cautious RAW Exposure nudge from medium-confidence Neutral Scene placement and Raw Technical headroom; larger lift remains manual.");
    } else if (stagedDeltaEv > 0.0f &&
        recommendation.confidence >= kStartingPointRawExposureTinyLiftConfidenceGate) {
        applyCappedVisibleMove(
            stagedDeltaEv,
            kStartingPointRawExposureTinyLiftCapEv,
            highlightRiskLift
                ? "Staged Starting Point applied a tiny positive RAW Exposure nudge despite highlight risk because Neutral Scene placement agreed and Raw Technical headroom was available."
                : "Staged Starting Point applied a tiny positive RAW Exposure nudge from lower-confidence Neutral Scene placement and Raw Technical headroom.",
            highlightRiskLift
                ? "Staged Starting Point applied a tiny positive RAW Exposure nudge under highlight risk; larger RAW Exposure lift remains manual."
                : "Staged Starting Point applied a tiny positive RAW Exposure nudge from lower-confidence Neutral Scene placement and Raw Technical headroom; larger RAW Exposure lift remains manual.");
    } else if (stagedDeltaEv < 0.0f &&
        recommendation.confidence >= kStartingPointRawExposureDownwardConfidenceGate) {
        applyCappedVisibleMove(
            stagedDeltaEv,
            kStartingPointRawExposureDownwardCapEv,
            "Staged Starting Point applied a small downward RAW Exposure nudge from lower-confidence Neutral Scene placement.",
            "Staged Starting Point applied a small downward RAW Exposure nudge from lower-confidence Neutral Scene placement; larger RAW Exposure lowering remains manual.");
    }
    return recommendation;
}

bool BuildRenderedPostWhiteBalanceRawExposureRecommendation(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const RawAutoStartPointStageDiagnostics* renderedRawPlacementStage,
    const Stack::RawAutoBase::RawExposureRecommendation& preWhiteBalanceRecommendation,
    Stack::RawAutoBase::RawExposureRecommendation& outRecommendation,
    std::string& outWithheldSummary,
    std::string& outWarning) {
    outRecommendation = preWhiteBalanceRecommendation;
    if (renderedRawPlacementStage == nullptr ||
        renderedRawPlacementStage->status != RawAutoStartPointStageStatus::Complete ||
        !renderedRawPlacementStage->scene.valid) {
        outWithheldSummary =
            "RAW Exposure pending: White Balance changed before a post-WB Raw Placement render";
        outWarning =
            "RAW Exposure was withheld because the available exposure evidence was measured before the applied Suggested WB multipliers.";
        return false;
    }

    float renderedDeltaEv = 0.0f;
    if (!ComputeRawExposureDeltaFromSceneStats(renderedRawPlacementStage->scene, renderedDeltaEv)) {
        outWithheldSummary =
            "RAW Exposure pending: post-WB Raw Placement evidence lacks exposure placement stats";
        outWarning =
            "Rendered post-WB Raw Placement evidence existed, but it did not include usable scene EV percentiles.";
        return false;
    }

    const float previousDeltaEv =
        preWhiteBalanceRecommendation.suggestedEv - currentRecipe.preToneExposureEv;
    if (std::isfinite(previousDeltaEv) &&
        std::abs(previousDeltaEv) >= 0.15f &&
        previousDeltaEv * renderedDeltaEv < 0.0f) {
        outWithheldSummary = "RAW Exposure withheld: post-WB Raw Placement disagreement";
        outWarning =
            "Rendered post-WB Raw Placement evidence disagreed with the pre-WB RAW Exposure direction.";
        return false;
    }

    float stagedDeltaEv = renderedDeltaEv;
    if (std::abs(stagedDeltaEv) < kStartingPointRawExposureMinMoveEv) {
        outWithheldSummary = "RAW Exposure already near post-WB target";
        return false;
    }

    if (stagedDeltaEv > 0.0f) {
        const RawAutoStartPointRawSafetyStats* rawSafety = FindCompleteRawSafetyStats(stages);
        if (rawSafety == nullptr) {
            outWithheldSummary =
                "RAW Exposure pending: post-WB Raw Placement needs Raw Technical headroom";
            outWarning =
                "Positive RAW Exposure after Suggested WB was withheld because Raw Technical headroom evidence is unavailable.";
            return false;
        }
        const float positiveHeadroomEv = std::isfinite(rawSafety->wbScaledHeadroomEv)
            ? std::max(0.0f, rawSafety->wbScaledHeadroomEv)
            : 0.0f;
        stagedDeltaEv = std::min(stagedDeltaEv, positiveHeadroomEv);
        if (stagedDeltaEv < kStartingPointRawExposureMinMoveEv) {
            outWithheldSummary = "RAW Exposure withheld: Raw Technical headroom";
            outWarning =
                "Rendered post-WB Raw Placement suggested a lift, but Raw Technical WB-scaled headroom is below the minimum visible one-click move.";
            return false;
        }
    }

    float visibleCapEv = 0.0f;
    const char* capRationale = "visible one-click limit";
    if (preWhiteBalanceRecommendation.confidence >= kStartingPointRawExposureConfidenceGate) {
        visibleCapEv = kStartingPointRawExposureOneClickCapEv;
    } else if (preWhiteBalanceRecommendation.confidence >= kStartingPointRawExposureCautiousConfidenceGate) {
        visibleCapEv = kStartingPointRawExposureCautiousCapEv;
        capRationale = "cautious post-WB one-click cap";
    } else if (stagedDeltaEv > 0.0f &&
        preWhiteBalanceRecommendation.confidence >= kStartingPointRawExposureTinyLiftConfidenceGate) {
        visibleCapEv = kStartingPointRawExposureTinyLiftCapEv;
        capRationale = "tiny positive post-WB one-click cap";
    } else if (stagedDeltaEv < 0.0f &&
        preWhiteBalanceRecommendation.confidence >= kStartingPointRawExposureDownwardConfidenceGate) {
        visibleCapEv = kStartingPointRawExposureDownwardCapEv;
        capRationale = "small downward post-WB one-click cap";
    } else {
        outWithheldSummary =
            "RAW Exposure suggestion kept manual: confidence is below the post-WB one-click gate";
        outWarning =
            "Rendered post-WB Raw Placement evidence existed, but the original RAW Exposure confidence was below the lower-confidence one-click nudge gates.";
        return false;
    }

    stagedDeltaEv = std::clamp(stagedDeltaEv, -visibleCapEv, visibleCapEv);
    const bool cappedToOneClick = std::abs(stagedDeltaEv - renderedDeltaEv) > 0.001f;
    if (std::abs(stagedDeltaEv) < kStartingPointRawExposureMinMoveEv) {
        outWithheldSummary = "RAW Exposure already near post-WB target";
        return false;
    }

    outRecommendation.valid = true;
    outRecommendation.autoApplyAllowed = true;
    outRecommendation.blockedByHighlightRisk = false;
    outRecommendation.deltaEv = stagedDeltaEv;
    outRecommendation.suggestedEv = currentRecipe.preToneExposureEv + stagedDeltaEv;
    outRecommendation.rationale =
        cappedToOneClick
            ? std::string("Rendered post-WB Raw Placement capped RAW Exposure to the ") + capRationale + " using Raw Technical headroom; larger move remains manual."
            : "Rendered post-WB Raw Placement allowed RAW Exposure after Suggested WB using visible candidate evidence.";
    return true;
}

RawAutoStartPointScoreTerm MakeScoreTerm(
    std::string id,
    std::string label,
    float value01,
    float weight,
    std::string rationale) {
    RawAutoStartPointScoreTerm term;
    term.id = std::move(id);
    term.label = std::move(label);
    term.value01 = Saturate01(value01);
    term.weight = weight;
    term.weightedValue = term.value01 * weight;
    term.rationale = std::move(rationale);
    return term;
}

RawAutoStartPointPenaltyTerm MakePenaltyTerm(
    std::string id,
    std::string label,
    float value,
    float weight,
    std::string rationale) {
    RawAutoStartPointPenaltyTerm term;
    term.id = std::move(id);
    term.label = std::move(label);
    term.value = Saturate01(value);
    term.weight = weight;
    term.weightedValue = term.value * weight;
    term.rationale = std::move(rationale);
    return term;
}

float ScenePlacementScoreFromEv(float medianEv, float whiteAnchorEv) {
    if (!std::isfinite(medianEv) || !std::isfinite(whiteAnchorEv)) {
        return 0.0f;
    }
    constexpr float targetMedianRelativeToWhiteEv = -2.7f;
    const float targetMedianEv = whiteAnchorEv + targetMedianRelativeToWhiteEv;
    return Closeness01(std::abs(medianEv - targetMedianEv), 0.20f, 1.00f);
}

float ScenePlacementScoreFromSceneStats(const RawAutoStartPointSceneStageStats& stats) {
    const float whiteAnchorEv = std::isfinite(stats.evPercentiles.p999)
        ? stats.evPercentiles.p999
        : stats.evPercentiles.p99;
    return ScenePlacementScoreFromEv(stats.evPercentiles.p50, whiteAnchorEv);
}

float ScenePlacementScoreFromAnalysis(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    std::string& rationale) {
    const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
    if (!stats.valid) {
        rationale = "Current-frame scene placement stats are unavailable.";
        return 0.0f;
    }
    const float whiteAnchorEv = std::isfinite(stats.p999Ev) ? stats.p999Ev : stats.p99Ev;
    rationale =
        "Fallback from current-frame pre-display stats; raw-placement readback is not captured yet.";
    return ScenePlacementScoreFromEv(stats.p50Ev, whiteAnchorEv) * 0.60f;
}

float ComputeScenePlacementScore(
    RawAutoStartPointCandidateKind kind,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::RawExposureRecommendation& exposure,
    std::string& rationale) {
    if (kind == RawAutoStartPointCandidateKind::Base) {
        if (const RawAutoStartPointStageDiagnostics* rawPlacementStage =
                FindStageDiagnostics(stages, RawAutoStartPointStage::RawPlacement);
            rawPlacementStage != nullptr && rawPlacementStage->scene.valid) {
            float score = ScenePlacementScoreFromSceneStats(rawPlacementStage->scene);
            if (rawPlacementStage->status == RawAutoStartPointStageStatus::Projected) {
                score *= 0.88f;
                rationale =
                    "Scored from Base projected Raw Placement stage after the visible RAW Exposure candidate; this is not a rendered candidate readback.";
            } else if (IsRenderedCandidateStage(*rawPlacementStage)) {
                rationale =
                    "Scored from rendered Base Raw Placement candidate evidence after the visible RAW Exposure candidate.";
            } else {
                rationale =
                    "Scored from current Raw Placement stage readback before Local Range, Finish Tone, and View Transform.";
            }
            if (exposure.valid && !exposure.blockedByHighlightRisk) {
                score = std::max(score, 0.35f + exposure.confidence * 0.50f);
                rationale += " Base projection also uses RAW Exposure recommendation confidence.";
            }
            return Saturate01(score);
        }
        if (const RawAutoStartPointSceneStageStats* preDisplay =
                FindSceneStats(stages, RawAutoStartPointStage::FinishToneCandidate)) {
            float score = ScenePlacementScoreFromSceneStats(*preDisplay) * 0.65f;
            if (exposure.valid && !exposure.blockedByHighlightRisk) {
                score = std::max(score, 0.35f + exposure.confidence * 0.50f);
            }
            rationale =
                "Raw Placement readback is unavailable; Base score projects from the RAW Exposure suggestion and current pre-display stage.";
            return Saturate01(score);
        }
    } else if (const RawAutoStartPointSceneStageStats* preDisplay =
                   FindSceneStats(stages, RawAutoStartPointStage::FinishToneCandidate)) {
        rationale = "Scored from current Finish Tone Candidate pre-View-Transform readback.";
        return ScenePlacementScoreFromSceneStats(*preDisplay) * 0.75f;
    }

    float score = ScenePlacementScoreFromAnalysis(analysis, rationale);
    if (kind == RawAutoStartPointCandidateKind::Base &&
        exposure.valid &&
        !exposure.blockedByHighlightRisk) {
        score = std::max(score, 0.35f + exposure.confidence * 0.50f);
        rationale += " Base projection also uses RAW Exposure recommendation confidence.";
    }
    return Saturate01(score);
}

float ComputeToneShapeScore(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    std::string& rationale) {
    const RawAutoStartPointSceneStageStats* preDisplay =
        FindSceneStats(stages, RawAutoStartPointStage::FinishToneCandidate);
    if (preDisplay == nullptr) {
        rationale = "Pre-display Finish Tone stage is unavailable; tone score is neutral.";
        return 0.50f;
    }
    const float spreadScore = HighIsGood01(preDisplay->midSpreadEv, 0.75f, 1.75f);
    float score = 0.20f + 0.80f * spreadScore;
    if (preDisplay->status == RawAutoStartPointStageStatus::Projected) {
        score *= 0.90f;
        rationale =
            "Scored from projected Base Finish Tone Candidate after the visible RAW Exposure candidate; this is not a rendered candidate readback.";
    } else if (const RawAutoStartPointStageDiagnostics* stage =
                   FindStageDiagnostics(stages, RawAutoStartPointStage::FinishToneCandidate);
               stage != nullptr && IsRenderedCandidateStage(*stage)) {
        rationale =
            "Scored from rendered Finish Tone Candidate evidence before View Transform can hide contrast.";
    } else {
        rationale =
            "Scored from Finish Tone Candidate mid-spread before View Transform can hide contrast.";
    }
    return Saturate01(score);
}

float ComputeDisplayReadabilityScore(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    std::string& rationale) {
    const RawAutoStartPointStageDiagnostics* displayStage =
        FindStageDiagnostics(stages, RawAutoStartPointStage::DisplayCandidate);
    if (displayStage != nullptr &&
        displayStage->status == RawAutoStartPointStageStatus::Pending) {
        rationale = displayStage->statusMessage.empty()
            ? "Base Display Candidate is pending until a post-edit Display Fit render is available."
            : displayStage->statusMessage;
        return 0.0f;
    }
    const RawAutoStartPointDisplayStageStats* display = FindDisplayStats(stages);
    if (display == nullptr) {
        rationale = "Display Candidate readback is unavailable.";
        return 0.0f;
    }
    constexpr float targetDisplayMid = 0.45f;
    const float midScore =
        Closeness01(std::abs(display->displayP50 - targetDisplayMid), 0.08f, 0.28f);
    const float spreadScore = HighIsGood01(display->displaySpread, 0.35f, 0.70f);
    const float highClipScore = LowIsGood01(display->displayClipHighFraction, 0.001f, 0.020f);
    const float lowClipScore = LowIsGood01(display->displayClipLowFraction, 0.001f, 0.030f);
    const bool renderedCandidate = displayStage != nullptr && IsRenderedCandidateStage(*displayStage);
    rationale = renderedCandidate
        ? (display->metricsAreLinearDisplay
              ? "Scored from rendered Display Candidate linear-display readback."
              : "Scored from rendered Display Candidate display readback.")
        : (display->metricsAreLinearDisplay
              ? "Scored from Display Candidate linear-display readback."
              : "Scored from Display Candidate display readback.");
    return Saturate01(
        0.35f * midScore +
        0.25f * spreadScore +
        0.20f * highClipScore +
        0.20f * lowClipScore);
}

float ComputeLocalConflictScore(
    RawAutoStartPointCandidateKind kind,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    std::string& rationale) {
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> balancedLocal =
        Stack::RawAutoBase::SelectBalancedLocalRangeAdjustments(recommendations);

    if (kind == RawAutoStartPointCandidateKind::Balanced) {
        if (balancedLocal.empty()) {
            rationale =
                "Balanced Local Range has no suggestion that passes the 70% confidence and 1.00 EV caps.";
            return 0.55f;
        }

        float confidenceSum = 0.0f;
        for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : balancedLocal) {
            confidenceSum += std::clamp(suggestion.confidence, 0.0f, 1.0f);
        }
        const float averageConfidence =
            confidenceSum / static_cast<float>(balancedLocal.size());
        rationale =
            "Balanced Local Range uses only capped visible graph suggestions: confidence >= 70%, |delta| <= 1.00 EV, max two authored adjustment points.";
        return Saturate01(0.65f + 0.35f * averageConfidence);
    }

    float maxConfidence = 0.0f;
    int validSuggestions = 0;
    for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion :
         recommendations.localAdjustments) {
        if (!suggestion.valid) {
            continue;
        }
        ++validSuggestions;
        maxConfidence = std::max(maxConfidence, suggestion.confidence);
    }
    if (validSuggestions == 0) {
        rationale = "No current Local Range suggestions require Base to add local edits.";
        return 1.0f;
    }
    rationale =
        "Local Range suggestions exist; Base stays conservative and leaves local edits unchanged in this dry run.";
    return Saturate01(1.0f - 0.35f * maxConfidence);
}

float ComputeEditConservatismScore(
    RawAutoStartPointCandidateKind kind,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const Stack::RawAutoBase::ViewFitDecision& fitDecision,
    std::string& rationale) {
    if (kind == RawAutoStartPointCandidateKind::CurrentFit) {
        rationale = fitDecision.canApply
            ? "CurrentFit changes only Display Fit in the dry-run candidate."
            : "CurrentFit is conservative, but Display Fit cannot be estimated yet.";
        return fitDecision.canApply ? 0.98f : 0.80f;
    }

    const Stack::RawAutoBase::RawExposureRecommendation& exposure = recommendations.exposure;
    float score = 0.95f;
    if (exposure.valid && !exposure.blockedByHighlightRisk) {
        score = 1.0f - SmoothStep(0.50f, 1.50f, std::abs(exposure.deltaEv));
    }
    if (recommendations.whiteBalance.autoApplyAllowed) {
        score *= 0.92f;
    }
    if (kind == RawAutoStartPointCandidateKind::Balanced) {
        const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> balancedLocal =
            Stack::RawAutoBase::SelectBalancedLocalRangeAdjustments(recommendations);
        float maxDeltaEv = 0.0f;
        for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : balancedLocal) {
            maxDeltaEv = std::max(maxDeltaEv, std::abs(suggestion.deltaEv));
        }
        score *= (balancedLocal.empty() ? 0.88f : 0.92f);
        score *= 1.0f - 0.10f * SmoothStep(0.60f, 1.00f, maxDeltaEv);
        rationale =
            "Balanced Local Range stays conservative by limiting Local Range to at most two visible capped adjustment points.";
        return Saturate01(score);
    }

    rationale =
        "Base score favors small RAW Exposure moves, metadata/default WB, and Display Fit only.";
    return Saturate01(score);
}

float ComputeRawSafetyScore(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    std::string& rationale) {
    if (const RawAutoStartPointStageDiagnostics* rawTechnical =
            FindStageDiagnostics(stages, RawAutoStartPointStage::RawTechnical)) {
        const RawAutoStartPointRawSafetyStats& rawSafety = rawTechnical->rawSafety;
        if (rawTechnical->status == RawAutoStartPointStageStatus::Complete && rawSafety.valid) {
            const float maxNearClip = std::max({
                rawSafety.perChannelNearClippedFraction[0],
                rawSafety.perChannelNearClippedFraction[1],
                rawSafety.perChannelNearClippedFraction[2]
            });
            const float headroomScore = HighIsGood01(rawSafety.wbScaledHeadroomEv, 0.25f, 2.0f);
            const float clipScore = LowIsGood01(maxNearClip, 0.001f, 0.020f);
            const float fullClipScore = LowIsGood01(rawSafety.fullClipFraction, 0.0001f, 0.005f);
            rationale =
                "Scored from Raw Technical safety ledger: WB-scaled headroom, per-channel near clipping, and full-clip fraction.";
            return Saturate01(
                0.45f * headroomScore +
                0.35f * clipScore +
                0.20f * std::min(fullClipScore, rawSafety.highlightRecoverabilityScore));
        }
    }
    if (!analysis.valid) {
        rationale = "Raw safety fallback cannot run without analysis.";
        return 0.0f;
    }
    const float highlightRisk = Stack::RawAutoBase::ComputeHighlightRisk01(analysis);
    rationale =
        "Fallback from current highlight signals; dedicated raw safety ledger is not captured yet.";
    return Saturate01(1.0f - highlightRisk);
}

RawAutoStartPointCandidateScore BuildCandidateScore(
    RawAutoStartPointCandidateKind kind,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const Stack::RawAutoBase::ViewFitDecision& fitDecision) {
    RawAutoStartPointCandidateScore score;
    score.valid = analysis.valid || !stages.empty();

    std::string rationale;
    const float rawSafetyScore = ComputeRawSafetyScore(stages, analysis, rationale);
    score.terms.push_back(
        MakeScoreTerm("raw-safety", "Raw Safety", rawSafetyScore, 25.0f, rationale));

    rationale.clear();
    const float scenePlacementScore =
        ComputeScenePlacementScore(kind, stages, analysis, recommendations.exposure, rationale);
    score.terms.push_back(MakeScoreTerm(
        "scene-placement",
        "Scene Placement",
        scenePlacementScore,
        25.0f,
        rationale));

    rationale.clear();
    const float localConflictScore = ComputeLocalConflictScore(kind, recommendations, rationale);
    score.terms.push_back(MakeScoreTerm(
        "local-conflict",
        "Local Conflict",
        localConflictScore,
        15.0f,
        rationale));

    rationale.clear();
    const float toneShapeScore = ComputeToneShapeScore(stages, rationale);
    score.terms.push_back(
        MakeScoreTerm("tone-shape", "Tone Shape", toneShapeScore, 10.0f, rationale));

    rationale.clear();
    const float displayReadabilityScore = ComputeDisplayReadabilityScore(stages, rationale);
    score.terms.push_back(MakeScoreTerm(
        "display-readability",
        "Display Readability",
        displayReadabilityScore,
        15.0f,
        rationale));

    rationale.clear();
    const float editConservatismScore =
        ComputeEditConservatismScore(kind, recommendations, fitDecision, rationale);
    score.terms.push_back(MakeScoreTerm(
        "edit-conservatism",
        "Edit Conservatism",
        editConservatismScore,
        10.0f,
        rationale));

    const Stack::RawAutoBase::NoiseDetailRecommendation& noiseDetail = recommendations.noiseDetail;
    if (noiseDetail.valid) {
        const float noisePenalty =
            Saturate01(noiseDetail.effectiveNoiseScore * (0.65f + 0.10f * noiseDetail.shadowLiftEv));
        score.penalties.push_back(MakePenaltyTerm(
            "noise-risk",
            "Noise Risk",
            noisePenalty,
            6.0f,
            "Noise/detail recommendation estimates risk from ISO metadata and shadow-lift pressure."));
    }

    float colorPenalty = 0.0f;
    std::string colorRationale = "Camera/as-shot white balance metadata is available.";
    if (!analysis.metadata.hasCameraWhiteBalance) {
        colorPenalty = recommendations.whiteBalance.confidence >= 0.85f ? 0.10f : 0.30f;
        colorRationale =
            "Camera/as-shot white balance metadata is unavailable, so image-derived WB remains uncertain.";
    } else if (recommendations.whiteBalance.alternateCandidateAvailable &&
               recommendations.whiteBalance.confidence < 0.50f) {
        colorPenalty = 0.12f;
        colorRationale =
            "Alternate neutral estimate is weak; Base keeps metadata/current WB.";
    }
    if (colorPenalty > 0.0f) {
        score.penalties.push_back(MakePenaltyTerm(
            "color-constancy",
            "Color Constancy",
            colorPenalty,
            6.0f,
            colorRationale));
    }

    const float hiddenCompensationPenalty =
        std::max(0.0f, displayReadabilityScore - scenePlacementScore) * 0.5f +
        std::max(0.0f, displayReadabilityScore - toneShapeScore) * 0.5f;
    if (hiddenCompensationPenalty > 0.0f) {
        score.penalties.push_back(MakePenaltyTerm(
            "hidden-compensation",
            "Hidden Compensation",
            hiddenCompensationPenalty,
            10.0f,
            "Penalizes candidates that look readable mainly because Display Fit hides upstream placement or tone issues."));
    }

    float total = 0.0f;
    for (const RawAutoStartPointScoreTerm& term : score.terms) {
        total += term.weightedValue;
    }
    for (const RawAutoStartPointPenaltyTerm& penalty : score.penalties) {
        total -= penalty.weightedValue;
    }
    score.totalScore = std::clamp(total, 0.0f, 100.0f);

    score.subscores.valid = score.valid;
    score.subscores.rawSafetyScore = rawSafetyScore;
    score.subscores.scenePlacementScore = scenePlacementScore;
    score.subscores.localConflictScore = localConflictScore;
    score.subscores.toneShapeScore = toneShapeScore;
    score.subscores.displayReadabilityScore = displayReadabilityScore;
    score.subscores.editConservatismScore = editConservatismScore;
    for (const RawAutoStartPointPenaltyTerm& penalty : score.penalties) {
        if (penalty.id == "noise-risk") {
            score.subscores.noisePenalty = penalty.value;
        } else if (penalty.id == "color-constancy") {
            score.subscores.colorConstancyPenalty = penalty.value;
        } else if (penalty.id == "hidden-compensation") {
            score.subscores.hiddenCompensationPenalty = penalty.value;
        }
    }

    score.summary =
        "Dry-run score " + FormatScore(score.totalScore) +
        "/100. Terms are weighted diagnostics, not an applied decision.";
    return score;
}

const char* WhiteBalanceMethodLabel(
    Stack::RawAutoBase::WhiteBalanceRecommendation::Method method) {
    switch (method) {
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::CameraAsShot:
            return "Camera/as-shot";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld:
            return "Gray World";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::ShadesOfGray:
            return "Shades of Gray";
        case Stack::RawAutoBase::WhiteBalanceRecommendation::Method::GreyEdge:
            return "Grey Edge";
        default:
            return "Unknown";
    }
}

std::string ResolveWhiteBalancePolicy(
    const Stack::RawAutoBase::WhiteBalanceRecommendation& whiteBalance) {
    if (whiteBalance.cameraWhiteBalanceAvailable) {
        return "Keep camera/as-shot WB metadata.";
    }
    if (whiteBalance.autoApplyAllowed && whiteBalance.alternateCandidateAvailable) {
        std::ostringstream out;
        out << "Apply Suggested WB (" << WhiteBalanceMethodLabel(whiteBalance.method)
            << ") as visible multipliers.";
        return out.str();
    }
    if (whiteBalance.manualWhiteBalanceProtected) {
        return "Keep current manual/custom WB.";
    }
    if (whiteBalance.alternateCandidateAvailable) {
        return "Keep current WB; Suggested WB remains manual.";
    }
    return "Keep current WB; image-derived WB remains a suggestion.";
}

std::string FormatConfidencePercent(float confidence01) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0)
        << std::clamp(confidence01, 0.0f, 1.0f) * 100.0f << "%";
    return out.str();
}

std::string FormatBalancedLocalSuggestionSummary(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions) {
    if (suggestions.empty()) {
        return "No Balanced Local Range point passed the 70% confidence and 1.00 EV delta caps.";
    }

    std::ostringstream out;
    out << "Local Range: " << suggestions.size()
        << (suggestions.size() == 1 ? " authored adjustment point: " : " authored adjustment points: ");
    for (std::size_t i = 0; i < suggestions.size(); ++i) {
        const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion = suggestions[i];
        if (i > 0) {
            out << "; ";
        }
        out << (suggestion.label.empty()
            ? Stack::RawAutoBase::SuggestedLocalAdjustmentKindLabel(suggestion.kind)
            : suggestion.label)
            << " " << FormatSignedEv(suggestion.deltaEv)
            << " at scene " << FormatSignedEv(suggestion.targetEv)
            << ", width " << std::fixed << std::setprecision(2) << suggestion.widthEv << " EV"
            << ", feather " << suggestion.feather
            << ", confidence " << FormatConfidencePercent(suggestion.confidence);
        if (std::isfinite(suggestion.affectedAreaPercent) &&
            suggestion.affectedAreaPercent > 0.0f) {
            out << ", area " << std::setprecision(1) << suggestion.affectedAreaPercent << "%";
        }
        if (suggestion.colorQualifierEnabled) {
            out << ", color-targeted"
                << " width " << std::setprecision(2) << suggestion.colorWidth
                << ", color feather " << suggestion.colorFeather;
        }
    }
    return out.str();
}

bool LocalSuggestionPassesStrictOneClickGate(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.valid &&
        suggestion.confidence >= kStartingPointLocalRangeStrictConfidenceGate &&
        std::isfinite(suggestion.targetEv) &&
        std::isfinite(suggestion.deltaEv);
}

bool LocalSuggestionPassesCautiousOneClickGate(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.valid &&
        suggestion.confidence >= kStartingPointLocalRangeCautiousConfidenceGate &&
        std::isfinite(suggestion.targetEv) &&
        std::isfinite(suggestion.deltaEv);
}

bool LocalSuggestionIsContextualOneClickKind(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.kind == Stack::RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows ||
        suggestion.kind == Stack::RawAutoBase::SuggestedLocalAdjustmentKind::OpenBacklitSubject ||
        suggestion.kind == Stack::RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky ||
        suggestion.kind == Stack::RawAutoBase::SuggestedLocalAdjustmentKind::RecoverHighlights;
}

bool LocalSuggestionPassesContextOneClickGate(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.valid &&
        LocalSuggestionIsContextualOneClickKind(suggestion) &&
        suggestion.confidence >= kStartingPointLocalRangeContextConfidenceGate &&
        std::isfinite(suggestion.targetEv) &&
        std::isfinite(suggestion.deltaEv);
}

Stack::RawAutoBase::SuggestedLocalAdjustment CapLocalSuggestionDelta(
    Stack::RawAutoBase::SuggestedLocalAdjustment suggestion,
    float maxAbsDeltaEv) {
    suggestion.deltaEv = std::clamp(
        suggestion.deltaEv,
        -std::abs(maxAbsDeltaEv),
        std::abs(maxAbsDeltaEv));
    return suggestion;
}

bool LocalSuggestionIsDarkRevealLift(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.kind == Stack::RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows ||
        suggestion.kind == Stack::RawAutoBase::SuggestedLocalAdjustmentKind::OpenBacklitSubject;
}

bool LocalSuggestionPassesDarkRevealOneClickGate(
    const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
    return suggestion.valid &&
        LocalSuggestionIsDarkRevealLift(suggestion) &&
        suggestion.confidence >= kStartingPointDarkRevealLocalConfidenceGate &&
        suggestion.deltaEv > 0.0f &&
        std::isfinite(suggestion.targetEv) &&
        std::isfinite(suggestion.deltaEv);
}

Stack::RawAutoBase::SuggestedLocalAdjustment PromoteLocalSuggestionForDarkReveal(
    Stack::RawAutoBase::SuggestedLocalAdjustment suggestion,
    float darkRevealScore01) {
    if (!LocalSuggestionIsDarkRevealLift(suggestion) || suggestion.deltaEv <= 0.0f) {
        return suggestion;
    }

    const float minimumLiftEv =
        0.70f + 0.35f * Saturate01(darkRevealScore01);
    suggestion.deltaEv = std::min(
        kStartingPointDarkRevealLocalMaxAbsDeltaEv,
        std::max(suggestion.deltaEv, minimumLiftEv));
    if (!suggestion.rationale.empty()) {
        suggestion.rationale += " ";
    }
    suggestion.rationale +=
        "Build Starting Point promoted this to a dark-reveal visible lift; noise remains a review warning instead of a one-click blocker.";
    return suggestion;
}

std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> SelectOneClickLocalSuggestions(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions,
    std::size_t maxSuggestions = 2,
    float darkRevealScore01 = 0.0f) {
    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> selected;
    if (maxSuggestions == 0) {
        return selected;
    }
    const std::size_t cappedMax = std::min<std::size_t>(maxSuggestions, 2);
    selected.reserve(cappedMax);
    const bool darkReveal = darkRevealScore01 >= kStartingPointDarkRevealGate;
    if (darkReveal) {
        for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : suggestions) {
            if (!LocalSuggestionPassesDarkRevealOneClickGate(suggestion)) {
                continue;
            }
            selected.push_back(PromoteLocalSuggestionForDarkReveal(
                CapLocalSuggestionDelta(
                    suggestion,
                    kStartingPointDarkRevealLocalMaxAbsDeltaEv),
                darkRevealScore01));
            if (selected.size() >= cappedMax) {
                break;
            }
        }
        if (!selected.empty()) {
            return selected;
        }
    }

    for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : suggestions) {
        const bool passesStrict = LocalSuggestionPassesStrictOneClickGate(suggestion);
        const bool passesCautious = LocalSuggestionPassesCautiousOneClickGate(suggestion);
        const bool passesContext = LocalSuggestionPassesContextOneClickGate(suggestion);
        if (!passesStrict && !passesCautious && !passesContext) {
            continue;
        }

        const bool selectedHasColorQualifier = std::any_of(
            selected.begin(),
            selected.end(),
            [](const Stack::RawAutoBase::SuggestedLocalAdjustment& item) {
                return item.colorQualifierEnabled;
            });
        if (selectedHasColorQualifier) {
            break;
        }
        if (suggestion.colorQualifierEnabled && !selected.empty()) {
            continue;
        }

        selected.push_back(CapLocalSuggestionDelta(
            suggestion,
            passesStrict
                ? kStartingPointLocalRangeStrictMaxAbsDeltaEv
                : passesCautious
                    ? kStartingPointLocalRangeCautiousMaxAbsDeltaEv
                    : kStartingPointLocalRangeContextMaxAbsDeltaEv));
        if (selected.size() >= cappedMax || suggestion.colorQualifierEnabled) {
            break;
        }
    }
    return selected;
}

Stack::RawAutoBase::SuggestedLocalAdjustment ShiftLocalSuggestionForUpstreamExposure(
    Stack::RawAutoBase::SuggestedLocalAdjustment suggestion,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawRecipe::RawDevelopmentRecipe& upstreamRecipe,
    bool allowShift) {
    if (!allowShift) {
        return suggestion;
    }
    const float rawExposureDeltaEv =
        upstreamRecipe.preToneExposureEv - currentRecipe.preToneExposureEv;
    if (std::isfinite(rawExposureDeltaEv) &&
        std::abs(rawExposureDeltaEv) >= 0.001f &&
        std::isfinite(suggestion.targetEv)) {
        suggestion.targetEv += rawExposureDeltaEv;
    }
    return suggestion;
}

std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> ShiftLocalSuggestionsForUpstreamExposure(
    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> suggestions,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawRecipe::RawDevelopmentRecipe& upstreamRecipe,
    bool allowShift) {
    for (Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : suggestions) {
        suggestion =
            ShiftLocalSuggestionForUpstreamExposure(
                suggestion,
                currentRecipe,
                upstreamRecipe,
                allowShift);
    }
    return suggestions;
}

Stack::RawAutoBase::SuggestedLocalAdjustment BuildWideRangeFallbackLocalSuggestion(
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    Stack::RawAutoBase::SuggestedLocalAdjustment suggestion;
    const RawAutoStartPointSceneStageStats* scene =
        FindSceneStats(stages, RawAutoStartPointStage::FinishToneCandidate);
    if (scene == nullptr) {
        scene = FindSceneStats(stages, RawAutoStartPointStage::RawPlacement);
    }

    float targetEv = std::numeric_limits<float>::quiet_NaN();
    float wideSpreadEv = 0.0f;
    bool usedCurrentFrameFallback = false;
    if (scene != nullptr &&
        scene->evPercentiles.valid &&
        std::isfinite(scene->wideSpreadEv) &&
        scene->wideSpreadEv >= 3.75f) {
        wideSpreadEv = scene->wideSpreadEv;
        targetEv = std::isfinite(scene->evPercentiles.p25)
            ? scene->evPercentiles.p25
            : scene->evPercentiles.p05;
    } else if (analysis.valid && analysis.currentFrameStats.valid) {
        const Stack::RawAnalysis::PercentileStats& stats = analysis.currentFrameStats;
        const float shadowSpanEv = stats.p50Ev - stats.p05Ev;
        if (!std::isfinite(stats.dynamicRangeEv) ||
            stats.dynamicRangeEv < 6.0f ||
            !std::isfinite(stats.p05Ev) ||
            !std::isfinite(stats.p50Ev) ||
            !std::isfinite(shadowSpanEv) ||
            shadowSpanEv < 1.25f) {
            return suggestion;
        }
        wideSpreadEv = stats.dynamicRangeEv;
        targetEv = stats.p05Ev + 0.44f * shadowSpanEv;
        usedCurrentFrameFallback = true;
    }
    if (!std::isfinite(targetEv)) {
        return suggestion;
    }

    const float rangeScore = SmoothStep(
        usedCurrentFrameFallback ? 6.0f : 3.75f,
        usedCurrentFrameFallback ? 11.0f : 7.0f,
        wideSpreadEv);
    suggestion.valid = true;
    suggestion.kind = Stack::RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    suggestion.targetEv = targetEv;
    suggestion.deltaEv = 0.25f + 0.15f * rangeScore;
    suggestion.widthEv = 2.4f;
    suggestion.feather = 0.72f;
    suggestion.protectHighlights = true;
    suggestion.confidence = 0.55f + 0.10f * rangeScore;
    suggestion.affectedAreaPercent = 25.0f;
    suggestion.label = "Balance deep shadows";
    suggestion.rationale = usedCurrentFrameFallback
        ? "Creates a conservative visible Local Range lift near the lower scene quartile because broad current-frame RAW range and shadow span were present but no reliable semantic region proposal passed."
        : "Creates a conservative visible Local Range lift at the lower scene quartile because wide-range staged evidence found no reliable semantic region proposal.";
    return suggestion;
}

bool ApplyLocalSuggestionsToRecipe(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& appliedSuggestions) {
    appliedSuggestions.clear();
    for (const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion : suggestions) {
        Stack::RawRecipe::RawDevelopmentRecipe candidateRecipe = recipe;
        if (!Stack::RawAutoBase::ApplySuggestedLocalAdjustment(
                suggestion,
                candidateRecipe)) {
            continue;
        }
        recipe = std::move(candidateRecipe);
        appliedSuggestions.push_back(suggestion);
    }
    return !appliedSuggestions.empty();
}

std::string FormatLocalRangeAppliedSummary(std::size_t pointCount) {
    std::ostringstream out;
    out << "Local Range " << pointCount
        << (pointCount == 1 ? " point" : " points");
    return out.str();
}

nlohmann::json MakeTonePoint(float x, float y) {
    return nlohmann::json {
        { "x", JsonFloat(std::clamp(x, 0.0f, 1.0f)) },
        { "y", JsonFloat(std::clamp(y, 0.0f, 1.0f)) },
        { "shape", 1 }
    };
}

bool FinishTonePointsLookNeutral(const nlohmann::json& pointsJson) {
    if (!pointsJson.is_array()) {
        return true;
    }
    for (const nlohmann::json& item : pointsJson) {
        if (!item.is_object()) {
            continue;
        }
        const float x = JsonNumberOr(item, "x", 0.0f);
        const float y = JsonNumberOr(item, "y", x);
        if (!std::isfinite(x) || !std::isfinite(y)) {
            continue;
        }
        if (std::abs(std::clamp(x, 0.0f, 1.0f) - std::clamp(y, 0.0f, 1.0f)) > 0.003f) {
            return false;
        }
    }
    return true;
}

bool FinishToneHasHiddenNonDefaultTone(const nlohmann::json& finishTone) {
    if (!finishTone.is_object()) {
        return false;
    }
    const auto boolEnabled = [&](const char* key) {
        const auto it = finishTone.find(key);
        return it != finishTone.end() && it->is_boolean() && it->get<bool>();
    };
    const auto finiteNonZero = [&](const char* key) {
        const auto it = finishTone.find(key);
        return it != finishTone.end() &&
            it->is_number() &&
            std::abs(it->get<float>()) > 0.001f;
    };

    return boolEnabled("localBaselineEnabled") ||
        boolEnabled("foundationAdaptiveAssist") ||
        finiteNonZero("localBaselineStrength") ||
        finiteNonZero("localShadowOpening") ||
        finiteNonZero("localHighlightCompression") ||
        finiteNonZero("foundationShadows") ||
        finiteNonZero("foundationDarks") ||
        finiteNonZero("foundationMidtones") ||
        finiteNonZero("foundationLights") ||
        finiteNonZero("foundationHighlights");
}

bool FinishToneLooksAutoWritableNeutral(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    std::string& reason) {
    const nlohmann::json& finishTone = currentRecipe.finishTone.layerJson;
    if (!finishTone.is_object()) {
        return true;
    }

    if (JsonIntOr(finishTone, "mode", 1) != 1 || JsonIntOr(finishTone, "domain", 1) != 1) {
        reason = "Finish Tone mode/domain were edited; user-owned Finish Tone is not auto-rewritten.";
        return false;
    }
    if (FinishToneHasHiddenNonDefaultTone(finishTone)) {
        reason = "Finish Tone has advanced tone fields outside the compact graph; auto authoring is withheld.";
        return false;
    }
    if (!FinishTonePointsLookNeutral(finishTone.value("points", nlohmann::json::array())) ||
        !FinishTonePointsLookNeutral(finishTone.value("preparedPoints", nlohmann::json::array()))) {
        reason = "Finish Tone graph already has non-neutral points; user-owned tone is not auto-rewritten.";
        return false;
    }
    return true;
}

RawAutoStartPointFinishToneProposal BuildMildFinishToneProposalFromStages(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages) {
    RawAutoStartPointFinishToneProposal proposal;
    proposal.layerJson = Stack::RawRecipe::DefaultFinishToneJson();

    std::string neutralReason;
    if (!FinishToneLooksAutoWritableNeutral(currentRecipe, neutralReason)) {
        proposal.summary = neutralReason;
        proposal.warnings.push_back(neutralReason);
        return proposal;
    }

    const RawAutoStartPointSceneStageStats* preDisplay =
        FindSceneStats(stages, RawAutoStartPointStage::FinishToneCandidate);
    if (preDisplay == nullptr || !preDisplay->valid) {
        proposal.summary =
            "Mild Finish Tone needs Finish Tone Candidate stage evidence before authoring visible points.";
        proposal.warnings.push_back(proposal.summary);
        return proposal;
    }

    const float midSpreadEv = preDisplay->midSpreadEv;
    const float wideSpreadEv = preDisplay->wideSpreadEv;
    if (!std::isfinite(midSpreadEv) || !std::isfinite(wideSpreadEv)) {
        proposal.summary = "Mild Finish Tone stage spread is unavailable.";
        proposal.warnings.push_back(proposal.summary);
        return proposal;
    }

    float strength =
        std::clamp(0.25f * (1.0f - SmoothStep(1.0f, 2.40f, midSpreadEv)), 0.0f, 0.25f);
    strength *= LowIsGood01(wideSpreadEv, 5.5f, 8.0f);
    if (analysis.highlight.valid) {
        strength *= LowIsGood01(analysis.highlight.displayClipPercent, 1.0f, 5.0f);
        strength *= LowIsGood01(analysis.highlight.hdrPixelPercent, 1.0f, 6.0f);
    }
    strength = std::clamp(strength, 0.0f, 0.25f);

    const float p01Ev = preDisplay->evPercentiles.valid
        ? preDisplay->evPercentiles.p01
        : analysis.currentFrameStats.p01Ev;
    const float p99Ev = preDisplay->evPercentiles.valid
        ? preDisplay->evPercentiles.p99
        : analysis.currentFrameStats.p99Ev;
    float logMinEv = std::clamp(std::floor(p01Ev - 0.5f), -20.0f, 0.0f);
    float logMaxEv = std::clamp(std::ceil(p99Ev + 0.5f), 0.0f, 20.0f);
    if (logMaxEv <= logMinEv + 0.1f) {
        logMaxEv = std::min(20.0f, logMinEv + 6.0f);
    }

    const float darkRevealScore = ComputeDarkRevealScore(analysis, stages);
    if (darkRevealScore >= kStartingPointDarkRevealGate) {
        const float revealStrength = std::clamp(
            0.18f + 0.17f * darkRevealScore,
            0.0f,
            kStartingPointDarkRevealFinishToneStrengthLimit);
        const float shadowX = 0.18f;
        const float lowerMidX = 0.45f;
        const float lightX = 0.75f;
        const float shadowY =
            std::clamp(shadowX + 0.36f * revealStrength, 0.0f, lowerMidX - 0.01f);
        const float lowerMidY =
            std::clamp(lowerMidX + 0.24f * revealStrength, shadowY + 0.01f, lightX - 0.01f);
        const float lightY =
            std::clamp(lightX + 0.06f * revealStrength, lowerMidY + 0.01f, 1.0f);
        nlohmann::json points = nlohmann::json::array({
            MakeTonePoint(0.0f, 0.0f),
            MakeTonePoint(shadowX, shadowY),
            MakeTonePoint(lowerMidX, lowerMidY),
            MakeTonePoint(lightX, lightY),
            MakeTonePoint(1.0f, 1.0f)
        });

        proposal.valid = true;
        proposal.shadowReveal = true;
        proposal.strength = revealStrength;
        proposal.midSpreadEv = midSpreadEv;
        proposal.wideSpreadEv = wideSpreadEv;
        proposal.logMinEv = logMinEv;
        proposal.logMaxEv = logMaxEv;
        proposal.layerJson["mode"] = 1;
        proposal.layerJson["domain"] = 1;
        proposal.layerJson["activeGraphView"] = 0;
        proposal.layerJson["logMinEv"] = logMinEv;
        proposal.layerJson["logMaxEv"] = logMaxEv;
        proposal.layerJson["points"] = points;
        proposal.layerJson["preparedPoints"] = points;

        std::ostringstream out;
        out << "Shadow-reveal visible Finish Tone curve: strength "
            << std::fixed << std::setprecision(2) << revealStrength
            << ", dark score " << darkRevealScore
            << ", mid spread " << midSpreadEv
            << " EV, graph range " << logMinEv << " to " << logMaxEv << " EV"
            << ", 5 visible points with shadow y " << std::setprecision(3) << shadowY
            << ", lower-mid y " << lowerMidY
            << ", light y " << lightY << ".";
        proposal.summary = out.str();
        proposal.warnings.push_back(
            "Shadow-reveal Finish Tone intentionally opens a dark RAW starting point; visible noise may increase and remains editable through the authored curve.");
        return proposal;
    }

    const float dynamicRangeScore = SmoothStep(3.8f, 6.5f, wideSpreadEv);
    const bool highlightPressure =
        analysis.highlight.valid &&
        (analysis.highlight.displayClipPercent > 1.0f ||
         analysis.highlight.hdrPixelPercent > 1.0f ||
         analysis.highlight.partialClipColorRisk);
    if ((strength < 0.035f && dynamicRangeScore >= 0.04f) || highlightPressure) {
        const float balanceStrength = std::clamp(
            0.08f + 0.10f * std::max(
                dynamicRangeScore,
                highlightPressure ? 0.35f : 0.0f),
            0.0f,
            kStartingPointFinishToneCautiousStrengthLimit);
        const float shadowX = 0.18f;
        const float lowerMidX = 0.45f;
        const float lightX = 0.75f;
        const float shadowY = std::clamp(
            shadowX + 0.40f * balanceStrength,
            0.0f,
            lowerMidX - 0.01f);
        const float lowerMidY = std::clamp(
            lowerMidX + 0.10f * balanceStrength,
            shadowY + 0.01f,
            lightX - 0.01f);
        const float lightY = std::clamp(
            lightX - 0.30f * balanceStrength,
            lowerMidY + 0.01f,
            1.0f);
        nlohmann::json points = nlohmann::json::array({
            MakeTonePoint(0.0f, 0.0f),
            MakeTonePoint(shadowX, shadowY),
            MakeTonePoint(lowerMidX, lowerMidY),
            MakeTonePoint(lightX, lightY),
            MakeTonePoint(1.0f, 1.0f)
        });

        proposal.valid = true;
        proposal.dynamicRangeBalance = true;
        proposal.strength = balanceStrength;
        proposal.midSpreadEv = midSpreadEv;
        proposal.wideSpreadEv = wideSpreadEv;
        proposal.logMinEv = logMinEv;
        proposal.logMaxEv = logMaxEv;
        proposal.layerJson["mode"] = 1;
        proposal.layerJson["domain"] = 1;
        proposal.layerJson["activeGraphView"] = 0;
        proposal.layerJson["logMinEv"] = logMinEv;
        proposal.layerJson["logMaxEv"] = logMaxEv;
        proposal.layerJson["points"] = points;
        proposal.layerJson["preparedPoints"] = points;

        std::ostringstream out;
        out << "Dynamic-range visible Finish Tone curve: strength "
            << std::fixed << std::setprecision(2) << balanceStrength
            << ", wide spread " << wideSpreadEv
            << " EV, graph range " << logMinEv << " to " << logMaxEv << " EV"
            << ", 5 visible points with shadow y " << std::setprecision(3) << shadowY
            << ", lower-mid y " << lowerMidY
            << ", light y " << lightY << ".";
        proposal.summary = out.str();
        proposal.warnings.push_back(
            "Dynamic-range Finish Tone gently opens shadows and compresses upper tones through visible graph points; it does not reconstruct clipped sensor data.");
        return proposal;
    }

    if (strength < 0.035f) {
        std::ostringstream out;
        out << "Mild Finish Tone withheld: pre-display contrast is already sufficient"
            << " (mid spread " << std::fixed << std::setprecision(2) << midSpreadEv
            << " EV, wide spread " << wideSpreadEv << " EV).";
        proposal.summary = out.str();
        proposal.warnings.push_back(proposal.summary);
        return proposal;
    }

    const float shadowY = std::clamp(0.25f - 0.07f * strength, 0.0f, 1.0f);
    const float lightY = std::clamp(0.75f + 0.07f * strength, 0.0f, 1.0f);
    nlohmann::json points = nlohmann::json::array({
        MakeTonePoint(0.0f, 0.0f),
        MakeTonePoint(0.25f, shadowY),
        MakeTonePoint(0.50f, 0.50f),
        MakeTonePoint(0.75f, lightY),
        MakeTonePoint(1.0f, 1.0f)
    });

    proposal.valid = true;
    proposal.strength = strength;
    proposal.midSpreadEv = midSpreadEv;
    proposal.wideSpreadEv = wideSpreadEv;
    proposal.logMinEv = logMinEv;
    proposal.logMaxEv = logMaxEv;
    proposal.layerJson["mode"] = 1;
    proposal.layerJson["domain"] = 1;
    proposal.layerJson["activeGraphView"] = 0;
    proposal.layerJson["logMinEv"] = logMinEv;
    proposal.layerJson["logMaxEv"] = logMaxEv;
    proposal.layerJson["points"] = points;
    proposal.layerJson["preparedPoints"] = points;

    std::ostringstream out;
    out << "Mild visible Finish Tone S-curve: strength "
        << std::fixed << std::setprecision(2) << strength
        << ", mid spread " << midSpreadEv
        << " EV, graph range " << logMinEv << " to " << logMaxEv << " EV"
        << ", 5 visible points with shadow y " << std::setprecision(3) << shadowY
        << ", mid y 0.500, light y " << lightY << ".";
    proposal.summary = out.str();
    return proposal;
}

RawAutoStartPointCandidate BuildCurrentFitCandidate(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const Stack::RawAutoBase::ViewFitDecision& fitDecision) {
    RawAutoStartPointCandidate candidate;
    candidate.valid = analysis.valid || !stages.empty();
    candidate.kind = RawAutoStartPointCandidateKind::CurrentFit;
    candidate.id = "current-fit-dry-run";
    candidate.label = "CurrentFit";
    candidate.summary =
        "Dry run: current recipe plus Display Fit proposal only; no recipe values were applied.";
    candidate.hasRecipe = true;
    candidate.recipe = currentRecipe;
    candidate.stageDiagnostics = stages;
    candidate.visibleEdits.rawExposureEvValid = true;
    candidate.visibleEdits.preToneExposureEv = currentRecipe.preToneExposureEv;
    candidate.visibleEdits.whiteBalanceValid = true;
    candidate.visibleEdits.whiteBalancePolicy = "Keep current WB.";
    candidate.visibleEdits.localRangeValid = false;
    candidate.visibleEdits.localRangeSummary = "CurrentFit leaves Local Range unchanged.";
    candidate.visibleEdits.finishToneValid = false;
    candidate.visibleEdits.finishToneNeutral = true;
    candidate.visibleEdits.finishToneSummary = "CurrentFit leaves Finish Tone unchanged.";
    candidate.visibleEdits.displayFitValid = fitDecision.canApply;
    candidate.visibleEdits.viewTransformSummary = FormatViewFitSummary(fitDecision.fit);
    if (fitDecision.canApply) {
        candidate.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::DisplayFit);
    } else {
        candidate.warnings.push_back("Display Fit proposal is unavailable until current-frame stats are ready.");
    }
    candidate.score =
        BuildCandidateScore(candidate.kind, stages, analysis, recommendations, fitDecision);
    return candidate;
}

RawAutoStartPointCandidate BuildBaseCandidate(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const RawAutoStartPointStageDiagnostics* renderedPostWhiteBalanceRawPlacementStage,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const Stack::RawAutoBase::ViewFitDecision& fitDecision) {
    RawAutoStartPointCandidate candidate;
    candidate.valid = analysis.valid || !stages.empty();
    candidate.kind = RawAutoStartPointCandidateKind::Base;
    candidate.id = "base-dry-run";
    candidate.label = "Base";
    candidate.summary =
        "Dry run: conservative Base proposal for RAW Exposure, staged Local Range evidence, Finish Tone evidence, and Display Fit.";
    candidate.hasRecipe = true;
    candidate.recipe = currentRecipe;
    std::vector<RawAutoStartPointStageDiagnostics> candidateStages = stages;
    const float darkRevealScore = ComputeDarkRevealScore(analysis, stages);

    const bool useSuggestedWhiteBalance =
        recommendations.whiteBalance.valid &&
        recommendations.whiteBalance.autoApplyAllowed &&
        recommendations.whiteBalance.alternateCandidateAvailable &&
        !recommendations.whiteBalance.cameraWhiteBalanceAvailable &&
        !recommendations.whiteBalance.manualWhiteBalanceProtected;
    candidate.visibleEdits.whiteBalanceValid = true;
    candidate.visibleEdits.whiteBalancePolicy = ResolveWhiteBalancePolicy(recommendations.whiteBalance);
    if (useSuggestedWhiteBalance) {
        Stack::RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(
            candidate.recipe,
            recommendations.whiteBalance);
        candidate.visibleEdits.whiteBalanceMultipliers = {
            recommendations.whiteBalance.gainsR,
            recommendations.whiteBalance.gainsG,
            recommendations.whiteBalance.gainsB
        };
        candidate.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::WhiteBalance);
    }

    Stack::RawAutoBase::RawExposureRecommendation exposure =
        BuildStartingPointRawExposureRecommendation(
            currentRecipe,
            stages,
            recommendations.exposure);
    bool useRenderedPostWhiteBalanceExposure = false;
    std::string postWhiteBalanceExposureWithheld;
    std::string postWhiteBalanceExposureWarning;
    if (useSuggestedWhiteBalance &&
        exposure.valid &&
        !exposure.blockedByHighlightRisk) {
        Stack::RawAutoBase::RawExposureRecommendation postWhiteBalanceExposure;
        if (BuildRenderedPostWhiteBalanceRawExposureRecommendation(
                currentRecipe,
                stages,
                renderedPostWhiteBalanceRawPlacementStage,
                exposure,
                postWhiteBalanceExposure,
                postWhiteBalanceExposureWithheld,
                postWhiteBalanceExposureWarning)) {
            exposure = postWhiteBalanceExposure;
            useRenderedPostWhiteBalanceExposure = true;
        }
    }
    const bool useSuggestedExposure =
        exposure.valid &&
        !exposure.blockedByHighlightRisk &&
        std::isfinite(exposure.suggestedEv) &&
        (!useSuggestedWhiteBalance || useRenderedPostWhiteBalanceExposure);
    candidate.visibleEdits.rawExposureEvValid = true;
    candidate.visibleEdits.preToneExposureEv =
        useSuggestedExposure ? exposure.suggestedEv : currentRecipe.preToneExposureEv;
    const bool rawExposureChanged =
        useSuggestedExposure &&
        std::abs(candidate.visibleEdits.preToneExposureEv - currentRecipe.preToneExposureEv) > 0.001f;
    if (rawExposureChanged) {
        candidate.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::RawExposure);
    }
    if (exposure.blockedByHighlightRisk) {
        candidate.warnings.push_back("Positive RAW Exposure is blocked by highlight risk.");
    }
    if (rawExposureChanged) {
        candidate.recipe.preToneExposureEv = candidate.visibleEdits.preToneExposureEv;
    } else if (useSuggestedWhiteBalance && exposure.valid) {
        candidate.warnings.push_back(
            postWhiteBalanceExposureWarning.empty()
                ? "Base RAW Exposure is held until post-WB Raw Placement evidence is available."
                : postWhiteBalanceExposureWarning);
    }
    const bool upstreamChanged = rawExposureChanged || useSuggestedWhiteBalance;
    if (upstreamChanged) {
        RawAutoStartPointStageDiagnostics projectedRawPlacement;
        RawAutoStartPointStageStatus rawPlacementRequestReplaces =
            RawAutoStartPointStageStatus::Unavailable;
        if (!useSuggestedWhiteBalance &&
            BuildProjectedBaseRawPlacementStage(
                stages,
                currentRecipe.preToneExposureEv,
                candidate.visibleEdits.preToneExposureEv,
                projectedRawPlacement)) {
            rawPlacementRequestReplaces = RawAutoStartPointStageStatus::Projected;
            ReplaceStageDiagnostics(candidateStages, std::move(projectedRawPlacement));
        }
        candidate.renderRequests.push_back(BuildCandidateRenderRequest(
            candidate.kind,
            RawAutoStartPointStage::RawPlacement,
            rawPlacementRequestReplaces,
            candidate.recipe,
            candidate.visibleEdits.touchedControls,
            rawPlacementRequestReplaces == RawAutoStartPointStageStatus::Projected
                ? "Render Base Raw Placement after the proposed visible RAW Exposure value instead of relying on projected staged evidence."
                : "Render Base Raw Placement after the proposed visible upstream Starting Point values; no complete matching current staged evidence exists to project this candidate."));
        Stack::RawRecipe::RawDevelopmentRecipe finishToneCandidateRecipe = candidate.recipe;
        std::vector<RawAutoStartPointControl> finishToneControls =
            candidate.visibleEdits.touchedControls;
        bool finishToneAfterLocalRange = false;
        const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> oneClickLocalSuggestions =
            upstreamChanged
                ? SelectOneClickLocalSuggestions(
                    recommendations.localAdjustments,
                    2,
                    darkRevealScore)
                : std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>();
        if (!oneClickLocalSuggestions.empty()) {
            const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> shiftedLocalSuggestions =
                ShiftLocalSuggestionsForUpstreamExposure(
                    oneClickLocalSuggestions,
                    currentRecipe,
                    candidate.recipe,
                    !useSuggestedWhiteBalance);
            Stack::RawRecipe::RawDevelopmentRecipe localCandidateRecipe = candidate.recipe;
            std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> appliedLocalSuggestions;
            if (ApplyLocalSuggestionsToRecipe(
                    shiftedLocalSuggestions,
                    localCandidateRecipe,
                    appliedLocalSuggestions)) {
                ReplaceStageDiagnostics(
                    candidateStages,
                    BuildPendingBaseLocalCandidateStage(appliedLocalSuggestions.front()));
                std::vector<RawAutoStartPointControl> localControls =
                    candidate.visibleEdits.touchedControls;
                localControls.push_back(RawAutoStartPointControl::LocalRange);
                finishToneCandidateRecipe = localCandidateRecipe;
                finishToneControls = localControls;
                finishToneAfterLocalRange = true;
                candidate.renderRequests.push_back(BuildCandidateRenderRequest(
                    candidate.kind,
                    RawAutoStartPointStage::LocalCandidate,
                    RawAutoStartPointStageStatus::Pending,
                    localCandidateRecipe,
                    std::move(localControls),
                    "Render Base Local Candidate after the proposed visible upstream Starting Point values plus the safe visible Local Range points; use this rendered evidence before graph apply."));
                candidate.visibleEdits.localRangeSummary =
                    "Base queued rendered Local Candidate evidence for " +
                    FormatLocalRangeAppliedSummary(appliedLocalSuggestions.size()) +
                    " before applying Local Range.";
            } else {
                candidate.warnings.push_back(
                    "Base Local Candidate render was not queued because the one-click Local Range graph points could not be authored safely.");
            }
        }
        RawAutoStartPointStageDiagnostics projectedFinishTone;
        RawAutoStartPointStageStatus finishToneRequestReplaces =
            RawAutoStartPointStageStatus::Unavailable;
        if (!finishToneAfterLocalRange &&
            !useSuggestedWhiteBalance &&
            BuildProjectedBaseFinishToneCandidateStage(
                stages,
                currentRecipe.preToneExposureEv,
                candidate.visibleEdits.preToneExposureEv,
                projectedFinishTone)) {
            finishToneRequestReplaces = RawAutoStartPointStageStatus::Projected;
            ReplaceStageDiagnostics(candidateStages, std::move(projectedFinishTone));
        }
        candidate.renderRequests.push_back(BuildCandidateRenderRequest(
            candidate.kind,
            RawAutoStartPointStage::FinishToneCandidate,
            finishToneRequestReplaces,
            finishToneCandidateRecipe,
            finishToneControls,
            finishToneAfterLocalRange
                ? "Render Base pre-display tone after the proposed visible upstream Starting Point values plus the safe visible Local Range graph points."
                : finishToneRequestReplaces == RawAutoStartPointStageStatus::Projected
                ? "Render Base pre-display tone after the proposed visible RAW Exposure value instead of relying on projected current tone evidence."
                : "Render Base pre-display tone after the proposed visible upstream Starting Point values; no complete current Finish Tone Candidate exists to project this candidate."));
        ReplaceStageDiagnostics(
            candidateStages,
            BuildPendingBaseDisplayCandidateStage(
                currentRecipe.preToneExposureEv,
                candidate.visibleEdits.preToneExposureEv));
        std::vector<RawAutoStartPointControl> displayControls = candidate.visibleEdits.touchedControls;
        displayControls.push_back(RawAutoStartPointControl::DisplayFit);
        candidate.renderRequests.push_back(BuildCandidateRenderRequest(
            candidate.kind,
            RawAutoStartPointStage::DisplayCandidate,
            RawAutoStartPointStageStatus::Pending,
            candidate.recipe,
            std::move(displayControls),
            "Render Base Display Candidate after the proposed upstream Starting Point values, then fit View Transform from that post-edit analysis."));
    }

    candidate.visibleEdits.localRangeValid = false;
    if (candidate.visibleEdits.localRangeSummary.empty()) {
        candidate.visibleEdits.localRangeSummary = "Base does not author Local Range points in this pass.";
    }
    candidate.visibleEdits.finishToneValid = false;
    candidate.visibleEdits.finishToneNeutral = true;
    candidate.visibleEdits.finishToneSummary = "Base keeps Finish Tone neutral/unchanged in this pass.";
    candidate.visibleEdits.displayFitValid = upstreamChanged || fitDecision.canApply;
    candidate.visibleEdits.viewTransformSummary = upstreamChanged
        ? "Display Fit will be measured after the proposed upstream Starting Point values render."
        : FormatViewFitSummary(fitDecision.fit);
    if (candidate.visibleEdits.displayFitValid) {
        candidate.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::DisplayFit);
    }
    candidate.stageDiagnostics = std::move(candidateStages);
    if (StageProjected(candidate.stageDiagnostics, RawAutoStartPointStage::RawPlacement)) {
        candidate.warnings.push_back(
            "Base Raw Placement is projected from staged evidence after the proposed RAW Exposure; a true per-candidate render is still unavailable.");
    } else if (!StageComplete(candidate.stageDiagnostics, RawAutoStartPointStage::RawPlacement)) {
        candidate.warnings.push_back(
            "Raw Placement readback is unavailable; Base score uses a current pre-display fallback.");
    }
    if (StageProjected(candidate.stageDiagnostics, RawAutoStartPointStage::FinishToneCandidate)) {
        candidate.warnings.push_back(
            "Base Finish Tone Candidate is projected after the proposed RAW Exposure; a true pre-display candidate render is still unavailable.");
    }
    if (const RawAutoStartPointStageDiagnostics* localStage =
            FindStageDiagnostics(candidate.stageDiagnostics, RawAutoStartPointStage::LocalCandidate);
        localStage != nullptr &&
        localStage->status == RawAutoStartPointStageStatus::Pending) {
        candidate.warnings.push_back(
            "Base Local Candidate is pending until the post-exposure Local Range render completes.");
    }
    if (const RawAutoStartPointStageDiagnostics* displayStage =
            FindStageDiagnostics(candidate.stageDiagnostics, RawAutoStartPointStage::DisplayCandidate);
        displayStage != nullptr &&
        displayStage->status == RawAutoStartPointStageStatus::Pending) {
        candidate.warnings.push_back(
            "Base Display Candidate is pending until the post-edit Display Fit render completes.");
    }
    candidate.score =
        BuildCandidateScore(candidate.kind, candidate.stageDiagnostics, analysis, recommendations, fitDecision);
    return candidate;
}

RawAutoStartPointCandidate BuildBalancedCandidate(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const Stack::RawAutoBase::ViewFitDecision& fitDecision) {
    RawAutoStartPointCandidate candidate;
    candidate.valid = analysis.valid || !stages.empty();
    candidate.kind = RawAutoStartPointCandidateKind::Balanced;
    candidate.id = "balanced-local-tone-dry-run";
    candidate.label = "Balanced Local/Tone";
    candidate.summary =
        "Dry run: conservative Balanced proposal for Local Range and mild Finish Tone; RAW Exposure, WB, and Display Fit stay unchanged.";
    candidate.stageDiagnostics = stages;

    candidate.visibleEdits.rawExposureEvValid = true;
    candidate.visibleEdits.preToneExposureEv = currentRecipe.preToneExposureEv;
    candidate.visibleEdits.whiteBalanceValid = true;
    candidate.visibleEdits.whiteBalancePolicy = "Keep current WB.";

    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> balancedLocal =
        Stack::RawAutoBase::SelectBalancedLocalRangeAdjustments(recommendations);
    candidate.visibleEdits.localRangeValid = !balancedLocal.empty();
    candidate.visibleEdits.localRangePointCount = static_cast<int>(balancedLocal.size());
    candidate.visibleEdits.localRangeSummary =
        FormatBalancedLocalSuggestionSummary(balancedLocal);
    if (!balancedLocal.empty()) {
        candidate.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::LocalRange);
        const bool colorQualified =
            std::any_of(
                balancedLocal.begin(),
                balancedLocal.end(),
                [](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
                    return suggestion.colorQualifierEnabled;
                });
        if (colorQualified) {
            candidate.warnings.push_back(
                "Balanced Local is limited to one color-targeted point because Local Range has one shared color target.");
        }
    } else if (!recommendations.localAdjustments.empty()) {
        candidate.warnings.push_back(
            "Local Range suggestions exist, but none passed the Balanced confidence/delta caps.");
    }

    const RawAutoStartPointFinishToneProposal finishToneProposal =
        BuildMildFinishToneProposalFromStages(currentRecipe, analysis, stages);
    candidate.visibleEdits.finishToneValid = finishToneProposal.valid;
    candidate.visibleEdits.finishToneNeutral = !finishToneProposal.valid;
    candidate.visibleEdits.finishToneSummary = finishToneProposal.summary.empty()
        ? "Balanced keeps Finish Tone neutral/unchanged."
        : finishToneProposal.summary;
    if (finishToneProposal.valid) {
        candidate.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::FinishTone);
    } else {
        candidate.warnings.insert(
            candidate.warnings.end(),
            finishToneProposal.warnings.begin(),
            finishToneProposal.warnings.end());
    }
    candidate.visibleEdits.displayFitValid = false;
    candidate.visibleEdits.viewTransformSummary =
        "Display Fit is unchanged by the Pass 6 Balanced Local/Tone actions.";
    const RawAutoStartPointStageDiagnostics* localCandidateStage =
        FindStageDiagnostics(stages, RawAutoStartPointStage::LocalCandidate);
    if (localCandidateStage == nullptr ||
        localCandidateStage->status == RawAutoStartPointStageStatus::Unavailable) {
        candidate.warnings.push_back(
            "Local Candidate readback is unavailable; Balanced Local uses current local suggestion evidence.");
    } else if (localCandidateStage->status != RawAutoStartPointStageStatus::Complete) {
        candidate.warnings.push_back(
            "Local Candidate has pre-Local-Range fallback evidence; a true post-Local-Range candidate render is still unavailable.");
    }
    candidate.score =
        BuildCandidateScore(candidate.kind, stages, analysis, recommendations, fitDecision);
    return candidate;
}

void AddCandidateUiLines(
    RawAutoStartPointDiagnostics& diagnostics,
    const RawAutoStartPointCandidate& candidate) {
    const std::string candidateLabel =
        CandidateDisplayLabel(candidate);
    const std::string touchedControls = FormatTouchedControls(candidate.visibleEdits);
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        candidateLabel,
        candidate.score.valid ? FormatScore(candidate.score.totalScore) + "/100" : "Unscored",
        candidate.summary
    });
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        candidateLabel + " score components",
        FormatScoreComponentSummary(candidate.score),
        FormatScoreComponentDetail(candidate.score)
    });
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        candidateLabel + " visible controls",
        touchedControls,
        "Visible-control ownership for this candidate only; dry-run diagnostics do not apply recipe values."
    });
    diagnostics.uiView.lines.push_back({
        candidate.renderRequests.empty()
            ? RawAutoStartPointDiagnosticSeverity::Info
            : RawAutoStartPointDiagnosticSeverity::Warning,
        candidateLabel + " render requests",
        FormatCandidateRenderRequestValue(candidate.renderRequests),
        FormatCandidateRenderRequestDetail(candidate.renderRequests)
    });
    std::string warningDetail;
    for (const std::string& warning : candidate.warnings) {
        if (!warningDetail.empty()) {
            warningDetail += " ";
        }
        warningDetail += warning;
    }
    if (warningDetail.empty()) {
        warningDetail = "No candidate-specific warnings were reported for this dry-run candidate.";
    } else {
        warningDetail += " Dry-run diagnostics do not apply recipe values.";
    }
    diagnostics.uiView.lines.push_back({
        candidate.warnings.empty()
            ? RawAutoStartPointDiagnosticSeverity::Info
            : RawAutoStartPointDiagnosticSeverity::Warning,
        candidateLabel + " warnings",
        std::to_string(candidate.warnings.size()),
        warningDetail
    });
    if (candidate.visibleEdits.rawExposureEvValid) {
        diagnostics.uiView.lines.push_back({
            RawAutoStartPointDiagnosticSeverity::Info,
            candidate.label + " RAW Exposure",
            FormatSignedEv(candidate.visibleEdits.preToneExposureEv),
            "Candidate value for visible RawDevelopmentRecipe::preToneExposureEv; not applied."
        });
    }
    if (candidate.visibleEdits.displayFitValid) {
        diagnostics.uiView.lines.push_back({
            RawAutoStartPointDiagnosticSeverity::Info,
            candidate.label + " Display Fit",
            "Proposed",
            candidate.visibleEdits.viewTransformSummary + " Not applied."
        });
    }
    if (!candidate.visibleEdits.localRangeSummary.empty()) {
        const bool balancedLocalMiss =
            candidate.kind == RawAutoStartPointCandidateKind::Balanced &&
            !candidate.visibleEdits.localRangeValid;
        diagnostics.uiView.lines.push_back({
            balancedLocalMiss
                ? RawAutoStartPointDiagnosticSeverity::Warning
                : RawAutoStartPointDiagnosticSeverity::Info,
            candidate.label + " Local Range",
            candidate.visibleEdits.localRangeValid
                ? std::to_string(candidate.visibleEdits.localRangePointCount) + " point(s)"
                : (candidate.kind == RawAutoStartPointCandidateKind::Balanced ? "None" : "Unchanged"),
            candidate.visibleEdits.localRangeSummary + " Dry-run report does not apply recipe values."
        });
    }
    if (!candidate.visibleEdits.finishToneSummary.empty()) {
        const bool balancedToneMiss =
            candidate.kind == RawAutoStartPointCandidateKind::Balanced &&
            !candidate.visibleEdits.finishToneValid;
        diagnostics.uiView.lines.push_back({
            balancedToneMiss
                ? RawAutoStartPointDiagnosticSeverity::Warning
                : RawAutoStartPointDiagnosticSeverity::Info,
            candidate.label + " Finish Tone",
            candidate.visibleEdits.finishToneValid ? "Proposed" : "Unchanged",
            candidate.visibleEdits.finishToneSummary + " Dry-run report does not apply recipe values."
        });
    }
}

void AddCandidateScoreOrderUiLine(RawAutoStartPointDiagnostics& diagnostics) {
    if (diagnostics.candidates.empty()) {
        return;
    }
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Candidate score order",
        FormatCandidateScoreOrderValue(diagnostics.candidates),
        "Sorted from existing dry-run total scores for scanning only; this line does not change candidate scoring, diagnostic selection, action readiness, or recipe values."
    });
}

const RawAutoStartPointCandidate* FindCandidate(
    const std::vector<RawAutoStartPointCandidate>& candidates,
    RawAutoStartPointCandidateKind kind) {
    for (const RawAutoStartPointCandidate& candidate : candidates) {
        if (candidate.kind == kind) {
            return &candidate;
        }
    }
    return nullptr;
}

const RawAutoStartPointCandidate* FindCandidateWithRenderedCompleteStage(
    const std::vector<RawAutoStartPointCandidate>& candidates,
    RawAutoStartPointCandidateKind kind,
    RawAutoStartPointStage stage) {
    for (const RawAutoStartPointCandidate& candidate : candidates) {
        if (candidate.kind == kind && CandidateHasRenderedCompleteStage(candidate, stage)) {
            return &candidate;
        }
    }
    return nullptr;
}

const RawAutoStartPointCandidate* FindCandidateWithRenderedCompleteStageAndRecipe(
    const std::vector<RawAutoStartPointCandidate>& candidates,
    RawAutoStartPointCandidateKind kind,
    RawAutoStartPointStage stage,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    for (const RawAutoStartPointCandidate& candidate : candidates) {
        if (candidate.kind == kind &&
            candidate.hasRecipe &&
            CandidateHasRenderedCompleteStage(candidate, stage) &&
            VisibleUpstreamRecipeMatches(candidate.recipe, recipe)) {
            return &candidate;
        }
    }
    return nullptr;
}

const RawAutoStartPointCandidate* FindCandidateWithStageAndRecipe(
    const std::vector<RawAutoStartPointCandidate>& candidates,
    RawAutoStartPointCandidateKind kind,
    RawAutoStartPointStage stage,
    RawAutoStartPointStageStatus status,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    for (const RawAutoStartPointCandidate& candidate : candidates) {
        const RawAutoStartPointStageDiagnostics* diagnostics =
            FindStageDiagnostics(candidate.stageDiagnostics, stage);
        if (candidate.kind == kind &&
            candidate.hasRecipe &&
            diagnostics != nullptr &&
            diagnostics->status == status &&
            VisibleUpstreamRecipeMatches(candidate.recipe, recipe)) {
            return &candidate;
        }
    }
    return nullptr;
}

RawAutoStartPointCandidate* FindMutableCandidate(
    std::vector<RawAutoStartPointCandidate>& candidates,
    RawAutoStartPointCandidateKind kind) {
    for (RawAutoStartPointCandidate& candidate : candidates) {
        if (candidate.kind == kind) {
            return &candidate;
        }
    }
    return nullptr;
}

bool RequestMatchesVisibleRecipeAndControls(
    const RawAutoStartPointCandidateRenderRequest& request,
    RawAutoStartPointStage stage,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const std::vector<RawAutoStartPointControl>& expectedControls) {
    return request.valid &&
        request.hasRecipe &&
        request.stage == stage &&
        request.expectedControls == expectedControls &&
        VisibleUpstreamRecipeMatches(request.recipe, recipe);
}

bool QueueBaseFinalDisplayCandidateRequestIfNeeded(
    RawAutoStartPointDiagnostics& diagnostics,
    const RawAutoStartPointConservativePlan& plan,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe) {
    if (!plan.valid ||
        !plan.hasUpstreamRecipeChanges ||
        !plan.needsPostApplyDisplayFit) {
        return false;
    }

    if (FindCandidateWithRenderedCompleteStageAndRecipe(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::DisplayCandidate,
            plan.upstreamRecipe) != nullptr) {
        return false;
    }

    RawAutoStartPointCandidate* baseCandidate =
        FindMutableCandidate(diagnostics.candidates, RawAutoStartPointCandidateKind::Base);
    if (baseCandidate == nullptr) {
        return false;
    }

    std::vector<RawAutoStartPointControl> displayControls =
        plan.visibleEdits.touchedControls;
    if (std::find(
            displayControls.begin(),
            displayControls.end(),
            RawAutoStartPointControl::DisplayFit) == displayControls.end()) {
        displayControls.push_back(RawAutoStartPointControl::DisplayFit);
    }

    const auto exactRequest = std::find_if(
        baseCandidate->renderRequests.begin(),
        baseCandidate->renderRequests.end(),
        [&](const RawAutoStartPointCandidateRenderRequest& request) {
            return RequestMatchesVisibleRecipeAndControls(
                request,
                RawAutoStartPointStage::DisplayCandidate,
                plan.upstreamRecipe,
                displayControls);
        });
    if (exactRequest != baseCandidate->renderRequests.end()) {
        return false;
    }

    baseCandidate->renderRequests.erase(
        std::remove_if(
            baseCandidate->renderRequests.begin(),
            baseCandidate->renderRequests.end(),
            [&](const RawAutoStartPointCandidateRenderRequest& request) {
                return request.stage == RawAutoStartPointStage::DisplayCandidate &&
                    !RequestMatchesVisibleRecipeAndControls(
                        request,
                        RawAutoStartPointStage::DisplayCandidate,
                        plan.upstreamRecipe,
                        displayControls);
            }),
        baseCandidate->renderRequests.end());

    const RawAutoStartPointStageDiagnostics* previousDisplayStage =
        FindStageDiagnostics(
            baseCandidate->stageDiagnostics,
            RawAutoStartPointStage::DisplayCandidate);
    const RawAutoStartPointStageStatus replacesStatus =
        previousDisplayStage == nullptr
            ? RawAutoStartPointStageStatus::Unavailable
            : previousDisplayStage->status;

    RawAutoStartPointStageDiagnostics pendingDisplay =
        BuildPendingBaseDisplayCandidateStage(
            currentRecipe.preToneExposureEv,
            plan.visibleEdits.preToneExposureEv);
    pendingDisplay.statusMessage =
        "Base Display Candidate pending: render the full visible Starting Point recipe after staged Local Range or Finish Tone writes, then fit View Transform.";
    pendingDisplay.display.statusMessage = pendingDisplay.statusMessage;
    ReplaceStageDiagnostics(baseCandidate->stageDiagnostics, std::move(pendingDisplay));

    baseCandidate->visibleEdits.displayFitValid = true;
    baseCandidate->visibleEdits.viewTransformSummary =
        "Display Fit will be rendered from the full visible Starting Point recipe.";
    baseCandidate->renderRequests.push_back(BuildCandidateRenderRequest(
        RawAutoStartPointCandidateKind::Base,
        RawAutoStartPointStage::DisplayCandidate,
        replacesStatus,
        plan.upstreamRecipe,
        std::move(displayControls),
        "Render Base Display Candidate after the full visible Starting Point recipe, including rendered Local Range or Finish Tone graph writes, then fit View Transform before applying."));
    AppendUniqueSummary(
        baseCandidate->warnings,
        "Base Display Candidate was re-queued for the full visible Starting Point recipe after staged evidence changed Local Range or Finish Tone.");
    return true;
}

int PartialEvidenceStatusPriority(RawAutoStartPointStageStatus status) {
    switch (status) {
        case RawAutoStartPointStageStatus::Projected: return 4;
        case RawAutoStartPointStageStatus::Fallback: return 3;
        case RawAutoStartPointStageStatus::Pending: return 2;
        case RawAutoStartPointStageStatus::Unavailable: return 1;
        case RawAutoStartPointStageStatus::Complete:
        default:
            return 0;
    }
}

std::string BuildBaseCandidatePartialEvidenceSummary(
    const std::vector<RawAutoStartPointCandidate>& candidates) {
    static constexpr std::array<RawAutoStartPointStage, 4> kBaseEvidenceStages = {
        RawAutoStartPointStage::RawPlacement,
        RawAutoStartPointStage::LocalCandidate,
        RawAutoStartPointStage::FinishToneCandidate,
        RawAutoStartPointStage::DisplayCandidate
    };

    std::vector<std::string> partialParts;
    for (RawAutoStartPointStage stage : kBaseEvidenceStages) {
        bool hasComplete = false;
        bool hasPartial = false;
        RawAutoStartPointStageStatus strongestPartial =
            RawAutoStartPointStageStatus::Unavailable;
        for (const RawAutoStartPointCandidate& candidate : candidates) {
            if (candidate.kind != RawAutoStartPointCandidateKind::Base) {
                continue;
            }
            const RawAutoStartPointStageDiagnostics* diagnostics =
                FindStageDiagnostics(candidate.stageDiagnostics, stage);
            if (diagnostics == nullptr) {
                continue;
            }
            if (diagnostics->status == RawAutoStartPointStageStatus::Complete) {
                hasComplete = true;
                break;
            }
            if (!hasPartial ||
                PartialEvidenceStatusPriority(diagnostics->status) >
                    PartialEvidenceStatusPriority(strongestPartial)) {
                hasPartial = true;
                strongestPartial = diagnostics->status;
            }
        }
        if (!hasComplete && hasPartial) {
            partialParts.push_back(
                std::string(StageLabel(stage)) + " " +
                StageStatusStableString(strongestPartial));
        }
    }

    if (partialParts.empty()) {
        return {};
    }
    return "Base candidate evidence partial: " + JoinCommaSummaryParts(partialParts);
}

void AddVisibleActionScopeUiLine(RawAutoStartPointDiagnostics& diagnostics) {
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Visible action scope",
        "Explicit actions",
        "Build Starting Point writes safe visible controls, including Suggested WB only when camera/as-shot metadata is unavailable and neutral evidence is strong; Add Local Range writes Local Range only; Add Mild Tone writes Finish Tone only. This diagnostics line does not apply recipe values."
    });
}

void AddActionReadinessUiLines(RawAutoStartPointDiagnostics& diagnostics) {
    const RawAutoStartPointCandidate* base =
        FindCandidate(diagnostics.candidates, RawAutoStartPointCandidateKind::Base);
    const RawAutoStartPointCandidate* balanced =
        FindCandidate(diagnostics.candidates, RawAutoStartPointCandidateKind::Balanced);

    const bool baseReady =
        base != nullptr &&
        base->valid &&
        base->visibleEdits.rawExposureEvValid &&
        base->visibleEdits.displayFitValid;
    diagnostics.uiView.lines.push_back({
        baseReady
            ? RawAutoStartPointDiagnosticSeverity::Info
            : RawAutoStartPointDiagnosticSeverity::Warning,
        "Build Starting Point action",
        baseReady ? "Ready" : "Pending",
        baseReady
            ? "Explicit Build Starting Point can write safe visible controls including RAW Exposure, Suggested WB when policy allows, and Display Fit. Dry-run diagnostics do not apply recipe values."
            : "Build Starting Point needs current analysis plus a Base candidate with visible RAW Exposure, optional Suggested WB, and Display Fit values. Dry-run diagnostics do not apply recipe values."
    });

    const bool localReady =
        balanced != nullptr &&
        balanced->visibleEdits.localRangeValid &&
        balanced->visibleEdits.localRangePointCount > 0;
    diagnostics.uiView.lines.push_back({
        localReady
            ? RawAutoStartPointDiagnosticSeverity::Info
            : RawAutoStartPointDiagnosticSeverity::Warning,
        "Add Local Range action",
        localReady
            ? std::to_string(balanced->visibleEdits.localRangePointCount) + " point(s)"
            : "No candidate",
        localReady
            ? balanced->visibleEdits.localRangeSummary +
                  " Explicit action writes visible Local Range points only; dry-run diagnostics do not apply recipe values."
            : (balanced != nullptr && !balanced->visibleEdits.localRangeSummary.empty()
                  ? balanced->visibleEdits.localRangeSummary
                  : std::string("No Balanced Local Range candidate is ready.")) +
                  " Add Local Range should remain unavailable until a visible Local Range candidate exists."
    });

    const bool toneReady =
        balanced != nullptr &&
        balanced->visibleEdits.finishToneValid;
    diagnostics.uiView.lines.push_back({
        toneReady
            ? RawAutoStartPointDiagnosticSeverity::Info
            : RawAutoStartPointDiagnosticSeverity::Warning,
        "Add Mild Tone action",
        toneReady ? "Ready" : "No candidate",
        toneReady
            ? balanced->visibleEdits.finishToneSummary +
                  " Explicit action writes visible Finish Tone graph points only; dry-run diagnostics do not apply recipe values."
            : (balanced != nullptr && !balanced->visibleEdits.finishToneSummary.empty()
                  ? balanced->visibleEdits.finishToneSummary
                  : std::string("No mild Finish Tone candidate is ready.")) +
                  " Add Mild Tone should remain unavailable until a visible Finish Tone candidate exists."
    });
}

void AddDiagnosticsSourceLine(
    RawAutoStartPointDiagnosticsView& view,
    const std::string& sourceKey) {
    if (sourceKey.empty()) {
        return;
    }
    view.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Source",
        sourceKey,
        "Starting Point evidence and candidate summaries are scoped to this RAW source."
    });
}

bool IsBaseEvidenceAvailabilityWarning(const std::string& warning) {
    return warning.find("Base Raw Placement is projected") != std::string::npos ||
        warning.find("Raw Placement readback is unavailable") != std::string::npos ||
        warning.find("Base Finish Tone Candidate is projected") != std::string::npos ||
        warning.find("Base Local Candidate is pending") != std::string::npos ||
        warning.find("Base Display Candidate is pending") != std::string::npos;
}

void RefreshBaseCandidateEvidenceWarnings(RawAutoStartPointCandidate& candidate) {
    if (candidate.kind != RawAutoStartPointCandidateKind::Base) {
        return;
    }

    candidate.warnings.erase(
        std::remove_if(
            candidate.warnings.begin(),
            candidate.warnings.end(),
            IsBaseEvidenceAvailabilityWarning),
        candidate.warnings.end());

    if (StageProjected(candidate.stageDiagnostics, RawAutoStartPointStage::RawPlacement)) {
        candidate.warnings.push_back(
            "Base Raw Placement is projected from staged evidence after the proposed RAW Exposure; a true per-candidate render is still unavailable.");
    } else if (!StageComplete(candidate.stageDiagnostics, RawAutoStartPointStage::RawPlacement)) {
        candidate.warnings.push_back(
            "Raw Placement readback is unavailable; Base score uses a current pre-display fallback.");
    }
    if (StageProjected(candidate.stageDiagnostics, RawAutoStartPointStage::FinishToneCandidate)) {
        candidate.warnings.push_back(
            "Base Finish Tone Candidate is projected after the proposed RAW Exposure; a true pre-display candidate render is still unavailable.");
    }
    if (const RawAutoStartPointStageDiagnostics* localStage =
            FindStageDiagnostics(candidate.stageDiagnostics, RawAutoStartPointStage::LocalCandidate);
        localStage != nullptr &&
        localStage->status == RawAutoStartPointStageStatus::Pending) {
        candidate.warnings.push_back(
            "Base Local Candidate is pending until the post-exposure Local Range render completes.");
    }
    if (const RawAutoStartPointStageDiagnostics* displayStage =
            FindStageDiagnostics(candidate.stageDiagnostics, RawAutoStartPointStage::DisplayCandidate);
        displayStage != nullptr &&
        displayStage->status == RawAutoStartPointStageStatus::Pending) {
        candidate.warnings.push_back(
            "Base Display Candidate is pending until the post-edit Display Fit render completes.");
    }
}

void RefreshDryRunEvidenceWarnings(
    RawAutoStartPointDiagnostics& diagnostics,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages) {
    diagnostics.warnings.clear();
    if (!StageComplete(stages, RawAutoStartPointStage::RawPlacement) &&
        !AnyCandidateStageComplete(diagnostics.candidates, RawAutoStartPointStage::RawPlacement)) {
        diagnostics.warnings.push_back(
            "Raw Placement readback is not captured yet, so Base scoring uses fallback evidence.");
    }
    if (!StageComplete(stages, RawAutoStartPointStage::NeutralScene)) {
        diagnostics.warnings.push_back(
            "Neutral Scene readback is not captured yet; WB and RAW Exposure evidence remains partial.");
    }
}

void RefreshSelectedCandidate(RawAutoStartPointDiagnostics& diagnostics) {
    diagnostics.hasSelectedCandidate = false;
    diagnostics.selectedCandidateIndex = -1;
    float bestScore = -1.0f;
    for (std::size_t i = 0; i < diagnostics.candidates.size(); ++i) {
        const RawAutoStartPointCandidate& candidate = diagnostics.candidates[i];
        if (!candidate.score.valid || candidate.score.totalScore <= bestScore) {
            continue;
        }
        bestScore = candidate.score.totalScore;
        diagnostics.hasSelectedCandidate = true;
        diagnostics.selectedCandidateIndex = static_cast<int>(i);
        diagnostics.selectedCandidateKind = candidate.kind;
    }
}

void RebuildDryRunDiagnosticsUi(
    RawAutoStartPointDiagnostics& diagnostics,
    const std::vector<RawAutoStartPointStageDiagnostics>& stages) {
    diagnostics.uiView.title = "Build Starting Point Dry Run";
    diagnostics.uiView.summary =
        "Dry-run CurrentFit/Base/Balanced Local/Tone candidate report. The report itself does not write recipe values.";
    diagnostics.uiView.lines.clear();
    AddDiagnosticsSourceLine(diagnostics.uiView, diagnostics.sourceKey);
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Dry run",
        "Yes",
        "Candidate diagnostics compute values and scores; the report itself does not write recipes."
    });
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Recipe writes",
        "None",
        "Build Starting Point, Local Range, and Mild Tone write visible recipe controls only through explicit UI actions."
    });
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Stage evidence",
        std::to_string(stages.size()),
        "Score terms name whether they used Raw Technical, Raw Placement, Local Candidate, Finish Tone Candidate, Display Candidate, rendered, projected, or fallback evidence."
    });
    AddCandidateScoreOrderUiLine(diagnostics);
    AddVisibleActionScopeUiLine(diagnostics);
    AddActionReadinessUiLines(diagnostics);
    for (const RawAutoStartPointCandidate& candidate : diagnostics.candidates) {
        AddCandidateUiLines(diagnostics, candidate);
    }
    if (diagnostics.hasSelectedCandidate &&
        diagnostics.selectedCandidateIndex >= 0 &&
        diagnostics.selectedCandidateIndex < static_cast<int>(diagnostics.candidates.size())) {
        const RawAutoStartPointCandidate& selected =
            diagnostics.candidates[static_cast<std::size_t>(diagnostics.selectedCandidateIndex)];
        std::string selectionDetail =
            "Selection is informational only; appliedRecipeValues remains false.";
        if (selected.score.valid) {
            selectionDetail += " Selected score " +
                FormatScore(selected.score.totalScore) + "/100.";
        }
        if (!selected.score.summary.empty()) {
            selectionDetail += " " + selected.score.summary;
        }
        selectionDetail += " Visible controls: " +
            FormatTouchedControls(selected.visibleEdits) + ".";
        diagnostics.uiView.lines.push_back({
            RawAutoStartPointDiagnosticSeverity::Info,
            "Diagnostic selection",
            selected.label.empty() ? CandidateKindLabel(selected.kind) : selected.label,
            selectionDetail
        });
    }
    for (const std::string& warning : diagnostics.warnings) {
        diagnostics.uiView.lines.push_back({
            RawAutoStartPointDiagnosticSeverity::Warning,
            "Partial evidence",
            "Fallback",
            warning
        });
    }
}

} // namespace

RawAutoStartPointFinishToneProposal BuildMildFinishToneProposal(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const RawAutoStartPointDiagnostics& diagnostics) {
    return BuildMildFinishToneProposalFromStages(
        currentRecipe,
        analysis,
        ExtractStageDiagnostics(diagnostics));
}

RawAutoStartPointConservativePlan BuildConservativeStartingPointPlan(
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations,
    const RawAutoStartPointDiagnostics& diagnostics) {
    RawAutoStartPointConservativePlan plan;
    plan.upstreamRecipe = currentRecipe;
    plan.hasAnalysis = analysis.valid && analysis.currentFrameStats.valid;
    if (!plan.hasAnalysis) {
        plan.summary = "Build Starting Point needs a RAW preview analysis before applying visible controls.";
        plan.withheldSummaries.push_back("Preview analysis pending");
        plan.evidenceSummaries.push_back("Preview analysis pending");
        return plan;
    }

    plan.valid = true;
    plan.displayFitRecommended = true;
    plan.visibleEdits.rawExposureEvValid = true;
    plan.visibleEdits.preToneExposureEv = currentRecipe.preToneExposureEv;
    plan.visibleEdits.whiteBalanceValid = true;
    plan.visibleEdits.whiteBalancePolicy = ResolveWhiteBalancePolicy(recommendations.whiteBalance);
    plan.visibleEdits.displayFitValid = true;
    plan.visibleEdits.viewTransformSummary =
        "Display Fit will be refreshed after upstream Starting Point edits settle.";

    bool whiteBalanceChangedInPlan = false;
    const Stack::RawAutoBase::WhiteBalanceRecommendation& whiteBalance =
        recommendations.whiteBalance;
    if (whiteBalance.valid &&
        whiteBalance.autoApplyAllowed &&
        whiteBalance.alternateCandidateAvailable &&
        !whiteBalance.cameraWhiteBalanceAvailable &&
        !whiteBalance.manualWhiteBalanceProtected) {
        Stack::RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(
            plan.upstreamRecipe,
            whiteBalance);
        plan.visibleEdits.whiteBalancePolicy = ResolveWhiteBalancePolicy(whiteBalance);
        plan.visibleEdits.whiteBalanceMultipliers = {
            whiteBalance.gainsR,
            whiteBalance.gainsG,
            whiteBalance.gainsB
        };
        plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::WhiteBalance);
        plan.appliedSummaries.push_back(
            std::string("White Balance ") + WhiteBalanceMethodLabel(whiteBalance.method));
        AppendUniqueSummary(
            plan.evidenceSummaries,
            "Suggested WB from strong neutral estimate");
        plan.hasUpstreamRecipeChanges = true;
        whiteBalanceChangedInPlan = true;
    } else if (whiteBalance.manualWhiteBalanceProtected) {
        plan.withheldSummaries.push_back("White Balance unchanged: manual/custom WB protected");
    } else if (whiteBalance.cameraWhiteBalanceAvailable) {
        plan.withheldSummaries.push_back("White Balance unchanged: camera/as-shot WB kept");
    } else if (whiteBalance.alternateCandidateAvailable) {
        plan.withheldSummaries.push_back(
            "White Balance suggestion kept manual: confidence below auto-apply policy");
    } else {
        plan.withheldSummaries.push_back("White Balance unchanged: no reliable neutral estimate");
    }

    bool rawExposureChangedInPlan = false;
    bool localRangeChangedInPlan = false;
    bool finishToneChangedInPlan = false;
    Stack::RawAutoBase::RawExposureRecommendation exposure =
        BuildStartingPointRawExposureRecommendation(
            currentRecipe,
            ExtractStageDiagnostics(diagnostics),
            recommendations.exposure);
    const std::vector<RawAutoStartPointStageDiagnostics> extractedStages =
        ExtractStageDiagnostics(diagnostics);
    const float darkRevealScore = ComputeDarkRevealScore(analysis, extractedStages);
    const bool darkRevealMode = darkRevealScore >= kStartingPointDarkRevealGate;
    if (darkRevealMode) {
        AppendUniqueSummary(
            plan.evidenceSummaries,
            "Dark reveal mode: underexposed scene percentiles allow stronger editable shadow controls");
        AppendUniqueSummary(
            plan.warnings,
            "Build Starting Point is prioritizing shadow visibility for this dark RAW; visible noise may increase and can be adjusted with the authored controls.");
    }
    if (FindCompleteRawSafetyStats(extractedStages) != nullptr) {
        AppendUniqueSummary(plan.evidenceSummaries, "Raw Technical safety ledger available");
    }
    if (StageComplete(extractedStages, RawAutoStartPointStage::NeutralScene)) {
        AppendUniqueSummary(plan.evidenceSummaries, "Neutral Scene evidence available");
    }
    const std::string baseCandidatePartialEvidenceSummary =
        BuildBaseCandidatePartialEvidenceSummary(diagnostics.candidates);
    if (!baseCandidatePartialEvidenceSummary.empty()) {
        AppendUniqueSummary(plan.evidenceSummaries, baseCandidatePartialEvidenceSummary);
    }
    const RawAutoStartPointCandidate* anyRenderedBaseRawPlacementCandidate =
        FindCandidateWithRenderedCompleteStage(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::RawPlacement);
    const RawAutoStartPointCandidate* renderedBaseRawPlacementCandidate =
        whiteBalanceChangedInPlan
            ? FindCandidateWithRenderedCompleteStageAndRecipe(
                  diagnostics.candidates,
                  RawAutoStartPointCandidateKind::Base,
                  RawAutoStartPointStage::RawPlacement,
                  plan.upstreamRecipe)
            : anyRenderedBaseRawPlacementCandidate;
    const RawAutoStartPointStageDiagnostics* renderedBaseRawPlacementStage =
        renderedBaseRawPlacementCandidate == nullptr
            ? nullptr
            : FindStageDiagnostics(
                  renderedBaseRawPlacementCandidate->stageDiagnostics,
                  RawAutoStartPointStage::RawPlacement);
    const bool renderedPostWhiteBalanceRawPlacementRecipeMismatch =
        whiteBalanceChangedInPlan &&
        anyRenderedBaseRawPlacementCandidate != nullptr &&
        renderedBaseRawPlacementCandidate == nullptr;
    bool rawExposurePostWhiteBalanceWithheld = false;
    bool rawExposureUsedRenderedPostWhiteBalanceEvidence = false;
    if (whiteBalanceChangedInPlan &&
        exposure.valid &&
        !exposure.blockedByHighlightRisk) {
        Stack::RawAutoBase::RawExposureRecommendation postWhiteBalanceExposure;
        std::string withheldSummary;
        std::string warning;
        if (BuildRenderedPostWhiteBalanceRawExposureRecommendation(
                currentRecipe,
                extractedStages,
                renderedBaseRawPlacementStage,
                exposure,
                postWhiteBalanceExposure,
                withheldSummary,
                warning)) {
            exposure = postWhiteBalanceExposure;
            rawExposureUsedRenderedPostWhiteBalanceEvidence = true;
        } else {
            if (!withheldSummary.empty()) {
                plan.withheldSummaries.push_back(withheldSummary);
            }
            if (!warning.empty()) {
                plan.warnings.push_back(warning);
            }
            if (renderedPostWhiteBalanceRawPlacementRecipeMismatch) {
                AppendUniqueSummary(
                    plan.warnings,
                    "Rendered post-WB Raw Placement evidence was withheld because its visible recipe did not match the selected Suggested WB values.");
            }
            rawExposurePostWhiteBalanceWithheld = true;
        }
    }
    if (!rawExposurePostWhiteBalanceWithheld &&
        exposure.valid &&
        exposure.autoApplyAllowed &&
        !exposure.blockedByHighlightRisk &&
        std::isfinite(exposure.suggestedEv)) {
        const float targetEv = std::clamp(exposure.suggestedEv, -8.0f, 8.0f);
        if (std::abs(targetEv - currentRecipe.preToneExposureEv) > 0.001f) {
            plan.upstreamRecipe.preToneExposureEv = targetEv;
            plan.visibleEdits.preToneExposureEv = targetEv;
            plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::RawExposure);
            plan.appliedSummaries.push_back("RAW Exposure " + FormatSignedEv(targetEv));
            AppendUniqueSummary(
                plan.evidenceSummaries,
                rawExposureUsedRenderedPostWhiteBalanceEvidence
                    ? "RAW Exposure from rendered post-WB Raw Placement"
                    : "RAW Exposure from conservative exposure evidence");
            if (exposure.rationale.find("remains manual") != std::string::npos) {
                const bool rawExposureLowered = targetEv < currentRecipe.preToneExposureEv;
                const bool postWhiteBalance =
                    exposure.rationale.find("post-WB") != std::string::npos;
                const bool tinyPositive =
                    exposure.rationale.find("tiny positive") != std::string::npos;
                const bool cautious =
                    exposure.rationale.find("cautious") != std::string::npos;
                std::string withheldSummary;
                if (rawExposureLowered) {
                    withheldSummary = postWhiteBalance
                        ? "Additional RAW Exposure lowering withheld: small downward post-WB one-click cap"
                        : "Additional RAW Exposure lowering withheld: small downward one-click cap";
                } else if (tinyPositive) {
                    withheldSummary = postWhiteBalance
                        ? "Additional RAW Exposure lift withheld: tiny positive post-WB one-click cap"
                        : "Additional RAW Exposure lift withheld: tiny positive one-click cap";
                } else if (cautious) {
                    withheldSummary = postWhiteBalance
                        ? "Additional RAW Exposure lift withheld: cautious post-WB 0.25 EV cap"
                        : "Additional RAW Exposure lift withheld: cautious 0.25 EV cap";
                } else {
                    withheldSummary = postWhiteBalance
                        ? "Additional RAW Exposure lift withheld: post-WB one-click cap"
                        : "Additional RAW Exposure lift withheld: one-click cap";
                }
                plan.withheldSummaries.push_back(std::move(withheldSummary));
            }
            plan.hasUpstreamRecipeChanges = true;
            rawExposureChangedInPlan = true;
        } else {
            plan.withheldSummaries.push_back("RAW Exposure already near target");
        }
    } else if (!rawExposurePostWhiteBalanceWithheld &&
        exposure.valid &&
        exposure.blockedByHighlightRisk) {
        plan.withheldSummaries.push_back("RAW Exposure withheld: highlight risk");
        plan.warnings.push_back("Positive RAW Exposure was withheld because highlight safety blocks the lift.");
    } else if (!rawExposurePostWhiteBalanceWithheld &&
        exposure.valid &&
        exposure.rationale.find("Raw Technical WB-scaled headroom") != std::string::npos) {
        plan.withheldSummaries.push_back("RAW Exposure withheld: Raw Technical headroom");
        plan.warnings.push_back(exposure.rationale);
    } else if (!rawExposurePostWhiteBalanceWithheld &&
        exposure.valid &&
        exposure.rationale.find("Neutral Scene placement disagrees") != std::string::npos) {
        plan.withheldSummaries.push_back("RAW Exposure withheld: Neutral Scene disagreement");
        plan.warnings.push_back(exposure.rationale);
    } else if (!rawExposurePostWhiteBalanceWithheld && exposure.valid) {
        const float proposedDeltaEv =
            std::isfinite(exposure.suggestedEv)
                ? exposure.suggestedEv - currentRecipe.preToneExposureEv
                : exposure.deltaEv;
        const float proposedTargetEv =
            std::isfinite(exposure.suggestedEv)
                ? exposure.suggestedEv
                : currentRecipe.preToneExposureEv + (std::isfinite(proposedDeltaEv) ? proposedDeltaEv : 0.0f);
        const float displayDeltaEv = std::isfinite(proposedDeltaEv) ? proposedDeltaEv : 0.0f;
        std::vector<std::string> manualReasons;
        if (std::isfinite(exposure.confidence) &&
            exposure.confidence < kStartingPointRawExposureCautiousConfidenceGate) {
            const bool proposedLowering =
                std::isfinite(proposedDeltaEv) && proposedDeltaEv < 0.0f;
            manualReasons.push_back(
                proposedLowering
                    ? "confidence " + FormatConfidencePercent(exposure.confidence) +
                        " below 35% downward gate"
                    : "confidence " + FormatConfidencePercent(exposure.confidence) +
                        " below 35% tiny-lift gate");
        }
        if (std::isfinite(proposedDeltaEv) &&
            exposure.confidence >= kStartingPointRawExposureCautiousConfidenceGate &&
            exposure.confidence < kStartingPointRawExposureConfidenceGate &&
            std::abs(proposedDeltaEv) > kStartingPointRawExposureCautiousCapEv) {
            manualReasons.push_back(
                "medium-confidence delta " + FormatSignedEv(proposedDeltaEv) + " exceeds the 0.25 EV cautious cap");
        } else if (std::isfinite(proposedDeltaEv) &&
            std::abs(proposedDeltaEv) > kStartingPointRawExposureOneClickCapEv) {
            manualReasons.push_back(
                "delta " + FormatSignedEv(proposedDeltaEv) + " exceeds the 0.50 EV one-click cap");
        }
        if (!exposure.autoApplyAllowed && manualReasons.empty()) {
            manualReasons.push_back("one-click policy kept it manual");
        }
        std::ostringstream manualSummary;
        manualSummary << "RAW Exposure suggestion kept manual: target "
            << FormatSignedEv(proposedTargetEv)
            << ", delta " << FormatSignedEv(displayDeltaEv)
            << ", confidence " << FormatConfidencePercent(exposure.confidence);
        const std::string joinedReasons = JoinCommaSummaryParts(manualReasons);
        if (!joinedReasons.empty()) {
            manualSummary << " (" << joinedReasons << ")";
        }
        plan.withheldSummaries.push_back(manualSummary.str());
        if (!exposure.rationale.empty()) {
            AppendUniqueSummary(plan.warnings, exposure.rationale);
        }
    } else if (!rawExposurePostWhiteBalanceWithheld) {
        plan.withheldSummaries.push_back(
            exposure.rationale.empty()
                ? "RAW Exposure unchanged: no one-click proposal"
                : "RAW Exposure unchanged: " + exposure.rationale);
    }

    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> localSuggestionsForPlan =
        recommendations.localAdjustments;
    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> oneClickLocalCandidates =
        SelectOneClickLocalSuggestions(localSuggestionsForPlan, 2, darkRevealScore);
    if (oneClickLocalCandidates.empty()) {
        Stack::RawAutoBase::SuggestedLocalAdjustment fallback =
            BuildWideRangeFallbackLocalSuggestion(extractedStages, analysis);
        if (fallback.valid) {
            localSuggestionsForPlan.push_back(std::move(fallback));
            oneClickLocalCandidates = SelectOneClickLocalSuggestions(
                localSuggestionsForPlan,
                2,
                darkRevealScore);
        }
    }
    const bool oneClickLocalUsesCautiousGate =
        std::any_of(
            oneClickLocalCandidates.begin(),
            oneClickLocalCandidates.end(),
            [](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
                return !LocalSuggestionPassesStrictOneClickGate(suggestion);
            });
    const bool oneClickLocalUsesDarkRevealGate =
        darkRevealMode &&
        std::any_of(
            oneClickLocalCandidates.begin(),
            oneClickLocalCandidates.end(),
            [](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
                return LocalSuggestionPassesDarkRevealOneClickGate(suggestion) &&
                    !LocalSuggestionPassesCautiousOneClickGate(suggestion);
            });
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> shiftedOneClickLocalForPlan =
        ShiftLocalSuggestionsForUpstreamExposure(
            oneClickLocalCandidates,
            currentRecipe,
            plan.upstreamRecipe,
            rawExposureChangedInPlan && !whiteBalanceChangedInPlan);
    Stack::RawRecipe::RawDevelopmentRecipe oneClickLocalRecipe = plan.upstreamRecipe;
    std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment> oneClickLocalForPlan;
    const bool oneClickLocalRecipeReady =
        !shiftedOneClickLocalForPlan.empty() &&
        ApplyLocalSuggestionsToRecipe(
            shiftedOneClickLocalForPlan,
            oneClickLocalRecipe,
            oneClickLocalForPlan);
    const RawAutoStartPointCandidate* anyRenderedBaseLocalCandidate =
        FindCandidateWithRenderedCompleteStage(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::LocalCandidate);
    const RawAutoStartPointCandidate* renderedBaseLocalCandidate =
        oneClickLocalRecipeReady && (rawExposureChangedInPlan || whiteBalanceChangedInPlan)
            ? FindCandidateWithRenderedCompleteStageAndRecipe(
                  diagnostics.candidates,
                  RawAutoStartPointCandidateKind::Base,
                  RawAutoStartPointStage::LocalCandidate,
                  oneClickLocalRecipe)
            : nullptr;
    const RawAutoStartPointStageDiagnostics* renderedBaseLocalStage =
        renderedBaseLocalCandidate == nullptr
            ? nullptr
            : FindStageDiagnostics(
                  renderedBaseLocalCandidate->stageDiagnostics,
                  RawAutoStartPointStage::LocalCandidate);
    const bool renderedBaseLocalEvidenceValid =
        renderedBaseLocalStage != nullptr &&
        renderedBaseLocalStage->scene.valid;
    const bool renderedPostExposureLocalEvidenceReady =
        rawExposureChangedInPlan &&
        !whiteBalanceChangedInPlan &&
        renderedBaseLocalEvidenceValid;
    const bool renderedPostWhiteBalanceLocalEvidenceReady =
        whiteBalanceChangedInPlan &&
        !rawExposureChangedInPlan &&
        renderedBaseLocalEvidenceValid;
    const bool renderedPostWhiteBalanceExposureLocalEvidenceReady =
        whiteBalanceChangedInPlan &&
        rawExposureChangedInPlan &&
        renderedBaseLocalEvidenceValid;
    const bool renderedPostUpstreamLocalEvidenceReady =
        renderedPostExposureLocalEvidenceReady ||
        renderedPostWhiteBalanceLocalEvidenceReady ||
        renderedPostWhiteBalanceExposureLocalEvidenceReady;
    const bool renderedLocalEvidenceRecipeMismatch =
        oneClickLocalRecipeReady &&
        (rawExposureChangedInPlan || whiteBalanceChangedInPlan) &&
        anyRenderedBaseLocalCandidate != nullptr &&
        renderedBaseLocalCandidate == nullptr;
    if (!oneClickLocalCandidates.empty() &&
        (rawExposureChangedInPlan || whiteBalanceChangedInPlan)) {
        if (!oneClickLocalRecipeReady) {
            plan.withheldSummaries.push_back(
                "Local Range withheld: graph points could not be authored safely");
            plan.warnings.push_back(
                "One-click Local Range candidates existed, but they overlapped the current graph or exceeded graph capacity.");
        } else if (renderedPostUpstreamLocalEvidenceReady) {
            plan.upstreamRecipe = std::move(oneClickLocalRecipe);
            plan.visibleEdits.localRangeValid = true;
            plan.visibleEdits.localRangePointCount =
                static_cast<int>(oneClickLocalForPlan.size());
            plan.visibleEdits.localRangeSummary =
                FormatBalancedLocalSuggestionSummary(oneClickLocalForPlan);
            plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::LocalRange);
            plan.appliedSummaries.push_back(FormatLocalRangeAppliedSummary(oneClickLocalForPlan.size()));
            AppendUniqueSummary(
                plan.evidenceSummaries,
                oneClickLocalUsesDarkRevealGate
                    ? "Local Range from exact rendered Local Candidate with dark-reveal gate"
                    : oneClickLocalUsesCautiousGate
                    ? "Local Range from exact rendered Local Candidate with cautious one-click gate"
                    : "Local Range from exact rendered Local Candidate");
            plan.hasUpstreamRecipeChanges = true;
            localRangeChangedInPlan = true;
        } else if (rawExposureChangedInPlan && !whiteBalanceChangedInPlan) {
            plan.upstreamRecipe = std::move(oneClickLocalRecipe);
            plan.visibleEdits.localRangeValid = true;
            plan.visibleEdits.localRangePointCount =
                static_cast<int>(oneClickLocalForPlan.size());
            plan.visibleEdits.localRangeSummary =
                FormatBalancedLocalSuggestionSummary(oneClickLocalForPlan);
            plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::LocalRange);
            plan.appliedSummaries.push_back(FormatLocalRangeAppliedSummary(oneClickLocalForPlan.size()));
            AppendUniqueSummary(
                plan.evidenceSummaries,
                oneClickLocalUsesDarkRevealGate
                    ? "Local Range from dark-reveal current evidence shifted by RAW Exposure"
                    : oneClickLocalUsesCautiousGate
                    ? "Local Range from cautious current evidence shifted by RAW Exposure"
                    : "Local Range from strict current evidence shifted by RAW Exposure");
            plan.hasUpstreamRecipeChanges = true;
            localRangeChangedInPlan = true;
        } else if (whiteBalanceChangedInPlan && rawExposureChangedInPlan) {
            plan.withheldSummaries.push_back(
                "Local Range pending: upstream RAW Exposure/WB changed before post-upstream Local Candidate evidence");
            AppendUniqueSummary(
                plan.evidenceSummaries,
                "Local Range awaiting matching rendered Local Candidate evidence");
            if (renderedLocalEvidenceRecipeMismatch) {
                AppendUniqueSummary(
                    plan.warnings,
                    "Rendered Local Candidate evidence was withheld because its visible recipe did not match the selected WB, RAW Exposure, and Local Range values.");
            }
            plan.warnings.push_back(
                "One-click Local Range suggestions were withheld because they were measured before the applied Suggested WB and RAW Exposure values.");
        } else if (whiteBalanceChangedInPlan) {
            plan.withheldSummaries.push_back(
                "Local Range pending: White Balance changed before post-WB Local Candidate evidence");
            AppendUniqueSummary(
                plan.evidenceSummaries,
                "Local Range awaiting matching rendered Local Candidate evidence");
            if (renderedLocalEvidenceRecipeMismatch) {
                AppendUniqueSummary(
                    plan.warnings,
                    "Rendered Local Candidate evidence was withheld because its visible recipe did not match the selected WB plus Local Range values.");
            }
            plan.warnings.push_back(
                "One-click Local Range suggestions were withheld because they were measured before the applied Suggested WB multipliers.");
        } else {
            plan.withheldSummaries.push_back(
                "Local Range pending: no rendered post-exposure Local Candidate evidence yet");
            AppendUniqueSummary(
                plan.evidenceSummaries,
                "Local Range awaiting matching rendered Local Candidate evidence");
            if (renderedLocalEvidenceRecipeMismatch) {
                AppendUniqueSummary(
                    plan.warnings,
                    "Rendered Local Candidate evidence was withheld because its visible recipe did not match the selected RAW Exposure plus Local Range values.");
            }
            plan.warnings.push_back(
                "One-click Local Range suggestions were withheld because they were measured before the applied RAW Exposure change; no rendered post-exposure Local Candidate evidence is available yet.");
        }
    } else if (!oneClickLocalCandidates.empty()) {
        if (oneClickLocalRecipeReady) {
            plan.upstreamRecipe = std::move(oneClickLocalRecipe);
            plan.visibleEdits.localRangeValid = true;
            plan.visibleEdits.localRangePointCount =
                static_cast<int>(oneClickLocalForPlan.size());
            plan.visibleEdits.localRangeSummary =
                FormatBalancedLocalSuggestionSummary(oneClickLocalForPlan);
            plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::LocalRange);
            plan.appliedSummaries.push_back(FormatLocalRangeAppliedSummary(oneClickLocalForPlan.size()));
            AppendUniqueSummary(
                plan.evidenceSummaries,
                oneClickLocalUsesDarkRevealGate
                    ? "Local Range from dark-reveal current Local Candidate evidence"
                    : oneClickLocalUsesCautiousGate
                    ? "Local Range from cautious current Local Candidate evidence"
                    : "Local Range from current Local Candidate evidence");
            plan.hasUpstreamRecipeChanges = true;
            localRangeChangedInPlan = true;
        } else {
            plan.withheldSummaries.push_back("Local Range withheld: graph points could not be authored safely");
            plan.warnings.push_back("One-click Local Range candidates existed, but they overlapped the current graph or exceeded graph capacity.");
        }
    } else if (!recommendations.localAdjustments.empty()) {
        plan.withheldSummaries.push_back(
            darkRevealMode
                ? "Local Range withheld: dark reveal found suggestions, but no shadow/subject lift passed the 45% confidence / 1.15 EV gate"
                : "Local Range withheld: no suggestion passed the 50% contextual / 0.45 EV or 70% / 0.60 EV one-click gates");
    } else {
        plan.withheldSummaries.push_back("Local Range unchanged");
    }

    const RawAutoStartPointCandidate* renderedBaseDisplayFitCandidate =
        FindCandidateWithRenderedCompleteStageAndRecipe(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::DisplayCandidate,
            plan.upstreamRecipe);
    const bool renderedBaseDisplayFitAvailable =
        rawExposureChangedInPlan &&
        renderedBaseDisplayFitCandidate != nullptr &&
        renderedBaseDisplayFitCandidate->hasRecipe &&
        renderedBaseDisplayFitCandidate->recipe.viewTransform.layerJson.is_object();
    const RawAutoStartPointCandidate* anyRenderedBaseFinishToneCandidate =
        FindCandidateWithRenderedCompleteStage(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::FinishToneCandidate);
    const RawAutoStartPointCandidate* renderedBaseFinishToneCandidate =
        FindCandidateWithRenderedCompleteStageAndRecipe(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::FinishToneCandidate,
            plan.upstreamRecipe);
    const bool renderedBaseFinishToneEvidenceReady =
        plan.hasUpstreamRecipeChanges &&
        renderedBaseFinishToneCandidate != nullptr;
    const RawAutoStartPointCandidate* projectedBaseFinishToneCandidate =
        (!renderedBaseFinishToneEvidenceReady &&
         rawExposureChangedInPlan &&
         !whiteBalanceChangedInPlan &&
         !localRangeChangedInPlan)
            ? FindCandidateWithStageAndRecipe(
                diagnostics.candidates,
                RawAutoStartPointCandidateKind::Base,
                RawAutoStartPointStage::FinishToneCandidate,
                RawAutoStartPointStageStatus::Projected,
                plan.upstreamRecipe)
            : nullptr;
    const bool projectedBaseFinishToneEvidenceReady =
        projectedBaseFinishToneCandidate != nullptr;
    const RawAutoStartPointFinishToneProposal toneProposal =
        renderedBaseFinishToneEvidenceReady
            ? BuildMildFinishToneProposalFromStages(
                currentRecipe,
                analysis,
                renderedBaseFinishToneCandidate->stageDiagnostics)
            : projectedBaseFinishToneEvidenceReady
                ? BuildMildFinishToneProposalFromStages(
                    currentRecipe,
                    analysis,
                    projectedBaseFinishToneCandidate->stageDiagnostics)
            : BuildMildFinishToneProposal(currentRecipe, analysis, diagnostics);
    const bool darkRevealToneSafe =
        toneProposal.shadowReveal &&
        (!analysis.highlight.valid || !analysis.highlight.severeSensorClip);
    const bool dynamicRangeToneSafe =
        toneProposal.dynamicRangeBalance;
    const bool toneHighlightSafe =
        darkRevealToneSafe ||
        dynamicRangeToneSafe ||
        !analysis.highlight.valid ||
        (analysis.highlight.displayClipPercent <= 1.0f &&
         analysis.highlight.hdrPixelPercent <= 4.0f);
    const bool upstreamChangedBeforeTone = plan.hasUpstreamRecipeChanges;
    const bool finishToneUsesDarkRevealGate =
        toneProposal.valid && toneProposal.shadowReveal;
    const bool finishToneUsesDynamicRangeGate =
        toneProposal.valid && toneProposal.dynamicRangeBalance;
    const bool finishToneUsesCautiousGate =
        toneProposal.valid &&
        !toneProposal.shadowReveal &&
        toneProposal.strength > kStartingPointFinishToneStrictStrengthLimit;
    const float finishToneStrengthLimit = toneProposal.shadowReveal
        ? kStartingPointDarkRevealFinishToneStrengthLimit
        : kStartingPointFinishToneCautiousStrengthLimit;
    const bool mildFinishToneReady =
        toneProposal.valid &&
        toneProposal.strength <= finishToneStrengthLimit &&
        toneHighlightSafe;
    const bool visibleBalanceToneCanApplyAfterLocalRange =
        (toneProposal.shadowReveal || toneProposal.dynamicRangeBalance) &&
        localRangeChangedInPlan &&
        !rawExposureChangedInPlan &&
        !whiteBalanceChangedInPlan &&
        !renderedBaseDisplayFitAvailable;
    const bool mildFinishToneCanApply =
        mildFinishToneReady &&
        (!upstreamChangedBeforeTone ||
         visibleBalanceToneCanApplyAfterLocalRange ||
         ((renderedBaseFinishToneEvidenceReady || projectedBaseFinishToneEvidenceReady) &&
          !renderedBaseDisplayFitAvailable));
    if (mildFinishToneCanApply) {
        nlohmann::json finishTone = plan.upstreamRecipe.finishTone.layerJson.is_object()
            ? plan.upstreamRecipe.finishTone.layerJson
            : Stack::RawRecipe::DefaultFinishToneJson();
        finishTone["type"] = "ToneCurve";
        const auto copyVisibleKey = [&](const char* key) {
            const auto it = toneProposal.layerJson.find(key);
            if (it != toneProposal.layerJson.end()) {
                finishTone[key] = *it;
            }
        };
        copyVisibleKey("mode");
        copyVisibleKey("domain");
        copyVisibleKey("activeGraphView");
        copyVisibleKey("logMinEv");
        copyVisibleKey("logMaxEv");
        copyVisibleKey("points");
        copyVisibleKey("preparedPoints");
        plan.upstreamRecipe.finishTone.layerJson = std::move(finishTone);
        plan.visibleEdits.finishToneValid = true;
        plan.visibleEdits.finishToneNeutral = false;
        plan.visibleEdits.finishToneSummary = toneProposal.summary;
        plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::FinishTone);
        plan.appliedSummaries.push_back(finishToneUsesDarkRevealGate
            ? "Shadow-reveal Finish Tone"
            : finishToneUsesDynamicRangeGate
                ? "Dynamic-range Finish Tone"
                : "Mild Finish Tone");
        AppendUniqueSummary(
            plan.evidenceSummaries,
            finishToneUsesDarkRevealGate
                ? (renderedBaseFinishToneEvidenceReady
                    ? "Finish Tone from dark-reveal exact rendered Finish Tone Candidate"
                    : projectedBaseFinishToneEvidenceReady
                        ? "Finish Tone from dark-reveal projected post-exposure Finish Tone Candidate"
                        : visibleBalanceToneCanApplyAfterLocalRange
                            ? "Finish Tone from dark-reveal current pre-display tone evidence after Local Range"
                            : "Finish Tone from dark-reveal current pre-display tone evidence")
                : finishToneUsesDynamicRangeGate
                ? (renderedBaseFinishToneEvidenceReady
                    ? "Finish Tone from dynamic-range exact rendered Finish Tone Candidate"
                    : projectedBaseFinishToneEvidenceReady
                        ? "Finish Tone from dynamic-range projected post-exposure Finish Tone Candidate"
                        : visibleBalanceToneCanApplyAfterLocalRange
                            ? "Finish Tone from dynamic-range current pre-display tone evidence after Local Range"
                            : "Finish Tone from dynamic-range current pre-display tone evidence")
                : finishToneUsesCautiousGate
                ? (renderedBaseFinishToneEvidenceReady
                    ? "Finish Tone from cautious exact rendered Finish Tone Candidate"
                    : projectedBaseFinishToneEvidenceReady
                        ? "Finish Tone from cautious projected post-exposure Finish Tone Candidate"
                        : "Finish Tone from cautious current pre-display tone evidence")
                : (renderedBaseFinishToneEvidenceReady
                    ? "Finish Tone from exact rendered Finish Tone Candidate"
                    : projectedBaseFinishToneEvidenceReady
                        ? "Finish Tone from projected post-exposure Finish Tone Candidate"
                        : "Finish Tone from current pre-display tone evidence"));
        plan.hasUpstreamRecipeChanges = true;
        finishToneChangedInPlan = true;
    } else if (mildFinishToneReady && upstreamChangedBeforeTone) {
        plan.visibleEdits.finishToneSummary = toneProposal.summary;
        if (renderedBaseFinishToneEvidenceReady && renderedBaseDisplayFitAvailable) {
            plan.withheldSummaries.push_back(
                "Finish Tone withheld: rendered Display Fit would need a post-tone Display Candidate render");
            AppendUniqueSummary(
                plan.evidenceSummaries,
                "Finish Tone awaiting matching post-tone Display Candidate evidence");
            plan.warnings.push_back(
                std::string(toneProposal.shadowReveal
                    ? "Shadow-reveal"
                    : toneProposal.dynamicRangeBalance ? "Dynamic-range" : "Mild") +
                    " Finish Tone had rendered post-exposure evidence, but applying it would stale the rendered Display Fit already available for the one-click recipe.");
        } else {
            plan.withheldSummaries.push_back(
                "Finish Tone pending: upstream controls changed before a post-edit Finish Tone Candidate render");
            AppendUniqueSummary(
                plan.evidenceSummaries,
                "Finish Tone awaiting matching rendered Finish Tone Candidate evidence");
            plan.warnings.push_back(
                std::string(toneProposal.shadowReveal
                    ? "Shadow-reveal"
                    : toneProposal.dynamicRangeBalance ? "Dynamic-range" : "Mild") +
                    " Finish Tone was withheld because it was measured before the applied upstream Starting Point edits.");
        }
    } else if (anyRenderedBaseFinishToneCandidate != nullptr &&
        upstreamChangedBeforeTone &&
        renderedBaseFinishToneCandidate == nullptr) {
        plan.visibleEdits.finishToneSummary = toneProposal.summary;
        plan.withheldSummaries.push_back(
            "Finish Tone pending: rendered candidate does not match current upstream recipe");
        AppendUniqueSummary(
            plan.evidenceSummaries,
            "Finish Tone awaiting matching rendered Finish Tone Candidate evidence");
        plan.warnings.push_back(
            "Rendered Finish Tone Candidate evidence was withheld because White Balance, RAW Exposure, or Local Range changed after it was rendered.");
    } else if (toneProposal.valid) {
        if (toneProposal.strength > finishToneStrengthLimit) {
            std::ostringstream withheld;
            withheld << "Finish Tone withheld: proposal strength "
                << std::fixed << std::setprecision(2) << toneProposal.strength
                << " exceeds the " << finishToneStrengthLimit
                << (toneProposal.shadowReveal
                    ? " dark-reveal curve cap"
                    : toneProposal.dynamicRangeBalance
                        ? " dynamic-range curve cap"
                        : " one-click cautious curve cap");
            plan.withheldSummaries.push_back(withheld.str());
        } else {
            plan.withheldSummaries.push_back(
                "Finish Tone withheld: highlight safety blocked this contrast-adding curve");
        }
    } else {
        plan.visibleEdits.finishToneSummary = toneProposal.summary;
        plan.withheldSummaries.push_back(
            toneProposal.summary.empty() ? "Finish Tone unchanged" : toneProposal.summary);
        plan.warnings.insert(
            plan.warnings.end(),
            toneProposal.warnings.begin(),
            toneProposal.warnings.end());
    }

    const RawAutoStartPointCandidate* renderedFinalDisplayFitCandidate =
        FindCandidateWithRenderedCompleteStageAndRecipe(
            diagnostics.candidates,
            RawAutoStartPointCandidateKind::Base,
            RawAutoStartPointStage::DisplayCandidate,
            plan.upstreamRecipe);
    const bool renderedFinalDisplayFitReady =
        plan.hasUpstreamRecipeChanges &&
        renderedFinalDisplayFitCandidate != nullptr &&
        renderedFinalDisplayFitCandidate->hasRecipe &&
        renderedFinalDisplayFitCandidate->recipe.viewTransform.layerJson.is_object();
    if (renderedFinalDisplayFitReady) {
        plan.upstreamRecipe.viewTransform =
            renderedFinalDisplayFitCandidate->recipe.viewTransform;
        plan.visibleEdits.viewTransformSummary =
            FormatViewTransformLayerSummary(
                renderedFinalDisplayFitCandidate->recipe.viewTransform.layerJson,
                "Display Fit rendered");
        plan.usedRenderedDisplayFit = true;
        plan.needsPostApplyDisplayFit = false;
        AppendUniqueSummary(
            plan.evidenceSummaries,
            "Display Fit from exact rendered Display Candidate");
    } else if (plan.hasUpstreamRecipeChanges) {
        plan.visibleEdits.viewTransformSummary =
            "Display Fit will be refreshed after upstream Starting Point edits settle.";
        plan.needsPostApplyDisplayFit = true;
        AppendUniqueSummary(
            plan.evidenceSummaries,
            "Display Fit awaiting matching rendered Display Candidate evidence");
    } else {
        const Stack::RawAutoBase::ViewFitDecision decision =
            Stack::RawAutoBase::BuildAutoBaseViewFitDecision(analysis, currentRecipe);
        plan.visibleEdits.viewTransformSummary = decision.canApply
            ? "Display Fit current-frame fit: " + FormatViewFitSummary(decision.fit)
            : FormatViewFitSummary(decision.fit);
        plan.needsPostApplyDisplayFit = false;
        AppendUniqueSummary(
            plan.evidenceSummaries,
            "Display Fit from current-frame view-transform fit");
    }

    plan.visibleEdits.displayFitValid = true;
    plan.visibleEdits.touchedControls.push_back(RawAutoStartPointControl::DisplayFit);
    plan.appliedSummaries.push_back("Display Fit refresh");
    if (!baseCandidatePartialEvidenceSummary.empty()) {
        plan.warnings.push_back(
            "Build Starting Point carried partial Base candidate evidence; graph, tone, and final display writes stay gated until matching rendered candidate stages exist.");
    }

    std::vector<std::string> summaryParts = plan.appliedSummaries;
    if (summaryParts.empty()) {
        summaryParts.push_back("Display Fit refresh");
    }
    plan.summary = "Build Starting Point plan: " + JoinCommaSummaryParts(summaryParts) + ".";
    if (!plan.withheldSummaries.empty()) {
        plan.summary += " Withheld: " + JoinCommaSummaryParts(plan.withheldSummaries) + ".";
    }
    return plan;
}

const char* IntentStableString(RawAutoStartPointIntent intent) {
    switch (intent) {
        case RawAutoStartPointIntent::Balanced: return "balanced";
        case RawAutoStartPointIntent::Farther: return "farther";
        case RawAutoStartPointIntent::Base:
        default:
            return "base";
    }
}

const char* IntentLabel(RawAutoStartPointIntent intent) {
    switch (intent) {
        case RawAutoStartPointIntent::Balanced: return "Balanced";
        case RawAutoStartPointIntent::Farther: return "Farther";
        case RawAutoStartPointIntent::Base:
        default:
            return "Base";
    }
}

RawAutoStartPointIntent IntentFromStableString(const std::string& value) {
    if (value == "balanced") {
        return RawAutoStartPointIntent::Balanced;
    }
    if (value == "farther") {
        return RawAutoStartPointIntent::Farther;
    }
    return RawAutoStartPointIntent::Base;
}

const char* CandidateKindStableString(RawAutoStartPointCandidateKind kind) {
    switch (kind) {
        case RawAutoStartPointCandidateKind::CurrentFit: return "current-fit";
        case RawAutoStartPointCandidateKind::Balanced: return "balanced";
        case RawAutoStartPointCandidateKind::Farther: return "farther";
        case RawAutoStartPointCandidateKind::Base:
        default:
            return "base";
    }
}

const char* CandidateKindLabel(RawAutoStartPointCandidateKind kind) {
    switch (kind) {
        case RawAutoStartPointCandidateKind::CurrentFit: return "Current Fit";
        case RawAutoStartPointCandidateKind::Balanced: return "Balanced";
        case RawAutoStartPointCandidateKind::Farther: return "Farther";
        case RawAutoStartPointCandidateKind::Base:
        default:
            return "Base";
    }
}

RawAutoStartPointCandidateKind CandidateKindFromStableString(const std::string& value) {
    if (value == "current-fit") {
        return RawAutoStartPointCandidateKind::CurrentFit;
    }
    if (value == "balanced") {
        return RawAutoStartPointCandidateKind::Balanced;
    }
    if (value == "farther") {
        return RawAutoStartPointCandidateKind::Farther;
    }
    return RawAutoStartPointCandidateKind::Base;
}

const char* StageStableString(RawAutoStartPointStage stage) {
    switch (stage) {
        case RawAutoStartPointStage::RawTechnical: return "raw-technical";
        case RawAutoStartPointStage::NeutralScene: return "neutral-scene";
        case RawAutoStartPointStage::RawPlacement: return "raw-placement";
        case RawAutoStartPointStage::LocalCandidate: return "local-candidate";
        case RawAutoStartPointStage::FinishToneCandidate: return "finish-tone-candidate";
        case RawAutoStartPointStage::DisplayCandidate: return "display-candidate";
        default:
            return "raw-technical";
    }
}

const char* StageLabel(RawAutoStartPointStage stage) {
    switch (stage) {
        case RawAutoStartPointStage::RawTechnical: return "Raw Technical";
        case RawAutoStartPointStage::NeutralScene: return "Neutral Scene";
        case RawAutoStartPointStage::RawPlacement: return "Raw Placement";
        case RawAutoStartPointStage::LocalCandidate: return "Local Candidate";
        case RawAutoStartPointStage::FinishToneCandidate: return "Finish Tone Candidate";
        case RawAutoStartPointStage::DisplayCandidate: return "Display Candidate";
        default:
            return "Raw Technical";
    }
}

RawAutoStartPointStage StageFromStableString(const std::string& value) {
    if (value == "neutral-scene") {
        return RawAutoStartPointStage::NeutralScene;
    }
    if (value == "raw-placement") {
        return RawAutoStartPointStage::RawPlacement;
    }
    if (value == "local-candidate") {
        return RawAutoStartPointStage::LocalCandidate;
    }
    if (value == "finish-tone-candidate") {
        return RawAutoStartPointStage::FinishToneCandidate;
    }
    if (value == "display-candidate") {
        return RawAutoStartPointStage::DisplayCandidate;
    }
    return RawAutoStartPointStage::RawTechnical;
}

const char* StageStatusStableString(RawAutoStartPointStageStatus status) {
    switch (status) {
        case RawAutoStartPointStageStatus::Pending: return "pending";
        case RawAutoStartPointStageStatus::Complete: return "complete";
        case RawAutoStartPointStageStatus::Fallback: return "fallback";
        case RawAutoStartPointStageStatus::Projected: return "projected";
        case RawAutoStartPointStageStatus::Unavailable:
        default:
            return "unavailable";
    }
}

const char* ControlStableString(RawAutoStartPointControl control) {
    switch (control) {
        case RawAutoStartPointControl::RawExposure: return "raw-exposure";
        case RawAutoStartPointControl::WhiteBalance: return "white-balance";
        case RawAutoStartPointControl::LocalRange: return "local-range";
        case RawAutoStartPointControl::FinishTone: return "finish-tone";
        case RawAutoStartPointControl::DisplayFit: return "display-fit";
        default:
            return "raw-exposure";
    }
}

const char* ControlLabel(RawAutoStartPointControl control) {
    switch (control) {
        case RawAutoStartPointControl::RawExposure: return "RAW Exposure";
        case RawAutoStartPointControl::WhiteBalance: return "White Balance";
        case RawAutoStartPointControl::LocalRange: return "Local Range";
        case RawAutoStartPointControl::FinishTone: return "Finish Tone";
        case RawAutoStartPointControl::DisplayFit: return "Display Fit / View Transform";
        default:
            return "RAW Exposure";
    }
}

const char* DiagnosticSeverityStableString(RawAutoStartPointDiagnosticSeverity severity) {
    switch (severity) {
        case RawAutoStartPointDiagnosticSeverity::Warning: return "warning";
        case RawAutoStartPointDiagnosticSeverity::Error: return "error";
        case RawAutoStartPointDiagnosticSeverity::Info:
        default:
            return "info";
    }
}

RawAutoStartPointDiagnostics MakeUnavailableDiagnostics(
    std::string sourceKey,
    std::string statusMessage) {
    RawAutoStartPointDiagnostics diagnostics;
    diagnostics.sourceKey = std::move(sourceKey);
    diagnostics.statusMessage = std::move(statusMessage);
    diagnostics.dryRunOnly = true;
    diagnostics.appliedRecipeValues = false;
    diagnostics.uiView.summary = diagnostics.statusMessage;
    AddDiagnosticsSourceLine(diagnostics.uiView, diagnostics.sourceKey);
    diagnostics.uiView.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Build Starting Point",
        "Unavailable",
        diagnostics.statusMessage
    });
    return diagnostics;
}

RawAutoStartPointDiagnostics BuildDryRunCandidateDiagnostics(
    RawAutoStartPointDiagnostics diagnostics,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations) {
    std::vector<RawAutoStartPointStageDiagnostics> stages = ExtractStageDiagnostics(diagnostics);
    RawAutoStartPointStageDiagnostics renderedPostWhiteBalanceRawPlacementStage;
    bool hasRenderedPostWhiteBalanceRawPlacementStage = false;
    const bool useSuggestedWhiteBalance =
        recommendations.whiteBalance.valid &&
        recommendations.whiteBalance.autoApplyAllowed &&
        recommendations.whiteBalance.alternateCandidateAvailable &&
        !recommendations.whiteBalance.cameraWhiteBalanceAvailable &&
        !recommendations.whiteBalance.manualWhiteBalanceProtected;
    Stack::RawRecipe::RawDevelopmentRecipe postWhiteBalanceRecipe = currentRecipe;
    if (useSuggestedWhiteBalance) {
        Stack::RawAutoBase::ApplyWhiteBalanceRecommendationToRecipe(
            postWhiteBalanceRecipe,
            recommendations.whiteBalance);
    }
    if (const RawAutoStartPointCandidate* renderedBaseRawPlacementCandidate =
            useSuggestedWhiteBalance
                ? FindCandidateWithRenderedCompleteStageAndRecipe(
                      diagnostics.candidates,
                      RawAutoStartPointCandidateKind::Base,
                      RawAutoStartPointStage::RawPlacement,
                      postWhiteBalanceRecipe)
                : nullptr) {
        if (const RawAutoStartPointStageDiagnostics* stage =
                FindStageDiagnostics(
                    renderedBaseRawPlacementCandidate->stageDiagnostics,
                    RawAutoStartPointStage::RawPlacement)) {
            renderedPostWhiteBalanceRawPlacementStage = *stage;
            hasRenderedPostWhiteBalanceRawPlacementStage = true;
        }
    }
    diagnostics.valid = diagnostics.valid || analysis.valid || !stages.empty();
    diagnostics.dryRunOnly = true;
    diagnostics.appliedRecipeValues = false;
    diagnostics.requestedIntent = RawAutoStartPointIntent::Base;
    if (diagnostics.sourceKey.empty()) {
        diagnostics.sourceKey = analysis.sourceKey;
    }
    diagnostics.statusMessage = diagnostics.valid
        ? "Build Starting Point dry-run candidates are available in Diagnostics only."
        : "Build Starting Point dry-run candidates need a RAW preview analysis first.";
    diagnostics.warnings.clear();

    const Stack::RawAutoBase::ViewFitDecision fitDecision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(analysis, currentRecipe);

    diagnostics.candidates.clear();
    diagnostics.candidates.push_back(BuildCurrentFitCandidate(
        currentRecipe,
        stages,
        analysis,
        recommendations,
        fitDecision));
    diagnostics.candidates.push_back(BuildBaseCandidate(
        currentRecipe,
        stages,
        hasRenderedPostWhiteBalanceRawPlacementStage
            ? &renderedPostWhiteBalanceRawPlacementStage
            : nullptr,
        analysis,
        recommendations,
        fitDecision));
    diagnostics.candidates.push_back(BuildBalancedCandidate(
        currentRecipe,
        stages,
        analysis,
        recommendations,
        fitDecision));

    RefreshDryRunEvidenceWarnings(diagnostics, stages);
    RefreshSelectedCandidate(diagnostics);
    RebuildDryRunDiagnosticsUi(diagnostics, stages);
    return diagnostics;
}

RawAutoStartPointDiagnostics MergeCandidateRenderResults(
    RawAutoStartPointDiagnostics diagnostics,
    const std::vector<RawAutoStartPointCandidateRenderResult>& results,
    const Stack::RawRecipe::RawDevelopmentRecipe& currentRecipe,
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const Stack::RawAutoBase::AutoBaseRecommendations& recommendations) {
    if (results.empty() || diagnostics.candidates.empty()) {
        return diagnostics;
    }

    const Stack::RawAutoBase::ViewFitDecision fitDecision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(analysis, currentRecipe);
    int mergedCount = 0;
    bool rejectedRenderedEvidence = false;
    for (const RawAutoStartPointCandidateRenderResult& result : results) {
        if (!result.attempted || !result.success || !result.request.valid) {
            continue;
        }

        const std::vector<RawAutoStartPointStageDiagnostics> renderedStages =
            ExtractStageDiagnostics(result.diagnostics);
        const RawAutoStartPointStageDiagnostics* renderedStage =
            FindStageDiagnostics(renderedStages, result.request.stage);
        if (renderedStage == nullptr ||
            renderedStage->status != RawAutoStartPointStageStatus::Complete) {
            continue;
        }

        for (RawAutoStartPointCandidate& candidate : diagnostics.candidates) {
            const auto matchingRequest = std::find_if(
                candidate.renderRequests.begin(),
                candidate.renderRequests.end(),
                [&](const RawAutoStartPointCandidateRenderRequest& request) {
                    return request.id == result.request.id &&
                        request.stage == result.request.stage;
                });
            if (matchingRequest == candidate.renderRequests.end()) {
                continue;
            }
            if (!RenderRequestRecipeStillMatches(*matchingRequest, result.request)) {
                AppendUniqueSummary(
                    candidate.warnings,
                    std::string("Rendered ") + CandidateKindLabel(candidate.kind) +
                        " candidate evidence for " + StageLabel(result.request.stage) +
                        " was rejected because " +
                        RenderRequestMismatchReason(*matchingRequest, result.request) +
                        ".");
                rejectedRenderedEvidence = true;
                continue;
            }

            RawAutoStartPointStageDiagnostics replacement = *renderedStage;
            replacement.stage = result.request.stage;
            replacement.status = RawAutoStartPointStageStatus::Complete;
            replacement.confidence01 = std::max(replacement.confidence01, 1.0f);
            const std::string renderedStatus =
                std::string("Rendered ") + CandidateKindLabel(candidate.kind) +
                " candidate evidence for " + StageLabel(result.request.stage) +
                (result.request.stage == RawAutoStartPointStage::DisplayCandidate
                    ? " from the queued visible recipe after fitting View Transform."
                    : " from the queued visible recipe.");
            replacement.statusMessage = renderedStatus +
                (replacement.statusMessage.empty() ? "" : " " + replacement.statusMessage);
            if (replacement.scene.valid) {
                replacement.scene.stage = result.request.stage;
                replacement.scene.status = RawAutoStartPointStageStatus::Complete;
                replacement.scene.statusMessage = replacement.statusMessage;
            }
            if (replacement.display.valid) {
                replacement.display.status = RawAutoStartPointStageStatus::Complete;
                replacement.display.statusMessage = replacement.statusMessage;
            }

            ReplaceStageDiagnostics(candidate.stageDiagnostics, std::move(replacement));
            if (result.hasRenderedRecipe) {
                candidate.hasRecipe = true;
                candidate.recipe = result.renderedRecipe;
                if (result.request.stage == RawAutoStartPointStage::DisplayCandidate) {
                    candidate.visibleEdits.displayFitValid = true;
                    candidate.visibleEdits.viewTransformSummary =
                        FormatViewTransformLayerSummary(
                            result.renderedRecipe.viewTransform.layerJson,
                            "Display Fit rendered");
                }
            }
            if (result.request.stage == RawAutoStartPointStage::LocalCandidate) {
                candidate.visibleEdits.localRangeSummary =
                    "Rendered Local Candidate evidence is ready for the one-click Local Range gate.";
            }
            candidate.renderRequests.erase(matchingRequest);
            RefreshBaseCandidateEvidenceWarnings(candidate);
            ++mergedCount;
            break;
        }
    }

    if (mergedCount <= 0) {
        if (rejectedRenderedEvidence) {
            diagnostics.valid = true;
            diagnostics.statusMessage =
                "Build Starting Point rejected stale or mis-scoped per-candidate render evidence.";
            const std::vector<RawAutoStartPointStageDiagnostics> stages =
                ExtractStageDiagnostics(diagnostics);
            RebuildDryRunDiagnosticsUi(diagnostics, stages);
        }
        return diagnostics;
    }

    diagnostics.valid = true;
    diagnostics.dryRunOnly = true;
    diagnostics.appliedRecipeValues = false;
    diagnostics.statusMessage =
        "Build Starting Point dry-run candidates include rendered per-candidate evidence.";
    const RawAutoStartPointConservativePlan plan =
        BuildConservativeStartingPointPlan(
            currentRecipe,
            analysis,
            recommendations,
            diagnostics);
    const bool queuedFinalDisplayRequest =
        QueueBaseFinalDisplayCandidateRequestIfNeeded(
            diagnostics,
            plan,
            currentRecipe);
    if (queuedFinalDisplayRequest) {
        diagnostics.statusMessage =
            "Build Starting Point dry-run candidates include rendered evidence and a queued full-recipe Display Candidate.";
    }
    for (RawAutoStartPointCandidate& candidate : diagnostics.candidates) {
        candidate.score = BuildCandidateScore(
            candidate.kind,
            candidate.stageDiagnostics,
            analysis,
            recommendations,
            fitDecision);
    }

    const std::vector<RawAutoStartPointStageDiagnostics> stages =
        ExtractStageDiagnostics(diagnostics);
    RefreshDryRunEvidenceWarnings(diagnostics, stages);
    RefreshSelectedCandidate(diagnostics);
    RebuildDryRunDiagnosticsUi(diagnostics, stages);
    return diagnostics;
}

RawAutoStartPointDiagnosticsView BuildDiagnosticsView(
    const RawAutoStartPointDiagnostics& diagnostics) {
    RawAutoStartPointDiagnosticsView view = diagnostics.uiView;
    if (view.title.empty()) {
        view.title = "Build Starting Point Diagnostics";
    }
    if (view.summary.empty()) {
        view.summary = diagnostics.statusMessage.empty()
            ? "No RAW starting-point diagnostics have been produced."
            : diagnostics.statusMessage;
    }
    if (!view.lines.empty()) {
        return view;
    }

    AddDiagnosticsSourceLine(view, diagnostics.sourceKey);
    view.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "State",
        diagnostics.valid ? "Ready" : "Unavailable",
        diagnostics.statusMessage
    });
    view.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Mode",
        IntentLabel(diagnostics.requestedIntent),
        "Requested starting-point intent."
    });
    view.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Recipe writes",
        diagnostics.appliedRecipeValues ? "Applied" : "None",
        diagnostics.dryRunOnly
            ? "Diagnostics are dry-run only."
            : "Explicit starting-point actions write visible recipe controls only."
    });
    view.lines.push_back({
        RawAutoStartPointDiagnosticSeverity::Info,
        "Candidates",
        std::to_string(diagnostics.candidates.size()),
        "Candidate reports summarize the available starting-point evidence; rendering this report does not write recipe values."
    });
    if (diagnostics.hasSelectedCandidate) {
        view.lines.push_back({
            RawAutoStartPointDiagnosticSeverity::Info,
            "Selected",
            CandidateKindLabel(diagnostics.selectedCandidateKind),
            "Selection is diagnostic only unless appliedRecipeValues is true."
        });
    }
    return view;
}

std::vector<RawAutoStartPointCandidateRenderRequest> CollectCandidateRenderRequests(
    const RawAutoStartPointDiagnostics& diagnostics) {
    std::vector<RawAutoStartPointCandidateRenderRequest> requests;
    for (const RawAutoStartPointCandidate& candidate : diagnostics.candidates) {
        for (const RawAutoStartPointCandidateRenderRequest& request : candidate.renderRequests) {
            if (!request.valid || !request.hasRecipe) {
                continue;
            }
            requests.push_back(request);
        }
    }
    return requests;
}

nlohmann::json SerializeDiagnosticsView(
    const RawAutoStartPointDiagnosticsView& view) {
    nlohmann::json lines = nlohmann::json::array();
    for (const RawAutoStartPointDiagnosticLine& line : view.lines) {
        lines.push_back({
            { "severity", DiagnosticSeverityStableString(line.severity) },
            { "label", JsonStringOrNull(line.label) },
            { "value", JsonStringOrNull(line.value) },
            { "detail", JsonStringOrNull(line.detail) }
        });
    }
    return nlohmann::json {
        { "title", JsonStringOrNull(view.title) },
        { "summary", JsonStringOrNull(view.summary) },
        { "lines", std::move(lines) }
    };
}

nlohmann::json SerializeDiagnostics(
    const RawAutoStartPointDiagnostics& diagnostics) {
    return nlohmann::json {
        { "version", diagnostics.version },
        { "valid", diagnostics.valid },
        { "dryRunOnly", diagnostics.dryRunOnly },
        { "appliedRecipeValues", diagnostics.appliedRecipeValues },
        { "requestedIntent", IntentStableString(diagnostics.requestedIntent) },
        { "requestedIntentLabel", IntentLabel(diagnostics.requestedIntent) },
        { "hasSelectedCandidate", diagnostics.hasSelectedCandidate },
        { "selectedCandidateIndex", diagnostics.selectedCandidateIndex },
        { "selectedCandidateKind", CandidateKindStableString(diagnostics.selectedCandidateKind) },
        { "sourceKey", JsonStringOrNull(diagnostics.sourceKey) },
        { "statusMessage", JsonStringOrNull(diagnostics.statusMessage) },
        { "warnings", SerializeStringVector(diagnostics.warnings) },
        { "rawSafety", SerializeRawSafetyStats(diagnostics.rawSafety) },
        { "candidates", SerializeCandidates(diagnostics.candidates) },
        { "uiView", SerializeDiagnosticsView(BuildDiagnosticsView(diagnostics)) }
    };
}

} // namespace Stack::RawAutoStartPoint
