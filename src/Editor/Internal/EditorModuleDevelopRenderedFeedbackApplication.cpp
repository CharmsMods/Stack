#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackApplication.h"

#include "Editor/Internal/EditorModuleDevelopCandidateGuidance.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Stack::Editor::DevelopRenderedFeedback {

using namespace Stack::Editor::DevelopCandidateGuidance;
using namespace Stack::Editor::DevelopCandidateScoring;

namespace {

bool TryApplyRenderedEnsembleMergeToSolve(
    DevelopAutoCandidateSolveResult& result,
    const nlohmann::json& previousToneJson,
    const DevelopAutoCandidateSolve& selectedCandidate) {
    if (!previousToneJson.value("autoCandidateRenderedEnsembleMergeSuggested", false)) {
        return false;
    }

    const nlohmann::json ensembleIdsJson =
        previousToneJson.value("autoCandidateRenderedEnsembleMergeIds", nlohmann::json::array());
    const nlohmann::json ensembleScoresJson =
        previousToneJson.value("autoCandidateRenderedEnsembleMergeScores", nlohmann::json::array());
    std::vector<std::string> ensembleIds;
    if (ensembleIdsJson.is_array()) {
        for (const nlohmann::json& idJson : ensembleIdsJson) {
            const std::string candidateId = idJson.is_string() ? idJson.get<std::string>() : std::string();
            if (!candidateId.empty() &&
                std::find(ensembleIds.begin(), ensembleIds.end(), candidateId) == ensembleIds.end()) {
                ensembleIds.push_back(candidateId);
                if (ensembleIds.size() >= 3) {
                    break;
                }
            }
        }
    }
    if (ensembleIds.size() < 3) {
        return false;
    }

    auto findCandidateById = [&](const std::string& candidateId) {
        return std::find_if(
            result.candidates.begin(),
            result.candidates.end(),
            [&](const DevelopAutoCandidateSolve& candidate) {
                return candidate.id == candidateId && !candidate.rejected;
            });
    };
    auto renderedScoreForId = [&](const std::string& candidateId, std::size_t scoreIndex) {
        if (ensembleScoresJson.is_array() &&
            scoreIndex < ensembleScoresJson.size() &&
            ensembleScoresJson[scoreIndex].is_number()) {
            return ensembleScoresJson[scoreIndex].get<float>();
        }
        float renderedScore = -1.0f;
        if (TryReadRenderedCandidateScore(
                previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
                candidateId,
                renderedScore)) {
            return renderedScore;
        }
        return -1.0f;
    };

    auto firstIt = findCandidateById(ensembleIds[0]);
    auto secondIt = findCandidateById(ensembleIds[1]);
    auto thirdIt = findCandidateById(ensembleIds[2]);
    if (firstIt == result.candidates.end() ||
        secondIt == result.candidates.end() ||
        thirdIt == result.candidates.end()) {
        return false;
    }

    const float firstRenderedScore = renderedScoreForId(ensembleIds[0], 0);
    const float secondRenderedScore = renderedScoreForId(ensembleIds[1], 1);
    const float thirdRenderedScore = renderedScoreForId(ensembleIds[2], 2);
    const float maxRenderedScore =
        std::max(firstRenderedScore, std::max(secondRenderedScore, thirdRenderedScore));
    const float minRenderedScore =
        std::min(firstRenderedScore, std::min(secondRenderedScore, thirdRenderedScore));
    const float scoreSpread =
        previousToneJson.value(
            "autoCandidateRenderedEnsembleMergeScoreSpread",
            maxRenderedScore - minRenderedScore);
    const float metricSpread =
        previousToneJson.value("autoCandidateRenderedEnsembleMergeMetricSpread", 0.0f);
    const float firstSecondDistance =
        DevelopAutoCandidateDistance(firstIt->guidance, secondIt->guidance);
    const float firstThirdDistance =
        DevelopAutoCandidateDistance(firstIt->guidance, thirdIt->guidance);
    const float secondThirdDistance =
        DevelopAutoCandidateDistance(secondIt->guidance, thirdIt->guidance);
    const float averageGuidanceDistance =
        (firstSecondDistance + firstThirdDistance + secondThirdDistance) / 3.0f;
    const int distinctPairCount =
        (firstSecondDistance >= 0.13f ? 1 : 0) +
        (firstThirdDistance >= 0.13f ? 1 : 0) +
        (secondThirdDistance >= 0.13f ? 1 : 0);

    const bool ensembleScoresAreStrong =
        firstRenderedScore >= 0.60f &&
        secondRenderedScore >= 0.56f &&
        thirdRenderedScore >= 0.52f &&
        scoreSpread <= 0.22f;
    const bool ensembleIsMeaningfullyDifferent =
        metricSpread >= 0.10f ||
        averageGuidanceDistance >= 0.16f ||
        distinctPairCount >= 2;
    if (!ensembleScoresAreStrong || !ensembleIsMeaningfullyDifferent) {
        return false;
    }

    const DevelopAutoCandidateSolve firstCandidate = *firstIt;
    const DevelopAutoCandidateSolve secondCandidate = *secondIt;
    const DevelopAutoCandidateSolve thirdCandidate = *thirdIt;
    if (firstCandidate.whiteBalanceProbe ||
        secondCandidate.whiteBalanceProbe ||
        thirdCandidate.whiteBalanceProbe) {
        return false;
    }
    float firstWeight = std::max(0.01f, firstRenderedScore);
    float secondWeight = std::max(0.01f, secondRenderedScore);
    float thirdWeight = std::max(0.01f, thirdRenderedScore);
    const float initialWeightSum = firstWeight + secondWeight + thirdWeight;
    firstWeight = std::clamp(firstWeight / initialWeightSum, 0.22f, 0.46f);
    secondWeight = std::clamp(secondWeight / initialWeightSum, 0.22f, 0.46f);
    thirdWeight = std::clamp(thirdWeight / initialWeightSum, 0.22f, 0.46f);
    const float clampedWeightSum = firstWeight + secondWeight + thirdWeight;
    firstWeight /= clampedWeightSum;
    secondWeight /= clampedWeightSum;
    thirdWeight /= clampedWeightSum;

    DevelopAutoCandidateSolve merged;
    merged.id = "renderedFeedbackEnsembleMerge";
    merged.label = "Rendered Ensemble Merge";
    merged.reason =
        "Merged three strong, distinct rendered survivors in authored settings space so the next solve can reconcile a broader intent set without blending final pixels.";
    merged.guidance = BlendDevelopAutoCandidateGuidance(
        firstCandidate.guidance,
        secondCandidate.guidance,
        thirdCandidate.guidance,
        firstWeight,
        secondWeight,
        thirdWeight);
    merged.score = std::min(
        1.0f,
        std::max(firstCandidate.score, std::max(secondCandidate.score, thirdCandidate.score)) + 0.022f);
    result.candidates.push_back(merged);
    result.selectionSource = "renderedMetricsEnsembleMerge";
    result.authoredGuidance = merged.guidance;
    result.selectedId = merged.id;
    result.selectedLabel = merged.label;
    result.selectedScore = merged.score;
    ClearDevelopResultWhiteBalanceProbe(result);
    result.mergeApplied = true;
    result.mergeFirstId = firstCandidate.id;
    result.mergeSecondId = secondCandidate.id;
    result.mergeThirdId = thirdCandidate.id;
    result.mergeFirstWeight = firstWeight;
    result.mergeSecondWeight = secondWeight;
    result.mergeThirdWeight = thirdWeight;
    result.renderedFeedbackAction = "merged";
    result.renderedFeedbackRevisionStage =
        DevelopRenderedRevisionStageForGuidanceDelta(
            selectedCandidate.guidance,
            merged.guidance,
            merged.id);
    result.renderedFeedbackRevisionReason =
        DevelopRenderedRevisionStageReason(
            result.renderedFeedbackRevisionStage,
            "Rendered ensemble merge affects multiple authored stages; the next pass should validate the broader merged intent against actual rendered output.");
    return true;
}

} // namespace

bool ApplyRenderedCandidateFeedbackToSolve(
    DevelopAutoCandidateSolveResult& result,
    const nlohmann::json& previousToneJson,
    std::uint64_t preliminaryFingerprint) {
    constexpr float kMinimumRenderedScore = 0.48f;

    if (!previousToneJson.is_object() || preliminaryFingerprint == 0) {
        return false;
    }
    auto stopWithoutApplying = [&](std::string reason) {
        result.renderedFeedbackStopReason = std::move(reason);
        result.renderedFeedbackStopIsConverged =
            IsDevelopRenderedFeedbackStopConvergedReason(result.renderedFeedbackStopReason);
        return false;
    };

    const std::string metricsStatus =
        previousToneJson.value("autoCandidateRenderMetricsStatus", std::string());
    if (metricsStatus != "ready" && metricsStatus != "partial") {
        return stopWithoutApplying("renderedMetricsNotReady");
    }
    const std::uint64_t renderedFingerprint =
        previousToneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0));
    const std::string bestId =
        previousToneJson.value("autoCandidateRenderedBestId", std::string());
    const float bestScore =
        previousToneJson.value("autoCandidateRenderedBestScore", -1.0f);
    const std::string bestRelativeStatus =
        previousToneJson.value("autoCandidateRenderedBestRelativeStatus", std::string());
    const float bestRelativeRepairBonus =
        previousToneJson.value("autoCandidateRenderedBestRelativeRepairBonus", 0.0f);
    const float bestRelativeDistanceBonus =
        previousToneJson.value("autoCandidateRenderedBestRelativeDistanceBonus", 0.0f);
    const float bestRelativeRegressionPenalty =
        previousToneJson.value("autoCandidateRenderedBestRelativeRegressionPenalty", 0.0f);
    const std::string refineIntent =
        previousToneJson.value("autoCandidateRenderedRefineIntent", std::string());
    const std::string requestedRevisionStage =
        previousToneJson.value("autoCandidateRenderedRevisionStage", std::string());
    const std::string stageBoundarySignal =
        previousToneJson.value("autoCandidateRenderedStageBoundarySignal", std::string());
    auto resolveRenderedFeedbackStage = [&](const std::string& fallbackStage) {
        if (stageBoundarySignal == "finishToneOnly" && requestedRevisionStage == "finishTone") {
            return std::string("finishTone");
        }
        return fallbackStage;
    };
    auto resolveRenderedFeedbackReason = [&](const std::string& stage, const std::string& fallbackReason) {
        if (stageBoundarySignal == "finishToneOnly" && stage == "finishTone") {
            const std::string requestedReason =
                previousToneJson.value("autoCandidateRenderedRevisionReason", std::string());
            if (!requestedReason.empty()) {
                return requestedReason;
            }
        }
        return DevelopRenderedRevisionStageReason(stage, fallbackReason);
    };
    const bool hasRefineIntent =
        refineIntent == "brightenMids" ||
        refineIntent == "openShadows" ||
        refineIntent == "protectHighlights" ||
        refineIntent == "addContrast" ||
        refineIntent == "cleanShadows" ||
        refineIntent == "preserveTexture";
    const std::uint64_t previousSolveFingerprint =
        previousToneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
    const bool renderedMetricsMatchCurrentSolve = renderedFingerprint == preliminaryFingerprint;
    const bool renderedMetricsMatchRefineBase =
        hasRefineIntent &&
        previousSolveFingerprint != 0 &&
        renderedFingerprint == previousSolveFingerprint;
    const bool renderedMetricsMatchPreviousSolve =
        previousSolveFingerprint != 0 &&
        renderedFingerprint == previousSolveFingerprint;
    if (!renderedMetricsMatchCurrentSolve &&
        !renderedMetricsMatchRefineBase &&
        !renderedMetricsMatchPreviousSolve) {
        return stopWithoutApplying("renderedMetricsForPreviousSolve");
    }
    if (previousToneJson.value(
            "autoCandidateRenderedFeedbackAppliedFingerprint",
            static_cast<std::uint64_t>(0)) == renderedFingerprint) {
        return stopWithoutApplying("renderedFeedbackAlreadyApplied");
    }

    const int previousPass = previousToneJson.value("autoCandidateRenderedFeedbackPass", 0);
    const DevelopConvergenceAdmissionPolicy admissionPolicy =
        ResolveDevelopConvergenceAdmissionPolicy(previousToneJson, previousPass, hasRefineIntent);
    result.renderedFeedbackAdmissionBaseMinimumImprovement =
        admissionPolicy.baseMinimumImprovement;
    result.renderedFeedbackAdmissionMinimumImprovement =
        admissionPolicy.minimumImprovement;
    result.renderedFeedbackAdmissionTightened = admissionPolicy.tightened;
    result.renderedFeedbackAdmissionReason = admissionPolicy.reason;
    result.renderedFeedbackAdmissionEvidenceState = admissionPolicy.evidenceState;
    result.renderedFeedbackAdmissionEvidenceDecision = admissionPolicy.evidenceDecision;
    result.renderedFeedbackAdmissionEvidencePass = admissionPolicy.evidencePass;

    if (previousPass >= kDevelopRenderedFeedbackMaxPasses) {
        return stopWithoutApplying("renderedFeedbackPassLimit");
    }

    if ((bestId.empty() || bestScore < kMinimumRenderedScore) && !hasRefineIntent) {
        return stopWithoutApplying(bestId.empty() ? "noRenderedBestCandidate" : "renderedBestBelowQualityFloor");
    }

    float selectedRenderedScore = -1.0f;
    const bool selectedRendered =
        TryReadRenderedCandidateScore(
            previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
            result.selectedId,
            selectedRenderedScore);
    result.renderedFeedbackImprovement =
        selectedRendered ? (bestScore - selectedRenderedScore) : -1.0f;
    if (!hasRefineIntent && previousPass > 0 && !selectedRendered) {
        const std::string previousAction =
            previousToneJson.value("autoCandidateRenderedFeedbackAction", std::string());
        const std::string previousBestId =
            previousToneJson.value("autoCandidateRenderedFeedbackBestId", std::string());
        if (previousAction == "adopted" && previousBestId == bestId) {
            return stopWithoutApplying("renderedAdoptionNoFurtherGain");
        }
    }

    EditorRenderWorker::DevelopCandidateRenderMetrics currentSelectedMetrics;
    float currentSelectedMetricScore = -1.0f;
    const bool currentSelectedMetricsValid =
        TryReadRenderedCandidateMetrics(
            previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
            result.selectedId,
            currentSelectedMetrics,
            currentSelectedMetricScore);
    EditorRenderWorker::DevelopCandidateRenderMetrics currentBestMetrics;
    float currentBestMetricScore = -1.0f;
    const bool currentBestMetricsValid =
        TryReadRenderedCandidateMetrics(
            previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
            bestId,
            currentBestMetrics,
            currentBestMetricScore);
    EditorRenderWorker::DevelopCandidateRenderMetrics previousBestMetrics;
    float previousBestMetricScore = -1.0f;
    std::string previousBestMetricId;
    const bool previousBestMetricsValid =
        TryReadLastRenderedHistoryMetrics(
            previousToneJson,
            renderedFingerprint,
            previousBestMetrics,
            previousBestMetricScore,
            previousBestMetricId);
    if (currentBestMetricsValid && previousBestMetricsValid && previousPass > 0) {
        result.renderedFeedbackStabilityDistance =
            EditorRenderWorker::CompareDevelopCandidateRenderMetrics(currentBestMetrics, previousBestMetrics);
        result.renderedFeedbackStabilityScoreDelta =
            std::fabs(bestScore - previousBestMetricScore);
        result.renderedFeedbackStabilityReferenceId = previousBestMetricId;
        const bool renderedStateStable =
            result.renderedFeedbackStabilityDistance < 0.045f &&
            result.renderedFeedbackStabilityScoreDelta < 0.020f;
        const bool noUsefulRenderedImprovement =
            !selectedRendered ||
            bestId == result.selectedId ||
            bestScore < selectedRenderedScore + admissionPolicy.minimumImprovement;
        if (renderedStateStable && noUsefulRenderedImprovement) {
            return stopWithoutApplying("renderedMetricsStable");
        }
    }
    if (currentBestMetricsValid) {
        const DevelopRenderedFeedbackTrend trend =
            EvaluateDevelopRenderedFeedbackTrend(
                previousToneJson,
                renderedFingerprint,
                bestId,
                bestScore,
                currentBestMetrics,
                selectedRenderedScore,
                selectedRendered,
                previousPass,
                hasRefineIntent);
        result.renderedFeedbackTrendHistoryCount = trend.historyCount;
        result.renderedFeedbackTrendSameBestCount = trend.sameBestCount;
        result.renderedFeedbackTrendScoreSpread = trend.scoreSpread;
        result.renderedFeedbackTrendNearestDistance = trend.nearestMetricDistance;
        result.renderedFeedbackTrendReferenceId = trend.referenceId;
        if (!trend.stopReason.empty()) {
            return stopWithoutApplying(trend.stopReason);
        }
    }
    if (hasRefineIntent && currentSelectedMetricsValid) {
        const DevelopRenderedMonotonicGuardDecision monotonicGuard =
            EvaluateRenderedRefineMonotonicGuard(
                previousToneJson,
                renderedFingerprint,
                refineIntent,
                currentSelectedMetrics,
                currentSelectedMetricScore,
                currentSelectedMetricScore >= 0.0f,
                previousPass);
        result.renderedFeedbackMonotonicMetric = monotonicGuard.metric;
        result.renderedFeedbackMonotonicPreviousValue = monotonicGuard.previousValue;
        result.renderedFeedbackMonotonicCurrentValue = monotonicGuard.currentValue;
        result.renderedFeedbackMonotonicReferenceId = monotonicGuard.referenceId;
        if (monotonicGuard.stop) {
            return stopWithoutApplying(monotonicGuard.reason);
        }
    }

    if (!hasRefineIntent &&
        bestId == result.selectedId) {
        return stopWithoutApplying("selectedCandidateStillBest");
    }
    if (!hasRefineIntent &&
        WouldRenderedFeedbackReverseRecentAdoption(previousToneJson, result.selectedId, bestId)) {
        return stopWithoutApplying("wouldReverseRecentRenderedAdoption");
    }
    if (!hasRefineIntent &&
        selectedRendered &&
        bestScore < selectedRenderedScore + admissionPolicy.minimumImprovement) {
        const bool clearedBaseButNotEvidenceThreshold =
            admissionPolicy.tightened &&
            bestScore >= selectedRenderedScore + admissionPolicy.baseMinimumImprovement;
        return stopWithoutApplying(
            clearedBaseButNotEvidenceThreshold
                ? "convergenceAdmissionNoMeaningfulImprovement"
                : "noMeaningfulRenderedImprovement");
    }
    if (!hasRefineIntent &&
        selectedRendered &&
        (bestRelativeStatus == "regressedAgainstSelected" ||
         bestRelativeStatus == "missedActiveRepair") &&
        bestRelativeRegressionPenalty >
            bestRelativeRepairBonus + bestRelativeDistanceBonus + 0.012f &&
        bestScore < selectedRenderedScore + 0.055f) {
        return stopWithoutApplying("renderedBestRelativeRegression");
    }
    const std::string repeatedChoiceStopReason =
        !hasRefineIntent
            ? RepeatedRenderedChoiceStopReason(
                previousToneJson,
                result.selectedId,
                bestId,
                selectedRenderedScore,
                selectedRendered,
                bestScore)
            : std::string();
    if (!repeatedChoiceStopReason.empty()) {
        return stopWithoutApplying(repeatedChoiceStopReason);
    }
    if (hasRefineIntent &&
        WouldRepeatUnhelpfulRenderedRefinement(
            previousToneJson,
            refineIntent,
            selectedRenderedScore,
            selectedRendered)) {
        return stopWithoutApplying("renderedRefineDidNotImprove");
    }

    auto selectedIt = std::find_if(
        result.candidates.begin(),
        result.candidates.end(),
        [&result](const DevelopAutoCandidateSolve& candidate) {
            return candidate.id == result.selectedId && !candidate.rejected;
        });
    if (selectedIt == result.candidates.end()) {
        return stopWithoutApplying("selectedCandidateUnavailableForRenderedFeedback");
    }

    DevelopAutoCandidateSolve bestCandidate;
    if (!hasRefineIntent) {
        auto bestIt = std::find_if(
            result.candidates.begin(),
            result.candidates.end(),
            [&bestId](const DevelopAutoCandidateSolve& candidate) {
                return candidate.id == bestId && !candidate.rejected;
            });
        if (bestIt == result.candidates.end()) {
            return stopWithoutApplying("renderedBestCandidateUnavailableForSolve");
        }
        bestCandidate = *bestIt;
    }

    result.renderedFeedbackApplied = true;
    result.renderedFeedbackSourceFingerprint = renderedFingerprint;
    result.renderedFeedbackPass = previousPass + 1;
    result.renderedFeedbackPreviousSelectedId = result.selectedId;
    result.renderedFeedbackPreviousSelectedScore =
        selectedRendered ? selectedRenderedScore : result.selectedScore;
    result.renderedFeedbackBestId = bestId;
    result.renderedFeedbackBestScore = bestScore;

    const DevelopAutoCandidateSolve selectedCandidate = *selectedIt;
    if (hasRefineIntent) {
        const std::string preferredRenderedCandidateId =
            PreferredRenderedRefineCandidateId(refineIntent);
        if (!preferredRenderedCandidateId.empty()) {
            auto renderedCandidateIt = std::find_if(
                result.candidates.begin(),
                result.candidates.end(),
                [&](const DevelopAutoCandidateSolve& candidate) {
                    return candidate.id == preferredRenderedCandidateId && !candidate.rejected;
                });
            if (renderedCandidateIt != result.candidates.end()) {
                result.selectionSource = "renderedMetricsRefine";
                result.authoredGuidance = renderedCandidateIt->guidance;
                result.selectedId = renderedCandidateIt->id;
                result.selectedLabel = renderedCandidateIt->label;
                result.selectedScore = std::min(1.0f, std::max(renderedCandidateIt->score, selectedCandidate.score + 0.010f));
                SetDevelopResultWhiteBalanceProbe(result, *renderedCandidateIt);
                result.mergeApplied = false;
                result.mergeFirstId.clear();
                result.mergeSecondId.clear();
                result.mergeThirdId.clear();
                result.mergeFirstWeight = 1.0f;
                result.mergeSecondWeight = 0.0f;
                result.mergeThirdWeight = 0.0f;
                result.renderedFeedbackAction = "refined";
                result.renderedFeedbackRefineIntent = refineIntent;
                result.renderedFeedbackRefineReason = renderedCandidateIt->reason;
                result.renderedFeedbackRevisionStage =
                    resolveRenderedFeedbackStage(
                        DevelopRenderedRevisionStageForRefineIntent(refineIntent));
                result.renderedFeedbackRevisionReason =
                    resolveRenderedFeedbackReason(
                        result.renderedFeedbackRevisionStage,
                        renderedCandidateIt->reason);
                return true;
            }
        }

        // Refine the current rendered winner from measured output mismatch, with damped moves only.
        DevelopAutoCandidateSolve refined;
        refined.id = "renderedFeedbackRefine";
        refined.label = "Rendered Feedback Refine";
        refined.reason = previousToneJson.value(
            "autoCandidateRenderedRefineReason",
            std::string("Adjusted the current selected candidate from rendered metrics in authored settings space."));
        refined.guidance = selectedCandidate.guidance;
        refined.whiteBalanceProbe = selectedCandidate.whiteBalanceProbe;
        refined.whiteBalanceMode = selectedCandidate.whiteBalanceMode;
        if (refineIntent == "brightenMids") {
            refined.label = "Rendered Brightness Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                0.08f,
                0.06f,
                0.10f,
                0.04f,
                0.00f,
                -0.03f);
        } else if (refineIntent == "openShadows") {
            refined.label = "Rendered Shadow Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                0.04f,
                0.10f,
                0.16f,
                0.06f,
                -0.02f,
                -0.04f);
        } else if (refineIntent == "protectHighlights") {
            refined.label = "Rendered Highlight Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                -0.08f,
                0.16f,
                0.04f,
                0.24f,
                -0.04f,
                -0.08f);
        } else if (refineIntent == "addContrast") {
            refined.label = "Rendered Contrast Refine";
            refined.guidance = AdjustDevelopAutoCandidateGuidance(
                selectedCandidate.guidance,
                0.00f,
                -0.05f,
                -0.05f,
                0.02f,
                0.06f,
                0.16f);
        }
        refined.score = std::min(1.0f, selectedCandidate.score + 0.012f);
        result.candidates.push_back(refined);
        result.selectionSource = "renderedMetricsRefine";
        result.authoredGuidance = refined.guidance;
        result.selectedId = refined.id;
        result.selectedLabel = refined.label;
        result.selectedScore = refined.score;
        SetDevelopResultWhiteBalanceProbe(result, refined);
        result.mergeApplied = false;
        result.mergeFirstId.clear();
        result.mergeSecondId.clear();
        result.mergeThirdId.clear();
        result.mergeFirstWeight = 1.0f;
        result.mergeSecondWeight = 0.0f;
        result.mergeThirdWeight = 0.0f;
        result.renderedFeedbackAction = "refined";
        result.renderedFeedbackRefineIntent = refineIntent;
        result.renderedFeedbackRefineReason = refined.reason;
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(
                DevelopRenderedRevisionStageForRefineIntent(refineIntent));
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                refined.reason);
        return true;
    }

    if (!hasRefineIntent &&
        TryApplyRenderedEnsembleMergeToSolve(result, previousToneJson, selectedCandidate)) {
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(result.renderedFeedbackRevisionStage);
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                result.renderedFeedbackRevisionReason);
        return true;
    }

    if (!hasRefineIntent &&
        previousToneJson.value("autoCandidateRenderedMergeSuggested", false)) {
        const std::string mergeFirstId =
            previousToneJson.value("autoCandidateRenderedMergeFirstId", std::string());
        const std::string mergeSecondId =
            previousToneJson.value("autoCandidateRenderedMergeSecondId", std::string());
        if (!mergeFirstId.empty() && !mergeSecondId.empty() && mergeFirstId != mergeSecondId) {
            auto findCandidateById = [&](const std::string& candidateId) {
                return std::find_if(
                    result.candidates.begin(),
                    result.candidates.end(),
                    [&](const DevelopAutoCandidateSolve& candidate) {
                        return candidate.id == candidateId && !candidate.rejected;
                    });
            };

            auto firstIt = findCandidateById(mergeFirstId);
            auto secondIt = findCandidateById(mergeSecondId);
            if (firstIt != result.candidates.end() &&
                secondIt != result.candidates.end() &&
                !firstIt->whiteBalanceProbe &&
                !secondIt->whiteBalanceProbe) {
                float firstRenderedScore =
                    previousToneJson.value("autoCandidateRenderedMergeFirstScore", -1.0f);
                float secondRenderedScore =
                    previousToneJson.value("autoCandidateRenderedMergeSecondScore", -1.0f);
                float renderedScoreFromSolves = -1.0f;
                if (firstRenderedScore < 0.0f &&
                    TryReadRenderedCandidateScore(
                        previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
                        mergeFirstId,
                        renderedScoreFromSolves)) {
                    firstRenderedScore = renderedScoreFromSolves;
                }
                renderedScoreFromSolves = -1.0f;
                if (secondRenderedScore < 0.0f &&
                    TryReadRenderedCandidateScore(
                        previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array()),
                        mergeSecondId,
                        renderedScoreFromSolves)) {
                    secondRenderedScore = renderedScoreFromSolves;
                }

                const float renderedScoreGap =
                    std::fabs(firstRenderedScore - secondRenderedScore);
                const float candidateDistance =
                    DevelopAutoCandidateDistance(firstIt->guidance, secondIt->guidance);
                const float metricDistance =
                    previousToneJson.value("autoCandidateRenderedMergeMetricDistance", 0.0f);
                const bool pairScoresAreStrong =
                    firstRenderedScore >= 0.58f &&
                    secondRenderedScore >= 0.54f &&
                    renderedScoreGap <= 0.16f;
                const bool pairIsMeaningfullyDifferent =
                    candidateDistance >= 0.16f ||
                    metricDistance >= 0.11f;
                if (pairScoresAreStrong && pairIsMeaningfullyDifferent) {
                    const DevelopAutoCandidateSolve firstCandidate = *firstIt;
                    const DevelopAutoCandidateSolve secondCandidate = *secondIt;
                    const float firstWeight = std::clamp(
                        0.50f + (firstRenderedScore - secondRenderedScore) * 0.65f,
                        0.42f,
                        0.62f);
                    DevelopAutoCandidateSolve merged;
                    merged.id = "renderedFeedbackPairMerge";
                    merged.label = "Rendered Pair Merge";
                    merged.reason =
                        "Merged two strong, distinct rendered survivors in authored settings space so the next solve can reconcile their intent instead of blending pixels.";
                    merged.guidance = BlendDevelopAutoCandidateGuidance(
                        firstCandidate.guidance,
                        secondCandidate.guidance,
                        firstWeight);
                    merged.score = std::min(
                        1.0f,
                        std::max(firstCandidate.score, secondCandidate.score) + 0.018f);
                    result.candidates.push_back(merged);
                    result.selectionSource = "renderedMetricsPairMerge";
                    result.authoredGuidance = merged.guidance;
                    result.selectedId = merged.id;
                    result.selectedLabel = merged.label;
                    result.selectedScore = merged.score;
                    ClearDevelopResultWhiteBalanceProbe(result);
                    result.mergeApplied = true;
                    result.mergeFirstId = firstCandidate.id;
                    result.mergeSecondId = secondCandidate.id;
                    result.mergeThirdId.clear();
                    result.mergeFirstWeight = firstWeight;
                    result.mergeSecondWeight = 1.0f - firstWeight;
                    result.mergeThirdWeight = 0.0f;
                    result.renderedFeedbackAction = "merged";
                    result.renderedFeedbackRevisionStage =
                        resolveRenderedFeedbackStage(
                            DevelopRenderedRevisionStageForGuidanceDelta(
                                selectedCandidate.guidance,
                                merged.guidance,
                                merged.id));
                    result.renderedFeedbackRevisionReason =
                        resolveRenderedFeedbackReason(
                            result.renderedFeedbackRevisionStage,
                            "Rendered pair merge affects multiple authored stages; the next pass should validate the merged intent against actual rendered output.");
                    return true;
                }
            }
        }
    }

    const float renderedImprovement =
        selectedRendered ? (bestScore - selectedRenderedScore) : 1.0f;
    const float candidateDistance =
        DevelopAutoCandidateDistance(selectedCandidate.guidance, bestCandidate.guidance);
    const bool canMergeRenderedFeedback =
        selectedRendered &&
        !selectedCandidate.whiteBalanceProbe &&
        !bestCandidate.whiteBalanceProbe &&
        selectedRenderedScore >= 0.50f &&
        renderedImprovement <= 0.12f &&
        candidateDistance >= 0.10f;

    if (canMergeRenderedFeedback) {
        // Modest rendered wins are treated as a combined intent instead of a hard jump.
        const float selectedWeight = std::clamp(
            0.50f - renderedImprovement * 1.15f,
            0.36f,
            0.50f);
        DevelopAutoCandidateSolve merged;
        merged.id = "renderedFeedbackMerge";
        merged.label = "Rendered Feedback Merge";
        merged.reason =
            "Merged the current selected candidate and rendered-best survivor in authored settings space after rendered comparison.";
        merged.guidance = BlendDevelopAutoCandidateGuidance(
            selectedCandidate.guidance,
            bestCandidate.guidance,
            selectedWeight);
        merged.score = std::min(1.0f, std::max(selectedCandidate.score, bestCandidate.score) + 0.015f);
        result.candidates.push_back(merged);
        result.selectionSource = "renderedMetricsMerge";
        result.authoredGuidance = merged.guidance;
        result.selectedId = merged.id;
        result.selectedLabel = merged.label;
        result.selectedScore = merged.score;
        ClearDevelopResultWhiteBalanceProbe(result);
        result.mergeApplied = true;
        result.mergeFirstId = selectedCandidate.id;
        result.mergeSecondId = bestCandidate.id;
        result.mergeThirdId.clear();
        result.mergeFirstWeight = selectedWeight;
        result.mergeSecondWeight = 1.0f - selectedWeight;
        result.mergeThirdWeight = 0.0f;
        result.renderedFeedbackAction = "merged";
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(
                DevelopRenderedRevisionStageForGuidanceDelta(
                    selectedCandidate.guidance,
                    merged.guidance,
                    merged.id));
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                "Rendered selected-vs-best merge affects multiple authored stages; the next pass should validate the merged intent against actual rendered output.");
    } else {
        // Clear rendered wins still adopt the better authored candidate directly.
        result.selectionSource = "renderedMetrics";
        result.authoredGuidance = bestCandidate.guidance;
        result.selectedId = bestCandidate.id;
        result.selectedLabel = bestCandidate.label;
        result.selectedScore = bestCandidate.score;
        SetDevelopResultWhiteBalanceProbe(result, bestCandidate);
        result.renderedFeedbackAction = "adopted";
        result.renderedFeedbackRevisionStage =
            resolveRenderedFeedbackStage(
                DevelopRenderedRevisionStageForGuidanceDelta(
                    selectedCandidate.guidance,
                    bestCandidate.guidance,
                    bestCandidate.id));
        result.renderedFeedbackRevisionReason =
            resolveRenderedFeedbackReason(
                result.renderedFeedbackRevisionStage,
                "Rendered feedback adopted a stronger authored candidate; the next pass should validate the earliest changed stage first.");
        if (result.selectedId != "mergedAutoPick") {
            result.mergeApplied = false;
            result.mergeFirstId.clear();
            result.mergeSecondId.clear();
            result.mergeThirdId.clear();
            result.mergeFirstWeight = 1.0f;
            result.mergeSecondWeight = 0.0f;
            result.mergeThirdWeight = 0.0f;
        }
    }
    return true;
}
} // namespace Stack::Editor::DevelopRenderedFeedback
