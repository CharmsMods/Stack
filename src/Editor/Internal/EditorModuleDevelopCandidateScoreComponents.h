#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"

namespace Stack::Editor::DevelopCandidateScoring {

nlohmann::json BuildDevelopAutoCandidateScoreComponents(
    const DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base,
    EditorNodeGraph::DevelopAutoIntent intent,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopDynamicRange::DevelopDynamicRangeRegionEvidence& regionEvidence,
    const DevelopDynamicRange::DevelopDynamicRangeStrategy& dynamicRangeStrategy,
    const DevelopSubjectImportance::DevelopSubjectSceneIntent& subjectSceneIntent,
    float darkness,
    float shadowRescueNeed,
    float hdrNeed,
    float flatSceneNeed,
    float underBrightBroadHighlightEv);

nlohmann::json BuildFallbackDevelopAutoCandidateScoreComponents(
    const DevelopAutoCandidateSolve& candidate,
    const EditorNodeGraph::DevelopAutoGuidance& base);

} // namespace Stack::Editor::DevelopCandidateScoring
