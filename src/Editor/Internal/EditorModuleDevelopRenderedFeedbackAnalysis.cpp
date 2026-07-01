#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackAnalysis.h"

namespace Stack::Editor::DevelopRenderedFeedbackAnalysis {

DevelopRenderedDuplicateDecision EvaluateDevelopRenderedCandidateDuplicate(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativeFinalMetrics,
    bool candidatePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidatePreFinishMetrics,
    bool representativePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativePreFinishMetrics) {
    DevelopRenderedDuplicateDecision decision;
    decision.finalDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(candidateFinalMetrics, representativeFinalMetrics);
    if (candidatePreFinishValid && representativePreFinishValid) {
        decision.preFinishDistance =
            EditorRenderWorker::CompareDevelopCandidateRenderMetrics(candidatePreFinishMetrics, representativePreFinishMetrics);
    }

    if (decision.finalDistance >= kDevelopRenderedDuplicateDistance) {
        return decision;
    }

    // Final tone can mask upstream differences. Keep the candidate alive when
    // the final render is near-duplicate but the pre-finish boundary is not.
    if (decision.preFinishDistance >= kDevelopRenderedPreFinishDistinctDistance) {
        decision.preFinishDistinct = true;
        return decision;
    }

    decision.duplicate = true;
    return decision;
}

std::string ClassifyDevelopRenderedStageBoundary(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestFinalMetrics,
    bool finalMetricsValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedPreFinishMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestPreFinishMetrics,
    bool preFinishMetricsValid,
    float& outFinalDistance,
    float& outPreFinishDistance) {
    outFinalDistance = finalMetricsValid
        ? EditorRenderWorker::CompareDevelopCandidateRenderMetrics(selectedFinalMetrics, bestFinalMetrics)
        : -1.0f;
    outPreFinishDistance = preFinishMetricsValid
        ? EditorRenderWorker::CompareDevelopCandidateRenderMetrics(selectedPreFinishMetrics, bestPreFinishMetrics)
        : -1.0f;

    if (!finalMetricsValid) {
        return "missingFinalMetrics";
    }
    if (!preFinishMetricsValid) {
        return "missingPreFinishMetrics";
    }

    constexpr float kMeaningfulFinalDistance = kDevelopRenderedDuplicateDistance;
    constexpr float kStablePreFinishDistance = 0.055f;
    constexpr float kMeaningfulPreFinishDistance = kDevelopRenderedPreFinishDistinctDistance;
    if (outFinalDistance >= kMeaningfulFinalDistance &&
        outPreFinishDistance <= kStablePreFinishDistance) {
        return "finishToneOnly";
    }
    if (outPreFinishDistance >= kMeaningfulPreFinishDistance &&
        outFinalDistance < kMeaningfulFinalDistance) {
        return "preFinishChangedButFinalMasked";
    }
    if (outPreFinishDistance >= kMeaningfulPreFinishDistance) {
        return "preFinishChanged";
    }
    return "stable";
}

} // namespace Stack::Editor::DevelopRenderedFeedbackAnalysis
