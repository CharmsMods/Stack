#include "Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h"

#include <algorithm>
#include <cmath>

namespace Stack::Editor::DevelopRenderedCandidateScoring {

namespace {

constexpr float kDevelopRenderedDuplicateDistance = 0.085f;

} // namespace

float ScoreDevelopRenderedCandidateMetrics(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    float solveScore) {
    const float medianBalance = 1.0f - std::clamp(std::abs(metrics.medianLuma - 0.42f) / 0.42f, 0.0f, 1.0f);
    const float usefulContrast = std::clamp(metrics.contrastSpan / 0.62f, 0.0f, 1.0f);
    const float highlightSafety = 1.0f - std::clamp(
        metrics.clippedFraction * 8.0f +
            metrics.highlightFraction * 0.45f -
            metrics.meaningfulHighlightPressure * 0.10f,
        0.0f,
        1.0f);
    const float highlightBrightnessFeeling =
        1.0f - std::clamp(metrics.highlightGrayRisk * 0.82f, 0.0f, 1.0f);
    const float shadowSafety = 1.0f - std::clamp(std::max(0.0f, metrics.shadowFraction - 0.48f) / 0.42f, 0.0f, 1.0f);
    const float colorPlausibility =
        1.0f - std::clamp(
            std::max(0.0f, metrics.lowSaturationFraction - 0.72f) / 0.28f +
                std::max(0.0f, 0.08f - metrics.meanSaturation) / 0.08f +
                metrics.colorCastRisk * 0.42f,
            0.0f,
            1.0f);
    const float visualSafety =
        1.0f - std::clamp(
            metrics.haloRiskFraction * 3.0f +
                metrics.shadowTextureRisk * 0.22f +
                std::max(0.0f, metrics.edgeContrast - 0.64f) * 0.40f,
            0.0f,
            1.0f);
    const float localSafety =
        1.0f - std::clamp(
            std::max(0.0f, metrics.localHighlightPressure - 0.58f) / 0.42f +
                std::max(0.0f, metrics.localShadowPressure - 0.74f) / 0.26f +
                std::max(0.0f, metrics.localContrastPeak - 0.80f) * 0.35f +
                metrics.localDamageRiskPeak * 0.38f +
                metrics.localDamageRiskMean * 0.18f,
            0.0f,
            1.0f);
    const float centerSafety =
        1.0f - std::clamp(
            metrics.centerShadowFraction * 0.32f +
                metrics.centerHighlightFraction * 0.44f,
            0.0f,
            1.0f);
    const float localShape =
        std::clamp(metrics.localLumaSpread / 0.48f, 0.0f, 1.0f) * localSafety * centerSafety;
    const float markedSubjectPresence =
        std::clamp(metrics.subjectMarkedPositiveCoverage * 1.8f, 0.0f, 1.0f);
    const float markedReadabilityWeight = std::clamp(
        metrics.subjectMarkedPositiveCoverage +
            metrics.subjectMarkedRevealCoverage * 0.75f -
            metrics.subjectMarkedMoodCoverage * 0.35f,
        0.0f,
        1.0f);
    const float markedProtectionWeight = std::clamp(
        metrics.subjectMarkedProtectCoverage +
            metrics.subjectMarkedPositiveCoverage * 0.35f,
        0.0f,
        1.0f);
    const float markedMoodWeight = std::clamp(metrics.subjectMarkedMoodCoverage * 1.4f, 0.0f, 1.0f);
    const float markedSubjectFit = std::clamp(
        metrics.subjectMarkedReadabilityScore * markedReadabilityWeight * 0.46f +
            (1.0f - metrics.subjectMarkedProtectionRisk) * markedProtectionWeight * 0.32f +
            metrics.subjectMarkedMoodPreservationScore * markedMoodWeight * 0.22f,
        0.0f,
        1.0f);
    const float lowPriorityPenalty =
        std::clamp(metrics.subjectMarkedLowPriorityPressure * 0.045f, 0.0f, 0.045f);
    return std::clamp(
        solveScore * 0.37f +
            medianBalance * 0.16f +
            usefulContrast * 0.12f +
            highlightSafety * 0.10f +
            highlightBrightnessFeeling * 0.03f +
            shadowSafety * 0.05f +
            colorPlausibility * 0.05f +
            visualSafety * 0.06f +
            localShape * 0.06f +
            markedSubjectFit * markedSubjectPresence * 0.045f -
            lowPriorityPenalty,
        0.0f,
        1.0f);
}

DevelopRenderedRelativeComparison CompareDevelopRenderedCandidateToSelected(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidate,
    float standaloneScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selected,
    float selectedScore,
    const std::string& activeRefineIntent,
    bool selectedBaseline) {
    DevelopRenderedRelativeComparison comparison;
    comparison.standaloneScore = standaloneScore;
    comparison.selectedScore = selectedScore;
    comparison.adjustedScore = std::clamp(standaloneScore, 0.0f, 1.0f);
    comparison.metricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(candidate, selected);

    if (selectedBaseline) {
        comparison.status = "selectedBaseline";
        comparison.repairMetric = "selectedBaseline";
        comparison.reason = "Selected candidate establishes the rendered comparison baseline.";
        return comparison;
    }

    auto scaledPositive = [](float value, float deadZone, float scale, float maxValue) {
        return std::clamp((value - deadZone) / std::max(0.0001f, scale), 0.0f, 1.0f) * maxValue;
    };
    auto addRegression = [&](float value, const std::string& reason) {
        if (value <= 0.0f) {
            return;
        }
        comparison.regressionPenalty += value;
        if (comparison.reason.empty() || comparison.status == "relativeCompared") {
            comparison.reason = reason;
        }
    };

    comparison.distanceBonus =
        scaledPositive(
            comparison.metricDistance,
            kDevelopRenderedDuplicateDistance,
            0.24f,
            0.014f);

    addRegression(
        scaledPositive(candidate.clippedFraction - selected.clippedFraction, 0.010f, 0.050f, 0.040f),
        "Candidate increased broad highlight clipping relative to the selected render.");
    if (candidate.highlightFraction > 0.56f) {
        addRegression(
            scaledPositive(candidate.highlightFraction - selected.highlightFraction, 0.10f, 0.24f, 0.026f),
            "Candidate crowded more highlights than the selected render.");
    }
    if (candidate.localHighlightPressure > 0.62f) {
        addRegression(
            scaledPositive(candidate.localHighlightPressure - selected.localHighlightPressure, 0.12f, 0.24f, 0.024f),
            "Candidate increased localized highlight pressure relative to the selected render.");
    }
    if (candidate.highlightGrayRisk > 0.42f) {
        addRegression(
            scaledPositive(candidate.highlightGrayRisk - selected.highlightGrayRisk, 0.08f, 0.24f, 0.026f),
            "Candidate made broad highlights look flatter or grayer than the selected render.");
    }
    if (selected.meaningfulHighlightPressure > 0.34f &&
        candidate.meaningfulHighlightPressure + 0.12f < selected.meaningfulHighlightPressure &&
        candidate.clippedFraction >= selected.clippedFraction - 0.006f) {
        addRegression(
            scaledPositive(selected.meaningfulHighlightPressure - candidate.meaningfulHighlightPressure, 0.10f, 0.30f, 0.024f),
            "Candidate reduced structured broad-highlight evidence without a matching clipping improvement.");
    }
    if (candidate.shadowTextureRisk > 0.62f) {
        addRegression(
            scaledPositive(candidate.shadowTextureRisk - selected.shadowTextureRisk, 0.08f, 0.24f, 0.030f),
            "Candidate increased shadow texture/noise pressure relative to the selected render.");
    }
    if (candidate.localDamageRiskPeak > 0.66f) {
        addRegression(
            scaledPositive(candidate.localDamageRiskPeak - selected.localDamageRiskPeak, 0.10f, 0.28f, 0.025f),
            "Candidate increased localized render-damage risk relative to the selected render.");
    }
    if (candidate.haloRiskFraction > 0.10f) {
        addRegression(
            scaledPositive(candidate.haloRiskFraction - selected.haloRiskFraction, 0.04f, 0.16f, 0.024f),
            "Candidate increased halo or edge-glow risk relative to the selected render.");
    }
    if (candidate.colorCastRisk > 0.70f) {
        addRegression(
            scaledPositive(candidate.colorCastRisk - selected.colorCastRisk, 0.12f, 0.30f, 0.018f),
            "Candidate increased color-cast risk relative to the selected render.");
    }
    if (candidate.subjectMarkedPositiveCoverage > 0.010f &&
        selected.subjectMarkedPositiveCoverage > 0.010f) {
        if (candidate.subjectMarkedReadabilityScore + 0.08f < selected.subjectMarkedReadabilityScore &&
            selected.subjectMarkedRevealCoverage + selected.subjectMarkedPositiveCoverage >
                selected.subjectMarkedMoodCoverage * 0.70f) {
            addRegression(
                scaledPositive(
                    selected.subjectMarkedReadabilityScore - candidate.subjectMarkedReadabilityScore,
                    0.07f,
                    0.24f,
                    0.022f),
                "Candidate made the user-marked important region less readable than the selected render.");
        }
        if (candidate.subjectMarkedProtectionRisk > 0.42f) {
            addRegression(
                scaledPositive(
                    candidate.subjectMarkedProtectionRisk - selected.subjectMarkedProtectionRisk,
                    0.07f,
                    0.24f,
                    0.022f),
                "Candidate increased protection risk in the user-marked important region.");
        }
    }

    if (candidate.medianLuma < 0.16f && candidate.medianLuma < selected.medianLuma - 0.14f) {
        addRegression(0.018f, "Candidate made the rendered mids materially darker than the selected baseline.");
    } else if (candidate.medianLuma > 0.72f && candidate.medianLuma > selected.medianLuma + 0.16f) {
        addRegression(0.018f, "Candidate made the rendered mids materially brighter than the selected baseline.");
    }

    if (activeRefineIntent == "protectHighlights") {
        comparison.repairMetric = "highlightPressure";
        comparison.repairDelta =
            (selected.clippedFraction - candidate.clippedFraction) * 1.80f +
            (selected.highlightFraction - candidate.highlightFraction) * 0.38f +
            (selected.localHighlightPressure - candidate.localHighlightPressure) * 0.28f +
            (selected.centerHighlightFraction - candidate.centerHighlightFraction) * 0.20f;
    } else if (activeRefineIntent == "openShadows") {
        comparison.repairMetric = "shadowOpening";
        comparison.repairDelta =
            (selected.shadowFraction - candidate.shadowFraction) * 0.45f +
            (selected.localShadowPressure - candidate.localShadowPressure) * 0.32f +
            (selected.centerShadowFraction - candidate.centerShadowFraction) * 0.25f +
            (candidate.medianLuma - selected.medianLuma) * 0.18f;
    } else if (activeRefineIntent == "brightenMids") {
        comparison.repairMetric = "medianLuma";
        comparison.repairDelta =
            (candidate.medianLuma - selected.medianLuma) * 0.70f +
            (candidate.meanLuma - selected.meanLuma) * 0.30f -
            std::max(0.0f, candidate.highlightFraction - selected.highlightFraction) * 0.20f -
            std::max(0.0f, candidate.clippedFraction - selected.clippedFraction) * 1.20f;
    } else if (activeRefineIntent == "addContrast") {
        comparison.repairMetric = "contrast";
        comparison.repairDelta =
            (candidate.contrastSpan - selected.contrastSpan) * 0.60f +
            (candidate.localContrastPeak - selected.localContrastPeak) * 0.18f -
            std::max(0.0f, candidate.haloRiskFraction - selected.haloRiskFraction) * 0.45f -
            std::max(0.0f, candidate.localDamageRiskPeak - selected.localDamageRiskPeak) * 0.15f;
    } else if (activeRefineIntent == "cleanShadows") {
        comparison.repairMetric = "shadowTextureRisk";
        comparison.repairDelta =
            (selected.shadowTextureRisk - candidate.shadowTextureRisk) * 0.55f +
            (selected.localDamageRiskPeak - candidate.localDamageRiskPeak) * 0.24f +
            (selected.localShadowPressure - candidate.localShadowPressure) * 0.12f -
            std::max(0.0f, candidate.shadowFraction - selected.shadowFraction) * 0.20f;
    } else if (activeRefineIntent == "preserveTexture") {
        comparison.repairMetric = "textureSeparation";
        comparison.repairDelta =
            (candidate.edgeContrast - selected.edgeContrast) * 0.45f +
            (candidate.localContrastPeak - selected.localContrastPeak) * 0.16f -
            std::max(0.0f, candidate.shadowTextureRisk - selected.shadowTextureRisk) * 0.30f -
            std::max(0.0f, candidate.haloRiskFraction - selected.haloRiskFraction) * 0.30f;
    } else {
        comparison.repairMetric = "generalRenderRisk";
        comparison.repairDelta =
            std::max(0.0f, selected.clippedFraction - candidate.clippedFraction) * 1.20f +
            std::max(0.0f, selected.highlightFraction - candidate.highlightFraction - 0.03f) * 0.16f +
            std::max(0.0f, selected.shadowFraction - candidate.shadowFraction - 0.04f) * 0.12f +
            std::max(0.0f, candidate.contrastSpan - selected.contrastSpan - 0.02f) * 0.10f;
    }

    comparison.repairBonus =
        scaledPositive(comparison.repairDelta, activeRefineIntent.empty() ? 0.020f : 0.010f, 0.16f,
            activeRefineIntent.empty() ? 0.018f : 0.045f);

    if (!activeRefineIntent.empty() && comparison.repairDelta < -0.014f) {
        addRegression(
            scaledPositive(-comparison.repairDelta, 0.014f, 0.16f, 0.040f),
            "Candidate moved opposite the active rendered repair intent.");
    } else if (!activeRefineIntent.empty() &&
        comparison.repairDelta < 0.008f &&
        standaloneScore > selectedScore + 0.025f) {
        addRegression(0.018f, "Candidate scored well overall but did not materially address the active rendered repair intent.");
    }

    comparison.regressionPenalty = std::min(comparison.regressionPenalty, 0.095f);
    comparison.adjustedScore =
        std::clamp(
            standaloneScore +
                comparison.repairBonus +
                comparison.distanceBonus -
                comparison.regressionPenalty,
            0.0f,
            1.0f);

    if (!activeRefineIntent.empty() && comparison.repairDelta > 0.010f) {
        comparison.status = "improvesActiveRepair";
        comparison.reason =
            "Candidate improves the active rendered repair target relative to the selected baseline.";
    } else if (!activeRefineIntent.empty() && comparison.repairDelta < -0.014f) {
        comparison.status = "missedActiveRepair";
        if (comparison.reason.empty()) {
            comparison.reason =
                "Candidate moved away from the active rendered repair target relative to the selected baseline.";
        }
    } else if (comparison.regressionPenalty > comparison.repairBonus + 0.015f) {
        comparison.status = "regressedAgainstSelected";
        if (comparison.reason.empty()) {
            comparison.reason =
                "Candidate introduced more rendered risk than it repaired relative to the selected baseline.";
        }
    } else if (comparison.metricDistance < kDevelopRenderedDuplicateDistance) {
        comparison.status = "nearSelected";
        comparison.reason =
            "Candidate is close to the selected render after comparing rendered metrics.";
    } else {
        comparison.status = "relativeCompared";
        comparison.reason =
            "Candidate was compared against the selected rendered baseline and retained its standalone score with small relative adjustments.";
    }

    return comparison;
}

std::string ClassifyDevelopRenderedCandidateDamage(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent) {
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool cleanIntent = intent == EditorNodeGraph::DevelopAutoIntent::CleanBase;

    const float broadClipLimit = rangeIntent ? 0.080f : 0.055f;
    if (metrics.clippedFraction > broadClipLimit ||
        (metrics.highlightFraction > 0.78f && metrics.clippedFraction > 0.012f)) {
        return "Rejected because rendered metrics showed broad highlight clipping/crowding beyond the selected intent.";
    }
    if (metrics.meaningfulHighlightPressure > 0.58f &&
        metrics.clippedFraction > (rangeIntent ? 0.040f : 0.026f) &&
        metrics.highlightTileCoverage > 0.24f) {
        return "Rejected because rendered metrics showed meaningful broad-highlight structure clipping beyond the selected intent.";
    }
    if (metrics.haloRiskFraction > 0.22f &&
        (metrics.edgeContrast > 0.34f || metrics.localContrastPeak > 0.76f)) {
        return "Rejected because rendered metrics showed strong halo or edge-glow risk.";
    }
    const float localizedRiskLimit = rangeIntent || flatIntent ? 0.94f : 0.88f;
    if (metrics.localDamageRiskPeak > localizedRiskLimit &&
        metrics.localDamageRiskMean > 0.20f &&
        (metrics.localHighlightPressure > 0.66f ||
         metrics.localShadowPressure > 0.80f ||
         metrics.localContrastPeak > 0.88f)) {
        return "Rejected because rendered spatial metrics showed a localized damage hotspot that would be visible in the final image.";
    }
    const float colorCastLimit = rangeIntent || darkIntent ? 0.92f : 0.86f;
    if (metrics.colorCastRisk > colorCastLimit &&
        (std::fabs(metrics.magentaGreenBias) > 0.30f ||
         (metrics.channelImbalance > 0.90f && metrics.meanSaturation > 0.42f) ||
         (metrics.lowSaturationFraction > 0.78f && metrics.meanSaturation < 0.08f))) {
        return "Rejected because rendered color metrics showed an extreme cast or channel imbalance beyond the selected intent.";
    }
    const float flatContrastFloor = flatIntent ? 0.20f : 0.30f;
    if (metrics.lowSaturationFraction > 0.93f &&
        metrics.meanSaturation < 0.055f &&
        metrics.contrastSpan < flatContrastFloor) {
        return "Rejected because rendered metrics looked washed out and gray with weak tonal separation.";
    }
    if (!flatIntent &&
        metrics.highlightGrayRisk > 0.86f &&
        metrics.highlightBandFraction > 0.20f &&
        metrics.highlightMeanLuma < 0.62f &&
        metrics.clippedFraction < 0.018f) {
        return "Rejected because rendered metrics showed broad highlights flattening toward gray instead of staying luminous.";
    }
    const float shadowTextureLimit = cleanIntent ? 0.92f : 0.86f;
    const float shadowFractionLimit = rangeIntent ? 0.62f : 0.50f;
    if (metrics.shadowTextureRisk > shadowTextureLimit &&
        metrics.shadowFraction > shadowFractionLimit &&
        metrics.medianLuma > 0.16f) {
        return "Rejected because rendered metrics showed noisy lifted shadows beyond the current cleanliness/texture intent.";
    }
    if (metrics.medianLuma < (darkIntent ? 0.055f : 0.075f) &&
        metrics.shadowFraction > (darkIntent ? 0.90f : 0.80f)) {
        return "Rejected because rendered metrics were too weak to preserve a usable brightness hierarchy.";
    }
    if (metrics.medianLuma > 0.84f && metrics.highlightFraction > 0.64f) {
        return "Rejected because rendered metrics were too bright and highlight-heavy to preserve a believable hierarchy.";
    }
    return {};
}

std::string ResolveDevelopRenderedRefineIntent(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent,
    std::string& outReason) {
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;

    if (metrics.clippedFraction > 0.018f || metrics.highlightFraction > 0.56f) {
        outReason = "Rendered metrics showed highlight crowding or clipping, so the solver should try more highlight protection.";
        return "protectHighlights";
    }
    if (metrics.meaningfulHighlightPressure > 0.46f &&
        (metrics.clippedFraction > 0.006f || metrics.localHighlightPressure > 0.44f)) {
        outReason = "Rendered metrics showed structured broad-highlight pressure, so the solver should protect meaningful highlights before treating them as tiny glints.";
        return "protectHighlights";
    }
    const bool localizedDamageRisk =
        metrics.localDamageRiskPeak > 0.70f &&
        metrics.localDamageRiskMean > 0.10f;
    if (localizedDamageRisk &&
        (metrics.localHighlightPressure > 0.56f || metrics.centerHighlightFraction > 0.30f)) {
        outReason = "Rendered spatial metrics showed a localized bright-region damage hotspot, so the solver should try more highlight/local exposure restraint.";
        return "protectHighlights";
    }
    if (localizedDamageRisk &&
        metrics.localContrastPeak > 0.84f &&
        (metrics.haloRiskFraction > 0.03f || metrics.edgeContrast > 0.30f)) {
        outReason = "Rendered spatial metrics showed a localized high-contrast damage hotspot, so the solver should back off highlight/local contrast shaping.";
        return "protectHighlights";
    }
    if (localizedDamageRisk &&
        metrics.localShadowPressure > (darkIntent ? 0.78f : 0.64f) &&
        metrics.localHighlightPressure < 0.58f &&
        metrics.highlightFraction < 0.34f &&
        metrics.clippedFraction < 0.010f) {
        if (metrics.shadowTextureRisk > (darkIntent ? 0.66f : 0.54f)) {
            outReason = "Rendered spatial metrics showed a localized shadow damage hotspot with texture/noise pressure, so the solver should try cleaner shadows instead of lifting more.";
            return "cleanShadows";
        }
        outReason = "Rendered spatial metrics showed a localized shadow damage hotspot without highlight pressure, so the solver should try opening shadows in a damped way.";
        return "openShadows";
    }
    if (localizedDamageRisk &&
        !darkIntent &&
        metrics.localContrastPeak < 0.24f &&
        metrics.lowSaturationFraction > 0.68f &&
        metrics.highlightFraction < 0.30f &&
        metrics.clippedFraction < 0.006f) {
        outReason = "Rendered spatial metrics showed a localized flat-gray hotspot without clipping pressure, so the solver should try a modest contrast refinement.";
        return "addContrast";
    }
    if (metrics.localHighlightPressure > 0.70f || metrics.centerHighlightFraction > 0.46f) {
        outReason = "Rendered local metrics showed concentrated highlight crowding, so the solver should try more highlight/local exposure restraint.";
        return "protectHighlights";
    }
    if (metrics.haloRiskFraction > 0.10f && metrics.highlightFraction > 0.22f) {
        outReason = "Rendered metrics showed edge overshoot risk around brighter transitions, so the solver should back off highlight/local contrast pressure.";
        return "protectHighlights";
    }
    if (metrics.highlightGrayRisk > 0.48f &&
        metrics.highlightBandFraction > 0.16f &&
        metrics.clippedFraction < 0.018f) {
        outReason = "Rendered metrics showed broad highlights flattening toward gray, so the solver should try restoring highlight brightness separation.";
        return "addContrast";
    }
    if (metrics.localContrastPeak > 0.86f && metrics.haloRiskFraction > 0.06f) {
        outReason = "Rendered local metrics showed very steep regional contrast with edge-risk pressure, so the solver should back off local highlight/contrast shaping.";
        return "protectHighlights";
    }
    if (metrics.shadowTextureRisk > (darkIntent ? 0.78f : 0.66f) &&
        metrics.highlightFraction < 0.34f &&
        metrics.localHighlightPressure < 0.62f &&
        metrics.clippedFraction < 0.010f &&
        (metrics.shadowFraction > 0.24f ||
         metrics.localShadowPressure > 0.42f ||
         metrics.centerShadowFraction > 0.34f)) {
        outReason = "Rendered metrics showed shadow texture/noise pressure without matching highlight trouble, so the solver should try a cleaner-shadow refinement instead of simply lifting more.";
        return "cleanShadows";
    }
    if (!darkIntent &&
        metrics.edgeContrast > 0.0f &&
        metrics.edgeContrast < 0.24f &&
        metrics.contrastSpan > 0.32f &&
        metrics.localLumaSpread > 0.12f &&
        metrics.shadowTextureRisk < 0.32f &&
        metrics.highlightFraction < 0.34f &&
        metrics.clippedFraction < 0.006f &&
        metrics.lowSaturationFraction < 0.84f) {
        outReason = "Rendered metrics showed safe tones but subdued fine separation, so the solver should try a texture-preserving cleanup balance.";
        return "preserveTexture";
    }
    if (!darkIntent &&
        metrics.centerMeanLuma > 0.0f &&
        metrics.centerMeanLuma < 0.16f &&
        metrics.centerShadowFraction > 0.52f &&
        metrics.localHighlightPressure < 0.58f) {
        outReason = "Rendered local metrics showed the center region buried in shadows without matching highlight pressure, so the solver should try a damped shadow-opening refinement.";
        return "openShadows";
    }
    if (!darkIntent && metrics.medianLuma < 0.18f && metrics.highlightFraction < 0.22f && metrics.clippedFraction < 0.008f) {
        outReason = "Rendered metrics showed the selected result landing too dark without highlight pressure, so the solver should try brighter mids.";
        return "brightenMids";
    }
    if (metrics.localShadowPressure > (darkIntent ? 0.88f : 0.78f) &&
        metrics.localHighlightPressure < 0.62f &&
        metrics.clippedFraction < 0.010f) {
        outReason = "Rendered local metrics showed a tile with heavy shadow crowding, so the solver should try opening shadows without making the whole image flat.";
        return "openShadows";
    }
    if (metrics.shadowFraction > (darkIntent ? 0.72f : 0.62f) && metrics.medianLuma < (darkIntent ? 0.14f : 0.24f)) {
        outReason = "Rendered metrics showed heavy shadow crowding, so the solver should try a damped shadow-opening refinement.";
        return "openShadows";
    }
    if ((punchyIntent || metrics.medianLuma >= 0.20f) &&
        metrics.localLumaSpread < 0.14f &&
        metrics.localContrastPeak < 0.32f &&
        metrics.highlightFraction < 0.30f &&
        metrics.clippedFraction < 0.006f &&
        metrics.lowSaturationFraction < 0.86f) {
        outReason = "Rendered local metrics showed low regional separation without clipping pressure, so the solver should try a modest contrast refinement.";
        return "addContrast";
    }
    if ((punchyIntent || metrics.medianLuma >= 0.20f) &&
        metrics.contrastSpan < 0.24f &&
        metrics.highlightFraction < 0.34f &&
        metrics.clippedFraction < 0.006f &&
        metrics.lowSaturationFraction < 0.86f) {
        outReason = "Rendered metrics showed low separation without clipping pressure, so the solver should try a modest contrast refinement.";
        return "addContrast";
    }

    outReason.clear();
    return {};
}

} // namespace Stack::Editor::DevelopRenderedCandidateScoring
