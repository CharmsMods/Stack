#pragma once

#include "Editor/Internal/EditorModuleDevelopCandidateScoring.h"
#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

namespace Stack::Editor::DevelopAutoSolveApplication {

struct DevelopAutoIntentProfile {
    float autoStrengthScale = 1.0f;
    float autoStrengthBias = 0.0f;
    float exposureBias = 0.0f;
    float dynamicRangeBias = 0.0f;
    float shadowLiftBias = 0.0f;
    float highlightGuardBias = 0.0f;
    float highlightCharacterBias = 0.0f;
    float contrastBias = 0.0f;
    float rawExposureBias = 0.0f;
    float rawLiftScale = 1.0f;
    float rawHighlightRecoveryBias = 0.0f;
    float rawNoiseBias = 0.0f;
    float prepStrengthBias = 0.0f;
    float prepShadowBias = 0.0f;
    float prepHighlightBias = 0.0f;
    float prepContrastLift = 0.0f;
    float prepNoiseBias = 0.0f;
};

DevelopAutoIntentProfile ResolveDevelopAutoIntentProfile(EditorNodeGraph::DevelopAutoIntent intent);

EditorNodeGraph::DevelopAutoGuidance BuildModeAwareDevelopGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopAutoIntentProfile& profile);

void ApplyDevelopSelectedAutoSolve(
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance,
    const DevelopAutoIntentProfile& intentProfile,
    nlohmann::json& toneJson,
    bool queueToneCalibration,
    bool rewriteRawSettings);

} // namespace Stack::Editor::DevelopAutoSolveApplication
