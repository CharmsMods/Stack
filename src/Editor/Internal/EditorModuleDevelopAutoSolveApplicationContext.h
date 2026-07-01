#pragma once

#include "Editor/Internal/EditorModuleDevelopAutoSolveApplication.h"

namespace Stack::Editor::DevelopAutoSolveApplication {

enum class DevelopAutoSceneProfile : int {
    Balanced = 0,
    HighlightHeavy = 1,
    ShadowHeavy = 2,
    Flat = 3,
    NoisyLowLight = 4
};

struct DevelopSelectedAutoSolveContext {
    Raw::RawDevelopSettings defaults;
    nlohmann::json localExposureStrategy = nlohmann::json::object();

    DevelopAutoSceneProfile sceneProfile = DevelopAutoSceneProfile::Balanced;

    float autoStrength = 0.0f;
    float exposureBias = 0.0f;
    float dynamicRange = 1.0f;
    float shadowLift = 0.0f;
    float highlightGuard = 0.0f;
    float highlightCharacter = 0.0f;
    float contrastBias = 0.0f;

    float localExposureRangeRedistribution = 0.0f;
    float localExposureHighlightCompression = 0.0f;
    float localExposureShadowOpening = 0.0f;
    float localExposureNoiseGuard = 0.0f;
    float localExposureHaloGuard = 0.0f;
    float localExposureTextureGuard = 0.0f;
    float localExposureShadowEvBudget = 0.0f;
    float localExposureHighlightEvBudget = 0.0f;
    float localExposureStrengthTarget = 0.5f;

    float darkness = 0.0f;
    float hdrNeed = 0.0f;
    float flatSceneNeed = 0.0f;
    float shadowRescueNeed = 0.0f;
    float stableSceneGuard = 0.0f;
    float brightWindowNeed = 0.0f;
    float highlightNeed = 0.0f;
    float highlightHeavyNeed = 0.0f;
    float rawBaselineLiftNeed = 0.0f;

    float rawExposureBias = 0.0f;
    float prepStrengthBias = 0.0f;
    float prepShadowBias = 0.0f;
    float prepHighlightBias = 0.0f;
    float prepContrastLift = 0.0f;
    float prepNoiseBias = 0.0f;

    float noiseNeed = 0.0f;
    float darkNoisyLowLightDenoiseNeed = 0.0f;
    float shadowBoost = 0.0f;
    float recommendedLift = 0.0f;
    float recommendedTrim = 0.0f;
    float userBiasEv = 0.0f;
    float shadowBoostScale = 1.0f;
    float recommendedLiftScale = 1.0f;
    float highlightExposurePenalty = 0.0f;
    float rawLiftHeadroomScale = 1.0f;
    float broadHighlightPlacementLift = 0.0f;
    float highlightModeScore = 0.0f;
};

DevelopSelectedAutoSolveContext BuildDevelopSelectedAutoSolveContext(
    const Raw::RawMetadata& metadata,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopAutoIntentProfile& intentProfile);

void WriteDevelopSelectedSolveTonePreamble(
    nlohmann::json& toneJson,
    const EditorNodeGraph::RawDevelopPayload& payload,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance);

void ApplyDevelopSelectedRawSettings(
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const DevelopSelectedAutoSolveContext& context,
    bool rewriteRawSettings);

Raw::RawDetailFusionSettings ApplyDevelopSelectedScenePrepSettings(
    EditorNodeGraph::RawDevelopPayload& payload,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopSelectedAutoSolveContext& context);

void WriteDevelopSelectedSolveToneDiagnostics(
    nlohmann::json& toneJson,
    const Raw::RawMetadata& metadata,
    const EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawDetailFusionSettings& prepSettings,
    const DevelopDynamicRange::DevelopToneAutoStats& stats,
    const DevelopCandidateScoring::DevelopAutoCandidateSolveResult& candidateSolve,
    const DevelopSelectedAutoSolveContext& context);

void QueueDevelopToneCalibration(nlohmann::json& toneJson);

} // namespace Stack::Editor::DevelopAutoSolveApplication
