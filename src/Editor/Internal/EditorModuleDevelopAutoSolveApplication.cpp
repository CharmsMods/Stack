#include "Editor/Internal/EditorModuleDevelopAutoSolveApplication.h"

#include "Editor/Internal/EditorModuleDevelopAutoSolveApplicationContext.h"

#include <algorithm>
#include <utility>

using namespace Stack::Editor::DevelopCandidateScoring;
using namespace Stack::Editor::DevelopDynamicRange;

namespace Stack::Editor::DevelopAutoSolveApplication {

DevelopAutoIntentProfile ResolveDevelopAutoIntentProfile(EditorNodeGraph::DevelopAutoIntent intent) {
    DevelopAutoIntentProfile profile;
    switch (intent) {
        case EditorNodeGraph::DevelopAutoIntent::CleanBase:
            // Keep the data tidy and editable, with less final-tone commitment.
            profile.autoStrengthScale = 0.90f;
            profile.dynamicRangeBias = -0.05f;
            profile.highlightGuardBias = 0.08f;
            profile.contrastBias = -0.16f;
            profile.rawNoiseBias = 0.06f;
            profile.prepStrengthBias = -0.08f;
            profile.prepNoiseBias = 0.06f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::FlatEditingBase:
            // Open useful mids/range for manual work without pretending it is a log dump.
            profile.autoStrengthBias = 0.05f;
            profile.dynamicRangeBias = 0.38f;
            profile.shadowLiftBias = 0.24f;
            profile.highlightGuardBias = 0.20f;
            profile.contrastBias = -0.38f;
            profile.rawLiftScale = 1.05f;
            profile.prepStrengthBias = 0.08f;
            profile.prepShadowBias = 0.18f;
            profile.prepHighlightBias = 0.16f;
            profile.prepContrastLift = -0.16f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::BrightNatural:
            // Brighter render intent should land mostly through placement, not reckless clipping.
            profile.exposureBias = 0.16f;
            profile.shadowLiftBias = 0.10f;
            profile.highlightGuardBias = 0.08f;
            profile.contrastBias = -0.04f;
            profile.rawExposureBias = 0.12f;
            profile.prepShadowBias = 0.08f;
            profile.prepContrastLift = 0.04f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::DarkNatural:
            // Preserve low-key mood and avoid forcing dark scenes into gray mids.
            profile.exposureBias = -0.16f;
            profile.shadowLiftBias = -0.18f;
            profile.highlightGuardBias = 0.04f;
            profile.contrastBias = 0.06f;
            profile.rawExposureBias = -0.14f;
            profile.rawLiftScale = 0.82f;
            profile.prepStrengthBias = -0.08f;
            profile.prepShadowBias = -0.18f;
            profile.prepContrastLift = -0.05f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast:
            // Add separation and endpoint confidence while still using highlight safeguards.
            profile.autoStrengthScale = 1.05f;
            profile.dynamicRangeBias = -0.12f;
            profile.shadowLiftBias = -0.16f;
            profile.highlightGuardBias = 0.06f;
            profile.highlightCharacterBias = 0.20f;
            profile.contrastBias = 0.36f;
            profile.rawLiftScale = 0.92f;
            profile.prepStrengthBias = 0.03f;
            profile.prepShadowBias = -0.10f;
            profile.prepContrastLift = 0.18f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail:
            // Fit more visible range while keeping language honest about clipped data.
            profile.autoStrengthBias = 0.10f;
            profile.dynamicRangeBias = 0.58f;
            profile.shadowLiftBias = 0.34f;
            profile.highlightGuardBias = 0.42f;
            profile.highlightCharacterBias = -0.10f;
            profile.contrastBias = -0.24f;
            profile.rawLiftScale = 1.08f;
            profile.rawHighlightRecoveryBias = 0.14f;
            profile.prepStrengthBias = 0.14f;
            profile.prepShadowBias = 0.26f;
            profile.prepHighlightBias = 0.28f;
            profile.prepContrastLift = -0.10f;
            break;
        case EditorNodeGraph::DevelopAutoIntent::NaturalFinished:
        default:
            break;
    }
    return profile;
}

EditorNodeGraph::DevelopAutoGuidance BuildModeAwareDevelopGuidance(
    const EditorNodeGraph::DevelopAutoGuidance& guidance,
    const DevelopAutoIntentProfile& profile) {
    EditorNodeGraph::DevelopAutoGuidance effective = guidance;
    effective.autoStrength = std::clamp(
        guidance.autoStrength * profile.autoStrengthScale + profile.autoStrengthBias,
        0.0f,
        2.4f);
    effective.exposureBias = std::clamp(guidance.exposureBias + profile.exposureBias, -2.0f, 2.0f);
    effective.dynamicRange = std::clamp(guidance.dynamicRange + profile.dynamicRangeBias, 0.25f, 3.0f);
    effective.shadowLift = std::clamp(guidance.shadowLift + profile.shadowLiftBias, -1.25f, 1.25f);
    effective.highlightGuard = std::clamp(guidance.highlightGuard + profile.highlightGuardBias, -1.25f, 1.25f);
    effective.highlightCharacter = std::clamp(guidance.highlightCharacter + profile.highlightCharacterBias, -1.25f, 1.25f);
    effective.contrastBias = std::clamp(guidance.contrastBias + profile.contrastBias, -1.25f, 1.25f);
    effective.subjectSceneBias = std::clamp(guidance.subjectSceneBias, -1.0f, 1.0f);
    effective.moodReadabilityBias = std::clamp(guidance.moodReadabilityBias, -1.0f, 1.0f);
    return effective;
}

void ApplyDevelopSelectedAutoSolve(
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    const DevelopToneAutoStats& stats,
    const DevelopAutoCandidateSolveResult& candidateSolve,
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance,
    const DevelopAutoIntentProfile& intentProfile,
    nlohmann::json& toneJson,
    bool queueToneCalibration,
    bool rewriteRawSettings) {
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance = candidateSolve.authoredGuidance;

    WriteDevelopSelectedSolveTonePreamble(toneJson, payload, candidateSolve, modeGuidance);

    const DevelopSelectedAutoSolveContext context = BuildDevelopSelectedAutoSolveContext(
        metadata,
        stats,
        candidateSolve,
        solveGuidance,
        intentProfile);
    ApplyDevelopSelectedRawSettings(payload, metadata, stats, candidateSolve, context, rewriteRawSettings);
    const Raw::RawDetailFusionSettings prepSettings =
        ApplyDevelopSelectedScenePrepSettings(payload, stats, context);

    WriteDevelopSelectedSolveToneDiagnostics(
        toneJson,
        metadata,
        payload,
        prepSettings,
        stats,
        candidateSolve,
        context);

    if (queueToneCalibration) {
        QueueDevelopToneCalibration(toneJson);
    }
    payload.integratedToneLayerJson = std::move(toneJson);
}

} // namespace Stack::Editor::DevelopAutoSolveApplication
