#pragma once

#include "Editor/EditorRenderWorker.h"
#include "ThirdParty/json.hpp"

#include <string>

namespace Stack::Validation::Detail {

nlohmann::json DevelopAutoSolveRenderedMetricsToJson(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics);

struct DevelopAutoSolveRenderedMetricFixtures {
    EditorRenderWorker::DevelopCandidateRenderMetrics visualRiskMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics spatialRiskMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics subjectRiskMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics markedSubjectMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics lowPrioritySubjectMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics colorCastMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics similarRenderedMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics distinctRenderedMetrics;
    EditorRenderWorker::DevelopCandidateRenderMetrics renderedCleanShadowMetrics;

    bool renderedVisualRiskMetricsPopulated = false;
    bool renderedHighlightGrayMetricsPopulated = false;
    bool renderedMeaningfulHighlightMetricsPopulated = false;
    bool renderedLocalMetricsPopulated = false;
    bool renderedSubjectMetricsPopulated = false;
    bool renderedMarkedSubjectMetricsPopulated = false;
    bool renderedMarkedLowPriorityMetricsPopulated = false;
    bool renderedSpatialRiskMetricsPopulated = false;
    bool renderedColorCastMetricsPopulated = false;
    bool renderedDuplicateMetricDistanceWorks = false;
    bool renderedStageBoundaryClassifierWorks = false;
    bool renderedStageAwareDuplicateClusteringWorks = false;
    bool renderedDamageClassifierWorks = false;
    bool renderedLocalRefineIntentWorks = false;
    bool renderedRelativeComparisonWorks = false;

    float renderedDuplicateMetricDistance = 0.0f;
    float renderedDistinctMetricDistance = 0.0f;
    float renderedLocalMetricDistance = 0.0f;
    float renderedColorMetricDistance = 0.0f;
    float renderedMarkedSubjectMetricDistance = 0.0f;
    float finishOnlyFinalMetricDistance = -1.0f;
    float finishOnlyPreFinishMetricDistance = -1.0f;
    float preFinishChangedFinalMetricDistance = -1.0f;
    float preFinishChangedPreFinishMetricDistance = -1.0f;
    float stageAwareDuplicateFinalDistance = -1.0f;
    float stageAwareDuplicatePreFinishDistance = -1.0f;
    float stageAwareMaskedFinalDistance = -1.0f;
    float stageAwareMaskedPreFinishDistance = -1.0f;
    float relativeAdjustedRawScore = 0.0f;
    float relativeAdjustedIntentScore = 0.0f;
    float relativeRawScoreRepairDelta = 0.0f;
    float relativeIntentRepairDelta = 0.0f;
    float relativeRawScoreRegressionPenalty = 0.0f;

    bool stageAwareDuplicatePreFinishDistinct = false;
    bool stageAwareMaskedPreFinishDistinct = false;

    std::string finishOnlyStageBoundary;
    std::string preFinishChangedStageBoundary;
    std::string damagedClipReason;
    std::string damagedHaloReason;
    std::string damagedGrayReason;
    std::string damagedShadowNoiseReason;
    std::string damagedSpatialHotspotReason;
    std::string damagedColorCastReason;
    std::string safeRenderedDamageReason;
    std::string relativeRawScoreStatus;
    std::string relativeIntentStatus;
    std::string localCenterShadowIntent;
    std::string localHighlightIntent;
    std::string structuredHighlightPressureIntent;
    std::string structuredHighlightPressureReason;
    std::string localSpatialHighlightRiskIntent;
    std::string localSpatialHighlightRiskReason;
    std::string localFlatIntent;
    std::string renderedCleanShadowIntent;
    std::string localSpatialShadowRiskIntent;
    std::string localSpatialShadowRiskReason;
    std::string localSpatialFlatRiskIntent;
    std::string localSpatialFlatRiskReason;
    std::string renderedPreserveTextureIntent;
};

DevelopAutoSolveRenderedMetricFixtures BuildDevelopAutoSolveRenderedMetricFixtures();

} // namespace Stack::Validation::Detail
