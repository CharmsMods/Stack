#include "Editor/EditorModule.h"
#include "Editor/Internal/EditorModuleDevelopCandidateRenderPayload.h"
#include "Editor/Internal/EditorModuleDevelopCandidateShared.h"

#include "Editor/Layers/ToneLayers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <imgui.h>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

using Stack::Editor::DevelopCandidate::DevelopExpectedDirtyBoundaryForCandidateStage;
using Stack::Editor::DevelopCandidate::DevelopObservedDirtyBoundaryFromCacheHits;
using Stack::Editor::DevelopCandidate::DevelopRenderedRevisionStageForCandidateId;
using Stack::Editor::DevelopCandidate::DevelopStageCacheValidation;
using Stack::Editor::DevelopCandidate::EvaluateDevelopStageCacheValidation;
using Stack::Editor::DevelopCandidate::ApplyDevelopGuidanceToCandidateRenderPayload;
using Stack::Editor::DevelopCandidate::IsFinishToneProbeCandidateIdForRenderRequest;
using Stack::Editor::DevelopCandidate::IsSubjectIntentProbeCandidateIdForRenderRequest;
using Stack::Editor::DevelopCandidate::IsWhiteBalanceProbeCandidateIdForRenderRequest;
using Stack::Editor::DevelopCandidate::ReadDevelopAuthoredGuidanceFromToneJson;
using Stack::Editor::DevelopCandidate::TryResolveWhiteBalanceProbeCandidateModeForRenderRequest;

namespace {

constexpr const char* kDevelopAdaptiveRenderBudgetVersion = "AdaptiveRenderBudgetV1";
constexpr std::size_t kDefaultDevelopCandidateRenderRequestsPerNode = 4;
constexpr std::size_t kMaxDevelopCandidateRenderRequestsPerNode = 6;
constexpr std::size_t kMaxDevelopCandidateRenderRequestsTotal = 20;
constexpr int kDevelopCandidateMetricReadbackMidMaxDimension = 1800;
constexpr int kDevelopCandidateMetricReadbackLargeMaxDimension = 1536;
constexpr int kDevelopCandidateMetricReadbackVeryLargeMaxDimension = 1280;
constexpr double kDevelopCandidateFeedbackQuietSeconds = 0.60;
constexpr std::size_t kDevelopSubjectMetricMaxRegions = 32;
constexpr std::size_t kDevelopSubjectMetricMaxStrokes = 32;
constexpr std::size_t kDevelopSubjectMetricMaxStrokePoints = 64;

struct DevelopCandidateRenderBudgetLimits {
    std::size_t defaultPerNode = kDefaultDevelopCandidateRenderRequestsPerNode;
    std::size_t maxPerNode = kMaxDevelopCandidateRenderRequestsPerNode;
    std::size_t maxTotal = kMaxDevelopCandidateRenderRequestsTotal;
    std::string reason = "default";
};

DevelopCandidateRenderBudgetLimits ResolveDevelopCandidateRenderBudgetLimits(int sourceWidth, int sourceHeight) {
    DevelopCandidateRenderBudgetLimits limits;
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return limits;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(sourceWidth) * static_cast<std::uint64_t>(sourceHeight);
    if (pixelCount >= 50000000ull) {
        // Full-resolution rendered feedback is diagnostic, not the user-facing output.
        // On very large RAWs, probe fewer candidates so interaction stays recoverable.
        limits.defaultPerNode = 2;
        limits.maxPerNode = 3;
        limits.maxTotal = 6;
        limits.reason = "veryLargeSourceMemoryGuard";
    } else if (pixelCount >= 30000000ull) {
        limits.defaultPerNode = 3;
        limits.maxPerNode = 4;
        limits.maxTotal = 10;
        limits.reason = "largeSourceMemoryGuard";
    } else if (pixelCount >= 16000000ull) {
        limits.defaultPerNode = kDefaultDevelopCandidateRenderRequestsPerNode;
        limits.maxPerNode = kDefaultDevelopCandidateRenderRequestsPerNode;
        limits.maxTotal = 12;
        limits.reason = "midSizeSourceExpansionGuard";
    }
    return limits;
}

int ResolveDevelopCandidateMetricReadbackMaxDimension(int sourceWidth, int sourceHeight) {
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return 0;
    }

    const std::uint64_t pixelCount =
        static_cast<std::uint64_t>(sourceWidth) * static_cast<std::uint64_t>(sourceHeight);
    if (pixelCount >= 50000000ull) {
        // Rendered feedback metrics are diagnostic. Keep very large RAW readbacks
        // representative without allocating a full-size RGBA buffer per candidate stage.
        return kDevelopCandidateMetricReadbackVeryLargeMaxDimension;
    }
    if (pixelCount >= 30000000ull) {
        return kDevelopCandidateMetricReadbackLargeMaxDimension;
    }
    if (pixelCount >= 16000000ull) {
        return kDevelopCandidateMetricReadbackMidMaxDimension;
    }
    return 0;
}

double DevelopCandidateFeedbackQuietRemainingSeconds(double lastInteractionTime, double now) {
    if (lastInteractionTime < 0.0 || now < lastInteractionTime) {
        return 0.0;
    }
    return std::max(0.0, kDevelopCandidateFeedbackQuietSeconds - (now - lastInteractionTime));
}

bool ShouldDeferDevelopCandidateRenderRequest(double lastInteractionTime, double now) {
    return DevelopCandidateFeedbackQuietRemainingSeconds(lastInteractionTime, now) > 0.0;
}

bool CanScheduleDevelopCandidateRenderRequest(
    std::size_t totalRequestCount,
    std::size_t nodeRequestCount,
    std::size_t nodeRequestBudget = kDefaultDevelopCandidateRenderRequestsPerNode,
    std::size_t totalRequestBudget = kMaxDevelopCandidateRenderRequestsTotal,
    std::size_t maxNodeRequestBudget = kMaxDevelopCandidateRenderRequestsPerNode) {
    const std::size_t clampedNodeBudget = std::clamp(
        nodeRequestBudget,
        static_cast<std::size_t>(1),
        std::max<std::size_t>(1, maxNodeRequestBudget));
    return totalRequestCount < std::max<std::size_t>(1, totalRequestBudget) &&
           nodeRequestCount < clampedNodeBudget;
}

EditorRenderWorker::DevelopSubjectMetricSampling BuildDevelopSubjectMetricSampling(
    const EditorNodeGraph::DevelopSubjectImportanceMap& importance) {
    EditorRenderWorker::DevelopSubjectMetricSampling sampling;
    if (!importance.enabled) {
        return sampling;
    }

    sampling.enabled = true;
    sampling.regions.reserve(std::min(importance.regions.size(), kDevelopSubjectMetricMaxRegions));
    for (const EditorNodeGraph::DevelopSubjectImportanceRegion& region : importance.regions) {
        if (sampling.regions.size() >= kDevelopSubjectMetricMaxRegions) {
            break;
        }
        if (!region.enabled || region.strength <= 0.001f) {
            continue;
        }

        EditorRenderWorker::DevelopSubjectMetricRegion metricRegion;
        metricRegion.id = region.id;
        metricRegion.mode = static_cast<int>(region.mode);
        metricRegion.enabled = true;
        metricRegion.lowPriority =
            region.mode == EditorNodeGraph::DevelopSubjectImportanceMode::Ignore;
        metricRegion.centerX = std::clamp(region.centerX, 0.0f, 1.0f);
        metricRegion.centerY = std::clamp(region.centerY, 0.0f, 1.0f);
        metricRegion.radiusX = std::clamp(region.radiusX, 0.005f, 1.0f);
        metricRegion.radiusY = std::clamp(region.radiusY, 0.005f, 1.0f);
        metricRegion.feather = std::clamp(region.feather, 0.0f, 1.0f);
        metricRegion.strength = std::clamp(region.strength, 0.0f, 1.0f);
        sampling.regions.push_back(metricRegion);
    }

    sampling.strokes.reserve(std::min(importance.strokes.size(), kDevelopSubjectMetricMaxStrokes));
    for (const EditorNodeGraph::DevelopSubjectImportanceStroke& stroke : importance.strokes) {
        if (sampling.strokes.size() >= kDevelopSubjectMetricMaxStrokes) {
            break;
        }
        if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
            continue;
        }

        EditorRenderWorker::DevelopSubjectMetricStroke metricStroke;
        metricStroke.id = stroke.id;
        metricStroke.mode = static_cast<int>(stroke.mode);
        metricStroke.enabled = true;
        metricStroke.lowPriority =
            stroke.subtract ||
            stroke.mode == EditorNodeGraph::DevelopSubjectImportanceMode::Ignore;
        metricStroke.radius = std::clamp(stroke.radius, 0.002f, 0.50f);
        metricStroke.feather = std::clamp(stroke.feather, 0.0f, 1.0f);
        metricStroke.strength = std::clamp(stroke.strength, 0.0f, 1.0f);

        const std::size_t sourcePointCount = stroke.points.size();
        const std::size_t step = sourcePointCount > kDevelopSubjectMetricMaxStrokePoints
            ? static_cast<std::size_t>(std::ceil(
                static_cast<double>(sourcePointCount) /
                static_cast<double>(kDevelopSubjectMetricMaxStrokePoints)))
            : 1u;
        metricStroke.points.reserve(std::min(sourcePointCount, kDevelopSubjectMetricMaxStrokePoints));
        for (std::size_t index = 0; index < sourcePointCount; index += step) {
            const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& point = stroke.points[index];
            metricStroke.points.push_back({
                std::clamp(point.x, 0.0f, 1.0f),
                std::clamp(point.y, 0.0f, 1.0f)
            });
            if (metricStroke.points.size() >= kDevelopSubjectMetricMaxStrokePoints) {
                break;
            }
        }
        const EditorNodeGraph::DevelopSubjectImportanceStrokePoint& lastPoint = stroke.points.back();
        if (!metricStroke.points.empty()) {
            const EditorRenderWorker::DevelopSubjectMetricPoint lastMetricPoint {
                std::clamp(lastPoint.x, 0.0f, 1.0f),
                std::clamp(lastPoint.y, 0.0f, 1.0f)
            };
            const EditorRenderWorker::DevelopSubjectMetricPoint& currentBack = metricStroke.points.back();
            if ((std::fabs(currentBack.x - lastMetricPoint.x) > 0.0001f ||
                 std::fabs(currentBack.y - lastMetricPoint.y) > 0.0001f) &&
                metricStroke.points.size() < kDevelopSubjectMetricMaxStrokePoints) {
                metricStroke.points.push_back(lastMetricPoint);
            }
        }
        if (metricStroke.points.empty()) {
            continue;
        }

        float minX = 1.0f;
        float minY = 1.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (const EditorRenderWorker::DevelopSubjectMetricPoint& point : metricStroke.points) {
            minX = std::min(minX, point.x);
            minY = std::min(minY, point.y);
            maxX = std::max(maxX, point.x);
            maxY = std::max(maxY, point.y);
        }
        const float support =
            metricStroke.radius * (1.0f + std::clamp(metricStroke.feather, 0.0f, 1.0f) * 0.85f);
        metricStroke.minX = std::clamp(minX - support, 0.0f, 1.0f);
        metricStroke.minY = std::clamp(minY - support, 0.0f, 1.0f);
        metricStroke.maxX = std::clamp(maxX + support, 0.0f, 1.0f);
        metricStroke.maxY = std::clamp(maxY + support, 0.0f, 1.0f);
        sampling.strokes.push_back(std::move(metricStroke));
    }

    sampling.enabled = !sampling.regions.empty() || !sampling.strokes.empty();
    return sampling;
}

struct DevelopAdaptiveRenderBudgetPolicy {
    std::size_t budget = kDefaultDevelopCandidateRenderRequestsPerNode;
    std::string reason = "default";
    std::string continuationDecision;
    std::string convergenceState;
    std::string convergenceDecision;
    std::string convergenceReason;
    bool expanded = false;
    bool narrowed = false;
};

bool IsActionableDevelopRevisionStage(const std::string& stage) {
    return stage == "rawBase" ||
           stage == "rawGlobal" ||
           stage == "rawCleanup" ||
           stage == "scenePrep" ||
           stage == "finishTone" ||
           stage == "multiStage";
}

DevelopAdaptiveRenderBudgetPolicy ResolveDevelopAdaptiveRenderBudgetPolicy(
    const nlohmann::json& toneJson,
    std::uint64_t solveFingerprint,
    std::uint64_t renderedFingerprint,
    std::size_t candidateCount,
    const std::string& activeRevisionStage,
    const std::string& activeRefineIntent) {
    DevelopAdaptiveRenderBudgetPolicy policy;
    if (!toneJson.is_object()) {
        return policy;
    }

    const nlohmann::json continuation =
        toneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    if (continuation.is_object()) {
        policy.continuationDecision = continuation.value("decision", std::string());
    }
    const nlohmann::json convergenceEvidence =
        toneJson.value("autoCandidateConvergenceEvidence", nlohmann::json::object());
    if (convergenceEvidence.is_object()) {
        policy.convergenceState = convergenceEvidence.value("state", std::string());
        policy.convergenceDecision = convergenceEvidence.value("decision", std::string());
        policy.convergenceReason = convergenceEvidence.value("reason", std::string());
    }

    const bool currentSolveNeedsRenderedMetrics =
        solveFingerprint != 0 && renderedFingerprint != solveFingerprint;
    const std::string continuationReason =
        continuation.is_object()
            ? continuation.value("reason", std::string())
            : std::string();
    const int continuationPass =
        continuation.is_object() ? continuation.value("pass", 0) : 0;
    const bool continuationWantsRender =
        continuation.is_object() &&
        (continuation.value("nextStep", std::string()) == "renderUpdatedSolve" ||
         continuation.value("nextStep", std::string()) == "renderCandidates" ||
         continuation.value("requiresRenderedMetrics", false));
    const bool meaningfulContinuation =
        currentSolveNeedsRenderedMetrics &&
        policy.continuationDecision == "continue" &&
        continuationWantsRender &&
        continuationPass > 0;
    const bool stageFocused =
        IsActionableDevelopRevisionStage(activeRevisionStage);
    const bool refineFocused =
        !activeRefineIntent.empty();
    const bool mergeOrAdoptionValidation =
        continuationReason == "merged" ||
        continuationReason == "refined" ||
        continuationReason == "adopted" ||
        continuationReason == "renderedBestImproved";
    const int convergencePass =
        convergenceEvidence.is_object()
            ? convergenceEvidence.value("pass", continuationPass)
            : continuationPass;
    const bool convergenceEvidenceAwaitingMetrics =
        policy.convergenceState == "awaitingRenderedMetrics" &&
        policy.convergenceDecision == "waitForRenderedMetrics";
    const bool admissionAlreadyTightened =
        toneJson.value("autoCandidateConvergenceAdmissionTightened", false);
    const bool focusedLateConvergence =
        currentSolveNeedsRenderedMetrics &&
        convergenceEvidenceAwaitingMetrics &&
        continuationPass > 0 &&
        (admissionAlreadyTightened || convergencePass >= 2);

    if (meaningfulContinuation && candidateCount > kDefaultDevelopCandidateRenderRequestsPerNode) {
        policy.budget = std::min(
            kMaxDevelopCandidateRenderRequestsPerNode,
            kDefaultDevelopCandidateRenderRequestsPerNode + static_cast<std::size_t>(1));
        policy.reason = "validateContinuation";
        policy.expanded = true;
    }
    if (meaningfulContinuation &&
        candidateCount > policy.budget &&
        (refineFocused || stageFocused || mergeOrAdoptionValidation)) {
        policy.budget = kMaxDevelopCandidateRenderRequestsPerNode;
        policy.reason = refineFocused
            ? "validateActiveRefineIntent"
            : (stageFocused ? "validateResponsibleStage" : "validateRenderedMerge");
        policy.expanded = true;
    }
    if (!policy.expanded &&
        focusedLateConvergence &&
        candidateCount > static_cast<std::size_t>(3) &&
        !stageFocused &&
        !refineFocused &&
        !mergeOrAdoptionValidation) {
        // Late continuation without a named repair target should validate the best few
        // rendered alternatives instead of broadening the search again.
        policy.budget = static_cast<std::size_t>(3);
        policy.reason = "convergenceEvidenceFocusedValidation";
        policy.narrowed = true;
    }
    if (!currentSolveNeedsRenderedMetrics && policy.continuationDecision == "stop" &&
        policy.reason == "default") {
        policy.reason = "continuationStopped";
    } else if (currentSolveNeedsRenderedMetrics &&
        policy.continuationDecision == "waitForRenderedMetrics" &&
        policy.reason == "default") {
        policy.reason = "initialRenderedMetrics";
    } else if (currentSolveNeedsRenderedMetrics && policy.reason == "default") {
        policy.reason = "currentSolveNeedsRenderedMetrics";
    }
    return policy;
}

void CollectRenderGraphUpstreamNodeIds(
    const RenderGraphSnapshot& graph,
    int nodeId,
    std::unordered_set<int>& visited) {
    if (nodeId <= 0 || !visited.insert(nodeId).second) {
        return;
    }
    for (const RenderGraphLink& link : graph.links) {
        if (link.toNodeId == nodeId) {
            CollectRenderGraphUpstreamNodeIds(graph, link.fromNodeId, visited);
        }
    }
}

bool ReadDevelopGuidanceFromCandidateJson(
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

float DevelopCandidateRenderGuidanceDistance(
    const EditorNodeGraph::DevelopAutoGuidance& a,
    const EditorNodeGraph::DevelopAutoGuidance& b) {
    return
        std::fabs(a.exposureBias - b.exposureBias) * 0.70f +
        std::fabs(a.dynamicRange - b.dynamicRange) * 0.35f +
        std::fabs(a.shadowLift - b.shadowLift) * 0.40f +
        std::fabs(a.highlightGuard - b.highlightGuard) * 0.40f +
        std::fabs(a.highlightCharacter - b.highlightCharacter) * 0.20f +
        std::fabs(a.contrastBias - b.contrastBias) * 0.45f +
        std::fabs(a.subjectSceneBias - b.subjectSceneBias) * 0.18f +
        std::fabs(a.moodReadabilityBias - b.moodReadabilityBias) * 0.18f +
        std::fabs(a.autoStrength - b.autoStrength) * 0.20f;
}

bool IsRenderedLocalCandidateIdForRenderRequest(const std::string& candidateId) {
    return
        candidateId == "renderedLocalBrightenMids" ||
        candidateId == "renderedLocalShadowOpening" ||
        candidateId == "renderedLocalHighlightRestraint" ||
        candidateId == "renderedLocalContrastShape" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture";
}

bool IsCleanupProbeCandidateIdForRenderRequest(const std::string& candidateId) {
    return
        candidateId == "cleanShadows" ||
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalCleanShadows" ||
        candidateId == "renderedLocalPreserveTexture";
}

bool IsModeNeighborCandidateIdForRenderRequest(const std::string& candidateId) {
    return candidateId.rfind("modeNeighbor", 0) == 0;
}

bool IsExposurePlacementCandidateIdForRenderRequest(const std::string& candidateId) {
    return candidateId == "highlightProtectedMids";
}

bool TryReadWhiteBalanceModeForRenderRequest(
    const std::string& value,
    Raw::WhiteBalanceMode& outMode) {
    if (value == "As Shot" || value == "AsShot") {
        outMode = Raw::WhiteBalanceMode::AsShot;
        return true;
    }
    if (value == "Auto") {
        outMode = Raw::WhiteBalanceMode::Auto;
        return true;
    }
    if (value == "Neutral") {
        outMode = Raw::WhiteBalanceMode::Neutral;
        return true;
    }
    if (value == "Manual") {
        outMode = Raw::WhiteBalanceMode::Manual;
        return true;
    }
    return false;
}

bool TryReadCandidateWhiteBalanceOverrideForRenderRequest(
    const nlohmann::json& candidate,
    Raw::WhiteBalanceMode& outMode) {
    if (!candidate.is_object()) {
        return false;
    }
    const nlohmann::json rawOverrides =
        candidate.value("rawOverrides", nlohmann::json::object());
    if (!rawOverrides.is_object()) {
        return false;
    }
    return TryReadWhiteBalanceModeForRenderRequest(
        rawOverrides.value("whiteBalanceMode", std::string()),
        outMode);
}

float DevelopCandidateRenderRevisionStageBonus(
    const std::string& candidateId,
    const std::string& revisionStage) {
    if (revisionStage == "rawGlobal") {
        if (candidateId == "protectHighlights" ||
            candidateId == "highlightProtectedMids" ||
            candidateId == "renderedLocalHighlightRestraint") {
            return 0.10f;
        }
        if (candidateId == "maximumRange") {
            return 0.04f;
        }
        if (IsWhiteBalanceProbeCandidateIdForRenderRequest(candidateId)) {
            return 0.08f;
        }
    } else if (revisionStage == "scenePrep") {
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
            return 0.10f;
        }
        if (candidateId == "highlightProtectedMids") {
            return 0.04f;
        }
    } else if (revisionStage == "finishTone") {
        if (IsFinishToneProbeCandidateIdForRenderRequest(candidateId) ||
            candidateId.rfind("modeNeighbor", 0) == 0) {
            return 0.09f;
        }
    } else if (revisionStage == "rawCleanup") {
        if (IsCleanupProbeCandidateIdForRenderRequest(candidateId)) {
            return 0.11f;
        }
    } else if (revisionStage == "multiStage") {
        if (IsRenderedLocalCandidateIdForRenderRequest(candidateId) ||
            IsCleanupProbeCandidateIdForRenderRequest(candidateId) ||
            IsExposurePlacementCandidateIdForRenderRequest(candidateId) ||
            IsWhiteBalanceProbeCandidateIdForRenderRequest(candidateId) ||
            candidateId.rfind("modeNeighbor", 0) == 0) {
            return 0.035f;
        }
    }
    return 0.0f;
}

bool IsDevelopCandidateRelevantToRevisionStage(
    const std::string& candidateId,
    const std::string& revisionStage) {
    if (revisionStage.empty() || revisionStage == "none" || revisionStage == "converged") {
        return false;
    }
    const std::string candidateStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    return
        candidateStage == revisionStage ||
        DevelopCandidateRenderRevisionStageBonus(candidateId, revisionStage) > 0.0f ||
        (revisionStage == "multiStage" && candidateStage == "multiStage");
}

bool IsDevelopCandidateRelevantToRenderedRefineIntent(
    const std::string& candidateId,
    const std::string& refineIntent) {
    if (refineIntent.empty()) {
        return false;
    }
    if (refineIntent == "brightenMids") {
        return candidateId == "renderedLocalBrightenMids" ||
            candidateId == "brighterMids" ||
            candidateId == "subjectReadableMids";
    }
    if (refineIntent == "openShadows") {
        return candidateId == "renderedLocalShadowOpening" ||
            candidateId == "brighterMids" ||
            candidateId == "maximumRange" ||
            candidateId == "haloSafeLocalRange" ||
            candidateId == "localRangeGuard" ||
            candidateId == "shadowReadabilityLift" ||
            candidateId == "shadowNoiseFloor" ||
            IsSubjectIntentProbeCandidateIdForRenderRequest(candidateId);
    }
    if (refineIntent == "protectHighlights") {
        return
            candidateId == "renderedLocalHighlightRestraint" ||
            candidateId == "protectHighlights" ||
            candidateId == "highlightProtectedMids" ||
            candidateId == "broadHighlightGuard" ||
            candidateId == "haloSafeLocalRange" ||
            candidateId == "localRangeGuard" ||
            candidateId == "brightHighlightRolloff" ||
            candidateId == "luminousHighlightAnchor" ||
            candidateId == "naturalContrastGuard" ||
            candidateId == "specularHighlightTolerance" ||
            candidateId == "maximumRange";
    }
    if (refineIntent == "addContrast") {
        return
            candidateId == "renderedLocalContrastShape" ||
            candidateId == "strongerContrast" ||
            candidateId == "naturalContrastGuard" ||
            candidateId == "luminousHighlightAnchor" ||
            candidateId == "tonePunchierShape" ||
            candidateId == "toneFlatterEditing" ||
            candidateId == "toneDarkerToe" ||
            candidateId.rfind("modeNeighbor", 0) == 0;
    }
    if (refineIntent == "cleanShadows") {
        return candidateId == "renderedLocalCleanShadows" || candidateId == "cleanShadows" || candidateId == "shadowNoiseFloor";
    }
    if (refineIntent == "preserveTexture") {
        return candidateId == "renderedLocalPreserveTexture" || candidateId == "preserveTexture";
    }
    return false;
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

} // namespace

RenderGraphRawDevelopPayload EditorModule::BuildDevelopCandidateRenderPayloadForValidation(
    RenderGraphRawDevelopPayload payload,
    const EditorNodeGraph::DevelopAutoGuidance& currentGuidance,
    const EditorNodeGraph::DevelopAutoGuidance& candidateGuidance,
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent) {
    ApplyDevelopGuidanceToCandidateRenderPayload(
        payload,
        currentGuidance,
        candidateGuidance,
        candidateId,
        intent);
    return payload;
}

bool EditorModule::IsDevelopCandidateRelevantToRevisionStageForValidation(
    const std::string& candidateId,
    const std::string& revisionStage) {
    return IsDevelopCandidateRelevantToRevisionStage(candidateId, revisionStage);
}

bool EditorModule::IsDevelopCandidateRelevantToRenderedRefineIntentForValidation(
    const std::string& candidateId,
    const std::string& refineIntent) {
    return IsDevelopCandidateRelevantToRenderedRefineIntent(candidateId, refineIntent);
}

std::string EditorModule::ClassifyDevelopCandidateStageCacheForValidation(
    const std::string& candidateRevisionStage,
    bool rawBaseCacheHit,
    bool preFinishCacheHit,
    bool& outExpectationMet,
    std::string& outExpectedBoundary,
    std::string& outValidationStatus) {
    const DevelopStageCacheValidation validation =
        EvaluateDevelopStageCacheValidation(
            candidateRevisionStage,
            rawBaseCacheHit,
            preFinishCacheHit);
    outExpectationMet = validation.evaluated ? validation.met : true;
    outExpectedBoundary = validation.expectedBoundary;
    outValidationStatus = validation.status;
    return DevelopObservedDirtyBoundaryFromCacheHits(rawBaseCacheHit, preFinishCacheHit);
}

int EditorModule::ClassifyDevelopCandidateStageScheduleForValidation(
    const std::string& candidateRevisionStage,
    bool selectedCandidate,
    std::string& outExpectedDirtyBoundary,
    std::string& outReason) {
    outExpectedDirtyBoundary =
        DevelopExpectedDirtyBoundaryForCandidateStage(candidateRevisionStage);
    outReason =
        DevelopCandidateStageSchedulerReason(candidateRevisionStage, selectedCandidate);
    return DevelopCandidateStageScheduleRank(candidateRevisionStage, selectedCandidate);
}

bool EditorModule::CanScheduleDevelopCandidateRenderRequestForValidation(
    std::size_t totalRequestCount,
    std::size_t nodeRequestCount,
    std::size_t nodeRequestBudget) {
    return CanScheduleDevelopCandidateRenderRequest(
        totalRequestCount,
        nodeRequestCount,
        nodeRequestBudget);
}

int EditorModule::ResolveDevelopCandidateMetricReadbackMaxDimensionForValidation(
    int sourceWidth,
    int sourceHeight) {
    return ResolveDevelopCandidateMetricReadbackMaxDimension(sourceWidth, sourceHeight);
}

std::size_t EditorModule::ResolveDevelopAdaptiveRenderBudgetForValidation(
    const nlohmann::json& toneJson,
    std::uint64_t solveFingerprint,
    std::uint64_t renderedFingerprint,
    std::size_t candidateCount,
    const std::string& activeRevisionStage,
    const std::string& activeRefineIntent,
    std::string& outReason,
    bool& outExpanded,
    bool* outNarrowed) {
    const DevelopAdaptiveRenderBudgetPolicy policy =
        ResolveDevelopAdaptiveRenderBudgetPolicy(
            toneJson,
            solveFingerprint,
            renderedFingerprint,
            candidateCount,
            activeRevisionStage,
            activeRefineIntent);
    outReason = policy.reason;
    outExpanded = policy.expanded;
    if (outNarrowed) {
        *outNarrowed = policy.narrowed;
    }
    return policy.budget;
}

double EditorModule::DevelopCandidateFeedbackQuietSecondsForValidation() {
    return kDevelopCandidateFeedbackQuietSeconds;
}

double EditorModule::DevelopCandidateFeedbackQuietRemainingSecondsForValidation(
    double lastInteractionTime,
    double now) {
    return DevelopCandidateFeedbackQuietRemainingSeconds(lastInteractionTime, now);
}

EditorModule::DevelopCandidateFeedbackGateDecision EditorModule::ClassifyDevelopCandidateFeedbackGateForValidation(
    std::uint64_t resultInteractionSerial,
    std::uint64_t currentInteractionSerial,
    double lastInteractionTime,
    double now) {
    if (resultInteractionSerial != currentInteractionSerial) {
        return DevelopCandidateFeedbackGateDecision::DropStaleInteraction;
    }
    if (ShouldDeferDevelopCandidateRenderRequest(lastInteractionTime, now)) {
        return DevelopCandidateFeedbackGateDecision::DeferRecentInteraction;
    }
    return DevelopCandidateFeedbackGateDecision::Apply;
}

bool EditorModule::ShouldDeferDevelopCandidateRenderRequestForValidation(
    double lastInteractionTime,
    double now) {
    return ShouldDeferDevelopCandidateRenderRequest(lastInteractionTime, now);
}

bool EditorModule::IsRecentRawDevelopInteractionForNode(
    int nodeId,
    double now,
    double windowSeconds) const {
    const auto it = m_RawDevelopInteractionTimes.find(nodeId);
    if (it == m_RawDevelopInteractionTimes.end() || it->second < 0.0) {
        return false;
    }
    return now >= it->second && now - it->second < windowSeconds;
}

void EditorModule::RecordRawDevelopInteraction(int nodeId) {
    if (nodeId <= 0 || ImGui::GetCurrentContext() == nullptr) {
        return;
    }
    const double now = ImGui::GetTime();
    m_LastRawDevelopInteractionTime = now;
    m_RawDevelopInteractionTimes[nodeId] = now;
    m_RawDevelopInteractionSerials[nodeId] = ++m_RawDevelopInteractionSerialCounter;
    m_DeferredDevelopCandidateFeedbackTimes.erase(nodeId);
}

std::uint64_t EditorModule::GetRawDevelopInteractionSerial(int nodeId) const {
    const auto it = m_RawDevelopInteractionSerials.find(nodeId);
    return it != m_RawDevelopInteractionSerials.end() ? it->second : 0;
}

void EditorModule::ScheduleDeferredDevelopCandidateFeedback(int nodeId, double now) {
    if (nodeId <= 0) {
        return;
    }
    m_DeferredDevelopCandidateFeedbackTimes[nodeId] = now;
}

void EditorModule::RefreshDeferredDevelopCandidateFeedbackIfReady(double now) {
    if (m_DeferredDevelopCandidateFeedbackTimes.empty()) {
        return;
    }

    std::vector<int> readyNodeIds;
    readyNodeIds.reserve(m_DeferredDevelopCandidateFeedbackTimes.size());
    for (const auto& entry : m_DeferredDevelopCandidateFeedbackTimes) {
        if (!IsRecentRawDevelopInteractionForNode(
                entry.first,
                now,
                kDevelopCandidateFeedbackQuietSeconds)) {
            readyNodeIds.push_back(entry.first);
        }
    }
    for (int nodeId : readyNodeIds) {
        m_DeferredDevelopCandidateFeedbackTimes.erase(nodeId);
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node ||
            node->kind != EditorNodeGraph::NodeKind::RawDevelop ||
            node->rawDevelop.uiMode != EditorNodeGraph::RawDevelopUiMode::Auto) {
            continue;
        }
        MarkRenderDirty(nodeId);
    }
}

bool EditorModule::GetDevelopCandidateFeedbackDeferredStatus(
    int nodeId,
    double now,
    double& outRemainingSeconds) const {
    outRemainingSeconds = 0.0;
    if (nodeId <= 0 || m_DeferredDevelopCandidateFeedbackTimes.find(nodeId) == m_DeferredDevelopCandidateFeedbackTimes.end()) {
        return false;
    }

    const auto interactionIt = m_RawDevelopInteractionTimes.find(nodeId);
    if (interactionIt != m_RawDevelopInteractionTimes.end()) {
        outRemainingSeconds =
            DevelopCandidateFeedbackQuietRemainingSeconds(interactionIt->second, now);
    }
    return true;
}

std::vector<EditorRenderWorker::DevelopCandidateRenderRequest> EditorModule::BuildDevelopCandidateRenderRequests(
    const RenderGraphSnapshot& graph,
    int sourceWidth,
    int sourceHeight) {
    std::vector<EditorRenderWorker::DevelopCandidateRenderRequest> requests;
    if (graph.outputNodeId <= 0) {
        return requests;
    }

    const DevelopCandidateRenderBudgetLimits budgetLimits =
        ResolveDevelopCandidateRenderBudgetLimits(sourceWidth, sourceHeight);
    const int metricReadbackMaxDimension =
        ResolveDevelopCandidateMetricReadbackMaxDimension(sourceWidth, sourceHeight);
    const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : -1.0;
    std::unordered_set<int> activePathNodeIds;
    CollectRenderGraphUpstreamNodeIds(graph, graph.outputNodeId, activePathNodeIds);
    for (const RenderGraphNode& renderNode : graph.nodes) {
        if (requests.size() >= budgetLimits.maxTotal) {
            break;
        }
        if (renderNode.kind != RenderGraphNodeKind::RawDevelop ||
            renderNode.nodeId <= 0 ||
            activePathNodeIds.find(renderNode.nodeId) == activePathNodeIds.end()) {
            continue;
        }

        const EditorNodeGraph::Node* editorNode = m_NodeGraph.FindNode(renderNode.nodeId);
        if (!editorNode ||
            editorNode->kind != EditorNodeGraph::NodeKind::RawDevelop ||
            editorNode->rawDevelop.uiMode != EditorNodeGraph::RawDevelopUiMode::Auto) {
            continue;
        }

        const nlohmann::json& toneJson = editorNode->rawDevelop.integratedToneLayerJson;
        if (!toneJson.is_object()) {
            continue;
        }
        const std::uint64_t solveFingerprint =
            toneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
        if (solveFingerprint == 0) {
            continue;
        }
        const std::uint64_t renderedFingerprint =
            toneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0));
        if (renderedFingerprint == solveFingerprint &&
            toneJson.value("autoCandidateRenderMetricsStatus", std::string()) == "ready") {
            continue;
        }

        const nlohmann::json candidates = toneJson.value("autoCandidateSolves", nlohmann::json::array());
        if (!candidates.is_array() || candidates.empty()) {
            continue;
        }
        const auto interactionTimeIt = m_RawDevelopInteractionTimes.find(renderNode.nodeId);
        const double lastInteractionTime =
            interactionTimeIt != m_RawDevelopInteractionTimes.end()
                ? interactionTimeIt->second
                : -1.0;
        if (ShouldDeferDevelopCandidateRenderRequest(lastInteractionTime, now)) {
            // The main viewport render should still update while the user drags,
            // but candidate feedback would be gated anyway. Wait and schedule one
            // fresh feedback pass after the authored Develop state settles.
            ScheduleDeferredDevelopCandidateFeedback(renderNode.nodeId, now);
            continue;
        }

        EditorNodeGraph::DevelopAutoGuidance currentGuidance =
            ReadDevelopAuthoredGuidanceFromToneJson(toneJson, editorNode->rawDevelop.autoGuidance);
        currentGuidance.intent = editorNode->rawDevelop.autoGuidance.intent;
        const std::string activeRevisionStage =
            toneJson.value("autoCandidateRenderedRevisionStage", std::string());
        const std::string activeRefineIntent =
            toneJson.value("autoCandidateRenderedRefineIntent", std::string());

        struct CandidateRenderOption {
            const nlohmann::json* json = nullptr;
            EditorNodeGraph::DevelopAutoGuidance guidance;
            std::uint64_t guidanceFingerprint = 0;
            float score = 0.0f;
            float revisionStageBonus = 0.0f;
            std::string revisionStage;
            std::string expectedDirtyBoundary;
            std::string stageSchedulerReason;
            int stageSchedulerRank = 0;
            bool selectedCandidate = false;
            bool activeStageMatch = false;
            bool stageReservedRequest = false;
            bool activeRefineIntentMatch = false;
            bool refineIntentReservedRequest = false;
            bool renderedLocal = false;
            bool cleanupProbe = false;
            bool modeNeighbor = false;
            bool exposurePlacement = false;
            bool finishToneProbe = false;
            bool subjectIntentProbe = false;
            bool whiteBalanceProbe = false;
            Raw::WhiteBalanceMode whiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
        };

        std::vector<CandidateRenderOption> selectedCandidates;
        std::vector<CandidateRenderOption> survivorCandidates;
        for (const nlohmann::json& candidate : candidates) {
            if (!candidate.is_object()) {
                continue;
            }
            CandidateRenderOption option;
            option.json = &candidate;
            option.guidance = currentGuidance;
            const std::string candidateId = candidate.value("id", std::string());
            if (!ReadDevelopGuidanceFromCandidateJson(candidate.value("guidance", nlohmann::json::object()), option.guidance)) {
                continue;
            }
            option.guidance.intent = editorNode->rawDevelop.autoGuidance.intent;
            option.score = candidate.value("score", 0.0f);
            option.guidanceFingerprint =
                candidate.value("guidanceFingerprint", static_cast<std::uint64_t>(0));
            option.renderedLocal = IsRenderedLocalCandidateIdForRenderRequest(candidateId);
            option.cleanupProbe = IsCleanupProbeCandidateIdForRenderRequest(candidateId);
            option.modeNeighbor = IsModeNeighborCandidateIdForRenderRequest(candidateId);
            option.exposurePlacement = IsExposurePlacementCandidateIdForRenderRequest(candidateId);
            option.finishToneProbe = IsFinishToneProbeCandidateIdForRenderRequest(candidateId);
            option.subjectIntentProbe =
                IsSubjectIntentProbeCandidateIdForRenderRequest(candidateId);
            Raw::WhiteBalanceMode candidateWhiteBalanceMode = Raw::WhiteBalanceMode::AsShot;
            option.whiteBalanceProbe =
                TryResolveWhiteBalanceProbeCandidateModeForRenderRequest(
                    candidateId,
                    candidateWhiteBalanceMode) ||
                TryReadCandidateWhiteBalanceOverrideForRenderRequest(
                    candidate,
                    candidateWhiteBalanceMode);
            option.whiteBalanceMode = candidateWhiteBalanceMode;
            option.revisionStageBonus =
                DevelopCandidateRenderRevisionStageBonus(
                    candidateId,
                    activeRevisionStage);
            option.revisionStage =
                option.whiteBalanceProbe ? "rawGlobal" : DevelopRenderedRevisionStageForCandidateId(candidateId);
            option.expectedDirtyBoundary =
                DevelopExpectedDirtyBoundaryForCandidateStage(option.revisionStage);
            option.activeStageMatch =
                IsDevelopCandidateRelevantToRevisionStage(candidateId, activeRevisionStage);
            option.activeRefineIntentMatch =
                IsDevelopCandidateRelevantToRenderedRefineIntent(candidateId, activeRefineIntent);

            const std::string status = candidate.value("status", std::string());
            if (status == "selected") {
                option.selectedCandidate = true;
                option.stageSchedulerRank =
                    DevelopCandidateStageScheduleRank(option.revisionStage, true);
                option.stageSchedulerReason =
                    DevelopCandidateStageSchedulerReason(option.revisionStage, true);
                selectedCandidates.push_back(std::move(option));
            } else if (status == "survivor") {
                option.stageSchedulerRank =
                    DevelopCandidateStageScheduleRank(option.revisionStage, false);
                option.stageSchedulerReason =
                    DevelopCandidateStageSchedulerReason(option.revisionStage, false);
                survivorCandidates.push_back(std::move(option));
            }
        }
        std::sort(survivorCandidates.begin(), survivorCandidates.end(), [](const CandidateRenderOption& a, const CandidateRenderOption& b) {
            return a.score > b.score;
        });
        DevelopAdaptiveRenderBudgetPolicy adaptiveBudgetPolicy =
            ResolveDevelopAdaptiveRenderBudgetPolicy(
                toneJson,
                solveFingerprint,
                renderedFingerprint,
                selectedCandidates.size() + survivorCandidates.size(),
                activeRevisionStage,
                activeRefineIntent);
        std::size_t nodeRenderBudget = std::min(adaptiveBudgetPolicy.budget, budgetLimits.maxPerNode);
        if (nodeRenderBudget < adaptiveBudgetPolicy.budget) {
            adaptiveBudgetPolicy.reason = budgetLimits.reason;
            adaptiveBudgetPolicy.narrowed = true;
        } else if (budgetLimits.maxTotal < kMaxDevelopCandidateRenderRequestsTotal &&
            adaptiveBudgetPolicy.reason == "default") {
            adaptiveBudgetPolicy.reason = budgetLimits.reason;
        }

        std::vector<CandidateRenderOption> renderCandidates;
        std::vector<EditorNodeGraph::DevelopAutoGuidance> includedGuidance;
        for (const CandidateRenderOption& candidate : selectedCandidates) {
            if (renderCandidates.size() >= nodeRenderBudget) {
                break;
            }
            const std::string id = candidate.json ? candidate.json->value("id", std::string()) : std::string();
            const bool alreadyIncluded = std::any_of(
                renderCandidates.begin(),
                renderCandidates.end(),
                [&id](const CandidateRenderOption& existing) {
                    return existing.json && existing.json->value("id", std::string()) == id;
                });
            if (!alreadyIncluded) {
                renderCandidates.push_back(candidate);
                includedGuidance.push_back(candidate.guidance);
            }
        }

        auto renderCandidateId = [](const CandidateRenderOption& candidate) {
            return candidate.json ? candidate.json->value("id", std::string()) : std::string();
        };
        auto alreadyIncludedCandidateId = [&](const std::string& id) {
            return std::any_of(
                renderCandidates.begin(),
                renderCandidates.end(),
                [&id, &renderCandidateId](const CandidateRenderOption& existing) {
                    return renderCandidateId(existing) == id;
                });
        };
        auto renderSetAlreadyCoversActiveStage = [&]() {
            return std::any_of(
                renderCandidates.begin(),
                renderCandidates.end(),
                [](const CandidateRenderOption& candidate) {
                    return candidate.activeStageMatch;
                });
        };
        auto renderSetAlreadyCoversActiveRefineIntent = [&]() {
            return activeRefineIntent.empty() ||
                std::any_of(
                    renderCandidates.begin(),
                    renderCandidates.end(),
                    [](const CandidateRenderOption& candidate) {
                    return candidate.activeRefineIntentMatch;
                });
        };

        if (renderCandidates.size() < nodeRenderBudget &&
            !renderSetAlreadyCoversActiveRefineIntent()) {
            int bestRefineIndex = -1;
            float bestRefinePriority = -1.0f;
            for (std::size_t index = 0; index < survivorCandidates.size(); ++index) {
                const CandidateRenderOption& candidate = survivorCandidates[index];
                if (!candidate.activeRefineIntentMatch) {
                    continue;
                }
                const std::string id = renderCandidateId(candidate);
                if (alreadyIncludedCandidateId(id)) {
                    continue;
                }
                const float priority =
                    candidate.score +
                    candidate.revisionStageBonus +
                    (candidate.renderedLocal ? 0.12f : 0.0f) +
                    (candidate.cleanupProbe ? 0.05f : 0.0f) +
                    (candidate.finishToneProbe ? 0.05f : 0.0f) +
                    (candidate.subjectIntentProbe ? 0.055f : 0.0f) +
                    (candidate.whiteBalanceProbe ? 0.055f : 0.0f) +
                    (candidate.exposurePlacement ? 0.05f : 0.0f);
                if (priority > bestRefinePriority) {
                    bestRefinePriority = priority;
                    bestRefineIndex = static_cast<int>(index);
                }
            }
            if (bestRefineIndex >= 0) {
                CandidateRenderOption selected = survivorCandidates[static_cast<std::size_t>(bestRefineIndex)];
                selected.refineIntentReservedRequest = true;
                renderCandidates.push_back(selected);
                includedGuidance.push_back(selected.guidance);
                survivorCandidates.erase(survivorCandidates.begin() + bestRefineIndex);
            }
        }

        if (renderCandidates.size() < nodeRenderBudget &&
            !renderSetAlreadyCoversActiveStage()) {
            int bestStageIndex = -1;
            float bestStagePriority = -1.0f;
            for (std::size_t index = 0; index < survivorCandidates.size(); ++index) {
                const CandidateRenderOption& candidate = survivorCandidates[index];
                if (!candidate.activeStageMatch) {
                    continue;
                }
                const std::string id = renderCandidateId(candidate);
                if (alreadyIncludedCandidateId(id)) {
                    continue;
                }
                const float priority =
                    candidate.score +
                    candidate.revisionStageBonus +
                    (candidate.renderedLocal ? 0.08f : 0.0f) +
                    (candidate.cleanupProbe ? 0.06f : 0.0f) +
                    (candidate.modeNeighbor ? 0.05f : 0.0f) +
                    (candidate.finishToneProbe ? 0.05f : 0.0f) +
                    (candidate.subjectIntentProbe ? 0.055f : 0.0f) +
                    (candidate.whiteBalanceProbe ? 0.065f : 0.0f) +
                    (candidate.exposurePlacement ? 0.07f : 0.0f);
                if (priority > bestStagePriority) {
                    bestStagePriority = priority;
                    bestStageIndex = static_cast<int>(index);
                }
            }
            if (bestStageIndex >= 0) {
                CandidateRenderOption selected = survivorCandidates[static_cast<std::size_t>(bestStageIndex)];
                selected.stageReservedRequest = true;
                renderCandidates.push_back(selected);
                includedGuidance.push_back(selected.guidance);
                survivorCandidates.erase(survivorCandidates.begin() + bestStageIndex);
            }
        }

        while (renderCandidates.size() < nodeRenderBudget && !survivorCandidates.empty()) {
            int bestIndex = -1;
            float bestPriority = -1.0f;
            bool foundDiverseCandidate = false;
            for (std::size_t index = 0; index < survivorCandidates.size(); ++index) {
                const CandidateRenderOption& candidate = survivorCandidates[index];
                const std::string id = renderCandidateId(candidate);
                if (alreadyIncludedCandidateId(id)) {
                    continue;
                }

                float minDistance = includedGuidance.empty() ? 1.0f : std::numeric_limits<float>::infinity();
                for (const EditorNodeGraph::DevelopAutoGuidance& existingGuidance : includedGuidance) {
                    minDistance = std::min(
                        minDistance,
                        DevelopCandidateRenderGuidanceDistance(candidate.guidance, existingGuidance));
                }
                const bool diverseEnough =
                    minDistance >= 0.18f ||
                    candidate.renderedLocal ||
                    candidate.cleanupProbe ||
                    candidate.modeNeighbor ||
                    candidate.finishToneProbe ||
                    candidate.subjectIntentProbe ||
                    candidate.whiteBalanceProbe ||
                    candidate.exposurePlacement;
                if (!diverseEnough && foundDiverseCandidate) {
                    continue;
                }
                if (diverseEnough && !foundDiverseCandidate) {
                    foundDiverseCandidate = true;
                    bestIndex = -1;
                    bestPriority = -1.0f;
                }

                const float diversityBonus = std::clamp(minDistance / 0.70f, 0.0f, 1.0f) * 0.12f;
                const float measuredMismatchBonus = candidate.renderedLocal ? 0.08f : 0.0f;
                const float cleanupProbeBonus = candidate.cleanupProbe ? 0.06f : 0.0f;
                const float modeNeighborBonus = candidate.modeNeighbor ? 0.05f : 0.0f;
                const float exposurePlacementBonus = candidate.exposurePlacement ? 0.07f : 0.0f;
                const float finishToneProbeBonus = candidate.finishToneProbe ? 0.055f : 0.0f;
                const float subjectIntentProbeBonus = candidate.subjectIntentProbe ? 0.060f : 0.0f;
                const float whiteBalanceProbeBonus = candidate.whiteBalanceProbe ? 0.060f : 0.0f;
                const float priority =
                    candidate.score +
                    diversityBonus +
                    measuredMismatchBonus +
                    cleanupProbeBonus +
                    modeNeighborBonus +
                    finishToneProbeBonus +
                    subjectIntentProbeBonus +
                    whiteBalanceProbeBonus +
                    exposurePlacementBonus +
                    candidate.revisionStageBonus;
                if (priority > bestPriority) {
                    bestPriority = priority;
                    bestIndex = static_cast<int>(index);
                }
            }
            if (bestIndex < 0) {
                break;
            }
            CandidateRenderOption selected = survivorCandidates[static_cast<std::size_t>(bestIndex)];
            renderCandidates.push_back(selected);
            includedGuidance.push_back(selected.guidance);
            survivorCandidates.erase(survivorCandidates.begin() + bestIndex);
        }

        auto firstScheduledCandidate = std::find_if(
            renderCandidates.begin(),
            renderCandidates.end(),
            [](const CandidateRenderOption& candidate) {
                return !candidate.selectedCandidate;
            });
        std::stable_sort(
            firstScheduledCandidate,
            renderCandidates.end(),
            [&](const CandidateRenderOption& a, const CandidateRenderOption& b) {
                if (a.stageSchedulerRank != b.stageSchedulerRank) {
                    return a.stageSchedulerRank < b.stageSchedulerRank;
                }
                if (a.stageReservedRequest != b.stageReservedRequest) {
                    return a.stageReservedRequest;
                }
                if (a.refineIntentReservedRequest != b.refineIntentReservedRequest) {
                    return a.refineIntentReservedRequest;
                }
                if (a.activeStageMatch != b.activeStageMatch) {
                    return a.activeStageMatch;
                }
                if (a.activeRefineIntentMatch != b.activeRefineIntentMatch) {
                    return a.activeRefineIntentMatch;
                }
                if (std::fabs(a.score - b.score) > 0.0001f) {
                    return a.score > b.score;
                }
                return renderCandidateId(a) < renderCandidateId(b);
            });

        int stageSchedulerOrder = 0;
        std::size_t nodeRequestCount = 0;
        for (const CandidateRenderOption& renderCandidate : renderCandidates) {
            if (!CanScheduleDevelopCandidateRenderRequest(
                    requests.size(),
                    nodeRequestCount,
                    nodeRenderBudget,
                    budgetLimits.maxTotal,
                    budgetLimits.maxPerNode)) {
                break;
            }
            const nlohmann::json* candidate = renderCandidate.json;
            if (!candidate) {
                continue;
            }

            EditorNodeGraph::DevelopAutoGuidance candidateGuidance = renderCandidate.guidance;
            candidateGuidance.intent = editorNode->rawDevelop.autoGuidance.intent;

            EditorRenderWorker::DevelopCandidateRenderRequest request;
            request.developNodeId = renderNode.nodeId;
            request.candidateId = candidate->value("id", std::string("candidate"));
            request.candidateLabel = candidate->value("label", request.candidateId);
            request.candidateRevisionStage = renderCandidate.revisionStage;
            request.activeRevisionStage = activeRevisionStage;
            request.activeRefineIntent = activeRefineIntent;
            request.stageSchedulerExpectedDirtyBoundary = renderCandidate.expectedDirtyBoundary;
            request.stageSchedulerReason = renderCandidate.stageSchedulerReason;
            request.stageSchedulerOrder = stageSchedulerOrder++;
            request.stageSchedulerRank = renderCandidate.stageSchedulerRank;
            request.activeStageMatch = renderCandidate.activeStageMatch;
            request.stageReservedRequest = renderCandidate.stageReservedRequest;
            request.activeRefineIntentMatch = renderCandidate.activeRefineIntentMatch;
            request.refineIntentReservedRequest = renderCandidate.refineIntentReservedRequest;
            request.rawDevelop = renderNode.rawDevelop;
            request.dirtyGeneration = renderNode.requestRevision;
            request.solveFingerprint = solveFingerprint;
            request.rawDevelopInteractionSerial =
                GetRawDevelopInteractionSerial(renderNode.nodeId);
            request.guidanceFingerprint = renderCandidate.guidanceFingerprint;
            request.solveScore = candidate->value("score", 0.0f);
            request.subjectSampling =
                BuildDevelopSubjectMetricSampling(editorNode->rawDevelop.subjectImportance);
            request.adaptiveRenderBudget =
                static_cast<int>(std::min<std::size_t>(
                    nodeRenderBudget,
                    budgetLimits.maxPerNode));
            request.adaptiveRenderBudgetVersion = kDevelopAdaptiveRenderBudgetVersion;
            request.adaptiveRenderBudgetReason = adaptiveBudgetPolicy.reason;
            request.adaptiveRenderBudgetContinuationDecision =
                adaptiveBudgetPolicy.continuationDecision;
            request.adaptiveRenderBudgetExpanded = adaptiveBudgetPolicy.expanded;
            request.adaptiveRenderBudgetNarrowed = adaptiveBudgetPolicy.narrowed;
            request.adaptiveRenderBudgetConvergenceState =
                adaptiveBudgetPolicy.convergenceState;
            request.adaptiveRenderBudgetConvergenceDecision =
                adaptiveBudgetPolicy.convergenceDecision;
            request.adaptiveRenderBudgetConvergenceReason =
                adaptiveBudgetPolicy.convergenceReason;
            request.metricReadbackMaxDimension = metricReadbackMaxDimension;
            const Raw::WhiteBalanceMode* whiteBalanceOverride =
                renderCandidate.whiteBalanceProbe
                    ? &renderCandidate.whiteBalanceMode
                    : nullptr;
            ApplyDevelopGuidanceToCandidateRenderPayload(
                request.rawDevelop,
                currentGuidance,
                candidateGuidance,
                request.candidateId,
                editorNode->rawDevelop.autoGuidance.intent,
                whiteBalanceOverride);
            requests.push_back(std::move(request));
            ++nodeRequestCount;
        }
    }
    return requests;
}

