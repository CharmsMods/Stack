#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleDevelopCandidateShared.h"
#include "Editor/Internal/EditorModuleDevelopRenderedCandidateScoring.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackRecords.h"

#include "Editor/Layers/ToneLayers.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace Stack::Editor::DevelopCandidate;
using namespace Stack::Editor::DevelopRenderedCandidateScoring;
using namespace Stack::Editor::DevelopRenderedFeedbackAnalysis;
using namespace Stack::Editor::DevelopRenderedFeedback;
using namespace Stack::Editor::DevelopRenderedFeedbackRecords;

namespace {

std::string RepeatedRenderedRefinementStopReason(
    const nlohmann::json& toneJson,
    const std::string& refineIntent,
    float selectedRenderScore,
    bool selectedRenderScoreValid) {
    if (refineIntent.empty()) {
        return {};
    }
    if (toneJson.value("autoCandidateRenderedFeedbackAction", std::string()) != "refined" ||
        toneJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) != refineIntent) {
        return {};
    }

    if (!selectedRenderScoreValid) {
        return toneJson.value("autoCandidateRenderedFeedbackPass", 0) > 0
            ? "renderedRefineRepeatedIntent"
            : std::string();
    }

    const float previousSelectedScore =
        toneJson.value("autoCandidateRenderedFeedbackPreviousSelectedScore", -1.0f);
    if (previousSelectedScore >= 0.0f && selectedRenderScore < previousSelectedScore + 0.025f) {
        return "renderedRefineDidNotImprove";
    }
    if (toneJson.value("autoCandidateRenderedFeedbackPass", 0) >= 2) {
        return "renderedRefineRepeatedIntent";
    }
    return {};
}

const EditorNodeGraph::Node* FindUpstreamRawSourceForDevelopNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& developNode) {
    const EditorNodeGraph::Link* rawInput =
        graph.FindInputLink(developNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}


} // namespace


std::string EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent,
    std::string& outReason) {
    return ResolveDevelopRenderedRefineIntent(metrics, intent, outReason);
}

std::string EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent) {
    return ClassifyDevelopRenderedCandidateDamage(metrics, intent);
}

float EditorModule::ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateMetrics,
    float candidateStandaloneScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedMetrics,
    float selectedScore,
    const std::string& activeRefineIntent,
    std::string& outStatus,
    std::string& outRepairMetric,
    float& outMetricDistance,
    float& outRepairDelta,
    float& outRepairBonus,
    float& outRegressionPenalty) {
    const DevelopRenderedRelativeComparison comparison =
        CompareDevelopRenderedCandidateToSelected(
            candidateMetrics,
            candidateStandaloneScore,
            selectedMetrics,
            selectedScore,
            activeRefineIntent,
            false);
    outStatus = comparison.status;
    outRepairMetric = comparison.repairMetric;
    outMetricDistance = comparison.metricDistance;
    outRepairDelta = comparison.repairDelta;
    outRepairBonus = comparison.repairBonus;
    outRegressionPenalty = comparison.regressionPenalty;
    return comparison.adjustedScore;
}

std::string EditorModule::ClassifyDevelopRenderedStageBoundaryForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestFinalMetrics,
    bool finalMetricsValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedPreFinishMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestPreFinishMetrics,
    bool preFinishMetricsValid,
    float& outFinalDistance,
    float& outPreFinishDistance) {
    return ClassifyDevelopRenderedStageBoundary(
        selectedFinalMetrics,
        bestFinalMetrics,
        finalMetricsValid,
        selectedPreFinishMetrics,
        bestPreFinishMetrics,
        preFinishMetricsValid,
        outFinalDistance,
        outPreFinishDistance);
}

bool EditorModule::ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativeFinalMetrics,
    bool candidatePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidatePreFinishMetrics,
    bool representativePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativePreFinishMetrics,
    float& outFinalDistance,
    float& outPreFinishDistance,
    bool& outPreFinishDistinct) {
    const DevelopRenderedDuplicateDecision decision =
        EvaluateDevelopRenderedCandidateDuplicate(
            candidateFinalMetrics,
            representativeFinalMetrics,
            candidatePreFinishValid,
            candidatePreFinishMetrics,
            representativePreFinishValid,
            representativePreFinishMetrics);
    outFinalDistance = decision.finalDistance;
    outPreFinishDistance = decision.preFinishDistance;
    outPreFinishDistinct = decision.preFinishDistinct;
    return decision.duplicate;
}


bool EditorModule::IsDevelopRenderedFeedbackStopConvergedReason(
    const std::string& stopReason) {
    return Stack::Editor::DevelopRenderedFeedback::IsDevelopRenderedFeedbackStopConvergedReason(stopReason);
}

bool EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation(
    const std::string& stopReason) {
    return IsDevelopRenderedFeedbackStopConvergedReason(stopReason);
}




void EditorModule::ApplyDevelopCandidateRenderFeedback(
    const std::vector<EditorRenderWorker::DevelopCandidateRenderResult>& results) {
    if (results.empty()) {
        return;
    }

    std::unordered_map<int, std::vector<const EditorRenderWorker::DevelopCandidateRenderResult*>> resultsByNode;
    for (const EditorRenderWorker::DevelopCandidateRenderResult& result : results) {
        if (result.developNodeId > 0 && result.solveFingerprint != 0) {
            resultsByNode[result.developNodeId].push_back(&result);
        }
    }
    if (resultsByNode.empty()) {
        return;
    }

    bool persistedStateChanged = false;
    for (const auto& entry : resultsByNode) {
        EditorNodeGraph::Node* node = m_NodeGraph.FindNode(entry.first);
        if (!node ||
            node->kind != EditorNodeGraph::NodeKind::RawDevelop ||
            !node->rawDevelop.integratedToneLayerJson.is_object()) {
            continue;
        }

        const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        const std::uint64_t currentInteractionSerial =
            GetRawDevelopInteractionSerial(node->id);
        bool hasCurrentInteractionResult = false;
        for (const EditorRenderWorker::DevelopCandidateRenderResult* result : entry.second) {
            if (result && result->rawDevelopInteractionSerial == currentInteractionSerial) {
                hasCurrentInteractionResult = true;
                break;
            }
        }
        if (!hasCurrentInteractionResult) {
            continue;
        }

        const auto interactionTimeIt = m_RawDevelopInteractionTimes.find(node->id);
        const double lastInteractionTime =
            interactionTimeIt != m_RawDevelopInteractionTimes.end()
                ? interactionTimeIt->second
                : -1.0;
        const DevelopCandidateFeedbackGateDecision gateDecision =
            ClassifyDevelopCandidateFeedbackGateForValidation(
                currentInteractionSerial,
                currentInteractionSerial,
                lastInteractionTime,
                now);
        if (gateDecision == DevelopCandidateFeedbackGateDecision::DeferRecentInteraction) {
            ScheduleDeferredDevelopCandidateFeedback(node->id, now);
            continue;
        }

        nlohmann::json toneJson = node->rawDevelop.integratedToneLayerJson;
        const std::uint64_t currentRevision = std::max<std::uint64_t>(1, GetNodeDirtyGeneration(node->id));
        const std::uint64_t currentFingerprint =
            toneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
        if (currentFingerprint == 0) {
            continue;
        }

        struct RenderedCandidateSummary {
            std::size_t jsonIndex = 0;
            std::string id;
            std::string label;
            std::uint64_t guidanceFingerprint = 0;
            float renderScore = -1.0f;
            float standaloneRenderScore = -1.0f;
            EditorRenderWorker::DevelopCandidateRenderMetrics metrics;
            EditorRenderWorker::DevelopCandidateRenderMetrics preFinishMetrics;
            bool preFinishValid = false;
            std::string activeRefineIntent;
            std::string relativeComparisonStatus = "standalone";
            std::string relativeRepairMetric;
            std::string relativeComparisonReason;
            float relativeMetricDistance = -1.0f;
            float relativeRepairDelta = 0.0f;
            float relativeRepairBonus = 0.0f;
            float relativeRegressionPenalty = 0.0f;
            float relativeDistanceBonus = 0.0f;
            bool damaged = false;
            std::string damageReason;
            bool duplicate = false;
            std::string duplicateOf;
            float duplicateFinalDistance = -1.0f;
            float duplicatePreFinishDistance = -1.0f;
            bool preFinishDistinctFromRepresentative = false;
            std::string preFinishDistinctRepresentativeId;
            float preFinishDistinctFinalDistance = -1.0f;
            float preFinishDistinctDistance = -1.0f;
        };

        nlohmann::json rendered = nlohmann::json::array();
        std::vector<RenderedCandidateSummary> renderedSummaries;
        int successCount = 0;
        int failureCount = 0;
        int preFinishSuccessCount = 0;
        int preFinishReuseCount = 0;
        int metricsReadbackDownsampledCount = 0;
        int preFinishMetricsReadbackDownsampledCount = 0;
        int metricReadbackMaxDimension = 0;
        int rawBaseFinalCacheHitCount = 0;
        int preFinishFinalCacheHitCount = 0;
        int observedRawBaseDirtyCount = 0;
        int observedScenePrepDirtyCount = 0;
        int observedFinishToneDirtyCount = 0;
        int observedUnknownDirtyCount = 0;
        int stageCacheValidationCount = 0;
        int stageCacheValidationMetCount = 0;
        int stageSchedulerCount = 0;
        int stageSchedulerExpectedRawBaseCount = 0;
        int stageSchedulerExpectedScenePrepCount = 0;
        int stageSchedulerExpectedFinishToneCount = 0;
        int stageSchedulerExpectedUnknownCount = 0;
        bool stageSchedulerOrderMonotonic = true;
        int lastStageSchedulerRank = -1;
        int activeStageRequestCount = 0;
        int stageReservedRequestCount = 0;
        int activeRefineIntentRequestCount = 0;
        int refineIntentReservedRequestCount = 0;
        int adaptiveBudgetExpandedRequestCount = 0;
        int adaptiveBudgetNarrowedRequestCount = 0;
        int adaptiveRenderBudget = static_cast<int>(kDefaultDevelopCandidateRenderRequestsPerNode);
        std::string adaptiveRenderBudgetReason = "default";
        std::string adaptiveRenderBudgetContinuationDecision;
        std::string adaptiveRenderBudgetConvergenceState;
        std::string adaptiveRenderBudgetConvergenceDecision;
        std::string adaptiveRenderBudgetConvergenceReason;
        double totalCandidateElapsedMs = 0.0;
        double totalFinalGraphMs = 0.0;
        double totalFinalReadbackMs = 0.0;
        double totalFinalAnalysisMs = 0.0;
        double totalPreFinishGraphMs = 0.0;
        double totalPreFinishReadbackMs = 0.0;
        double totalPreFinishAnalysisMs = 0.0;
        double slowestCandidateElapsedMs = 0.0;
        std::string slowestCandidateLabel;
        int damageCount = 0;
        int duplicateCount = 0;
        int preFinishDistinctSurvivorCount = 0;
        std::string bestCandidateId;
        std::string bestCandidateLabel;
        float bestRenderScore = -1.0f;
        EditorRenderWorker::DevelopCandidateRenderMetrics bestMetrics;
        bool bestMetricsValid = false;
        EditorRenderWorker::DevelopCandidateRenderMetrics bestPreFinishMetrics;
        bool bestPreFinishMetricsValid = false;
        bool renderedPairMergeSuggested = false;
        std::string renderedMergeFirstId;
        std::string renderedMergeFirstLabel;
        float renderedMergeFirstScore = -1.0f;
        std::string renderedMergeSecondId;
        std::string renderedMergeSecondLabel;
        float renderedMergeSecondScore = -1.0f;
        float renderedMergeMetricDistance = 0.0f;
        bool renderedEnsembleMergeSuggested = false;
        nlohmann::json renderedEnsembleMergeIds = nlohmann::json::array();
        nlohmann::json renderedEnsembleMergeLabels = nlohmann::json::array();
        nlohmann::json renderedEnsembleMergeScores = nlohmann::json::array();
        nlohmann::json renderedEnsembleMergeMetricDistances = nlohmann::json::object();
        float renderedEnsembleMergeMetricSpread = 0.0f;
        float renderedEnsembleMergeScoreSpread = 0.0f;
        const std::string selectedCandidateId =
            toneJson.value("autoCandidateSelectedId", std::string());
        float selectedRenderScore = -1.0f;
        bool selectedRenderScoreValid = false;
        float selectedStandaloneRenderScore = -1.0f;
        bool selectedStandaloneRenderScoreValid = false;
        EditorRenderWorker::DevelopCandidateRenderMetrics selectedMetrics;
        bool selectedMetricsValid = false;
        EditorRenderWorker::DevelopCandidateRenderMetrics selectedPreFinishMetrics;
        bool selectedPreFinishMetricsValid = false;
        std::string activeRenderedRepairIntent;
        for (const EditorRenderWorker::DevelopCandidateRenderResult* result : entry.second) {
            if (!result ||
                result->rawDevelopInteractionSerial != currentInteractionSerial ||
                result->dirtyGeneration != currentRevision ||
                result->solveFingerprint != currentFingerprint) {
                continue;
            }

            nlohmann::json item;
            item["id"] = result->candidateId;
            item["label"] = result->candidateLabel;
            item["success"] = result->success;
            item["solveScore"] = result->solveScore;
            item["guidanceFingerprint"] = result->guidanceFingerprint;
            item["candidateRevisionStage"] = result->candidateRevisionStage;
            item["activeRevisionStage"] = result->activeRevisionStage;
            item["activeRefineIntent"] = result->activeRefineIntent;
            item["stageSchedulerOrder"] = result->stageSchedulerOrder;
            item["stageSchedulerRank"] = result->stageSchedulerRank;
            item["adaptiveRenderBudgetVersion"] = result->adaptiveRenderBudgetVersion;
            item["adaptiveRenderBudget"] = result->adaptiveRenderBudget;
            item["adaptiveRenderBudgetReason"] = result->adaptiveRenderBudgetReason;
            item["adaptiveRenderBudgetContinuationDecision"] =
                result->adaptiveRenderBudgetContinuationDecision;
            item["adaptiveRenderBudgetExpanded"] = result->adaptiveRenderBudgetExpanded;
            item["adaptiveRenderBudgetNarrowed"] = result->adaptiveRenderBudgetNarrowed;
            item["adaptiveRenderBudgetConvergenceState"] =
                result->adaptiveRenderBudgetConvergenceState;
            item["adaptiveRenderBudgetConvergenceDecision"] =
                result->adaptiveRenderBudgetConvergenceDecision;
            item["adaptiveRenderBudgetConvergenceReason"] =
                result->adaptiveRenderBudgetConvergenceReason;
            item["stageSchedulerExpectedDirtyBoundary"] =
                result->stageSchedulerExpectedDirtyBoundary;
            item["stageSchedulerReason"] = result->stageSchedulerReason;
            item["activeStageMatch"] = result->activeStageMatch;
            item["stageReservedRequest"] = result->stageReservedRequest;
            item["activeRefineIntentMatch"] = result->activeRefineIntentMatch;
            item["refineIntentReservedRequest"] = result->refineIntentReservedRequest;
            item["width"] = result->width;
            item["height"] = result->height;
            item["preFinishSuccess"] = result->preFinishSuccess;
            item["preFinishWidth"] = result->preFinishWidth;
            item["preFinishHeight"] = result->preFinishHeight;
            item["preFinishReusedFromFinalRender"] = result->preFinishReusedFromFinalRender;
            item["metricReadbackMaxDimension"] = result->metricReadbackMaxDimension;
            item["metricsReadbackDownsampled"] = result->metricsReadbackDownsampled;
            item["preFinishMetricsReadbackDownsampled"] = result->preFinishMetricsReadbackDownsampled;
            item["rawBaseCacheHitDuringFinalRender"] = result->rawBaseCacheHitDuringFinalRender;
            item["preFinishCacheHitDuringFinalRender"] = result->preFinishCacheHitDuringFinalRender;
            item["rawDevelopInteractionSerial"] = result->rawDevelopInteractionSerial;
            item["timingVersion"] = "CandidateRenderTimingV1";
            item["totalElapsedMs"] = result->totalElapsedMs;
            item["finalGraphMs"] = result->finalGraphMs;
            item["finalReadbackMs"] = result->finalReadbackMs;
            item["finalAnalysisMs"] = result->finalAnalysisMs;
            item["preFinishGraphMs"] = result->preFinishGraphMs;
            item["preFinishReadbackMs"] = result->preFinishReadbackMs;
            item["preFinishAnalysisMs"] = result->preFinishAnalysisMs;
            totalCandidateElapsedMs += std::max(0.0f, result->totalElapsedMs);
            totalFinalGraphMs += std::max(0.0f, result->finalGraphMs);
            totalFinalReadbackMs += std::max(0.0f, result->finalReadbackMs);
            totalFinalAnalysisMs += std::max(0.0f, result->finalAnalysisMs);
            totalPreFinishGraphMs += std::max(0.0f, result->preFinishGraphMs);
            totalPreFinishReadbackMs += std::max(0.0f, result->preFinishReadbackMs);
            totalPreFinishAnalysisMs += std::max(0.0f, result->preFinishAnalysisMs);
            if (result->totalElapsedMs > slowestCandidateElapsedMs) {
                slowestCandidateElapsedMs = result->totalElapsedMs;
                slowestCandidateLabel = result->candidateLabel.empty()
                    ? result->candidateId
                    : result->candidateLabel;
            }
            const std::string observedDirtyBoundary = result->success
                ? DevelopObservedDirtyBoundaryFromCacheHits(
                    result->rawBaseCacheHitDuringFinalRender,
                    result->preFinishCacheHitDuringFinalRender)
                : std::string("unknown");
            const DevelopStageCacheValidation stageCacheValidation =
                result->success
                    ? EvaluateDevelopStageCacheValidation(
                        result->candidateRevisionStage,
                        result->rawBaseCacheHitDuringFinalRender,
                        result->preFinishCacheHitDuringFinalRender)
                    : DevelopStageCacheValidation{};
            item["observedDirtyBoundary"] = observedDirtyBoundary;
            item["stageCacheExpectedBoundary"] = stageCacheValidation.expectedBoundary;
            item["stageCacheExpectedRawBaseReuse"] = stageCacheValidation.expectedRawBaseReuse;
            item["stageCacheExpectedPreFinishReuse"] = stageCacheValidation.expectedPreFinishReuse;
            item["stageCacheExpectationEvaluated"] = stageCacheValidation.evaluated;
            item["stageCacheExpectationMet"] =
                stageCacheValidation.evaluated ? stageCacheValidation.met : true;
            item["stageCacheValidationStatus"] = stageCacheValidation.status;
            if (!stageCacheValidation.reason.empty()) {
                item["stageCacheValidationReason"] = stageCacheValidation.reason;
            }
            if (result->activeStageMatch) {
                ++activeStageRequestCount;
            }
            if (result->stageReservedRequest) {
                ++stageReservedRequestCount;
            }
            if (result->activeRefineIntentMatch) {
                ++activeRefineIntentRequestCount;
            }
            if (result->refineIntentReservedRequest) {
                ++refineIntentReservedRequestCount;
            }
            adaptiveRenderBudget = std::max(
                adaptiveRenderBudget,
                result->adaptiveRenderBudget);
            if (result->adaptiveRenderBudgetExpanded) {
                ++adaptiveBudgetExpandedRequestCount;
                if (adaptiveRenderBudgetReason == "default" ||
                    adaptiveRenderBudgetReason.empty()) {
                    adaptiveRenderBudgetReason = result->adaptiveRenderBudgetReason;
                }
            } else if (adaptiveRenderBudgetReason == "default" &&
                !result->adaptiveRenderBudgetReason.empty()) {
                adaptiveRenderBudgetReason = result->adaptiveRenderBudgetReason;
            }
            if (adaptiveRenderBudgetContinuationDecision.empty() &&
                !result->adaptiveRenderBudgetContinuationDecision.empty()) {
                adaptiveRenderBudgetContinuationDecision =
                    result->adaptiveRenderBudgetContinuationDecision;
            }
            if (result->adaptiveRenderBudgetNarrowed) {
                ++adaptiveBudgetNarrowedRequestCount;
                adaptiveRenderBudgetReason = result->adaptiveRenderBudgetReason;
            }
            if (adaptiveRenderBudgetConvergenceState.empty() &&
                !result->adaptiveRenderBudgetConvergenceState.empty()) {
                adaptiveRenderBudgetConvergenceState =
                    result->adaptiveRenderBudgetConvergenceState;
            }
            if (adaptiveRenderBudgetConvergenceDecision.empty() &&
                !result->adaptiveRenderBudgetConvergenceDecision.empty()) {
                adaptiveRenderBudgetConvergenceDecision =
                    result->adaptiveRenderBudgetConvergenceDecision;
            }
            if (adaptiveRenderBudgetConvergenceReason.empty() &&
                !result->adaptiveRenderBudgetConvergenceReason.empty()) {
                adaptiveRenderBudgetConvergenceReason =
                    result->adaptiveRenderBudgetConvergenceReason;
            }
            ++stageSchedulerCount;
            if (result->stageSchedulerRank < lastStageSchedulerRank) {
                stageSchedulerOrderMonotonic = false;
            }
            lastStageSchedulerRank = result->stageSchedulerRank;
            if (result->stageSchedulerExpectedDirtyBoundary == "rawBase") {
                ++stageSchedulerExpectedRawBaseCount;
            } else if (result->stageSchedulerExpectedDirtyBoundary == "scenePrep") {
                ++stageSchedulerExpectedScenePrepCount;
            } else if (result->stageSchedulerExpectedDirtyBoundary == "finishTone") {
                ++stageSchedulerExpectedFinishToneCount;
            } else {
                ++stageSchedulerExpectedUnknownCount;
            }
            if (result->success) {
                ++successCount;
                metricReadbackMaxDimension =
                    std::max(metricReadbackMaxDimension, result->metricReadbackMaxDimension);
                if (result->metricsReadbackDownsampled) {
                    ++metricsReadbackDownsampledCount;
                }
                if (observedDirtyBoundary == "rawBase") {
                    ++observedRawBaseDirtyCount;
                } else if (observedDirtyBoundary == "scenePrep") {
                    ++observedScenePrepDirtyCount;
                } else if (observedDirtyBoundary == "finishTone") {
                    ++observedFinishToneDirtyCount;
                } else {
                    ++observedUnknownDirtyCount;
                }
                if (stageCacheValidation.evaluated) {
                    ++stageCacheValidationCount;
                    if (stageCacheValidation.met) {
                        ++stageCacheValidationMetCount;
                    }
                }
                if (result->rawBaseCacheHitDuringFinalRender) {
                    ++rawBaseFinalCacheHitCount;
                }
                if (result->preFinishCacheHitDuringFinalRender) {
                    ++preFinishFinalCacheHitCount;
                }
                if (result->preFinishSuccess) {
                    ++preFinishSuccessCount;
                    if (result->preFinishMetricsReadbackDownsampled) {
                        ++preFinishMetricsReadbackDownsampledCount;
                    }
                    if (result->preFinishReusedFromFinalRender) {
                        ++preFinishReuseCount;
                    }
                }
                const float renderScore = ScoreDevelopRenderedCandidateMetrics(result->metrics, result->solveScore);
                item["standaloneRenderScore"] = renderScore;
                item["renderScore"] = renderScore;
                item["metrics"] = DevelopCandidateRenderMetricsToJson(result->metrics);
                item["renderedStatus"] = "candidate";
                if (result->candidateId == selectedCandidateId) {
                    selectedRenderScore = renderScore;
                    selectedRenderScoreValid = true;
                    selectedStandaloneRenderScore = renderScore;
                    selectedStandaloneRenderScoreValid = true;
                    selectedMetrics = result->metrics;
                    selectedMetricsValid = true;
                    if (result->preFinishSuccess) {
                        selectedPreFinishMetrics = result->preFinishMetrics;
                        selectedPreFinishMetricsValid = true;
                    }
                }
                RenderedCandidateSummary summary;
                summary.jsonIndex = rendered.size();
                summary.id = result->candidateId;
                summary.label = result->candidateLabel;
                summary.guidanceFingerprint = result->guidanceFingerprint;
                summary.renderScore = renderScore;
                summary.standaloneRenderScore = renderScore;
                summary.metrics = result->metrics;
                summary.activeRefineIntent = result->activeRefineIntent;
                if (activeRenderedRepairIntent.empty() && !result->activeRefineIntent.empty()) {
                    activeRenderedRepairIntent = result->activeRefineIntent;
                }
                if (result->preFinishSuccess) {
                    summary.preFinishMetrics = result->preFinishMetrics;
                    summary.preFinishValid = true;
                    item["preFinishMetrics"] =
                        DevelopCandidateRenderMetricsToJson(result->preFinishMetrics);
                    item["finalVsPreFinishMetricDistance"] =
                        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
                            result->metrics,
                            result->preFinishMetrics);
                }
                summary.damageReason = ClassifyDevelopRenderedCandidateDamage(
                    result->metrics,
                    node->rawDevelop.autoGuidance.intent);
                summary.damaged = !summary.damageReason.empty();
                renderedSummaries.push_back(std::move(summary));
            } else {
                ++failureCount;
                item["error"] = result->error;
            }
            rendered.push_back(std::move(item));
        }

        if (rendered.empty()) {
            continue;
        }

        if (activeRenderedRepairIntent.empty()) {
            activeRenderedRepairIntent =
                toneJson.value("autoCandidateRenderedRefineIntent", std::string());
        }
        if (selectedMetricsValid && selectedStandaloneRenderScoreValid) {
            for (RenderedCandidateSummary& summary : renderedSummaries) {
                const DevelopRenderedRelativeComparison comparison =
                    CompareDevelopRenderedCandidateToSelected(
                        summary.metrics,
                        summary.standaloneRenderScore,
                        selectedMetrics,
                        selectedStandaloneRenderScore,
                        activeRenderedRepairIntent,
                        summary.id == selectedCandidateId);
                summary.renderScore = comparison.adjustedScore;
                summary.relativeComparisonStatus = comparison.status;
                summary.relativeRepairMetric = comparison.repairMetric;
                summary.relativeComparisonReason = comparison.reason;
                summary.relativeMetricDistance = comparison.metricDistance;
                summary.relativeRepairDelta = comparison.repairDelta;
                summary.relativeRepairBonus = comparison.repairBonus;
                summary.relativeRegressionPenalty = comparison.regressionPenalty;
                summary.relativeDistanceBonus = comparison.distanceBonus;

                rendered[summary.jsonIndex]["renderScore"] = summary.renderScore;
                rendered[summary.jsonIndex]["relativeComparisonStatus"] =
                    summary.relativeComparisonStatus;
                rendered[summary.jsonIndex]["relativeRepairMetric"] =
                    summary.relativeRepairMetric;
                rendered[summary.jsonIndex]["relativeMetricDistance"] =
                    summary.relativeMetricDistance;
                rendered[summary.jsonIndex]["relativeRepairDelta"] =
                    summary.relativeRepairDelta;
                rendered[summary.jsonIndex]["relativeRepairBonus"] =
                    summary.relativeRepairBonus;
                rendered[summary.jsonIndex]["relativeRegressionPenalty"] =
                    summary.relativeRegressionPenalty;
                rendered[summary.jsonIndex]["relativeDistanceBonus"] =
                    summary.relativeDistanceBonus;
                rendered[summary.jsonIndex]["relativeComparisonReason"] =
                    summary.relativeComparisonReason;
                if (summary.id == selectedCandidateId) {
                    selectedRenderScore = summary.renderScore;
                    selectedRenderScoreValid = true;
                }
            }
        }

        std::vector<std::size_t> renderedOrder(renderedSummaries.size());
        for (std::size_t index = 0; index < renderedOrder.size(); ++index) {
            renderedOrder[index] = index;
        }
        std::sort(renderedOrder.begin(), renderedOrder.end(), [&](std::size_t a, std::size_t b) {
            return renderedSummaries[a].renderScore > renderedSummaries[b].renderScore;
        });

        std::vector<std::size_t> representativeIndices;
        for (std::size_t summaryIndex : renderedOrder) {
            RenderedCandidateSummary& candidate = renderedSummaries[summaryIndex];
            if (candidate.damaged) {
                ++damageCount;
                rendered[candidate.jsonIndex]["renderedStatus"] = "renderedRejectedDamage";
                rendered[candidate.jsonIndex]["rejectReason"] = candidate.damageReason;
                continue;
            }
            for (std::size_t representativeIndex : representativeIndices) {
                const RenderedCandidateSummary& representative = renderedSummaries[representativeIndex];
                const DevelopRenderedDuplicateDecision duplicateDecision =
                    EvaluateDevelopRenderedCandidateDuplicate(
                        candidate.metrics,
                        representative.metrics,
                        candidate.preFinishValid,
                        candidate.preFinishMetrics,
                        representative.preFinishValid,
                        representative.preFinishMetrics);
                if (duplicateDecision.duplicate) {
                    candidate.duplicate = true;
                    candidate.duplicateOf = representative.id;
                    candidate.duplicateFinalDistance = duplicateDecision.finalDistance;
                    candidate.duplicatePreFinishDistance = duplicateDecision.preFinishDistance;
                    break;
                }
                if (duplicateDecision.preFinishDistinct &&
                    duplicateDecision.preFinishDistance > candidate.preFinishDistinctDistance) {
                    candidate.preFinishDistinctFromRepresentative = true;
                    candidate.preFinishDistinctRepresentativeId = representative.id;
                    candidate.preFinishDistinctFinalDistance = duplicateDecision.finalDistance;
                    candidate.preFinishDistinctDistance = duplicateDecision.preFinishDistance;
                }
            }
            if (candidate.duplicate) {
                ++duplicateCount;
                rendered[candidate.jsonIndex]["renderedStatus"] = "renderedDuplicate";
                rendered[candidate.jsonIndex]["duplicateOf"] = candidate.duplicateOf;
                rendered[candidate.jsonIndex]["duplicateFinalMetricDistance"] =
                    candidate.duplicateFinalDistance;
                rendered[candidate.jsonIndex]["duplicatePreFinishMetricDistance"] =
                    candidate.duplicatePreFinishDistance;
                rendered[candidate.jsonIndex]["duplicateReason"] =
                    "Rendered metrics are too similar to a higher-scoring candidate.";
            } else {
                representativeIndices.push_back(summaryIndex);
                rendered[candidate.jsonIndex]["renderedStatus"] = "survivor";
                if (candidate.preFinishDistinctFromRepresentative) {
                    ++preFinishDistinctSurvivorCount;
                    rendered[candidate.jsonIndex]["preFinishDistinctFrom"] =
                        candidate.preFinishDistinctRepresentativeId;
                    rendered[candidate.jsonIndex]["preFinishDistinctFinalMetricDistance"] =
                        candidate.preFinishDistinctFinalDistance;
                    rendered[candidate.jsonIndex]["preFinishDistinctMetricDistance"] =
                        candidate.preFinishDistinctDistance;
                    rendered[candidate.jsonIndex]["preFinishDistinctReason"] =
                        "Final rendered metrics were near-duplicate, but the pre-finish boundary differed enough to preserve this candidate for stage-aware feedback.";
                }
            }
        }

        nlohmann::json renderedRejectionMemory =
            toneJson.value("autoCandidateRenderedRejectionMemory", nlohmann::json::array());
        if (!renderedRejectionMemory.is_array()) {
            renderedRejectionMemory = nlohmann::json::array();
        }
        auto rememberRenderedRejection = [&](const RenderedCandidateSummary& candidate) {
            if (candidate.guidanceFingerprint == 0 || candidate.id.empty()) {
                return;
            }
            for (auto it = renderedRejectionMemory.begin(); it != renderedRejectionMemory.end();) {
                if (it->is_object() &&
                    it->value("guidanceFingerprint", static_cast<std::uint64_t>(0)) == candidate.guidanceFingerprint &&
                    it->value("id", std::string()) == candidate.id) {
                    it = renderedRejectionMemory.erase(it);
                } else {
                    ++it;
                }
            }
            renderedRejectionMemory.push_back({
                { "id", candidate.id },
                { "label", candidate.label },
                { "guidanceFingerprint", candidate.guidanceFingerprint },
                { "status", "renderedRejectedDamage" },
                { "reason", candidate.damageReason },
                { "renderScore", candidate.renderScore },
                { "solveFingerprint", currentFingerprint },
                { "revision", currentRevision }
            });
        };
        for (const RenderedCandidateSummary& candidate : renderedSummaries) {
            if (candidate.damaged) {
                rememberRenderedRejection(candidate);
            }
        }
        constexpr std::size_t kMaxRenderedRejectionMemoryEntries = 24;
        while (renderedRejectionMemory.size() > kMaxRenderedRejectionMemoryEntries) {
            renderedRejectionMemory.erase(renderedRejectionMemory.begin());
        }

        for (const RenderedCandidateSummary& summary : renderedSummaries) {
            if (summary.damaged || summary.duplicate) {
                continue;
            }
            if (summary.renderScore > bestRenderScore) {
                bestRenderScore = summary.renderScore;
                bestCandidateId = summary.id;
                bestCandidateLabel = summary.label;
                bestMetrics = summary.metrics;
                bestMetricsValid = true;
                bestPreFinishMetrics = summary.preFinishMetrics;
                bestPreFinishMetricsValid = summary.preFinishValid;
            }
        }
        const RenderedCandidateSummary* bestSummary = nullptr;
        for (const RenderedCandidateSummary& summary : renderedSummaries) {
            if (!bestCandidateId.empty() && summary.id == bestCandidateId) {
                bestSummary = &summary;
                break;
            }
        }

        if (representativeIndices.size() >= 2) {
            const RenderedCandidateSummary& first = renderedSummaries[representativeIndices[0]];
            const RenderedCandidateSummary& second = renderedSummaries[representativeIndices[1]];
            renderedMergeMetricDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(first.metrics, second.metrics);
            const float scoreGap = std::fabs(first.renderScore - second.renderScore);
            renderedPairMergeSuggested =
                first.renderScore >= 0.58f &&
                second.renderScore >= 0.54f &&
                scoreGap <= 0.16f &&
                renderedMergeMetricDistance >= 0.11f;
            if (renderedPairMergeSuggested) {
                renderedMergeFirstId = first.id;
                renderedMergeFirstLabel = first.label;
                renderedMergeFirstScore = first.renderScore;
                renderedMergeSecondId = second.id;
                renderedMergeSecondLabel = second.label;
                renderedMergeSecondScore = second.renderScore;
                rendered[first.jsonIndex]["renderedMergeRole"] = "first";
                rendered[second.jsonIndex]["renderedMergeRole"] = "second";
            }
        }

        if (representativeIndices.size() >= 3) {
            const RenderedCandidateSummary& first = renderedSummaries[representativeIndices[0]];
            const RenderedCandidateSummary& second = renderedSummaries[representativeIndices[1]];
            const RenderedCandidateSummary& third = renderedSummaries[representativeIndices[2]];
            const float firstSecondDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(first.metrics, second.metrics);
            const float firstThirdDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(first.metrics, third.metrics);
            const float secondThirdDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(second.metrics, third.metrics);
            renderedEnsembleMergeMetricSpread =
                (firstSecondDistance + firstThirdDistance + secondThirdDistance) / 3.0f;
            renderedEnsembleMergeScoreSpread =
                std::max(first.renderScore, std::max(second.renderScore, third.renderScore)) -
                std::min(first.renderScore, std::min(second.renderScore, third.renderScore));
            const int distinctMetricPairCount =
                (firstSecondDistance >= 0.08f ? 1 : 0) +
                (firstThirdDistance >= 0.08f ? 1 : 0) +
                (secondThirdDistance >= 0.08f ? 1 : 0);
            renderedEnsembleMergeSuggested =
                first.renderScore >= 0.60f &&
                second.renderScore >= 0.56f &&
                third.renderScore >= 0.52f &&
                renderedEnsembleMergeScoreSpread <= 0.22f &&
                renderedEnsembleMergeMetricSpread >= 0.10f &&
                distinctMetricPairCount >= 2;
            if (renderedEnsembleMergeSuggested) {
                renderedEnsembleMergeIds = nlohmann::json::array({ first.id, second.id, third.id });
                renderedEnsembleMergeLabels = nlohmann::json::array({ first.label, second.label, third.label });
                renderedEnsembleMergeScores =
                    nlohmann::json::array({ first.renderScore, second.renderScore, third.renderScore });
                renderedEnsembleMergeMetricDistances = {
                    { "firstSecond", firstSecondDistance },
                    { "firstThird", firstThirdDistance },
                    { "secondThird", secondThirdDistance }
                };
                rendered[first.jsonIndex]["renderedMergeRole"] = "ensemble1";
                rendered[second.jsonIndex]["renderedMergeRole"] = "ensemble2";
                rendered[third.jsonIndex]["renderedMergeRole"] = "ensemble3";
            }
        }

        float selectedBestFinalMetricDistance = -1.0f;
        float selectedBestPreFinishMetricDistance = -1.0f;
        const bool selectedBestFinalMetricsValid =
            selectedMetricsValid && bestMetricsValid && !selectedCandidateId.empty() && !bestCandidateId.empty();
        const bool selectedBestPreFinishMetricsValid =
            selectedPreFinishMetricsValid && bestPreFinishMetricsValid && selectedBestFinalMetricsValid;
        const std::string stageBoundarySignal =
            ClassifyDevelopRenderedStageBoundary(
                selectedMetrics,
                bestMetrics,
                selectedBestFinalMetricsValid,
                selectedPreFinishMetrics,
                bestPreFinishMetrics,
                selectedBestPreFinishMetricsValid,
                selectedBestFinalMetricDistance,
                selectedBestPreFinishMetricDistance);

        const std::size_t beforeHash = HashDevelopRenderedFeedbackJsonValue(toneJson);
        toneJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        toneJson["autoCandidateRenderedFingerprint"] = currentFingerprint;
        toneJson["autoCandidateRenderedAtRevision"] = currentRevision;
        toneJson["autoCandidateRenderedSolves"] = std::move(rendered);
        toneJson["autoCandidateRenderedCount"] = successCount;
        toneJson["autoCandidateRenderedPreFinishCount"] = preFinishSuccessCount;
        toneJson["autoCandidateRenderedPreFinishReuseCount"] = preFinishReuseCount;
        toneJson["autoCandidateRenderedPreFinishReuseStatus"] =
            preFinishSuccessCount <= 0
                ? "none"
                : (preFinishReuseCount >= preFinishSuccessCount ? "all" : (preFinishReuseCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedMetricReadbackMaxDimension"] = metricReadbackMaxDimension;
        toneJson["autoCandidateRenderedMetricReadbackCapped"] =
            metricsReadbackDownsampledCount > 0 || preFinishMetricsReadbackDownsampledCount > 0;
        toneJson["autoCandidateRenderedMetricReadbackDownsampledCount"] =
            metricsReadbackDownsampledCount;
        toneJson["autoCandidateRenderedPreFinishMetricReadbackDownsampledCount"] =
            preFinishMetricsReadbackDownsampledCount;
        toneJson["autoCandidateRenderedRawBaseFinalCacheHitCount"] = rawBaseFinalCacheHitCount;
        toneJson["autoCandidateRenderedPreFinishFinalCacheHitCount"] = preFinishFinalCacheHitCount;
        toneJson["autoCandidateRenderedRawBaseFinalCacheHitStatus"] =
            successCount <= 0
                ? "none"
                : (rawBaseFinalCacheHitCount >= successCount ? "all" : (rawBaseFinalCacheHitCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedPreFinishFinalCacheHitStatus"] =
            successCount <= 0
                ? "none"
                : (preFinishFinalCacheHitCount >= successCount ? "all" : (preFinishFinalCacheHitCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedObservedDirtyBoundaryCounts"] = {
            { "rawBase", observedRawBaseDirtyCount },
            { "scenePrep", observedScenePrepDirtyCount },
            { "finishTone", observedFinishToneDirtyCount },
            { "unknown", observedUnknownDirtyCount }
        };
        toneJson["autoCandidateRenderedStageSchedulerVersion"] = "StageSchedulerV1";
        toneJson["autoCandidateRenderedStageSchedulerCount"] = stageSchedulerCount;
        toneJson["autoCandidateRenderedStageSchedulerStatus"] =
            stageSchedulerCount <= 0
                ? "none"
                : (stageSchedulerOrderMonotonic ? "ordered" : "outOfOrder");
        toneJson["autoCandidateRenderedStageSchedulerExpectedBoundaryCounts"] = {
            { "rawBase", stageSchedulerExpectedRawBaseCount },
            { "scenePrep", stageSchedulerExpectedScenePrepCount },
            { "finishTone", stageSchedulerExpectedFinishToneCount },
            { "unknown", stageSchedulerExpectedUnknownCount }
        };
        toneJson["autoCandidateRenderedStageCacheValidationCount"] = stageCacheValidationCount;
        toneJson["autoCandidateRenderedStageCacheValidationMetCount"] = stageCacheValidationMetCount;
        toneJson["autoCandidateRenderedStageCacheValidationMissCount"] =
            std::max(0, stageCacheValidationCount - stageCacheValidationMetCount);
        toneJson["autoCandidateRenderedStageCacheValidationStatus"] =
            stageCacheValidationCount <= 0
                ? "none"
                : (stageCacheValidationMetCount >= stageCacheValidationCount
                    ? "all"
                    : (stageCacheValidationMetCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedActiveStageRequestCount"] = activeStageRequestCount;
        toneJson["autoCandidateRenderedStageReservedRequestCount"] = stageReservedRequestCount;
        toneJson["autoCandidateRenderedActiveRefineIntentRequestCount"] = activeRefineIntentRequestCount;
        toneJson["autoCandidateRenderedRefineIntentReservedRequestCount"] = refineIntentReservedRequestCount;
        toneJson["autoCandidateRenderedAdaptiveBudgetVersion"] =
            kDevelopAdaptiveRenderBudgetVersion;
        toneJson["autoCandidateRenderedAdaptiveBudget"] = adaptiveRenderBudget;
        toneJson["autoCandidateRenderedAdaptiveBudgetDefault"] =
            static_cast<int>(kDefaultDevelopCandidateRenderRequestsPerNode);
        toneJson["autoCandidateRenderedAdaptiveBudgetMax"] =
            static_cast<int>(kMaxDevelopCandidateRenderRequestsPerNode);
        toneJson["autoCandidateRenderedAdaptiveBudgetExpanded"] =
            adaptiveBudgetExpandedRequestCount > 0;
        toneJson["autoCandidateRenderedAdaptiveBudgetExpandedRequestCount"] =
            adaptiveBudgetExpandedRequestCount;
        toneJson["autoCandidateRenderedAdaptiveBudgetNarrowed"] =
            adaptiveBudgetNarrowedRequestCount > 0;
        toneJson["autoCandidateRenderedAdaptiveBudgetNarrowedRequestCount"] =
            adaptiveBudgetNarrowedRequestCount;
        toneJson["autoCandidateRenderedAdaptiveBudgetReason"] =
            adaptiveRenderBudgetReason;
        toneJson["autoCandidateRenderedAdaptiveBudgetContinuationDecision"] =
            adaptiveRenderBudgetContinuationDecision;
        toneJson["autoCandidateRenderedAdaptiveBudgetConvergenceState"] =
            adaptiveRenderBudgetConvergenceState;
        toneJson["autoCandidateRenderedAdaptiveBudgetConvergenceDecision"] =
            adaptiveRenderBudgetConvergenceDecision;
        toneJson["autoCandidateRenderedAdaptiveBudgetConvergenceReason"] =
            adaptiveRenderBudgetConvergenceReason;
        toneJson["autoCandidateRenderedTimingVersion"] = "CandidateRenderTimingV1";
        toneJson["autoCandidateRenderedTotalElapsedMs"] = totalCandidateElapsedMs;
        toneJson["autoCandidateRenderedFinalGraphMs"] = totalFinalGraphMs;
        toneJson["autoCandidateRenderedFinalReadbackMs"] = totalFinalReadbackMs;
        toneJson["autoCandidateRenderedFinalAnalysisMs"] = totalFinalAnalysisMs;
        toneJson["autoCandidateRenderedPreFinishGraphMs"] = totalPreFinishGraphMs;
        toneJson["autoCandidateRenderedPreFinishReadbackMs"] = totalPreFinishReadbackMs;
        toneJson["autoCandidateRenderedPreFinishAnalysisMs"] = totalPreFinishAnalysisMs;
        toneJson["autoCandidateRenderedSlowestElapsedMs"] = slowestCandidateElapsedMs;
        toneJson["autoCandidateRenderedSlowestLabel"] = slowestCandidateLabel;
        toneJson["autoCandidateRenderedRejectionMemory"] = std::move(renderedRejectionMemory);
        toneJson["autoCandidateRenderedRejectionMemoryMaxEntries"] =
            static_cast<int>(kMaxRenderedRejectionMemoryEntries);
        toneJson["autoCandidateRenderedUniqueCount"] =
            std::max(0, successCount - duplicateCount - damageCount);
        toneJson["autoCandidateRenderedDamageCount"] = damageCount;
        toneJson["autoCandidateRenderedDuplicateCount"] = duplicateCount;
        toneJson["autoCandidateRenderedDuplicateDistance"] = kDevelopRenderedDuplicateDistance;
        toneJson["autoCandidateRenderedPreFinishDistinctDistance"] =
            kDevelopRenderedPreFinishDistinctDistance;
        toneJson["autoCandidateRenderedPreFinishDistinctSurvivorCount"] =
            preFinishDistinctSurvivorCount;
        toneJson["autoCandidateRenderedFailureCount"] = failureCount;
        toneJson["autoCandidateRenderedBestId"] = bestCandidateId;
        toneJson["autoCandidateRenderedBestLabel"] = bestCandidateLabel;
        toneJson["autoCandidateRenderedBestScore"] = std::max(0.0f, bestRenderScore);
        toneJson["autoCandidateRenderedRelativeComparisonVersion"] =
            selectedMetricsValid ? "RenderedRelativeComparisonV1" : "notApplied";
        toneJson["autoCandidateRenderedActiveRepairIntent"] = activeRenderedRepairIntent;
        toneJson["autoCandidateRenderedSelectedStandaloneScore"] =
            selectedStandaloneRenderScoreValid ? selectedStandaloneRenderScore : -1.0f;
        toneJson["autoCandidateRenderedBestStandaloneScore"] =
            bestSummary ? bestSummary->standaloneRenderScore : -1.0f;
        toneJson["autoCandidateRenderedBestRelativeStatus"] =
            bestSummary ? bestSummary->relativeComparisonStatus : std::string();
        toneJson["autoCandidateRenderedBestRelativeRepairMetric"] =
            bestSummary ? bestSummary->relativeRepairMetric : std::string();
        toneJson["autoCandidateRenderedBestRelativeMetricDistance"] =
            bestSummary ? bestSummary->relativeMetricDistance : -1.0f;
        toneJson["autoCandidateRenderedBestRelativeRepairDelta"] =
            bestSummary ? bestSummary->relativeRepairDelta : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeRepairBonus"] =
            bestSummary ? bestSummary->relativeRepairBonus : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeRegressionPenalty"] =
            bestSummary ? bestSummary->relativeRegressionPenalty : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeDistanceBonus"] =
            bestSummary ? bestSummary->relativeDistanceBonus : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeReason"] =
            bestSummary ? bestSummary->relativeComparisonReason : std::string();
        toneJson["autoCandidateRenderedMergeSuggested"] = renderedPairMergeSuggested;
        toneJson["autoCandidateRenderedMergeFirstId"] = renderedMergeFirstId;
        toneJson["autoCandidateRenderedMergeFirstLabel"] = renderedMergeFirstLabel;
        toneJson["autoCandidateRenderedMergeFirstScore"] = renderedMergeFirstScore;
        toneJson["autoCandidateRenderedMergeSecondId"] = renderedMergeSecondId;
        toneJson["autoCandidateRenderedMergeSecondLabel"] = renderedMergeSecondLabel;
        toneJson["autoCandidateRenderedMergeSecondScore"] = renderedMergeSecondScore;
        toneJson["autoCandidateRenderedMergeMetricDistance"] = renderedMergeMetricDistance;
        toneJson["autoCandidateRenderedEnsembleMergeSuggested"] = renderedEnsembleMergeSuggested;
        toneJson["autoCandidateRenderedEnsembleMergeIds"] = std::move(renderedEnsembleMergeIds);
        toneJson["autoCandidateRenderedEnsembleMergeLabels"] = std::move(renderedEnsembleMergeLabels);
        toneJson["autoCandidateRenderedEnsembleMergeScores"] = std::move(renderedEnsembleMergeScores);
        toneJson["autoCandidateRenderedEnsembleMergeMetricDistances"] =
            std::move(renderedEnsembleMergeMetricDistances);
        toneJson["autoCandidateRenderedEnsembleMergeMetricSpread"] =
            renderedEnsembleMergeMetricSpread;
        toneJson["autoCandidateRenderedEnsembleMergeScoreSpread"] =
            renderedEnsembleMergeScoreSpread;
        toneJson["autoCandidateRenderedStageBoundarySignal"] = stageBoundarySignal;
        toneJson["autoCandidateRenderedSelectedBestMetricDistance"] = selectedBestFinalMetricDistance;
        toneJson["autoCandidateRenderedSelectedBestPreFinishDistance"] = selectedBestPreFinishMetricDistance;
        toneJson["autoCandidateRenderedSelectedBestPreFinishValid"] = selectedBestPreFinishMetricsValid;
        toneJson["autoCandidateRenderMetricsStatus"] =
            successCount <= 0 ? "failed" : (failureCount > 0 ? "partial" : "ready");
        toneJson["autoCandidateGalleryStatus"] = "deferred";
        std::string feedbackAction = "stopped";
        std::string stopReason = "unknown";
        std::string refineIntent;
        std::string refineReason;
        std::string preFinishRefineIntent;
        std::string preFinishRefineReason;
        if (selectedMetricsValid) {
            refineIntent = ResolveDevelopRenderedRefineIntent(
                selectedMetrics,
                node->rawDevelop.autoGuidance.intent,
                refineReason);
        }
        if (selectedPreFinishMetricsValid) {
            preFinishRefineIntent = ResolveDevelopRenderedRefineIntent(
                selectedPreFinishMetrics,
                node->rawDevelop.autoGuidance.intent,
                preFinishRefineReason);
            if (!preFinishRefineIntent.empty() && preFinishRefineIntent != "addContrast") {
                refineIntent = preFinishRefineIntent;
                refineReason = preFinishRefineReason +
                    " Detected before finish tone, so feedback should revise the earlier responsible stage.";
            }
        }
        const std::string repeatedRefineStopReason =
            RepeatedRenderedRefinementStopReason(
                toneJson,
                refineIntent,
                selectedRenderScore,
                selectedRenderScoreValid);
        bool renderedFeedbackWorthTrying = false;
        bool renderedFeedbackRefineRequested = false;
        const int currentRenderedFeedbackPass =
            toneJson.value("autoCandidateRenderedFeedbackPass", 0);
        const bool bestRelativeRegression =
            bestSummary &&
            (bestSummary->relativeComparisonStatus == "regressedAgainstSelected" ||
             bestSummary->relativeComparisonStatus == "missedActiveRepair") &&
            bestSummary->relativeRegressionPenalty >
                bestSummary->relativeRepairBonus + bestSummary->relativeDistanceBonus + 0.012f;
        if (successCount <= 0) {
            feedbackAction = "failed";
            stopReason = "candidateRendersFailed";
        } else if (bestCandidateId.empty()) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = damageCount > 0
                    ? "allRenderedCandidatesRejectedForDamage"
                    : "noRenderedBestCandidate";
            }
        } else if (selectedCandidateId.empty()) {
            stopReason = "noSelectedCandidate";
        } else if (currentRenderedFeedbackPass >= kDevelopRenderedFeedbackMaxPasses) {
            stopReason = "renderedFeedbackPassLimit";
        } else if (toneJson.value(
                "autoCandidateRenderedFeedbackAppliedFingerprint",
                static_cast<std::uint64_t>(0)) == currentFingerprint) {
            stopReason = "renderedFeedbackAlreadyApplied";
        } else if (!repeatedRefineStopReason.empty() &&
            (bestCandidateId == selectedCandidateId ||
             bestRenderScore < 0.48f ||
             (selectedRenderScoreValid && bestRenderScore < selectedRenderScore + 0.025f))) {
            stopReason = repeatedRefineStopReason;
        } else if (bestCandidateId == selectedCandidateId) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = "selectedCandidateStillBest";
            }
        } else if (bestRenderScore < 0.48f) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = "renderedBestBelowQualityFloor";
            }
        } else if (selectedRenderScoreValid && bestRenderScore < selectedRenderScore + 0.025f) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = "noMeaningfulRenderedImprovement";
            }
        } else if (bestRelativeRegression &&
            selectedRenderScoreValid &&
            bestRenderScore < selectedRenderScore + 0.055f) {
            stopReason = "renderedBestRelativeRegression";
        } else if (WouldRenderedFeedbackReverseRecentAdoption(toneJson, selectedCandidateId, bestCandidateId)) {
            stopReason = "wouldReverseRecentRenderedAdoption";
        } else {
            feedbackAction = "solveRequested";
            stopReason = "renderedBestImproved";
            renderedFeedbackWorthTrying = true;
        }
        const bool renderedStopConverged =
            !renderedFeedbackWorthTrying &&
            successCount > 0 &&
            IsDevelopRenderedFeedbackStopConvergedReason(stopReason);
        std::string revisionStage =
            renderedFeedbackWorthTrying ? "multiStage" : (renderedStopConverged ? "converged" : "none");
        std::string revisionReason = stopReason;
        if (renderedFeedbackWorthTrying) {
            if (renderedFeedbackRefineRequested) {
                revisionStage = DevelopRenderedRevisionStageForRefineIntent(refineIntent);
                revisionReason = refineReason.empty() ? stopReason : refineReason;
            } else {
                if (stageBoundarySignal == "finishToneOnly") {
                    revisionStage = "finishTone";
                    revisionReason =
                        "Final rendered metrics changed while pre-finish metrics stayed close, so the follow-up solve should validate finish tone rather than rerouting upstream stages.";
                } else {
                    revisionStage = DevelopRenderedRevisionStageForCandidateId(bestCandidateId);
                    revisionReason =
                        "Rendered metrics selected a stronger survivor; the follow-up solve should validate the earliest changed authored stage.";
                }
            }
        }
        toneJson["autoCandidateRenderedRefineIntent"] =
            renderedFeedbackRefineRequested ? refineIntent : std::string();
        toneJson["autoCandidateRenderedRefineReason"] =
            renderedFeedbackRefineRequested ? refineReason : std::string();
        toneJson["autoCandidateRenderedPreFinishRefineIntent"] = preFinishRefineIntent;
        toneJson["autoCandidateRenderedPreFinishRefineReason"] = preFinishRefineReason;
        toneJson["autoCandidateRenderedRevisionStage"] = revisionStage;
        toneJson["autoCandidateRenderedRevisionReason"] = revisionReason;
        toneJson["autoCandidateRenderedConvergenceStatus"] =
            renderedFeedbackWorthTrying ? "renderedFeedbackSolveRequested" : feedbackAction;
        toneJson["autoCandidateRenderedStopReason"] = stopReason;
        toneJson["autoCandidateRenderedSelectedScore"] =
            selectedRenderScoreValid ? selectedRenderScore : -1.0f;
        toneJson["autoCandidateRenderedSelectedScoreValid"] = selectedRenderScoreValid;
        toneJson["autoCandidateRenderedConverged"] = renderedStopConverged;
        const int nextRenderedFeedbackPass = renderedFeedbackWorthTrying
            ? std::min(currentRenderedFeedbackPass + 1, kDevelopRenderedFeedbackMaxPasses)
            : currentRenderedFeedbackPass;
        const float renderedImprovement =
            selectedRenderScoreValid ? bestRenderScore - selectedRenderScore : -1.0f;
        const nlohmann::json continuationPolicy =
            BuildDevelopRenderedContinuationPolicyRecord(
                renderedFeedbackWorthTrying ? "continue" : "stop",
                stopReason,
                renderedFeedbackWorthTrying ? "applyAutoSolve" : "none",
                renderedFeedbackWorthTrying,
                false,
                currentRenderedFeedbackPass,
                nextRenderedFeedbackPass,
                revisionStage,
                revisionReason,
                renderedImprovement,
                stageBoundarySignal,
                bestSummary ? bestSummary->relativeComparisonStatus : std::string(),
                successCount,
                failureCount);
        toneJson["autoCandidateRenderedContinuationVersion"] =
            kDevelopRenderedContinuationVersion;
        toneJson["autoCandidateRenderedContinuationPolicy"] = continuationPolicy;
        AppendDevelopCandidateRenderedFeedbackHistory(
            toneJson,
            currentFingerprint,
            selectedCandidateId,
            selectedRenderScore,
            selectedRenderScoreValid,
            bestCandidateId,
            bestRenderScore,
            successCount,
            failureCount,
            feedbackAction,
            stopReason,
            renderedFeedbackRefineRequested ? refineIntent : std::string(),
            renderedFeedbackRefineRequested ? refineReason : std::string(),
            selectedMetricsValid ? &selectedMetrics : nullptr,
            bestMetricsValid ? &bestMetrics : nullptr);
        const std::string renderedLoopState = renderedFeedbackWorthTrying
            ? std::string("solveRequested")
            : (successCount <= 0
                ? std::string("failed")
                : (renderedStopConverged ? std::string("converged") : std::string("stopped")));
        WriteDevelopCandidateRenderedFeedbackLoopRecord(
            toneJson,
            currentFingerprint,
            currentRevision,
            renderedLoopState,
            feedbackAction,
            stopReason,
            renderedFeedbackWorthTrying ? std::string("applyAutoSolve") : std::string("none"),
            renderedFeedbackWorthTrying,
            false,
            selectedCandidateId,
            selectedRenderScore,
            selectedRenderScoreValid,
            bestCandidateId,
            bestRenderScore,
            successCount,
            failureCount);

        const std::size_t afterHash = HashDevelopRenderedFeedbackJsonValue(toneJson);
        if (beforeHash != afterHash) {
            node->rawDevelop.integratedToneLayerJson = std::move(toneJson);
            persistedStateChanged = true;
        }
        if (renderedFeedbackWorthTrying) {
            const EditorNodeGraph::Node* rawSource =
                FindUpstreamRawSourceForDevelopNode(m_NodeGraph, *node);
            if (rawSource && rawSource->kind == EditorNodeGraph::NodeKind::RawSource) {
                if (UpdateDevelopAutoState(
                        node->id,
                        node->rawDevelop,
                        rawSource->rawSource.metadata,
                        true,
                        true)) {
                    MarkRenderDirty(node->id);
                    persistedStateChanged = true;
                }
            }
        }
    }

    if (persistedStateChanged) {
        m_Dirty = true;
    }
}
