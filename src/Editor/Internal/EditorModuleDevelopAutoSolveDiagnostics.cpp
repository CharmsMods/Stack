#include "Editor/Internal/EditorModuleDevelopAutoSolveDiagnostics.h"

#include "Editor/Internal/EditorModuleDevelopCandidateScoreComponents.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h"
#include "Editor/Internal/EditorModuleDevelopSubjectImportance.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace Stack::Editor::DevelopAutoSolveDiagnostics {

using namespace Stack::Editor::DevelopCandidateScoring;
using namespace Stack::Editor::DevelopDynamicRange;
using namespace Stack::Editor::DevelopRenderedFeedback;
using namespace Stack::Editor::DevelopSubjectImportance;

namespace {

constexpr const char* kDevelopContinuationCandidateBiasVersion = "ContinuationCandidateBiasV1";
constexpr const char* kDevelopContinuationCandidateExpansionVersion = "ContinuationCandidateExpansionV1";

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

struct DevelopAutoStageFingerprints {
    std::uint64_t metadata = 0;
    std::uint64_t rawBase = 0;
    std::uint64_t rawGlobal = 0;
    std::uint64_t scenePrep = 0;
    std::uint64_t finishTone = 0;
    std::uint64_t finalValidation = 0;
};

struct DevelopAutoStageHashBuilder {
    std::uint64_t hash = 1469598103934665603ull;

    void AddValue(std::uint64_t value) {
        hash ^= value;
        hash *= 1099511628211ull;
    }

    void AddBool(bool value) {
        AddValue(value ? 1ull : 0ull);
    }

    void AddInt(int value) {
        AddValue(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    }

    void AddFloat(float value, float scale = 1000.0f) {
        const int quantized = static_cast<int>(std::lround(value * scale));
        AddInt(quantized);
    }

    void AddString(const std::string& value) {
        for (unsigned char ch : value) {
            AddValue(static_cast<std::uint64_t>(ch));
        }
    }
};

std::string DevelopAutoStageStateForRevisionStage(const std::string& stage) {
    if (stage == "rawCleanup") {
        return "RENDER_RAW_BASE";
    }
    if (stage == "rawGlobal") {
        return "SOLVE_GLOBAL";
    }
    if (stage == "scenePrep") {
        return "SOLVE_SCENE_PREP";
    }
    if (stage == "finishTone") {
        return "SOLVE_FINISH_TONE";
    }
    if (stage == "converged") {
        return "CONVERGED";
    }
    if (stage == "none") {
        return "VALIDATE_FINAL";
    }
    return "SOLVE_GLOBAL";
}

int DevelopAutoStageOrder(const std::string& stageState) {
    if (stageState == "NEED_SOURCE") return 0;
    if (stageState == "METADATA_BOOTSTRAP") return 1;
    if (stageState == "RENDER_RAW_BASE") return 2;
    if (stageState == "ANALYZE_RAW_BASE") return 3;
    if (stageState == "SOLVE_GLOBAL") return 4;
    if (stageState == "RENDER_GLOBAL_BASE") return 5;
    if (stageState == "SOLVE_SCENE_PREP") return 6;
    if (stageState == "RENDER_PREFINISH") return 7;
    if (stageState == "ANALYZE_PREFINISH") return 8;
    if (stageState == "SOLVE_FINISH_TONE") return 9;
    if (stageState == "RENDER_FINAL") return 10;
    if (stageState == "VALIDATE_FINAL") return 11;
    if (stageState == "CONVERGED") return 12;
    return 4;
}

std::string DevelopAutoStageForChangedFingerprint(
    const nlohmann::json& previousFingerprints,
    const DevelopAutoStageFingerprints& current) {
    if (!previousFingerprints.is_object()) {
        return "METADATA_BOOTSTRAP";
    }
    if (previousFingerprints.value("metadata", static_cast<std::uint64_t>(0)) != current.metadata) {
        return "METADATA_BOOTSTRAP";
    }
    if (previousFingerprints.value("rawBase", static_cast<std::uint64_t>(0)) != current.rawBase) {
        return "RENDER_RAW_BASE";
    }
    if (previousFingerprints.value("rawGlobal", static_cast<std::uint64_t>(0)) != current.rawGlobal) {
        return "SOLVE_GLOBAL";
    }
    if (previousFingerprints.value("scenePrep", static_cast<std::uint64_t>(0)) != current.scenePrep) {
        return "SOLVE_SCENE_PREP";
    }
    if (previousFingerprints.value("finishTone", static_cast<std::uint64_t>(0)) != current.finishTone) {
        return "SOLVE_FINISH_TONE";
    }
    if (previousFingerprints.value("finalValidation", static_cast<std::uint64_t>(0)) != current.finalValidation) {
        return "VALIDATE_FINAL";
    }
    return "CONVERGED";
}

std::string DevelopAutoStagePassKind(int pass) {
    if (pass <= 0) {
        return "metadataBootstrap";
    }
    if (pass == 1) {
        return "statsSolve";
    }
    if (pass == 2) {
        return "renderedFeedbackRefine";
    }
    return "emergencyStabilization";
}

std::string DevelopAutoStageStatusFor(
    const std::string& stageState,
    const std::string& earliestStage,
    bool finalConverged,
    bool awaitingRenderedMetrics) {
    if (finalConverged) {
        return stageState == "CONVERGED" ? "complete" : "validated";
    }
    if (stageState == "CONVERGED") {
        return awaitingRenderedMetrics ? "awaitingRenderedMetrics" : "notReached";
    }
    if (stageState == "VALIDATE_FINAL" && awaitingRenderedMetrics) {
        return "awaitingRenderedMetrics";
    }
    const int stageOrder = DevelopAutoStageOrder(stageState);
    const int earliestOrder = DevelopAutoStageOrder(earliestStage);
    return stageOrder < earliestOrder ? "reused" : "complete";
}

DevelopAutoStageFingerprints BuildDevelopAutoStageFingerprints(
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings,
    const Raw::RawDetailFusionSettings& prepSettings,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopAutoCandidateSolveResult& result,
    const DevelopToneAutoStats& stats) {
    DevelopAutoStageFingerprints fingerprints;

    DevelopAutoStageHashBuilder metadataHash;
    metadataHash.AddString(metadata.sourcePath);
    metadataHash.AddString(metadata.cameraMake);
    metadataHash.AddString(metadata.cameraModel);
    metadataHash.AddInt(metadata.rawWidth);
    metadataHash.AddInt(metadata.rawHeight);
    metadataHash.AddInt(metadata.visibleWidth);
    metadataHash.AddInt(metadata.visibleHeight);
    metadataHash.AddInt(metadata.orientation);
    metadataHash.AddInt(metadata.bitDepth);
    metadataHash.AddInt(static_cast<int>(metadata.cfaPattern));
    metadataHash.AddInt(static_cast<int>(metadata.pixelLayout));
    metadataHash.AddBool(metadata.mosaiced);
    metadataHash.AddBool(metadata.isDng);
    metadataHash.AddBool(metadata.hasDngBaselineExposure);
    metadataHash.AddFloat(metadata.dngBaselineExposure, 1000.0f);
    metadataHash.AddFloat(metadata.blackLevel, 1000.0f);
    metadataHash.AddFloat(metadata.whiteLevel, 1000.0f);
    for (float value : metadata.perChannelBlack) metadataHash.AddFloat(value, 1000.0f);
    for (float value : metadata.cameraWhiteBalance) metadataHash.AddFloat(value, 1000.0f);
    for (float value : metadata.daylightWhiteBalance) metadataHash.AddFloat(value, 1000.0f);
    fingerprints.metadata = metadataHash.hash;

    DevelopAutoStageHashBuilder rawBaseHash;
    rawBaseHash.AddValue(fingerprints.metadata);
    rawBaseHash.AddFloat(settings.exposureStops, 1000.0f);
    rawBaseHash.AddInt(static_cast<int>(settings.whiteBalanceMode));
    for (float value : settings.manualWhiteBalance) rawBaseHash.AddFloat(value, 1000.0f);
    rawBaseHash.AddBool(settings.overrideBlackLevel);
    rawBaseHash.AddFloat(settings.blackLevelOverride, 1000.0f);
    rawBaseHash.AddBool(settings.overrideWhiteLevel);
    rawBaseHash.AddFloat(settings.whiteLevelOverride, 1000.0f);
    rawBaseHash.AddInt(static_cast<int>(settings.highlightMode));
    rawBaseHash.AddFloat(settings.highlightStrength, 1000.0f);
    rawBaseHash.AddFloat(settings.highlightThreshold, 1000.0f);
    rawBaseHash.AddInt(static_cast<int>(settings.demosaicMethod));
    rawBaseHash.AddBool(settings.cameraTransformEnabled);
    rawBaseHash.AddInt(static_cast<int>(settings.cameraTransformSource));
    rawBaseHash.AddBool(settings.debugBypassCameraTransform);
    rawBaseHash.AddBool(settings.debugTransposeCameraMatrix);
    rawBaseHash.AddInt(static_cast<int>(settings.debugView));
    rawBaseHash.AddInt(settings.rotationDegrees);
    rawBaseHash.AddBool(settings.rotateToFitFrame);
    rawBaseHash.AddBool(settings.flipHorizontally);
    rawBaseHash.AddBool(settings.flipVertically);
    rawBaseHash.AddFloat(settings.falseColorSuppression, 1000.0f);
    rawBaseHash.AddFloat(settings.defringeStrength, 1000.0f);
    rawBaseHash.AddFloat(settings.highlightEdgeCleanup, 1000.0f);
    rawBaseHash.AddInt(settings.chromaRadius);
    rawBaseHash.AddFloat(settings.preserveRealColor, 1000.0f);
    rawBaseHash.AddFloat(settings.lateralRedCyan, 1000.0f);
    rawBaseHash.AddFloat(settings.lateralBlueYellow, 1000.0f);
    rawBaseHash.AddBool(settings.mosaicDenoise.enabled);
    rawBaseHash.AddBool(settings.mosaicDenoise.hotPixelSuppression);
    rawBaseHash.AddFloat(settings.mosaicDenoise.hotPixelThreshold, 1000.0f);
    rawBaseHash.AddFloat(settings.mosaicDenoise.lumaStrength, 1000.0f);
    rawBaseHash.AddFloat(settings.mosaicDenoise.chromaStrength, 1000.0f);
    rawBaseHash.AddInt(settings.mosaicDenoise.radius);
    rawBaseHash.AddFloat(settings.mosaicDenoise.edgeProtection, 1000.0f);
    rawBaseHash.AddInt(settings.mosaicDenoise.iterations);
    fingerprints.rawBase = rawBaseHash.hash;

    DevelopAutoStageHashBuilder rawGlobalHash;
    rawGlobalHash.AddValue(fingerprints.rawBase);
    rawGlobalHash.AddFloat(solveGuidance.exposureBias, 1000.0f);
    rawGlobalHash.AddFloat(solveGuidance.highlightGuard, 1000.0f);
    rawGlobalHash.AddFloat(solveGuidance.highlightCharacter, 1000.0f);
    rawGlobalHash.AddFloat(stats.highlightPressure, 1000.0f);
    rawGlobalHash.AddFloat(stats.clippingRatio, 10000.0f);
    fingerprints.rawGlobal = rawGlobalHash.hash;

    DevelopAutoStageHashBuilder scenePrepHash;
    scenePrepHash.AddValue(fingerprints.rawGlobal);
    scenePrepHash.AddBool(prepSettings.autoSafetyEnabled);
    scenePrepHash.AddFloat(prepSettings.minEvBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.maxEvBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.baseEvBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.noiseProtectionBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.highlightProtectionBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.shadowLiftLimitBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.wellExposedTargetBias, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.minEv, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.maxEv, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.baseEv, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.strength, 1000.0f);
    scenePrepHash.AddInt(prepSettings.sampleCount);
    scenePrepHash.AddFloat(prepSettings.baseRadiusPercent, 100000.0f);
    scenePrepHash.AddFloat(prepSettings.highlightProtection, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.shadowLiftLimit, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.noiseProtection, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.detailWeight, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.wellExposedTarget, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.smoothGradientProtection, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.textureSensitivity, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.skyBias, 1000.0f);
    scenePrepHash.AddInt(prepSettings.smoothnessRadius);
    scenePrepHash.AddInt(prepSettings.smoothAreaRadius);
    scenePrepHash.AddFloat(prepSettings.edgeAwareness, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.haloGuard, 1000.0f);
    scenePrepHash.AddFloat(prepSettings.maskDebandDither, 1000.0f);
    fingerprints.scenePrep = scenePrepHash.hash;

    DevelopAutoStageHashBuilder finishToneHash;
    finishToneHash.AddValue(fingerprints.scenePrep);
    finishToneHash.AddString(EditorNodeGraph::DevelopAutoIntentStableString(solveGuidance.intent));
    finishToneHash.AddFloat(solveGuidance.autoStrength, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.dynamicRange, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.shadowLift, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.highlightGuard, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.highlightCharacter, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.contrastBias, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.subjectSceneBias, 1000.0f);
    finishToneHash.AddFloat(solveGuidance.moodReadabilityBias, 1000.0f);
    finishToneHash.AddString(result.selectedId);
    finishToneHash.AddString(result.selectionSource);
    fingerprints.finishTone = finishToneHash.hash;

    DevelopAutoStageHashBuilder finalHash;
    finalHash.AddValue(fingerprints.finishTone);
    finalHash.AddValue(result.fingerprint);
    finalHash.AddInt(result.convergencePass);
    finalHash.AddBool(result.converged);
    finalHash.AddBool(result.renderedFeedbackApplied);
    finalHash.AddString(result.renderedFeedbackStopReason);
    finalHash.AddString(result.renderedFeedbackRevisionStage);
    fingerprints.finalValidation = finalHash.hash;
    return fingerprints;
}

std::string DevelopAutoCandidateStatusForDiagnostics(
    const DevelopAutoCandidateSolve& candidate,
    const DevelopAutoCandidateSolveResult& result) {
    if (candidate.id == result.selectedId) {
        return "selected";
    }
    if (!candidate.rejected) {
        return "survivor";
    }
    if (candidate.rememberedRejection) {
        return "rejectedMemory";
    }
    return candidate.duplicate ? "rejectedDuplicate" : "rejectedDamage";
}

nlohmann::json BuildDevelopAutoCandidateLearningRecord(
    const DevelopAutoCandidateSolveResult& result) {
    nlohmann::json events = nlohmann::json::array();
    int selectedCount = 0;
    int survivorCount = 0;
    int rejectedCount = 0;

    auto appendEvent = [&](nlohmann::json event) {
        constexpr std::size_t kMaxLearningEvents = 24;
        if (events.size() < kMaxLearningEvents) {
            events.push_back(std::move(event));
        }
    };

    for (const DevelopAutoCandidateSolve& candidate : result.candidates) {
        const std::string status = DevelopAutoCandidateStatusForDiagnostics(candidate, result);
        if (status == "selected") {
            ++selectedCount;
        } else if (status == "survivor") {
            ++survivorCount;
        } else {
            ++rejectedCount;
        }

        nlohmann::json event;
        event["type"] =
            status == "selected"
                ? "candidateSelected"
                : (status == "survivor" ? "candidateSurvived" : "candidateRejected");
        event["candidateId"] = candidate.id;
        event["label"] = candidate.label;
        event["status"] = status;
        event["score"] = candidate.score;
        event["reason"] = candidate.reason;
        event["selectionSource"] = result.selectionSource;
        event["guidanceFingerprint"] =
            candidate.guidanceFingerprint != 0
                ? candidate.guidanceFingerprint
                : BuildDevelopAutoCandidateGuidanceFingerprint(candidate.id, candidate.guidance);
        event["autoIntent"] = EditorNodeGraph::DevelopAutoIntentStableString(candidate.guidance.intent);
        event["guidanceVector"] = {
            { "brightnessIntent", candidate.guidance.exposureBias },
            { "dynamicRange", candidate.guidance.dynamicRange },
            { "shadowLift", candidate.guidance.shadowLift },
            { "highlightGuard", candidate.guidance.highlightGuard },
            { "highlightCharacter", candidate.guidance.highlightCharacter },
            { "contrastBias", candidate.guidance.contrastBias },
            { "subjectSceneBias", candidate.guidance.subjectSceneBias },
            { "moodReadabilityBias", candidate.guidance.moodReadabilityBias }
        };
        if (!candidate.rejectReason.empty()) {
            event["rejectReason"] = candidate.rejectReason;
        }
        if (candidate.renderedMemoryRejected) {
            event["renderedMemoryRejected"] = true;
        }
        appendEvent(std::move(event));
    }

    if (result.mergeApplied) {
        appendEvent({
            { "type", "candidateMerged" },
            { "candidateId", result.selectedId },
            { "label", result.selectedLabel },
            { "firstId", result.mergeFirstId },
            { "secondId", result.mergeSecondId },
            { "thirdId", result.mergeThirdId },
            { "firstWeight", result.mergeFirstWeight },
            { "secondWeight", result.mergeSecondWeight },
            { "thirdWeight", result.mergeThirdWeight },
            { "selectionSource", result.selectionSource }
        });
    }

    if (result.renderedFeedbackApplied) {
        appendEvent({
            { "type", "renderedFeedbackApplied" },
            { "action", result.renderedFeedbackAction.empty() ? "applied" : result.renderedFeedbackAction },
            { "candidateId", result.selectedId },
            { "previousSelectedId", result.renderedFeedbackPreviousSelectedId },
            { "bestId", result.renderedFeedbackBestId },
            { "bestScore", result.renderedFeedbackBestScore },
            { "improvement", result.renderedFeedbackImprovement },
            { "revisionStage", result.renderedFeedbackRevisionStage },
            { "revisionReason", result.renderedFeedbackRevisionReason },
            { "sourceFingerprint", result.renderedFeedbackSourceFingerprint },
            { "pass", result.renderedFeedbackPass }
        });
    } else if (!result.renderedFeedbackStopReason.empty()) {
        appendEvent({
            { "type", "renderedFeedbackStopped" },
            { "candidateId", result.selectedId },
            { "stopReason", result.renderedFeedbackStopReason },
            { "converged", result.renderedFeedbackStopIsConverged },
            { "stabilityDistance", result.renderedFeedbackStabilityDistance },
            { "stabilityScoreDelta", result.renderedFeedbackStabilityScoreDelta },
            { "trendHistoryCount", result.renderedFeedbackTrendHistoryCount },
            { "trendSameBestCount", result.renderedFeedbackTrendSameBestCount },
            { "trendScoreSpread", result.renderedFeedbackTrendScoreSpread },
            { "trendNearestDistance", result.renderedFeedbackTrendNearestDistance },
            { "monotonicMetric", result.renderedFeedbackMonotonicMetric },
            { "monotonicPreviousValue", result.renderedFeedbackMonotonicPreviousValue },
            { "monotonicCurrentValue", result.renderedFeedbackMonotonicCurrentValue }
        });
    }

    if (result.converged) {
        appendEvent({
            { "type", "candidateSolveConverged" },
            { "candidateId", result.selectedId },
            { "pass", result.convergencePass },
            { "solveFingerprint", result.fingerprint }
        });
    }

    return {
        { "version", "CandidateOutcomeLearningV1" },
        { "status", "recordedNotApplied" },
        { "recorded", true },
        { "applied", false },
        { "appliedToCurrentImage", false },
        { "appliedToFutureImages", false },
        { "applicationReason", "Outcome learning is recorded for diagnostics and future controls, but no preference learning is applied in this pass." },
        { "currentImageLearning", {
            { "recorded", true },
            { "applied", false },
            { "status", "recordedOnly" }
        } },
        { "futureImageLearning", {
            { "recorded", true },
            { "applied", false },
            { "status", "notApplied" }
        } },
        { "userChoiceLearning", {
            { "recorded", false },
            { "applied", false },
            { "status", "deferredUntilCandidateSelectionUi" }
        } },
        { "solveFingerprint", result.fingerprint },
        { "contextFingerprint", result.candidateContextFingerprint },
        { "selectedId", result.selectedId },
        { "selectedLabel", result.selectedLabel },
        { "selectionSource", result.selectionSource },
        { "selectedScore", result.selectedScore },
        { "selectedEventCount", selectedCount },
        { "survivorEventCount", survivorCount },
        { "rejectedEventCount", rejectedCount },
        { "rejectedMemorySuppressionCount", result.rejectedMemorySuppressionCount },
        { "renderedRejectedMemorySuppressionCount", result.renderedRejectedMemorySuppressionCount },
        { "eventCount", static_cast<int>(events.size()) },
        { "maxEventCount", 24 },
        { "events", std::move(events) }
    };
}

} // namespace

void WriteDevelopAutoStageSolveDiagnostics(
    nlohmann::json& toneJson,
    const Raw::RawMetadata& metadata,
    const Raw::RawDevelopSettings& settings,
    const Raw::RawDetailFusionSettings& prepSettings,
    const EditorNodeGraph::DevelopAutoGuidance& solveGuidance,
    const DevelopAutoCandidateSolveResult& result,
    const DevelopToneAutoStats& stats) {
    const nlohmann::json previousFingerprints =
        toneJson.value("autoStageFingerprints", nlohmann::json::object());
    const DevelopAutoStageFingerprints fingerprints =
        BuildDevelopAutoStageFingerprints(
            metadata,
            settings,
            prepSettings,
            solveGuidance,
            result,
            stats);
    const bool awaitingRenderedMetrics =
        toneJson.value("autoCandidateRenderMetricsStatus", std::string()) == "pending";
    const bool finalConverged =
        result.converged ||
        (toneJson.value("autoCandidateRenderedConverged", false) &&
         !result.renderedFeedbackApplied);
    std::string earliestStage =
        DevelopAutoStageForChangedFingerprint(previousFingerprints, fingerprints);
    const std::string revisionStage =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackRevisionStage
            : toneJson.value("autoCandidateRenderedRevisionStage", std::string());
    if (!revisionStage.empty() &&
        revisionStage != "none" &&
        revisionStage != "converged") {
        const std::string revisionState =
            DevelopAutoStageStateForRevisionStage(revisionStage);
        if (DevelopAutoStageOrder(revisionState) < DevelopAutoStageOrder(earliestStage) ||
            earliestStage == "CONVERGED") {
            earliestStage = revisionState;
        }
    } else if (finalConverged) {
        earliestStage = "CONVERGED";
    }

    const int stagePass = result.renderedFeedbackPass > 0
        ? result.renderedFeedbackPass
        : std::max(0, result.convergencePass - 1);
    const nlohmann::json fingerprintsJson = {
        { "metadata", fingerprints.metadata },
        { "rawBase", fingerprints.rawBase },
        { "rawGlobal", fingerprints.rawGlobal },
        { "scenePrep", fingerprints.scenePrep },
        { "finishTone", fingerprints.finishTone },
        { "finalValidation", fingerprints.finalValidation }
    };

    auto stageEntry = [&](std::string state,
                          std::string boundary,
                          std::uint64_t fingerprint,
                          std::string reason) {
        nlohmann::json entry;
        entry["state"] = state;
        entry["boundary"] = boundary;
        entry["fingerprint"] = fingerprint;
        entry["status"] = DevelopAutoStageStatusFor(state, earliestStage, finalConverged, awaitingRenderedMetrics);
        entry["reason"] = std::move(reason);
        return entry;
    };

    nlohmann::json stages = nlohmann::json::array({
        stageEntry("NEED_SOURCE", "source", fingerprints.metadata, "Confirm a RAW source and metadata are available."),
        stageEntry("METADATA_BOOTSTRAP", "metadata", fingerprints.metadata, "Use source metadata, black/white levels, and WB anchors as the first solve input."),
        stageEntry("RENDER_RAW_BASE", "rawBase", fingerprints.rawBase, "Render the current RAW base boundary; in this build RAW exposure still lives inside this cache boundary."),
        stageEntry("ANALYZE_RAW_BASE", "rawBase", fingerprints.rawBase, "Analyze RAW/base risk such as clipping, highlight pressure, and noise where current stats are available."),
        stageEntry("SOLVE_GLOBAL", "rawGlobal", fingerprints.rawGlobal, "Solve global RAW exposure, WB, highlight reconstruction, and RAW cleanup intent."),
        stageEntry("RENDER_GLOBAL_BASE", "rawGlobal", fingerprints.rawGlobal, "Validate the global solve before scene-prep decisions consume it."),
        stageEntry("SOLVE_SCENE_PREP", "scenePrep", fingerprints.scenePrep, "Solve local exposure/range, highlight/shadow guard, noise guard, and halo safety."),
        stageEntry("RENDER_PREFINISH", "scenePrep", fingerprints.scenePrep, "Render the hidden pre-finish boundary after RAW and scene prep."),
        stageEntry("ANALYZE_PREFINISH", "scenePrep", fingerprints.scenePrep, "Analyze the pre-finish state so finish tone does not hide upstream mistakes."),
        stageEntry("SOLVE_FINISH_TONE", "finishTone", fingerprints.finishTone, "Solve integrated finish tone and contrast intent from the current pre-finish state."),
        stageEntry("RENDER_FINAL", "finishTone", fingerprints.finishTone, "Render final Develop output before downstream View Transform."),
        stageEntry("VALIDATE_FINAL", "finalValidation", fingerprints.finalValidation, "Validate rendered candidates and decide whether another bounded pass is useful."),
        stageEntry("CONVERGED", "finalValidation", fingerprints.finalValidation, "Stop when the solve is stable or rendered feedback no longer improves the authored state.")
    });

    toneJson["autoStageSolveVersion"] = "StagedAutoSolveV1";
    toneJson["autoStageSolveDescription"] =
        "Logical staged Auto solve record for RAW/base, global, scene-prep, pre-finish, finish-tone, and final validation boundaries.";
    toneJson["autoStagePassBudget"] = {
        { "maxRenderedFeedbackPasses", 3 },
        { "bootstrapPass", 0 },
        { "statsSolvePass", 1 },
        { "refinePass", 2 },
        { "emergencyStabilizationPass", 3 }
    };
    toneJson["autoStageCurrentPass"] = stagePass;
    toneJson["autoStageCurrentPassKind"] = DevelopAutoStagePassKind(stagePass);
    toneJson["autoStageEarliestDirtyStage"] = earliestStage;
    toneJson["autoStageEarliestDirtyBoundary"] =
        earliestStage == "METADATA_BOOTSTRAP" ? "metadata" :
        (earliestStage == "RENDER_RAW_BASE" || earliestStage == "ANALYZE_RAW_BASE" ? "rawBase" :
        (earliestStage == "SOLVE_GLOBAL" || earliestStage == "RENDER_GLOBAL_BASE" ? "rawGlobal" :
        (earliestStage == "SOLVE_SCENE_PREP" || earliestStage == "RENDER_PREFINISH" || earliestStage == "ANALYZE_PREFINISH" ? "scenePrep" :
        (earliestStage == "SOLVE_FINISH_TONE" || earliestStage == "RENDER_FINAL" ? "finishTone" : "finalValidation"))));
    toneJson["autoStageRevisionStage"] = revisionStage.empty() ? "none" : revisionStage;
    toneJson["autoStageResponsibleRevisionState"] =
        DevelopAutoStageStateForRevisionStage(revisionStage.empty() ? "none" : revisionStage);
    toneJson["autoStageRevisionReason"] =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackRevisionReason
            : toneJson.value("autoCandidateRenderedRevisionReason", std::string());
    toneJson["autoStageCurrentRawExposureInsideRawBase"] = true;
    toneJson["autoStageCacheSplitStatus"] =
        "logicalOnlyCurrentRawGpuPipelineStillRendersRawExposureInsideRawBase";
    toneJson["autoStageRenderedMetricsRequired"] = awaitingRenderedMetrics;
    toneJson["autoStageValidationState"] =
        finalConverged ? "converged" : (awaitingRenderedMetrics ? "awaitingRenderedMetrics" : "validatedThisSolve");
    toneJson["autoStageFingerprints"] = fingerprintsJson;
    toneJson["autoStageSolveStages"] = std::move(stages);
}

void WriteDevelopAutoCandidateSolveDiagnostics(
    nlohmann::json& toneJson,
    const DevelopAutoCandidateSolveResult& result,
    const EditorNodeGraph::DevelopAutoGuidance& baseGuidance) {
    const std::uint64_t previousRenderedFingerprint =
        toneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0));
    const std::uint64_t previousSolveFingerprint =
        toneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
    const std::string previousRenderMetricsStatus =
        toneJson.value("autoCandidateRenderMetricsStatus", std::string());
    const std::uint64_t previousRenderedFeedbackAppliedFingerprint =
        toneJson.value("autoCandidateRenderedFeedbackAppliedFingerprint", static_cast<std::uint64_t>(0));
    const int previousRenderedFeedbackPass =
        toneJson.value("autoCandidateRenderedFeedbackPass", 0);
    const std::string previousRenderedRevisionStage =
        toneJson.value("autoCandidateRenderedRevisionStage", std::string());
    const std::string previousRenderedRevisionReason =
        toneJson.value("autoCandidateRenderedRevisionReason", std::string());
    nlohmann::json rejectedMemory =
        toneJson.value("autoCandidateRejectedMemory", nlohmann::json::array());
    if (!rejectedMemory.is_array()) {
        rejectedMemory = nlohmann::json::array();
    }
    nlohmann::json candidates = nlohmann::json::array();
    int rejectedCount = 0;
    int survivorCount = 0;
    for (std::size_t candidateIndex = 0; candidateIndex < result.candidates.size(); ++candidateIndex) {
        const DevelopAutoCandidateSolve& candidate = result.candidates[candidateIndex];
        if (candidate.rejected) {
            ++rejectedCount;
        } else {
            ++survivorCount;
        }

        nlohmann::json entry;
        const std::uint64_t guidanceFingerprint =
            candidate.guidanceFingerprint != 0
                ? candidate.guidanceFingerprint
                : BuildDevelopAutoCandidateGuidanceFingerprint(candidate.id, candidate.guidance);
        entry["id"] = candidate.id;
        entry["label"] = candidate.label;
        entry["reason"] = candidate.reason;
        entry["score"] = candidate.score;
        entry["guidanceFingerprint"] = guidanceFingerprint;
        entry["status"] = DevelopAutoCandidateStatusForDiagnostics(candidate, result);
        if (!candidate.rejectReason.empty()) {
            entry["rejectReason"] = candidate.rejectReason;
        }
        if (candidate.renderedMemoryRejected) {
            entry["renderedMemoryRejected"] = true;
        }
        entry["changes"] = {
            { "brightnessIntentDelta", candidate.guidance.exposureBias - baseGuidance.exposureBias },
            { "dynamicRangeDelta", candidate.guidance.dynamicRange - baseGuidance.dynamicRange },
            { "shadowLiftDelta", candidate.guidance.shadowLift - baseGuidance.shadowLift },
            { "highlightGuardDelta", candidate.guidance.highlightGuard - baseGuidance.highlightGuard },
            { "highlightCharacterDelta", candidate.guidance.highlightCharacter - baseGuidance.highlightCharacter },
            { "contrastBiasDelta", candidate.guidance.contrastBias - baseGuidance.contrastBias }
        };
        if (candidate.whiteBalanceProbe) {
            entry["rawOverrides"] = {
                { "whiteBalanceMode", Raw::WhiteBalanceModeName(candidate.whiteBalanceMode) },
                { "stage", "rawWhiteBalance" }
            };
            entry["changes"]["whiteBalanceMode"] =
                Raw::WhiteBalanceModeName(candidate.whiteBalanceMode);
        }
        if (candidate.continuationBiasActive) {
            entry["continuationBiasVersion"] = kDevelopContinuationCandidateBiasVersion;
            entry["continuationBiasBonus"] = candidate.continuationBiasBonus;
            entry["continuationBiasReason"] = candidate.continuationBiasReason;
            entry["continuationBiasStage"] = candidate.continuationBiasStage;
            entry["continuationBiasRefineIntent"] = candidate.continuationBiasRefineIntent;
        }
        if (candidate.continuationExpansionCandidate) {
            entry["continuationExpansionVersion"] =
                kDevelopContinuationCandidateExpansionVersion;
            entry["continuationExpansionReason"] =
                candidate.continuationExpansionReason;
            entry["continuationExpansionStage"] =
                candidate.continuationExpansionStage;
            entry["continuationExpansionRefineIntent"] =
                candidate.continuationExpansionRefineIntent;
        }
        entry["guidance"] = {
            { "autoStrength", candidate.guidance.autoStrength },
            { "brightnessIntent", candidate.guidance.exposureBias },
            { "dynamicRange", candidate.guidance.dynamicRange },
            { "shadowLift", candidate.guidance.shadowLift },
            { "highlightGuard", candidate.guidance.highlightGuard },
            { "highlightCharacter", candidate.guidance.highlightCharacter },
            { "contrastBias", candidate.guidance.contrastBias },
            { "subjectSceneBias", candidate.guidance.subjectSceneBias },
            { "moodReadabilityBias", candidate.guidance.moodReadabilityBias }
        };
        nlohmann::json scoreComponents =
            candidate.scoreComponents.is_object() && !candidate.scoreComponents.empty()
                ? candidate.scoreComponents
                : BuildFallbackDevelopAutoCandidateScoreComponents(candidate, baseGuidance);
        scoreComponents["finalScore"] = candidate.score;
        scoreComponents["status"] = entry["status"];
        const float nearestSurvivorDistance =
            DevelopAutoCandidateNearestSurvivorDistance(result, candidateIndex);
        nlohmann::json dimensions =
            scoreComponents.value("dimensions", nlohmann::json::object());
        dimensions["candidateUniqueness"] =
            SaturateFloat(nearestSurvivorDistance / 0.60f);
        dimensions["renderedContinuationFit"] =
            candidate.continuationBiasActive
                ? SaturateFloat(0.50f + candidate.continuationBiasBonus / 0.24f)
                : dimensions.value("renderedContinuationFit", 0.50f);
        dimensions["renderedContinuationCoverage"] =
            candidate.continuationExpansionCandidate
                ? 1.0f
                : dimensions.value("renderedContinuationCoverage", 0.50f);
        scoreComponents["dimensions"] = std::move(dimensions);
        scoreComponents["renderedContinuationBias"] = {
            { "version", kDevelopContinuationCandidateBiasVersion },
            { "active", candidate.continuationBiasActive },
            { "bonus", candidate.continuationBiasBonus },
            { "reason", candidate.continuationBiasReason },
            { "stageFocus", candidate.continuationBiasStage },
            { "refineIntent", candidate.continuationBiasRefineIntent }
        };
        scoreComponents["renderedContinuationExpansion"] = {
            { "version", kDevelopContinuationCandidateExpansionVersion },
            { "active", candidate.continuationExpansionCandidate },
            { "reason", candidate.continuationExpansionReason },
            { "stageFocus", candidate.continuationExpansionStage },
            { "refineIntent", candidate.continuationExpansionRefineIntent }
        };
        scoreComponents["nearestSurvivorDistance"] = nearestSurvivorDistance;
        entry["scoreComponents"] = std::move(scoreComponents);
        candidates.push_back(std::move(entry));

        if (candidate.rejected && !candidate.rememberedRejection && candidate.id != "base") {
            for (auto it = rejectedMemory.begin(); it != rejectedMemory.end();) {
                if (it->is_object() &&
                    it->value("contextFingerprint", static_cast<std::uint64_t>(0)) == result.candidateContextFingerprint &&
                    it->value("id", std::string()) == candidate.id) {
                    it = rejectedMemory.erase(it);
                } else {
                    ++it;
                }
            }

            nlohmann::json memoryEntry;
            memoryEntry["id"] = candidate.id;
            memoryEntry["label"] = candidate.label;
            memoryEntry["reason"] = candidate.rejectReason;
            memoryEntry["status"] = candidate.duplicate ? "rejectedDuplicate" : "rejectedDamage";
            memoryEntry["contextFingerprint"] = result.candidateContextFingerprint;
            memoryEntry["candidateScore"] = candidate.score;
            rejectedMemory.push_back(std::move(memoryEntry));
        }
    }

    constexpr std::size_t kMaxRejectedMemoryEntries = 16;
    while (rejectedMemory.size() > kMaxRejectedMemoryEntries) {
        rejectedMemory.erase(rejectedMemory.begin());
    }

    toneJson["autoCandidateSolveVersion"] = "ParameterCandidatesV1";
    toneJson["autoCandidateScoreVersion"] = "ParameterScoreComponentsV1";
    toneJson["autoCandidateContinuationBiasVersion"] =
        kDevelopContinuationCandidateBiasVersion;
    toneJson["autoCandidateContinuationBiasActive"] =
        result.continuationBiasActive;
    toneJson["autoCandidateContinuationBiasDecision"] =
        result.continuationBiasDecision;
    toneJson["autoCandidateContinuationBiasReason"] =
        result.continuationBiasReason;
    toneJson["autoCandidateContinuationBiasStage"] =
        result.continuationBiasStage;
    toneJson["autoCandidateContinuationBiasRefineIntent"] =
        result.continuationBiasRefineIntent;
    toneJson["autoCandidateContinuationBiasAppliedCount"] =
        result.continuationBiasAppliedCount;
    toneJson["autoCandidateContinuationExpansionVersion"] =
        kDevelopContinuationCandidateExpansionVersion;
    toneJson["autoCandidateContinuationExpansionEligible"] =
        result.continuationExpansionEligible;
    toneJson["autoCandidateContinuationExpansionActive"] =
        result.continuationExpansionAddedCount > 0;
    toneJson["autoCandidateContinuationExpansionReason"] =
        result.continuationExpansionReason;
    toneJson["autoCandidateContinuationExpansionStage"] =
        result.continuationExpansionStage;
    toneJson["autoCandidateContinuationExpansionRefineIntent"] =
        result.continuationExpansionRefineIntent;
    toneJson["autoCandidateContinuationExpansionAddedCount"] =
        result.continuationExpansionAddedCount;
    const nlohmann::json dynamicRangeStrategy =
        result.dynamicRangeStrategy.is_object()
            ? result.dynamicRangeStrategy
            : nlohmann::json::object();
    toneJson["autoDynamicRangeStrategyVersion"] = kDevelopDynamicRangeStrategyVersion;
    toneJson["autoDynamicRangeStrategy"] = dynamicRangeStrategy;
    toneJson["autoDynamicRangeStrategyId"] =
        dynamicRangeStrategy.value("id", std::string("balancedRange"));
    toneJson["autoDynamicRangeStrategyLabel"] =
        dynamicRangeStrategy.value("label", std::string("Balanced Range"));
    toneJson["autoDynamicRangeStrategyReason"] =
        dynamicRangeStrategy.value("reason", std::string());
    toneJson["autoDynamicRangeHighlightPolicy"] =
        dynamicRangeStrategy.value("highlightPolicy", std::string());
    toneJson["autoDynamicRangeShadowPolicy"] =
        dynamicRangeStrategy.value("shadowPolicy", std::string());
    toneJson["autoDynamicRangeHighlightImportance"] =
        dynamicRangeStrategy.value("highlightImportance", 0.0f);
    toneJson["autoDynamicRangeShadowReadability"] =
        dynamicRangeStrategy.value("shadowReadability", 0.0f);
    toneJson["autoDynamicRangeNoiseConstraint"] =
        dynamicRangeStrategy.value("noiseConstraint", 0.0f);
    toneJson["autoDynamicRangeCompression"] =
        dynamicRangeStrategy.value("rangeCompression", 0.0f);
    toneJson["autoDynamicRangeBrightnessHierarchyRisk"] =
        dynamicRangeStrategy.value("brightnessHierarchyRisk", 0.0f);
    toneJson["autoDynamicRangeMeaningfulHighlightPressure"] =
        dynamicRangeStrategy.value("meaningfulHighlightPressure", 0.0f);
    toneJson["autoDynamicRangeNaturalContrastGuardNeed"] =
        dynamicRangeStrategy.value("naturalContrastGuardNeed", 0.0f);
    toneJson["autoDynamicRangeBrightHighlightRolloffNeed"] =
        dynamicRangeStrategy.value("brightHighlightRolloffNeed", 0.0f);
    toneJson["autoDynamicRangeHighlightBrightnessAnchorNeed"] =
        dynamicRangeStrategy.value("highlightBrightnessAnchorNeed", 0.0f);
    toneJson["autoDynamicRangeBroadHighlightGuardNeed"] =
        dynamicRangeStrategy.value("broadHighlightGuardNeed", 0.0f);
    toneJson["autoDynamicRangeSpecularHighlightToleranceNeed"] =
        dynamicRangeStrategy.value("specularHighlightToleranceNeed", 0.0f);
    toneJson["autoDynamicRangeShadowReadabilityLiftNeed"] =
        dynamicRangeStrategy.value("shadowReadabilityLiftNeed", 0.0f);
    toneJson["autoDynamicRangeShadowNoiseFloorNeed"] =
        dynamicRangeStrategy.value("shadowNoiseFloorNeed", 0.0f);
    toneJson["autoDynamicRangeLocalHighlightHotspotRisk"] =
        dynamicRangeStrategy.value("localHighlightHotspotRisk", 0.0f);
    toneJson["autoDynamicRangeLocalShadowHotspotRisk"] =
        dynamicRangeStrategy.value("localShadowHotspotRisk", 0.0f);
    toneJson["autoDynamicRangeLocalRangeConflict"] =
        dynamicRangeStrategy.value("localRangeConflict", 0.0f);
    toneJson["autoDynamicRangeLocalEvConflict"] =
        dynamicRangeStrategy.value("localEvConflict", 0.0f);
    toneJson["autoDynamicRangeLocalHaloRisk"] =
        dynamicRangeStrategy.value("localHaloRisk", 0.0f);
    toneJson["autoDynamicRangeLocalHaloGuardNeed"] =
        dynamicRangeStrategy.value("localHaloGuardNeed", 0.0f);
    toneJson["autoDynamicRangeFlatGrayRisk"] =
        dynamicRangeStrategy.value("flatGrayRisk", 0.0f);
    toneJson["autoDynamicRangeHighlightGrayRisk"] =
        dynamicRangeStrategy.value("highlightGrayRisk", 0.0f);
    const nlohmann::json strategyMap =
        dynamicRangeStrategy.value("strategyMap", nlohmann::json::object());
    toneJson["autoDynamicRangeStrategyMapVersion"] =
        strategyMap.value("version", std::string(kDevelopDynamicRangeStrategyMapVersion));
    toneJson["autoDynamicRangeStrategyMap"] = strategyMap;
    toneJson["autoDynamicRangeStrategyMapHighlightShadowAxis"] =
        strategyMap.value(
            "highlightShadowAxis",
            dynamicRangeStrategy.value("strategyMapHighlightShadowAxis", 0.0f));
    toneJson["autoDynamicRangeStrategyMapContrastRangeAxis"] =
        strategyMap.value(
            "contrastRangeAxis",
            dynamicRangeStrategy.value("strategyMapContrastRangeAxis", 0.0f));
    toneJson["autoDynamicRangeStrategyMapHighlightPriority"] =
        strategyMap.value(
            "highlightPriority",
            dynamicRangeStrategy.value("strategyMapHighlightPriority", 0.5f));
    toneJson["autoDynamicRangeStrategyMapShadowVisibility"] =
        strategyMap.value(
            "shadowVisibility",
            dynamicRangeStrategy.value("strategyMapShadowVisibility", 0.5f));
    toneJson["autoDynamicRangeStrategyMapNaturalContrast"] =
        strategyMap.value(
            "naturalContrast",
            dynamicRangeStrategy.value("strategyMapNaturalContrast", 0.5f));
    toneJson["autoDynamicRangeStrategyMapVisibleRange"] =
        strategyMap.value(
            "visibleRange",
            dynamicRangeStrategy.value("strategyMapVisibleRange", 0.5f));
    toneJson["autoDynamicRangeStrategyMapReason"] =
        strategyMap.value(
            "reason",
            dynamicRangeStrategy.value("strategyMapReason", std::string()));
    const nlohmann::json localExposureStrategy =
        dynamicRangeStrategy.value("localExposureStrategy", nlohmann::json::object());
    toneJson["autoDynamicRangeLocalExposureStrategyVersion"] =
        localExposureStrategy.value(
            "version",
            dynamicRangeStrategy.value(
                "localExposureStrategyVersion",
                std::string(kDevelopLocalExposureStrategyVersion)));
    toneJson["autoDynamicRangeLocalExposureStrategy"] = localExposureStrategy;
    toneJson["autoDynamicRangeLocalExposureStrategyId"] =
        localExposureStrategy.value(
            "id",
            dynamicRangeStrategy.value("localExposureStrategyId", std::string("balancedLocalPrep")));
    toneJson["autoDynamicRangeLocalExposureStrategyLabel"] =
        localExposureStrategy.value(
            "label",
            dynamicRangeStrategy.value("localExposureStrategyLabel", std::string("Balanced Local Prep")));
    toneJson["autoDynamicRangeLocalExposureStrategyReason"] =
        localExposureStrategy.value(
            "reason",
            dynamicRangeStrategy.value("localExposureStrategyReason", std::string()));
    toneJson["autoDynamicRangeLocalExposureRangeRedistribution"] =
        localExposureStrategy.value(
            "rangeRedistribution",
            dynamicRangeStrategy.value("localExposureRangeRedistribution", 0.0f));
    toneJson["autoDynamicRangeLocalExposureHighlightCompression"] =
        localExposureStrategy.value(
            "highlightCompression",
            dynamicRangeStrategy.value("localExposureHighlightCompression", 0.0f));
    toneJson["autoDynamicRangeLocalExposureShadowOpening"] =
        localExposureStrategy.value(
            "shadowOpening",
            dynamicRangeStrategy.value("localExposureShadowOpening", 0.0f));
    toneJson["autoDynamicRangeLocalExposureNoiseGuard"] =
        localExposureStrategy.value(
            "noiseGuard",
            dynamicRangeStrategy.value("localExposureNoiseGuard", 0.0f));
    toneJson["autoDynamicRangeLocalExposureHaloGuard"] =
        localExposureStrategy.value(
            "haloGuard",
            dynamicRangeStrategy.value("localExposureHaloGuard", 0.0f));
    toneJson["autoDynamicRangeLocalExposureTextureGuard"] =
        localExposureStrategy.value(
            "textureGuard",
            dynamicRangeStrategy.value("localExposureTextureGuard", 0.0f));
    toneJson["autoDynamicRangeLocalExposureShadowEvBudget"] =
        localExposureStrategy.value(
            "shadowEvBudget",
            dynamicRangeStrategy.value("localExposureShadowEvBudget", 0.0f));
    toneJson["autoDynamicRangeLocalExposureHighlightEvBudget"] =
        localExposureStrategy.value(
            "highlightEvBudget",
            dynamicRangeStrategy.value("localExposureHighlightEvBudget", 0.0f));
    toneJson["autoDynamicRangeLocalExposureStrengthTarget"] =
        localExposureStrategy.value(
            "strengthTarget",
            dynamicRangeStrategy.value("localExposureStrengthTarget", 0.5f));
    toneJson["autoDynamicRangeLocalExposureHighlightCrowding"] =
        dynamicRangeStrategy.value("localExposureHighlightCrowding", 0.0f);
    toneJson["autoDynamicRangeLocalExposureShadowCrowding"] =
        dynamicRangeStrategy.value("localExposureShadowCrowding", 0.0f);
    toneJson["autoDynamicRangeLocalExposureHaloStress"] =
        dynamicRangeStrategy.value("localExposureHaloStress", 0.0f);
    toneJson["autoDynamicRangeLocalExposureFlatnessRisk"] =
        dynamicRangeStrategy.value("localExposureFlatnessRisk", 0.0f);
    toneJson["autoDynamicRangeLocalExposureDamageRisk"] =
        dynamicRangeStrategy.value("localExposureDamageRisk", 0.0f);
    toneJson["autoDynamicRangeSmallSpecularClippingAllowed"] =
        dynamicRangeStrategy.value("smallSpecularClippingAllowed", false);
    const nlohmann::json regionEvidence =
        dynamicRangeStrategy.value("regionEvidence", nlohmann::json::object());
    toneJson["autoDynamicRangeRegionEvidenceVersion"] =
        kDevelopDynamicRangeRegionEvidenceVersion;
    toneJson["autoDynamicRangeRegionEvidence"] = regionEvidence;
    toneJson["autoDynamicRangeRegionEvidenceValid"] =
        regionEvidence.value("valid", false);
    toneJson["autoDynamicRangeRegionEvidenceSource"] =
        regionEvidence.value("source", std::string());
    toneJson["autoDynamicRangeRegionEvidenceCandidateId"] =
        regionEvidence.value("candidateId", std::string());
    toneJson["autoDynamicRangeLocalEvSpreadStops"] =
        regionEvidence.value("localEvSpreadStops", 0.0f);
    toneJson["autoDynamicRangeSmallSpecularLikely"] =
        regionEvidence.value("smallSpecularLikely", false);
    toneJson["autoDynamicRangeHighlightBandFraction"] =
        regionEvidence.value("highlightBandFraction", 0.0f);
    toneJson["autoDynamicRangeHighlightMeanLuma"] =
        regionEvidence.value("highlightMeanLuma", 0.0f);
    toneJson["autoDynamicRangeHighlightLowSaturationFraction"] =
        regionEvidence.value("highlightLowSaturationFraction", 0.0f);
    toneJson["autoDynamicRangeHighlightTileCoverage"] =
        regionEvidence.value("highlightTileCoverage", 0.0f);
    toneJson["autoDynamicRangeHighlightStructureScore"] =
        regionEvidence.value("highlightStructureScore", 0.0f);
    const nlohmann::json subjectSceneIntent =
        result.subjectSceneIntent.is_object()
            ? result.subjectSceneIntent
            : nlohmann::json::object();
    toneJson["autoSubjectSceneIntentVersion"] =
        subjectSceneIntent.value("version", std::string(kDevelopSubjectSceneIntentVersion));
    toneJson["autoSubjectSceneIntent"] = subjectSceneIntent;
    toneJson["autoSubjectSceneIntentId"] =
        subjectSceneIntent.value("id", std::string("automaticSceneBalance"));
    toneJson["autoSubjectSceneIntentLabel"] =
        subjectSceneIntent.value("label", std::string("Automatic Scene Balance"));
    toneJson["autoSubjectSceneIntentReason"] =
        subjectSceneIntent.value("reason", std::string());
    const nlohmann::json subjectSolveNotes =
        subjectSceneIntent.value("solveNotes", nlohmann::json::array());
    const bool subjectSolveNotesValid = subjectSolveNotes.is_array();
    std::string primarySubjectSolveNote;
    if (subjectSolveNotesValid && !subjectSolveNotes.empty() &&
        subjectSolveNotes.front().is_object()) {
        primarySubjectSolveNote =
            subjectSolveNotes.front().value("text", std::string());
    }
    toneJson["autoSubjectSceneSolveNotesVersion"] =
        subjectSceneIntent.value(
            "solveNotesVersion",
            std::string(kDevelopSubjectImportanceSolveNotesVersion));
    toneJson["autoSubjectSceneSolveNotes"] =
        subjectSolveNotesValid ? subjectSolveNotes : nlohmann::json::array();
    toneJson["autoSubjectSceneSolveNoteCount"] =
        subjectSolveNotesValid ? static_cast<int>(subjectSolveNotes.size()) : 0;
    toneJson["autoSubjectScenePrimarySolveNote"] = primarySubjectSolveNote;
    toneJson["autoSubjectSceneUserGuidanceStatus"] =
        subjectSceneIntent.value("userGuidanceStatus", std::string("notAvailable"));
    toneJson["autoSubjectSceneUserGuidanceActive"] =
        subjectSceneIntent.value("userGuidanceActive", false);
    toneJson["autoSubjectSceneAutomaticOnly"] =
        subjectSceneIntent.value("automaticOnly", true);
    toneJson["autoSubjectSceneUserSubjectSceneBias"] =
        subjectSceneIntent.value("userSubjectSceneBias", 0.0f);
    toneJson["autoSubjectSceneUserMoodReadabilityBias"] =
        subjectSceneIntent.value("userMoodReadabilityBias", 0.0f);
    toneJson["autoSubjectSceneUserGuidanceStrength"] =
        subjectSceneIntent.value("userGuidanceStrength", 0.0f);
    toneJson["autoSubjectSceneAutomaticConfidence"] =
        subjectSceneIntent.value("automaticConfidence", 0.0f);
    toneJson["autoSubjectSceneCenterPrior"] =
        subjectSceneIntent.value("centerPrior", 0.0f);
    toneJson["autoSubjectSceneReadabilityPressure"] =
        subjectSceneIntent.value("readabilityPressure", 0.0f);
    toneJson["autoSubjectSceneProtectionPressure"] =
        subjectSceneIntent.value("protectionPressure", 0.0f);
    toneJson["autoSubjectSceneMoodPreservationPressure"] =
        subjectSceneIntent.value("moodPreservationPressure", 0.0f);
    toneJson["autoSubjectSceneSubjectPriority"] =
        subjectSceneIntent.value("subjectPriority", 0.5f);
    toneJson["autoSubjectSceneSceneIntegrity"] =
        subjectSceneIntent.value("sceneIntegrity", 0.5f);
    toneJson["autoSubjectSceneImproveReadability"] =
        subjectSceneIntent.value("improveReadability", 0.5f);
    toneJson["autoSubjectScenePreserveMood"] =
        subjectSceneIntent.value("preserveMood", 0.5f);
    toneJson["autoSubjectSceneSubjectSceneAxis"] =
        subjectSceneIntent.value("subjectSceneAxis", 0.0f);
    toneJson["autoSubjectSceneMoodReadabilityAxis"] =
        subjectSceneIntent.value("moodReadabilityAxis", 0.0f);
    toneJson["autoSubjectSceneImportanceRegionCount"] =
        subjectSceneIntent.value("importanceRegionCount", 0);
    toneJson["autoSubjectSceneImportanceStrokeCount"] =
        subjectSceneIntent.value("importanceStrokeCount", 0);
    toneJson["autoSubjectSceneImportanceStrength"] =
        subjectSceneIntent.value("importanceStrength", 0.0f);
    toneJson["autoSubjectSceneImportanceImportant"] =
        subjectSceneIntent.value("importanceImportant", 0.0f);
    toneJson["autoSubjectSceneImportanceReveal"] =
        subjectSceneIntent.value("importanceReveal", 0.0f);
    toneJson["autoSubjectSceneImportanceProtect"] =
        subjectSceneIntent.value("importanceProtect", 0.0f);
    toneJson["autoSubjectSceneImportancePreserveMood"] =
        subjectSceneIntent.value("importancePreserveMood", 0.0f);
    toneJson["autoSubjectSceneImportanceIgnore"] =
        subjectSceneIntent.value("importanceIgnore", 0.0f);
    const nlohmann::json subjectImportanceMap =
        subjectSceneIntent.value("importanceMap", nlohmann::json::object());
    toneJson["autoSubjectSceneImportanceMapVersion"] =
        subjectImportanceMap.value("version", std::string(kDevelopSubjectImportanceMapVersion));
    toneJson["autoSubjectSceneImportanceMap"] = subjectImportanceMap;
    toneJson["autoSubjectSceneImportanceMapStatus"] =
        subjectImportanceMap.value("status", std::string("disabled"));
    toneJson["autoSubjectSceneImportanceMapActive"] =
        subjectImportanceMap.value("active", false);
    toneJson["autoSubjectSceneImportanceMapCoverage"] =
        subjectSceneIntent.value("importanceMapCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapPositiveCoverage"] =
        subjectSceneIntent.value("importanceMapPositiveCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapLowPriorityCoverage"] =
        subjectSceneIntent.value("importanceMapLowPriorityCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapRevealCoverage"] =
        subjectSceneIntent.value("importanceMapRevealCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapProtectCoverage"] =
        subjectSceneIntent.value("importanceMapProtectCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapMoodCoverage"] =
        subjectSceneIntent.value("importanceMapMoodCoverage", 0.0f);
    toneJson["autoSubjectSceneImportanceMapPeak"] =
        subjectSceneIntent.value("importanceMapPeak", 0.0f);
    toneJson["autoSubjectSceneImportanceMapConfidence"] =
        subjectSceneIntent.value("importanceMapConfidence", 0.0f);
    toneJson["autoSubjectSceneImportanceMapCenterBias"] =
        subjectSceneIntent.value("importanceMapCenterBias", 0.0f);
    toneJson["autoSubjectSceneImportanceMapEdgeBias"] =
        subjectSceneIntent.value("importanceMapEdgeBias", 0.0f);
    const nlohmann::json subjectRefinedMap =
        subjectSceneIntent.value("refinedImportanceMap", nlohmann::json::object());
    toneJson["autoSubjectSceneRefinedMapVersion"] =
        subjectRefinedMap.value("version", std::string(kDevelopSubjectRefinedMapVersion));
    toneJson["autoSubjectSceneRefinedMap"] = subjectRefinedMap;
    toneJson["autoSubjectSceneRefinedMapStatus"] =
        subjectRefinedMap.value("status", std::string("disabled"));
    toneJson["autoSubjectSceneRefinedMapActive"] =
        subjectRefinedMap.value("active", false);
    toneJson["autoSubjectSceneRefinedMapCoverage"] =
        subjectSceneIntent.value("refinedMapCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapLowPriorityCoverage"] =
        subjectSceneIntent.value("refinedMapLowPriorityCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapReadabilityCoverage"] =
        subjectSceneIntent.value("refinedMapReadabilityCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapProtectionCoverage"] =
        subjectSceneIntent.value("refinedMapProtectionCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapMoodCoverage"] =
        subjectSceneIntent.value("refinedMapMoodCoverage", 0.0f);
    toneJson["autoSubjectSceneRefinedMapPeak"] =
        subjectSceneIntent.value("refinedMapPeak", 0.0f);
    toneJson["autoSubjectSceneRefinedMapConfidence"] =
        subjectSceneIntent.value("refinedMapConfidence", 0.0f);
    toneJson["autoSubjectSceneRefinedMapBoundaryHint"] =
        subjectSceneIntent.value("refinedMapBoundaryHint", 0.0f);
    toneJson["autoSubjectSceneBrushStatus"] =
        subjectSceneIntent.value("importanceStrokeCount", 0) > 0
            ? "brushStrokesActiveEdgeRefineDeferred"
            : (subjectSceneIntent.value("importanceRegionCount", 0) > 0
                ? "regionGuidanceActive"
                : "deferred");
    toneJson["autoCandidateSolves"] = std::move(candidates);
    toneJson["autoCandidateSurvivorCount"] = survivorCount;
    toneJson["autoCandidateRejectedCount"] = rejectedCount;
    toneJson["autoCandidateContextFingerprint"] = result.candidateContextFingerprint;
    toneJson["autoCandidateRejectedMemory"] = std::move(rejectedMemory);
    toneJson["autoCandidateRejectedMemoryMaxEntries"] = static_cast<int>(kMaxRejectedMemoryEntries);
    toneJson["autoCandidateRejectedMemorySuppressionCount"] =
        result.rejectedMemorySuppressionCount;
    toneJson["autoCandidateRenderedRejectedMemorySuppressionCount"] =
        result.renderedRejectedMemorySuppressionCount;
    toneJson["autoCandidateSelectedId"] = result.selectedId;
    toneJson["autoCandidateSelectedLabel"] = result.selectedLabel;
    toneJson["autoCandidateSelectedScore"] = result.selectedScore;
    toneJson["autoCandidateSelectionSource"] = result.selectionSource;
    toneJson["autoCandidateSelectedWhiteBalanceProbe"] = result.authoredWhiteBalanceProbe;
    toneJson["autoCandidateSelectedWhiteBalanceMode"] =
        result.authoredWhiteBalanceProbe
            ? Raw::WhiteBalanceModeName(result.authoredWhiteBalanceMode)
            : std::string();
    toneJson["autoCandidateMergeApplied"] = result.mergeApplied;
    toneJson["autoCandidateMergeFirstId"] = result.mergeFirstId;
    toneJson["autoCandidateMergeSecondId"] = result.mergeSecondId;
    toneJson["autoCandidateMergeThirdId"] = result.mergeThirdId;
    toneJson["autoCandidateMergeFirstWeight"] = result.mergeFirstWeight;
    toneJson["autoCandidateMergeSecondWeight"] = result.mergeSecondWeight;
    toneJson["autoCandidateMergeThirdWeight"] = result.mergeThirdWeight;
    toneJson["autoCandidateConverged"] = result.converged;
    toneJson["autoCandidateConvergencePass"] = result.convergencePass;
    toneJson["autoCandidateMaxPasses"] = 6;
    toneJson["autoCandidateSolveFingerprint"] = result.fingerprint;
    toneJson["autoCandidateSelectionIsAuthoredState"] = true;
    toneJson["autoCandidateGalleryStatus"] = "deferred";
    const bool renderedMetricsMatchCurrentSolve =
        previousRenderedFingerprint == result.fingerprint &&
        !previousRenderMetricsStatus.empty();
    const bool renderedMetricsStoppedFromPreviousSolve =
        previousRenderedFingerprint != 0 &&
        previousRenderedFingerprint == previousSolveFingerprint &&
        !previousRenderMetricsStatus.empty() &&
        !result.renderedFeedbackApplied &&
        !result.renderedFeedbackStopReason.empty();
    const bool renderedMetricsReadyForCurrentSolve =
        (previousRenderedFingerprint == result.fingerprint || renderedMetricsStoppedFromPreviousSolve) &&
        (previousRenderMetricsStatus == "ready" || previousRenderMetricsStatus == "partial");
    toneJson["autoCandidateRenderMetricsStatus"] =
        (renderedMetricsMatchCurrentSolve || renderedMetricsStoppedFromPreviousSolve)
            ? previousRenderMetricsStatus
            : "pending";
    if (!renderedMetricsMatchCurrentSolve && !renderedMetricsStoppedFromPreviousSolve) {
        toneJson["autoCandidateRenderedConverged"] = false;
        toneJson["autoCandidateRenderedConvergenceStatus"] = "pending";
        toneJson["autoCandidateRenderedStopReason"] = "awaitingRenderedMetrics";
    }
    toneJson["autoCandidateRenderedFeedbackImprovement"] =
        result.renderedFeedbackImprovement;
    toneJson["autoCandidateRenderedStabilityDistance"] =
        result.renderedFeedbackStabilityDistance;
    toneJson["autoCandidateRenderedStabilityScoreDelta"] =
        result.renderedFeedbackStabilityScoreDelta;
    toneJson["autoCandidateRenderedStabilityReferenceId"] =
        result.renderedFeedbackStabilityReferenceId;
    toneJson["autoCandidateRenderedStabilityStatus"] =
        result.renderedFeedbackStabilityDistance >= 0.0f
            ? (result.renderedFeedbackStopReason == "renderedMetricsStable" ? "stable" : "measured")
            : "unavailable";
    toneJson["autoCandidateRenderedTrendHistoryCount"] =
        result.renderedFeedbackTrendHistoryCount;
    toneJson["autoCandidateRenderedTrendSameBestCount"] =
        result.renderedFeedbackTrendSameBestCount;
    toneJson["autoCandidateRenderedTrendScoreSpread"] =
        result.renderedFeedbackTrendScoreSpread;
    toneJson["autoCandidateRenderedTrendNearestDistance"] =
        result.renderedFeedbackTrendNearestDistance;
    toneJson["autoCandidateRenderedTrendReferenceId"] =
        result.renderedFeedbackTrendReferenceId;
    toneJson["autoCandidateRenderedTrendStatus"] =
        result.renderedFeedbackTrendHistoryCount > 0
            ? (result.renderedFeedbackStopReason == "renderedFeedbackNoImprovementTrend" ||
               result.renderedFeedbackStopReason == "renderedRefineNoImprovementTrend" ||
               result.renderedFeedbackStopReason == "renderedFeedbackStableTrend"
                   ? "stalled"
                   : "measured")
            : "unavailable";
    toneJson["autoCandidateRenderedMonotonicGuardStatus"] =
        !result.renderedFeedbackMonotonicMetric.empty()
            ? (result.renderedFeedbackStopReason.rfind("renderedRefineMonotonic", 0) == 0
                ? "stopped"
                : "measured")
            : "unavailable";
    toneJson["autoCandidateRenderedMonotonicMetric"] =
        result.renderedFeedbackMonotonicMetric;
    toneJson["autoCandidateRenderedMonotonicPreviousValue"] =
        result.renderedFeedbackMonotonicPreviousValue;
    toneJson["autoCandidateRenderedMonotonicCurrentValue"] =
        result.renderedFeedbackMonotonicCurrentValue;
    toneJson["autoCandidateRenderedMonotonicReferenceId"] =
        result.renderedFeedbackMonotonicReferenceId;
    toneJson["autoCandidateRenderedCarriedForwardCount"] =
        result.renderedFeedbackCarriedForwardCount;
    toneJson["autoCandidateRenderedFeedbackApplied"] = result.renderedFeedbackApplied;
    toneJson["autoCandidateRenderedFeedbackAppliedFingerprint"] =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackSourceFingerprint
            : previousRenderedFeedbackAppliedFingerprint;
    toneJson["autoCandidateRenderedFeedbackPass"] =
        result.renderedFeedbackApplied
            ? result.renderedFeedbackPass
            : previousRenderedFeedbackPass;
    if (result.renderedFeedbackApplied) {
        toneJson["autoCandidateRenderedFeedbackAction"] =
            result.renderedFeedbackAction.empty() ? "applied" : result.renderedFeedbackAction;
        toneJson["autoCandidateRenderedFeedbackStopReason"] = std::string();
        toneJson["autoCandidateRenderedConvergenceStatus"] =
            result.renderedFeedbackAction.empty() ? "feedbackApplied" : result.renderedFeedbackAction;
        toneJson["autoCandidateRenderedConverged"] = false;
        toneJson["autoCandidateRenderedFeedbackMerged"] =
            result.renderedFeedbackAction == "merged";
        toneJson["autoCandidateRenderedFeedbackPreviousSelectedId"] =
            result.renderedFeedbackPreviousSelectedId;
        toneJson["autoCandidateRenderedFeedbackPreviousSelectedScore"] =
            result.renderedFeedbackPreviousSelectedScore;
        toneJson["autoCandidateRenderedFeedbackBestId"] = result.renderedFeedbackBestId;
        toneJson["autoCandidateRenderedFeedbackBestScore"] = result.renderedFeedbackBestScore;
        if (!result.renderedFeedbackRefineIntent.empty()) {
            toneJson["autoCandidateRenderedFeedbackRefineIntent"] =
                result.renderedFeedbackRefineIntent;
            toneJson["autoCandidateRenderedFeedbackRefineReason"] =
                result.renderedFeedbackRefineReason;
        }
        toneJson["autoCandidateRenderedRevisionStage"] =
            result.renderedFeedbackRevisionStage.empty()
                ? "multiStage"
                : result.renderedFeedbackRevisionStage;
        toneJson["autoCandidateRenderedRevisionReason"] =
            result.renderedFeedbackRevisionReason;
    } else if (renderedMetricsReadyForCurrentSolve && !result.renderedFeedbackStopReason.empty()) {
        toneJson["autoCandidateRenderedFeedbackAction"] = "stopped";
        toneJson["autoCandidateRenderedFeedbackStopReason"] =
            result.renderedFeedbackStopReason;
        toneJson["autoCandidateRenderedConvergenceStatus"] = "stopped";
        toneJson["autoCandidateRenderedStopReason"] =
            result.renderedFeedbackStopReason;
        toneJson["autoCandidateRenderedConverged"] =
            result.renderedFeedbackStopIsConverged;
        toneJson["autoCandidateRenderedRevisionStage"] =
            result.renderedFeedbackStopIsConverged ? "converged" : "none";
        toneJson["autoCandidateRenderedRevisionReason"] =
            result.renderedFeedbackStopReason;
    } else {
        toneJson["autoCandidateRenderedRevisionStage"] =
            previousRenderedRevisionStage;
        toneJson["autoCandidateRenderedRevisionReason"] =
            previousRenderedRevisionReason;
    }

    const nlohmann::json renderedHistory =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    const int renderedHistoryCount =
        renderedHistory.is_array() ? static_cast<int>(renderedHistory.size()) : 0;
    const std::string currentRenderMetricsStatus =
        toneJson.value("autoCandidateRenderMetricsStatus", std::string());
    std::string loopState = "awaitingRenderedMetrics";
    std::string loopAction = "pendingRender";
    std::string loopStopReason =
        toneJson.value("autoCandidateRenderedStopReason", std::string("awaitingRenderedMetrics"));
    std::string loopNextStep = "renderCandidates";
    int loopPass = previousRenderedFeedbackPass;
    int loopNextPass = previousRenderedFeedbackPass;
    bool loopRequiresRenderedMetrics = true;
    bool loopRequiresAutoSolve = false;
    if (result.renderedFeedbackApplied) {
        loopState = "active";
        loopAction = result.renderedFeedbackAction.empty()
            ? std::string("applied")
            : result.renderedFeedbackAction;
        loopStopReason.clear();
        loopNextStep = "renderUpdatedSolve";
        loopPass = result.renderedFeedbackPass;
        loopNextPass = result.renderedFeedbackPass;
        loopRequiresRenderedMetrics = true;
    } else if (renderedMetricsReadyForCurrentSolve && !result.renderedFeedbackStopReason.empty()) {
        loopState = result.renderedFeedbackStopIsConverged ? "converged" : "stopped";
        loopAction = "stopped";
        loopStopReason = result.renderedFeedbackStopReason;
        loopNextStep = "none";
        loopPass = previousRenderedFeedbackPass;
        loopNextPass = previousRenderedFeedbackPass;
        loopRequiresRenderedMetrics = false;
    } else if (!renderedMetricsMatchCurrentSolve) {
        loopState = "awaitingRenderedMetrics";
        loopAction = "pendingRender";
        loopStopReason = "awaitingRenderedMetrics";
        loopNextStep = "renderCandidates";
    } else {
        loopState = "measured";
        loopAction = toneJson.value("autoCandidateRenderedFeedbackAction", std::string());
        loopStopReason = toneJson.value("autoCandidateRenderedStopReason", std::string());
        loopNextStep = "none";
        loopRequiresRenderedMetrics = false;
    }

    const std::string continuationDecision =
        result.renderedFeedbackApplied
            ? std::string("continue")
            : (loopState == "awaitingRenderedMetrics"
                ? std::string("waitForRenderedMetrics")
                : std::string("stop"));
    const std::string continuationReason =
        result.renderedFeedbackApplied
            ? (result.renderedFeedbackAction.empty()
                ? std::string("renderedFeedbackApplied")
                : result.renderedFeedbackAction)
            : loopStopReason;
    const nlohmann::json continuationPolicy =
        BuildDevelopRenderedContinuationPolicyRecord(
            continuationDecision,
            continuationReason,
            loopNextStep,
            loopRequiresAutoSolve,
            loopRequiresRenderedMetrics,
            loopPass,
            loopNextPass,
            toneJson.value("autoCandidateRenderedRevisionStage", std::string()),
            toneJson.value("autoCandidateRenderedRevisionReason", std::string()),
            result.renderedFeedbackImprovement,
            toneJson.value("autoCandidateRenderedStabilityStatus", std::string()),
            toneJson.value("autoCandidateRenderedTrendStatus", std::string()),
            toneJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()));
    toneJson["autoCandidateRenderedContinuationVersion"] =
        kDevelopRenderedContinuationVersion;
    toneJson["autoCandidateRenderedContinuationPolicy"] = continuationPolicy;

    const nlohmann::json convergenceEvidence =
        BuildDevelopAutoConvergenceEvidenceRecord(
            result,
            renderedMetricsMatchCurrentSolve,
            renderedMetricsReadyForCurrentSolve,
            currentRenderMetricsStatus,
            loopState,
            loopAction,
            loopStopReason,
            loopNextStep,
            loopRequiresAutoSolve,
            loopRequiresRenderedMetrics,
            loopPass,
            loopNextPass,
            renderedHistoryCount,
            continuationPolicy,
            toneJson);
    toneJson["autoCandidateConvergenceEvidenceVersion"] =
        kDevelopConvergenceEvidenceVersion;
    toneJson["autoCandidateConvergenceState"] =
        convergenceEvidence.value("state", std::string());
    toneJson["autoCandidateConvergenceDecision"] =
        convergenceEvidence.value("decision", std::string());
    toneJson["autoCandidateConvergenceReason"] =
        convergenceEvidence.value("reason", std::string());
    toneJson["autoCandidateConvergenceShouldContinue"] =
        convergenceEvidence.value("shouldContinue", false);
    toneJson["autoCandidateConvergenceAdmissionVersion"] =
        kDevelopConvergenceAdmissionVersion;
    toneJson["autoCandidateConvergenceAdmissionMinimumImprovement"] =
        result.renderedFeedbackAdmissionMinimumImprovement;
    toneJson["autoCandidateConvergenceAdmissionBaseMinimumImprovement"] =
        result.renderedFeedbackAdmissionBaseMinimumImprovement;
    toneJson["autoCandidateConvergenceAdmissionTightened"] =
        result.renderedFeedbackAdmissionTightened;
    toneJson["autoCandidateConvergenceAdmissionReason"] =
        result.renderedFeedbackAdmissionReason;
    toneJson["autoCandidateConvergenceAdmissionEvidenceState"] =
        result.renderedFeedbackAdmissionEvidenceState;
    toneJson["autoCandidateConvergenceAdmissionEvidenceDecision"] =
        result.renderedFeedbackAdmissionEvidenceDecision;
    toneJson["autoCandidateConvergenceAdmissionEvidencePass"] =
        result.renderedFeedbackAdmissionEvidencePass;
    toneJson["autoCandidateConvergenceEvidence"] = convergenceEvidence;

    toneJson["autoCandidateRenderedFeedbackLoopVersion"] =
        kDevelopRenderedFeedbackLoopVersion;
    toneJson["autoCandidateRenderedFeedbackLoop"] = {
        { "version", kDevelopRenderedFeedbackLoopVersion },
        { "state", loopState },
        { "action", loopAction },
        { "stopReason", loopStopReason },
        { "nextStep", loopNextStep },
        { "requiresRenderedMetrics", loopRequiresRenderedMetrics },
        { "requiresAutoSolve", loopRequiresAutoSolve },
        { "pass", loopPass },
        { "nextPass", loopNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "solveFingerprint", result.fingerprint },
        { "renderedFingerprint", previousRenderedFingerprint },
        { "appliedRenderedFingerprint", result.renderedFeedbackApplied
            ? result.renderedFeedbackSourceFingerprint
            : previousRenderedFeedbackAppliedFingerprint },
        { "renderMetricsStatus", currentRenderMetricsStatus },
        { "selectedId", result.selectedId },
        { "selectedScore", result.selectedScore },
        { "previousSelectedId", result.renderedFeedbackPreviousSelectedId },
        { "previousSelectedScore", result.renderedFeedbackPreviousSelectedScore },
        { "bestId", result.renderedFeedbackApplied
            ? result.renderedFeedbackBestId
            : toneJson.value("autoCandidateRenderedBestId", std::string()) },
        { "bestScore", result.renderedFeedbackApplied
            ? result.renderedFeedbackBestScore
            : toneJson.value("autoCandidateRenderedBestScore", -1.0f) },
        { "improvement", result.renderedFeedbackImprovement },
        { "revisionStage", toneJson.value("autoCandidateRenderedRevisionStage", std::string()) },
        { "revisionReason", toneJson.value("autoCandidateRenderedRevisionReason", std::string()) },
        { "stabilityStatus", toneJson.value("autoCandidateRenderedStabilityStatus", std::string()) },
        { "trendStatus", toneJson.value("autoCandidateRenderedTrendStatus", std::string()) },
        { "monotonicGuardStatus", toneJson.value("autoCandidateRenderedMonotonicGuardStatus", std::string()) },
        { "monotonicMetric", toneJson.value("autoCandidateRenderedMonotonicMetric", std::string()) },
        { "historyCount", renderedHistoryCount },
        { "carriedForwardCount", result.renderedFeedbackCarriedForwardCount },
        { "continuationPolicy", continuationPolicy }
    };

    nlohmann::json learningRecord = BuildDevelopAutoCandidateLearningRecord(result);
    toneJson["autoCandidateLearningVersion"] = learningRecord.value("version", std::string());
    toneJson["autoCandidateLearningStatus"] = learningRecord.value("status", std::string());
    toneJson["autoCandidateLearningRecorded"] = learningRecord.value("recorded", false);
    toneJson["autoCandidateLearningApplied"] = learningRecord.value("applied", false);
    toneJson["autoCandidateLearningAppliedToCurrentImage"] =
        learningRecord.value("appliedToCurrentImage", false);
    toneJson["autoCandidateLearningAppliedToFutureImages"] =
        learningRecord.value("appliedToFutureImages", false);
    toneJson["autoCandidateLearningEventCount"] = learningRecord.value("eventCount", 0);
    toneJson["autoCandidateLearningRecord"] = std::move(learningRecord);
}

} // namespace Stack::Editor::DevelopAutoSolveDiagnostics
