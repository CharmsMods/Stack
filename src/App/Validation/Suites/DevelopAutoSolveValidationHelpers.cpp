#include "App/Validation/Suites/DevelopAutoSolveValidationHelpers.h"

#include "Editor/EditorModule.h"

namespace Stack::Validation::Detail {

bool DevelopAutoSolveStageHasState(const nlohmann::json& stageSolves, const std::string& state) {
    if (!stageSolves.is_array()) {
        return false;
    }
    for (const nlohmann::json& stage : stageSolves) {
        if (stage.is_object() &&
            stage.value("state", std::string()) == state &&
            !stage.value("status", std::string()).empty()) {
            return true;
        }
    }
    return false;
}

EditorNodeGraph::DevelopAutoGuidance DevelopAutoSolveGuidanceFromToneJson(
    const nlohmann::json& toneJson,
    EditorNodeGraph::DevelopAutoGuidance fallback) {
    fallback.autoStrength = toneJson.value("autoSceneAssistStrength", fallback.autoStrength);
    fallback.exposureBias = toneJson.value("autoBrightnessIntent", fallback.exposureBias);
    fallback.dynamicRange = toneJson.value("autoDynamicRange", fallback.dynamicRange);
    fallback.shadowLift = toneJson.value("autoShadowBias", fallback.shadowLift);
    fallback.highlightGuard = toneJson.value("autoHighlightBias", fallback.highlightGuard);
    fallback.highlightCharacter = toneJson.value("autoHighlightCharacter", fallback.highlightCharacter);
    fallback.contrastBias = toneJson.value("autoContrastBias", fallback.contrastBias);
    fallback.subjectSceneBias = toneJson.value("autoSubjectSceneBias", fallback.subjectSceneBias);
    fallback.moodReadabilityBias = toneJson.value("autoMoodReadabilityBias", fallback.moodReadabilityBias);
    EditorModule::NormalizeDevelopAutoGuidance(fallback);
    return fallback;
}

EditorNodeGraph::DevelopAutoGuidance DevelopAutoSolveGuidanceFromCandidateJson(
    const nlohmann::json& guidanceJson,
    EditorNodeGraph::DevelopAutoGuidance fallback) {
    fallback.autoStrength = guidanceJson.value("autoStrength", fallback.autoStrength);
    fallback.exposureBias = guidanceJson.value("brightnessIntent", fallback.exposureBias);
    fallback.dynamicRange = guidanceJson.value("dynamicRange", fallback.dynamicRange);
    fallback.shadowLift = guidanceJson.value("shadowLift", fallback.shadowLift);
    fallback.highlightGuard = guidanceJson.value("highlightGuard", fallback.highlightGuard);
    fallback.highlightCharacter = guidanceJson.value("highlightCharacter", fallback.highlightCharacter);
    fallback.contrastBias = guidanceJson.value("contrastBias", fallback.contrastBias);
    fallback.subjectSceneBias = guidanceJson.value("subjectSceneBias", fallback.subjectSceneBias);
    fallback.moodReadabilityBias = guidanceJson.value("moodReadabilityBias", fallback.moodReadabilityBias);
    EditorModule::NormalizeDevelopAutoGuidance(fallback);
    return fallback;
}

bool IsDevelopAutoSolveFinishToneProbeId(const std::string& id) {
    return
        id == "toneSofterRolloff" ||
        id == "naturalContrastGuard" ||
        id == "brightHighlightRolloff" ||
        id == "luminousHighlightAnchor" ||
        id == "specularHighlightTolerance" ||
        id == "tonePunchierShape" ||
        id == "toneFlatterEditing" ||
        id == "toneDarkerToe";
}

bool IsDevelopAutoSolveWhiteBalanceProbeId(const std::string& id) {
    return
        id == "wbDaylightCorrection" ||
        id == "wbNeutralCorrection" ||
        id == "wbCameraMood";
}

} // namespace Stack::Validation::Detail
