#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "ThirdParty/json.hpp"

#include <string>

namespace Stack::Validation::Detail {

struct DevelopAutoSolveCandidateProbeSummary {
    EditorNodeGraph::DevelopAutoGuidance cleanShadowCandidateGuidance;
    EditorNodeGraph::DevelopAutoGuidance preserveTextureCandidateGuidance;
    EditorNodeGraph::DevelopAutoGuidance highlightProtectedMidsGuidance;
    EditorNodeGraph::DevelopAutoGuidance finishToneProbeGuidance;
    EditorNodeGraph::DevelopAutoGuidance naturalContrastGuardGuidance;
    EditorNodeGraph::DevelopAutoGuidance brightHighlightRolloffGuidance;
    EditorNodeGraph::DevelopAutoGuidance luminousHighlightAnchorGuidance;
    EditorNodeGraph::DevelopAutoGuidance specularHighlightToleranceGuidance;
    EditorNodeGraph::DevelopAutoGuidance whiteBalanceProbeGuidance;

    bool cleanShadowCandidateGenerated = false;
    bool preserveTextureCandidateGenerated = false;
    bool highlightProtectedMidsGenerated = false;
    bool highlightProtectedMidsEligible = false;
    bool highlightProtectedMidsMeaningfullyDifferent = false;
    bool finishToneProbeGenerated = false;
    bool finishToneProbeEligible = false;
    bool finishToneProbeHumanReadable = false;
    bool finishToneProbeMeaningfullyDifferent = false;
    bool naturalContrastGuardGenerated = false;
    bool naturalContrastGuardEligible = false;
    bool naturalContrastGuardHumanReadable = false;
    bool naturalContrastGuardDiagnosticsWritten = false;
    bool brightHighlightRolloffGenerated = false;
    bool brightHighlightRolloffEligible = false;
    bool brightHighlightRolloffHumanReadable = false;
    bool brightHighlightRolloffDiagnosticsWritten = false;
    bool luminousHighlightAnchorGenerated = false;
    bool luminousHighlightAnchorEligible = false;
    bool luminousHighlightAnchorHumanReadable = false;
    bool luminousHighlightAnchorDiagnosticsWritten = false;
    bool specularHighlightToleranceGenerated = false;
    bool specularHighlightToleranceEligible = false;
    bool specularHighlightToleranceHumanReadable = false;
    bool specularHighlightToleranceDiagnosticsWritten = false;
    bool modeNeighborCandidateGenerated = false;
    bool modeNeighborCandidateEligible = false;
    bool modeNeighborCandidateHumanReadable = false;
    bool modeNeighborCandidateMeaningfullyDifferent = false;
    bool whiteBalanceProbeGenerated = false;
    bool whiteBalanceProbeEligible = false;
    bool whiteBalanceProbeHumanReadable = false;
    bool whiteBalanceProbeDiagnosticsWritten = false;

    std::string finishToneProbeId;
    std::string whiteBalanceProbeId;
    std::string whiteBalanceProbeMode;
};

DevelopAutoSolveCandidateProbeSummary BuildDevelopAutoSolveCandidateProbeSummary(
    const nlohmann::json& candidateSolves,
    EditorNodeGraph::DevelopAutoGuidance fallbackGuidance);

} // namespace Stack::Validation::Detail
