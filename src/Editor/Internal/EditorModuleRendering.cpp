#include "Editor/EditorModule.h"

#include "Editor/Layers/ToneLayers.h"
#include "Renderer/GLHelpers.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <imgui.h>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr int kScalableGeneratorBaseRaster = 1024;
constexpr int kScalableGeneratorMaxRaster = 4096;
constexpr const char* kDevelopAdaptiveRenderBudgetVersion = "AdaptiveRenderBudgetV1";
constexpr const char* kDevelopLocalExposureStrategyVersion = "LocalExposureStrategyV1";
constexpr std::size_t kDefaultDevelopCandidateRenderRequestsPerNode = 4;
constexpr std::size_t kMaxDevelopCandidateRenderRequestsPerNode = 6;
constexpr std::size_t kMaxDevelopCandidateRenderRequestsTotal = 20;
constexpr int kDevelopCandidateMetricReadbackMidMaxDimension = 1800;
constexpr int kDevelopCandidateMetricReadbackLargeMaxDimension = 1536;
constexpr int kDevelopCandidateMetricReadbackVeryLargeMaxDimension = 1280;
constexpr double kDevelopCandidateFeedbackQuietSeconds = 0.60;
constexpr double kPreviewLikeRefreshQuietSeconds = 0.18;
constexpr std::size_t kDevelopSubjectMetricMaxRegions = 32;
constexpr std::size_t kDevelopSubjectMetricMaxStrokes = 32;

double MillisecondsBetween(
    const std::chrono::steady_clock::time_point& start,
    const std::chrono::steady_clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
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

std::size_t HashJsonValue(const nlohmann::json& value) {
    return std::hash<std::string>{}(value.dump());
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

struct CroppedRgbaImage {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

bool PollSharedTextureFence(GLsync& fence, bool& failed) {
    failed = false;
    if (!fence) {
        return true;
    }
    const GLenum waitResult = glClientWaitSync(fence, 0, 0);
    if (waitResult == GL_ALREADY_SIGNALED || waitResult == GL_CONDITION_SATISFIED) {
        glDeleteSync(fence);
        fence = nullptr;
        return true;
    }
    if (waitResult == GL_TIMEOUT_EXPIRED) {
        return false;
    }
    glDeleteSync(fence);
    fence = nullptr;
    failed = true;
    return false;
}

bool IsMaskOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::MaskGenerator ||
           kind == EditorNodeGraph::NodeKind::MaskCombine ||
           kind == EditorNodeGraph::NodeKind::MaskUtility ||
           kind == EditorNodeGraph::NodeKind::CustomMask ||
           kind == EditorNodeGraph::NodeKind::ImageToMask;
}

bool IsImageOutputNode(EditorNodeGraph::NodeKind kind) {
    return kind == EditorNodeGraph::NodeKind::Image ||
           kind == EditorNodeGraph::NodeKind::RawDevelop ||
           kind == EditorNodeGraph::NodeKind::RawDetailFusion ||
           kind == EditorNodeGraph::NodeKind::HdrMerge ||
           kind == EditorNodeGraph::NodeKind::Layer ||
           kind == EditorNodeGraph::NodeKind::ImageGenerator ||
           kind == EditorNodeGraph::NodeKind::Mix ||
           kind == EditorNodeGraph::NodeKind::DataMath ||
           kind == EditorNodeGraph::NodeKind::ChannelCombine ||
           kind == EditorNodeGraph::NodeKind::Output;
}

void ResolveRawDisplayDimensions(const Raw::RawMetadata& metadata, int& width, int& height) {
    width = Raw::DisplayWidth(metadata);
    height = Raw::DisplayHeight(metadata);
}

CroppedRgbaImage CropToAlphaBounds(const std::vector<unsigned char>& rgbaPixels, int width, int height, int padding = 0) {
    CroppedRgbaImage result;
    if (rgbaPixels.empty() || width <= 0 || height <= 0) {
        return result;
    }

    int minX = width;
    int minY = height;
    int maxX = -1;
    int maxY = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const size_t alphaIndex = static_cast<size_t>((y * width + x) * 4 + 3);
            if (alphaIndex >= rgbaPixels.size() || rgbaPixels[alphaIndex] == 0) {
                continue;
            }
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY) {
        result.pixels = rgbaPixels;
        result.width = width;
        result.height = height;
        return result;
    }

    minX = std::max(0, minX - std::max(0, padding));
    minY = std::max(0, minY - std::max(0, padding));
    maxX = std::min(width - 1, maxX + std::max(0, padding));
    maxY = std::min(height - 1, maxY + std::max(0, padding));

    result.width = std::max(1, maxX - minX + 1);
    result.height = std::max(1, maxY - minY + 1);
    result.pixels.assign(static_cast<size_t>(result.width * result.height * 4), 0);
    for (int y = 0; y < result.height; ++y) {
        for (int x = 0; x < result.width; ++x) {
            const size_t srcIndex = static_cast<size_t>(((minY + y) * width + (minX + x)) * 4);
            const size_t dstIndex = static_cast<size_t>((y * result.width + x) * 4);
            if (srcIndex + 3 >= rgbaPixels.size() || dstIndex + 3 >= result.pixels.size()) {
                continue;
            }
            result.pixels[dstIndex + 0] = rgbaPixels[srcIndex + 0];
            result.pixels[dstIndex + 1] = rgbaPixels[srcIndex + 1];
            result.pixels[dstIndex + 2] = rgbaPixels[srcIndex + 2];
            result.pixels[dstIndex + 3] = rgbaPixels[srcIndex + 3];
        }
    }
    return result;
}

std::vector<unsigned char> BuildTransparentPixels(int width, int height) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    return std::vector<unsigned char>(static_cast<size_t>(width * height * 4), 0);
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

struct DevelopStageCacheValidation {
    bool evaluated = false;
    bool met = true;
    bool expectedRawBaseReuse = false;
    bool expectedPreFinishReuse = false;
    std::string expectedBoundary = "none";
    std::string status = "notRequired";
    std::string reason;
};

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

void ClampCandidateMosaicDenoiseSettings(Raw::RawMosaicDenoiseSettings& settings) {
    settings.hotPixelThreshold = std::clamp(settings.hotPixelThreshold, 0.005f, 0.50f);
    settings.lumaStrength = std::clamp(settings.lumaStrength, 0.0f, 1.0f);
    settings.chromaStrength = std::clamp(settings.chromaStrength, 0.0f, 1.0f);
    settings.radius = std::clamp(settings.radius, 1, 4);
    settings.edgeProtection = std::clamp(settings.edgeProtection, 0.0f, 1.0f);
    settings.iterations = std::clamp(settings.iterations, 1, 2);
}

bool WouldReverseRecentRenderedAdoption(
    const nlohmann::json& toneJson,
    const std::string& selectedCandidateId,
    const std::string& bestCandidateId) {
    const nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array() || selectedCandidateId.empty() || bestCandidateId.empty()) {
        return false;
    }

    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        const std::string action = it->value("action", std::string());
        if (action != "adopted" && action != "merged" && action != "refined" && action != "solveRequested") {
            continue;
        }
        const std::string previousSelected = it->value("selectedId", std::string());
        const std::string previousBest = it->value("bestId", std::string());
        return previousSelected == bestCandidateId && previousBest == selectedCandidateId;
    }
    return false;
}

std::string RepeatedRenderedRefinementStopReason(
    const nlohmann::json& toneJson,
    const std::string& refineIntent,
    float selectedRenderScore,
    bool selectedRenderScoreValid) {
    if (refineIntent.empty()) {
        return {};
    }
    if (toneJson.value("autoCandidateRenderedFeedbackAction", std::string()) != "refined" ||
        toneJson.value("autoCandidateRenderedFeedbackRefineIntent", std::string()) != refineIntent) {
        return {};
    }

    if (!selectedRenderScoreValid) {
        return toneJson.value("autoCandidateRenderedFeedbackPass", 0) > 0
            ? "renderedRefineRepeatedIntent"
            : std::string();
    }

    const float previousSelectedScore =
        toneJson.value("autoCandidateRenderedFeedbackPreviousSelectedScore", -1.0f);
    if (previousSelectedScore >= 0.0f && selectedRenderScore < previousSelectedScore + 0.025f) {
        return "renderedRefineDidNotImprove";
    }
    if (toneJson.value("autoCandidateRenderedFeedbackPass", 0) >= 2) {
        return "renderedRefineRepeatedIntent";
    }
    return {};
}

nlohmann::json DevelopCandidateRenderMetricsToJson(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics) {
    return {
        { "meanLuma", metrics.meanLuma },
        { "medianLuma", metrics.medianLuma },
        { "p10Luma", metrics.p10Luma },
        { "p90Luma", metrics.p90Luma },
        { "shadowFraction", metrics.shadowFraction },
        { "highlightFraction", metrics.highlightFraction },
        { "clippedFraction", metrics.clippedFraction },
        { "contrastSpan", metrics.contrastSpan },
        { "meanRed", metrics.meanRed },
        { "meanGreen", metrics.meanGreen },
        { "meanBlue", metrics.meanBlue },
        { "warmCoolBias", metrics.warmCoolBias },
        { "magentaGreenBias", metrics.magentaGreenBias },
        { "channelImbalance", metrics.channelImbalance },
        { "colorCastRisk", metrics.colorCastRisk },
        { "meanSaturation", metrics.meanSaturation },
        { "lowSaturationFraction", metrics.lowSaturationFraction },
        { "highlightBandFraction", metrics.highlightBandFraction },
        { "highlightMeanLuma", metrics.highlightMeanLuma },
        { "highlightLowSaturationFraction", metrics.highlightLowSaturationFraction },
        { "highlightGrayRisk", metrics.highlightGrayRisk },
        { "highlightTileCoverage", metrics.highlightTileCoverage },
        { "highlightStructureScore", metrics.highlightStructureScore },
        { "meaningfulHighlightPressure", metrics.meaningfulHighlightPressure },
        { "edgeContrast", metrics.edgeContrast },
        { "haloRiskFraction", metrics.haloRiskFraction },
        { "shadowTextureRisk", metrics.shadowTextureRisk },
        { "localMeanLuma3x3", metrics.localMeanLuma },
        { "localContrastSpan3x3", metrics.localContrastSpan },
        { "localDamageRiskScore3x3", metrics.localDamageRiskScore },
        { "localLumaSpread", metrics.localLumaSpread },
        { "localEvSpreadStops", metrics.localEvSpreadStops },
        { "localEvConflict", metrics.localEvConflict },
        { "localContrastPeak", metrics.localContrastPeak },
        { "localShadowPressure", metrics.localShadowPressure },
        { "localHighlightPressure", metrics.localHighlightPressure },
        { "localDamageRiskMean", metrics.localDamageRiskMean },
        { "localDamageRiskPeak", metrics.localDamageRiskPeak },
        { "localDamageRiskPeakTile", metrics.localDamageRiskPeakTile },
        { "localExposureHighlightCrowding", metrics.localExposureHighlightCrowding },
        { "localExposureShadowCrowding", metrics.localExposureShadowCrowding },
        { "localExposureHaloStress", metrics.localExposureHaloStress },
        { "localExposureFlatnessRisk", metrics.localExposureFlatnessRisk },
        { "localExposureDamageRisk", metrics.localExposureDamageRisk },
        { "subjectCenterPrior", metrics.subjectCenterPrior },
        { "subjectReadabilityPressure", metrics.subjectReadabilityPressure },
        { "subjectProtectionPressure", metrics.subjectProtectionPressure },
        { "subjectMoodPreservationPressure", metrics.subjectMoodPreservationPressure },
        { "subjectImportanceConfidence", metrics.subjectImportanceConfidence },
        { "centerMeanLuma", metrics.centerMeanLuma },
        { "centerShadowFraction", metrics.centerShadowFraction },
        { "centerHighlightFraction", metrics.centerHighlightFraction },
        { "subjectMarkedSampleCount", metrics.subjectMarkedSampleCount },
        { "subjectMarkedCoverage", metrics.subjectMarkedCoverage },
        { "subjectMarkedPositiveCoverage", metrics.subjectMarkedPositiveCoverage },
        { "subjectMarkedRevealCoverage", metrics.subjectMarkedRevealCoverage },
        { "subjectMarkedProtectCoverage", metrics.subjectMarkedProtectCoverage },
        { "subjectMarkedMoodCoverage", metrics.subjectMarkedMoodCoverage },
        { "subjectMarkedLowPriorityCoverage", metrics.subjectMarkedLowPriorityCoverage },
        { "subjectMarkedMeanLuma", metrics.subjectMarkedMeanLuma },
        { "subjectMarkedShadowFraction", metrics.subjectMarkedShadowFraction },
        { "subjectMarkedHighlightFraction", metrics.subjectMarkedHighlightFraction },
        { "subjectMarkedClippedFraction", metrics.subjectMarkedClippedFraction },
        { "subjectMarkedContrastSpan", metrics.subjectMarkedContrastSpan },
        { "subjectMarkedReadabilityScore", metrics.subjectMarkedReadabilityScore },
        { "subjectMarkedProtectionRisk", metrics.subjectMarkedProtectionRisk },
        { "subjectMarkedMoodPreservationScore", metrics.subjectMarkedMoodPreservationScore },
        { "subjectMarkedLowPriorityMeanLuma", metrics.subjectMarkedLowPriorityMeanLuma },
        { "subjectMarkedLowPriorityBrightFraction", metrics.subjectMarkedLowPriorityBrightFraction },
        { "subjectMarkedLowPriorityPressure", metrics.subjectMarkedLowPriorityPressure }
    };
}

constexpr float kDevelopRenderedDuplicateDistance = 0.085f;
constexpr float kDevelopRenderedPreFinishDistinctDistance = 0.085f;
constexpr int kDevelopRenderedFeedbackMaxPasses = 3;
constexpr const char* kDevelopRenderedFeedbackLoopVersion = "RenderedFeedbackLoopV1";
constexpr const char* kDevelopRenderedContinuationVersion = "RenderedContinuationV1";

nlohmann::json BuildDevelopRenderedContinuationPolicyRecord(
    const std::string& decision,
    const std::string& reason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    int pass,
    int nextPass,
    const std::string& stageFocus,
    const std::string& stageReason,
    float improvement,
    const std::string& stageBoundarySignal,
    const std::string& relativeStatus,
    int successCount,
    int failureCount) {
    const int clampedPass =
        std::clamp(pass, 0, kDevelopRenderedFeedbackMaxPasses);
    const int clampedNextPass =
        std::clamp(nextPass, clampedPass, kDevelopRenderedFeedbackMaxPasses);
    nlohmann::json policy = {
        { "version", kDevelopRenderedContinuationVersion },
        { "decision", decision },
        { "reason", reason },
        { "nextStep", nextStep },
        { "requiresAutoSolve", requiresAutoSolve },
        { "requiresRenderedMetrics", requiresRenderedMetrics },
        { "shouldContinue", decision == "continue" },
        { "bounded", true },
        { "pass", clampedPass },
        { "nextPass", clampedNextPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "remainingPasses", std::max(0, kDevelopRenderedFeedbackMaxPasses - clampedNextPass) },
        { "stageFocus", stageFocus },
        { "stageReason", stageReason },
        { "evidence", {
            { "improvement", improvement },
            { "stageBoundarySignal", stageBoundarySignal },
            { "relativeStatus", relativeStatus },
            { "successCount", successCount },
            { "failureCount", failureCount }
        } }
    };
    return policy;
}

struct DevelopRenderedDuplicateDecision {
    bool duplicate = false;
    bool preFinishDistinct = false;
    float finalDistance = -1.0f;
    float preFinishDistance = -1.0f;
};

DevelopRenderedDuplicateDecision EvaluateDevelopRenderedCandidateDuplicate(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativeFinalMetrics,
    bool candidatePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidatePreFinishMetrics,
    bool representativePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativePreFinishMetrics) {
    DevelopRenderedDuplicateDecision decision;
    decision.finalDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(candidateFinalMetrics, representativeFinalMetrics);
    if (candidatePreFinishValid && representativePreFinishValid) {
        decision.preFinishDistance =
            EditorRenderWorker::CompareDevelopCandidateRenderMetrics(candidatePreFinishMetrics, representativePreFinishMetrics);
    }

    if (decision.finalDistance >= kDevelopRenderedDuplicateDistance) {
        return decision;
    }

    // Final tone can mask upstream differences. Keep the candidate alive when
    // the final render is near-duplicate but the pre-finish boundary is not.
    if (decision.preFinishDistance >= kDevelopRenderedPreFinishDistinctDistance) {
        decision.preFinishDistinct = true;
        return decision;
    }

    decision.duplicate = true;
    return decision;
}

std::string ClassifyDevelopRenderedStageBoundary(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestFinalMetrics,
    bool finalMetricsValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedPreFinishMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestPreFinishMetrics,
    bool preFinishMetricsValid,
    float& outFinalDistance,
    float& outPreFinishDistance) {
    outFinalDistance = finalMetricsValid
        ? EditorRenderWorker::CompareDevelopCandidateRenderMetrics(selectedFinalMetrics, bestFinalMetrics)
        : -1.0f;
    outPreFinishDistance = preFinishMetricsValid
        ? EditorRenderWorker::CompareDevelopCandidateRenderMetrics(selectedPreFinishMetrics, bestPreFinishMetrics)
        : -1.0f;

    if (!finalMetricsValid) {
        return "missingFinalMetrics";
    }
    if (!preFinishMetricsValid) {
        return "missingPreFinishMetrics";
    }

    constexpr float kMeaningfulFinalDistance = kDevelopRenderedDuplicateDistance;
    constexpr float kStablePreFinishDistance = 0.055f;
    constexpr float kMeaningfulPreFinishDistance = kDevelopRenderedPreFinishDistinctDistance;
    if (outFinalDistance >= kMeaningfulFinalDistance &&
        outPreFinishDistance <= kStablePreFinishDistance) {
        return "finishToneOnly";
    }
    if (outPreFinishDistance >= kMeaningfulPreFinishDistance &&
        outFinalDistance < kMeaningfulFinalDistance) {
        return "preFinishChangedButFinalMasked";
    }
    if (outPreFinishDistance >= kMeaningfulPreFinishDistance) {
        return "preFinishChanged";
    }
    return "stable";
}

void AppendDevelopCandidateRenderedFeedbackHistory(
    nlohmann::json& toneJson,
    std::uint64_t fingerprint,
    const std::string& selectedCandidateId,
    float selectedRenderScore,
    bool selectedRenderScoreValid,
    const std::string& bestCandidateId,
    float bestRenderScore,
    int successCount,
    int failureCount,
    const std::string& action,
    const std::string& stopReason,
    const std::string& refineIntent = {},
    const std::string& refineReason = {},
    const EditorRenderWorker::DevelopCandidateRenderMetrics* selectedMetrics = nullptr,
    const EditorRenderWorker::DevelopCandidateRenderMetrics* bestMetrics = nullptr) {
    nlohmann::json history =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    if (!history.is_array()) {
        history = nlohmann::json::array();
    }

    nlohmann::json entry;
    entry["fingerprint"] = fingerprint;
    entry["selectedId"] = selectedCandidateId;
    entry["selectedRenderScore"] = selectedRenderScoreValid ? selectedRenderScore : -1.0f;
    entry["selectedRenderScoreValid"] = selectedRenderScoreValid;
    entry["bestId"] = bestCandidateId;
    entry["bestRenderScore"] = std::max(0.0f, bestRenderScore);
    entry["successCount"] = successCount;
    entry["failureCount"] = failureCount;
    entry["action"] = action;
    entry["stopReason"] = stopReason;
    if (selectedMetrics) {
        entry["selectedMetrics"] = DevelopCandidateRenderMetricsToJson(*selectedMetrics);
    }
    if (bestMetrics) {
        entry["bestMetrics"] = DevelopCandidateRenderMetricsToJson(*bestMetrics);
    }
    if (!refineIntent.empty()) {
        entry["refineIntent"] = refineIntent;
        entry["refineReason"] = refineReason;
    }
    history.push_back(std::move(entry));

    constexpr std::size_t kMaxHistoryEntries = 8;
    while (history.size() > kMaxHistoryEntries) {
        history.erase(history.begin());
    }
    toneJson["autoCandidateRenderedFeedbackHistory"] = std::move(history);
}

void WriteDevelopCandidateRenderedFeedbackLoopRecord(
    nlohmann::json& toneJson,
    std::uint64_t solveFingerprint,
    std::uint64_t revision,
    const std::string& state,
    const std::string& action,
    const std::string& stopReason,
    const std::string& nextStep,
    bool requiresAutoSolve,
    bool requiresRenderedMetrics,
    const std::string& selectedCandidateId,
    float selectedRenderScore,
    bool selectedRenderScoreValid,
    const std::string& bestCandidateId,
    float bestRenderScore,
    int successCount,
    int failureCount) {
    const nlohmann::json renderedHistory =
        toneJson.value("autoCandidateRenderedFeedbackHistory", nlohmann::json::array());
    const int renderedHistoryCount =
        renderedHistory.is_array() ? static_cast<int>(renderedHistory.size()) : 0;
    const int currentPass =
        toneJson.value("autoCandidateRenderedFeedbackPass", 0);
    const nlohmann::json continuationPolicy =
        toneJson.value("autoCandidateRenderedContinuationPolicy", nlohmann::json::object());
    toneJson["autoCandidateRenderedFeedbackLoopVersion"] =
        kDevelopRenderedFeedbackLoopVersion;
    toneJson["autoCandidateRenderedFeedbackLoop"] = {
        { "version", kDevelopRenderedFeedbackLoopVersion },
        { "state", state },
        { "action", action },
        { "stopReason", stopReason },
        { "nextStep", nextStep },
        { "requiresAutoSolve", requiresAutoSolve },
        { "requiresRenderedMetrics", requiresRenderedMetrics },
        { "pass", currentPass },
        { "nextPass", requiresAutoSolve ? currentPass + 1 : currentPass },
        { "maxPasses", kDevelopRenderedFeedbackMaxPasses },
        { "solveFingerprint", solveFingerprint },
        { "renderedFingerprint", toneJson.value("autoCandidateRenderedFingerprint", static_cast<std::uint64_t>(0)) },
        { "renderedAtRevision", revision },
        { "renderMetricsStatus", toneJson.value("autoCandidateRenderMetricsStatus", std::string()) },
        { "selectedId", selectedCandidateId },
        { "selectedRenderScore", selectedRenderScoreValid ? selectedRenderScore : -1.0f },
        { "selectedRenderScoreValid", selectedRenderScoreValid },
        { "bestId", bestCandidateId },
        { "bestScore", std::max(0.0f, bestRenderScore) },
        { "successCount", successCount },
        { "failureCount", failureCount },
        { "revisionStage", toneJson.value("autoCandidateRenderedRevisionStage", std::string()) },
        { "revisionReason", toneJson.value("autoCandidateRenderedRevisionReason", std::string()) },
        { "historyCount", renderedHistoryCount },
        { "continuationPolicy", continuationPolicy }
    };
}

const EditorNodeGraph::Node* FindUpstreamRawSourceForDevelopNode(
    const EditorNodeGraph::Graph& graph,
    const EditorNodeGraph::Node& developNode) {
    const EditorNodeGraph::Link* rawInput =
        graph.FindInputLink(developNode.id, EditorNodeGraph::kRawInputSocketId);
    std::unordered_set<int> visited;
    while (rawInput) {
        if (!visited.insert(rawInput->fromNodeId).second) {
            return nullptr;
        }

        const EditorNodeGraph::Node* upstream = graph.FindNode(rawInput->fromNodeId);
        if (!upstream) {
            return nullptr;
        }
        if (upstream->kind == EditorNodeGraph::NodeKind::RawSource) {
            return upstream;
        }
        if (upstream->kind != EditorNodeGraph::NodeKind::RawNeuralDenoise) {
            return nullptr;
        }
        rawInput = graph.FindInputLink(upstream->id, EditorNodeGraph::kRawInputSocketId);
    }
    return nullptr;
}

EditorNodeGraph::DevelopAutoGuidance ReadDevelopAuthoredGuidanceFromToneJson(
    const nlohmann::json& toneJson,
    EditorNodeGraph::DevelopAutoGuidance fallback) {
    if (!toneJson.is_object()) {
        EditorModule::NormalizeDevelopAutoGuidance(fallback);
        return fallback;
    }

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

void ClampCandidateScenePrepSettings(Raw::RawDetailFusionSettings& settings) {
    settings.mode = Raw::RawDetailFusionMode::AutoAnalyze;
    settings.debugView = Raw::RawDetailFusionDebugView::FinalImage;
    settings.invertMask = false;
    settings.maskBlackPoint = std::clamp(settings.maskBlackPoint, 0.0f, 1.0f);
    settings.maskWhitePoint = std::clamp(settings.maskWhitePoint, settings.maskBlackPoint + 0.001f, 1.0f);
    settings.maskGamma = std::clamp(settings.maskGamma, 0.05f, 8.0f);
    settings.minEvBias = std::clamp(settings.minEvBias, -2.0f, 2.0f);
    settings.maxEvBias = std::clamp(settings.maxEvBias, -2.0f, 2.0f);
    settings.baseEvBias = std::clamp(settings.baseEvBias, -1.25f, 1.25f);
    settings.noiseProtectionBias = std::clamp(settings.noiseProtectionBias, -1.0f, 1.0f);
    settings.highlightProtectionBias = std::clamp(settings.highlightProtectionBias, -1.0f, 1.0f);
    settings.shadowLiftLimitBias = std::clamp(settings.shadowLiftLimitBias, -1.0f, 1.0f);
    settings.wellExposedTargetBias = std::clamp(settings.wellExposedTargetBias, -1.0f, 1.0f);
    settings.strength = std::clamp(settings.strength, 0.0f, 1.25f);
    settings.manualBlend = 0.0f;
}

void PreserveCandidateRawCleanupSettings(
    Raw::RawDevelopSettings& settings,
    const Raw::RawDevelopSettings& baseSettings) {
    const float falseColorSuppression = settings.falseColorSuppression;
    const float defringeStrength = settings.defringeStrength;
    const float highlightEdgeCleanup = settings.highlightEdgeCleanup;
    const int chromaRadius = settings.chromaRadius;
    const float preserveRealColor = settings.preserveRealColor;
    const float lateralRedCyan = settings.lateralRedCyan;
    const float lateralBlueYellow = settings.lateralBlueYellow;
    const Raw::RawMosaicDenoiseSettings mosaicDenoise = settings.mosaicDenoise;

    settings = baseSettings;
    settings.debugView = Raw::RawDebugView::FinalOutput;
    settings.falseColorSuppression = falseColorSuppression;
    settings.defringeStrength = defringeStrength;
    settings.highlightEdgeCleanup = highlightEdgeCleanup;
    settings.chromaRadius = chromaRadius;
    settings.preserveRealColor = preserveRealColor;
    settings.lateralRedCyan = lateralRedCyan;
    settings.lateralBlueYellow = lateralBlueYellow;
    settings.mosaicDenoise = mosaicDenoise;
}

void ApplyDevelopGuidanceToCandidateRenderPayload(
    RenderGraphRawDevelopPayload& payload,
    const EditorNodeGraph::DevelopAutoGuidance& currentGuidance,
    const EditorNodeGraph::DevelopAutoGuidance& candidateGuidance,
    const std::string& candidateId,
    EditorNodeGraph::DevelopAutoIntent intent,
    const Raw::WhiteBalanceMode* whiteBalanceOverride = nullptr) {
    const Raw::RawDevelopSettings baseRawSettings = payload.settings;
    const Raw::RawDetailFusionSettings baseScenePrepSettings = payload.scenePrepSettings;
    const std::string revisionStage = DevelopRenderedRevisionStageForCandidateId(candidateId);
    const float autoStrengthDelta = candidateGuidance.autoStrength - currentGuidance.autoStrength;
    const float brightnessDelta = candidateGuidance.exposureBias - currentGuidance.exposureBias;
    const float rangeDelta = candidateGuidance.dynamicRange - currentGuidance.dynamicRange;
    const float shadowDelta = candidateGuidance.shadowLift - currentGuidance.shadowLift;
    const float highlightDelta = candidateGuidance.highlightGuard - currentGuidance.highlightGuard;
    const float highlightCharacterDelta = candidateGuidance.highlightCharacter - currentGuidance.highlightCharacter;
    const float contrastDelta = candidateGuidance.contrastBias - currentGuidance.contrastBias;
    const bool cleanShadowProbe =
        candidateId == "cleanShadows" ||
        candidateId == "renderedLocalCleanShadows";
    const bool texturePreserveProbe =
        candidateId == "preserveTexture" ||
        candidateId == "renderedLocalPreserveTexture";
    const bool shadowOpeningProbe = candidateId == "renderedLocalShadowOpening";
    const bool protectedMidsProbe = candidateId == "highlightProtectedMids";
    const bool broadHighlightGuardProbe = candidateId == "broadHighlightGuard";
    const bool haloSafeLocalRangeProbe = candidateId == "haloSafeLocalRange";
    const bool localRangeGuardProbe = candidateId == "localRangeGuard";
    const bool shadowReadabilityLiftProbe = candidateId == "shadowReadabilityLift";
    const bool shadowNoiseFloorProbe = candidateId == "shadowNoiseFloor";
    const bool subjectReadableMidsProbe = candidateId == "subjectReadableMids";
    const bool sceneMoodPreservationProbe = candidateId == "sceneMoodPreservation";
    const bool finishToneProbe = IsFinishToneProbeCandidateIdForRenderRequest(candidateId);
    nlohmann::json localExposureStrategy = nlohmann::json::object();
    if (payload.integratedToneLayerJson.is_object()) {
        localExposureStrategy =
            payload.integratedToneLayerJson.value(
                "autoDynamicRangeLocalExposureStrategy",
                nlohmann::json::object());
        if (!localExposureStrategy.is_object() ||
            localExposureStrategy.value("version", std::string()).empty()) {
            const nlohmann::json dynamicRangeStrategy =
                payload.integratedToneLayerJson.value(
                    "autoDynamicRangeStrategy",
                    nlohmann::json::object());
            if (dynamicRangeStrategy.is_object()) {
                localExposureStrategy =
                    dynamicRangeStrategy.value(
                        "localExposureStrategy",
                        nlohmann::json::object());
            }
        }
    }
    const bool hasLocalExposureStrategy =
        localExposureStrategy.is_object() &&
        localExposureStrategy.value("version", std::string()) == kDevelopLocalExposureStrategyVersion;
    const float localRangeRedistribution =
        hasLocalExposureStrategy ? localExposureStrategy.value("rangeRedistribution", 0.0f) : 0.0f;
    const float localHighlightCompression =
        hasLocalExposureStrategy ? localExposureStrategy.value("highlightCompression", 0.0f) : 0.0f;
    const float localShadowOpening =
        hasLocalExposureStrategy ? localExposureStrategy.value("shadowOpening", 0.0f) : 0.0f;
    const float localNoiseGuard =
        hasLocalExposureStrategy ? localExposureStrategy.value("noiseGuard", 0.0f) : 0.0f;
    const float localHaloGuard =
        hasLocalExposureStrategy ? localExposureStrategy.value("haloGuard", 0.0f) : 0.0f;
    const float localTextureGuard =
        hasLocalExposureStrategy ? localExposureStrategy.value("textureGuard", 0.0f) : 0.0f;
    const float localStrengthTarget =
        hasLocalExposureStrategy ? localExposureStrategy.value("strengthTarget", 0.5f) : 0.5f;
    Raw::WhiteBalanceMode whiteBalanceProbeMode = Raw::WhiteBalanceMode::AsShot;
    bool whiteBalanceProbe =
        TryResolveWhiteBalanceProbeCandidateModeForRenderRequest(
            candidateId,
            whiteBalanceProbeMode);
    if (whiteBalanceOverride) {
        whiteBalanceProbeMode = *whiteBalanceOverride;
        whiteBalanceProbe = true;
    }

    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    payload.settings.debugView = Raw::RawDebugView::FinalOutput;
    payload.settings.exposureStops = std::clamp(
        payload.settings.exposureStops +
            brightnessDelta * 1.45f +
            std::max(0.0f, shadowDelta) * 0.18f -
            std::max(0.0f, highlightDelta) * 0.12f,
        -8.0f,
        8.0f);
    payload.settings.highlightStrength = std::clamp(
        payload.settings.highlightStrength +
            std::max(0.0f, highlightDelta) * 0.20f +
            std::max(0.0f, rangeDelta) * 0.08f,
        0.0f,
        1.0f);
    payload.settings.highlightThreshold = std::clamp(
        payload.settings.highlightThreshold -
            std::max(0.0f, highlightDelta) * 0.025f -
            std::max(0.0f, rangeDelta) * 0.015f +
            std::max(0.0f, highlightCharacterDelta) * 0.010f,
        0.82f,
        0.995f);
    if (whiteBalanceProbe) {
        payload.settings.whiteBalanceMode = whiteBalanceProbeMode;
        payload.settings.manualWhiteBalance = baseRawSettings.manualWhiteBalance;
    }

    if (cleanShadowProbe || shadowOpeningProbe) {
        // Shadow-opening candidates need cleanup support, otherwise the rendered
        // probe only proves that lifted shadows got noisier.
        Raw::RawMosaicDenoiseSettings& denoise = payload.settings.mosaicDenoise;
        denoise.enabled = true;
        denoise.hotPixelSuppression = true;
        denoise.lumaStrength += 0.12f + std::max(0.0f, shadowDelta) * 0.10f;
        denoise.chromaStrength += 0.10f + std::max(0.0f, shadowDelta) * 0.08f;
        denoise.edgeProtection += 0.08f;
        denoise.radius = std::max(denoise.radius, shadowOpeningProbe ? 3 : 2);
        if (shadowOpeningProbe) {
            denoise.iterations = std::max(denoise.iterations, 2);
        }
        payload.settings.falseColorSuppression = std::clamp(
            payload.settings.falseColorSuppression + 0.05f,
            0.0f,
            1.0f);
        payload.settings.defringeStrength = std::clamp(
            payload.settings.defringeStrength + 0.03f,
            0.0f,
            1.0f);
        payload.settings.highlightEdgeCleanup = std::clamp(
            payload.settings.highlightEdgeCleanup + 0.04f,
            0.0f,
            1.0f);
        payload.settings.preserveRealColor = std::clamp(
            payload.settings.preserveRealColor + 0.04f,
            0.0f,
            1.0f);
        ClampCandidateMosaicDenoiseSettings(denoise);
    } else if (texturePreserveProbe) {
        // Texture probes deliberately keep a little more real grain/detail so
        // rendered metrics can compare natural texture against cleaner shadows.
        Raw::RawMosaicDenoiseSettings& denoise = payload.settings.mosaicDenoise;
        denoise.lumaStrength -= 0.10f;
        denoise.chromaStrength -= 0.06f;
        denoise.edgeProtection += 0.14f;
        denoise.hotPixelThreshold += 0.02f;
        payload.settings.falseColorSuppression = std::clamp(
            payload.settings.falseColorSuppression - 0.04f,
            0.0f,
            1.0f);
        payload.settings.defringeStrength = std::clamp(
            payload.settings.defringeStrength - 0.02f,
            0.0f,
            1.0f);
        payload.settings.preserveRealColor = std::clamp(
            payload.settings.preserveRealColor + 0.08f,
            0.0f,
            1.0f);
        ClampCandidateMosaicDenoiseSettings(denoise);
    }

    Raw::RawDetailFusionSettings prep = payload.scenePrepSettings;
    // Candidate renders are probes of nearby authored states: bias the existing
    // solved payload instead of re-running the full Auto solve inside the worker.
    prep.strength += autoStrengthDelta * 0.16f + std::max(0.0f, rangeDelta) * 0.10f;
    prep.maxEvBias += shadowDelta * 0.95f + std::max(0.0f, rangeDelta) * 0.35f;
    prep.minEvBias -= std::max(0.0f, highlightDelta) * 0.30f + std::max(0.0f, rangeDelta) * 0.25f;
    prep.baseEvBias += brightnessDelta * 0.35f;
    prep.highlightProtectionBias += highlightDelta * 0.65f + std::max(0.0f, rangeDelta) * 0.10f;
    prep.noiseProtectionBias += std::max(0.0f, shadowDelta) * 0.10f + std::max(0.0f, rangeDelta) * 0.05f;
    prep.shadowLiftLimitBias -= std::max(0.0f, shadowDelta) * 0.10f + std::max(0.0f, rangeDelta) * 0.08f;
    prep.wellExposedTargetBias += contrastDelta * 0.16f + brightnessDelta * 0.05f;
    if (hasLocalExposureStrategy) {
        prep.strength +=
            (localStrengthTarget - 0.5f) * 0.06f +
            localRangeRedistribution * 0.04f -
            localHaloGuard * 0.02f;
        prep.maxEvBias +=
            localShadowOpening * 0.05f +
            localRangeRedistribution * 0.04f -
            localNoiseGuard * 0.03f -
            localHaloGuard * 0.03f;
        prep.minEvBias -=
            localHighlightCompression * 0.05f +
            localRangeRedistribution * 0.03f;
        prep.highlightProtectionBias += localHighlightCompression * 0.04f;
        prep.noiseProtectionBias += localNoiseGuard * 0.04f;
        prep.shadowLiftLimitBias +=
            localNoiseGuard * 0.03f +
            localHaloGuard * 0.02f -
            localShadowOpening * 0.02f;
        prep.haloGuard = std::clamp(prep.haloGuard + localHaloGuard * 0.03f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(
            prep.smoothGradientProtection + localHaloGuard * 0.03f,
            0.0f,
            1.0f);
        prep.edgeAwareness = std::clamp(
            prep.edgeAwareness + localHaloGuard * 0.02f + localTextureGuard * 0.02f,
            0.0f,
            1.0f);
        prep.textureSensitivity = std::clamp(
            prep.textureSensitivity + localTextureGuard * 0.03f - localNoiseGuard * 0.01f,
            0.0f,
            1.0f);
    }
    if (cleanShadowProbe || shadowOpeningProbe) {
        prep.noiseProtectionBias += 0.10f;
        prep.shadowLiftLimitBias += cleanShadowProbe ? 0.06f : 0.00f;
        prep.textureSensitivity = std::clamp(prep.textureSensitivity - 0.04f, 0.0f, 1.0f);
    } else if (texturePreserveProbe) {
        prep.noiseProtectionBias -= 0.06f;
        prep.shadowLiftLimitBias += 0.05f;
        prep.detailWeight = std::clamp(prep.detailWeight + 0.08f, 0.0f, 1.0f);
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.12f, 0.0f, 1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.06f, 0.0f, 1.0f);
    } else if (protectedMidsProbe) {
        // This probe tests the Guide 02/03 "lower global placement, support mids
        // locally" family without introducing a separate render algorithm.
        prep.maxEvBias += 0.10f;
        prep.highlightProtectionBias += 0.08f;
        prep.wellExposedTargetBias += 0.03f;
    } else if (broadHighlightGuardProbe) {
        // Broad meaningful highlights need local compression, not the
        // tiny-specular exception path. Keep RAW frozen and test Scene Prep
        // highlight restraint with extra halo/smooth-gradient protection.
        prep.minEvBias -= 0.16f + localHighlightCompression * 0.08f;
        prep.maxEvBias -= 0.04f;
        prep.highlightProtectionBias += 0.18f + localHighlightCompression * 0.06f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.08f + localHaloGuard * 0.03f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(
            prep.smoothGradientProtection + 0.06f + localHaloGuard * 0.02f,
            0.0f,
            1.0f);
        prep.wellExposedTargetBias -= 0.03f;
    } else if (localRangeGuardProbe) {
        // Rendered regional evidence asked for a scene-prep/local-range check,
        // so bias only local exposure/range safety while RAW stays frozen below.
        prep.maxEvBias += 0.10f + localRangeRedistribution * 0.08f + localShadowOpening * 0.04f;
        prep.minEvBias -= 0.08f + localRangeRedistribution * 0.05f + localHighlightCompression * 0.03f;
        prep.highlightProtectionBias += 0.10f + localHighlightCompression * 0.04f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.08f + localHaloGuard * 0.05f, 0.0f, 1.0f);
        prep.wellExposedTargetBias -= 0.02f;
    } else if (haloSafeLocalRangeProbe) {
        // This Guide 04 safety probe backs away from aggressive local EV
        // moves and raises anti-halo/gradient protection to check whether
        // the image needs safer local exposure rather than more range.
        prep.strength -= 0.08f + localHaloGuard * 0.03f;
        prep.maxEvBias -= 0.14f + localRangeRedistribution * 0.04f;
        prep.minEvBias += 0.04f;
        prep.highlightProtectionBias += 0.08f;
        prep.shadowLiftLimitBias += 0.08f + localHaloGuard * 0.04f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.16f + localHaloGuard * 0.06f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(
            prep.smoothGradientProtection + 0.14f + localHaloGuard * 0.05f,
            0.0f,
            1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.10f + localHaloGuard * 0.04f, 0.0f, 1.0f);
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.04f + localTextureGuard * 0.03f, 0.0f, 1.0f);
        prep.wellExposedTargetBias -= 0.03f;
    } else if (shadowReadabilityLiftProbe) {
        // This is the positive Guide 04 shadow path: open readable shadows
        // locally while preserving RAW placement and leaving noise guardrails on.
        prep.maxEvBias += 0.16f + localShadowOpening * 0.08f;
        prep.baseEvBias += 0.03f;
        prep.noiseProtectionBias += 0.04f + localNoiseGuard * 0.03f;
        prep.shadowLiftLimitBias -= 0.10f + localShadowOpening * 0.04f;
        prep.wellExposedTargetBias += 0.04f;
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.04f, 0.0f, 1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.04f, 0.0f, 1.0f);
        prep.haloGuard = std::clamp(prep.haloGuard + 0.03f, 0.0f, 1.0f);
    } else if (subjectReadableMidsProbe) {
        // Guide 05 subject-readable probes open likely/marked important mids
        // through Scene Prep so RAW exposure and downstream tone are not
        // confused with the subject-intent question being tested.
        prep.maxEvBias += 0.14f + localShadowOpening * 0.06f;
        prep.baseEvBias += 0.03f;
        prep.highlightProtectionBias += 0.05f;
        prep.noiseProtectionBias += 0.06f + localNoiseGuard * 0.03f;
        prep.shadowLiftLimitBias -= 0.08f + localShadowOpening * 0.03f;
        prep.wellExposedTargetBias += 0.05f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.04f + localHaloGuard * 0.02f, 0.0f, 1.0f);
        prep.edgeAwareness = std::clamp(prep.edgeAwareness + 0.05f, 0.0f, 1.0f);
        prep.textureSensitivity = std::clamp(prep.textureSensitivity + 0.03f, 0.0f, 1.0f);
    } else if (sceneMoodPreservationProbe) {
        // This is the Guide 05 counter-probe for silhouettes/low-key scenes:
        // reduce local lifting pressure so subject importance stays a bias
        // rather than an automatic command to neutralize the scene mood.
        prep.maxEvBias -= 0.14f + localNoiseGuard * 0.03f;
        prep.baseEvBias -= 0.02f;
        prep.highlightProtectionBias += 0.05f;
        prep.noiseProtectionBias += 0.10f + localNoiseGuard * 0.04f;
        prep.shadowLiftLimitBias += 0.10f + localHaloGuard * 0.03f;
        prep.wellExposedTargetBias -= 0.04f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.05f, 0.0f, 1.0f);
        prep.smoothGradientProtection = std::clamp(prep.smoothGradientProtection + 0.04f, 0.0f, 1.0f);
    } else if (shadowNoiseFloorProbe) {
        // Guide 04 allows darkness to remain dark when lifting would reveal
        // noise or gray mush. Test that as a Scene Prep limit, not a RAW EV move.
        prep.maxEvBias -= 0.18f + localNoiseGuard * 0.04f;
        prep.noiseProtectionBias += 0.16f + localNoiseGuard * 0.06f;
        prep.shadowLiftLimitBias += 0.14f + localNoiseGuard * 0.06f;
        prep.haloGuard = std::clamp(prep.haloGuard + 0.05f, 0.0f, 1.0f);
        prep.wellExposedTargetBias -= 0.04f;
        prep.detailWeight = std::clamp(prep.detailWeight + 0.04f, 0.0f, 1.0f);
    }
    ClampCandidateScenePrepSettings(prep);
    payload.scenePrepSettings = prep;

    bool frozeRawStage = false;
    bool frozeScenePrepStage = false;
    std::string stageConstraintReason;
    if (revisionStage == "scenePrep" || revisionStage == "finishTone") {
        // Scene-prep and finish-tone probes should validate downstream choices,
        // not silently become new RAW exposure/highlight/WB experiments.
        payload.settings = baseRawSettings;
        payload.settings.debugView = Raw::RawDebugView::FinalOutput;
        frozeRawStage = true;
        stageConstraintReason =
            revisionStage == "scenePrep"
                ? "Scene-prep candidate render preserves RAW-stage placement so local exposure changes are measured cleanly."
                : "Finish-tone candidate render preserves RAW and scene prep so contrast/finish changes are measured cleanly.";
    } else if (revisionStage == "rawCleanup") {
        // Cleanup/detail probes may alter denoise and cleanup fields, but not
        // global RAW placement; otherwise noise/detail metrics are confounded by EV shifts.
        PreserveCandidateRawCleanupSettings(payload.settings, baseRawSettings);
        frozeRawStage = true;
        stageConstraintReason =
            "RAW cleanup candidate render preserves global RAW placement while varying cleanup/detail fields.";
    }

    if (revisionStage == "finishTone") {
        payload.scenePrepSettings = baseScenePrepSettings;
        frozeScenePrepStage = true;
    }

    if (!payload.integratedToneLayerJson.is_object()) {
        payload.integratedToneLayerJson = nlohmann::json::object();
    }
    nlohmann::json& toneJson = payload.integratedToneLayerJson;
    toneJson["autoIntent"] = EditorNodeGraph::DevelopAutoIntentStableString(intent);
    toneJson["autoSceneAssistStrength"] = candidateGuidance.autoStrength;
    toneJson["autoBrightnessIntent"] = candidateGuidance.exposureBias;
    toneJson["autoRawExposurePreferenceEv"] = candidateGuidance.exposureBias * 2.0f;
    toneJson["autoDynamicRange"] = candidateGuidance.dynamicRange;
    toneJson["autoShadowBias"] = candidateGuidance.shadowLift;
    toneJson["autoHighlightBias"] = candidateGuidance.highlightGuard;
    toneJson["autoHighlightCharacter"] = candidateGuidance.highlightCharacter;
    toneJson["autoContrastBias"] = candidateGuidance.contrastBias;
    toneJson["autoCandidateRenderedProbeId"] = candidateId;
    toneJson["autoCandidateStageConstraint"] = revisionStage;
    toneJson["autoCandidateStageConstraintApplied"] = frozeRawStage || frozeScenePrepStage;
    toneJson["autoCandidateStageConstraintFrozenRaw"] = frozeRawStage;
    toneJson["autoCandidateStageConstraintFrozenScenePrep"] = frozeScenePrepStage;
    if (!stageConstraintReason.empty()) {
        toneJson["autoCandidateStageConstraintReason"] = stageConstraintReason;
    }
    if (cleanShadowProbe || texturePreserveProbe) {
        toneJson["autoCandidateCleanupProbe"] =
            texturePreserveProbe ? "preserveTexture" : "cleanerShadows";
    }
    if (finishToneProbe) {
        toneJson["autoCandidateFinishToneProbe"] = candidateId;
    }
    if (broadHighlightGuardProbe || haloSafeLocalRangeProbe || localRangeGuardProbe || shadowReadabilityLiftProbe || shadowNoiseFloorProbe || subjectReadableMidsProbe || sceneMoodPreservationProbe) {
        toneJson["autoCandidateScenePrepProbe"] = candidateId;
    }
    if (subjectReadableMidsProbe || sceneMoodPreservationProbe) {
        toneJson["autoCandidateSubjectIntentProbe"] = candidateId;
    }
    if (hasLocalExposureStrategy) {
        toneJson["autoCandidateLocalExposureStrategyVersion"] =
            kDevelopLocalExposureStrategyVersion;
        toneJson["autoCandidateLocalExposureStrategy"] = localExposureStrategy;
        toneJson["autoCandidateLocalExposureStrategyId"] =
            localExposureStrategy.value("id", std::string());
        toneJson["autoCandidateLocalExposureRangeRedistribution"] =
            localRangeRedistribution;
        toneJson["autoCandidateLocalExposureHighlightCompression"] =
            localHighlightCompression;
        toneJson["autoCandidateLocalExposureShadowOpening"] =
            localShadowOpening;
        toneJson["autoCandidateLocalExposureNoiseGuard"] =
            localNoiseGuard;
        toneJson["autoCandidateLocalExposureHaloGuard"] =
            localHaloGuard;
        toneJson["autoCandidateLocalExposureTextureGuard"] =
            localTextureGuard;
        toneJson["autoCandidateLocalExposureStrengthTarget"] =
            localStrengthTarget;
    }
    if (whiteBalanceProbe) {
        toneJson["autoCandidateWhiteBalanceProbe"] = candidateId;
        toneJson["autoCandidateWhiteBalanceMode"] =
            Raw::WhiteBalanceModeName(whiteBalanceProbeMode);
    }
    toneJson["autoCalibratePending"] = true;
    toneJson["autoCalibrateVariant"] = 0;
    toneJson["autoCalibrateRequestId"] =
        toneJson.value("autoCalibrateRequestId", static_cast<std::uint64_t>(0)) + 1;
}

float ScoreDevelopRenderedCandidateMetrics(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    float solveScore) {
    const float medianBalance = 1.0f - std::clamp(std::abs(metrics.medianLuma - 0.42f) / 0.42f, 0.0f, 1.0f);
    const float usefulContrast = std::clamp(metrics.contrastSpan / 0.62f, 0.0f, 1.0f);
    const float highlightSafety = 1.0f - std::clamp(
        metrics.clippedFraction * 8.0f +
            metrics.highlightFraction * 0.45f -
            metrics.meaningfulHighlightPressure * 0.10f,
        0.0f,
        1.0f);
    const float highlightBrightnessFeeling =
        1.0f - std::clamp(metrics.highlightGrayRisk * 0.82f, 0.0f, 1.0f);
    const float shadowSafety = 1.0f - std::clamp(std::max(0.0f, metrics.shadowFraction - 0.48f) / 0.42f, 0.0f, 1.0f);
    const float colorPlausibility =
        1.0f - std::clamp(
            std::max(0.0f, metrics.lowSaturationFraction - 0.72f) / 0.28f +
                std::max(0.0f, 0.08f - metrics.meanSaturation) / 0.08f +
                metrics.colorCastRisk * 0.42f,
            0.0f,
            1.0f);
    const float visualSafety =
        1.0f - std::clamp(
            metrics.haloRiskFraction * 3.0f +
                metrics.shadowTextureRisk * 0.22f +
                std::max(0.0f, metrics.edgeContrast - 0.64f) * 0.40f,
            0.0f,
            1.0f);
    const float localSafety =
        1.0f - std::clamp(
            std::max(0.0f, metrics.localHighlightPressure - 0.58f) / 0.42f +
                std::max(0.0f, metrics.localShadowPressure - 0.74f) / 0.26f +
                std::max(0.0f, metrics.localContrastPeak - 0.80f) * 0.35f +
                metrics.localDamageRiskPeak * 0.38f +
                metrics.localDamageRiskMean * 0.18f,
            0.0f,
            1.0f);
    const float centerSafety =
        1.0f - std::clamp(
            metrics.centerShadowFraction * 0.32f +
                metrics.centerHighlightFraction * 0.44f,
            0.0f,
            1.0f);
    const float localShape =
        std::clamp(metrics.localLumaSpread / 0.48f, 0.0f, 1.0f) * localSafety * centerSafety;
    const float markedSubjectPresence =
        std::clamp(metrics.subjectMarkedPositiveCoverage * 1.8f, 0.0f, 1.0f);
    const float markedReadabilityWeight = std::clamp(
        metrics.subjectMarkedPositiveCoverage +
            metrics.subjectMarkedRevealCoverage * 0.75f -
            metrics.subjectMarkedMoodCoverage * 0.35f,
        0.0f,
        1.0f);
    const float markedProtectionWeight = std::clamp(
        metrics.subjectMarkedProtectCoverage +
            metrics.subjectMarkedPositiveCoverage * 0.35f,
        0.0f,
        1.0f);
    const float markedMoodWeight = std::clamp(metrics.subjectMarkedMoodCoverage * 1.4f, 0.0f, 1.0f);
    const float markedSubjectFit = std::clamp(
        metrics.subjectMarkedReadabilityScore * markedReadabilityWeight * 0.46f +
            (1.0f - metrics.subjectMarkedProtectionRisk) * markedProtectionWeight * 0.32f +
            metrics.subjectMarkedMoodPreservationScore * markedMoodWeight * 0.22f,
        0.0f,
        1.0f);
    const float lowPriorityPenalty =
        std::clamp(metrics.subjectMarkedLowPriorityPressure * 0.045f, 0.0f, 0.045f);
    return std::clamp(
        solveScore * 0.37f +
            medianBalance * 0.16f +
            usefulContrast * 0.12f +
            highlightSafety * 0.10f +
            highlightBrightnessFeeling * 0.03f +
            shadowSafety * 0.05f +
            colorPlausibility * 0.05f +
            visualSafety * 0.06f +
            localShape * 0.06f +
            markedSubjectFit * markedSubjectPresence * 0.045f -
            lowPriorityPenalty,
        0.0f,
        1.0f);
}

struct DevelopRenderedRelativeComparison {
    float adjustedScore = -1.0f;
    float standaloneScore = -1.0f;
    float selectedScore = -1.0f;
    float metricDistance = -1.0f;
    float repairDelta = 0.0f;
    float repairBonus = 0.0f;
    float regressionPenalty = 0.0f;
    float distanceBonus = 0.0f;
    std::string status = "standalone";
    std::string repairMetric;
    std::string reason;
};

DevelopRenderedRelativeComparison CompareDevelopRenderedCandidateToSelected(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidate,
    float standaloneScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selected,
    float selectedScore,
    const std::string& activeRefineIntent,
    bool selectedBaseline) {
    DevelopRenderedRelativeComparison comparison;
    comparison.standaloneScore = standaloneScore;
    comparison.selectedScore = selectedScore;
    comparison.adjustedScore = std::clamp(standaloneScore, 0.0f, 1.0f);
    comparison.metricDistance =
        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(candidate, selected);

    if (selectedBaseline) {
        comparison.status = "selectedBaseline";
        comparison.repairMetric = "selectedBaseline";
        comparison.reason = "Selected candidate establishes the rendered comparison baseline.";
        return comparison;
    }

    auto scaledPositive = [](float value, float deadZone, float scale, float maxValue) {
        return std::clamp((value - deadZone) / std::max(0.0001f, scale), 0.0f, 1.0f) * maxValue;
    };
    auto addRegression = [&](float value, const std::string& reason) {
        if (value <= 0.0f) {
            return;
        }
        comparison.regressionPenalty += value;
        if (comparison.reason.empty() || comparison.status == "relativeCompared") {
            comparison.reason = reason;
        }
    };

    comparison.distanceBonus =
        scaledPositive(
            comparison.metricDistance,
            kDevelopRenderedDuplicateDistance,
            0.24f,
            0.014f);

    addRegression(
        scaledPositive(candidate.clippedFraction - selected.clippedFraction, 0.010f, 0.050f, 0.040f),
        "Candidate increased broad highlight clipping relative to the selected render.");
    if (candidate.highlightFraction > 0.56f) {
        addRegression(
            scaledPositive(candidate.highlightFraction - selected.highlightFraction, 0.10f, 0.24f, 0.026f),
            "Candidate crowded more highlights than the selected render.");
    }
    if (candidate.localHighlightPressure > 0.62f) {
        addRegression(
            scaledPositive(candidate.localHighlightPressure - selected.localHighlightPressure, 0.12f, 0.24f, 0.024f),
            "Candidate increased localized highlight pressure relative to the selected render.");
    }
    if (candidate.highlightGrayRisk > 0.42f) {
        addRegression(
            scaledPositive(candidate.highlightGrayRisk - selected.highlightGrayRisk, 0.08f, 0.24f, 0.026f),
            "Candidate made broad highlights look flatter or grayer than the selected render.");
    }
    if (selected.meaningfulHighlightPressure > 0.34f &&
        candidate.meaningfulHighlightPressure + 0.12f < selected.meaningfulHighlightPressure &&
        candidate.clippedFraction >= selected.clippedFraction - 0.006f) {
        addRegression(
            scaledPositive(selected.meaningfulHighlightPressure - candidate.meaningfulHighlightPressure, 0.10f, 0.30f, 0.024f),
            "Candidate reduced structured broad-highlight evidence without a matching clipping improvement.");
    }
    if (candidate.shadowTextureRisk > 0.62f) {
        addRegression(
            scaledPositive(candidate.shadowTextureRisk - selected.shadowTextureRisk, 0.08f, 0.24f, 0.030f),
            "Candidate increased shadow texture/noise pressure relative to the selected render.");
    }
    if (candidate.localDamageRiskPeak > 0.66f) {
        addRegression(
            scaledPositive(candidate.localDamageRiskPeak - selected.localDamageRiskPeak, 0.10f, 0.28f, 0.025f),
            "Candidate increased localized render-damage risk relative to the selected render.");
    }
    if (candidate.haloRiskFraction > 0.10f) {
        addRegression(
            scaledPositive(candidate.haloRiskFraction - selected.haloRiskFraction, 0.04f, 0.16f, 0.024f),
            "Candidate increased halo or edge-glow risk relative to the selected render.");
    }
    if (candidate.colorCastRisk > 0.70f) {
        addRegression(
            scaledPositive(candidate.colorCastRisk - selected.colorCastRisk, 0.12f, 0.30f, 0.018f),
            "Candidate increased color-cast risk relative to the selected render.");
    }
    if (candidate.subjectMarkedPositiveCoverage > 0.010f &&
        selected.subjectMarkedPositiveCoverage > 0.010f) {
        if (candidate.subjectMarkedReadabilityScore + 0.08f < selected.subjectMarkedReadabilityScore &&
            selected.subjectMarkedRevealCoverage + selected.subjectMarkedPositiveCoverage >
                selected.subjectMarkedMoodCoverage * 0.70f) {
            addRegression(
                scaledPositive(
                    selected.subjectMarkedReadabilityScore - candidate.subjectMarkedReadabilityScore,
                    0.07f,
                    0.24f,
                    0.022f),
                "Candidate made the user-marked important region less readable than the selected render.");
        }
        if (candidate.subjectMarkedProtectionRisk > 0.42f) {
            addRegression(
                scaledPositive(
                    candidate.subjectMarkedProtectionRisk - selected.subjectMarkedProtectionRisk,
                    0.07f,
                    0.24f,
                    0.022f),
                "Candidate increased protection risk in the user-marked important region.");
        }
    }

    if (candidate.medianLuma < 0.16f && candidate.medianLuma < selected.medianLuma - 0.14f) {
        addRegression(0.018f, "Candidate made the rendered mids materially darker than the selected baseline.");
    } else if (candidate.medianLuma > 0.72f && candidate.medianLuma > selected.medianLuma + 0.16f) {
        addRegression(0.018f, "Candidate made the rendered mids materially brighter than the selected baseline.");
    }

    if (activeRefineIntent == "protectHighlights") {
        comparison.repairMetric = "highlightPressure";
        comparison.repairDelta =
            (selected.clippedFraction - candidate.clippedFraction) * 1.80f +
            (selected.highlightFraction - candidate.highlightFraction) * 0.38f +
            (selected.localHighlightPressure - candidate.localHighlightPressure) * 0.28f +
            (selected.centerHighlightFraction - candidate.centerHighlightFraction) * 0.20f;
    } else if (activeRefineIntent == "openShadows") {
        comparison.repairMetric = "shadowOpening";
        comparison.repairDelta =
            (selected.shadowFraction - candidate.shadowFraction) * 0.45f +
            (selected.localShadowPressure - candidate.localShadowPressure) * 0.32f +
            (selected.centerShadowFraction - candidate.centerShadowFraction) * 0.25f +
            (candidate.medianLuma - selected.medianLuma) * 0.18f;
    } else if (activeRefineIntent == "brightenMids") {
        comparison.repairMetric = "medianLuma";
        comparison.repairDelta =
            (candidate.medianLuma - selected.medianLuma) * 0.70f +
            (candidate.meanLuma - selected.meanLuma) * 0.30f -
            std::max(0.0f, candidate.highlightFraction - selected.highlightFraction) * 0.20f -
            std::max(0.0f, candidate.clippedFraction - selected.clippedFraction) * 1.20f;
    } else if (activeRefineIntent == "addContrast") {
        comparison.repairMetric = "contrast";
        comparison.repairDelta =
            (candidate.contrastSpan - selected.contrastSpan) * 0.60f +
            (candidate.localContrastPeak - selected.localContrastPeak) * 0.18f -
            std::max(0.0f, candidate.haloRiskFraction - selected.haloRiskFraction) * 0.45f -
            std::max(0.0f, candidate.localDamageRiskPeak - selected.localDamageRiskPeak) * 0.15f;
    } else if (activeRefineIntent == "cleanShadows") {
        comparison.repairMetric = "shadowTextureRisk";
        comparison.repairDelta =
            (selected.shadowTextureRisk - candidate.shadowTextureRisk) * 0.55f +
            (selected.localDamageRiskPeak - candidate.localDamageRiskPeak) * 0.24f +
            (selected.localShadowPressure - candidate.localShadowPressure) * 0.12f -
            std::max(0.0f, candidate.shadowFraction - selected.shadowFraction) * 0.20f;
    } else if (activeRefineIntent == "preserveTexture") {
        comparison.repairMetric = "textureSeparation";
        comparison.repairDelta =
            (candidate.edgeContrast - selected.edgeContrast) * 0.45f +
            (candidate.localContrastPeak - selected.localContrastPeak) * 0.16f -
            std::max(0.0f, candidate.shadowTextureRisk - selected.shadowTextureRisk) * 0.30f -
            std::max(0.0f, candidate.haloRiskFraction - selected.haloRiskFraction) * 0.30f;
    } else {
        comparison.repairMetric = "generalRenderRisk";
        comparison.repairDelta =
            std::max(0.0f, selected.clippedFraction - candidate.clippedFraction) * 1.20f +
            std::max(0.0f, selected.highlightFraction - candidate.highlightFraction - 0.03f) * 0.16f +
            std::max(0.0f, selected.shadowFraction - candidate.shadowFraction - 0.04f) * 0.12f +
            std::max(0.0f, candidate.contrastSpan - selected.contrastSpan - 0.02f) * 0.10f;
    }

    comparison.repairBonus =
        scaledPositive(comparison.repairDelta, activeRefineIntent.empty() ? 0.020f : 0.010f, 0.16f,
            activeRefineIntent.empty() ? 0.018f : 0.045f);

    if (!activeRefineIntent.empty() && comparison.repairDelta < -0.014f) {
        addRegression(
            scaledPositive(-comparison.repairDelta, 0.014f, 0.16f, 0.040f),
            "Candidate moved opposite the active rendered repair intent.");
    } else if (!activeRefineIntent.empty() &&
        comparison.repairDelta < 0.008f &&
        standaloneScore > selectedScore + 0.025f) {
        addRegression(0.018f, "Candidate scored well overall but did not materially address the active rendered repair intent.");
    }

    comparison.regressionPenalty = std::min(comparison.regressionPenalty, 0.095f);
    comparison.adjustedScore =
        std::clamp(
            standaloneScore +
                comparison.repairBonus +
                comparison.distanceBonus -
                comparison.regressionPenalty,
            0.0f,
            1.0f);

    if (!activeRefineIntent.empty() && comparison.repairDelta > 0.010f) {
        comparison.status = "improvesActiveRepair";
        comparison.reason =
            "Candidate improves the active rendered repair target relative to the selected baseline.";
    } else if (!activeRefineIntent.empty() && comparison.repairDelta < -0.014f) {
        comparison.status = "missedActiveRepair";
        if (comparison.reason.empty()) {
            comparison.reason =
                "Candidate moved away from the active rendered repair target relative to the selected baseline.";
        }
    } else if (comparison.regressionPenalty > comparison.repairBonus + 0.015f) {
        comparison.status = "regressedAgainstSelected";
        if (comparison.reason.empty()) {
            comparison.reason =
                "Candidate introduced more rendered risk than it repaired relative to the selected baseline.";
        }
    } else if (comparison.metricDistance < kDevelopRenderedDuplicateDistance) {
        comparison.status = "nearSelected";
        comparison.reason =
            "Candidate is close to the selected render after comparing rendered metrics.";
    } else {
        comparison.status = "relativeCompared";
        comparison.reason =
            "Candidate was compared against the selected rendered baseline and retained its standalone score with small relative adjustments.";
    }

    return comparison;
}

std::string ClassifyDevelopRenderedCandidateDamage(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent) {
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool flatIntent = intent == EditorNodeGraph::DevelopAutoIntent::FlatEditingBase;
    const bool rangeIntent = intent == EditorNodeGraph::DevelopAutoIntent::MaximumRangeDetail;
    const bool cleanIntent = intent == EditorNodeGraph::DevelopAutoIntent::CleanBase;

    const float broadClipLimit = rangeIntent ? 0.080f : 0.055f;
    if (metrics.clippedFraction > broadClipLimit ||
        (metrics.highlightFraction > 0.78f && metrics.clippedFraction > 0.012f)) {
        return "Rejected because rendered metrics showed broad highlight clipping/crowding beyond the selected intent.";
    }
    if (metrics.meaningfulHighlightPressure > 0.58f &&
        metrics.clippedFraction > (rangeIntent ? 0.040f : 0.026f) &&
        metrics.highlightTileCoverage > 0.24f) {
        return "Rejected because rendered metrics showed meaningful broad-highlight structure clipping beyond the selected intent.";
    }
    if (metrics.haloRiskFraction > 0.22f &&
        (metrics.edgeContrast > 0.34f || metrics.localContrastPeak > 0.76f)) {
        return "Rejected because rendered metrics showed strong halo or edge-glow risk.";
    }
    const float localizedRiskLimit = rangeIntent || flatIntent ? 0.94f : 0.88f;
    if (metrics.localDamageRiskPeak > localizedRiskLimit &&
        metrics.localDamageRiskMean > 0.20f &&
        (metrics.localHighlightPressure > 0.66f ||
         metrics.localShadowPressure > 0.80f ||
         metrics.localContrastPeak > 0.88f)) {
        return "Rejected because rendered spatial metrics showed a localized damage hotspot that would be visible in the final image.";
    }
    const float colorCastLimit = rangeIntent || darkIntent ? 0.92f : 0.86f;
    if (metrics.colorCastRisk > colorCastLimit &&
        (std::fabs(metrics.magentaGreenBias) > 0.30f ||
         (metrics.channelImbalance > 0.90f && metrics.meanSaturation > 0.42f) ||
         (metrics.lowSaturationFraction > 0.78f && metrics.meanSaturation < 0.08f))) {
        return "Rejected because rendered color metrics showed an extreme cast or channel imbalance beyond the selected intent.";
    }
    const float flatContrastFloor = flatIntent ? 0.20f : 0.30f;
    if (metrics.lowSaturationFraction > 0.93f &&
        metrics.meanSaturation < 0.055f &&
        metrics.contrastSpan < flatContrastFloor) {
        return "Rejected because rendered metrics looked washed out and gray with weak tonal separation.";
    }
    if (!flatIntent &&
        metrics.highlightGrayRisk > 0.86f &&
        metrics.highlightBandFraction > 0.20f &&
        metrics.highlightMeanLuma < 0.62f &&
        metrics.clippedFraction < 0.018f) {
        return "Rejected because rendered metrics showed broad highlights flattening toward gray instead of staying luminous.";
    }
    const float shadowTextureLimit = cleanIntent ? 0.92f : 0.86f;
    const float shadowFractionLimit = rangeIntent ? 0.62f : 0.50f;
    if (metrics.shadowTextureRisk > shadowTextureLimit &&
        metrics.shadowFraction > shadowFractionLimit &&
        metrics.medianLuma > 0.16f) {
        return "Rejected because rendered metrics showed noisy lifted shadows beyond the current cleanliness/texture intent.";
    }
    if (metrics.medianLuma < (darkIntent ? 0.055f : 0.075f) &&
        metrics.shadowFraction > (darkIntent ? 0.90f : 0.80f)) {
        return "Rejected because rendered metrics were too weak to preserve a usable brightness hierarchy.";
    }
    if (metrics.medianLuma > 0.84f && metrics.highlightFraction > 0.64f) {
        return "Rejected because rendered metrics were too bright and highlight-heavy to preserve a believable hierarchy.";
    }
    return {};
}

std::string ResolveDevelopRenderedRefineIntent(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent,
    std::string& outReason) {
    const bool darkIntent = intent == EditorNodeGraph::DevelopAutoIntent::DarkNatural;
    const bool punchyIntent = intent == EditorNodeGraph::DevelopAutoIntent::PunchyHighContrast;

    if (metrics.clippedFraction > 0.018f || metrics.highlightFraction > 0.56f) {
        outReason = "Rendered metrics showed highlight crowding or clipping, so the solver should try more highlight protection.";
        return "protectHighlights";
    }
    if (metrics.meaningfulHighlightPressure > 0.46f &&
        (metrics.clippedFraction > 0.006f || metrics.localHighlightPressure > 0.44f)) {
        outReason = "Rendered metrics showed structured broad-highlight pressure, so the solver should protect meaningful highlights before treating them as tiny glints.";
        return "protectHighlights";
    }
    const bool localizedDamageRisk =
        metrics.localDamageRiskPeak > 0.70f &&
        metrics.localDamageRiskMean > 0.10f;
    if (localizedDamageRisk &&
        (metrics.localHighlightPressure > 0.56f || metrics.centerHighlightFraction > 0.30f)) {
        outReason = "Rendered spatial metrics showed a localized bright-region damage hotspot, so the solver should try more highlight/local exposure restraint.";
        return "protectHighlights";
    }
    if (localizedDamageRisk &&
        metrics.localContrastPeak > 0.84f &&
        (metrics.haloRiskFraction > 0.03f || metrics.edgeContrast > 0.30f)) {
        outReason = "Rendered spatial metrics showed a localized high-contrast damage hotspot, so the solver should back off highlight/local contrast shaping.";
        return "protectHighlights";
    }
    if (localizedDamageRisk &&
        metrics.localShadowPressure > (darkIntent ? 0.78f : 0.64f) &&
        metrics.localHighlightPressure < 0.58f &&
        metrics.highlightFraction < 0.34f &&
        metrics.clippedFraction < 0.010f) {
        if (metrics.shadowTextureRisk > (darkIntent ? 0.66f : 0.54f)) {
            outReason = "Rendered spatial metrics showed a localized shadow damage hotspot with texture/noise pressure, so the solver should try cleaner shadows instead of lifting more.";
            return "cleanShadows";
        }
        outReason = "Rendered spatial metrics showed a localized shadow damage hotspot without highlight pressure, so the solver should try opening shadows in a damped way.";
        return "openShadows";
    }
    if (localizedDamageRisk &&
        !darkIntent &&
        metrics.localContrastPeak < 0.24f &&
        metrics.lowSaturationFraction > 0.68f &&
        metrics.highlightFraction < 0.30f &&
        metrics.clippedFraction < 0.006f) {
        outReason = "Rendered spatial metrics showed a localized flat-gray hotspot without clipping pressure, so the solver should try a modest contrast refinement.";
        return "addContrast";
    }
    if (metrics.localHighlightPressure > 0.70f || metrics.centerHighlightFraction > 0.46f) {
        outReason = "Rendered local metrics showed concentrated highlight crowding, so the solver should try more highlight/local exposure restraint.";
        return "protectHighlights";
    }
    if (metrics.haloRiskFraction > 0.10f && metrics.highlightFraction > 0.22f) {
        outReason = "Rendered metrics showed edge overshoot risk around brighter transitions, so the solver should back off highlight/local contrast pressure.";
        return "protectHighlights";
    }
    if (metrics.highlightGrayRisk > 0.48f &&
        metrics.highlightBandFraction > 0.16f &&
        metrics.clippedFraction < 0.018f) {
        outReason = "Rendered metrics showed broad highlights flattening toward gray, so the solver should try restoring highlight brightness separation.";
        return "addContrast";
    }
    if (metrics.localContrastPeak > 0.86f && metrics.haloRiskFraction > 0.06f) {
        outReason = "Rendered local metrics showed very steep regional contrast with edge-risk pressure, so the solver should back off local highlight/contrast shaping.";
        return "protectHighlights";
    }
    if (metrics.shadowTextureRisk > (darkIntent ? 0.78f : 0.66f) &&
        metrics.highlightFraction < 0.34f &&
        metrics.localHighlightPressure < 0.62f &&
        metrics.clippedFraction < 0.010f &&
        (metrics.shadowFraction > 0.24f ||
         metrics.localShadowPressure > 0.42f ||
         metrics.centerShadowFraction > 0.34f)) {
        outReason = "Rendered metrics showed shadow texture/noise pressure without matching highlight trouble, so the solver should try a cleaner-shadow refinement instead of simply lifting more.";
        return "cleanShadows";
    }
    if (!darkIntent &&
        metrics.edgeContrast > 0.0f &&
        metrics.edgeContrast < 0.24f &&
        metrics.contrastSpan > 0.32f &&
        metrics.localLumaSpread > 0.12f &&
        metrics.shadowTextureRisk < 0.32f &&
        metrics.highlightFraction < 0.34f &&
        metrics.clippedFraction < 0.006f &&
        metrics.lowSaturationFraction < 0.84f) {
        outReason = "Rendered metrics showed safe tones but subdued fine separation, so the solver should try a texture-preserving cleanup balance.";
        return "preserveTexture";
    }
    if (!darkIntent &&
        metrics.centerMeanLuma > 0.0f &&
        metrics.centerMeanLuma < 0.16f &&
        metrics.centerShadowFraction > 0.52f &&
        metrics.localHighlightPressure < 0.58f) {
        outReason = "Rendered local metrics showed the center region buried in shadows without matching highlight pressure, so the solver should try a damped shadow-opening refinement.";
        return "openShadows";
    }
    if (!darkIntent && metrics.medianLuma < 0.18f && metrics.highlightFraction < 0.22f && metrics.clippedFraction < 0.008f) {
        outReason = "Rendered metrics showed the selected result landing too dark without highlight pressure, so the solver should try brighter mids.";
        return "brightenMids";
    }
    if (metrics.localShadowPressure > (darkIntent ? 0.88f : 0.78f) &&
        metrics.localHighlightPressure < 0.62f &&
        metrics.clippedFraction < 0.010f) {
        outReason = "Rendered local metrics showed a tile with heavy shadow crowding, so the solver should try opening shadows without making the whole image flat.";
        return "openShadows";
    }
    if (metrics.shadowFraction > (darkIntent ? 0.72f : 0.62f) && metrics.medianLuma < (darkIntent ? 0.14f : 0.24f)) {
        outReason = "Rendered metrics showed heavy shadow crowding, so the solver should try a damped shadow-opening refinement.";
        return "openShadows";
    }
    if ((punchyIntent || metrics.medianLuma >= 0.20f) &&
        metrics.localLumaSpread < 0.14f &&
        metrics.localContrastPeak < 0.32f &&
        metrics.highlightFraction < 0.30f &&
        metrics.clippedFraction < 0.006f &&
        metrics.lowSaturationFraction < 0.86f) {
        outReason = "Rendered local metrics showed low regional separation without clipping pressure, so the solver should try a modest contrast refinement.";
        return "addContrast";
    }
    if ((punchyIntent || metrics.medianLuma >= 0.20f) &&
        metrics.contrastSpan < 0.24f &&
        metrics.highlightFraction < 0.34f &&
        metrics.clippedFraction < 0.006f &&
        metrics.lowSaturationFraction < 0.86f) {
        outReason = "Rendered metrics showed low separation without clipping pressure, so the solver should try a modest contrast refinement.";
        return "addContrast";
    }

    outReason.clear();
    return {};
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

std::string EditorModule::ResolveDevelopRenderedRefineIntentForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent,
    std::string& outReason) {
    return ResolveDevelopRenderedRefineIntent(metrics, intent, outReason);
}

std::string EditorModule::ClassifyDevelopRenderedCandidateDamageForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& metrics,
    EditorNodeGraph::DevelopAutoIntent intent) {
    return ClassifyDevelopRenderedCandidateDamage(metrics, intent);
}

float EditorModule::ScoreDevelopRenderedCandidateRelativeToSelectedForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateMetrics,
    float candidateStandaloneScore,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedMetrics,
    float selectedScore,
    const std::string& activeRefineIntent,
    std::string& outStatus,
    std::string& outRepairMetric,
    float& outMetricDistance,
    float& outRepairDelta,
    float& outRepairBonus,
    float& outRegressionPenalty) {
    const DevelopRenderedRelativeComparison comparison =
        CompareDevelopRenderedCandidateToSelected(
            candidateMetrics,
            candidateStandaloneScore,
            selectedMetrics,
            selectedScore,
            activeRefineIntent,
            false);
    outStatus = comparison.status;
    outRepairMetric = comparison.repairMetric;
    outMetricDistance = comparison.metricDistance;
    outRepairDelta = comparison.repairDelta;
    outRepairBonus = comparison.repairBonus;
    outRegressionPenalty = comparison.regressionPenalty;
    return comparison.adjustedScore;
}

std::string EditorModule::ClassifyDevelopRenderedStageBoundaryForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestFinalMetrics,
    bool finalMetricsValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& selectedPreFinishMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& bestPreFinishMetrics,
    bool preFinishMetricsValid,
    float& outFinalDistance,
    float& outPreFinishDistance) {
    return ClassifyDevelopRenderedStageBoundary(
        selectedFinalMetrics,
        bestFinalMetrics,
        finalMetricsValid,
        selectedPreFinishMetrics,
        bestPreFinishMetrics,
        preFinishMetricsValid,
        outFinalDistance,
        outPreFinishDistance);
}

bool EditorModule::ShouldTreatDevelopRenderedCandidateAsDuplicateForValidation(
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidateFinalMetrics,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativeFinalMetrics,
    bool candidatePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& candidatePreFinishMetrics,
    bool representativePreFinishValid,
    const EditorRenderWorker::DevelopCandidateRenderMetrics& representativePreFinishMetrics,
    float& outFinalDistance,
    float& outPreFinishDistance,
    bool& outPreFinishDistinct) {
    const DevelopRenderedDuplicateDecision decision =
        EvaluateDevelopRenderedCandidateDuplicate(
            candidateFinalMetrics,
            representativeFinalMetrics,
            candidatePreFinishValid,
            candidatePreFinishMetrics,
            representativePreFinishValid,
            representativePreFinishMetrics);
    outFinalDistance = decision.finalDistance;
    outPreFinishDistance = decision.preFinishDistance;
    outPreFinishDistinct = decision.preFinishDistinct;
    return decision.duplicate;
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

bool EditorModule::IsDevelopRenderedFeedbackStopConvergedReason(
    const std::string& stopReason) {
    if (stopReason == "renderedMetricsStable" ||
        stopReason == "renderedFeedbackNoImprovementTrend" ||
        stopReason == "renderedRefineNoImprovementTrend" ||
        stopReason == "renderedFeedbackStableTrend" ||
        stopReason == "selectedCandidateStillBest" ||
        stopReason == "noMeaningfulRenderedImprovement" ||
        stopReason == "convergenceAdmissionNoMeaningfulImprovement" ||
        stopReason == "renderedRefineDidNotImprove" ||
        stopReason == "renderedRefineRepeatedIntent" ||
        stopReason == "renderedMergeConverged" ||
        stopReason == "renderedMergeDidNotImprove" ||
        stopReason == "renderedAdoptionNoFurtherGain" ||
        stopReason == "renderedBestRelativeRegression") {
        return true;
    }
    return stopReason.rfind("renderedRefineMonotonic", 0) == 0;
}

bool EditorModule::IsDevelopRenderedFeedbackStopConvergedForValidation(
    const std::string& stopReason) {
    return IsDevelopRenderedFeedbackStopConvergedReason(stopReason);
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

std::vector<unsigned char> EditorModule::GetScopePixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
    if (!node) {
        return {};
    }

    if (!IsImageOutputNode(node->kind) && !IsMaskOutputNode(node->kind)) {
        return {};
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    const std::string sourceSocketId = IsMaskOutputNode(node->kind)
        ? EditorNodeGraph::kMaskOutputSocketId
        : EditorNodeGraph::kImageOutputSocketId;
    if ((node->kind == EditorNodeGraph::NodeKind::Output &&
         TryResolveReferenceSourcePixelsForOutput(nodeId, sourcePixels, sourceW, sourceH, sourceCh)) ||
        (node->kind != EditorNodeGraph::NodeKind::Output &&
         TryResolveReferenceSourcePixels(nodeId, sourceSocketId, sourcePixels, sourceW, sourceH, sourceCh))) {
        // Scope renders use the same reference canvas as the graph path.
    } else if (node->kind == EditorNodeGraph::NodeKind::Image && !node->image.pixels.empty() &&
        node->image.width > 0 && node->image.height > 0) {
        sourcePixels = node->image.pixels;
        sourceW = node->image.width;
        sourceH = node->image.height;
        sourceCh = std::max(1, node->image.channels);
    } else {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& graphNode : m_NodeGraph.GetNodes()) {
            if (graphNode.kind == EditorNodeGraph::NodeKind::Image && !graphNode.image.pixels.empty() &&
                graphNode.image.width > 0 && graphNode.image.height > 0) {
                sourcePixels = graphNode.image.pixels;
                sourceW = graphNode.image.width;
                sourceH = graphNode.image.height;
                sourceCh = std::max(1, graphNode.image.channels);
                break;
            }
        }
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    if (IsMaskOutputNode(node->kind) || node->kind == EditorNodeGraph::NodeKind::Output) {
        snapshot.outputNodeId = nodeId;
    } else {
        const int syntheticOutputId = -200000 - nodeId;
        RenderGraphNode outputNode;
        outputNode.nodeId = syntheticOutputId;
        outputNode.kind = RenderGraphNodeKind::Output;
        snapshot.nodes.push_back(std::move(outputNode));
        snapshot.links.push_back(RenderGraphLink{
            nodeId,
            EditorNodeGraph::kImageOutputSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
        snapshot.outputNodeId = syntheticOutputId;
    }

    RenderPipeline scopePipeline;
    scopePipeline.Initialize();
    scopePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, sourceCh);
    scopePipeline.ExecuteGraph(snapshot);
    return scopePipeline.GetScopesPixels(outW, outH);
}

std::vector<unsigned char> EditorModule::GetPreviewPixelsForNode(int nodeId, int& outW, int& outH) {
    outW = 0;
    outH = 0;

    const EditorNodeGraph::Node* previewNode = m_NodeGraph.FindNode(nodeId);
    if (!previewNode || previewNode->kind != EditorNodeGraph::NodeKind::Preview) {
        return {};
    }

    const EditorNodeGraph::Link* input = m_NodeGraph.FindAnyInputLink(nodeId, EditorNodeGraph::kPreviewInputSocketId);
    if (!input) {
        return {};
    }

    EditorNodeGraph::SocketDefinition sourceSocket;
    if (!m_NodeGraph.FindSocket(input->fromNodeId, input->fromSocketId, &sourceSocket)) {
        return {};
    }

    const EditorNodeGraph::Node* sourceNode = m_NodeGraph.FindNode(input->fromNodeId);
    if (!sourceNode) {
        return {};
    }

    if (sourceSocket.type != EditorNodeGraph::SocketType::Image &&
        sourceSocket.type != EditorNodeGraph::SocketType::Mask) {
        return {};
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Preview renders use the same reference canvas as the inspected stream.
    } else if (sourceNode->kind == EditorNodeGraph::NodeKind::Image && !sourceNode->image.pixels.empty() &&
        sourceNode->image.width > 0 && sourceNode->image.height > 0) {
        sourcePixels = sourceNode->image.pixels;
        sourceW = sourceNode->image.width;
        sourceH = sourceNode->image.height;
        sourceCh = std::max(1, sourceNode->image.channels);
    } else {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Image && !node.image.pixels.empty() &&
                node.image.width > 0 && node.image.height > 0) {
                sourcePixels = node.image.pixels;
                sourceW = node.image.width;
                sourceH = node.image.height;
                sourceCh = std::max(1, node.image.channels);
                break;
            }
        }
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    snapshot.outputNodeId = input->fromNodeId;
    snapshot.outputSocketId = input->fromSocketId;

    RenderPipeline previewPipeline;
    previewPipeline.Initialize();
    previewPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    previewPipeline.ExecuteGraph(snapshot);
    return previewPipeline.GetPreviewPixels(outW, outH, 512);
}

bool EditorModule::BuildSingleOutputExportRaster(std::vector<unsigned char>& outPixels, int& outW, int& outH) const {
    outW = 0;
    outH = 0;
    outPixels.clear();

    if (!m_NodeGraph.IsOutputConnected()) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;

    if (TryResolveReferenceSourcePixelsForOutput(snapshot.outputNodeId, sourcePixels, sourceW, sourceH, sourceCh)) {
        // Use reference canvas.
    } else if (const EditorNodeGraph::Node* activeImage = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId())) {
        if (activeImage->kind == EditorNodeGraph::NodeKind::Image && !activeImage->image.pixels.empty() && activeImage->image.width > 0 && activeImage->image.height > 0) {
            sourcePixels = activeImage->image.pixels;
            sourceW = activeImage->image.width;
            sourceH = activeImage->image.height;
            sourceCh = std::max(1, activeImage->image.channels);
        }
    }

    if (sourcePixels.empty()) {
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (node.kind == EditorNodeGraph::NodeKind::Image && !node.image.pixels.empty() && node.image.width > 0 && node.image.height > 0) {
                sourcePixels = node.image.pixels;
                sourceW = node.image.width;
                sourceH = node.image.height;
                sourceCh = std::max(1, node.image.channels);
                break;
            }
        }
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        sourceW = 256;
        sourceH = 256;
        sourceCh = 4;
        sourcePixels.assign(static_cast<size_t>(sourceW * sourceH * sourceCh), 0);
        for (size_t i = 3; i < sourcePixels.size(); i += 4) {
            sourcePixels[i] = 255;
        }
    }

    RenderPipeline exportPipeline;
    exportPipeline.Initialize();
    exportPipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    exportPipeline.ExecuteGraph(snapshot);
    outPixels = exportPipeline.GetOutputPixels(outW, outH);
    return !outPixels.empty() && outW > 0 && outH > 0;
}

bool EditorModule::ProbeViewTransformInputStats(int viewTransformNodeId, RenderTextureStats& outStats) const {
    outStats = {};

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(viewTransformNodeId);
    if (!node ||
        node->kind != EditorNodeGraph::NodeKind::Layer ||
        node->layerType != LayerType::ViewTransform) {
        return false;
    }

    const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(viewTransformNodeId, EditorNodeGraph::kImageInputSocketId);
    if (!input) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    if (!TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
        sourcePixels = m_Pipeline.GetSourcePixelsRaw();
        sourceW = m_Pipeline.GetCanvasWidth();
        sourceH = m_Pipeline.GetCanvasHeight();
        sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
    }
    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -300000 - viewTransformNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));
    snapshot.links.push_back(RenderGraphLink{
        input->fromNodeId,
        input->fromSocketId,
        syntheticOutputId,
        EditorNodeGraph::kImageInputSocketId
    });
    snapshot.outputNodeId = syntheticOutputId;
    snapshot.outputSocketId = EditorNodeGraph::kImageInputSocketId;

    RenderPipeline probePipeline;
    probePipeline.Initialize();
    probePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    probePipeline.ExecuteGraph(snapshot);
    outStats = probePipeline.GetOutputTextureStats();
    return outStats.valid;
}

int EditorModule::ResolveFocusedToneCurveNodeId() const {
    auto isToneCurveNodeId = [&](int nodeId) {
        const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
        if (!node) {
            return false;
        }
        if (node->kind == EditorNodeGraph::NodeKind::Layer &&
            node->layerType == LayerType::ToneCurve &&
            node->layerIndex >= 0 &&
            node->layerIndex < static_cast<int>(m_Layers.size())) {
            return true;
        }
        return node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
            node->rawDevelop.integratedToneEnabled;
    };

    if (m_CanvasToolKind == CanvasToolKind::ToneCurveTarget &&
        isToneCurveNodeId(m_CanvasToolOwnerNodeId)) {
        return m_CanvasToolOwnerNodeId;
    }
    if (isToneCurveNodeId(m_ActiveComplexNodeId)) {
        return m_ActiveComplexNodeId;
    }
    const int selectedNodeId = m_NodeGraph.GetSelectedNodeId();
    if (isToneCurveNodeId(selectedNodeId)) {
        return selectedNodeId;
    }
    return -1;
}

bool EditorModule::HasFocusedToneCurveViewportInteraction() const {
    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        return false;
    }

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return false;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        return IsCanvasToolActiveForNode(toneCurveNodeId, CanvasToolKind::ToneCurveTarget);
    }

    return true;
}

void EditorModule::ClearTrackedToneCurveProbe() {
    if (m_LastToneCurveProbeNodeId <= 0) {
        m_LastToneCurveProbeNodeId = -1;
        return;
    }
    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_LastToneCurveProbeNodeId);
    if (node &&
        node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get())) {
            toneCurve->ClearViewportProbe();
            toneCurve->EndViewportTargetDrag();
        }
    } else if (node &&
        node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        ClearIntegratedToneTransientState(m_LastToneCurveProbeNodeId);
    }
    m_LastToneCurveProbeNodeId = -1;
}

void EditorModule::ClearToneCurveViewportProbe() {
    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        ClearTrackedToneCurveProbe();
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        m_LastToneCurveProbeNodeId = -1;
        return;
    }

    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        if (ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get())) {
            toneCurve->ClearViewportProbe();
            toneCurve->EndViewportTargetDrag();
            m_LastToneCurveProbeNodeId = toneCurveNodeId;
        }
        return;
    }

    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        ClearIntegratedToneTransientState(toneCurveNodeId);
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        return;
    }

    m_LastToneCurveProbeNodeId = -1;
}

bool EditorModule::SampleToneCurveViewportPixel(
    int toneCurveNodeId,
    ToneCurveSamplingBasis basis,
    float u,
    float v,
    std::array<float, 4>& outRgba) const {
    outRgba = { 0.0f, 0.0f, 0.0f, 0.0f };

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return false;
    }

    if (basis == ToneCurveSamplingBasis::FinalPreview) {
        return m_Pipeline.SampleOutputPixel(u, v, outRgba);
    }

    if (!CanRefreshPreviewLikeNodes()) {
        return false;
    }

    std::vector<unsigned char> sourcePixels;
    int sourceW = 0;
    int sourceH = 0;
    int sourceCh = 4;
    RenderGraphSnapshot snapshot = BuildGraphSnapshot();
    const int syntheticOutputId = -350000 - toneCurveNodeId;
    RenderGraphNode outputNode;
    outputNode.nodeId = syntheticOutputId;
    outputNode.kind = RenderGraphNodeKind::Output;
    snapshot.nodes.push_back(std::move(outputNode));

    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve) {
        const EditorNodeGraph::Link* input = m_NodeGraph.FindInputLink(toneCurveNodeId, EditorNodeGraph::kImageInputSocketId);
        if (!input) {
            return false;
        }

        if (!TryResolveReferenceSourcePixels(input->fromNodeId, input->fromSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
            if (TryResolveReferenceSourceDimensions(input->fromNodeId, input->fromSocketId, sourceW, sourceH) &&
                sourceW > 0 &&
                sourceH > 0) {
                sourceCh = 4;
                sourcePixels = BuildTransparentPixels(sourceW, sourceH);
            } else {
                sourcePixels = m_Pipeline.GetSourcePixelsRaw();
                sourceW = m_Pipeline.GetCanvasWidth();
                sourceH = m_Pipeline.GetCanvasHeight();
                sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
            }
        }
        snapshot.links.push_back(RenderGraphLink{
            input->fromNodeId,
            input->fromSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (!TryResolveReferenceSourcePixels(toneCurveNodeId, EditorNodeGraph::kImageOutputSocketId, sourcePixels, sourceW, sourceH, sourceCh)) {
            sourcePixels = m_Pipeline.GetSourcePixelsRaw();
            sourceW = m_Pipeline.GetCanvasWidth();
            sourceH = m_Pipeline.GetCanvasHeight();
            sourceCh = std::max(1, m_Pipeline.GetSourceChannels());
        }

        bool foundRawDevelopNode = false;
        for (RenderGraphNode& renderNode : snapshot.nodes) {
            if (renderNode.nodeId == toneCurveNodeId && renderNode.kind == RenderGraphNodeKind::RawDevelop) {
                renderNode.rawDevelop.integratedToneEnabled = false;
                foundRawDevelopNode = true;
                break;
            }
        }
        if (!foundRawDevelopNode) {
            return false;
        }

        snapshot.links.push_back(RenderGraphLink{
            toneCurveNodeId,
            EditorNodeGraph::kImageOutputSocketId,
            syntheticOutputId,
            EditorNodeGraph::kImageInputSocketId
        });
    } else {
        return false;
    }

    if (sourcePixels.empty() || sourceW <= 0 || sourceH <= 0) {
        return false;
    }

    snapshot.outputNodeId = syntheticOutputId;
    snapshot.outputSocketId = EditorNodeGraph::kImageInputSocketId;

    RenderPipeline probePipeline;
    probePipeline.Initialize();
    probePipeline.LoadSourceFromPixels(sourcePixels.data(), sourceW, sourceH, std::max(1, sourceCh));
    probePipeline.ExecuteGraph(snapshot);
    return probePipeline.SampleOutputPixel(u, v, outRgba);
}

void EditorModule::UpdateToneCurveViewportProbe(float u, float v) {
    if (m_CanvasToolKind == CanvasToolKind::PickColor || m_CanvasToolKind == CanvasToolKind::AdjustAberrationCenter) {
        ClearTrackedToneCurveProbe();
        return;
    }

    const int toneCurveNodeId = ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        ClearTrackedToneCurveProbe();
        return;
    }

    const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        ClearTrackedToneCurveProbe();
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        ClearTrackedToneCurveProbe();
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }

    std::array<float, 4> rgba {};
    ToneCurveSamplingBasis sampledBasis = toneCurve->GetSamplingBasis();
    bool sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    if (!sampled && sampledBasis == ToneCurveSamplingBasis::CurveInput) {
        sampledBasis = ToneCurveSamplingBasis::FinalPreview;
        sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    }
    if (!sampled) {
        toneCurve->ClearViewportProbe();
        if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
        }
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        return;
    }

    toneCurve->UpdateViewportProbe(sampledBasis, u, v, rgba);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
    }
    m_LastToneCurveProbeNodeId = toneCurveNodeId;
}

void EditorModule::BeginToneCurveViewportTargetDrag(float u, float v) {
    const int toneCurveNodeId = m_CanvasToolKind == CanvasToolKind::ToneCurveTarget
        ? m_CanvasToolOwnerNodeId
        : ResolveFocusedToneCurveNodeId();
    if (toneCurveNodeId <= 0) {
        return;
    }

    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    std::array<float, 4> rgba {};
    ToneCurveSamplingBasis sampledBasis = toneCurve->GetSamplingBasis();
    bool sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    if (!sampled && sampledBasis == ToneCurveSamplingBasis::CurveInput) {
        sampledBasis = ToneCurveSamplingBasis::FinalPreview;
        sampled = SampleToneCurveViewportPixel(toneCurveNodeId, sampledBasis, u, v, rgba);
    }
    if (!sampled) {
        return;
    }

    if (m_LastToneCurveProbeNodeId > 0 && m_LastToneCurveProbeNodeId != toneCurveNodeId) {
        ClearTrackedToneCurveProbe();
    }
    if (toneCurve->BeginViewportTargetDrag(sampledBasis, u, v, rgba)) {
        if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
            node->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();
            StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
        }
        m_LastToneCurveProbeNodeId = toneCurveNodeId;
        MarkRenderDirty(toneCurveNodeId);
    }
}

void EditorModule::UpdateToneCurveViewportTargetDrag(float deltaCurveY) {
    if (m_CanvasToolKind != CanvasToolKind::ToneCurveTarget || m_CanvasToolOwnerNodeId <= 0) {
        return;
    }
    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(m_CanvasToolOwnerNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(m_CanvasToolOwnerNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    toneCurve->UpdateViewportTargetDrag(deltaCurveY);
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        node->rawDevelop.integratedToneLayerJson = toneCurve->Serialize();
        StoreIntegratedToneTransientState(m_CanvasToolOwnerNodeId, *toneCurve);
    }
    MarkRenderDirty(m_CanvasToolOwnerNodeId);
}

void EditorModule::EndToneCurveViewportTargetDrag() {
    const int toneCurveNodeId = m_CanvasToolKind == CanvasToolKind::ToneCurveTarget
        ? m_CanvasToolOwnerNodeId
        : m_LastToneCurveProbeNodeId;
    if (toneCurveNodeId <= 0) {
        return;
    }

    EditorNodeGraph::Node* node = m_NodeGraph.FindNode(toneCurveNodeId);
    if (!node) {
        return;
    }

    ToneCurveLayer integratedTone;
    ToneCurveLayer* toneCurve = nullptr;
    if (node->kind == EditorNodeGraph::NodeKind::Layer &&
        node->layerType == LayerType::ToneCurve &&
        node->layerIndex >= 0 &&
        node->layerIndex < static_cast<int>(m_Layers.size())) {
        toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[static_cast<std::size_t>(node->layerIndex)].get());
    } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop &&
        node->rawDevelop.integratedToneEnabled) {
        if (node->rawDevelop.integratedToneLayerJson.is_object()) {
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
        }
        RestoreIntegratedToneTransientState(toneCurveNodeId, integratedTone);
        toneCurve = &integratedTone;
    }
    if (!toneCurve) {
        return;
    }

    toneCurve->EndViewportTargetDrag();
    if (node->kind == EditorNodeGraph::NodeKind::RawDevelop) {
        StoreIntegratedToneTransientState(toneCurveNodeId, *toneCurve);
    }
}

void EditorModule::RenderGraphScopeNode(EditorNodeGraph::ScopeKind scopeKind, int sourceNodeId) {
    m_Scopes.RenderScopeNode(this, scopeKind, sourceNodeId);
}

void EditorModule::MarkRenderDirty(int touchedNodeId) {
    const bool wasAlreadyDirty = m_RenderDirty;
    m_RenderDirty = true;
    m_Dirty = true;
    ++m_RenderRevision;
    if (!wasAlreadyDirty) {
        m_LastRenderDirtyTime = ImGui::GetTime();
    }
    if (touchedNodeId > 0) {
        const std::vector<int> downstreamNodeIds = m_NodeGraph.GetDownstreamRenderNodeIds(touchedNodeId);
        const std::vector<int> downstreamOutputNodeIds = m_NodeGraph.GetDownstreamOutputNodeIds(touchedNodeId);
        m_GraphPerformanceStats.lastInvalidationWasFull = false;
        m_GraphPerformanceStats.lastTouchedNodeId = touchedNodeId;
        m_GraphPerformanceStats.lastDirtyNodeCount = static_cast<int>(downstreamNodeIds.size());
        m_GraphPerformanceStats.lastDirtyOutputCount = static_cast<int>(downstreamOutputNodeIds.size());
        MarkDownstreamNodesDirty(touchedNodeId);
        MarkCompositeOutputsDirty(downstreamOutputNodeIds);
    } else {
        m_GraphPerformanceStats.lastInvalidationWasFull = true;
        m_GraphPerformanceStats.lastTouchedNodeId = -1;
        m_GraphPerformanceStats.lastDirtyNodeCount = 0;
        for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
            if (m_NodeGraph.IsRenderChainNode(node)) {
                ++m_GraphPerformanceStats.lastDirtyNodeCount;
            }
        }
        m_GraphPerformanceStats.lastDirtyOutputCount =
            static_cast<int>(m_NodeGraph.GetConnectedOutputNodeIds().size());
        MarkAllRenderNodesDirty();
        MarkCompositeOutputsDirty(m_NodeGraph.GetConnectedOutputNodeIds());
        m_PreviewDisplayedRevisions.clear();
        m_PreviewRequestedGenerations.clear();
        m_PreviewCompletedGenerations.clear();
        m_ScopeDisplayedRevisions.clear();
    }
}

EditorRenderWorker::Snapshot EditorModule::BuildRenderSnapshot(std::uint64_t generation) {
    EditorRenderWorker::Snapshot snapshot;
    snapshot.generation = generation;
    snapshot.graph = BuildGraphSnapshot();
    snapshot.outputConnected = GetViewportMode() == ViewportMode::SingleOutputPreview && m_NodeGraph.IsOutputConnected();
    const EditorNodeGraph::Node* activeComplexNode =
        m_ActiveComplexNodeId > 0 ? m_NodeGraph.FindNode(m_ActiveComplexNodeId) : nullptr;
    const bool rawDevelopComplexPreview =
        snapshot.outputConnected &&
        m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        activeComplexNode &&
        activeComplexNode->kind == EditorNodeGraph::NodeKind::RawDevelop;
    snapshot.previewMaxDimension = rawDevelopComplexPreview ? 2048 : 0;
    const auto clampPreviewDimensions = [&](int& width, int& height) {
        if (snapshot.previewMaxDimension <= 0 || width <= 0 || height <= 0) {
            return;
        }
        const int longestSide = std::max(width, height);
        if (longestSide <= snapshot.previewMaxDimension) {
            return;
        }
        width = std::max(1, static_cast<int>(
            (static_cast<long long>(width) * snapshot.previewMaxDimension + longestSide / 2) / longestSide));
        height = std::max(1, static_cast<int>(
            (static_cast<long long>(height) * snapshot.previewMaxDimension + longestSide / 2) / longestSide));
    };
    const bool autoGainMaskPreviewActive =
        snapshot.outputConnected &&
        m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
        m_ActiveComplexNodeId == m_AutoGainMaskPreviewNodeId &&
        m_AutoGainMaskPreviewNodeId > 0 &&
        m_NodeGraph.FindNode(m_AutoGainMaskPreviewNodeId) &&
        m_NodeGraph.FindNode(m_AutoGainMaskPreviewNodeId)->kind == EditorNodeGraph::NodeKind::RawDetailFusion;
    if (autoGainMaskPreviewActive) {
        snapshot.graph.outputNodeId = m_AutoGainMaskPreviewNodeId;
        snapshot.graph.outputSocketId = EditorNodeGraph::kMaskOutputSocketId;
        snapshot.graph.autoGainMaskPreview = true;
    }
    if (snapshot.outputConnected) {
        if (autoGainMaskPreviewActive &&
            TryResolveReferenceSourceBuffer(
                m_AutoGainMaskPreviewNodeId,
                EditorNodeGraph::kMaskOutputSocketId,
                snapshot.sourcePixels,
                snapshot.width,
                snapshot.height,
                snapshot.channels)) {
            // Use the inspected Pre-Local Exposure node's reference canvas.
        } else if (!autoGainMaskPreviewActive &&
            TryResolveReferenceSourceBufferForOutput(
                snapshot.graph.outputNodeId,
                snapshot.sourcePixels,
                snapshot.width,
                snapshot.height,
                snapshot.channels)) {
            // Use the output's reference canvas for multi-source channel recombination.
        } else if (const EditorNodeGraph::Node* activeImage = m_NodeGraph.FindNode(m_NodeGraph.GetActiveImageNodeId())) {
            if (activeImage->kind == EditorNodeGraph::NodeKind::Image &&
                !activeImage->image.pixels.empty() &&
                activeImage->image.width > 0 &&
                activeImage->image.height > 0) {
                snapshot.sourcePixels = EnsureSharedImagePixels(activeImage->image);
                snapshot.width = activeImage->image.width;
                snapshot.height = activeImage->image.height;
                snapshot.channels = std::max(1, activeImage->image.channels);
            } else if (activeImage->kind == EditorNodeGraph::NodeKind::RawSource) {
                ResolveRawDisplayDimensions(activeImage->rawSource.metadata, snapshot.width, snapshot.height);
                snapshot.channels = 4;
                clampPreviewDimensions(snapshot.width, snapshot.height);
            }
        }
        if (snapshot.sourcePixels.empty() && (snapshot.width <= 0 || snapshot.height <= 0)) {
            snapshot.sourcePixels = MakeSharedSourcePixelBufferCopy(m_Pipeline.GetSourcePixelsRaw());
            snapshot.width = m_Pipeline.GetCanvasWidth();
            snapshot.height = m_Pipeline.GetCanvasHeight();
            snapshot.channels = m_Pipeline.GetSourceChannels();
        }
        if (snapshot.sourcePixels.empty() && (snapshot.width <= 0 || snapshot.height <= 0)) {
            for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
                if (node.kind == EditorNodeGraph::NodeKind::Image &&
                    !node.image.pixels.empty() &&
                    node.image.width > 0 &&
                    node.image.height > 0) {
                    snapshot.sourcePixels = EnsureSharedImagePixels(node.image);
                    snapshot.width = node.image.width;
                    snapshot.height = node.image.height;
                    snapshot.channels = std::max(1, node.image.channels);
                    break;
                } else if (node.kind == EditorNodeGraph::NodeKind::RawSource &&
                    node.rawSource.metadata.visibleWidth > 0 &&
                    node.rawSource.metadata.visibleHeight > 0) {
                    ResolveRawDisplayDimensions(node.rawSource.metadata, snapshot.width, snapshot.height);
                    snapshot.channels = 4;
                    clampPreviewDimensions(snapshot.width, snapshot.height);
                    break;
                }
            }
        }
        if (snapshot.width <= 0 || snapshot.height <= 0) {
            snapshot.width = 256;
            snapshot.height = 256;
            snapshot.channels = 4;
            snapshot.sourcePixels = MakeSharedPixelBufferOwned(BuildTransparentPixels(snapshot.width, snapshot.height));
        }
    }
    const bool requiresLegacyLayerStack = snapshot.graph.nodes.empty();
    if (requiresLegacyLayerStack) {
        snapshot.masks = BuildGraphRenderMasks();
        for (const RenderLayerStep& step : BuildGraphRenderSteps()) {
            if (step.layer) {
                nlohmann::json item = nlohmann::json::object();
                item["layer"] = step.layer->Serialize();
                item["maskNodeId"] = step.maskNodeId;
                snapshot.layerSteps.push_back(std::move(item));
                snapshot.layers.push_back(step.layer->Serialize());
            }
        }
    }
    if (snapshot.outputConnected) {
        snapshot.developCandidateRenders =
            BuildDevelopCandidateRenderRequests(snapshot.graph, snapshot.width, snapshot.height);
    }
    return snapshot;
}

void EditorModule::RefreshCompletedChainCacheIfNeeded() const {
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    if (m_CachedCompletedChainsStructureRevision == structureRevision) {
        return;
    }

    m_CachedCompletedChains.clear();
    for (const EditorNodeGraph::CompletedChainInfo& chain : m_NodeGraph.GetCompletedChains()) {
        CachedCompositeChainState state;
        state.info = chain;
        m_CachedCompletedChains.push_back(std::move(state));
    }
    m_CachedConnectedOutputCount = static_cast<int>(m_CachedCompletedChains.size());
    m_CachedCompletedChainsStructureRevision = structureRevision;
}

void EditorModule::RefreshCompositeMetadataCacheIfNeeded() {
    RefreshCompletedChainCacheIfNeeded();
    const std::uint64_t structureRevision = m_NodeGraph.GetStructureRevision();
    if (m_CachedCompositeMetadataStructureRevision == structureRevision &&
        m_CachedCompositeMetadataRenderRevision == m_RenderRevision &&
        m_CachedCompositeFingerprints.size() == m_CachedCompletedChains.size() &&
        m_CachedCompositeLabels.size() == m_CachedCompletedChains.size()) {
        return;
    }

    std::unordered_map<int, std::size_t> previousFingerprints = m_CachedCompositeFingerprints;
    m_CachedCompositeFingerprints.clear();
    m_CachedCompositeLabels.clear();
    for (CachedCompositeChainState& chain : m_CachedCompletedChains) {
        chain.fingerprint = BuildCompositeChainFingerprint(chain.info);
        chain.label = BuildCompositeChainLabel(chain.info);
        m_CachedCompositeFingerprints[chain.info.outputNodeId] = chain.fingerprint;
        m_CachedCompositeLabels[chain.info.outputNodeId] = chain.label;
        const auto previousIt = previousFingerprints.find(chain.info.outputNodeId);
        if (previousIt == previousFingerprints.end() || previousIt->second != chain.fingerprint) {
            MarkCompositeOutputsDirty(std::vector<int>{ chain.info.outputNodeId });
        }
    }

    PruneCompositeDirtyState();
    m_CachedCompositeMetadataStructureRevision = structureRevision;
    m_CachedCompositeMetadataRenderRevision = m_RenderRevision;
}

void EditorModule::MarkDownstreamNodesDirty(int touchedNodeId) {
    for (int nodeId : m_NodeGraph.GetDownstreamRenderNodeIds(touchedNodeId)) {
        m_NodeDirtyGenerations[nodeId] = ++m_NodeDirtyGenerationCounter;
    }
}

void EditorModule::MarkAllRenderNodesDirty() {
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (m_NodeGraph.IsRenderChainNode(node)) {
            m_NodeDirtyGenerations[node.id] = ++m_NodeDirtyGenerationCounter;
        }
    }
}

void EditorModule::MarkCompositeOutputsDirty(const std::vector<int>& outputNodeIds) {
    for (const int outputNodeId : outputNodeIds) {
        if (outputNodeId > 0) {
            m_CompositeOutputDirtyGenerations[outputNodeId] = ++m_CompositeDirtyGenerationCounter;
        }
    }
}

void EditorModule::PruneCompositeDirtyState() {
    std::unordered_set<int> activeOutputIds;
    activeOutputIds.reserve(m_CachedCompletedChains.size());
    for (const CachedCompositeChainState& chain : m_CachedCompletedChains) {
        activeOutputIds.insert(chain.info.outputNodeId);
    }

    auto pruneMap = [&activeOutputIds](auto& map) {
        for (auto it = map.begin(); it != map.end();) {
            if (!activeOutputIds.count(it->first)) {
                it = map.erase(it);
            } else {
                ++it;
            }
        }
    };
    pruneMap(m_CompositeOutputDirtyGenerations);
    pruneMap(m_CompositeOutputRequestedGenerations);
    pruneMap(m_CompositeOutputCompletedGenerations);
}

std::uint64_t EditorModule::GetNodeDirtyGeneration(int nodeId) const {
    const auto it = m_NodeDirtyGenerations.find(nodeId);
    return it != m_NodeDirtyGenerations.end() ? it->second : 0;
}

bool EditorModule::IsRecentRawDevelopInteraction(double now) const {
    if (now < 0.0) {
        if (ImGui::GetCurrentContext() == nullptr) {
            return false;
        }
        now = ImGui::GetTime();
    }
    return m_LastRawDevelopInteractionTime >= 0.0 &&
        (now - m_LastRawDevelopInteractionTime) < 0.35;
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

bool EditorModule::CanRefreshPreviewLikeNodes() const {
    return !ShouldDeferPreviewLikeWork();
}

bool EditorModule::ShouldDeferPreviewLikeWork(double now) const {
    if (m_RenderDirty || m_RenderPending || m_RenderWorker.IsBusy()) {
        return true;
    }

    if (now < 0.0) {
        now = ImGui::GetCurrentContext() ? ImGui::GetTime() : -1.0;
    }
    if (now >= 0.0) {
        if (now - m_LastRenderDirtyTime < kPreviewLikeRefreshQuietSeconds) {
            return true;
        }
        if (IsRecentRawDevelopInteraction(now)) {
            return true;
        }
    } else if (IsRecentRawDevelopInteraction()) {
        return true;
    }

    return false;
}

bool EditorModule::HasPendingPreviewRefreshes() const {
    if (!CanRefreshPreviewLikeNodes()) {
        return true;
    }

    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Preview &&
            node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask) {
            continue;
        }
        if (GetPreviewNodeRevision(node.id) == 0) {
            continue;
        }
        const std::uint64_t desiredRevision = GetPreviewNodeRevision(node.id);
        const auto displayedIt = m_PreviewDisplayedRevisions.find(node.id);
        const std::uint64_t displayedRevision = displayedIt != m_PreviewDisplayedRevisions.end() ? displayedIt->second : 0;
        if (desiredRevision != displayedRevision) {
            return true;
        }
    }
    return false;
}

std::vector<EditorRenderWorker::CompositeOutputRequest> EditorModule::BuildCompositeOutputRequests() {
    RefreshCompositeMetadataCacheIfNeeded();

    std::vector<EditorRenderWorker::CompositeOutputRequest> requests;
    requests.reserve(m_CachedCompletedChains.size());
    SharedPixelBuffer pipelineSourcePixels;
    for (const CachedCompositeChainState& chainState : m_CachedCompletedChains) {
        CompositeSceneItem* item = FindCompositeSceneItem(chainState.info.outputNodeId);
        if (!item) {
            continue;
        }

        const std::uint64_t dirtyGeneration =
            m_CompositeOutputDirtyGenerations.count(chainState.info.outputNodeId)
                ? m_CompositeOutputDirtyGenerations[chainState.info.outputNodeId]
                : 0;
        const std::uint64_t completedGeneration =
            m_CompositeOutputCompletedGenerations.count(chainState.info.outputNodeId)
                ? m_CompositeOutputCompletedGenerations[chainState.info.outputNodeId]
                : 0;
        const std::uint64_t requestedGeneration =
            m_CompositeOutputRequestedGenerations.count(chainState.info.outputNodeId)
                ? m_CompositeOutputRequestedGenerations[chainState.info.outputNodeId]
                : 0;
        const bool scalableGenerator = CompletedChainSourceUsesScalableGenerator(chainState.info.outputNodeId);
        const float scaleX = std::max(0.01f, std::abs(item->scale.x));
        const float scaleY = std::max(0.01f, std::abs(item->scale.y));
        const int desiredRasterWidth = scalableGenerator
            ? std::clamp(
                static_cast<int>(std::ceil(std::max(
                    static_cast<float>(kScalableGeneratorBaseRaster),
                    std::max(1.0f, static_cast<float>(item->textureWidth <= 0 ? kScalableGeneratorBaseRaster : item->textureWidth)) * scaleX))),
                256,
                kScalableGeneratorMaxRaster)
            : std::max(1, item->textureWidth);
        const int desiredRasterHeight = scalableGenerator
            ? std::clamp(
                static_cast<int>(std::ceil(std::max(
                    static_cast<float>(kScalableGeneratorBaseRaster),
                    std::max(1.0f, static_cast<float>(item->textureHeight <= 0 ? kScalableGeneratorBaseRaster : item->textureHeight)) * scaleY))),
                256,
                kScalableGeneratorMaxRaster)
            : std::max(1, item->textureHeight);
        const bool needsTextureRefresh =
            item->texture == 0 ||
            completedGeneration < dirtyGeneration ||
            item->cachedChainFingerprint != chainState.fingerprint ||
            (scalableGenerator &&
             (item->textureWidth < desiredRasterWidth || item->textureHeight < desiredRasterHeight));
        const bool requestAlreadyPending =
            requestedGeneration >= dirtyGeneration &&
            item->requestedChainFingerprint == chainState.fingerprint &&
            (!scalableGenerator ||
             (item->requestedRasterWidth >= desiredRasterWidth &&
              item->requestedRasterHeight >= desiredRasterHeight));
        if (!needsTextureRefresh || requestAlreadyPending) {
            continue;
        }

        EditorRenderWorker::CompositeOutputRequest request;
        request.outputNodeId = chainState.info.outputNodeId;
        request.sourceNodeId = m_NodeGraph.ResolveReferenceSourceNodeIdForOutput(chainState.info.outputNodeId);
        if (request.sourceNodeId <= 0) {
            request.sourceNodeId = chainState.info.sourceNodeId;
        }

        if (TryResolveReferenceSourceBufferForOutput(
                chainState.info.outputNodeId,
                request.sourcePixels,
                request.width,
                request.height,
                request.channels)) {
            // Render this output against its own reference canvas.
        } else if (TryCopyImageNodeSharedPixels(
            request.sourceNodeId,
            request.sourcePixels,
            request.width,
            request.height,
            request.channels)) {
            // Reuse source-node pixels or a transparent RAW reference buffer.
        } else if (!m_Pipeline.GetSourcePixelsRaw().empty() &&
            m_Pipeline.GetCanvasWidth() > 0 &&
            m_Pipeline.GetCanvasHeight() > 0) {
            if (pipelineSourcePixels.empty()) {
                pipelineSourcePixels = MakeSharedSourcePixelBufferCopy(m_Pipeline.GetSourcePixelsRaw());
            }
            request.sourcePixels = pipelineSourcePixels;
            request.width = m_Pipeline.GetCanvasWidth();
            request.height = m_Pipeline.GetCanvasHeight();
            request.channels = std::max(1, m_Pipeline.GetSourceChannels());
        } else if (scalableGenerator) {
            request.width = desiredRasterWidth;
            request.height = desiredRasterHeight;
            request.channels = 4;
            request.sourcePixels = MakeSharedPixelBufferOwned(BuildTransparentPixels(request.width, request.height));
        } else {
            request.width = 256;
            request.height = 256;
            request.channels = 4;
            request.sourcePixels = MakeSharedPixelBufferOwned(BuildTransparentPixels(request.width, request.height));
        }

        request.dirtyGeneration = dirtyGeneration;
        request.chainFingerprint = chainState.fingerprint;
        item->requestedRenderRevision = dirtyGeneration;
        item->requestedChainFingerprint = chainState.fingerprint;
        item->requestedRasterWidth = request.width;
        item->requestedRasterHeight = request.height;
        m_CompositeOutputRequestedGenerations[chainState.info.outputNodeId] = dirtyGeneration;
        requests.push_back(std::move(request));
    }

    return requests;
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

std::vector<EditorRenderWorker::PreviewRequest> EditorModule::BuildPreviewRequests() {
    std::vector<EditorRenderWorker::PreviewRequest> requests;
    SharedPixelBuffer pipelineSourcePixels;
    SharedPixelBuffer fallbackImagePixels;
    int fallbackImageWidth = 0;
    int fallbackImageHeight = 0;
    int fallbackImageChannels = 4;
    for (const EditorNodeGraph::Node& node : m_NodeGraph.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Preview &&
            node.kind != EditorNodeGraph::NodeKind::RawDetailAutoMask) {
            continue;
        }

        const bool generatedAutoMaskPreview = node.kind == EditorNodeGraph::NodeKind::RawDetailAutoMask;
        const EditorNodeGraph::Link* input = generatedAutoMaskPreview
            ? m_NodeGraph.FindInputLink(node.id, EditorNodeGraph::kImageInputSocketId)
            : m_NodeGraph.FindAnyInputLink(node.id, EditorNodeGraph::kPreviewInputSocketId);
        if (!input) {
            m_PreviewRequestedGenerations.erase(node.id);
            m_PreviewCompletedGenerations.erase(node.id);
            m_PreviewPixelCache.erase(node.id);
            continue;
        }

        EditorNodeGraph::SocketDefinition sourceSocket;
        const int sourceNodeId = generatedAutoMaskPreview ? node.id : input->fromNodeId;
        const std::string sourceSocketId = generatedAutoMaskPreview
            ? EditorNodeGraph::kMaskOutputSocketId
            : input->fromSocketId;
        if (!m_NodeGraph.FindSocket(sourceNodeId, sourceSocketId, &sourceSocket)) {
            continue;
        }
        if (sourceSocket.type != EditorNodeGraph::SocketType::Image &&
            sourceSocket.type != EditorNodeGraph::SocketType::Mask) {
            continue;
        }

        const std::uint64_t dirtyGeneration = GetPreviewNodeRevision(node.id);
        if (dirtyGeneration == 0) {
            continue;
        }
        const std::uint64_t requestedGeneration =
            m_PreviewRequestedGenerations.count(node.id) ? m_PreviewRequestedGenerations[node.id] : 0;
        const std::uint64_t completedGeneration =
            m_PreviewCompletedGenerations.count(node.id) ? m_PreviewCompletedGenerations[node.id] : 0;
        if (requestedGeneration >= dirtyGeneration || completedGeneration >= dirtyGeneration) {
            continue;
        }

        EditorRenderWorker::PreviewRequest request;
        request.previewNodeId = node.id;
        request.sourceNodeId = sourceNodeId;
        request.sourceSocketId = sourceSocketId;
        request.maskInput = sourceSocket.type == EditorNodeGraph::SocketType::Mask;
        request.directSourceOutput = true;
        request.dirtyGeneration = dirtyGeneration;

        if (TryResolveReferenceSourceBuffer(
                sourceNodeId,
                sourceSocketId,
                request.sourcePixels,
                request.width,
                request.height,
                request.channels)) {
            // Preview channel/combined streams on their resolved reference canvas.
        } else if (TryCopyImageNodeSharedPixels(
            sourceNodeId,
            request.sourcePixels,
            request.width,
            request.height,
            request.channels)) {
            // Direct image/RAW source preview.
        } else {
            if (pipelineSourcePixels.empty() && !m_Pipeline.GetSourcePixelsRaw().empty()) {
                pipelineSourcePixels = MakeSharedSourcePixelBufferCopy(m_Pipeline.GetSourcePixelsRaw());
            }
            request.sourcePixels = pipelineSourcePixels;
            request.width = m_Pipeline.GetCanvasWidth();
            request.height = m_Pipeline.GetCanvasHeight();
            request.channels = std::max(1, m_Pipeline.GetSourceChannels());
        }
        if (request.sourcePixels.empty()) {
            if (fallbackImagePixels.empty()) {
                for (const EditorNodeGraph::Node& graphNode : m_NodeGraph.GetNodes()) {
                    if (graphNode.kind != EditorNodeGraph::NodeKind::Image ||
                        graphNode.image.pixels.empty() ||
                        graphNode.image.width <= 0 ||
                        graphNode.image.height <= 0) {
                        continue;
                    }
                    fallbackImagePixels = EnsureSharedImagePixels(graphNode.image);
                    fallbackImageWidth = graphNode.image.width;
                    fallbackImageHeight = graphNode.image.height;
                    fallbackImageChannels = std::max(1, graphNode.image.channels);
                    break;
                }
            }
            request.sourcePixels = fallbackImagePixels;
            request.width = fallbackImageWidth;
            request.height = fallbackImageHeight;
            request.channels = fallbackImageChannels;
        }
        if (request.sourcePixels.empty() || request.width <= 0 || request.height <= 0) {
            request.width = 256;
            request.height = 256;
            request.channels = 4;
            request.sourcePixels = MakeSharedPixelBufferOwned(
                BuildTransparentPixels(request.width, request.height));
        }

        m_PreviewRequestedGenerations[node.id] = dirtyGeneration;
        requests.push_back(std::move(request));
    }
    return requests;
}

void EditorModule::ApplyToneCurveAutoRewriteFeedback(const std::vector<ToneCurveAutoRewriteFeedback>& feedbacks) {
    if (feedbacks.empty()) {
        return;
    }

    std::unordered_map<int, ToneCurveAutoRewriteFeedback> latestByNode;
    latestByNode.reserve(feedbacks.size());
    for (const ToneCurveAutoRewriteFeedback& feedback : feedbacks) {
        if (!feedback.valid || feedback.nodeId <= 0 || feedback.requestRevision == 0) {
            continue;
        }
        latestByNode[feedback.nodeId] = feedback;
    }
    if (latestByNode.empty()) {
        return;
    }

    std::unordered_map<int, std::uint64_t> currentRevisions;
    currentRevisions.reserve(latestByNode.size());
    for (const auto& entry : latestByNode) {
        currentRevisions[entry.first] = GetNodeDirtyGeneration(entry.first);
    }

    bool persistedStateChanged = false;
    for (const auto& entry : latestByNode) {
        const ToneCurveAutoRewriteFeedback& feedback = entry.second;
        const auto currentRevisionIt = currentRevisions.find(feedback.nodeId);
        if (currentRevisionIt == currentRevisions.end() || currentRevisionIt->second != feedback.requestRevision) {
            continue;
        }

        EditorNodeGraph::Node* node = m_NodeGraph.FindNode(feedback.nodeId);
        if (!node) {
            continue;
        }

        if (node->kind == EditorNodeGraph::NodeKind::Layer) {
            if (node->layerIndex < 0 ||
                node->layerIndex >= static_cast<int>(m_Layers.size()) ||
                !m_Layers[node->layerIndex]) {
                continue;
            }

            ToneCurveLayer* toneCurve = dynamic_cast<ToneCurveLayer*>(m_Layers[node->layerIndex].get());
            if (!toneCurve) {
                continue;
            }

            const std::size_t currentLayerHash = HashJsonValue(toneCurve->Serialize());
            toneCurve->ApplyAutoRewriteFeedback(feedback);
            if (currentLayerHash != feedback.authoredStateHash) {
                persistedStateChanged = true;
            }
        } else if (node->kind == EditorNodeGraph::NodeKind::RawDevelop && node->rawDevelop.integratedToneEnabled) {
            ToneCurveLayer integratedTone;
            integratedTone.Deserialize(node->rawDevelop.integratedToneLayerJson);
            const std::size_t currentLayerHash = HashJsonValue(integratedTone.Serialize());
            integratedTone.ApplyAutoRewriteFeedback(feedback);
            node->rawDevelop.integratedToneLayerJson = integratedTone.Serialize();
            if (currentLayerHash != feedback.authoredStateHash) {
                persistedStateChanged = true;
            }
        }
    }

    if (persistedStateChanged) {
        m_Dirty = true;
    }
}

void EditorModule::ApplyDevelopCandidateRenderFeedback(
    const std::vector<EditorRenderWorker::DevelopCandidateRenderResult>& results) {
    if (results.empty()) {
        return;
    }

    std::unordered_map<int, std::vector<const EditorRenderWorker::DevelopCandidateRenderResult*>> resultsByNode;
    for (const EditorRenderWorker::DevelopCandidateRenderResult& result : results) {
        if (result.developNodeId > 0 && result.solveFingerprint != 0) {
            resultsByNode[result.developNodeId].push_back(&result);
        }
    }
    if (resultsByNode.empty()) {
        return;
    }

    bool persistedStateChanged = false;
    for (const auto& entry : resultsByNode) {
        EditorNodeGraph::Node* node = m_NodeGraph.FindNode(entry.first);
        if (!node ||
            node->kind != EditorNodeGraph::NodeKind::RawDevelop ||
            !node->rawDevelop.integratedToneLayerJson.is_object()) {
            continue;
        }

        const double now = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
        const std::uint64_t currentInteractionSerial =
            GetRawDevelopInteractionSerial(node->id);
        bool hasCurrentInteractionResult = false;
        for (const EditorRenderWorker::DevelopCandidateRenderResult* result : entry.second) {
            if (result && result->rawDevelopInteractionSerial == currentInteractionSerial) {
                hasCurrentInteractionResult = true;
                break;
            }
        }
        if (!hasCurrentInteractionResult) {
            continue;
        }

        const auto interactionTimeIt = m_RawDevelopInteractionTimes.find(node->id);
        const double lastInteractionTime =
            interactionTimeIt != m_RawDevelopInteractionTimes.end()
                ? interactionTimeIt->second
                : -1.0;
        const DevelopCandidateFeedbackGateDecision gateDecision =
            ClassifyDevelopCandidateFeedbackGateForValidation(
                currentInteractionSerial,
                currentInteractionSerial,
                lastInteractionTime,
                now);
        if (gateDecision == DevelopCandidateFeedbackGateDecision::DeferRecentInteraction) {
            ScheduleDeferredDevelopCandidateFeedback(node->id, now);
            continue;
        }

        nlohmann::json toneJson = node->rawDevelop.integratedToneLayerJson;
        const std::uint64_t currentRevision = std::max<std::uint64_t>(1, GetNodeDirtyGeneration(node->id));
        const std::uint64_t currentFingerprint =
            toneJson.value("autoCandidateSolveFingerprint", static_cast<std::uint64_t>(0));
        if (currentFingerprint == 0) {
            continue;
        }

        struct RenderedCandidateSummary {
            std::size_t jsonIndex = 0;
            std::string id;
            std::string label;
            std::uint64_t guidanceFingerprint = 0;
            float renderScore = -1.0f;
            float standaloneRenderScore = -1.0f;
            EditorRenderWorker::DevelopCandidateRenderMetrics metrics;
            EditorRenderWorker::DevelopCandidateRenderMetrics preFinishMetrics;
            bool preFinishValid = false;
            std::string activeRefineIntent;
            std::string relativeComparisonStatus = "standalone";
            std::string relativeRepairMetric;
            std::string relativeComparisonReason;
            float relativeMetricDistance = -1.0f;
            float relativeRepairDelta = 0.0f;
            float relativeRepairBonus = 0.0f;
            float relativeRegressionPenalty = 0.0f;
            float relativeDistanceBonus = 0.0f;
            bool damaged = false;
            std::string damageReason;
            bool duplicate = false;
            std::string duplicateOf;
            float duplicateFinalDistance = -1.0f;
            float duplicatePreFinishDistance = -1.0f;
            bool preFinishDistinctFromRepresentative = false;
            std::string preFinishDistinctRepresentativeId;
            float preFinishDistinctFinalDistance = -1.0f;
            float preFinishDistinctDistance = -1.0f;
        };

        nlohmann::json rendered = nlohmann::json::array();
        std::vector<RenderedCandidateSummary> renderedSummaries;
        int successCount = 0;
        int failureCount = 0;
        int preFinishSuccessCount = 0;
        int preFinishReuseCount = 0;
        int metricsReadbackDownsampledCount = 0;
        int preFinishMetricsReadbackDownsampledCount = 0;
        int metricReadbackMaxDimension = 0;
        int rawBaseFinalCacheHitCount = 0;
        int preFinishFinalCacheHitCount = 0;
        int observedRawBaseDirtyCount = 0;
        int observedScenePrepDirtyCount = 0;
        int observedFinishToneDirtyCount = 0;
        int observedUnknownDirtyCount = 0;
        int stageCacheValidationCount = 0;
        int stageCacheValidationMetCount = 0;
        int stageSchedulerCount = 0;
        int stageSchedulerExpectedRawBaseCount = 0;
        int stageSchedulerExpectedScenePrepCount = 0;
        int stageSchedulerExpectedFinishToneCount = 0;
        int stageSchedulerExpectedUnknownCount = 0;
        bool stageSchedulerOrderMonotonic = true;
        int lastStageSchedulerRank = -1;
        int activeStageRequestCount = 0;
        int stageReservedRequestCount = 0;
        int activeRefineIntentRequestCount = 0;
        int refineIntentReservedRequestCount = 0;
        int adaptiveBudgetExpandedRequestCount = 0;
        int adaptiveBudgetNarrowedRequestCount = 0;
        int adaptiveRenderBudget = static_cast<int>(kDefaultDevelopCandidateRenderRequestsPerNode);
        std::string adaptiveRenderBudgetReason = "default";
        std::string adaptiveRenderBudgetContinuationDecision;
        std::string adaptiveRenderBudgetConvergenceState;
        std::string adaptiveRenderBudgetConvergenceDecision;
        std::string adaptiveRenderBudgetConvergenceReason;
        double totalCandidateElapsedMs = 0.0;
        double totalFinalGraphMs = 0.0;
        double totalFinalReadbackMs = 0.0;
        double totalFinalAnalysisMs = 0.0;
        double totalPreFinishGraphMs = 0.0;
        double totalPreFinishReadbackMs = 0.0;
        double totalPreFinishAnalysisMs = 0.0;
        double slowestCandidateElapsedMs = 0.0;
        std::string slowestCandidateLabel;
        int damageCount = 0;
        int duplicateCount = 0;
        int preFinishDistinctSurvivorCount = 0;
        std::string bestCandidateId;
        std::string bestCandidateLabel;
        float bestRenderScore = -1.0f;
        EditorRenderWorker::DevelopCandidateRenderMetrics bestMetrics;
        bool bestMetricsValid = false;
        EditorRenderWorker::DevelopCandidateRenderMetrics bestPreFinishMetrics;
        bool bestPreFinishMetricsValid = false;
        bool renderedPairMergeSuggested = false;
        std::string renderedMergeFirstId;
        std::string renderedMergeFirstLabel;
        float renderedMergeFirstScore = -1.0f;
        std::string renderedMergeSecondId;
        std::string renderedMergeSecondLabel;
        float renderedMergeSecondScore = -1.0f;
        float renderedMergeMetricDistance = 0.0f;
        bool renderedEnsembleMergeSuggested = false;
        nlohmann::json renderedEnsembleMergeIds = nlohmann::json::array();
        nlohmann::json renderedEnsembleMergeLabels = nlohmann::json::array();
        nlohmann::json renderedEnsembleMergeScores = nlohmann::json::array();
        nlohmann::json renderedEnsembleMergeMetricDistances = nlohmann::json::object();
        float renderedEnsembleMergeMetricSpread = 0.0f;
        float renderedEnsembleMergeScoreSpread = 0.0f;
        const std::string selectedCandidateId =
            toneJson.value("autoCandidateSelectedId", std::string());
        float selectedRenderScore = -1.0f;
        bool selectedRenderScoreValid = false;
        float selectedStandaloneRenderScore = -1.0f;
        bool selectedStandaloneRenderScoreValid = false;
        EditorRenderWorker::DevelopCandidateRenderMetrics selectedMetrics;
        bool selectedMetricsValid = false;
        EditorRenderWorker::DevelopCandidateRenderMetrics selectedPreFinishMetrics;
        bool selectedPreFinishMetricsValid = false;
        std::string activeRenderedRepairIntent;
        for (const EditorRenderWorker::DevelopCandidateRenderResult* result : entry.second) {
            if (!result ||
                result->rawDevelopInteractionSerial != currentInteractionSerial ||
                result->dirtyGeneration != currentRevision ||
                result->solveFingerprint != currentFingerprint) {
                continue;
            }

            nlohmann::json item;
            item["id"] = result->candidateId;
            item["label"] = result->candidateLabel;
            item["success"] = result->success;
            item["solveScore"] = result->solveScore;
            item["guidanceFingerprint"] = result->guidanceFingerprint;
            item["candidateRevisionStage"] = result->candidateRevisionStage;
            item["activeRevisionStage"] = result->activeRevisionStage;
            item["activeRefineIntent"] = result->activeRefineIntent;
            item["stageSchedulerOrder"] = result->stageSchedulerOrder;
            item["stageSchedulerRank"] = result->stageSchedulerRank;
            item["adaptiveRenderBudgetVersion"] = result->adaptiveRenderBudgetVersion;
            item["adaptiveRenderBudget"] = result->adaptiveRenderBudget;
            item["adaptiveRenderBudgetReason"] = result->adaptiveRenderBudgetReason;
            item["adaptiveRenderBudgetContinuationDecision"] =
                result->adaptiveRenderBudgetContinuationDecision;
            item["adaptiveRenderBudgetExpanded"] = result->adaptiveRenderBudgetExpanded;
            item["adaptiveRenderBudgetNarrowed"] = result->adaptiveRenderBudgetNarrowed;
            item["adaptiveRenderBudgetConvergenceState"] =
                result->adaptiveRenderBudgetConvergenceState;
            item["adaptiveRenderBudgetConvergenceDecision"] =
                result->adaptiveRenderBudgetConvergenceDecision;
            item["adaptiveRenderBudgetConvergenceReason"] =
                result->adaptiveRenderBudgetConvergenceReason;
            item["stageSchedulerExpectedDirtyBoundary"] =
                result->stageSchedulerExpectedDirtyBoundary;
            item["stageSchedulerReason"] = result->stageSchedulerReason;
            item["activeStageMatch"] = result->activeStageMatch;
            item["stageReservedRequest"] = result->stageReservedRequest;
            item["activeRefineIntentMatch"] = result->activeRefineIntentMatch;
            item["refineIntentReservedRequest"] = result->refineIntentReservedRequest;
            item["width"] = result->width;
            item["height"] = result->height;
            item["preFinishSuccess"] = result->preFinishSuccess;
            item["preFinishWidth"] = result->preFinishWidth;
            item["preFinishHeight"] = result->preFinishHeight;
            item["preFinishReusedFromFinalRender"] = result->preFinishReusedFromFinalRender;
            item["metricReadbackMaxDimension"] = result->metricReadbackMaxDimension;
            item["metricsReadbackDownsampled"] = result->metricsReadbackDownsampled;
            item["preFinishMetricsReadbackDownsampled"] = result->preFinishMetricsReadbackDownsampled;
            item["rawBaseCacheHitDuringFinalRender"] = result->rawBaseCacheHitDuringFinalRender;
            item["preFinishCacheHitDuringFinalRender"] = result->preFinishCacheHitDuringFinalRender;
            item["rawDevelopInteractionSerial"] = result->rawDevelopInteractionSerial;
            item["timingVersion"] = "CandidateRenderTimingV1";
            item["totalElapsedMs"] = result->totalElapsedMs;
            item["finalGraphMs"] = result->finalGraphMs;
            item["finalReadbackMs"] = result->finalReadbackMs;
            item["finalAnalysisMs"] = result->finalAnalysisMs;
            item["preFinishGraphMs"] = result->preFinishGraphMs;
            item["preFinishReadbackMs"] = result->preFinishReadbackMs;
            item["preFinishAnalysisMs"] = result->preFinishAnalysisMs;
            totalCandidateElapsedMs += std::max(0.0f, result->totalElapsedMs);
            totalFinalGraphMs += std::max(0.0f, result->finalGraphMs);
            totalFinalReadbackMs += std::max(0.0f, result->finalReadbackMs);
            totalFinalAnalysisMs += std::max(0.0f, result->finalAnalysisMs);
            totalPreFinishGraphMs += std::max(0.0f, result->preFinishGraphMs);
            totalPreFinishReadbackMs += std::max(0.0f, result->preFinishReadbackMs);
            totalPreFinishAnalysisMs += std::max(0.0f, result->preFinishAnalysisMs);
            if (result->totalElapsedMs > slowestCandidateElapsedMs) {
                slowestCandidateElapsedMs = result->totalElapsedMs;
                slowestCandidateLabel = result->candidateLabel.empty()
                    ? result->candidateId
                    : result->candidateLabel;
            }
            const std::string observedDirtyBoundary = result->success
                ? DevelopObservedDirtyBoundaryFromCacheHits(
                    result->rawBaseCacheHitDuringFinalRender,
                    result->preFinishCacheHitDuringFinalRender)
                : std::string("unknown");
            const DevelopStageCacheValidation stageCacheValidation =
                result->success
                    ? EvaluateDevelopStageCacheValidation(
                        result->candidateRevisionStage,
                        result->rawBaseCacheHitDuringFinalRender,
                        result->preFinishCacheHitDuringFinalRender)
                    : DevelopStageCacheValidation{};
            item["observedDirtyBoundary"] = observedDirtyBoundary;
            item["stageCacheExpectedBoundary"] = stageCacheValidation.expectedBoundary;
            item["stageCacheExpectedRawBaseReuse"] = stageCacheValidation.expectedRawBaseReuse;
            item["stageCacheExpectedPreFinishReuse"] = stageCacheValidation.expectedPreFinishReuse;
            item["stageCacheExpectationEvaluated"] = stageCacheValidation.evaluated;
            item["stageCacheExpectationMet"] =
                stageCacheValidation.evaluated ? stageCacheValidation.met : true;
            item["stageCacheValidationStatus"] = stageCacheValidation.status;
            if (!stageCacheValidation.reason.empty()) {
                item["stageCacheValidationReason"] = stageCacheValidation.reason;
            }
            if (result->activeStageMatch) {
                ++activeStageRequestCount;
            }
            if (result->stageReservedRequest) {
                ++stageReservedRequestCount;
            }
            if (result->activeRefineIntentMatch) {
                ++activeRefineIntentRequestCount;
            }
            if (result->refineIntentReservedRequest) {
                ++refineIntentReservedRequestCount;
            }
            adaptiveRenderBudget = std::max(
                adaptiveRenderBudget,
                result->adaptiveRenderBudget);
            if (result->adaptiveRenderBudgetExpanded) {
                ++adaptiveBudgetExpandedRequestCount;
                if (adaptiveRenderBudgetReason == "default" ||
                    adaptiveRenderBudgetReason.empty()) {
                    adaptiveRenderBudgetReason = result->adaptiveRenderBudgetReason;
                }
            } else if (adaptiveRenderBudgetReason == "default" &&
                !result->adaptiveRenderBudgetReason.empty()) {
                adaptiveRenderBudgetReason = result->adaptiveRenderBudgetReason;
            }
            if (adaptiveRenderBudgetContinuationDecision.empty() &&
                !result->adaptiveRenderBudgetContinuationDecision.empty()) {
                adaptiveRenderBudgetContinuationDecision =
                    result->adaptiveRenderBudgetContinuationDecision;
            }
            if (result->adaptiveRenderBudgetNarrowed) {
                ++adaptiveBudgetNarrowedRequestCount;
                adaptiveRenderBudgetReason = result->adaptiveRenderBudgetReason;
            }
            if (adaptiveRenderBudgetConvergenceState.empty() &&
                !result->adaptiveRenderBudgetConvergenceState.empty()) {
                adaptiveRenderBudgetConvergenceState =
                    result->adaptiveRenderBudgetConvergenceState;
            }
            if (adaptiveRenderBudgetConvergenceDecision.empty() &&
                !result->adaptiveRenderBudgetConvergenceDecision.empty()) {
                adaptiveRenderBudgetConvergenceDecision =
                    result->adaptiveRenderBudgetConvergenceDecision;
            }
            if (adaptiveRenderBudgetConvergenceReason.empty() &&
                !result->adaptiveRenderBudgetConvergenceReason.empty()) {
                adaptiveRenderBudgetConvergenceReason =
                    result->adaptiveRenderBudgetConvergenceReason;
            }
            ++stageSchedulerCount;
            if (result->stageSchedulerRank < lastStageSchedulerRank) {
                stageSchedulerOrderMonotonic = false;
            }
            lastStageSchedulerRank = result->stageSchedulerRank;
            if (result->stageSchedulerExpectedDirtyBoundary == "rawBase") {
                ++stageSchedulerExpectedRawBaseCount;
            } else if (result->stageSchedulerExpectedDirtyBoundary == "scenePrep") {
                ++stageSchedulerExpectedScenePrepCount;
            } else if (result->stageSchedulerExpectedDirtyBoundary == "finishTone") {
                ++stageSchedulerExpectedFinishToneCount;
            } else {
                ++stageSchedulerExpectedUnknownCount;
            }
            if (result->success) {
                ++successCount;
                metricReadbackMaxDimension =
                    std::max(metricReadbackMaxDimension, result->metricReadbackMaxDimension);
                if (result->metricsReadbackDownsampled) {
                    ++metricsReadbackDownsampledCount;
                }
                if (observedDirtyBoundary == "rawBase") {
                    ++observedRawBaseDirtyCount;
                } else if (observedDirtyBoundary == "scenePrep") {
                    ++observedScenePrepDirtyCount;
                } else if (observedDirtyBoundary == "finishTone") {
                    ++observedFinishToneDirtyCount;
                } else {
                    ++observedUnknownDirtyCount;
                }
                if (stageCacheValidation.evaluated) {
                    ++stageCacheValidationCount;
                    if (stageCacheValidation.met) {
                        ++stageCacheValidationMetCount;
                    }
                }
                if (result->rawBaseCacheHitDuringFinalRender) {
                    ++rawBaseFinalCacheHitCount;
                }
                if (result->preFinishCacheHitDuringFinalRender) {
                    ++preFinishFinalCacheHitCount;
                }
                if (result->preFinishSuccess) {
                    ++preFinishSuccessCount;
                    if (result->preFinishMetricsReadbackDownsampled) {
                        ++preFinishMetricsReadbackDownsampledCount;
                    }
                    if (result->preFinishReusedFromFinalRender) {
                        ++preFinishReuseCount;
                    }
                }
                const float renderScore = ScoreDevelopRenderedCandidateMetrics(result->metrics, result->solveScore);
                item["standaloneRenderScore"] = renderScore;
                item["renderScore"] = renderScore;
                item["metrics"] = DevelopCandidateRenderMetricsToJson(result->metrics);
                item["renderedStatus"] = "candidate";
                if (result->candidateId == selectedCandidateId) {
                    selectedRenderScore = renderScore;
                    selectedRenderScoreValid = true;
                    selectedStandaloneRenderScore = renderScore;
                    selectedStandaloneRenderScoreValid = true;
                    selectedMetrics = result->metrics;
                    selectedMetricsValid = true;
                    if (result->preFinishSuccess) {
                        selectedPreFinishMetrics = result->preFinishMetrics;
                        selectedPreFinishMetricsValid = true;
                    }
                }
                RenderedCandidateSummary summary;
                summary.jsonIndex = rendered.size();
                summary.id = result->candidateId;
                summary.label = result->candidateLabel;
                summary.guidanceFingerprint = result->guidanceFingerprint;
                summary.renderScore = renderScore;
                summary.standaloneRenderScore = renderScore;
                summary.metrics = result->metrics;
                summary.activeRefineIntent = result->activeRefineIntent;
                if (activeRenderedRepairIntent.empty() && !result->activeRefineIntent.empty()) {
                    activeRenderedRepairIntent = result->activeRefineIntent;
                }
                if (result->preFinishSuccess) {
                    summary.preFinishMetrics = result->preFinishMetrics;
                    summary.preFinishValid = true;
                    item["preFinishMetrics"] =
                        DevelopCandidateRenderMetricsToJson(result->preFinishMetrics);
                    item["finalVsPreFinishMetricDistance"] =
                        EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
                            result->metrics,
                            result->preFinishMetrics);
                }
                summary.damageReason = ClassifyDevelopRenderedCandidateDamage(
                    result->metrics,
                    node->rawDevelop.autoGuidance.intent);
                summary.damaged = !summary.damageReason.empty();
                renderedSummaries.push_back(std::move(summary));
            } else {
                ++failureCount;
                item["error"] = result->error;
            }
            rendered.push_back(std::move(item));
        }

        if (rendered.empty()) {
            continue;
        }

        if (activeRenderedRepairIntent.empty()) {
            activeRenderedRepairIntent =
                toneJson.value("autoCandidateRenderedRefineIntent", std::string());
        }
        if (selectedMetricsValid && selectedStandaloneRenderScoreValid) {
            for (RenderedCandidateSummary& summary : renderedSummaries) {
                const DevelopRenderedRelativeComparison comparison =
                    CompareDevelopRenderedCandidateToSelected(
                        summary.metrics,
                        summary.standaloneRenderScore,
                        selectedMetrics,
                        selectedStandaloneRenderScore,
                        activeRenderedRepairIntent,
                        summary.id == selectedCandidateId);
                summary.renderScore = comparison.adjustedScore;
                summary.relativeComparisonStatus = comparison.status;
                summary.relativeRepairMetric = comparison.repairMetric;
                summary.relativeComparisonReason = comparison.reason;
                summary.relativeMetricDistance = comparison.metricDistance;
                summary.relativeRepairDelta = comparison.repairDelta;
                summary.relativeRepairBonus = comparison.repairBonus;
                summary.relativeRegressionPenalty = comparison.regressionPenalty;
                summary.relativeDistanceBonus = comparison.distanceBonus;

                rendered[summary.jsonIndex]["renderScore"] = summary.renderScore;
                rendered[summary.jsonIndex]["relativeComparisonStatus"] =
                    summary.relativeComparisonStatus;
                rendered[summary.jsonIndex]["relativeRepairMetric"] =
                    summary.relativeRepairMetric;
                rendered[summary.jsonIndex]["relativeMetricDistance"] =
                    summary.relativeMetricDistance;
                rendered[summary.jsonIndex]["relativeRepairDelta"] =
                    summary.relativeRepairDelta;
                rendered[summary.jsonIndex]["relativeRepairBonus"] =
                    summary.relativeRepairBonus;
                rendered[summary.jsonIndex]["relativeRegressionPenalty"] =
                    summary.relativeRegressionPenalty;
                rendered[summary.jsonIndex]["relativeDistanceBonus"] =
                    summary.relativeDistanceBonus;
                rendered[summary.jsonIndex]["relativeComparisonReason"] =
                    summary.relativeComparisonReason;
                if (summary.id == selectedCandidateId) {
                    selectedRenderScore = summary.renderScore;
                    selectedRenderScoreValid = true;
                }
            }
        }

        std::vector<std::size_t> renderedOrder(renderedSummaries.size());
        for (std::size_t index = 0; index < renderedOrder.size(); ++index) {
            renderedOrder[index] = index;
        }
        std::sort(renderedOrder.begin(), renderedOrder.end(), [&](std::size_t a, std::size_t b) {
            return renderedSummaries[a].renderScore > renderedSummaries[b].renderScore;
        });

        std::vector<std::size_t> representativeIndices;
        for (std::size_t summaryIndex : renderedOrder) {
            RenderedCandidateSummary& candidate = renderedSummaries[summaryIndex];
            if (candidate.damaged) {
                ++damageCount;
                rendered[candidate.jsonIndex]["renderedStatus"] = "renderedRejectedDamage";
                rendered[candidate.jsonIndex]["rejectReason"] = candidate.damageReason;
                continue;
            }
            for (std::size_t representativeIndex : representativeIndices) {
                const RenderedCandidateSummary& representative = renderedSummaries[representativeIndex];
                const DevelopRenderedDuplicateDecision duplicateDecision =
                    EvaluateDevelopRenderedCandidateDuplicate(
                        candidate.metrics,
                        representative.metrics,
                        candidate.preFinishValid,
                        candidate.preFinishMetrics,
                        representative.preFinishValid,
                        representative.preFinishMetrics);
                if (duplicateDecision.duplicate) {
                    candidate.duplicate = true;
                    candidate.duplicateOf = representative.id;
                    candidate.duplicateFinalDistance = duplicateDecision.finalDistance;
                    candidate.duplicatePreFinishDistance = duplicateDecision.preFinishDistance;
                    break;
                }
                if (duplicateDecision.preFinishDistinct &&
                    duplicateDecision.preFinishDistance > candidate.preFinishDistinctDistance) {
                    candidate.preFinishDistinctFromRepresentative = true;
                    candidate.preFinishDistinctRepresentativeId = representative.id;
                    candidate.preFinishDistinctFinalDistance = duplicateDecision.finalDistance;
                    candidate.preFinishDistinctDistance = duplicateDecision.preFinishDistance;
                }
            }
            if (candidate.duplicate) {
                ++duplicateCount;
                rendered[candidate.jsonIndex]["renderedStatus"] = "renderedDuplicate";
                rendered[candidate.jsonIndex]["duplicateOf"] = candidate.duplicateOf;
                rendered[candidate.jsonIndex]["duplicateFinalMetricDistance"] =
                    candidate.duplicateFinalDistance;
                rendered[candidate.jsonIndex]["duplicatePreFinishMetricDistance"] =
                    candidate.duplicatePreFinishDistance;
                rendered[candidate.jsonIndex]["duplicateReason"] =
                    "Rendered metrics are too similar to a higher-scoring candidate.";
            } else {
                representativeIndices.push_back(summaryIndex);
                rendered[candidate.jsonIndex]["renderedStatus"] = "survivor";
                if (candidate.preFinishDistinctFromRepresentative) {
                    ++preFinishDistinctSurvivorCount;
                    rendered[candidate.jsonIndex]["preFinishDistinctFrom"] =
                        candidate.preFinishDistinctRepresentativeId;
                    rendered[candidate.jsonIndex]["preFinishDistinctFinalMetricDistance"] =
                        candidate.preFinishDistinctFinalDistance;
                    rendered[candidate.jsonIndex]["preFinishDistinctMetricDistance"] =
                        candidate.preFinishDistinctDistance;
                    rendered[candidate.jsonIndex]["preFinishDistinctReason"] =
                        "Final rendered metrics were near-duplicate, but the pre-finish boundary differed enough to preserve this candidate for stage-aware feedback.";
                }
            }
        }

        nlohmann::json renderedRejectionMemory =
            toneJson.value("autoCandidateRenderedRejectionMemory", nlohmann::json::array());
        if (!renderedRejectionMemory.is_array()) {
            renderedRejectionMemory = nlohmann::json::array();
        }
        auto rememberRenderedRejection = [&](const RenderedCandidateSummary& candidate) {
            if (candidate.guidanceFingerprint == 0 || candidate.id.empty()) {
                return;
            }
            for (auto it = renderedRejectionMemory.begin(); it != renderedRejectionMemory.end();) {
                if (it->is_object() &&
                    it->value("guidanceFingerprint", static_cast<std::uint64_t>(0)) == candidate.guidanceFingerprint &&
                    it->value("id", std::string()) == candidate.id) {
                    it = renderedRejectionMemory.erase(it);
                } else {
                    ++it;
                }
            }
            renderedRejectionMemory.push_back({
                { "id", candidate.id },
                { "label", candidate.label },
                { "guidanceFingerprint", candidate.guidanceFingerprint },
                { "status", "renderedRejectedDamage" },
                { "reason", candidate.damageReason },
                { "renderScore", candidate.renderScore },
                { "solveFingerprint", currentFingerprint },
                { "revision", currentRevision }
            });
        };
        for (const RenderedCandidateSummary& candidate : renderedSummaries) {
            if (candidate.damaged) {
                rememberRenderedRejection(candidate);
            }
        }
        constexpr std::size_t kMaxRenderedRejectionMemoryEntries = 24;
        while (renderedRejectionMemory.size() > kMaxRenderedRejectionMemoryEntries) {
            renderedRejectionMemory.erase(renderedRejectionMemory.begin());
        }

        for (const RenderedCandidateSummary& summary : renderedSummaries) {
            if (summary.damaged || summary.duplicate) {
                continue;
            }
            if (summary.renderScore > bestRenderScore) {
                bestRenderScore = summary.renderScore;
                bestCandidateId = summary.id;
                bestCandidateLabel = summary.label;
                bestMetrics = summary.metrics;
                bestMetricsValid = true;
                bestPreFinishMetrics = summary.preFinishMetrics;
                bestPreFinishMetricsValid = summary.preFinishValid;
            }
        }
        const RenderedCandidateSummary* bestSummary = nullptr;
        for (const RenderedCandidateSummary& summary : renderedSummaries) {
            if (!bestCandidateId.empty() && summary.id == bestCandidateId) {
                bestSummary = &summary;
                break;
            }
        }

        if (representativeIndices.size() >= 2) {
            const RenderedCandidateSummary& first = renderedSummaries[representativeIndices[0]];
            const RenderedCandidateSummary& second = renderedSummaries[representativeIndices[1]];
            renderedMergeMetricDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(first.metrics, second.metrics);
            const float scoreGap = std::fabs(first.renderScore - second.renderScore);
            renderedPairMergeSuggested =
                first.renderScore >= 0.58f &&
                second.renderScore >= 0.54f &&
                scoreGap <= 0.16f &&
                renderedMergeMetricDistance >= 0.11f;
            if (renderedPairMergeSuggested) {
                renderedMergeFirstId = first.id;
                renderedMergeFirstLabel = first.label;
                renderedMergeFirstScore = first.renderScore;
                renderedMergeSecondId = second.id;
                renderedMergeSecondLabel = second.label;
                renderedMergeSecondScore = second.renderScore;
                rendered[first.jsonIndex]["renderedMergeRole"] = "first";
                rendered[second.jsonIndex]["renderedMergeRole"] = "second";
            }
        }

        if (representativeIndices.size() >= 3) {
            const RenderedCandidateSummary& first = renderedSummaries[representativeIndices[0]];
            const RenderedCandidateSummary& second = renderedSummaries[representativeIndices[1]];
            const RenderedCandidateSummary& third = renderedSummaries[representativeIndices[2]];
            const float firstSecondDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(first.metrics, second.metrics);
            const float firstThirdDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(first.metrics, third.metrics);
            const float secondThirdDistance =
                EditorRenderWorker::CompareDevelopCandidateRenderMetrics(second.metrics, third.metrics);
            renderedEnsembleMergeMetricSpread =
                (firstSecondDistance + firstThirdDistance + secondThirdDistance) / 3.0f;
            renderedEnsembleMergeScoreSpread =
                std::max(first.renderScore, std::max(second.renderScore, third.renderScore)) -
                std::min(first.renderScore, std::min(second.renderScore, third.renderScore));
            const int distinctMetricPairCount =
                (firstSecondDistance >= 0.08f ? 1 : 0) +
                (firstThirdDistance >= 0.08f ? 1 : 0) +
                (secondThirdDistance >= 0.08f ? 1 : 0);
            renderedEnsembleMergeSuggested =
                first.renderScore >= 0.60f &&
                second.renderScore >= 0.56f &&
                third.renderScore >= 0.52f &&
                renderedEnsembleMergeScoreSpread <= 0.22f &&
                renderedEnsembleMergeMetricSpread >= 0.10f &&
                distinctMetricPairCount >= 2;
            if (renderedEnsembleMergeSuggested) {
                renderedEnsembleMergeIds = nlohmann::json::array({ first.id, second.id, third.id });
                renderedEnsembleMergeLabels = nlohmann::json::array({ first.label, second.label, third.label });
                renderedEnsembleMergeScores =
                    nlohmann::json::array({ first.renderScore, second.renderScore, third.renderScore });
                renderedEnsembleMergeMetricDistances = {
                    { "firstSecond", firstSecondDistance },
                    { "firstThird", firstThirdDistance },
                    { "secondThird", secondThirdDistance }
                };
                rendered[first.jsonIndex]["renderedMergeRole"] = "ensemble1";
                rendered[second.jsonIndex]["renderedMergeRole"] = "ensemble2";
                rendered[third.jsonIndex]["renderedMergeRole"] = "ensemble3";
            }
        }

        float selectedBestFinalMetricDistance = -1.0f;
        float selectedBestPreFinishMetricDistance = -1.0f;
        const bool selectedBestFinalMetricsValid =
            selectedMetricsValid && bestMetricsValid && !selectedCandidateId.empty() && !bestCandidateId.empty();
        const bool selectedBestPreFinishMetricsValid =
            selectedPreFinishMetricsValid && bestPreFinishMetricsValid && selectedBestFinalMetricsValid;
        const std::string stageBoundarySignal =
            ClassifyDevelopRenderedStageBoundary(
                selectedMetrics,
                bestMetrics,
                selectedBestFinalMetricsValid,
                selectedPreFinishMetrics,
                bestPreFinishMetrics,
                selectedBestPreFinishMetricsValid,
                selectedBestFinalMetricDistance,
                selectedBestPreFinishMetricDistance);

        const std::size_t beforeHash = HashJsonValue(toneJson);
        toneJson["autoCandidateRenderedVersion"] = "RenderMetricsV1";
        toneJson["autoCandidateRenderedFingerprint"] = currentFingerprint;
        toneJson["autoCandidateRenderedAtRevision"] = currentRevision;
        toneJson["autoCandidateRenderedSolves"] = std::move(rendered);
        toneJson["autoCandidateRenderedCount"] = successCount;
        toneJson["autoCandidateRenderedPreFinishCount"] = preFinishSuccessCount;
        toneJson["autoCandidateRenderedPreFinishReuseCount"] = preFinishReuseCount;
        toneJson["autoCandidateRenderedPreFinishReuseStatus"] =
            preFinishSuccessCount <= 0
                ? "none"
                : (preFinishReuseCount >= preFinishSuccessCount ? "all" : (preFinishReuseCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedMetricReadbackMaxDimension"] = metricReadbackMaxDimension;
        toneJson["autoCandidateRenderedMetricReadbackCapped"] =
            metricsReadbackDownsampledCount > 0 || preFinishMetricsReadbackDownsampledCount > 0;
        toneJson["autoCandidateRenderedMetricReadbackDownsampledCount"] =
            metricsReadbackDownsampledCount;
        toneJson["autoCandidateRenderedPreFinishMetricReadbackDownsampledCount"] =
            preFinishMetricsReadbackDownsampledCount;
        toneJson["autoCandidateRenderedRawBaseFinalCacheHitCount"] = rawBaseFinalCacheHitCount;
        toneJson["autoCandidateRenderedPreFinishFinalCacheHitCount"] = preFinishFinalCacheHitCount;
        toneJson["autoCandidateRenderedRawBaseFinalCacheHitStatus"] =
            successCount <= 0
                ? "none"
                : (rawBaseFinalCacheHitCount >= successCount ? "all" : (rawBaseFinalCacheHitCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedPreFinishFinalCacheHitStatus"] =
            successCount <= 0
                ? "none"
                : (preFinishFinalCacheHitCount >= successCount ? "all" : (preFinishFinalCacheHitCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedObservedDirtyBoundaryCounts"] = {
            { "rawBase", observedRawBaseDirtyCount },
            { "scenePrep", observedScenePrepDirtyCount },
            { "finishTone", observedFinishToneDirtyCount },
            { "unknown", observedUnknownDirtyCount }
        };
        toneJson["autoCandidateRenderedStageSchedulerVersion"] = "StageSchedulerV1";
        toneJson["autoCandidateRenderedStageSchedulerCount"] = stageSchedulerCount;
        toneJson["autoCandidateRenderedStageSchedulerStatus"] =
            stageSchedulerCount <= 0
                ? "none"
                : (stageSchedulerOrderMonotonic ? "ordered" : "outOfOrder");
        toneJson["autoCandidateRenderedStageSchedulerExpectedBoundaryCounts"] = {
            { "rawBase", stageSchedulerExpectedRawBaseCount },
            { "scenePrep", stageSchedulerExpectedScenePrepCount },
            { "finishTone", stageSchedulerExpectedFinishToneCount },
            { "unknown", stageSchedulerExpectedUnknownCount }
        };
        toneJson["autoCandidateRenderedStageCacheValidationCount"] = stageCacheValidationCount;
        toneJson["autoCandidateRenderedStageCacheValidationMetCount"] = stageCacheValidationMetCount;
        toneJson["autoCandidateRenderedStageCacheValidationMissCount"] =
            std::max(0, stageCacheValidationCount - stageCacheValidationMetCount);
        toneJson["autoCandidateRenderedStageCacheValidationStatus"] =
            stageCacheValidationCount <= 0
                ? "none"
                : (stageCacheValidationMetCount >= stageCacheValidationCount
                    ? "all"
                    : (stageCacheValidationMetCount > 0 ? "partial" : "missed"));
        toneJson["autoCandidateRenderedActiveStageRequestCount"] = activeStageRequestCount;
        toneJson["autoCandidateRenderedStageReservedRequestCount"] = stageReservedRequestCount;
        toneJson["autoCandidateRenderedActiveRefineIntentRequestCount"] = activeRefineIntentRequestCount;
        toneJson["autoCandidateRenderedRefineIntentReservedRequestCount"] = refineIntentReservedRequestCount;
        toneJson["autoCandidateRenderedAdaptiveBudgetVersion"] =
            kDevelopAdaptiveRenderBudgetVersion;
        toneJson["autoCandidateRenderedAdaptiveBudget"] = adaptiveRenderBudget;
        toneJson["autoCandidateRenderedAdaptiveBudgetDefault"] =
            static_cast<int>(kDefaultDevelopCandidateRenderRequestsPerNode);
        toneJson["autoCandidateRenderedAdaptiveBudgetMax"] =
            static_cast<int>(kMaxDevelopCandidateRenderRequestsPerNode);
        toneJson["autoCandidateRenderedAdaptiveBudgetExpanded"] =
            adaptiveBudgetExpandedRequestCount > 0;
        toneJson["autoCandidateRenderedAdaptiveBudgetExpandedRequestCount"] =
            adaptiveBudgetExpandedRequestCount;
        toneJson["autoCandidateRenderedAdaptiveBudgetNarrowed"] =
            adaptiveBudgetNarrowedRequestCount > 0;
        toneJson["autoCandidateRenderedAdaptiveBudgetNarrowedRequestCount"] =
            adaptiveBudgetNarrowedRequestCount;
        toneJson["autoCandidateRenderedAdaptiveBudgetReason"] =
            adaptiveRenderBudgetReason;
        toneJson["autoCandidateRenderedAdaptiveBudgetContinuationDecision"] =
            adaptiveRenderBudgetContinuationDecision;
        toneJson["autoCandidateRenderedAdaptiveBudgetConvergenceState"] =
            adaptiveRenderBudgetConvergenceState;
        toneJson["autoCandidateRenderedAdaptiveBudgetConvergenceDecision"] =
            adaptiveRenderBudgetConvergenceDecision;
        toneJson["autoCandidateRenderedAdaptiveBudgetConvergenceReason"] =
            adaptiveRenderBudgetConvergenceReason;
        toneJson["autoCandidateRenderedTimingVersion"] = "CandidateRenderTimingV1";
        toneJson["autoCandidateRenderedTotalElapsedMs"] = totalCandidateElapsedMs;
        toneJson["autoCandidateRenderedFinalGraphMs"] = totalFinalGraphMs;
        toneJson["autoCandidateRenderedFinalReadbackMs"] = totalFinalReadbackMs;
        toneJson["autoCandidateRenderedFinalAnalysisMs"] = totalFinalAnalysisMs;
        toneJson["autoCandidateRenderedPreFinishGraphMs"] = totalPreFinishGraphMs;
        toneJson["autoCandidateRenderedPreFinishReadbackMs"] = totalPreFinishReadbackMs;
        toneJson["autoCandidateRenderedPreFinishAnalysisMs"] = totalPreFinishAnalysisMs;
        toneJson["autoCandidateRenderedSlowestElapsedMs"] = slowestCandidateElapsedMs;
        toneJson["autoCandidateRenderedSlowestLabel"] = slowestCandidateLabel;
        toneJson["autoCandidateRenderedRejectionMemory"] = std::move(renderedRejectionMemory);
        toneJson["autoCandidateRenderedRejectionMemoryMaxEntries"] =
            static_cast<int>(kMaxRenderedRejectionMemoryEntries);
        toneJson["autoCandidateRenderedUniqueCount"] =
            std::max(0, successCount - duplicateCount - damageCount);
        toneJson["autoCandidateRenderedDamageCount"] = damageCount;
        toneJson["autoCandidateRenderedDuplicateCount"] = duplicateCount;
        toneJson["autoCandidateRenderedDuplicateDistance"] = kDevelopRenderedDuplicateDistance;
        toneJson["autoCandidateRenderedPreFinishDistinctDistance"] =
            kDevelopRenderedPreFinishDistinctDistance;
        toneJson["autoCandidateRenderedPreFinishDistinctSurvivorCount"] =
            preFinishDistinctSurvivorCount;
        toneJson["autoCandidateRenderedFailureCount"] = failureCount;
        toneJson["autoCandidateRenderedBestId"] = bestCandidateId;
        toneJson["autoCandidateRenderedBestLabel"] = bestCandidateLabel;
        toneJson["autoCandidateRenderedBestScore"] = std::max(0.0f, bestRenderScore);
        toneJson["autoCandidateRenderedRelativeComparisonVersion"] =
            selectedMetricsValid ? "RenderedRelativeComparisonV1" : "notApplied";
        toneJson["autoCandidateRenderedActiveRepairIntent"] = activeRenderedRepairIntent;
        toneJson["autoCandidateRenderedSelectedStandaloneScore"] =
            selectedStandaloneRenderScoreValid ? selectedStandaloneRenderScore : -1.0f;
        toneJson["autoCandidateRenderedBestStandaloneScore"] =
            bestSummary ? bestSummary->standaloneRenderScore : -1.0f;
        toneJson["autoCandidateRenderedBestRelativeStatus"] =
            bestSummary ? bestSummary->relativeComparisonStatus : std::string();
        toneJson["autoCandidateRenderedBestRelativeRepairMetric"] =
            bestSummary ? bestSummary->relativeRepairMetric : std::string();
        toneJson["autoCandidateRenderedBestRelativeMetricDistance"] =
            bestSummary ? bestSummary->relativeMetricDistance : -1.0f;
        toneJson["autoCandidateRenderedBestRelativeRepairDelta"] =
            bestSummary ? bestSummary->relativeRepairDelta : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeRepairBonus"] =
            bestSummary ? bestSummary->relativeRepairBonus : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeRegressionPenalty"] =
            bestSummary ? bestSummary->relativeRegressionPenalty : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeDistanceBonus"] =
            bestSummary ? bestSummary->relativeDistanceBonus : 0.0f;
        toneJson["autoCandidateRenderedBestRelativeReason"] =
            bestSummary ? bestSummary->relativeComparisonReason : std::string();
        toneJson["autoCandidateRenderedMergeSuggested"] = renderedPairMergeSuggested;
        toneJson["autoCandidateRenderedMergeFirstId"] = renderedMergeFirstId;
        toneJson["autoCandidateRenderedMergeFirstLabel"] = renderedMergeFirstLabel;
        toneJson["autoCandidateRenderedMergeFirstScore"] = renderedMergeFirstScore;
        toneJson["autoCandidateRenderedMergeSecondId"] = renderedMergeSecondId;
        toneJson["autoCandidateRenderedMergeSecondLabel"] = renderedMergeSecondLabel;
        toneJson["autoCandidateRenderedMergeSecondScore"] = renderedMergeSecondScore;
        toneJson["autoCandidateRenderedMergeMetricDistance"] = renderedMergeMetricDistance;
        toneJson["autoCandidateRenderedEnsembleMergeSuggested"] = renderedEnsembleMergeSuggested;
        toneJson["autoCandidateRenderedEnsembleMergeIds"] = std::move(renderedEnsembleMergeIds);
        toneJson["autoCandidateRenderedEnsembleMergeLabels"] = std::move(renderedEnsembleMergeLabels);
        toneJson["autoCandidateRenderedEnsembleMergeScores"] = std::move(renderedEnsembleMergeScores);
        toneJson["autoCandidateRenderedEnsembleMergeMetricDistances"] =
            std::move(renderedEnsembleMergeMetricDistances);
        toneJson["autoCandidateRenderedEnsembleMergeMetricSpread"] =
            renderedEnsembleMergeMetricSpread;
        toneJson["autoCandidateRenderedEnsembleMergeScoreSpread"] =
            renderedEnsembleMergeScoreSpread;
        toneJson["autoCandidateRenderedStageBoundarySignal"] = stageBoundarySignal;
        toneJson["autoCandidateRenderedSelectedBestMetricDistance"] = selectedBestFinalMetricDistance;
        toneJson["autoCandidateRenderedSelectedBestPreFinishDistance"] = selectedBestPreFinishMetricDistance;
        toneJson["autoCandidateRenderedSelectedBestPreFinishValid"] = selectedBestPreFinishMetricsValid;
        toneJson["autoCandidateRenderMetricsStatus"] =
            successCount <= 0 ? "failed" : (failureCount > 0 ? "partial" : "ready");
        toneJson["autoCandidateGalleryStatus"] = "deferred";
        std::string feedbackAction = "stopped";
        std::string stopReason = "unknown";
        std::string refineIntent;
        std::string refineReason;
        std::string preFinishRefineIntent;
        std::string preFinishRefineReason;
        if (selectedMetricsValid) {
            refineIntent = ResolveDevelopRenderedRefineIntent(
                selectedMetrics,
                node->rawDevelop.autoGuidance.intent,
                refineReason);
        }
        if (selectedPreFinishMetricsValid) {
            preFinishRefineIntent = ResolveDevelopRenderedRefineIntent(
                selectedPreFinishMetrics,
                node->rawDevelop.autoGuidance.intent,
                preFinishRefineReason);
            if (!preFinishRefineIntent.empty() && preFinishRefineIntent != "addContrast") {
                refineIntent = preFinishRefineIntent;
                refineReason = preFinishRefineReason +
                    " Detected before finish tone, so feedback should revise the earlier responsible stage.";
            }
        }
        const std::string repeatedRefineStopReason =
            RepeatedRenderedRefinementStopReason(
                toneJson,
                refineIntent,
                selectedRenderScore,
                selectedRenderScoreValid);
        bool renderedFeedbackWorthTrying = false;
        bool renderedFeedbackRefineRequested = false;
        const int currentRenderedFeedbackPass =
            toneJson.value("autoCandidateRenderedFeedbackPass", 0);
        const bool bestRelativeRegression =
            bestSummary &&
            (bestSummary->relativeComparisonStatus == "regressedAgainstSelected" ||
             bestSummary->relativeComparisonStatus == "missedActiveRepair") &&
            bestSummary->relativeRegressionPenalty >
                bestSummary->relativeRepairBonus + bestSummary->relativeDistanceBonus + 0.012f;
        if (successCount <= 0) {
            feedbackAction = "failed";
            stopReason = "candidateRendersFailed";
        } else if (bestCandidateId.empty()) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = damageCount > 0
                    ? "allRenderedCandidatesRejectedForDamage"
                    : "noRenderedBestCandidate";
            }
        } else if (selectedCandidateId.empty()) {
            stopReason = "noSelectedCandidate";
        } else if (currentRenderedFeedbackPass >= kDevelopRenderedFeedbackMaxPasses) {
            stopReason = "renderedFeedbackPassLimit";
        } else if (toneJson.value(
                "autoCandidateRenderedFeedbackAppliedFingerprint",
                static_cast<std::uint64_t>(0)) == currentFingerprint) {
            stopReason = "renderedFeedbackAlreadyApplied";
        } else if (!repeatedRefineStopReason.empty() &&
            (bestCandidateId == selectedCandidateId ||
             bestRenderScore < 0.48f ||
             (selectedRenderScoreValid && bestRenderScore < selectedRenderScore + 0.025f))) {
            stopReason = repeatedRefineStopReason;
        } else if (bestCandidateId == selectedCandidateId) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = "selectedCandidateStillBest";
            }
        } else if (bestRenderScore < 0.48f) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = "renderedBestBelowQualityFloor";
            }
        } else if (selectedRenderScoreValid && bestRenderScore < selectedRenderScore + 0.025f) {
            if (!refineIntent.empty()) {
                feedbackAction = "solveRequested";
                stopReason = "renderedSelectedNeedsRefinement";
                renderedFeedbackWorthTrying = true;
                renderedFeedbackRefineRequested = true;
            } else {
                stopReason = "noMeaningfulRenderedImprovement";
            }
        } else if (bestRelativeRegression &&
            selectedRenderScoreValid &&
            bestRenderScore < selectedRenderScore + 0.055f) {
            stopReason = "renderedBestRelativeRegression";
        } else if (WouldReverseRecentRenderedAdoption(toneJson, selectedCandidateId, bestCandidateId)) {
            stopReason = "wouldReverseRecentRenderedAdoption";
        } else {
            feedbackAction = "solveRequested";
            stopReason = "renderedBestImproved";
            renderedFeedbackWorthTrying = true;
        }
        const bool renderedStopConverged =
            !renderedFeedbackWorthTrying &&
            successCount > 0 &&
            IsDevelopRenderedFeedbackStopConvergedReason(stopReason);
        std::string revisionStage =
            renderedFeedbackWorthTrying ? "multiStage" : (renderedStopConverged ? "converged" : "none");
        std::string revisionReason = stopReason;
        if (renderedFeedbackWorthTrying) {
            if (renderedFeedbackRefineRequested) {
                revisionStage = DevelopRenderedRevisionStageForRefineIntent(refineIntent);
                revisionReason = refineReason.empty() ? stopReason : refineReason;
            } else {
                if (stageBoundarySignal == "finishToneOnly") {
                    revisionStage = "finishTone";
                    revisionReason =
                        "Final rendered metrics changed while pre-finish metrics stayed close, so the follow-up solve should validate finish tone rather than rerouting upstream stages.";
                } else {
                    revisionStage = DevelopRenderedRevisionStageForCandidateId(bestCandidateId);
                    revisionReason =
                        "Rendered metrics selected a stronger survivor; the follow-up solve should validate the earliest changed authored stage.";
                }
            }
        }
        toneJson["autoCandidateRenderedRefineIntent"] =
            renderedFeedbackRefineRequested ? refineIntent : std::string();
        toneJson["autoCandidateRenderedRefineReason"] =
            renderedFeedbackRefineRequested ? refineReason : std::string();
        toneJson["autoCandidateRenderedPreFinishRefineIntent"] = preFinishRefineIntent;
        toneJson["autoCandidateRenderedPreFinishRefineReason"] = preFinishRefineReason;
        toneJson["autoCandidateRenderedRevisionStage"] = revisionStage;
        toneJson["autoCandidateRenderedRevisionReason"] = revisionReason;
        toneJson["autoCandidateRenderedConvergenceStatus"] =
            renderedFeedbackWorthTrying ? "renderedFeedbackSolveRequested" : feedbackAction;
        toneJson["autoCandidateRenderedStopReason"] = stopReason;
        toneJson["autoCandidateRenderedSelectedScore"] =
            selectedRenderScoreValid ? selectedRenderScore : -1.0f;
        toneJson["autoCandidateRenderedSelectedScoreValid"] = selectedRenderScoreValid;
        toneJson["autoCandidateRenderedConverged"] = renderedStopConverged;
        const int nextRenderedFeedbackPass = renderedFeedbackWorthTrying
            ? std::min(currentRenderedFeedbackPass + 1, kDevelopRenderedFeedbackMaxPasses)
            : currentRenderedFeedbackPass;
        const float renderedImprovement =
            selectedRenderScoreValid ? bestRenderScore - selectedRenderScore : -1.0f;
        const nlohmann::json continuationPolicy =
            BuildDevelopRenderedContinuationPolicyRecord(
                renderedFeedbackWorthTrying ? "continue" : "stop",
                stopReason,
                renderedFeedbackWorthTrying ? "applyAutoSolve" : "none",
                renderedFeedbackWorthTrying,
                false,
                currentRenderedFeedbackPass,
                nextRenderedFeedbackPass,
                revisionStage,
                revisionReason,
                renderedImprovement,
                stageBoundarySignal,
                bestSummary ? bestSummary->relativeComparisonStatus : std::string(),
                successCount,
                failureCount);
        toneJson["autoCandidateRenderedContinuationVersion"] =
            kDevelopRenderedContinuationVersion;
        toneJson["autoCandidateRenderedContinuationPolicy"] = continuationPolicy;
        AppendDevelopCandidateRenderedFeedbackHistory(
            toneJson,
            currentFingerprint,
            selectedCandidateId,
            selectedRenderScore,
            selectedRenderScoreValid,
            bestCandidateId,
            bestRenderScore,
            successCount,
            failureCount,
            feedbackAction,
            stopReason,
            renderedFeedbackRefineRequested ? refineIntent : std::string(),
            renderedFeedbackRefineRequested ? refineReason : std::string(),
            selectedMetricsValid ? &selectedMetrics : nullptr,
            bestMetricsValid ? &bestMetrics : nullptr);
        const std::string renderedLoopState = renderedFeedbackWorthTrying
            ? std::string("solveRequested")
            : (successCount <= 0
                ? std::string("failed")
                : (renderedStopConverged ? std::string("converged") : std::string("stopped")));
        WriteDevelopCandidateRenderedFeedbackLoopRecord(
            toneJson,
            currentFingerprint,
            currentRevision,
            renderedLoopState,
            feedbackAction,
            stopReason,
            renderedFeedbackWorthTrying ? std::string("applyAutoSolve") : std::string("none"),
            renderedFeedbackWorthTrying,
            false,
            selectedCandidateId,
            selectedRenderScore,
            selectedRenderScoreValid,
            bestCandidateId,
            bestRenderScore,
            successCount,
            failureCount);

        const std::size_t afterHash = HashJsonValue(toneJson);
        if (beforeHash != afterHash) {
            node->rawDevelop.integratedToneLayerJson = std::move(toneJson);
            persistedStateChanged = true;
        }
        if (renderedFeedbackWorthTrying) {
            const EditorNodeGraph::Node* rawSource =
                FindUpstreamRawSourceForDevelopNode(m_NodeGraph, *node);
            if (rawSource && rawSource->kind == EditorNodeGraph::NodeKind::RawSource) {
                if (UpdateDevelopAutoState(
                        node->id,
                        node->rawDevelop,
                        rawSource->rawSource.metadata,
                        true,
                        true)) {
                    MarkRenderDirty(node->id);
                    persistedStateChanged = true;
                }
            }
        }
    }

    if (persistedStateChanged) {
        m_Dirty = true;
    }
}

void EditorModule::ConsumeRenderWorkerResults() {
    auto releaseDeferredResultResources = [](EditorRenderWorker::Result& result) {
        if (result.outputTexture.readyFence) {
            glDeleteSync(result.outputTexture.readyFence);
            result.outputTexture.readyFence = nullptr;
        }
        if (result.outputTexture.texture != 0) {
            glDeleteTextures(1, &result.outputTexture.texture);
            result.outputTexture.texture = 0;
        }
    };
    auto deferResultUntilReady = [&](EditorRenderWorker::Result&& result) {
        for (EditorRenderWorker::Result& pending : m_DeferredRenderResults) {
            releaseDeferredResultResources(pending);
        }
        m_DeferredRenderResults.clear();
        m_DeferredRenderResults.push_back(std::move(result));
    };

    std::deque<EditorRenderWorker::Result> resultsToProcess;
    while (!m_DeferredRenderResults.empty()) {
        resultsToProcess.push_back(std::move(m_DeferredRenderResults.front()));
        m_DeferredRenderResults.pop_front();
    }

    EditorRenderWorker::Result result;
    while (m_RenderWorker.TryConsumeCompleted(result)) {
        resultsToProcess.push_back(std::move(result));
    }

    while (!resultsToProcess.empty()) {
        EditorRenderWorker::Result result = std::move(resultsToProcess.front());
        resultsToProcess.pop_front();

        if (result.outputTexture.texture != 0) {
            bool fenceFailed = false;
            if (!PollSharedTextureFence(result.outputTexture.readyFence, fenceFailed)) {
                if (fenceFailed) {
                    glDeleteTextures(1, &result.outputTexture.texture);
                    result.outputTexture.texture = 0;
                } else {
                    deferResultUntilReady(std::move(result));
                    continue;
                }
            }
        }
        if (result.generation < m_RenderGeneration) {
            if (result.outputTexture.texture != 0) {
                glDeleteTextures(1, &result.outputTexture.texture);
                result.outputTexture.texture = 0;
            }
            continue;
        }
        const auto submittedIt = m_HdrMergeSubmittedNodesByGeneration.find(result.generation);
        const std::vector<int> activeHdrMergeNodeIds =
            submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()
                ? submittedIt->second
                : std::vector<int>{};
        m_RenderPending = m_RenderWorkerAvailable && m_RenderWorker.IsBusy();
        m_HdrMergeRenderingNodeIds.clear();
        m_GraphPerformanceStats.lastMainRenderMs = result.mainRenderMs;
        m_GraphPerformanceStats.lastPreviewRenderMs = result.previewRenderMs;
        m_GraphPerformanceStats.lastCompositeRenderMs = result.compositeRenderMs;
        m_GraphPerformanceStats.lastRenderedPreviewCount = result.renderedPreviewCount;
        m_GraphPerformanceStats.lastRenderedCompositeCount = result.renderedCompositeCount;
        m_GraphPerformanceStats.lastMainGraphStats = result.mainGraphStats;
        if (result.success && result.outputTexture.texture != 0) {
            m_Pipeline.AdoptExternalOutputTexture(
                result.outputTexture.texture,
                result.outputTexture.width,
                result.outputTexture.height);
            result.outputTexture.texture = 0;
            m_LastCompletedRenderGeneration = result.generation;
            for (int nodeId : activeHdrMergeNodeIds) {
                m_HdrMergeCompletedGenerations[nodeId] = std::max(
                    m_HdrMergeCompletedGenerations[nodeId],
                    m_HdrMergeRequestedGenerations.count(nodeId) ? m_HdrMergeRequestedGenerations[nodeId] : GetNodeDirtyGeneration(nodeId));
                m_HdrMergeFailureMessages.erase(nodeId);
            }
        } else if (!m_NodeGraph.IsOutputConnected()) {
            m_Pipeline.ClearOutput();
        } else if (!activeHdrMergeNodeIds.empty()) {
            m_Pipeline.ClearOutput();
            const std::string baseMessage = result.error.empty() ? "Render failed" : result.error;
            for (int nodeId : activeHdrMergeNodeIds) {
                const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
                const std::string nodeName = (node && !node->title.empty())
                    ? node->title
                    : std::string("HDR Merge");
                m_HdrMergeFailureMessages[nodeId] = baseMessage;
                QueueUiNotification(
                    UiNotificationSeverity::Error,
                    nodeName + ": " + baseMessage,
                    "hdr-merge-render-failed-" + std::to_string(nodeId));
            }
        }
        ApplyToneCurveAutoRewriteFeedback(result.toneCurveAutoRewrites);
        ApplyDevelopCandidateRenderFeedback(result.developCandidateRenders);
        if (submittedIt != m_HdrMergeSubmittedNodesByGeneration.end()) {
            m_HdrMergeSubmittedNodesByGeneration.erase(submittedIt);
        }
        for (auto it = m_HdrMergeSubmittedNodesByGeneration.begin(); it != m_HdrMergeSubmittedNodesByGeneration.end();) {
            if (it->first < result.generation) {
                it = m_HdrMergeSubmittedNodesByGeneration.erase(it);
            } else {
                ++it;
            }
        }

        for (const EditorRenderWorker::CompositeOutputResult& compositeResult : result.compositeOutputs) {
            CompositeSceneItem* item = FindCompositeSceneItem(compositeResult.outputNodeId);
            if (!item) {
                continue;
            }
            if (!compositeResult.success || compositeResult.pixels.empty() || compositeResult.width <= 0 || compositeResult.height <= 0) {
                continue;
            }

            const bool scalableGenerator = CompletedChainSourceUsesScalableGenerator(compositeResult.outputNodeId);
            const bool keepFullRasterFrame = CompletedChainSourceKeepsFullRasterFrame(compositeResult.outputNodeId);
            const bool hadRasterBeforeUpload = item->textureWidth > 0 && item->textureHeight > 0;
            const float previousDrawWidth = static_cast<float>(item->textureWidth) * std::max(0.01f, std::abs(item->scale.x));
            const float previousDrawHeight = static_cast<float>(item->textureHeight) * std::max(0.01f, std::abs(item->scale.y));
            const CroppedRgbaImage cropped = (scalableGenerator && keepFullRasterFrame)
                ? CroppedRgbaImage{}
                : CropToAlphaBounds(
                    compositeResult.pixels,
                    compositeResult.width,
                    compositeResult.height,
                    scalableGenerator ? 2 : 0);
            const std::vector<unsigned char>& uploadPixels = (scalableGenerator && keepFullRasterFrame)
                ? compositeResult.pixels
                : (!cropped.pixels.empty() ? cropped.pixels : compositeResult.pixels);
            const int uploadW = (scalableGenerator && keepFullRasterFrame)
                ? compositeResult.width
                : (cropped.width > 0 ? cropped.width : compositeResult.width);
            const int uploadH = (scalableGenerator && keepFullRasterFrame)
                ? compositeResult.height
                : (cropped.height > 0 ? cropped.height : compositeResult.height);
            if (item->texture != 0) {
                glDeleteTextures(1, &item->texture);
                item->texture = 0;
            }
            item->texture = GLHelpers::CreateTextureFromPixels(uploadPixels.data(), uploadW, uploadH, 4);
            item->textureWidth = uploadW;
            item->textureHeight = uploadH;
            item->rgbaPixels = uploadPixels;
            item->cachedRenderRevision = compositeResult.dirtyGeneration;
            item->cachedChainFingerprint = compositeResult.chainFingerprint;
            item->requestedRasterWidth = uploadW;
            item->requestedRasterHeight = uploadH;
            m_CompositeOutputCompletedGenerations[compositeResult.outputNodeId] = compositeResult.dirtyGeneration;
        }

        for (const EditorRenderWorker::PreviewResult& previewResult : result.previews) {
            if (!previewResult.success ||
                previewResult.pixels.empty() ||
                previewResult.width <= 0 ||
                previewResult.height <= 0) {
                if (!previewResult.error.empty()) {
                    QueueUiNotification(
                        UiNotificationSeverity::Error,
                        previewResult.error,
                        "editor-preview-failed-" + std::to_string(previewResult.previewNodeId));
                }
                continue;
            }
            const std::uint64_t desiredRevision = GetPreviewNodeRevision(previewResult.previewNodeId);
            if (previewResult.dirtyGeneration < desiredRevision) {
                continue;
            }

            GraphPreviewPixels cached;
            cached.pixels = previewResult.pixels;
            cached.width = previewResult.width;
            cached.height = previewResult.height;
            cached.revision = previewResult.dirtyGeneration;
            m_PreviewPixelCache[previewResult.previewNodeId] = std::move(cached);
            m_PreviewCompletedGenerations[previewResult.previewNodeId] = previewResult.dirtyGeneration;
            m_PreviewDisplayedRevisions[previewResult.previewNodeId] = previewResult.dirtyGeneration;
        }
    }
}

void EditorModule::SubmitRenderIfReady() {
    const bool compositeMode = GetViewportMode() == ViewportMode::CompositeCanvas;
    if (!m_RenderWorkerAvailable && !m_RenderDirty) {
        return;
    }
    const double now = ImGui::GetTime();
    RefreshDeferredDevelopCandidateFeedbackIfReady(now);
    const bool recentRawDevelopInteraction = IsRecentRawDevelopInteraction(now);
    const bool localGraphEdit =
        !m_GraphPerformanceStats.lastInvalidationWasFull &&
        m_GraphPerformanceStats.lastTouchedNodeId > 0;
    const double renderSubmitDelay = (recentRawDevelopInteraction || localGraphEdit) ? 0.006 : 0.02;
    if (m_RenderDirty && now - m_LastRenderDirtyTime < renderSubmitDelay) {
        return;
    }
    const bool workerBusy = m_RenderWorkerAvailable && (m_RenderPending || m_RenderWorker.IsBusy());
    m_GraphPerformanceStats.lastPreviewRequestBuildMs = 0.0f;
    m_GraphPerformanceStats.lastCompositeRequestBuildMs = 0.0f;
    std::vector<EditorRenderWorker::PreviewRequest> previewRequests;
    if (m_RenderWorkerAvailable && !workerBusy && !ShouldDeferPreviewLikeWork(now)) {
        const auto previewBuildBegin = std::chrono::steady_clock::now();
        previewRequests = BuildPreviewRequests();
        m_GraphPerformanceStats.lastPreviewRequestBuildMs =
            MillisecondsBetween(previewBuildBegin, std::chrono::steady_clock::now());
    }
    if (!m_RenderDirty && previewRequests.empty()) {
        return;
    }
    // A newer single-output edit should be allowed to replace stale in-flight
    // background Develop feedback instead of waiting for every old probe to
    // drain. The worker checks the pending generation at safe GL boundaries.
    if (!compositeMode && m_RenderPending && !m_RenderDirty) {
        return;
    }

    const int activeOutputNodeId = m_NodeGraph.ResolvePreviewOutputNodeId();
    const std::vector<int> activeHdrMergeNodeIds =
        (!compositeMode && activeOutputNodeId > 0) ? CollectHdrMergeNodesForOutput(activeOutputNodeId) : std::vector<int>{};
    const auto submitPreviewOnlyRequests = [&](std::vector<EditorRenderWorker::PreviewRequest>& requests) {
        if (requests.empty() || !m_RenderWorkerAvailable) {
            m_RenderPending = false;
            return;
        }
        ++m_RenderGeneration;
        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.outputConnected = false;
        snapshot.sourcePixels = {};
        snapshot.developCandidateRenders.clear();
        snapshot.previews = std::move(requests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        m_RenderPending = true;
        m_RenderWorker.Submit(std::move(snapshot));
    };
    const auto recordHdrMergeSubmission = [&]() {
        m_HdrMergeRenderingNodeIds.clear();
        for (int nodeId : activeHdrMergeNodeIds) {
            m_HdrMergeRequestedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeFailureMessages.erase(nodeId);
            m_HdrMergeRenderingNodeIds.insert(nodeId);
        }
        m_HdrMergeSubmittedNodesByGeneration[m_RenderGeneration] = activeHdrMergeNodeIds;
    };
    const auto blockInvalidHdrMergeOutput = [&]() -> bool {
        if (activeHdrMergeNodeIds.empty()) {
            return false;
        }

        bool blocked = false;
        for (int nodeId : activeHdrMergeNodeIds) {
            const HdrMergeNodeStatus status = GetHdrMergeNodeStatus(nodeId);
            if (status.state != HdrMergeRenderState::BlockedMissingInput &&
                status.state != HdrMergeRenderState::IncompatibleInput) {
                continue;
            }
            const EditorNodeGraph::Node* node = m_NodeGraph.FindNode(nodeId);
            const std::string nodeName = (node && !node->title.empty())
                ? node->title
                : std::string("HDR Merge");
            QueueUiNotification(
                UiNotificationSeverity::Error,
                nodeName + ": " + status.message,
                "hdr-merge-invalid-" + std::to_string(nodeId));
            blocked = true;
        }
        if (!blocked) {
            return false;
        }

        m_RenderDirty = false;
        m_RenderPending = false;
        m_Pipeline.ClearOutput();
        m_HdrMergeRenderingNodeIds.clear();
        submitPreviewOnlyRequests(previewRequests);
        return true;
    };

    if (compositeMode) {
        const auto compositeBuildBegin = std::chrono::steady_clock::now();
        std::vector<EditorRenderWorker::CompositeOutputRequest> requests = BuildCompositeOutputRequests();
        m_GraphPerformanceStats.lastCompositeRequestBuildMs =
            MillisecondsBetween(compositeBuildBegin, std::chrono::steady_clock::now());
        if (requests.empty() && previewRequests.empty()) {
            m_RenderDirty = false;
            if (!m_RenderWorkerAvailable || !m_RenderWorker.IsBusy()) {
                m_RenderPending = false;
            }
            return;
        }

        ++m_RenderGeneration;
        m_LastSubmittedRenderRevision = m_RenderRevision;
        m_RenderDirty = false;
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(previewRequests.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = static_cast<int>(requests.size());
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;

        if (m_RenderWorkerAvailable) {
            const auto snapshotBuildBegin = std::chrono::steady_clock::now();
            EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
            m_GraphPerformanceStats.lastSnapshotBuildMs =
                MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
            snapshot.outputConnected = false;
            snapshot.compositeOutputs = std::move(requests);
            snapshot.previews = std::move(previewRequests);
            m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
            m_GraphPerformanceStats.lastSubmittedCompositeCount = static_cast<int>(snapshot.compositeOutputs.size());
            m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = false;
            m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
            m_RenderPending = true;
            m_RenderWorker.Submit(std::move(snapshot));
        } else {
            m_RenderPending = false;
            for (const EditorRenderWorker::CompositeOutputRequest& request : requests) {
                int texW = 0;
                int texH = 0;
                std::vector<unsigned char> pixels = GetCompositePixelsForOutputNode(request.outputNodeId, texW, texH);
                CompositeSceneItem* item = FindCompositeSceneItem(request.outputNodeId);
                if (!item || pixels.empty() || texW <= 0 || texH <= 0) {
                    continue;
                }
                const bool scalableGenerator = CompletedChainSourceUsesScalableGenerator(request.outputNodeId);
                const bool keepFullRasterFrame = CompletedChainSourceKeepsFullRasterFrame(request.outputNodeId);
                const bool hadRasterBeforeUpload = item->textureWidth > 0 && item->textureHeight > 0;
                const float previousDrawWidth = static_cast<float>(item->textureWidth) * std::max(0.01f, std::abs(item->scale.x));
                const float previousDrawHeight = static_cast<float>(item->textureHeight) * std::max(0.01f, std::abs(item->scale.y));
                const CroppedRgbaImage cropped =
                    (scalableGenerator && keepFullRasterFrame) ? CroppedRgbaImage{} : CropToAlphaBounds(pixels, texW, texH, scalableGenerator ? 2 : 0);
                const std::vector<unsigned char>& uploadPixels =
                    (scalableGenerator && keepFullRasterFrame) ? pixels : (!cropped.pixels.empty() ? cropped.pixels : pixels);
                const int uploadW =
                    (scalableGenerator && keepFullRasterFrame) ? texW : (cropped.width > 0 ? cropped.width : texW);
                const int uploadH =
                    (scalableGenerator && keepFullRasterFrame) ? texH : (cropped.height > 0 ? cropped.height : texH);
                if (item->texture != 0) {
                    glDeleteTextures(1, &item->texture);
                    item->texture = 0;
                }
                item->texture = GLHelpers::CreateTextureFromPixels(uploadPixels.data(), uploadW, uploadH, 4);
                item->textureWidth = uploadW;
                item->textureHeight = uploadH;
                item->rgbaPixels = uploadPixels;
                item->cachedRenderRevision = request.dirtyGeneration;
                item->cachedChainFingerprint = request.chainFingerprint;
                item->requestedRasterWidth = uploadW;
                item->requestedRasterHeight = uploadH;
                m_CompositeOutputCompletedGenerations[request.outputNodeId] = request.dirtyGeneration;
            }
        }
        return;
    }
    if (!m_RenderDirty && !previewRequests.empty()) {
        submitPreviewOnlyRequests(previewRequests);
        return;
    }
    if (!m_NodeGraph.IsOutputConnected()) {
        m_RenderDirty = false;
        m_Pipeline.ClearOutput();
        submitPreviewOnlyRequests(previewRequests);
        m_HdrMergeRenderingNodeIds.clear();
        return;
    }
    if (m_RenderRevision <= m_LastSubmittedRenderRevision) {
        return;
    }
    if (blockInvalidHdrMergeOutput()) {
        return;
    }

    ++m_RenderGeneration;
    m_LastSubmittedRenderRevision = m_RenderRevision;
    m_RenderDirty = false;
    m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(previewRequests.size());
    m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
    m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
    m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;

    if (m_RenderWorkerAvailable) {
        recordHdrMergeSubmission();
        m_RenderPending = true;
        const auto snapshotBuildBegin = std::chrono::steady_clock::now();
        EditorRenderWorker::Snapshot snapshot = BuildRenderSnapshot(m_RenderGeneration);
        m_GraphPerformanceStats.lastSnapshotBuildMs =
            MillisecondsBetween(snapshotBuildBegin, std::chrono::steady_clock::now());
        snapshot.previews = std::move(previewRequests);
        m_GraphPerformanceStats.lastSubmittedPreviewCount = static_cast<int>(snapshot.previews.size());
        m_GraphPerformanceStats.lastSubmittedCompositeCount = 0;
        m_GraphPerformanceStats.lastSubmissionIncludedMainOutput = true;
        m_GraphPerformanceStats.lastSubmittedGeneration = m_RenderGeneration;
        m_RenderWorker.Submit(std::move(snapshot));
    } else {
        RenderGraphSnapshot graphSnapshot = BuildGraphSnapshot();
        if (GetViewportMode() == ViewportMode::SingleOutputPreview &&
            m_ActiveSubWindow == EditorSubWindow::ComplexNode &&
            m_ActiveComplexNodeId == m_AutoGainMaskPreviewNodeId &&
            m_AutoGainMaskPreviewNodeId > 0 &&
            m_NodeGraph.FindNode(m_AutoGainMaskPreviewNodeId) &&
            m_NodeGraph.FindNode(m_AutoGainMaskPreviewNodeId)->kind == EditorNodeGraph::NodeKind::RawDetailFusion) {
            graphSnapshot.outputNodeId = m_AutoGainMaskPreviewNodeId;
            graphSnapshot.outputSocketId = EditorNodeGraph::kMaskOutputSocketId;
            graphSnapshot.autoGainMaskPreview = true;
        }
        const auto mainRenderBegin = std::chrono::steady_clock::now();
        m_Pipeline.ExecuteGraph(graphSnapshot);
        m_GraphPerformanceStats.lastMainRenderMs =
            MillisecondsBetween(mainRenderBegin, std::chrono::steady_clock::now());
        m_GraphPerformanceStats.lastMainGraphStats = m_Pipeline.GetLastGraphExecutionStats();
        m_GraphPerformanceStats.lastPreviewRenderMs = 0.0f;
        m_GraphPerformanceStats.lastCompositeRenderMs = 0.0f;
        m_GraphPerformanceStats.lastRenderedPreviewCount = 0;
        m_GraphPerformanceStats.lastRenderedCompositeCount = 0;
        ApplyToneCurveAutoRewriteFeedback(m_Pipeline.GetToneCurveAutoRewriteFeedback());
        for (int nodeId : activeHdrMergeNodeIds) {
            m_HdrMergeRequestedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeCompletedGenerations[nodeId] = GetNodeDirtyGeneration(nodeId);
            m_HdrMergeFailureMessages.erase(nodeId);
        }
        m_HdrMergeRenderingNodeIds.clear();
        m_LastCompletedRenderGeneration = m_RenderGeneration;
    }
}
