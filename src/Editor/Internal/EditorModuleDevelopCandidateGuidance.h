#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"

namespace Stack::Editor::DevelopCandidateGuidance {

void SetDevelopResultWhiteBalanceProbe(
    DevelopCandidateScoring::DevelopAutoCandidateSolveResult& result,
    const DevelopCandidateScoring::DevelopAutoCandidateSolve& candidate);

void ClearDevelopResultWhiteBalanceProbe(
    DevelopCandidateScoring::DevelopAutoCandidateSolveResult& result);

EditorNodeGraph::DevelopAutoGuidance AdjustDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& base,
    float exposureBias,
    float dynamicRangeBias,
    float shadowLiftBias,
    float highlightGuardBias,
    float highlightCharacterBias,
    float contrastBias);

EditorNodeGraph::DevelopAutoGuidance BlendDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b,
    float aWeight);

EditorNodeGraph::DevelopAutoGuidance BlendDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b,
    const EditorNodeGraph::DevelopAutoGuidance& c,
    float aWeight,
    float bWeight,
    float cWeight);

} // namespace Stack::Editor::DevelopCandidateGuidance
