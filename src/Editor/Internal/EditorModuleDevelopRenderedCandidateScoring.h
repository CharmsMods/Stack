#pragma once

#include "Editor/EditorRenderWorker.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

#include <string>

namespace Stack::Editor::DevelopRenderedCandidateScoring {

struct DevelopRenderedRelativeComparison {
    float adjustedScore = -1.0f;
    float standaloneScore = -1.0f;
    float selectedScore = -1.0f;
    float metricDistance = -1.0f;
    float repairDelta = 0.0f;
    float repairBonus = 0.0f;
    float regressionPenalty = 0.0f;
    float distanceBonus = 0.0f;
    std::string status = "standalone";
    std::string repairMetric;
    std::string reason;
};

float ScoreDevelopRenderedCandidateMetrics(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    float solveScore);

DevelopRenderedRelativeComparison CompareDevelopRenderedCandidateToSelected(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidate,
    float standaloneScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selected,
    float selectedScore,
    const std::string& activeRefineIntent,
    bool selectedBaseline);

std::string ClassifyDevelopRenderedCandidateDamage(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent);

std::string ResolveDevelopRenderedRefineIntent(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent,
    std::string& outReason);

} // namespace Stack::Editor::DevelopRenderedCandidateScoring
