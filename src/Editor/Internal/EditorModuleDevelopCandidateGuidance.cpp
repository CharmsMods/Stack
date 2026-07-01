#include "Editor/Internal/EditorModuleDevelopCandidateGuidance.h"

#include "Editor/EditorModule.h"

#include <algorithm>

namespace Stack::Editor::DevelopCandidateGuidance {

using Stack::Editor::DevelopCandidateScoring::DevelopAutoCandidateSolve;
using Stack::Editor::DevelopCandidateScoring::DevelopAutoCandidateSolveResult;

void SetDevelopResultWhiteBalanceProbe(
    DevelopAutoCandidateSolveResult& result,
    const DevelopAutoCandidateSolve& candidate) {
    result.authoredWhiteBalanceProbe = candidate.whiteBalanceProbe;
    result.authoredWhiteBalanceMode = candidate.whiteBalanceMode;
}

void ClearDevelopResultWhiteBalanceProbe(DevelopAutoCandidateSolveResult& result) {
    result.authoredWhiteBalanceProbe = false;
    result.authoredWhiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
}

EditorNodeGraph::DevelopAutoGuidance AdjustDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& base,
    float exposureBias,
    float dynamicRangeBias,
    float shadowLiftBias,
    float highlightGuardBias,
    float highlightCharacterBias,
    float contrastBias) {
    EditorNodeGraph::DevelopAutoGuidance adjusted = base;
    adjusted.exposureBias += exposureBias;
    adjusted.dynamicRange += dynamicRangeBias;
    adjusted.shadowLift += shadowLiftBias;
    adjusted.highlightGuard += highlightGuardBias;
    adjusted.highlightCharacter += highlightCharacterBias;
    adjusted.contrastBias += contrastBias;
    EditorModule::NormalizeDevelopAutoGuidance(adjusted);
    return adjusted;
}

EditorNodeGraph::DevelopAutoGuidance BlendDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b,
    float aWeight) {
    const float safeAWeight = std::clamp(aWeight, 0.0f, 1.0f);
    const float bWeight = 1.0f - safeAWeight;
    EditorNodeGraph::DevelopAutoGuidance blended = a;
    blended.autoStrength = a.autoStrength * safeAWeight + b.autoStrength * bWeight;
    blended.exposureBias = a.exposureBias * safeAWeight + b.exposureBias * bWeight;
    blended.dynamicRange = a.dynamicRange * safeAWeight + b.dynamicRange * bWeight;
    blended.shadowLift = a.shadowLift * safeAWeight + b.shadowLift * bWeight;
    blended.highlightGuard = a.highlightGuard * safeAWeight + b.highlightGuard * bWeight;
    blended.highlightCharacter = a.highlightCharacter * safeAWeight + b.highlightCharacter * bWeight;
    blended.contrastBias = a.contrastBias * safeAWeight + b.contrastBias * bWeight;
    blended.subjectSceneBias = a.subjectSceneBias * safeAWeight + b.subjectSceneBias * bWeight;
    blended.moodReadabilityBias = a.moodReadabilityBias * safeAWeight + b.moodReadabilityBias * bWeight;
    EditorModule::NormalizeDevelopAutoGuidance(blended);
    return blended;
}

EditorNodeGraph::DevelopAutoGuidance BlendDevelopAutoCandidateGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b,
    const EditorNodeGraph::DevelopAutoGuidance& c,
    float aWeight,
    float bWeight,
    float cWeight) {
    const float safeAWeight = std::max(0.0f, aWeight);
    const float safeBWeight = std::max(0.0f, bWeight);
    const float safeCWeight = std::max(0.0f, cWeight);
    const float weightSum = safeAWeight + safeBWeight + safeCWeight;
    if (weightSum <= 0.0001f) {
        return a;
    }

    const float normalizedA = safeAWeight / weightSum;
    const float normalizedB = safeBWeight / weightSum;
    const float normalizedC = safeCWeight / weightSum;
    EditorNodeGraph::DevelopAutoGuidance blended = a;
    blended.autoStrength =
        a.autoStrength * normalizedA + b.autoStrength * normalizedB + c.autoStrength * normalizedC;
    blended.exposureBias =
        a.exposureBias * normalizedA + b.exposureBias * normalizedB + c.exposureBias * normalizedC;
    blended.dynamicRange =
        a.dynamicRange * normalizedA + b.dynamicRange * normalizedB + c.dynamicRange * normalizedC;
    blended.shadowLift =
        a.shadowLift * normalizedA + b.shadowLift * normalizedB + c.shadowLift * normalizedC;
    blended.highlightGuard =
        a.highlightGuard * normalizedA + b.highlightGuard * normalizedB + c.highlightGuard * normalizedC;
    blended.highlightCharacter =
        a.highlightCharacter * normalizedA + b.highlightCharacter * normalizedB + c.highlightCharacter * normalizedC;
    blended.contrastBias =
        a.contrastBias * normalizedA + b.contrastBias * normalizedB + c.contrastBias * normalizedC;
    blended.subjectSceneBias =
        a.subjectSceneBias * normalizedA + b.subjectSceneBias * normalizedB + c.subjectSceneBias * normalizedC;
    blended.moodReadabilityBias =
        a.moodReadabilityBias * normalizedA + b.moodReadabilityBias * normalizedB + c.moodReadabilityBias * normalizedC;
    EditorModule::NormalizeDevelopAutoGuidance(blended);
    return blended;
}

} // namespace Stack::Editor::DevelopCandidateGuidance
