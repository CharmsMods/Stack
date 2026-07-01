#include "Editor/Internal/EditorModuleDevelopCandidateGeneration.h"

#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleDevelopCandidateGuidance.h"
#include "Editor/Internal/EditorModuleDevelopCandidateScoreComponents.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackApplication.h"
#include "Editor/Internal/EditorModuleDevelopRenderedFeedbackConvergence.h"
#include "Editor/Internal/EditorModuleDevelopSubjectImportance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Stack::Editor::DevelopCandidateGeneration {

using namespace Stack::Editor::DevelopCandidateGuidance;
using namespace Stack::Editor::DevelopCandidateScoring;
using namespace Stack::Editor::DevelopDynamicRange;
using namespace Stack::Editor::DevelopRenderedFeedback;
using namespace Stack::Editor::DevelopSubjectImportance;

namespace {

float SaturateFloat(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

bool HasMeaningfulRawWhiteBalanceMetadata(const Raw::RawMetadata& metadata) {
    return metadata.cameraWhiteBalance[0] > 0.001f &&
        metadata.cameraWhiteBalance[1] > 0.001f &&
        metadata.cameraWhiteBalance[2] > 0.001f &&
        metadata.cameraWhiteBalance[3] > 0.001f;
}

bool HasMeaningfulRawDaylightWhiteBalanceMetadata(const Raw::RawMetadata& metadata) {
    return metadata.daylightWhiteBalance[0] > 0.001f &&
        metadata.daylightWhiteBalance[1] > 0.001f &&
        metadata.daylightWhiteBalance[2] > 0.001f &&
        metadata.daylightWhiteBalance[3] > 0.001f;
}

std::array<float, 3> NormalizeRawWhiteBalanceTriplet(const std::array<float, 4>& values) {
    const float green = std::max(0.001f, values[1]);
    return {
        std::max(0.001f, values[0]) / green,
        1.0f,
        std::max(0.001f, values[2]) / green
    };
}

float RawWhiteBalanceTripletDistance(
    const std::array<float, 3>& a,
    const std::array<float, 3>& b) {
    return
        std::fabs(std::log2(std::max(0.001f, a[0]) / std::max(0.001f, b[0]))) +
        std::fabs(std::log2(std::max(0.001f, a[2]) / std::max(0.001f, b[2])));
}
bool TryReadRememberedCandidateRejection(
    const nlohmann::json& toneJson,
    std::uint64_t candidateContextFingerprint,
    const std::string& candidateId,
    std::string& outReason) {
    const nlohmann::json memory =
        toneJson.value("autoCandidateRejectedMemory", nlohmann::json::array());
    if (!memory.is_array() || candidateContextFingerprint == 0 || candidateId.empty()) {
        return false;
    }

    for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
        if (!it->is_object() ||
            it->value("contextFingerprint", static_cast<std::uint64_t>(0)) != candidateContextFingerprint ||
            it->value("id", std::string()) != candidateId) {
            continue;
        }
        outReason = it->value(
            "reason",
            std::string("Rejected from recent solver memory for this same image/state context."));
        return true;
    }
    return false;
}

bool TryReadRememberedRenderedCandidateRejection(
    const nlohmann::json& toneJson,
    std::uint64_t guidanceFingerprint,
    const std::string& candidateId,
    std::string& outReason) {
    const nlohmann::json memory =
        toneJson.value("autoCandidateRenderedRejectionMemory", nlohmann::json::array());
    if (!memory.is_array() || guidanceFingerprint == 0 || candidateId.empty()) {
        return false;
    }

    for (auto it = memory.rbegin(); it != memory.rend(); ++it) {
        if (!it->is_object() ||
            it->value("guidanceFingerprint", static_cast<std::uint64_t>(0)) != guidanceFingerprint ||
            it->value("id", std::string()) != candidateId) {
            continue;
        }
        outReason = it->value(
            "reason",
            std::string("Rejected from recent rendered candidate memory for this same authored state."));
        return true;
    }
    return false;
}


DevelopDynamicRangeRegionEvidence ResolveDevelopDynamicRangeRegionEvidence(
    const nlohmann::json& previousToneJson) {
    DevelopDynamicRangeRegionEvidence evidence;
    if (!previousToneJson.is_object()) {
        return evidence;
    }

    const std::string metricsStatus =
        previousToneJson.value("autoCandidateRenderMetricsStatus", std::string());
    if (metricsStatus != "ready" && metricsStatus != "partial") {
        evidence.source = metricsStatus.empty()
            ? "awaitingRenderedMetrics"
            : std::string("renderMetrics") + metricsStatus;
        return evidence;
    }

    const nlohmann::json renderedSolves =
        previousToneJson.value("autoCandidateRenderedSolves", nlohmann::json::array());
    if (!renderedSolves.is_array()) {
        evidence.source = "renderMetricsMissing";
        return evidence;
    }

    EditorRenderWorker::DevelopCandidateRenderMetrics metrics;
    float renderScore = -1.0f;
    const std::array<std::pair<std::string, std::string>, 4> candidateSources = {{
        { previousToneJson.value("autoCandidateSelectedId", std::string()), "selectedRenderedCandidate" },
        { previousToneJson.value("autoCandidateRenderedFeedbackPreviousSelectedId", std::string()), "previousSelectedRenderedCandidate" },
        { std::string("base"), "baseRenderedCandidate" },
        { previousToneJson.value("autoCandidateRenderedBestId", std::string()), "bestRenderedCandidate" }
    }};

    for (const auto& [candidateId, source] : candidateSources) {
        if (candidateId.empty()) {
            continue;
        }
        if (TryReadRenderedCandidateMetrics(renderedSolves, candidateId, metrics, renderScore)) {
            return BuildDevelopDynamicRangeRegionEvidenceFromMetrics(
                metrics,
                source,
                candidateId,
                renderScore);
        }
    }

    evidence.source = "renderedCandidateMetricsUnavailable";
    return evidence;
}

bool HasDevelopAutoCandidateId(
    const std::vector<DevelopAutoCandidateSolve>& candidates,
    const std::string& id) {
    if (id.empty()) {
        return true;
    }
    return std::any_of(candidates.begin(), candidates.end(), [&](const DevelopAutoCandidateSolve& candidate) {
        return candidate.id == id;
    });
}

bool ReadDevelopCandidateGuidanceFromJson(
    const nlohmann::json& value,
    EditorNodeGraph::DevelopAutoGuidance& guidance) {
    if (!value.is_object()) {
        return false;
    }

    guidance.autoStrength = value.value("autoStrength", guidance.autoStrength);
    guidance.exposureBias = value.value("brightnessIntent", guidance.exposureBias);
    guidance.dynamicRange = value.value("dynamicRange", guidance.dynamicRange);
    guidance.shadowLift = value.value("shadowLift", guidance.shadowLift);
    guidance.highlightGuard = value.value("highlightGuard", guidance.highlightGuard);
    guidance.highlightCharacter = value.value("highlightCharacter", guidance.highlightCharacter);
    guidance.contrastBias = value.value("contrastBias", guidance.contrastBias);
    guidance.subjectSceneBias = value.value("subjectSceneBias", guidance.subjectSceneBias);
    guidance.moodReadabilityBias = value.value("moodReadabilityBias", guidance.moodReadabilityBias);
    EditorModule::NormalizeDevelopAutoGuidance(guidance);
    return true;
}

bool TryReadDevelopAutoCandidateFromToneJson(
    const nlohmann::json& toneJson,
    const std::string& candidateId,
    DevelopAutoCandidateSolve& outCandidate) {
    if (!toneJson.is_object() || candidateId.empty()) {
        return false;
    }

    const nlohmann::json candidates =
        toneJson.value("autoCandidateSolves", nlohmann::json::array());
    if (!candidates.is_array()) {
        return false;
    }

    for (const nlohmann::json& candidateJson : candidates) {
        if (!candidateJson.is_object() ||
            candidateJson.value("id", std::string()) != candidateId) {
            continue;
        }

        EditorNodeGraph::DevelopAutoGuidance guidance;
        if (!ReadDevelopCandidateGuidanceFromJson(
                candidateJson.value("guidance", nlohmann::json::object()),
                guidance)) {
            return false;
        }

        outCandidate.id = candidateId;
        outCandidate.label = candidateJson.value("label", candidateId);
        outCandidate.reason =
            "Carried forward the previous authored candidate so rendered feedback can converge across passes.";
        outCandidate.guidance = guidance;
        outCandidate.guidanceFingerprint =
            candidateJson.value(
                "guidanceFingerprint",
                BuildDevelopAutoCandidateGuidanceFingerprint(candidateId, guidance));
        outCandidate.score = std::clamp(candidateJson.value("score", 0.50f), 0.0f, 1.0f);
        outCandidate.scoreComponents =
            candidateJson.value("scoreComponents", nlohmann::json::object());
        if (!outCandidate.scoreComponents.is_object()) {
            outCandidate.scoreComponents = nlohmann::json::object();
        }
        outCandidate.rejected = false;
        outCandidate.duplicate = false;
        outCandidate.rememberedRejection = false;
        outCandidate.renderedMemoryRejected = false;
        Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
        outCandidate.whiteBalanceProbe =
            TryResolveDevelopWhiteBalanceProbeMode(candidateId, whiteBalanceMode);
        outCandidate.whiteBalanceMode = whiteBalanceMode;
        outCandidate.rejectReason.clear();
        return true;
    }
    return false;
}

std::vector<std::string> CollectDevelopRenderedSurvivorCandidateIdsForCarryForward(
    const nlohmann::json& toneJson,
    std::size_t maxIds) {
    std::vector<std::pair<float, std::string>> scoredIds;
    if (maxIds == 0) {
        return {};
    }

    const nlohmann::json renderedSolves =
        toneJson.value("autoCandidateRenderedSolves", nlohmann::json::array());
    if (!renderedSolves.is_array()) {
        return {};
    }

    for (const nlohmann::json& rendered : renderedSolves) {
        if (!rendered.is_object() || !rendered.value("success", false)) {
            continue;
        }
        const std::string id = rendered.value("id", std::string());
        if (id.empty()) {
            continue;
        }
        const std::string status = rendered.value("renderedStatus", std::string());
        if (status == "renderedRejectedDamage" || status == "renderedDuplicate") {
            continue;
        }
        const float renderScore = rendered.value("renderScore", -1.0f);
        if (renderScore < 0.0f) {
            continue;
        }
        scoredIds.emplace_back(renderScore, id);
    }

    std::sort(scoredIds.begin(), scoredIds.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    std::vector<std::string> ids;
    for (const auto& scored : scoredIds) {
        if (std::find(ids.begin(), ids.end(), scored.second) != ids.end()) {
            continue;
        }
        ids.push_back(scored.second);
        if (ids.size() >= maxIds) {
            break;
        }
    }
    return ids;
}

} // namespace

DevelopAutoCandidateSolveResult BuildDevelopAutoCandidateSolve(
    const EditorNodeGraph::DevelopAutoGuidance& modeGuidance,
    EditorNodeGraph::DevelopAutoIntent intent,
    const EditorNodeGraph::DevelopSubjectImportanceMap& subjectImportance,
    const DevelopToneAutoStats& stats,
    const Raw::RawMetadata& metadata,
    const nlohmann::json& previousToneJson) {
    DevelopAutoCandidateSolveResult result;
    result.candidateContextFingerprint =
        BuildDevelopAutoCandidateContextFingerprint(modeGuidance, intent, subjectImportance, stats);
    const float darkness =
        stats.valid ? std::clamp((0.18f - stats.midtonePercentile) / 0.18f, 0.0f, 1.0f) : 0.0f;
    const float deepShadow =
        stats.valid ? std::clamp((0.06f - stats.shadowPercentile) / 0.06f, 0.0f, 1.0f) : 0.0f;
    const float hdrNeed = SaturateFloat((stats.hdrSpreadEv - 2.8f) / 2.8f);
    const float flatSceneNeed =
        SaturateFloat((2.35f - stats.hdrSpreadEv) / 1.15f) *
        SaturateFloat(1.0f - stats.highlightPressure * 1.35f) *
        SaturateFloat(1.0f - stats.noiseRisk * 0.65f);
    const float shadowRescueNeed = SaturateFloat(darkness * 0.72f + deepShadow * 0.28f);
    const float underBrightBroadHighlightEv = stats.valid && stats.midtonePercentile > 0.08f
        ? std::clamp(std::log2(0.70f / std::max(stats.highlightPercentile, 0.0001f)), 0.0f, 1.25f)
        : 0.0f;
    const float tinySpecularAllowance = stats.valid
        ? SaturateFloat((0.010f - stats.clippingRatio) / 0.010f) *
            SaturateFloat((0.72f - stats.highlightPressure) / 0.72f)
        : 0.0f;
    const DevelopDynamicRangeRegionEvidence regionEvidence =
        ResolveDevelopDynamicRangeRegionEvidence(previousToneJson);
    const DevelopDynamicRangeStrategy dynamicRangeStrategy =
        ResolveDevelopDynamicRangeStrategy(
            intent,
            modeGuidance,
            stats,
            regionEvidence,
            darkness,
            shadowRescueNeed,
            hdrNeed,
            flatSceneNeed,
            tinySpecularAllowance);
    result.dynamicRangeStrategy = DevelopDynamicRangeStrategyToJson(dynamicRangeStrategy);
    const DevelopSubjectSceneIntent subjectSceneIntent =
        ResolveDevelopSubjectSceneIntent(
            intent,
            modeGuidance,
            subjectImportance,
            stats,
            regionEvidence);
    result.subjectSceneIntent = DevelopSubjectSceneIntentToJson(subjectSceneIntent);
    const DevelopContinuationCandidateBiasProfile continuationBias =
        ResolveDevelopContinuationCandidateBiasProfile(previousToneJson);
    result.continuationBiasActive = continuationBias.active;
    result.continuationBiasReason = continuationBias.reason;
    result.continuationBiasDecision = continuationBias.decision;
    result.continuationBiasStage = continuationBias.stageFocus;
    result.continuationBiasRefineIntent = continuationBias.refineIntent;
    result.continuationExpansionEligible = continuationBias.active;
    result.continuationExpansionReason = continuationBias.reason;
    result.continuationExpansionStage = continuationBias.stageFocus;
    result.continuationExpansionRefineIntent = continuationBias.refineIntent;

    auto addCandidateCore = [&](std::string id,
                                std::string label,
                                std::string reason,
                                EditorNodeGraph::DevelopAutoGuidance guidance,
                                bool continuationExpansionCandidate,
                                std::string continuationExpansionReason) {
        DevelopAutoCandidateSolve candidate;
        candidate.id = std::move(id);
        candidate.label = std::move(label);
        candidate.reason = std::move(reason);
        candidate.guidance = guidance;
        candidate.guidance.intent = intent;
        candidate.continuationExpansionCandidate = continuationExpansionCandidate;
        if (candidate.continuationExpansionCandidate) {
            candidate.continuationExpansionReason = std::move(continuationExpansionReason);
            candidate.continuationExpansionStage = continuationBias.stageFocus;
            candidate.continuationExpansionRefineIntent = continuationBias.refineIntent;
        }
        Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
        candidate.whiteBalanceProbe =
            TryResolveDevelopWhiteBalanceProbeMode(candidate.id, whiteBalanceMode);
        candidate.whiteBalanceMode = whiteBalanceMode;
        candidate.guidanceFingerprint =
            BuildDevelopAutoCandidateGuidanceFingerprint(
                candidate.id,
                candidate.guidance,
                &subjectImportance);
        candidate.score = ScoreDevelopAutoCandidate(
            candidate.id,
            intent,
            modeGuidance,
            stats,
            regionEvidence,
            dynamicRangeStrategy,
            subjectSceneIntent,
            darkness,
            shadowRescueNeed,
            hdrNeed,
            flatSceneNeed,
            underBrightBroadHighlightEv);
        if (ApplyDevelopContinuationCandidateBias(candidate, continuationBias)) {
            ++result.continuationBiasAppliedCount;
        }
        candidate.scoreComponents = BuildDevelopAutoCandidateScoreComponents(
            candidate,
            modeGuidance,
            intent,
            stats,
            regionEvidence,
            dynamicRangeStrategy,
            subjectSceneIntent,
            darkness,
            shadowRescueNeed,
            hdrNeed,
            flatSceneNeed,
            underBrightBroadHighlightEv);
        RejectDevelopAutoCandidateForDamage(candidate, modeGuidance, stats, intent);
        std::string rememberedReason;
        if (candidate.id != "base" &&
            TryReadRememberedCandidateRejection(
                previousToneJson,
                result.candidateContextFingerprint,
                candidate.id,
                rememberedReason)) {
            candidate.rejected = true;
            candidate.rememberedRejection = true;
            candidate.rejectReason = "Rejected from solver memory: " + rememberedReason;
            ++result.rejectedMemorySuppressionCount;
        }
        if (!candidate.rejected && candidate.id != "base" &&
            TryReadRememberedRenderedCandidateRejection(
                previousToneJson,
                candidate.guidanceFingerprint,
                candidate.id,
                rememberedReason)) {
            candidate.rejected = true;
            candidate.rememberedRejection = true;
            candidate.renderedMemoryRejected = true;
            candidate.rejectReason = "Rejected from rendered memory: " + rememberedReason;
            ++result.renderedRejectedMemorySuppressionCount;
        }
        result.candidates.push_back(std::move(candidate));
    };
    auto addCandidate = [&](std::string id,
                            std::string label,
                            std::string reason,
                            EditorNodeGraph::DevelopAutoGuidance guidance) {
        addCandidateCore(
            std::move(id),
            std::move(label),
            std::move(reason),
            guidance,
            false,
            std::string());
    };

    addCandidate(
        "base",
        "Base Solve",
        "Mode-aware solve using the current Auto intent as the starting point.",
        modeGuidance);

    if (stats.valid) {
        const bool rangeIntent =
            intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
        const bool flatIntent =
            intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
        const float strategyHighlightBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapHighlightPriority - 0.5f);
        const float strategyShadowBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapShadowVisibility - 0.5f);
        const float strategyNaturalContrastBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapNaturalContrast - 0.5f);
        const float strategyVisibleRangeBias =
            std::max(0.0f, dynamicRangeStrategy.strategyMapVisibleRange - 0.5f);
        const float subjectRevealIntent = SaturateFloat(
            subjectSceneIntent.subjectPriority * 0.26f +
            subjectSceneIntent.improveReadability * 0.24f +
            subjectSceneIntent.readabilityPressure * 0.14f +
            subjectSceneIntent.automaticConfidence * 0.08f +
            std::max(0.0f, subjectSceneIntent.userSubjectSceneBias) * 0.16f +
            std::max(0.0f, subjectSceneIntent.userMoodReadabilityBias) * 0.14f -
            regionEvidence.shadowNoiseLiftRisk * 0.08f -
            regionEvidence.localHaloRisk * 0.04f);
        const float sceneMoodIntent = SaturateFloat(
            subjectSceneIntent.sceneIntegrity * 0.24f +
            subjectSceneIntent.preserveMood * 0.26f +
            subjectSceneIntent.moodPreservationPressure * 0.14f +
            darkness * 0.10f +
            stats.noiseRisk * 0.06f +
            std::max(0.0f, -subjectSceneIntent.userSubjectSceneBias) * 0.12f +
            std::max(0.0f, -subjectSceneIntent.userMoodReadabilityBias) * 0.16f);
        const bool subjectReadableCandidateNeeded =
            subjectSceneIntent.subjectPriority > 0.56f &&
            subjectRevealIntent > 0.42f &&
            (subjectSceneIntent.userGuidanceActive ||
                subjectSceneIntent.automaticConfidence > 0.46f ||
                subjectSceneIntent.readabilityPressure > 0.30f) &&
            regionEvidence.shadowNoiseLiftRisk < 0.82f;
        const bool sceneMoodCandidateNeeded =
            sceneMoodIntent > 0.46f &&
            (subjectSceneIntent.userGuidanceActive ||
                subjectSceneIntent.preserveMood > 0.60f ||
                darkness > 0.22f) &&
            (subjectSceneIntent.sceneIntegrity > 0.52f ||
                subjectSceneIntent.userSubjectSceneBias < -0.20f ||
                subjectSceneIntent.userMoodReadabilityBias < -0.20f);

        auto addModeNeighborCandidate = [&](std::string id,
                                            std::string label,
                                            std::string reason,
                                            EditorNodeGraph::DevelopAutoGuidance guidance) {
            addCandidate(std::move(id), std::move(label), std::move(reason), guidance);
        };

        // Mode-neighbor probes are nearby intent vectors, not presets. They let
        // rendered feedback compare the current mode against plausible adjacent
        // tradeoffs without changing the selected Auto mode.
        switch (intent) {
            case EditorNodeGraph::DevelopAutoIntent::NaturalFinished:
                if (hdrNeed > 0.12f || stats.highlightPressure > 0.38f) {
                    addModeNeighborCandidate(
                        "modeNeighborNaturalMoreRange",
                        "Natural More Range",
                        "Test the neighboring Natural/Range intent when broad highlights or HDR spread make the default natural solve uncertain.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            -0.06f,
                            0.30f,
                            0.14f * (1.0f - stats.noiseRisk * 0.35f),
                            0.22f,
                            -0.05f,
                            -0.18f));
                }
                if (shadowRescueNeed > 0.18f || underBrightBroadHighlightEv > 0.12f) {
                    addModeNeighborCandidate(
                        "modeNeighborNaturalBrighterMids",
                        "Natural Brighter Mids",
                        "Test the neighboring Natural/Bright intent when useful mids may be landing too low.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            0.15f,
                            0.07f,
                            0.13f,
                            0.09f,
                            0.00f,
                            -0.04f));
                }
                if (flatSceneNeed > 0.16f || (stats.textureConfidence > 0.55f && stats.noiseRisk < 0.68f)) {
                    addModeNeighborCandidate(
                        "modeNeighborNaturalPunchier",
                        "Natural More Contrast",
                        "Test the neighboring Natural/Punchy intent when the image may support more separation without fake HDR.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            0.00f,
                            -0.18f,
                            -0.14f,
                            0.04f,
                            0.10f,
                            0.26f));
                }
                break;
            case EditorNodeGraph::DevelopAutoIntent::BrightNatural:
                addModeNeighborCandidate(
                    "modeNeighborBrightHighlightSafe",
                    "Bright Highlight Safe",
                    "Test a neighboring Bright/Range intent so a brighter result can keep believable highlight headroom.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        -0.05f,
                        0.22f,
                        0.04f,
                        0.28f,
                        -0.04f,
                        -0.09f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::DarkNatural:
                addModeNeighborCandidate(
                    "modeNeighborDarkReadableMids",
                    "Dark Readable Mids",
                    "Test a neighboring Dark/Natural intent that keeps mood while checking whether important mids need a little readability.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        0.08f,
                        0.10f,
                        0.14f,
                        0.05f,
                        -0.01f,
                        -0.03f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast:
                addModeNeighborCandidate(
                    "modeNeighborPunchySaferRange",
                    "Punchy Safer Range",
                    "Test a neighboring Punchy/Range intent that preserves separation while backing away from harsh highlight or shadow pressure.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        -0.03f,
                        0.22f,
                        0.08f,
                        0.22f,
                        -0.04f,
                        -0.14f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail:
                addModeNeighborCandidate(
                    "modeNeighborRangeNaturalShape",
                    "Range Natural Shape",
                    "Test a neighboring Range/Natural intent that gives back some contrast if maximum range starts to look too flat.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        0.02f,
                        -0.24f,
                        -0.12f,
                        -0.05f,
                        0.06f,
                        0.18f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::FlatEditingBase:
                addModeNeighborCandidate(
                    "modeNeighborFlatNaturalShape",
                    "Flat Natural Shape",
                    "Test a neighboring Flat/Natural intent so the editing base keeps enough tonal shape to remain useful.",
                    AdjustDevelopAutoCandidateGuidance(
                        modeGuidance,
                        0.02f,
                        -0.18f,
                        -0.08f,
                        -0.02f,
                        0.05f,
                        0.16f));
                break;
            case EditorNodeGraph::DevelopAutoIntent::CleanBase:
                if (stats.textureConfidence > 0.42f) {
                    addModeNeighborCandidate(
                        "modeNeighborCleanTextureCheck",
                        "Clean Texture Check",
                        "Test a neighboring Clean/Natural intent so the clean base does not erase real texture.",
                        AdjustDevelopAutoCandidateGuidance(
                            modeGuidance,
                            0.00f,
                            0.04f,
                            0.05f,
                            0.02f,
                            0.05f,
                            0.13f));
                }
                break;
        }

        const bool hasCameraWhiteBalance =
            HasMeaningfulRawWhiteBalanceMetadata(metadata);
        const bool hasDaylightWhiteBalance =
            HasMeaningfulRawDaylightWhiteBalanceMetadata(metadata);
        const std::array<float, 3> neutralWhiteBalance = { 1.0f, 1.0f, 1.0f };
        const std::array<float, 3> cameraWhiteBalance =
            hasCameraWhiteBalance
                ? NormalizeRawWhiteBalanceTriplet(metadata.cameraWhiteBalance)
                : neutralWhiteBalance;
        const std::array<float, 3> daylightWhiteBalance =
            hasDaylightWhiteBalance
                ? NormalizeRawWhiteBalanceTriplet(metadata.daylightWhiteBalance)
                : neutralWhiteBalance;
        const float cameraDaylightDistance =
            hasCameraWhiteBalance && hasDaylightWhiteBalance
                ? RawWhiteBalanceTripletDistance(cameraWhiteBalance, daylightWhiteBalance)
                : 0.0f;
        const float cameraNeutralDistance =
            hasCameraWhiteBalance
                ? RawWhiteBalanceTripletDistance(cameraWhiteBalance, neutralWhiteBalance)
                : 0.0f;

        if (hasCameraWhiteBalance &&
            hasDaylightWhiteBalance &&
            cameraDaylightDistance > 0.18f) {
            addCandidate(
                "wbDaylightCorrection",
                "WB Daylight Correction",
                "Test a daylight-oriented RAW white balance when camera and daylight metadata meaningfully disagree.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.03f,
                    0.00f,
                    0.04f,
                    -0.01f,
                    -0.03f));
        }

        const bool neutralCorrectionUseful =
            hasCameraWhiteBalance &&
            cameraNeutralDistance > 0.42f &&
            (intent == EditorNodeGraph::DevelopAutoIntent::CleanBase ||
                intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase ||
                intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail ||
                cameraDaylightDistance > 0.26f);
        if (neutralCorrectionUseful) {
            addCandidate(
                "wbNeutralCorrection",
                "WB Neutral Correction",
                "Test a conservative neutral RAW white balance for technically clean or editing-oriented output.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.02f,
                    0.00f,
                    0.03f,
                    -0.02f,
                    -0.04f));
        }

        // Candidate families are authored-state probes; they do not render or blend pixels here.
        addCandidate(
            "protectHighlights",
            "Protect Highlights",
            "Lower visible placement and strengthen highlight protection for broad bright-region risk.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                -0.08f - stats.highlightPressure * 0.12f,
                0.18f,
                0.06f,
                0.34f,
                -0.04f,
                -0.08f));

        if (hdrNeed > 0.18f || stats.highlightPressure > 0.42f || underBrightBroadHighlightEv > 0.18f) {
            addCandidate(
                "highlightProtectedMids",
                "Highlight-Protected Mids",
                "Test a lower global/RAW placement with local midtone support so highlights keep headroom without burying useful mids.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.18f - stats.highlightPressure * 0.12f,
                    0.34f,
                    0.28f * (1.0f - stats.noiseRisk * 0.35f),
                    0.38f,
                    -0.05f,
                    -0.16f));
        }

        if (dynamicRangeStrategy.broadHighlightGuardNeed > 0.34f ||
            dynamicRangeStrategy.strategyMapHighlightPriority > 0.66f ||
            dynamicRangeStrategy.id == "broadHighlightGuard" ||
            (stats.highlightPressure > 0.58f &&
                stats.clippingRatio > 0.006f &&
                !dynamicRangeStrategy.smallSpecularClippingAllowed)) {
            addCandidate(
                "broadHighlightGuard",
                "Broad Highlight Guard",
                "Test scene-prep highlight compression for broad bright regions while keeping RAW placement stable; this preserves visible range and does not recover fully clipped detail.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f + strategyVisibleRangeBias * 0.08f,
                    0.02f,
                    0.34f + strategyHighlightBias * 0.08f,
                    -0.06f,
                    -0.12f));
        }

        addCandidate(
            "brighterMids",
            "Brighter Mids",
            "Raise important midtone placement while keeping highlight guard active.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                0.10f + shadowRescueNeed * 0.10f,
                0.08f,
                0.16f,
                0.10f,
                0.00f,
                -0.04f));

        addCandidate(
            "maximumRange",
            "More Range",
            "Fit more highlight and shadow information into the visible range without claiming clipped-data recovery.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                -stats.highlightPressure * 0.06f,
                0.40f + strategyVisibleRangeBias * 0.12f,
                0.22f * (1.0f - stats.noiseRisk * 0.45f) + strategyShadowBias * 0.07f,
                0.32f + strategyHighlightBias * 0.08f,
                -0.06f,
                -0.20f - strategyVisibleRangeBias * 0.06f));

        if (regionEvidence.valid &&
            (regionEvidence.localRangeConflict > 0.40f ||
                regionEvidence.localEvConflict > 0.38f ||
                regionEvidence.localHaloRisk > 0.30f ||
                regionEvidence.localDamageRiskPeak > 0.62f ||
                (dynamicRangeStrategy.strategyMapVisibleRange > 0.68f &&
                    regionEvidence.localEvConflict > 0.28f))) {
            addCandidate(
                "localRangeGuard",
                "Local Range Guard",
                "Use rendered regional evidence to test gentler scene-prep range shaping where local highlight, shadow, or halo pressure is concentrated.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f,
                    0.08f * (1.0f - regionEvidence.shadowNoiseLiftRisk * 0.55f),
                    0.20f,
                    -0.04f,
                    -0.10f));
        }

        if (dynamicRangeStrategy.localHaloGuardNeed > 0.32f ||
            dynamicRangeStrategy.id == "haloSafeLocalRange" ||
            (regionEvidence.valid &&
                regionEvidence.localHaloRisk > 0.34f)) {
            addCandidate(
                "haloSafeLocalRange",
                "Halo-Safe Local Range",
                "Test safer scene-prep local exposure with stronger halo and smooth-gradient guardrails where rendered regional evidence warns about edge glow or artificial relighting.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.02f,
                    -0.08f,
                    -0.08f,
                    0.12f,
                    -0.04f,
                    -0.06f));
        }

        if (dynamicRangeStrategy.shadowReadabilityLiftNeed > 0.34f ||
            dynamicRangeStrategy.strategyMapShadowVisibility > 0.66f ||
            dynamicRangeStrategy.id == "shadowReadabilityLift" ||
            (shadowRescueNeed > 0.42f &&
                stats.noiseRisk < 0.48f &&
                regionEvidence.shadowNoiseLiftRisk < 0.52f)) {
            addCandidate(
                "shadowReadabilityLift",
                "Shadow Readability Lift",
                "Test scene-prep shadow and midtone opening for readable shadows while keeping RAW placement stable and noise guardrails active.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f + strategyVisibleRangeBias * 0.05f,
                    0.30f + strategyShadowBias * 0.08f,
                    0.08f,
                    -0.03f,
                    -0.08f));
        }

        if (subjectReadableCandidateNeeded) {
            addCandidate(
                "subjectReadableMids",
                "Subject Readable Mids",
                "Test a subject-priority Scene Prep branch that opens likely or user-marked important mids while keeping highlight, noise, and halo guardrails active.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.04f + std::max(0.0f, subjectSceneIntent.userMoodReadabilityBias) * 0.04f,
                    0.14f + subjectRevealIntent * 0.10f,
                    0.18f + subjectRevealIntent * 0.10f,
                    0.08f + subjectSceneIntent.protectionPressure * 0.05f,
                    -0.02f,
                    -0.06f));
        }

        if (sceneMoodCandidateNeeded) {
            addCandidate(
                "sceneMoodPreservation",
                "Scene Mood Preservation",
                "Test a scene-integrity branch that keeps low-key mood or silhouette intent from being forced into gray midtones; subject importance remains a bias, not a command.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    -0.04f,
                    -0.18f - sceneMoodIntent * 0.06f,
                    0.08f + subjectSceneIntent.protectionPressure * 0.04f,
                    0.03f,
                    0.08f));
        }

        if (dynamicRangeStrategy.shadowNoiseFloorNeed > 0.34f ||
            dynamicRangeStrategy.id == "shadowNoiseFloor" ||
            (stats.noiseRisk > 0.58f && shadowRescueNeed > 0.18f)) {
            addCandidate(
                "shadowNoiseFloor",
                "Shadow Noise Floor",
                "Test holding noisy or low-value dark regions darker with Scene Prep limits instead of lifting them into gray noise.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    -0.10f,
                    -0.24f,
                    0.10f,
                    0.02f,
                    0.07f));
        }

        addCandidate(
            "preserveMood",
            "Preserve Mood",
            "Keep darker scene hierarchy and avoid forcing low-key images into gray mids.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                -0.18f,
                -0.06f,
                -0.20f,
                0.06f,
                0.06f,
                0.10f));

        addCandidate(
            "strongerContrast",
            "More Contrast",
            "Restore separation and endpoints when the scene can support a punchier finish.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                0.00f,
                -0.12f,
                -0.10f,
                0.03f,
                0.10f,
                0.28f));

        if (hdrNeed > 0.18f || stats.highlightPressure > 0.34f) {
            addCandidate(
                "toneSofterRolloff",
                "Softer Highlight Rolloff",
                "Test a finish-tone shoulder that compresses visible highlights more gently without claiming clipped-data recovery.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.08f,
                    0.02f,
                    0.16f,
                    -0.18f,
                    -0.18f));
        }

        if (dynamicRangeStrategy.brightHighlightRolloffNeed > 0.32f ||
            dynamicRangeStrategy.id == "brightHighlightRolloff") {
            addCandidate(
                "brightHighlightRolloff",
                "Bright Highlight Rolloff",
                "Test a downstream highlight shoulder that preserves the feeling of bright light while still smoothing rolloff; this shapes visible data and does not recover fully clipped detail.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.01f,
                    -0.05f,
                    -0.03f,
                    0.10f,
                    0.24f,
                    0.08f));
        }

        if (dynamicRangeStrategy.highlightBrightnessAnchorNeed > 0.30f ||
            dynamicRangeStrategy.id == "luminousHighlightAnchor" ||
            (regionEvidence.valid &&
                regionEvidence.brightnessHierarchyRisk > 0.38f &&
                dynamicRangeStrategy.broadHighlightGuardNeed > 0.30f)) {
            addCandidate(
                "luminousHighlightAnchor",
                "Luminous Highlight Anchor",
                "Test downstream highlight separation so protected broad highlights stay luminous instead of flattening toward gray; this shapes visible range and does not recover clipped detail.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.04f,
                    0.02f,
                    0.30f,
                    0.16f));
        }

        if (dynamicRangeStrategy.naturalContrastGuardNeed > 0.32f ||
            (dynamicRangeStrategy.strategyMapNaturalContrast > 0.66f &&
                !rangeIntent &&
                !flatIntent) ||
            dynamicRangeStrategy.id == "naturalContrastGuard" ||
            (regionEvidence.valid &&
                (regionEvidence.flatGrayRisk > 0.40f ||
                    regionEvidence.brightnessHierarchyRisk > 0.42f))) {
            addCandidate(
                "naturalContrastGuard",
                "Natural Contrast Guard",
                "Test a downstream finish-tone shape that restores believable separation when range compression or flat-gray risk threatens the lighting hierarchy.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.12f,
                    -0.06f,
                    0.02f,
                    0.14f + strategyNaturalContrastBias * 0.05f,
                    0.24f + strategyNaturalContrastBias * 0.08f));
        }

        if ((dynamicRangeStrategy.smallSpecularClippingAllowed &&
                dynamicRangeStrategy.specularHighlightToleranceNeed > 0.28f) ||
            dynamicRangeStrategy.id == "specularHighlightTolerance") {
            addCandidate(
                "specularHighlightTolerance",
                "Specular Highlight Tolerance",
                "Test a finish-tone shape that lets tiny specular cores stay bright while preserving smooth rolloff around them; this is not clipped-data recovery.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.03f,
                    -0.16f,
                    -0.04f,
                    -0.16f,
                    0.32f,
                    0.14f));
        }

        if (flatSceneNeed > 0.14f ||
            stats.textureConfidence > 0.50f ||
            intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast ||
            dynamicRangeStrategy.strategyMapNaturalContrast > 0.68f) {
            addCandidate(
                "tonePunchierShape",
                "Punchier Tone Shape",
                "Test a downstream tone shape with stronger separation while keeping upstream RAW and scene prep stable.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.06f,
                    0.02f,
                    0.12f,
                    0.24f));
        }

        if (intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase ||
            intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail ||
            hdrNeed > 0.30f ||
            dynamicRangeStrategy.strategyMapVisibleRange > 0.68f) {
            addCandidate(
                "toneFlatterEditing",
                "Flatter Editing Tone",
                "Test a finish-tone shape that leaves more visible range and gentler endpoints for later Manual editing.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f + strategyVisibleRangeBias * 0.08f,
                    0.08f,
                    0.16f,
                    -0.10f,
                    -0.26f));
        }

        if (intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural ||
            darkness > 0.24f ||
            stats.noiseRisk > 0.55f) {
            addCandidate(
                "toneDarkerToe",
                "Darker Shadow Toe",
                "Test a darker downstream shadow toe so low-key mood or noisy dark regions are not forced into gray mids.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.05f,
                    -0.05f,
                    -0.20f,
                    0.04f,
                    0.06f,
                    0.14f));
        }

        addCandidate(
            "cleanShadows",
            "Cleaner Shadows",
            "Reduce shadow-opening pressure when noise risk is high so texture stays natural.",
            AdjustDevelopAutoCandidateGuidance(
                modeGuidance,
                0.03f,
                -0.08f,
                -0.12f,
                0.12f,
                -0.02f,
                -0.06f));

        if (stats.textureConfidence > 0.45f && stats.noiseRisk < 0.82f) {
            addCandidate(
                "preserveTexture",
                "Preserve Texture",
                "Test a texture-preserving cleanup/detail balance when image stats show real texture that could be smeared.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.01f,
                    -0.04f,
                    -0.05f,
                    0.05f,
                    0.04f,
                    0.12f));
        }
    }

    auto addContinuationExpansionCandidate = [&](std::string id,
                                                 std::string label,
                                                 std::string reason,
                                                 EditorNodeGraph::DevelopAutoGuidance guidance) {
        if (!continuationBias.active ||
            !stats.valid ||
            HasDevelopAutoCandidateId(result.candidates, id)) {
            return false;
        }
        const std::string expansionReason =
            "Rendered continuation requested another " +
            (continuationBias.stageFocus.empty() ? std::string("multi-stage") : continuationBias.stageFocus) +
            " validation pass, so this candidate family was generated even though the first-pass stats gate did not require it.";
        addCandidateCore(
            std::move(id),
            std::move(label),
            std::move(reason),
            guidance,
            true,
            expansionReason);
        ++result.continuationExpansionAddedCount;
        return true;
    };

    if (continuationBias.active && stats.valid) {
        const bool wantsRawGlobal =
            continuationBias.stageFocus == "rawGlobal" ||
            continuationBias.refineIntent == "protectHighlights";
        const bool wantsScenePrep =
            continuationBias.stageFocus == "scenePrep" ||
            continuationBias.refineIntent == "brightenMids" ||
            continuationBias.refineIntent == "openShadows";
        const bool wantsFinishTone =
            continuationBias.stageFocus == "finishTone" ||
            continuationBias.refineIntent == "addContrast";
        const bool wantsRawCleanup =
            continuationBias.stageFocus == "rawCleanup" ||
            continuationBias.refineIntent == "cleanShadows" ||
            continuationBias.refineIntent == "preserveTexture";

        if (wantsRawGlobal) {
            addContinuationExpansionCandidate(
                "highlightProtectedMids",
                "Highlight-Protected Mids",
                "Re-test a lower global/RAW placement with local midtone support because rendered continuation is focused on RAW/global highlight placement.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.16f - stats.highlightPressure * 0.10f,
                    0.32f,
                    0.24f * (1.0f - stats.noiseRisk * 0.35f),
                    0.36f,
                    -0.05f,
                    -0.15f));
        }

        if (wantsScenePrep) {
            addContinuationExpansionCandidate(
                "broadHighlightGuard",
                "Broad Highlight Guard",
                "Re-test broad highlight scene-prep protection because rendered continuation is focused on local exposure/range control.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f,
                    0.02f,
                    0.34f,
                    -0.06f,
                    -0.12f));
            addContinuationExpansionCandidate(
                "localRangeGuard",
                "Local Range Guard",
                "Re-test gentler scene-prep range shaping because rendered continuation is focused on regional local exposure pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.24f,
                    0.08f * (1.0f - regionEvidence.shadowNoiseLiftRisk * 0.55f),
                    0.20f,
                    -0.04f,
                    -0.10f));
            addContinuationExpansionCandidate(
                "haloSafeLocalRange",
                "Halo-Safe Local Range",
                "Re-test halo-safe scene-prep range shaping because rendered continuation is focused on local exposure safety.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.02f,
                    -0.08f,
                    -0.08f,
                    0.12f,
                    -0.04f,
                    -0.06f));
            addContinuationExpansionCandidate(
                "shadowReadabilityLift",
                "Shadow Readability Lift",
                "Re-test readable-shadow scene-prep opening because rendered continuation is focused on local exposure and shadow readability.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f,
                    0.30f,
                    0.08f,
                    -0.03f,
                    -0.08f));
            addContinuationExpansionCandidate(
                "shadowNoiseFloor",
                "Shadow Noise Floor",
                "Re-test a guarded shadow floor because rendered continuation is focused on scene-prep local exposure and shadow/noise pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    -0.10f,
                    -0.24f,
                    0.10f,
                    0.02f,
                    0.07f));
            addContinuationExpansionCandidate(
                "modeNeighborNaturalMoreRange",
                "Natural More Range",
                "Re-test a nearby range/readability intent because rendered continuation is focused on scene-prep local exposure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.04f,
                    0.28f,
                    0.16f * (1.0f - stats.noiseRisk * 0.35f),
                    0.18f,
                    -0.04f,
                    -0.14f));
        }

        if (wantsFinishTone) {
            addContinuationExpansionCandidate(
                "toneSofterRolloff",
                "Softer Highlight Rolloff",
                "Re-test a gentler downstream highlight shoulder because rendered continuation is focused on finish tone.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    0.08f,
                    0.02f,
                    0.16f,
                    -0.18f,
                    -0.18f));
            addContinuationExpansionCandidate(
                "brightHighlightRolloff",
                "Bright Highlight Rolloff",
                "Re-test a downstream highlight shoulder that keeps bright regions luminous because rendered continuation is focused on finish tone.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.01f,
                    -0.05f,
                    -0.03f,
                    0.10f,
                    0.24f,
                    0.08f));
            addContinuationExpansionCandidate(
                "luminousHighlightAnchor",
                "Luminous Highlight Anchor",
                "Re-test downstream highlight separation because rendered continuation is focused on finish-tone highlight brightness feeling.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.04f,
                    0.02f,
                    0.30f,
                    0.16f));
            addContinuationExpansionCandidate(
                "specularHighlightTolerance",
                "Specular Highlight Tolerance",
                "Re-test tiny-specular highlight tolerance because rendered continuation is focused on finish-tone highlight shape.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.03f,
                    -0.16f,
                    -0.04f,
                    -0.16f,
                    0.32f,
                    0.14f));
            addContinuationExpansionCandidate(
                "naturalContrastGuard",
                "Natural Contrast Guard",
                "Re-test natural contrast separation because rendered continuation is focused on finish-tone hierarchy.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.12f,
                    -0.06f,
                    0.02f,
                    0.14f,
                    0.24f));
            addContinuationExpansionCandidate(
                "tonePunchierShape",
                "Punchier Tone Shape",
                "Re-test a stronger downstream tone shape because rendered continuation is focused on finish tone.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.06f,
                    0.02f,
                    0.12f,
                    0.24f));
            addContinuationExpansionCandidate(
                "toneFlatterEditing",
                "Flatter Editing Tone",
                "Re-test a gentler downstream tone shape because rendered continuation needs another finish-tone comparison.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.02f,
                    0.20f,
                    0.08f,
                    0.16f,
                    -0.10f,
                    -0.26f));
            addContinuationExpansionCandidate(
                "toneDarkerToe",
                "Darker Shadow Toe",
                "Re-test downstream shadow toe placement because rendered continuation needs another finish-tone comparison.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.05f,
                    -0.05f,
                    -0.20f,
                    0.04f,
                    0.06f,
                    0.14f));
        }

        if (wantsRawCleanup) {
            addContinuationExpansionCandidate(
                "preserveTexture",
                "Preserve Texture",
                "Re-test a texture-preserving cleanup/detail balance because rendered continuation is focused on RAW cleanup.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.01f,
                    -0.04f,
                    -0.05f,
                    0.05f,
                    0.04f,
                    0.12f));
        }
    }

    const std::string renderedRefineIntent =
        previousToneJson.value("autoCandidateRenderedRefineIntent", std::string());
    const std::string renderedRefineReason =
        previousToneJson.value("autoCandidateRenderedRefineReason", std::string());
    const bool renderedFeedbackIterationActive =
        previousToneJson.value("autoCandidateRenderedFeedbackPass", 0) > 0 ||
        previousToneJson.value("autoCandidateRenderedFeedbackApplied", false) ||
        !renderedRefineIntent.empty();
    auto addRenderedLocalCandidate = [&](std::string id,
                                         std::string label,
                                         std::string fallbackReason,
                                         EditorNodeGraph::DevelopAutoGuidance guidance) {
        if (HasDevelopAutoCandidateId(result.candidates, id)) {
            return;
        }
        const std::string reason = renderedRefineReason.empty()
            ? fallbackReason
            : (renderedRefineReason + " Generated a rendered-local candidate family from that mismatch.");
        addCandidate(std::move(id), std::move(label), reason, guidance);
    };
    if (renderedFeedbackIterationActive) {
        if (renderedRefineIntent == "brightenMids") {
            addRenderedLocalCandidate(
                "renderedLocalBrightenMids",
                "Rendered Local Brighten Mids",
                "Rendered local metrics asked for brighter important mids without broad highlight pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.11f,
                    0.09f,
                    0.12f,
                    0.06f,
                    0.00f,
                    -0.04f));
        } else if (renderedRefineIntent == "openShadows") {
            addRenderedLocalCandidate(
                "renderedLocalShadowOpening",
                "Rendered Local Shadow Opening",
                "Rendered local metrics asked for opening crowded local shadows while avoiding a global flat lift.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.04f,
                    0.13f,
                    0.20f,
                    0.08f,
                    -0.02f,
                    -0.05f));
        } else if (renderedRefineIntent == "protectHighlights") {
            addRenderedLocalCandidate(
                "renderedLocalHighlightRestraint",
                "Rendered Local Highlight Restraint",
                "Rendered local metrics asked for more highlight/local exposure restraint without claiming clipped-data recovery.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.10f,
                    0.19f,
                    0.04f,
                    0.30f,
                    -0.06f,
                    -0.09f));
        } else if (renderedRefineIntent == "addContrast") {
            addRenderedLocalCandidate(
                "renderedLocalContrastShape",
                "Rendered Local Contrast Shape",
                "Rendered local metrics asked for more regional separation without broad clipping pressure.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.00f,
                    -0.08f,
                    -0.06f,
                    0.02f,
                    0.07f,
                    0.19f));
        } else if (renderedRefineIntent == "cleanShadows") {
            addRenderedLocalCandidate(
                "renderedLocalCleanShadows",
                "Rendered Local Cleaner Shadows",
                "Rendered metrics asked for a cleaner shadow/detail balance instead of more shadow lift.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    0.01f,
                    -0.07f,
                    -0.13f,
                    0.10f,
                    -0.02f,
                    -0.07f));
        } else if (renderedRefineIntent == "preserveTexture") {
            addRenderedLocalCandidate(
                "renderedLocalPreserveTexture",
                "Rendered Local Preserve Texture",
                "Rendered metrics asked for a texture-preserving cleanup balance rather than a smoother result.",
                AdjustDevelopAutoCandidateGuidance(
                    modeGuidance,
                    -0.01f,
                    -0.04f,
                    -0.04f,
                    0.04f,
                    0.05f,
                    0.11f));
        }
    }

    if (renderedFeedbackIterationActive) {
        auto addPreviousCandidate = [&](const std::string& candidateId,
                                        const std::string& fallbackLabel,
                                        const std::string& carryReason) {
            if (HasDevelopAutoCandidateId(result.candidates, candidateId)) {
                return false;
            }

            DevelopAutoCandidateSolve previousCandidate;
            if (!TryReadDevelopAutoCandidateFromToneJson(previousToneJson, candidateId, previousCandidate)) {
                return false;
            }

            previousCandidate.label = previousCandidate.label.empty() ? fallbackLabel : previousCandidate.label;
            previousCandidate.guidance.intent = intent;
            previousCandidate.guidanceFingerprint =
                BuildDevelopAutoCandidateGuidanceFingerprint(
                    previousCandidate.id,
                    previousCandidate.guidance,
                    &subjectImportance);
            previousCandidate.reason = carryReason;
            if (ApplyDevelopContinuationCandidateBias(previousCandidate, continuationBias)) {
                ++result.continuationBiasAppliedCount;
            }
            result.candidates.push_back(std::move(previousCandidate));
            ++result.renderedFeedbackCarriedForwardCount;
            return true;
        };
        addPreviousCandidate(
            previousToneJson.value("autoCandidateSelectedId", std::string()),
            "Previous Auto Pick",
            "Preserved the prior authored candidate so rendered feedback can compare and converge over multiple passes.");
        addPreviousCandidate(
            previousToneJson.value("autoCandidateRenderedBestId", std::string()),
            "Previous Rendered Best",
            "Preserved the prior authored rendered-best candidate so rendered feedback can compare and converge over multiple passes.");
        addPreviousCandidate(
            previousToneJson.value("autoCandidateRenderedMergeFirstId", std::string()),
            "Previous Rendered Merge Source",
            "Preserved the prior authored candidate from a rendered pair-merge source so the next solve can combine actual rendered survivors.");
        addPreviousCandidate(
            previousToneJson.value("autoCandidateRenderedMergeSecondId", std::string()),
            "Previous Rendered Merge Source",
            "Preserved the prior authored candidate from a rendered pair-merge source so the next solve can combine actual rendered survivors.");
        for (const std::string& survivorId : CollectDevelopRenderedSurvivorCandidateIdsForCarryForward(previousToneJson, 4)) {
            addPreviousCandidate(
                survivorId,
                "Previous Rendered Survivor",
                "Preserved the prior authored candidate from an actual rendered survivor so candidate convergence can compare more than only the selected and rendered-best states.");
        }
    }
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        DevelopAutoCandidateSolve& candidate = result.candidates[i];
        if (candidate.rejected) {
            continue;
        }
        const bool preserveRenderedLocalCandidate =
            renderedFeedbackIterationActive &&
            IsRenderedLocalRefineCandidateId(candidate.id);
        const bool preserveCleanupProbeCandidate =
            IsDevelopCleanupProbeCandidateId(candidate.id);
        const bool preserveModeNeighborCandidate =
            IsDevelopModeNeighborCandidateId(candidate.id);
        const bool preserveFinishToneProbeCandidate =
            IsDevelopFinishToneProbeCandidateId(candidate.id);
        const bool preserveSpecularToleranceCandidate =
            candidate.id == "specularHighlightTolerance";
        const bool preserveNaturalContrastGuardCandidate =
            candidate.id == "naturalContrastGuard";
        const bool preserveLuminousHighlightAnchorCandidate =
            candidate.id == "luminousHighlightAnchor";
        const bool preserveWhiteBalanceProbeCandidate =
            candidate.whiteBalanceProbe;
        const bool preserveBroadHighlightGuardCandidate =
            candidate.id == "broadHighlightGuard";
        const bool preserveLocalRangeGuardCandidate =
            candidate.id == "localRangeGuard";
        const bool preserveHaloSafeLocalRangeCandidate =
            candidate.id == "haloSafeLocalRange";
        const bool preserveShadowReadabilityLiftCandidate =
            candidate.id == "shadowReadabilityLift";
        const bool preserveShadowNoiseFloorCandidate =
            candidate.id == "shadowNoiseFloor";
        const bool preserveSubjectIntentCandidate =
            IsDevelopSubjectIntentCandidateId(candidate.id);
        for (std::size_t j = 0; j < i; ++j) {
            const DevelopAutoCandidateSolve& survivor = result.candidates[j];
            if (survivor.rejected) {
                continue;
            }
            // A rendered-local family comes from measured output mismatch, so keep it
            // available even when it is near a generic parameter family.
            if (preserveRenderedLocalCandidate &&
                !IsRenderedLocalRefineCandidateId(survivor.id)) {
                continue;
            }
            // Cleanup/detail probes intentionally vary hidden RAW cleanup during
            // rendered candidate evaluation, so keep them even when visible
            // guidance is near a generic family.
            if (preserveCleanupProbeCandidate &&
                !IsDevelopCleanupProbeCandidateId(survivor.id)) {
                continue;
            }
            // Mode-neighbor probes describe adjacent intent vectors. Prefer
            // keeping that labeled probe over a generic candidate with a very
            // similar visible guidance delta.
            if (preserveModeNeighborCandidate &&
                !IsDevelopModeNeighborCandidateId(survivor.id)) {
                continue;
            }
            // Finish-tone probes validate downstream shoulder/toe/contrast choices
            // against the same pre-finish image, so do not drop them merely for
            // being near the generic base/scene-prep families.
            if (preserveFinishToneProbeCandidate &&
                !IsDevelopFinishToneProbeCandidateId(survivor.id)) {
                continue;
            }
            // Specular tolerance is a Guide 04 exception path: it can be close
            // to other finish-tone probes, but its image intent is specifically
            // "do not overprotect tiny glints."
            if (preserveSpecularToleranceCandidate &&
                survivor.id != "specularHighlightTolerance") {
                continue;
            }
            // Natural Contrast Guard answers Guide 04 brightness-hierarchy
            // damage, so keep it distinct from generic punchier tone probes.
            if (preserveNaturalContrastGuardCandidate &&
                survivor.id != "naturalContrastGuard") {
                continue;
            }
            // Luminous Highlight Anchor is a finish-tone brightness-feeling
            // probe for broad/protected highlights, so keep it distinct from
            // generic rolloff or contrast candidates.
            if (preserveLuminousHighlightAnchorCandidate &&
                survivor.id != "luminousHighlightAnchor") {
                continue;
            }
            // White-balance probes share most visible guidance with the base
            // solve but change RAW color interpretation, so they need to
            // survive clustering long enough for rendered comparison.
            if (preserveWhiteBalanceProbeCandidate &&
                !survivor.whiteBalanceProbe) {
                continue;
            }
            // Broad Highlight Guard is a scene-prep-local highlight candidate;
            // it can look close to raw/global highlight candidates numerically,
            // but the stage being tested is intentionally different.
            if (preserveBroadHighlightGuardCandidate &&
                survivor.id != "broadHighlightGuard") {
                continue;
            }
            // Local Range Guard is emitted from rendered regional evidence and
            // changes scene-prep/local range behavior even when its visible
            // guidance is close to a generic range/readability candidate.
            if (preserveLocalRangeGuardCandidate &&
                survivor.id != "localRangeGuard") {
                continue;
            }
            // Halo-Safe Local Range is the Guide 04 anti-halo Scene Prep
            // check. Keep it distinct from generic range/readability probes.
            if (preserveHaloSafeLocalRangeCandidate &&
                survivor.id != "haloSafeLocalRange") {
                continue;
            }
            // Shadow Readability Lift is the positive Scene Prep counterpart to
            // Shadow Noise Floor: render-test clean local opening separately
            // from generic midtone/range candidates.
            if (preserveShadowReadabilityLiftCandidate &&
                survivor.id != "shadowReadabilityLift") {
                continue;
            }
            // Shadow Noise Floor is a Guide 04 scene-prep candidate for
            // intentionally holding noisy dark regions down. Its visible
            // guidance can be close to mood/tone candidates, but the stage
            // reason is different enough to render-test separately.
            if (preserveShadowNoiseFloorCandidate &&
                survivor.id != "shadowNoiseFloor") {
                continue;
            }
            // Guide 05 subject-intent probes represent alternate user/scene
            // interpretations, so keep them through generic numeric clustering.
            if (preserveSubjectIntentCandidate &&
                !IsDevelopSubjectIntentCandidateId(survivor.id)) {
                continue;
            }
            if (DevelopAutoCandidateDistance(candidate.guidance, survivor.guidance) < 0.18f) {
                candidate.rejected = true;
                candidate.duplicate = true;
                candidate.rejectReason = "Clustered as a duplicate of " + survivor.label + ".";
                break;
            }
        }
    }
    std::vector<std::size_t> survivorIndices;
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        if (!result.candidates[i].rejected) {
            survivorIndices.push_back(i);
        }
    }
    if (survivorIndices.empty()) {
        result.candidates.front().rejected = false;
        result.candidates.front().rejectReason.clear();
        survivorIndices.push_back(0);
    }

    std::sort(survivorIndices.begin(), survivorIndices.end(), [&](std::size_t a, std::size_t b) {
        return result.candidates[a].score > result.candidates[b].score;
    });

    const DevelopAutoCandidateSolve* selected = &result.candidates[survivorIndices.front()];
    result.authoredGuidance = selected->guidance;
    result.selectedId = selected->id;
    result.selectedLabel = selected->label;
    result.selectedScore = selected->score;
    SetDevelopResultWhiteBalanceProbe(result, *selected);

    if (survivorIndices.size() >= 2) {
        const DevelopAutoCandidateSolve firstCandidate = result.candidates[survivorIndices[0]];
        const DevelopAutoCandidateSolve secondCandidate = result.candidates[survivorIndices[1]];
        const float scoreGap = std::fabs(firstCandidate.score - secondCandidate.score);
        const float distance = DevelopAutoCandidateDistance(firstCandidate.guidance, secondCandidate.guidance);
        if (stats.valid &&
            !firstCandidate.whiteBalanceProbe &&
            !secondCandidate.whiteBalanceProbe &&
            scoreGap <= 0.07f &&
            distance >= 0.24f &&
            firstCandidate.score > 0.48f &&
            secondCandidate.score > 0.46f) {
            const float firstWeight = std::clamp(
                0.50f + (firstCandidate.score - secondCandidate.score) * 1.35f,
                0.42f,
                0.68f);
            DevelopAutoCandidateSolve merged;
            merged.id = "mergedAutoPick";
            merged.label = "Merged Auto Pick";
            merged.reason = "Merged the two strongest compatible solver candidates in authored settings space.";
            merged.guidance = BlendDevelopAutoCandidateGuidance(
                firstCandidate.guidance,
                secondCandidate.guidance,
                firstWeight);
            merged.score = std::min(1.0f, std::max(firstCandidate.score, secondCandidate.score) + 0.025f);
            merged.scoreComponents = BuildDevelopAutoCandidateScoreComponents(
                merged,
                modeGuidance,
                intent,
                stats,
                regionEvidence,
                dynamicRangeStrategy,
                subjectSceneIntent,
                darkness,
                shadowRescueNeed,
                hdrNeed,
                flatSceneNeed,
                underBrightBroadHighlightEv);
            result.candidates.push_back(merged);
            result.authoredGuidance = merged.guidance;
            result.selectedId = merged.id;
            result.selectedLabel = merged.label;
            result.selectedScore = merged.score;
            ClearDevelopResultWhiteBalanceProbe(result);
            result.selectionSource = "mergedParameterScore";
            result.mergeApplied = true;
            result.mergeFirstId = firstCandidate.id;
            result.mergeSecondId = secondCandidate.id;
            result.mergeThirdId.clear();
            result.mergeFirstWeight = firstWeight;
            result.mergeSecondWeight = 1.0f - firstWeight;
            result.mergeThirdWeight = 0.0f;
        }
    }

    const std::uint64_t preliminaryFingerprint = BuildDevelopAutoCandidateFingerprint(result, stats);
    ApplyRenderedCandidateFeedbackToSolve(result, previousToneJson, preliminaryFingerprint);
    result.fingerprint = BuildDevelopAutoCandidateFingerprint(result, stats);
    const std::uint64_t previousFingerprint =
        previousToneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
    const std::string previousSelectedId =
        previousToneJson.value("autoCandidateSelectedId", std::string());
    const int previousPass = previousToneJson.value("autoCandidateConvergencePass", 0);
    result.converged =
        stats.valid &&
        previousFingerprint == result.fingerprint &&
        previousSelectedId == result.selectedId &&
        previousPass > 0;
    result.convergencePass = result.converged
        ? previousPass
        : std::min(previousPass + 1, 6);
    return result;
}

} // namespace Stack::Editor::DevelopCandidateGeneration
