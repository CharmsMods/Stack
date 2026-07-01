#include "Editor/EditorModule.h"

#include "Editor/Internal/EditorModuleDevelopAutoSolveApplication.h"
#include "Editor/Internal/EditorModuleDevelopCandidateGeneration.h"
#include "Editor/Internal/EditorModuleDevelopDefaults.h"
#include "Editor/Internal/EditorModuleDevelopDynamicRangeStrategy.h"
#include "Editor/Internal/EditorModuleDevelopSubjectImportance.h"

#include <algorithm>

using Stack::Editor::DevelopDefaults::BuildDefaultIntegratedToneLayerJson;
using namespace Stack::Editor::DevelopAutoSolveApplication;
using namespace Stack::Editor::DevelopCandidateGeneration;
using namespace Stack::Editor::DevelopDynamicRange;
using namespace Stack::Editor::DevelopSubjectImportance;

void EditorModule::NormalizeDevelopAutoGuidance(EditorNodeGraph::DevelopAutoGuidance& guidance) {
    guidance.autoStrength = std::clamp(guidance.autoStrength, 0.0f, 2.4f);
    guidance.exposureBias = std::clamp(guidance.exposureBias, -2.0f, 2.0f);
    guidance.dynamicRange = std::clamp(guidance.dynamicRange, 0.25f, 3.0f);
    guidance.shadowLift = std::clamp(guidance.shadowLift, -1.25f, 1.25f);
    guidance.highlightGuard = std::clamp(guidance.highlightGuard, -1.25f, 1.25f);
    guidance.highlightCharacter = std::clamp(guidance.highlightCharacter, -1.25f, 1.25f);
    guidance.contrastBias = std::clamp(guidance.contrastBias, -1.25f, 1.25f);
    guidance.subjectSceneBias = std::clamp(guidance.subjectSceneBias, -1.0f, 1.0f);
    guidance.moodReadabilityBias = std::clamp(guidance.moodReadabilityBias, -1.0f, 1.0f);
}


void EditorModule::ApplyDevelopAutoSolve(
    EditorNodeGraph::RawDevelopPayload& payload,
    const Raw::RawMetadata& metadata,
    bool queueToneCalibration,
    bool rewriteRawSettings) {
    NormalizeDevelopAutoGuidance(payload.autoGuidance);
    NormalizeDevelopSubjectImportance(payload.subjectImportance);
    const DevelopAutoIntentProfile intentProfile = ResolveDevelopAutoIntentProfile(payload.autoGuidance.intent);
    const EditorNodeGraph::DevelopAutoGuidance modeGuidance =
        BuildModeAwareDevelopGuidance(payload.autoGuidance, intentProfile);

    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = BuildDefaultIntegratedToneLayerJson();
    }

    nlohmann::json toneJson = payload.integratedToneLayerJson;
    const DevelopToneAutoStats stats = ReadDevelopToneAutoStats(toneJson);
    const Stack::Editor::DevelopCandidateScoring::DevelopAutoCandidateSolveResult candidateSolve =
        BuildDevelopAutoCandidateSolve(
            modeGuidance,
            payload.autoGuidance.intent,
            payload.subjectImportance,
            stats,
            metadata,
            toneJson);
    ApplyDevelopSelectedAutoSolve(
        payload,
        metadata,
        stats,
        candidateSolve,
        modeGuidance,
        intentProfile,
        toneJson,
        queueToneCalibration,
        rewriteRawSettings);
}

