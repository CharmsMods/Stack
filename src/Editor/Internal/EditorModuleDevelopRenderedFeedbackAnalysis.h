#pragma once

#include "Editor/EditorRenderWorker.h"

#include <string>

namespace Stack::Editor::DevelopRenderedFeedbackAnalysis {

inline constexpr float kDevelopRenderedDuplicateDistance = 0.085f;
inline constexpr float kDevelopRenderedPreFinishDistinctDistance = 0.085f;

struct DevelopRenderedDuplicateDecision {
    bool duplicate = false;
    bool preFinishDistinct = false;
    float finalDistance = -1.0f;
    float preFinishDistance = -1.0f;
};

DevelopRenderedDuplicateDecision EvaluateDevelopRenderedCandidateDuplicate(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativeFinalMetrics,
    bool candidatePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidatePreFinishMetrics,
    bool representativePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativePreFinishMetrics);

std::string ClassifyDevelopRenderedStageBoundary(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestFinalMetrics,
    bool finalMetricsValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedPreFinishMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestPreFinishMetrics,
    bool preFinishMetricsValid,
    float& outFinalDistance,
    float& outPreFinishDistance);

} // namespace Stack::Editor::DevelopRenderedFeedbackAnalysis
