#include "Editor/Internal/EditorModuleDevelopCandidateShared.h"

#include <algorithm>

namespace Stack::Editor::DevelopCandidate {

bool IsSubjectIntentProbeCandidateIdForRenderRequest(const std::string& candidateId) {
    return
        candidateId == "subjectReadableMids" ||
        candidateId == "sceneMoodPreservation";
}

bool IsFinishToneProbeCandidateIdForRenderRequest(const std::string& candidateId) {
    return
        candidateId == "strongerContrast" ||
        candidateId == "toneSofterRolloff" ||
        candidateId == "naturalContrastGuard" ||
        candidateId == "brightHighlightRolloff" ||
        candidateId == "luminousHighlightAnchor" ||
        candidateId == "specularHighlightTolerance" ||
        candidateId == "tonePunchierShape" ||
        candidateId == "toneFlatterEditing" ||
        candidateId == "toneDarkerToe" ||
        candidateId == "renderedLocalContrastShape";
}

bool TryResolveWhiteBalanceProbeCandidateModeForRenderRequest(
    const std::string& candidateId,
    Raw::WhiteBalanceMode& outMode) {
    if (candidateId == "wbNeutralCorrection") {
        outMode = Raw::WhiteBalanceMode::Neutral;
        return true;
    }
    if (candidateId == "wbDaylightCorrection") {
        outMode = Raw::WhiteBalanceMode::Auto;
        return true;
    }
    if (candidateId == "wbCameraMood") {
        outMode = Raw::WhiteBalanceMode::AsShot;
        return true;
    }
    return false;
}

bool IsWhiteBalanceProbeCandidateIdForRenderRequest(const std::string& candidateId) {
    Raw::WhiteBalanceMode mode = Raw::WhiteBalanceMode::AsShot;
    return TryResolveWhiteBalanceProbeCandidateModeForRenderRequest(candidateId, mode);
}

std::string DevelopRenderedRevisionStageForRefineIntent(const std::string& refineIntent) {
    if (refineIntent == "protectHighlights") {
        return "rawGlobal";
    }
    if (refineIntent == "brightenMids" || refineIntent == "openShadows") {
        return "scenePrep";
    }
    if (refineIntent == "addContrast") {
        return "finishTone";
    }
    if (refineIntent == "cleanShadows" || refineIntent == "preserveTexture") {
        return "rawCleanup";
    }
    return "multiStage";
}

std::string DevelopRenderedRevisionStageForCandidateId(const std::string& candidateId) {
    if (candidateId == "protectHighlights" ||
        candidateId == "highlightProtectedMids" ||
        candidateId == "renderedLocalHighlightRestraint" ||
        IsWhiteBalanceProbeCandidateIdForRenderRequest(candidateId)) {
        return "rawGlobal";
    }
    if (candidateId == "brighterMids" ||
        candidateId == "maximumRange" ||
        candidateId == "broadHighlightGuard" ||
        candidateId == "haloSafeLocalRange" ||
        candidateId == "localRangeGuard" ||
        candidateId == "shadowReadabilityLift" ||
        candidateId == "shadowNoiseFloor" ||
        IsSubjectIntentProbeCandidateIdForRenderRequest(candidateId) ||
        candidateId == "renderedLocalBrightenMids" ||
        candidateId == "renderedLocalShadowOpening") {
        return "scenePrep";
    }
    if (IsFinishToneProbeCandidateIdForRenderRequest(candidateId)) {
        return "finishTone";
    }
    if (candidateId == "cleanShadows" ||
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture") {
        return "rawCleanup";
    }
    return "multiStage";
}

std::string DevelopExpectedDirtyBoundaryForCandidateStage(const std::string& candidateRevisionStage) {
    if (candidateRevisionStage == "finishTone") {
        return "finishTone";
    }
    if (candidateRevisionStage == "scenePrep") {
        return "scenePrep";
    }
    return "rawBase";
}

int DevelopCandidateStageScheduleRank(
    const std::string& candidateRevisionStage,
    bool selectedCandidate) {
    if (selectedCandidate) {
        return 0;
    }
    if (candidateRevisionStage == "finishTone") {
        return 1;
    }
    if (candidateRevisionStage == "scenePrep") {
        return 2;
    }
    if (candidateRevisionStage == "rawGlobal" ||
        candidateRevisionStage == "rawCleanup") {
        return 3;
    }
    return 4;
}

std::string DevelopCandidateStageSchedulerReason(
    const std::string& candidateRevisionStage,
    bool selectedCandidate) {
    if (selectedCandidate) {
        return "Selected candidate renders first to establish the baseline RAW, pre-finish, and final cache boundaries.";
    }
    if (candidateRevisionStage == "finishTone") {
        return "Finish-tone probes are downstream-only, so they render immediately after the selected baseline while RAW and pre-finish caches are still reusable.";
    }
    if (candidateRevisionStage == "scenePrep") {
        return "Scene-prep probes keep RAW frozen and render before RAW-dirty probes so the selected RAW base can be reused.";
    }
    if (candidateRevisionStage == "rawGlobal") {
        return "RAW/global probes can replace RAW-base and pre-finish caches, so they render after downstream reuse-sensitive probes.";
    }
    if (candidateRevisionStage == "rawCleanup") {
        return "RAW cleanup probes change denoise or cleanup inputs and can dirty RAW base, so they render after downstream reuse-sensitive probes.";
    }
    return "Multi-stage probes may dirty several boundaries, so they render after stage-constrained probes.";
}

std::string DevelopObservedDirtyBoundaryFromCacheHits(
    bool rawBaseCacheHit,
    bool preFinishCacheHit) {
    if (!rawBaseCacheHit) {
        return "rawBase";
    }
    if (!preFinishCacheHit) {
        return "scenePrep";
    }
    return "finishTone";
}

DevelopStageCacheValidation EvaluateDevelopStageCacheValidation(
    const std::string& candidateRevisionStage,
    bool rawBaseCacheHit,
    bool preFinishCacheHit) {
    DevelopStageCacheValidation validation;
    if (candidateRevisionStage == "scenePrep") {
        validation.evaluated = true;
        validation.expectedRawBaseReuse = true;
        validation.expectedBoundary = "scenePrep";
        validation.met = rawBaseCacheHit;
        validation.status = validation.met ? "met" : "missedRawBaseReuse";
        validation.reason = validation.met
            ? "Scene-prep candidates should reuse the RAW base while allowing the pre-finish boundary to rerender."
            : "Scene-prep candidate dirtied the RAW base even though the stage constraint should have frozen RAW-stage settings.";
        return validation;
    }
    if (candidateRevisionStage == "finishTone") {
        validation.evaluated = true;
        validation.expectedRawBaseReuse = true;
        validation.expectedPreFinishReuse = true;
        validation.expectedBoundary = "finishTone";
        validation.met = rawBaseCacheHit && preFinishCacheHit;
        validation.status = validation.met
            ? "met"
            : (!rawBaseCacheHit ? "missedRawBaseReuse" : "missedPreFinishReuse");
        validation.reason = validation.met
            ? "Finish-tone candidates should reuse RAW base and pre-finish boundaries, changing only the downstream finish."
            : (!rawBaseCacheHit
                ? "Finish-tone candidate dirtied the RAW base; the stage constraint should have frozen RAW-stage settings."
                : "Finish-tone candidate dirtied the pre-finish boundary; the stage constraint should have frozen scene-prep settings.");
        return validation;
    }
    if (candidateRevisionStage == "rawGlobal" || candidateRevisionStage == "rawCleanup" || candidateRevisionStage == "multiStage") {
        validation.expectedBoundary = "rawBase";
        validation.reason =
            "This candidate may legitimately dirty the RAW base in the current physical render path.";
    }
    return validation;
}

} // namespace Stack::Editor::DevelopCandidate
