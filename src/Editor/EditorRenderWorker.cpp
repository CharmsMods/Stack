#include "Renderer/RawCandidateRenderer.h"
#include "Editor/CompositePixels.h"
#include "EditorRenderWorker.h"

#include "Editor/Internal/EditorRenderWorkerScheduling.h"
#include "Editor/Internal/EditorRenderWorkerTileGraph.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/RawRenderGraphOverlay.h"
#include "Project/RawLayerStackSnapshot.h"
#include "Raw/RawAutoBase.h"
#include "Renderer/Internal/RenderPipelineGraphSchedule.h"
#include "Renderer/RawDevelopmentStageCachePolicy.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLLoader.h"
#include "Utils/PixelBufferUtils.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <sstream>
#include <stdexcept>

namespace {

constexpr int kRawWorkspaceGraphScopeMaxDimension = 192;
constexpr int kRawWorkspaceGradingScopeMaxDimension = 192;

std::size_t ResolveRawGraphScopeFingerprint(
    const EditorRenderWorker::RawWorkspaceSnapshot& workspace) {
    if (!workspace.hasRecipe) return 0;
    if (workspace.graphScopeInputFingerprint != 0) {
        return workspace.graphScopeInputFingerprint;
    }
    using Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint;
    using Stack::Renderer::RawDevelopmentCache::Stage;
    // Scope packets are reduced to their own fixed sampling size. The
    // interactive preview can change resolution several times during a drag,
    // but that does not change the image distribution represented here.
    const int scopeSamplingDimension = std::clamp(
        workspace.graphScopeMaxDimension,
        kRawWorkspaceGraphScopeMaxDimension,
        2048);
    switch (workspace.graphScopeStage) {
        case RawDevelopmentGraphScopeStage::LocalRangeInput:
            return BuildStageFingerprint(
                workspace.recipe,
                scopeSamplingDimension,
                Stage::RawPlacement);
        case RawDevelopmentGraphScopeStage::FinishToneInput:
            return BuildStageFingerprint(
                workspace.recipe,
                scopeSamplingDimension,
                Stage::PostLocalRange);
        case RawDevelopmentGraphScopeStage::ColorWarpInput:
            return BuildStageFingerprint(
                workspace.recipe,
                scopeSamplingDimension,
                Stage::PostFinishTone);
        case RawDevelopmentGraphScopeStage::None:
        default:
            return 0;
    }
}

void ReleaseSharedTexture(EditorRenderWorker::SharedTextureResult& texture) {
    texture.Reset();
}

std::string RenderFailureReason(const RenderPipeline& pipeline, const char* fallback) {
    const auto& graphError = pipeline.GetLastGraphExecutionStats().lastSpecializedFailure;
    if (!graphError.empty()) return graphError;
    const auto& denoiseError = pipeline.GetLastRawRgbDenoiseError();
    return denoiseError.empty() ? fallback : denoiseError;
}

bool FenceSharedTexture(
    EditorRenderWorker::SharedTextureResult& texture,
    Raw::RawGpuImageFamily family =
        Raw::RawGpuImageFamily::Presentation) {
    if (texture.texture == 0 || !texture.EnsureLease(family)) return false;
    texture.readyFence =
        glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (texture.readyFence == nullptr) {
        ReleaseSharedTexture(texture);
        return false;
    }
    glFlush();
    return true;
}

GLsync CreateSharedTextureFence() {
    GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (fence != nullptr) {
        glFlush();
    }
    return fence;
}

void ReleaseResultResources(EditorRenderWorker::Result& result) {
    ReleaseSharedTexture(result.outputTexture);
    ReleaseSharedTexture(result.rawWorkspace.localRangeOverlayTexture);
    if (result.outputTiles.readyFence) {
        glDeleteSync(result.outputTiles.readyFence);
        result.outputTiles.readyFence = nullptr;
    }
    for (EditorRenderWorker::SharedTextureTile& tile : result.outputTiles.tiles) {
        if (tile.texture != 0) {
            glDeleteTextures(1, &tile.texture);
            tile.texture = 0;
        }
    }
    result.outputTiles.tiles.clear();
}

int EstimateRenderProgressStepCount(const EditorRenderWorker::Snapshot& snapshot) {
    int steps = 0;
    if (!snapshot.compositeOutputs.empty()) {
        steps += static_cast<int>(snapshot.compositeOutputs.size());
    } else if (snapshot.outputConnected ||
        (snapshot.rawSessionTemplate &&
         snapshot.rawSessionTemplate->outputConnected)) {
        steps += 1;
    }
    steps += static_cast<int>(snapshot.developCandidateRenders.size());
    steps += static_cast<int>(snapshot.previews.size());
    if (snapshot.rawWorkspace.preciseSolveRequest.has_value()) {
        steps += 52;
    }
    return std::max(1, steps);
}

std::string TrimProgressText(std::string text, std::size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }
    if (maxLength <= 3) {
        return text.substr(0, maxLength);
    }
    return text.substr(0, maxLength - 3) + "...";
}

std::string BuildDevelopCandidateProgressLabel(
    const std::string& candidateLabel,
    const std::string& candidateRevisionStage,
    int candidateIndex,
    int candidateCount) {
    const int visibleIndex = std::max(1, candidateIndex + 1);
    const int visibleCount = std::max(visibleIndex, candidateCount);
    std::string label =
        "Measuring Develop feedback " +
        std::to_string(visibleIndex) +
        "/" +
        std::to_string(visibleCount);
    if (!candidateLabel.empty()) {
        label += ": " + TrimProgressText(candidateLabel, 48);
    }
    if (!candidateRevisionStage.empty()) {
        label += " [" + TrimProgressText(candidateRevisionStage, 24) + "]";
    }
    label += "...";
    return label;
}

float MillisecondsBetween(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

void FlipRgbaRowsInPlace(std::vector<unsigned char>& pixels, int width, int height) {
    if (pixels.empty() || width <= 0 || height <= 1) {
        return;
    }
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
    const std::size_t expectedBytes = rowBytes * static_cast<std::size_t>(height);
    if (pixels.size() < expectedBytes) {
        return;
    }

    std::vector<unsigned char> tempRow(rowBytes);
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() + static_cast<std::size_t>(y) * rowBytes;
        unsigned char* bottom = pixels.data() + static_cast<std::size_t>(height - 1 - y) * rowBytes;
        std::copy_n(top, rowBytes, tempRow.data());
        std::copy_n(bottom, rowBytes, top);
        std::copy_n(tempRow.data(), rowBytes, bottom);
    }
}

bool CaptureMainOutputPixelsForUiUpload(RenderPipeline& pipeline, EditorRenderWorker::Result& result) {
    result.pixels = pipeline.GetOutputPixels(result.width, result.height);
    if (result.pixels.empty() || result.width <= 0 || result.height <= 0) {
        result.pixels.clear();
        result.width = 0;
        result.height = 0;
        return false;
    }

    // GetOutputPixels returns top-left row order for file/export consumers. The
    // viewport upload path expects the same bottom-left row order as GL output.
    FlipRgbaRowsInPlace(result.pixels, result.width, result.height);
    return true;
}

void CaptureRawWorkspaceLocalRangeOverlay(RenderPipeline& pipeline, EditorRenderWorker::Result& result) {
    ReleaseSharedTexture(result.rawWorkspace.localRangeOverlayTexture);
    result.rawWorkspace.localRangeTargetPreviewRefined =
        pipeline.IsRawDevelopmentLocalRangeTargetPreviewRefined();
    result.rawWorkspace.localRangeTargetPreviewRefinementPending =
        pipeline.IsRawDevelopmentLocalRangeTargetPreviewRefinementPending();
    result.rawWorkspace.localRangeTargetPreviewMetrics =
        pipeline.GetRawDevelopmentLocalRangeTargetPreviewMetrics();
    result.rawWorkspace.localRangeOverlayWidth = 0;
    result.rawWorkspace.localRangeOverlayHeight = 0;
    if (result.rawWorkspace.sourceKey.empty() ||
        result.rawWorkspace.localRangeOverlayMode.empty() ||
        result.rawWorkspace.localRangeOverlayMode == "none") {
        return;
    }

    result.rawWorkspace.localRangeOverlayTexture.texture =
        pipeline.TakeRawDevelopmentLocalRangeOverlayTexture(
            result.rawWorkspace.localRangeOverlayWidth,
            result.rawWorkspace.localRangeOverlayHeight);
    result.rawWorkspace.localRangeOverlayTexture.width =
        result.rawWorkspace.localRangeOverlayWidth;
    result.rawWorkspace.localRangeOverlayTexture.height =
        result.rawWorkspace.localRangeOverlayHeight;
    if (result.rawWorkspace.localRangeOverlayTexture.texture == 0 ||
        result.rawWorkspace.localRangeOverlayWidth <= 0 ||
        result.rawWorkspace.localRangeOverlayHeight <= 0) {
        ReleaseSharedTexture(result.rawWorkspace.localRangeOverlayTexture);
        result.rawWorkspace.localRangeOverlayWidth = 0;
        result.rawWorkspace.localRangeOverlayHeight = 0;
        return;
    }
    if (!FenceSharedTexture(
            result.rawWorkspace.localRangeOverlayTexture,
            Raw::RawGpuImageFamily::AuxiliaryOverlay)) {
        result.rawWorkspace.localRangeOverlayWidth = 0;
        result.rawWorkspace.localRangeOverlayHeight = 0;
    }
}

void CaptureRawWorkspaceLocalRangeTargetSample(RenderPipeline& pipeline, EditorRenderWorker::Result& result) {
    result.rawWorkspace.localRangeTargetSample.valid = false;
    result.rawWorkspace.localRangeTargetSample.sceneEv = 0.0f;
    result.rawWorkspace.localRangeTargetSample.sceneLuma = 0.0f;
    result.rawWorkspace.localRangeTargetSample.sceneR = 0.0f;
    result.rawWorkspace.localRangeTargetSample.sceneG = 0.0f;
    result.rawWorkspace.localRangeTargetSample.sceneB = 0.0f;
    result.rawWorkspace.localRangeTargetSample.u = 0.0f;
    result.rawWorkspace.localRangeTargetSample.v = 0.0f;
    result.rawWorkspace.localRangeTargetSample.authoredZoneHitBits = 0;
    result.rawWorkspace.localRangeTargetSample.strongestAuthoredZoneWeight = 0.0f;
    if (result.rawWorkspace.sourceKey.empty()) {
        return;
    }

    float sceneEv = 0.0f;
    float sceneLuma = 0.0f;
    float sampleU = 0.0f;
    float sampleV = 0.0f;
    std::array<float, 3> sceneRgb = { 0.0f, 0.0f, 0.0f };
    std::uint32_t authoredZoneHitBits = 0;
    float strongestAuthoredZoneWeight = 0.0f;
    if (!pipeline.GetRawDevelopmentLocalRangeTargetSample(
            sceneEv,
            sceneLuma,
            sampleU,
            sampleV,
            &sceneRgb,
            &authoredZoneHitBits,
            &strongestAuthoredZoneWeight)) {
        return;
    }

    result.rawWorkspace.localRangeTargetSample.valid = true;
    result.rawWorkspace.localRangeTargetSample.sceneEv = sceneEv;
    result.rawWorkspace.localRangeTargetSample.sceneLuma = sceneLuma;
    result.rawWorkspace.localRangeTargetSample.sceneR = sceneRgb[0];
    result.rawWorkspace.localRangeTargetSample.sceneG = sceneRgb[1];
    result.rawWorkspace.localRangeTargetSample.sceneB = sceneRgb[2];
    result.rawWorkspace.localRangeTargetSample.u = sampleU;
    result.rawWorkspace.localRangeTargetSample.v = sampleV;
    result.rawWorkspace.localRangeTargetSample.authoredZoneHitBits =
        authoredZoneHitBits;
    result.rawWorkspace.localRangeTargetSample.strongestAuthoredZoneWeight =
        strongestAuthoredZoneWeight;
}

void CaptureRawWorkspaceViewTransformInputStats(
    RenderPipeline& pipeline,
    const EditorRenderWorker::Snapshot& snapshot,
    EditorRenderWorker::Result& result) {
    if (result.rawWorkspace.sourceKey.empty()) {
        result.rawWorkspace.viewTransformInputStats = {};
        result.rawWorkspace.finalDisplayStats = {};
        result.rawWorkspace.stageStatsReadbacks.clear();
        result.rawWorkspace.graphScopeReadback = {};
        result.rawWorkspace.startPointCandidateRenderRequests.clear();
        result.rawWorkspace.startPointCandidateRenderResults.clear();
        result.rawWorkspace.startPointDiagnostics =
            Stack::RawAutoStartPoint::MakeUnavailableDiagnostics({}, "No RAW workspace source is active.");
        result.rawWorkspace.analysis =
            Stack::RawAnalysis::BuildUnavailableAnalysis({}, "No RAW workspace source is active.");
        return;
    }
    result.rawWorkspace.viewTransformInputStats = pipeline.GetRawDevelopmentViewTransformInputStats();
    result.rawWorkspace.finalDisplayStats = pipeline.GetRawDevelopmentFinalDisplayStats();
    result.rawWorkspace.stageStatsReadbacks = pipeline.GetRawDevelopmentStageStatsReadbacks();
    result.rawWorkspace.graphScopeReadback =
        pipeline.GetRawDevelopmentGraphScopeReadback();
    result.rawWorkspace.colorWarpCloudPacket = {};
    const RawDevelopmentGraphScopeReadback& colorScope =
        result.rawWorkspace.graphScopeReadback;
    const std::size_t scopePixelCount = colorScope.valid &&
            colorScope.stage == RawDevelopmentGraphScopeStage::ColorWarpInput &&
            colorScope.width > 0 && colorScope.height > 0
        ? static_cast<std::size_t>(colorScope.width) *
            static_cast<std::size_t>(colorScope.height)
        : 0u;
    if (snapshot.rawWorkspace.hasRecipe && scopePixelCount > 0u &&
        colorScope.pixels.size() >= scopePixelCount * 3u) {
        constexpr std::size_t kBinColumns = 48u;
        constexpr std::size_t kBinRows = 48u;
        constexpr std::size_t kBinCount = kBinColumns * kBinRows;
        constexpr std::size_t kMaximumSamples = 4096u;
        std::array<std::size_t, kBinCount> counts {};
        const auto buildSample = [&](std::size_t index,
                                     Raw::RawColorCloudSample& sample) {
            const std::size_t offset = index * 3u;
            sample.rgb = {
                colorScope.pixels[offset + 0u],
                colorScope.pixels[offset + 1u],
                colorScope.pixels[offset + 2u]
            };
            if (!std::isfinite(sample.rgb[0]) ||
                !std::isfinite(sample.rgb[1]) ||
                !std::isfinite(sample.rgb[2])) {
                return false;
            }
            const Stack::RawRecipe::RawColorWarpCoordinate coordinate =
                Stack::RawRecipe::WorkingRgbToColorWarpCoordinate(
                    sample.rgb,
                    snapshot.rawWorkspace.recipe.technical.workingSpace);
            sample.a = coordinate.a;
            sample.b = coordinate.b;
            sample.sceneEv = coordinate.sceneEv;
            return std::isfinite(sample.a) &&
                std::isfinite(sample.b) &&
                std::isfinite(sample.sceneEv);
        };
        const auto binForSample = [&](const Raw::RawColorCloudSample& sample) {
            const float normalizedA = std::clamp(
                (sample.a + 1.0f) * 0.5f,
                0.0f,
                0.999999f);
            const float normalizedB = std::clamp(
                (sample.b + 1.0f) * 0.5f,
                0.0f,
                0.999999f);
            return static_cast<std::size_t>(
                       normalizedB * static_cast<float>(kBinRows)) *
                    kBinColumns +
                static_cast<std::size_t>(
                    normalizedA * static_cast<float>(kBinColumns));
        };
        Raw::RawColorCloudSample candidate;
        for (std::size_t index = 0; index < scopePixelCount; ++index) {
            if (buildSample(index, candidate)) {
                ++counts[binForSample(candidate)];
            }
        }
        std::array<std::size_t, kBinCount> quotas {};
        std::size_t totalQuota = 0u;
        for (std::size_t bin = 0; bin < kBinCount; ++bin) {
            if (counts[bin] == 0u) continue;
            quotas[bin] = std::min(
                counts[bin],
                std::max<std::size_t>(
                    1u,
                    static_cast<std::size_t>(std::ceil(
                        std::sqrt(static_cast<float>(counts[bin])) * 3.0f))));
            totalQuota += quotas[bin];
        }
        if (totalQuota > kMaximumSamples) {
            const float scale = static_cast<float>(kMaximumSamples) /
                static_cast<float>(totalQuota);
            totalQuota = 0u;
            for (std::size_t bin = 0; bin < kBinCount; ++bin) {
                if (quotas[bin] == 0u) continue;
                quotas[bin] = std::max<std::size_t>(
                    1u,
                    static_cast<std::size_t>(std::floor(
                        static_cast<float>(quotas[bin]) * scale)));
                totalQuota += quotas[bin];
            }
        }
        Raw::RawColorCloudPacket& packet =
            result.rawWorkspace.colorWarpCloudPacket;
        packet.sourceKey = result.rawWorkspace.sourceKey;
        packet.inputFingerprint =
            snapshot.rawWorkspace.graphScopeInputFingerprint;
        packet.workingSpace = static_cast<int>(
            snapshot.rawWorkspace.recipe.technical.workingSpace);
        packet.colorWarpVersion =
            snapshot.rawWorkspace.recipe.colorWarp.version;
        packet.samples.reserve(std::min(totalQuota, kMaximumSamples));
        std::array<std::size_t, kBinCount> seen {};
        std::array<std::size_t, kBinCount> selected {};
        for (std::size_t index = 0;
             index < scopePixelCount && packet.samples.size() < kMaximumSamples;
             ++index) {
            if (!buildSample(index, candidate)) continue;
            const std::size_t bin = binForSample(candidate);
            const std::size_t binSeen = ++seen[bin];
            const std::size_t desired =
                (binSeen * quotas[bin]) / counts[bin];
            if (desired <= selected[bin]) continue;
            ++selected[bin];
            packet.samples.push_back(candidate);
        }
    }
    result.rawWorkspace.startPointDiagnostics =
        pipeline.BuildRawDevelopmentStartPointDiagnostics(result.rawWorkspace.sourceKey);
    result.rawWorkspace.analysis =
        Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(
            Stack::Renderer::BuildCurrentFrameInputStats(result.rawWorkspace.viewTransformInputStats),
            result.rawWorkspace.sourceKey);
}

void CaptureRawWorkspaceAutoBaseRecommendations(
    RenderPipeline& pipeline,
    const EditorRenderWorker::Snapshot& snapshot,
    EditorRenderWorker::Result& result) {
    result.rawWorkspace.recommendations = Stack::RawAutoBase::AutoBaseRecommendations();
    result.rawWorkspace.startPointCandidateRenderRequests.clear();
    result.rawWorkspace.startPointCandidateRenderResults.clear();
    if (result.rawWorkspace.sourceKey.empty() ||
        !snapshot.rawWorkspace.hasRecipe ||
        !result.rawWorkspace.analysis.currentFrameStats.valid) {
        return;
    }

    const Stack::RawAutoBase::LocalSuggestionAnalysisImage& localImage =
        pipeline.GetRawDevelopmentLocalSuggestionImage();
    result.rawWorkspace.recommendations =
        Stack::RawAutoBase::BuildAutoBaseRecommendations(
            result.rawWorkspace.analysis,
            snapshot.rawWorkspace.recipe,
            nullptr,
            &localImage);
    result.rawWorkspace.startPointDiagnostics =
        Stack::RawAutoStartPoint::BuildDryRunCandidateDiagnostics(
            std::move(result.rawWorkspace.startPointDiagnostics),
            snapshot.rawWorkspace.recipe,
            result.rawWorkspace.analysis,
            result.rawWorkspace.recommendations);
    result.rawWorkspace.startPointCandidateRenderRequests =
        Stack::RawAutoStartPoint::CollectCandidateRenderRequests(
            result.rawWorkspace.startPointDiagnostics);
    static constexpr int kMaxStartingPointCandidateRenderPasses = 4;
    for (int passIndex = 0;
         passIndex < kMaxStartingPointCandidateRenderPasses &&
         !result.rawWorkspace.startPointCandidateRenderRequests.empty();
         ++passIndex) {
        std::vector<Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderResult> passResults =
            Stack::Renderer::RenderRawCandidates(
                pipeline,
                snapshot.graph,
                result.rawWorkspace.sourceKey,
                result.rawWorkspace.startPointCandidateRenderRequests);
        const bool anyPassSuccess = std::any_of(
            passResults.begin(),
            passResults.end(),
            [](const Stack::RawAutoStartPoint::RawAutoStartPointCandidateRenderResult& item) {
                return item.success;
            });
        result.rawWorkspace.startPointDiagnostics =
            Stack::RawAutoStartPoint::MergeCandidateRenderResults(
                std::move(result.rawWorkspace.startPointDiagnostics),
                passResults,
                snapshot.rawWorkspace.recipe,
                result.rawWorkspace.analysis,
                result.rawWorkspace.recommendations);
        result.rawWorkspace.startPointCandidateRenderResults.insert(
            result.rawWorkspace.startPointCandidateRenderResults.end(),
            std::make_move_iterator(passResults.begin()),
            std::make_move_iterator(passResults.end()));
        result.rawWorkspace.startPointCandidateRenderRequests =
            Stack::RawAutoStartPoint::CollectCandidateRenderRequests(
                result.rawWorkspace.startPointDiagnostics);
        if (!anyPassSuccess) {
            break;
        }
    }
}

float DevelopRiskAbove(float value, float safeValue, float fullRiskValue) {
    if (fullRiskValue <= safeValue) {
        return value > safeValue ? 1.0f : 0.0f;
    }
    return std::clamp((value - safeValue) / (fullRiskValue - safeValue), 0.0f, 1.0f);
}

float DevelopRiskBelow(float value, float safeValue, float fullRiskValue) {
    if (safeValue <= fullRiskValue) {
        return value < safeValue ? 1.0f : 0.0f;
    }
    return std::clamp((safeValue - value) / (safeValue - fullRiskValue), 0.0f, 1.0f);
}

float ComputeDevelopLocalDamageRisk(
    float tileMean,
    float tileContrast,
    float tileShadowFraction,
    float tileHighlightFraction,
    float globalLowSaturationFraction) {
    const float highlightCrowding =
        std::clamp(
            DevelopRiskAbove(tileHighlightFraction, 0.58f, 0.92f) * 0.74f +
                DevelopRiskAbove(tileMean, 0.82f, 0.97f) * 0.26f,
            0.0f,
            1.0f);
    const float shadowCrowding =
        std::clamp(
            DevelopRiskAbove(tileShadowFraction, 0.72f, 0.94f) * 0.68f +
                DevelopRiskBelow(tileMean, 0.12f, 0.04f) * 0.32f,
            0.0f,
            1.0f);
    const float edgeStress = DevelopRiskAbove(tileContrast, 0.84f, 0.98f);
    const float flatGrayRisk =
        DevelopRiskBelow(tileContrast, 0.18f, 0.06f) *
        DevelopRiskAbove(tileMean, 0.16f, 0.42f) *
        DevelopRiskAbove(globalLowSaturationFraction, 0.70f, 0.95f);

    // This is a compact diagnostic map, not a full perceptual damage map. It
    // flags regional pressure that should make ranking/rejection more cautious.
    return std::clamp(
        std::max({ highlightCrowding, shadowCrowding, edgeStress * 0.78f, flatGrayRisk * 0.70f }),
        0.0f,
        1.0f);
}

float SmoothDevelopSubjectMetricWeight(float normalizedDistance, float feather) {
    const float featherWidth = std::max(0.02f, std::clamp(feather, 0.0f, 1.0f) * 0.85f);
    if (normalizedDistance <= 1.0f) {
        return 1.0f;
    }
    if (normalizedDistance >= 1.0f + featherWidth) {
        return 0.0f;
    }
    const float t = std::clamp((normalizedDistance - 1.0f) / featherWidth, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f - smooth;
}

float DevelopSubjectMetricDistanceToSegmentSq(
    float px,
    float py,
    const EditorRenderWorker::DevelopSubjectMetricPoint& a,
    const EditorRenderWorker::DevelopSubjectMetricPoint& b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float wx = px - a.x;
    const float wy = py - a.y;
    const float segmentLenSq = vx * vx + vy * vy;
    const float t = segmentLenSq > 0.0000001f
        ? std::clamp((wx * vx + wy * vy) / segmentLenSq, 0.0f, 1.0f)
        : 0.0f;
    const float closestX = a.x + vx * t;
    const float closestY = a.y + vy * t;
    const float dx = px - closestX;
    const float dy = py - closestY;
    return dx * dx + dy * dy;
}

struct DevelopSubjectMetricWeights {
    float important = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;

    float Positive() const {
        return std::max({ important, reveal, protect, preserveMood });
    }

    float Any() const {
        return std::max(Positive(), lowPriority);
    }
};

void AddDevelopSubjectMetricModeWeight(
    DevelopSubjectMetricWeights& weights,
    int mode,
    bool lowPriority,
    float weight) {
    if (weight <= 0.001f) {
        return;
    }

    const int clampedMode = std::clamp(mode, 0, 4);
    if (lowPriority || clampedMode == 4) {
        weights.lowPriority = std::max(weights.lowPriority, weight);
        return;
    }

    switch (clampedMode) {
        case 1:
            weights.reveal = std::max(weights.reveal, weight);
            break;
        case 2:
            weights.protect = std::max(weights.protect, weight);
            break;
        case 3:
            weights.preserveMood = std::max(weights.preserveMood, weight);
            break;
        case 0:
        default:
            weights.important = std::max(weights.important, weight);
            break;
    }
}

DevelopSubjectMetricWeights ComputeDevelopSubjectMetricWeights(
    const EditorRenderWorker::DevelopSubjectMetricSampling& sampling,
    float nx,
    float ny) {
    DevelopSubjectMetricWeights weights;
    for (const EditorRenderWorker::DevelopSubjectMetricRegion& region : sampling.regions) {
        if (!region.enabled || region.strength <= 0.001f) {
            continue;
        }
        const float radiusX = std::clamp(region.radiusX, 0.005f, 1.0f);
        const float radiusY = std::clamp(region.radiusY, 0.005f, 1.0f);
        const float dx = (nx - std::clamp(region.centerX, 0.0f, 1.0f)) / radiusX;
        const float dy = (ny - std::clamp(region.centerY, 0.0f, 1.0f)) / radiusY;
        const float normalizedDistance = std::sqrt(dx * dx + dy * dy);
        const float weight =
            SmoothDevelopSubjectMetricWeight(normalizedDistance, region.feather) *
            std::clamp(region.strength, 0.0f, 1.0f);
        AddDevelopSubjectMetricModeWeight(weights, region.mode, region.lowPriority, weight);
    }

    for (const EditorRenderWorker::DevelopSubjectMetricStroke& stroke : sampling.strokes) {
        if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
            continue;
        }
        if (nx < stroke.minX || nx > stroke.maxX || ny < stroke.minY || ny > stroke.maxY) {
            continue;
        }

        float minDistanceSq = std::numeric_limits<float>::infinity();
        if (stroke.points.size() == 1) {
            const float dx = nx - stroke.points.front().x;
            const float dy = ny - stroke.points.front().y;
            minDistanceSq = dx * dx + dy * dy;
        } else {
            for (std::size_t i = 1; i < stroke.points.size(); ++i) {
                minDistanceSq = std::min(
                    minDistanceSq,
                    DevelopSubjectMetricDistanceToSegmentSq(nx, ny, stroke.points[i - 1], stroke.points[i]));
            }
        }

        const float radius = std::clamp(stroke.radius, 0.002f, 0.50f);
        const float normalizedDistance = std::sqrt(std::max(0.0f, minDistanceSq)) / radius;
        const float weight =
            SmoothDevelopSubjectMetricWeight(normalizedDistance, stroke.feather) *
            std::clamp(stroke.strength, 0.0f, 1.0f);
        AddDevelopSubjectMetricModeWeight(weights, stroke.mode, stroke.lowPriority, weight);
    }

    // Reduce/ignore strokes are user intent too. At overlap they soften positive
    // marks instead of becoming a hard subtraction mask.
    const float reduce = std::clamp(weights.lowPriority * 0.65f, 0.0f, 1.0f);
    weights.important = std::max(0.0f, weights.important - reduce);
    weights.reveal = std::max(0.0f, weights.reveal - reduce);
    weights.protect = std::max(0.0f, weights.protect - reduce);
    weights.preserveMood = std::max(0.0f, weights.preserveMood - reduce);
    return weights;
}

float DevelopWeightedHistogramPercentile(
    const std::array<double, 256>& histogram,
    double totalWeight,
    float percentile) {
    if (totalWeight <= 0.0) {
        return 0.0f;
    }
    const double target = std::clamp(static_cast<double>(percentile), 0.0, 1.0) * totalWeight;
    double cumulative = 0.0;
    for (int bucket = 0; bucket < 256; ++bucket) {
        cumulative += histogram[static_cast<std::size_t>(bucket)];
        if (cumulative >= target) {
            return static_cast<float>(bucket) / 255.0f;
        }
    }
    return 1.0f;
}

EditorRenderWorker::DevelopCandidateRenderMetrics AnalyzeDevelopCandidatePixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    const EditorRenderWorker::DevelopSubjectMetricSampling* subjectSampling) {
    EditorRenderWorker::DevelopCandidateRenderMetrics metrics;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return metrics;
    }

    std::array<int, 256> histogram {};
    double lumaSum = 0.0;
    double redSum = 0.0;
    double greenSum = 0.0;
    double blueSum = 0.0;
    double saturationSum = 0.0;
    int shadowCount = 0;
    int highlightCount = 0;
    int clippedCount = 0;
    int lowSaturationCount = 0;
    int sampleCount = 0;
    const int safeWidth = std::max(0, width);
    const int safeHeight = std::max(0, height);
    std::array<double, 9> tileLumaSum {};
    std::array<int, 9> tileSampleCount {};
    std::array<int, 9> tileShadowCount {};
    std::array<int, 9> tileHighlightCount {};
    std::array<float, 9> tileMinLuma {};
    std::array<float, 9> tileMaxLuma {};
    tileMinLuma.fill(1.0f);
    const size_t pixelCount = std::min(
        pixels.size() / 4u,
        static_cast<size_t>(std::max(0, width) * std::max(0, height)));
    const bool subjectSamplingActive =
        subjectSampling &&
        subjectSampling->enabled &&
        (!subjectSampling->regions.empty() || !subjectSampling->strokes.empty());
    const int subjectStride =
        pixelCount >= 8000000u ? 5 :
        pixelCount >= 2000000u ? 3 :
        pixelCount >= 600000u ? 2 : 1;
    std::array<double, 256> subjectMarkedHistogram {};
    double subjectAnyWeightSum = 0.0;
    double subjectPositiveWeightSum = 0.0;
    double subjectImportantWeightSum = 0.0;
    double subjectRevealWeightSum = 0.0;
    double subjectProtectWeightSum = 0.0;
    double subjectMoodWeightSum = 0.0;
    double subjectLowPriorityWeightSum = 0.0;
    double subjectMarkedLumaSum = 0.0;
    double subjectMarkedShadowWeight = 0.0;
    double subjectMarkedHighlightWeight = 0.0;
    double subjectMarkedClippedWeight = 0.0;
    double subjectLowPriorityLumaSum = 0.0;
    double subjectLowPriorityBrightWeight = 0.0;
    int subjectMetricSampleCount = 0;
    int subjectMarkedSampleCount = 0;
    std::vector<float> lumaValues(pixelCount, -1.0f);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const size_t offset = pixelIndex * 4u;
        const float r = static_cast<float>(pixels[offset + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[offset + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[offset + 2]) / 255.0f;
        const float a = static_cast<float>(pixels[offset + 3]) / 255.0f;
        if (a <= 0.0f) {
            continue;
        }
        const float luma = std::clamp(0.2126f * r + 0.7152f * g + 0.0722f * b, 0.0f, 1.0f);
        const float maxChannel = std::max({ r, g, b });
        const float minChannel = std::min({ r, g, b });
        const float saturation = maxChannel > 0.0001f
            ? std::clamp((maxChannel - minChannel) / maxChannel, 0.0f, 1.0f)
            : 0.0f;
        lumaValues[pixelIndex] = luma;
        const int bucket = std::clamp(static_cast<int>(std::lround(luma * 255.0f)), 0, 255);
        ++histogram[static_cast<size_t>(bucket)];
        lumaSum += luma;
        redSum += r;
        greenSum += g;
        blueSum += b;
        saturationSum += saturation;
        ++sampleCount;
        const int x = static_cast<int>(pixelIndex % static_cast<size_t>(safeWidth));
        const int y = static_cast<int>(pixelIndex / static_cast<size_t>(safeWidth));
        const int tileX = std::clamp((x * 3) / std::max(1, safeWidth), 0, 2);
        const int tileY = std::clamp((y * 3) / std::max(1, safeHeight), 0, 2);
        const int tileIndex = tileY * 3 + tileX;
        if (subjectSamplingActive &&
            (x % subjectStride) == 0 &&
            (y % subjectStride) == 0) {
            ++subjectMetricSampleCount;
            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(std::max(1, safeWidth));
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(std::max(1, safeHeight));
            const DevelopSubjectMetricWeights subjectWeights =
                ComputeDevelopSubjectMetricWeights(*subjectSampling, nx, ny);
            const float positiveWeight = std::clamp(subjectWeights.Positive(), 0.0f, 1.0f);
            const float lowPriorityWeight = std::clamp(subjectWeights.lowPriority, 0.0f, 1.0f);
            const float anyWeight = std::clamp(std::max(positiveWeight, lowPriorityWeight), 0.0f, 1.0f);
            if (anyWeight > 0.001f) {
                ++subjectMarkedSampleCount;
                subjectAnyWeightSum += anyWeight;
            }
            if (positiveWeight > 0.001f) {
                subjectPositiveWeightSum += positiveWeight;
                subjectImportantWeightSum += subjectWeights.important;
                subjectRevealWeightSum += subjectWeights.reveal;
                subjectProtectWeightSum += subjectWeights.protect;
                subjectMoodWeightSum += subjectWeights.preserveMood;
                subjectMarkedLumaSum += luma * positiveWeight;
                subjectMarkedHistogram[static_cast<std::size_t>(bucket)] += positiveWeight;
                if (luma < 0.10f) {
                    subjectMarkedShadowWeight += positiveWeight;
                }
                if (luma > 0.90f) {
                    subjectMarkedHighlightWeight += positiveWeight;
                }
                if (maxChannel >= 0.995f || luma >= 0.985f) {
                    subjectMarkedClippedWeight += positiveWeight;
                }
            }
            if (lowPriorityWeight > 0.001f) {
                subjectLowPriorityWeightSum += lowPriorityWeight;
                subjectLowPriorityLumaSum += luma * lowPriorityWeight;
                if (luma > 0.58f) {
                    subjectLowPriorityBrightWeight += lowPriorityWeight;
                }
            }
        }
        tileLumaSum[static_cast<size_t>(tileIndex)] += luma;
        ++tileSampleCount[static_cast<size_t>(tileIndex)];
        tileMinLuma[static_cast<size_t>(tileIndex)] =
            std::min(tileMinLuma[static_cast<size_t>(tileIndex)], luma);
        tileMaxLuma[static_cast<size_t>(tileIndex)] =
            std::max(tileMaxLuma[static_cast<size_t>(tileIndex)], luma);
        if (luma < 0.10f) {
            ++shadowCount;
            ++tileShadowCount[static_cast<size_t>(tileIndex)];
        }
        if (luma > 0.90f) {
            ++highlightCount;
            ++tileHighlightCount[static_cast<size_t>(tileIndex)];
        }
        if (maxChannel >= 0.995f || luma >= 0.985f) {
            ++clippedCount;
        }
        if (luma > 0.12f && saturation < 0.06f) {
            ++lowSaturationCount;
        }
    }

    if (sampleCount <= 0) {
        return metrics;
    }

    auto percentileFromHistogram = [&](float percentile) {
        const int target = std::clamp(
            static_cast<int>(std::lround(static_cast<float>(sampleCount - 1) * percentile)),
            0,
            sampleCount - 1);
        int cumulative = 0;
        for (int bucket = 0; bucket < 256; ++bucket) {
            cumulative += histogram[static_cast<size_t>(bucket)];
            if (cumulative > target) {
                return static_cast<float>(bucket) / 255.0f;
            }
        }
        return 1.0f;
    };

    metrics.meanLuma = static_cast<float>(lumaSum / static_cast<double>(sampleCount));
    metrics.p10Luma = percentileFromHistogram(0.10f);
    metrics.medianLuma = percentileFromHistogram(0.50f);
    metrics.p90Luma = percentileFromHistogram(0.90f);
    metrics.shadowFraction = static_cast<float>(shadowCount) / static_cast<float>(sampleCount);
    metrics.highlightFraction = static_cast<float>(highlightCount) / static_cast<float>(sampleCount);
    metrics.clippedFraction = static_cast<float>(clippedCount) / static_cast<float>(sampleCount);
    metrics.contrastSpan = std::max(0.0f, metrics.p90Luma - metrics.p10Luma);
    metrics.meanRed = static_cast<float>(redSum / static_cast<double>(sampleCount));
    metrics.meanGreen = static_cast<float>(greenSum / static_cast<double>(sampleCount));
    metrics.meanBlue = static_cast<float>(blueSum / static_cast<double>(sampleCount));
    const float meanMaxChannel = std::max({ metrics.meanRed, metrics.meanGreen, metrics.meanBlue });
    const float meanMinChannel = std::min({ metrics.meanRed, metrics.meanGreen, metrics.meanBlue });
    metrics.warmCoolBias =
        std::clamp(
            (metrics.meanRed - metrics.meanBlue) /
                std::max(0.08f, metrics.meanRed + metrics.meanBlue),
            -1.0f,
            1.0f);
    metrics.magentaGreenBias =
        std::clamp(
            (((metrics.meanRed + metrics.meanBlue) * 0.5f) - metrics.meanGreen) /
                std::max(0.08f, metrics.meanRed + metrics.meanGreen + metrics.meanBlue),
            -1.0f,
            1.0f);
    metrics.channelImbalance =
        std::clamp((meanMaxChannel - meanMinChannel) / std::max(0.08f, meanMaxChannel), 0.0f, 1.0f);
    metrics.meanSaturation = static_cast<float>(saturationSum / static_cast<double>(sampleCount));
    metrics.lowSaturationFraction = static_cast<float>(lowSaturationCount) / static_cast<float>(sampleCount);
    const float highlightBandThreshold =
        std::clamp(std::max(0.54f, metrics.p90Luma * 0.88f), 0.50f, 0.88f);
    std::array<int, 9> tileHighlightBandCount {};
    double highlightBandLumaSum = 0.0;
    int highlightBandCount = 0;
    int highlightBandLowSaturationCount = 0;
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const float luma = lumaValues[pixelIndex];
        if (luma < highlightBandThreshold) {
            continue;
        }
        const size_t offset = pixelIndex * 4u;
        const float a = static_cast<float>(pixels[offset + 3]) / 255.0f;
        if (a <= 0.0f) {
            continue;
        }
        const float r = static_cast<float>(pixels[offset + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[offset + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[offset + 2]) / 255.0f;
        const float maxChannel = std::max({ r, g, b });
        const float minChannel = std::min({ r, g, b });
        const float saturation = maxChannel > 0.0001f
            ? std::clamp((maxChannel - minChannel) / maxChannel, 0.0f, 1.0f)
            : 0.0f;
        const int x = static_cast<int>(pixelIndex % static_cast<size_t>(safeWidth));
        const int y = static_cast<int>(pixelIndex / static_cast<size_t>(safeWidth));
        const int tileX = std::clamp((x * 3) / std::max(1, safeWidth), 0, 2);
        const int tileY = std::clamp((y * 3) / std::max(1, safeHeight), 0, 2);
        ++tileHighlightBandCount[static_cast<size_t>(tileY * 3 + tileX)];
        highlightBandLumaSum += luma;
        ++highlightBandCount;
        if (saturation < 0.10f) {
            ++highlightBandLowSaturationCount;
        }
    }
    if (highlightBandCount > 0) {
        metrics.highlightBandFraction =
            static_cast<float>(highlightBandCount) / static_cast<float>(sampleCount);
        metrics.highlightMeanLuma =
            std::clamp(
                static_cast<float>(highlightBandLumaSum / static_cast<double>(highlightBandCount)),
                0.0f,
                1.0f);
        metrics.highlightLowSaturationFraction =
            static_cast<float>(highlightBandLowSaturationCount) / static_cast<float>(highlightBandCount);
    }
    // Compact color evidence for rendered comparisons. This is intentionally
    // conservative: strong warm/cool mood can be valid, while magenta/green or
    // single-channel imbalance is usually a more useful damage signal.
    metrics.colorCastRisk =
        std::clamp(
            std::max({
                DevelopRiskAbove(std::fabs(metrics.warmCoolBias), 0.76f, 0.96f) * 0.75f,
                DevelopRiskAbove(std::fabs(metrics.magentaGreenBias), 0.20f, 0.42f),
                DevelopRiskAbove(metrics.channelImbalance, 0.72f, 0.94f) * 0.65f }) *
                DevelopRiskAbove(metrics.meanLuma, 0.08f, 0.20f),
            0.0f,
            1.0f);
    float minTileMean = 1.0f;
    float maxTileMean = 0.0f;
    float localDamageRiskSum = 0.0f;
    int localDamageRiskCount = 0;
    float highlightStructureSum = 0.0f;
    int highlightStructureTileCount = 0;
    int populatedTileCount = 0;
    int localDarkTileCount = 0;
    int localBrightTileCount = 0;
    for (size_t tileIndex = 0; tileIndex < metrics.localMeanLuma.size(); ++tileIndex) {
        if (tileSampleCount[tileIndex] <= 0) {
            metrics.localMeanLuma[tileIndex] = metrics.meanLuma;
            metrics.localContrastSpan[tileIndex] = 0.0f;
            continue;
        }
        ++populatedTileCount;

        const float tileMean =
            static_cast<float>(tileLumaSum[tileIndex] / static_cast<double>(tileSampleCount[tileIndex]));
        const float tileContrast =
            std::max(0.0f, tileMaxLuma[tileIndex] - tileMinLuma[tileIndex]);
        const float tileShadow =
            static_cast<float>(tileShadowCount[tileIndex]) / static_cast<float>(tileSampleCount[tileIndex]);
        const float tileHighlight =
            static_cast<float>(tileHighlightCount[tileIndex]) / static_cast<float>(tileSampleCount[tileIndex]);
        const float tileHighlightBand =
            static_cast<float>(tileHighlightBandCount[tileIndex]) / static_cast<float>(tileSampleCount[tileIndex]);
        metrics.localMeanLuma[tileIndex] = tileMean;
        metrics.localContrastSpan[tileIndex] = tileContrast;
        minTileMean = std::min(minTileMean, tileMean);
        maxTileMean = std::max(maxTileMean, tileMean);
        if (tileMean < 0.22f || tileShadow > 0.36f) {
            ++localDarkTileCount;
        }
        if (tileMean > 0.62f || tileHighlight > 0.18f || tileHighlightBand > 0.18f) {
            ++localBrightTileCount;
        }
        metrics.localContrastPeak = std::max(metrics.localContrastPeak, tileContrast);
        metrics.localShadowPressure = std::max(metrics.localShadowPressure, tileShadow);
        metrics.localHighlightPressure = std::max(metrics.localHighlightPressure, tileHighlight);
        const bool meaningfulHighlightTile =
            tileHighlightBand > 0.08f ||
            (tileMean > highlightBandThreshold && tileContrast > 0.04f);
        if (meaningfulHighlightTile) {
            ++highlightStructureTileCount;
            highlightStructureSum += std::clamp(
                DevelopRiskAbove(tileContrast, 0.10f, 0.34f) * 0.72f +
                    DevelopRiskAbove(tileHighlightBand, 0.12f, 0.42f) * 0.28f,
                0.0f,
                1.0f);
        }
        const float localDamageRisk =
            ComputeDevelopLocalDamageRisk(
                tileMean,
                tileContrast,
                tileShadow,
                tileHighlight,
                metrics.lowSaturationFraction);
        metrics.localDamageRiskScore[tileIndex] = localDamageRisk;
        localDamageRiskSum += localDamageRisk;
        ++localDamageRiskCount;
        if (localDamageRisk > metrics.localDamageRiskPeak) {
            metrics.localDamageRiskPeak = localDamageRisk;
            metrics.localDamageRiskPeakTile = static_cast<int>(tileIndex);
        }
    }
    metrics.localLumaSpread = std::max(0.0f, maxTileMean - minTileMean);
    metrics.localEvSpreadStops = populatedTileCount > 0
        ? std::clamp(std::log2((maxTileMean + 0.025f) / (minTileMean + 0.025f)), 0.0f, 8.0f)
        : 0.0f;
    metrics.highlightTileCoverage = populatedTileCount > 0
        ? std::clamp(
            static_cast<float>(highlightStructureTileCount) / static_cast<float>(populatedTileCount),
            0.0f,
            1.0f)
        : 0.0f;
    metrics.highlightStructureScore = highlightStructureTileCount > 0
        ? std::clamp(highlightStructureSum / static_cast<float>(highlightStructureTileCount), 0.0f, 1.0f)
        : 0.0f;
    metrics.localDamageRiskMean = localDamageRiskCount > 0
        ? std::clamp(localDamageRiskSum / static_cast<float>(localDamageRiskCount), 0.0f, 1.0f)
        : 0.0f;
    metrics.centerMeanLuma = metrics.localMeanLuma[4];
    if (tileSampleCount[4] > 0) {
        metrics.centerShadowFraction =
            static_cast<float>(tileShadowCount[4]) / static_cast<float>(tileSampleCount[4]);
        metrics.centerHighlightFraction =
            static_cast<float>(tileHighlightCount[4]) / static_cast<float>(tileSampleCount[4]);
    }
    const float broadHighlightPresence = std::clamp(
        DevelopRiskAbove(metrics.highlightBandFraction, 0.12f, 0.36f) * 0.55f +
            DevelopRiskAbove(metrics.localHighlightPressure, 0.28f, 0.66f) * 0.30f +
            DevelopRiskAbove(metrics.centerHighlightFraction, 0.12f, 0.42f) * 0.15f,
        0.0f,
        1.0f);
    const float highlightDimness =
        metrics.highlightBandFraction > 0.0f
            ? DevelopRiskBelow(metrics.highlightMeanLuma, 0.70f, 0.48f)
            : 0.0f;
    const float highlightGrayness =
        DevelopRiskAbove(metrics.highlightLowSaturationFraction, 0.42f, 0.82f);
    const float highlightSeparationLoss = std::max(
        DevelopRiskBelow(metrics.contrastSpan, 0.36f, 0.16f),
        DevelopRiskBelow(metrics.localLumaSpread, 0.18f, 0.06f));
    metrics.highlightGrayRisk = std::clamp(
        broadHighlightPresence *
            (highlightDimness * 0.42f +
             highlightGrayness * 0.30f +
             highlightSeparationLoss * 0.20f +
             metrics.localDamageRiskMean * 0.08f),
        0.0f,
        1.0f);
    const float meaningfulHighlightArea =
        DevelopRiskAbove(metrics.highlightBandFraction, 0.10f, 0.32f);
    const float meaningfulHighlightCoverage =
        DevelopRiskAbove(metrics.highlightTileCoverage, 0.18f, 0.56f);
    const float tinySpecularDiscount =
        DevelopRiskBelow(metrics.highlightBandFraction, 0.12f, 0.03f) *
        DevelopRiskBelow(metrics.highlightTileCoverage, 0.22f, 0.05f);
    metrics.meaningfulHighlightPressure = std::clamp(
        meaningfulHighlightArea * 0.38f +
            meaningfulHighlightCoverage * 0.32f +
            metrics.highlightStructureScore * 0.18f +
            DevelopRiskAbove(metrics.highlightMeanLuma, 0.58f, 0.78f) * 0.08f +
            metrics.highlightGrayRisk * 0.08f -
            tinySpecularDiscount * 0.28f,
        0.0f,
        1.0f);

    double edgeContrastSum = 0.0;
    int edgeSampleCount = 0;
    int haloRiskCount = 0;
    double shadowDiffSum = 0.0;
    int shadowDiffCount = 0;
    auto lumaAt = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= safeWidth || y >= safeHeight) {
            return -1.0f;
        }
        const size_t index = static_cast<size_t>(y) * static_cast<size_t>(safeWidth) + static_cast<size_t>(x);
        return index < lumaValues.size() ? lumaValues[index] : -1.0f;
    };
    for (int y = 1; y + 1 < safeHeight; ++y) {
        for (int x = 1; x + 1 < safeWidth; ++x) {
            const float center = lumaAt(x, y);
            const float left = lumaAt(x - 1, y);
            const float right = lumaAt(x + 1, y);
            const float up = lumaAt(x, y - 1);
            const float down = lumaAt(x, y + 1);
            if (center < 0.0f || left < 0.0f || right < 0.0f || up < 0.0f || down < 0.0f) {
                continue;
            }
            const float localMin = std::min({ center, left, right, up, down });
            const float localMax = std::max({ center, left, right, up, down });
            const float localRange = localMax - localMin;
            if (localRange > 0.18f) {
                edgeContrastSum += localRange;
                ++edgeSampleCount;
                const float neighborMean = 0.25f * (left + right + up + down);
                const bool centerOvershootsNeighbors =
                    (center > neighborMean + 0.22f && center > left && center > right && center > up && center > down) ||
                    (center < neighborMean - 0.22f && center < left && center < right && center < up && center < down);
                if (centerOvershootsNeighbors) {
                    ++haloRiskCount;
                }
            }
            if (center < 0.22f) {
                shadowDiffSum +=
                    std::fabs(center - left) +
                    std::fabs(center - right) +
                    std::fabs(center - up) +
                    std::fabs(center - down);
                shadowDiffCount += 4;
            }
        }
    }
    metrics.edgeContrast = edgeSampleCount > 0
        ? std::clamp(static_cast<float>(edgeContrastSum / static_cast<double>(edgeSampleCount)), 0.0f, 1.0f)
        : 0.0f;
    metrics.haloRiskFraction = edgeSampleCount > 0
        ? std::clamp(static_cast<float>(haloRiskCount) / static_cast<float>(edgeSampleCount), 0.0f, 1.0f)
        : 0.0f;
    metrics.shadowTextureRisk = shadowDiffCount > 0
        ? std::clamp(static_cast<float>(shadowDiffSum / static_cast<double>(shadowDiffCount)) / 0.18f, 0.0f, 1.0f)
        : 0.0f;
    const float localDarkTileCoverage = populatedTileCount > 0
        ? static_cast<float>(localDarkTileCount) / static_cast<float>(populatedTileCount)
        : 0.0f;
    const float localBrightTileCoverage = populatedTileCount > 0
        ? static_cast<float>(localBrightTileCount) / static_cast<float>(populatedTileCount)
        : 0.0f;
    const float localMixedRangeCoverage =
        std::clamp(std::min(localDarkTileCoverage, localBrightTileCoverage) * 2.0f, 0.0f, 1.0f);
    metrics.localEvConflict = std::clamp(
        DevelopRiskAbove(metrics.localEvSpreadStops, 1.35f, 4.20f) * 0.36f +
            localMixedRangeCoverage * 0.28f +
            DevelopRiskAbove(metrics.localLumaSpread, 0.28f, 0.62f) * 0.14f +
            metrics.localDamageRiskMean * 0.08f +
            DevelopRiskAbove(metrics.edgeContrast, 0.30f, 0.70f) * 0.08f +
            DevelopRiskAbove(metrics.haloRiskFraction, 0.04f, 0.18f) * 0.06f,
        0.0f,
        1.0f);
    metrics.localExposureHighlightCrowding = std::clamp(
        DevelopRiskAbove(metrics.localHighlightPressure, 0.34f, 0.82f) * 0.30f +
            DevelopRiskAbove(metrics.centerHighlightFraction, 0.18f, 0.58f) * 0.16f +
            DevelopRiskAbove(metrics.highlightBandFraction, 0.12f, 0.42f) * 0.18f +
            DevelopRiskAbove(metrics.clippedFraction, 0.004f, 0.024f) * 0.14f +
            metrics.meaningfulHighlightPressure * 0.14f +
            metrics.localDamageRiskPeak * 0.08f,
        0.0f,
        1.0f);
    metrics.localExposureShadowCrowding = std::clamp(
        DevelopRiskAbove(metrics.localShadowPressure, 0.48f, 0.88f) * 0.30f +
            DevelopRiskAbove(metrics.centerShadowFraction, 0.32f, 0.74f) * 0.16f +
            DevelopRiskAbove(metrics.shadowFraction, 0.44f, 0.84f) * 0.18f +
            metrics.shadowTextureRisk * 0.16f +
            metrics.localDamageRiskPeak * 0.10f +
            metrics.localEvConflict * 0.10f,
        0.0f,
        1.0f);
    metrics.localExposureHaloStress = std::clamp(
        DevelopRiskAbove(metrics.haloRiskFraction, 0.04f, 0.18f) * 0.36f +
            DevelopRiskAbove(metrics.edgeContrast, 0.34f, 0.72f) * 0.22f +
            DevelopRiskAbove(metrics.localContrastPeak, 0.74f, 0.96f) * 0.16f +
            metrics.localEvConflict * 0.14f +
            metrics.localDamageRiskPeak * 0.12f,
        0.0f,
        1.0f);
    metrics.localExposureFlatnessRisk = std::clamp(
        DevelopRiskBelow(metrics.contrastSpan, 0.30f, 0.12f) * 0.26f +
            DevelopRiskBelow(metrics.localContrastPeak, 0.34f, 0.14f) * 0.22f +
            DevelopRiskBelow(metrics.localLumaSpread, 0.16f, 0.04f) * 0.20f +
            DevelopRiskAbove(metrics.lowSaturationFraction, 0.70f, 0.94f) * 0.16f +
            metrics.highlightGrayRisk * 0.12f +
            metrics.localDamageRiskMean * 0.04f,
        0.0f,
        1.0f);
    metrics.localExposureDamageRisk = std::clamp(
        metrics.localExposureHighlightCrowding * 0.22f +
            metrics.localExposureShadowCrowding * 0.18f +
            metrics.localExposureHaloStress * 0.30f +
            metrics.localExposureFlatnessRisk * 0.16f +
            metrics.localDamageRiskPeak * 0.14f,
        0.0f,
        1.0f);
    const float centerContrast = metrics.localContrastSpan[4];
    const float centerDistinctness =
        DevelopRiskAbove(std::fabs(metrics.centerMeanLuma - metrics.meanLuma), 0.06f, 0.28f);
    const float centerStructure = DevelopRiskAbove(centerContrast, 0.08f, 0.34f);
    const float centerNotCrushed =
        1.0f - DevelopRiskBelow(metrics.centerMeanLuma, 0.12f, 0.02f);
    const float centerNotBlown =
        1.0f - DevelopRiskAbove(metrics.centerMeanLuma, 0.86f, 0.98f);
    const float centerUsableTone =
        std::clamp(centerNotCrushed * centerNotBlown, 0.0f, 1.0f);
    const float centerDarkRisk =
        DevelopRiskBelow(metrics.centerMeanLuma, 0.30f, 0.08f);
    const float centerHighlightRisk =
        DevelopRiskAbove(metrics.centerMeanLuma, 0.72f, 0.94f);
    // This is a weak composition/detail prior, not subject detection. It gives
    // Auto a named place to preserve "what likely matters" until Guide 05 adds
    // user-painted importance maps and richer scene understanding.
    metrics.subjectCenterPrior = std::clamp(
        0.18f +
            centerUsableTone * 0.18f +
            centerStructure * 0.22f +
            centerDistinctness * 0.18f +
            DevelopRiskAbove(metrics.localLumaSpread, 0.12f, 0.44f) * 0.08f +
            std::max(
                DevelopRiskAbove(metrics.centerShadowFraction, 0.20f, 0.62f),
                DevelopRiskAbove(metrics.centerHighlightFraction, 0.10f, 0.42f)) * 0.08f -
            metrics.localExposureFlatnessRisk * 0.06f,
        0.0f,
        1.0f);
    metrics.subjectReadabilityPressure = std::clamp(
        metrics.subjectCenterPrior *
            (centerDarkRisk * 0.34f +
             DevelopRiskAbove(metrics.centerShadowFraction, 0.26f, 0.70f) * 0.24f +
             DevelopRiskAbove(metrics.localShadowPressure, 0.46f, 0.86f) * 0.14f +
             metrics.localEvConflict * 0.10f +
             centerStructure * 0.06f -
             metrics.shadowTextureRisk * 0.12f -
             metrics.localExposureDamageRisk * 0.08f),
        0.0f,
        1.0f);
    metrics.subjectProtectionPressure = std::clamp(
        metrics.subjectCenterPrior *
            (centerHighlightRisk * 0.24f +
             DevelopRiskAbove(metrics.centerHighlightFraction, 0.12f, 0.46f) * 0.24f +
             metrics.meaningfulHighlightPressure * 0.16f +
             metrics.localExposureHighlightCrowding * 0.12f +
             DevelopRiskAbove(metrics.clippedFraction, 0.004f, 0.024f) * 0.12f +
             centerStructure * 0.06f -
             metrics.localExposureHaloStress * 0.06f),
        0.0f,
        1.0f);
    metrics.subjectMoodPreservationPressure = std::clamp(
        metrics.subjectCenterPrior *
            (centerDarkRisk * 0.24f +
             localBrightTileCoverage * 0.18f +
             DevelopRiskAbove(metrics.localLumaSpread, 0.24f, 0.62f) * 0.16f +
             metrics.shadowTextureRisk * 0.16f +
             metrics.localExposureShadowCrowding * 0.10f +
             metrics.localExposureHaloStress * 0.08f -
             metrics.subjectReadabilityPressure * 0.10f),
        0.0f,
        1.0f);
    metrics.subjectImportanceConfidence = std::clamp(
        metrics.subjectCenterPrior * 0.36f +
            centerStructure * 0.18f +
            centerDistinctness * 0.14f +
            std::max({
                metrics.subjectReadabilityPressure,
                metrics.subjectProtectionPressure,
                metrics.subjectMoodPreservationPressure }) * 0.20f +
            metrics.localEvConflict * 0.06f +
            metrics.meaningfulHighlightPressure * 0.06f,
        0.0f,
        1.0f);
    if (subjectSamplingActive && subjectMetricSampleCount > 0 && subjectAnyWeightSum > 0.001) {
        metrics.subjectMarkedSampleCount = subjectMarkedSampleCount;
        metrics.subjectMarkedCoverage = std::clamp(
            static_cast<float>(subjectAnyWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedPositiveCoverage = std::clamp(
            static_cast<float>(subjectPositiveWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedRevealCoverage = std::clamp(
            static_cast<float>(subjectRevealWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedProtectCoverage = std::clamp(
            static_cast<float>(subjectProtectWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedMoodCoverage = std::clamp(
            static_cast<float>(subjectMoodWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedLowPriorityCoverage = std::clamp(
            static_cast<float>(subjectLowPriorityWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
    }
    if (subjectPositiveWeightSum > 0.001) {
        metrics.subjectMarkedMeanLuma = std::clamp(
            static_cast<float>(subjectMarkedLumaSum / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedShadowFraction = std::clamp(
            static_cast<float>(subjectMarkedShadowWeight / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedHighlightFraction = std::clamp(
            static_cast<float>(subjectMarkedHighlightWeight / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedClippedFraction = std::clamp(
            static_cast<float>(subjectMarkedClippedWeight / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        const float subjectP10 = DevelopWeightedHistogramPercentile(
            subjectMarkedHistogram,
            subjectPositiveWeightSum,
            0.10f);
        const float subjectP90 = DevelopWeightedHistogramPercentile(
            subjectMarkedHistogram,
            subjectPositiveWeightSum,
            0.90f);
        metrics.subjectMarkedContrastSpan = std::max(0.0f, subjectP90 - subjectP10);
        const float toneFit =
            1.0f - std::clamp(std::fabs(metrics.subjectMarkedMeanLuma - 0.38f) / 0.38f, 0.0f, 1.0f);
        const float contrastSupport = std::clamp(metrics.subjectMarkedContrastSpan / 0.34f, 0.0f, 1.0f);
        metrics.subjectMarkedReadabilityScore = std::clamp(
            toneFit * 0.44f +
                contrastSupport * 0.20f +
                (1.0f - metrics.subjectMarkedShadowFraction) * 0.18f +
                (1.0f - metrics.subjectMarkedClippedFraction) * 0.18f -
                metrics.subjectMarkedHighlightFraction * 0.08f,
            0.0f,
            1.0f);
        metrics.subjectMarkedProtectionRisk = std::clamp(
            metrics.subjectMarkedClippedFraction * 0.70f +
                DevelopRiskAbove(metrics.subjectMarkedHighlightFraction, 0.22f, 0.62f) * 0.20f +
                DevelopRiskBelow(metrics.subjectMarkedContrastSpan, 0.06f, 0.02f) * 0.10f,
            0.0f,
            1.0f);
        metrics.subjectMarkedMoodPreservationScore = std::clamp(
            (1.0f - DevelopRiskAbove(metrics.subjectMarkedMeanLuma, 0.60f, 0.90f)) * 0.48f +
                (1.0f - metrics.subjectMarkedClippedFraction) * 0.24f +
                contrastSupport * 0.18f +
                (1.0f - metrics.subjectMarkedHighlightFraction) * 0.10f,
            0.0f,
            1.0f);
    }
    if (subjectLowPriorityWeightSum > 0.001) {
        metrics.subjectMarkedLowPriorityMeanLuma = std::clamp(
            static_cast<float>(subjectLowPriorityLumaSum / subjectLowPriorityWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedLowPriorityBrightFraction = std::clamp(
            static_cast<float>(subjectLowPriorityBrightWeight / subjectLowPriorityWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedLowPriorityPressure = std::clamp(
            metrics.subjectMarkedLowPriorityCoverage *
                (0.35f + metrics.subjectMarkedLowPriorityBrightFraction * 0.65f),
            0.0f,
            1.0f);
    }
    return metrics;
}

} // namespace

EditorRenderWorker::EditorRenderWorker() = default;

EditorRenderWorker::~EditorRenderWorker() {
    Shutdown();
}

bool EditorRenderWorker::ExecuteOpenGlTaskBlocking(
    OpenGlTask task,
    std::string& error) {
    error.clear();
    if (!task) {
        error = "The OpenGL task is empty.";
        return false;
    }
    std::shared_ptr<OpenGlTaskState> state;
    try {
        state = std::make_shared<OpenGlTaskState>();
        state->task = std::move(task);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_StopRequested || !m_InitComplete || !m_InitSucceeded ||
                !m_Thread.joinable()) {
                error = "The OpenGL render worker is unavailable.";
                return false;
            }
            m_OpenGlTasks.push(state);
            m_Busy = true;
        }
    } catch (const std::bad_alloc&) {
        error = "Host memory could not queue the OpenGL compute task.";
        return false;
    }
    m_Cv.notify_one();
    std::unique_lock<std::mutex> lock(state->mutex);
    state->cv.wait(lock, [&state]() { return state->completed; });
    error = state->error;
    return state->success;
}

EditorRenderWorker::DevelopCandidateRenderMetrics
EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    return AnalyzeDevelopCandidatePixels(pixels, width, height, nullptr);
}

EditorRenderWorker::DevelopCandidateRenderMetrics
EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    const DevelopSubjectMetricSampling& subjectSampling) {
    return AnalyzeDevelopCandidatePixels(pixels, width, height, &subjectSampling);
}

float EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
    const DevelopCandidateRenderMetrics& a,
    const DevelopCandidateRenderMetrics& b) {
    // Weighted compact distance for clustering rendered candidates that are visually redundant.
    float localMeanDistance = 0.0f;
    float localContrastDistance = 0.0f;
    float localDamageRiskDistance = 0.0f;
    for (size_t tileIndex = 0; tileIndex < a.localMeanLuma.size(); ++tileIndex) {
        localMeanDistance += std::fabs(a.localMeanLuma[tileIndex] - b.localMeanLuma[tileIndex]);
        localContrastDistance += std::fabs(a.localContrastSpan[tileIndex] - b.localContrastSpan[tileIndex]);
        localDamageRiskDistance += std::fabs(a.localDamageRiskScore[tileIndex] - b.localDamageRiskScore[tileIndex]);
    }
    localMeanDistance /= static_cast<float>(a.localMeanLuma.size());
    localContrastDistance /= static_cast<float>(a.localContrastSpan.size());
    localDamageRiskDistance /= static_cast<float>(a.localDamageRiskScore.size());
    return
        std::fabs(a.medianLuma - b.medianLuma) * 0.90f +
        std::fabs(a.meanLuma - b.meanLuma) * 0.45f +
        std::fabs(a.p10Luma - b.p10Luma) * 0.45f +
        std::fabs(a.p90Luma - b.p90Luma) * 0.45f +
        std::fabs(a.shadowFraction - b.shadowFraction) * 0.38f +
        std::fabs(a.highlightFraction - b.highlightFraction) * 0.38f +
        std::fabs(a.clippedFraction - b.clippedFraction) * 1.30f +
        std::fabs(a.contrastSpan - b.contrastSpan) * 0.55f +
        std::fabs(a.meanRed - b.meanRed) * 0.10f +
        std::fabs(a.meanGreen - b.meanGreen) * 0.10f +
        std::fabs(a.meanBlue - b.meanBlue) * 0.10f +
        std::fabs(a.warmCoolBias - b.warmCoolBias) * 0.05f +
        std::fabs(a.magentaGreenBias - b.magentaGreenBias) * 0.12f +
        std::fabs(a.channelImbalance - b.channelImbalance) * 0.08f +
        std::fabs(a.colorCastRisk - b.colorCastRisk) * 0.08f +
        std::fabs(a.meanSaturation - b.meanSaturation) * 0.28f +
        std::fabs(a.lowSaturationFraction - b.lowSaturationFraction) * 0.18f +
        std::fabs(a.highlightBandFraction - b.highlightBandFraction) * 0.18f +
        std::fabs(a.highlightMeanLuma - b.highlightMeanLuma) * 0.16f +
        std::fabs(a.highlightLowSaturationFraction - b.highlightLowSaturationFraction) * 0.12f +
        std::fabs(a.highlightGrayRisk - b.highlightGrayRisk) * 0.22f +
        std::fabs(a.highlightTileCoverage - b.highlightTileCoverage) * 0.12f +
        std::fabs(a.highlightStructureScore - b.highlightStructureScore) * 0.10f +
        std::fabs(a.meaningfulHighlightPressure - b.meaningfulHighlightPressure) * 0.18f +
        std::fabs(a.haloRiskFraction - b.haloRiskFraction) * 0.40f +
        std::fabs(a.shadowTextureRisk - b.shadowTextureRisk) * 0.18f +
        localMeanDistance * 0.35f +
        localContrastDistance * 0.12f +
        localDamageRiskDistance * 0.16f +
        std::fabs(a.localLumaSpread - b.localLumaSpread) * 0.20f +
        std::fabs(a.localEvSpreadStops - b.localEvSpreadStops) * 0.035f +
        std::fabs(a.localEvConflict - b.localEvConflict) * 0.18f +
        std::fabs(a.localHighlightPressure - b.localHighlightPressure) * 0.22f +
        std::fabs(a.localShadowPressure - b.localShadowPressure) * 0.14f +
        std::fabs(a.localDamageRiskPeak - b.localDamageRiskPeak) * 0.18f +
        std::fabs(a.localDamageRiskMean - b.localDamageRiskMean) * 0.10f +
        std::fabs(a.localExposureHighlightCrowding - b.localExposureHighlightCrowding) * 0.10f +
        std::fabs(a.localExposureShadowCrowding - b.localExposureShadowCrowding) * 0.10f +
        std::fabs(a.localExposureHaloStress - b.localExposureHaloStress) * 0.14f +
        std::fabs(a.localExposureFlatnessRisk - b.localExposureFlatnessRisk) * 0.10f +
        std::fabs(a.localExposureDamageRisk - b.localExposureDamageRisk) * 0.16f +
        std::fabs(a.subjectCenterPrior - b.subjectCenterPrior) * 0.08f +
        std::fabs(a.subjectReadabilityPressure - b.subjectReadabilityPressure) * 0.12f +
        std::fabs(a.subjectProtectionPressure - b.subjectProtectionPressure) * 0.12f +
        std::fabs(a.subjectMoodPreservationPressure - b.subjectMoodPreservationPressure) * 0.10f +
        std::fabs(a.subjectImportanceConfidence - b.subjectImportanceConfidence) * 0.08f +
        std::fabs(a.centerMeanLuma - b.centerMeanLuma) * 0.18f +
        std::fabs(a.subjectMarkedCoverage - b.subjectMarkedCoverage) * 0.08f +
        std::fabs(a.subjectMarkedPositiveCoverage - b.subjectMarkedPositiveCoverage) * 0.10f +
        std::fabs(a.subjectMarkedMeanLuma - b.subjectMarkedMeanLuma) * 0.16f +
        std::fabs(a.subjectMarkedReadabilityScore - b.subjectMarkedReadabilityScore) * 0.12f +
        std::fabs(a.subjectMarkedProtectionRisk - b.subjectMarkedProtectionRisk) * 0.14f +
        std::fabs(a.subjectMarkedMoodPreservationScore - b.subjectMarkedMoodPreservationScore) * 0.08f +
        std::fabs(a.subjectMarkedLowPriorityPressure - b.subjectMarkedLowPriorityPressure) * 0.08f;
}

bool EditorRenderWorker::Initialize(GLFWwindow* sharedWindow) {
    if (m_Thread.joinable()) {
        return true;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    m_WorkerWindow = glfwCreateWindow(16, 16, "Stack Editor Render Worker", nullptr, sharedWindow);
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif
    if (!m_WorkerWindow) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_StopRequested = false;
        m_HasPending = false;
        m_ActiveOpenGlTask.reset();
        m_InvalidBeforeGeneration = 0;
        m_NextSchedulingSerial = 1;
        m_InvalidBeforeSchedulingSerial = 0;
        m_Rendering = false;
        m_InitComplete = false;
        m_InitSucceeded = false;
        m_InitError.clear();
        m_Busy = false;
    }
    try {
        m_Thread = std::thread([this]() {
            try {
                ThreadMain();
            } catch (const std::exception& error) {
                std::cerr
                    << "[EditorRenderWorker] Worker thread failed: "
                    << error.what() << "\n";
                HandleThreadFailure(error.what());
            } catch (...) {
                std::cerr
                    << "[EditorRenderWorker] Worker thread failed with an "
                       "unknown exception.\n";
                HandleThreadFailure("unknown worker-thread exception");
            }
        });
    } catch (const std::exception& error) {
        std::cerr << "[EditorRenderWorker] Could not start worker thread: "
                  << error.what() << "\n";
        if (m_WorkerWindow) {
            glfwDestroyWindow(m_WorkerWindow);
            m_WorkerWindow = nullptr;
        }
        return false;
    } catch (...) {
        std::cerr
            << "[EditorRenderWorker] Could not start worker thread.\n";
        if (m_WorkerWindow) {
            glfwDestroyWindow(m_WorkerWindow);
            m_WorkerWindow = nullptr;
        }
        return false;
    }

    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Cv.wait(lock, [this]() {
            return m_InitComplete;
        });
        if (m_InitSucceeded) {
            return true;
        }
        std::cerr << "[EditorRenderWorker] Failed to initialize worker pipeline";
        if (!m_InitError.empty()) {
            std::cerr << ": " << m_InitError;
        }
        std::cerr << "\n";
        m_StopRequested = true;
    }

    m_Cv.notify_all();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
    if (m_WorkerWindow) {
        glfwDestroyWindow(m_WorkerWindow);
        m_WorkerWindow = nullptr;
    }
    return false;
}

void EditorRenderWorker::HandleThreadFailure(
    const char* message) noexcept {
    std::queue<std::shared_ptr<OpenGlTaskState>> abandonedTasks;
    std::shared_ptr<OpenGlTaskState> activeTask;
    try {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (!m_InitComplete) {
            m_InitSucceeded = false;
            try {
                m_InitError =
                    message != nullptr
                    ? message
                    : "render worker failed";
            } catch (...) {
                m_InitError.clear();
            }
            m_InitComplete = true;
        }
        while (!m_CompletedTimings.empty()) m_CompletedTimings.pop();
        while (!m_CompletedViewportTimings.empty()) m_CompletedViewportTimings.pop();
        while (!m_Completed.empty()) {
            Result stale = std::move(m_Completed.front());
            m_Completed.pop_front();
            try {
                ReleaseResultResources(stale);
            } catch (...) {
                // The worker boundary must remain noexcept even if cleanup
                // encounters an unexpected host-side failure.
            }
        }
        if (m_FallbackCompleted) {
            ReleaseResultResources(*m_FallbackCompleted);
            m_FallbackCompleted.reset();
        }
        m_Pending = {};
        m_HasPending = false;
        abandonedTasks.swap(m_OpenGlTasks);
        activeTask = std::move(m_ActiveOpenGlTask);
        m_Busy = false;
        m_StopRequested = true;
        m_ProgressCompletedSteps = 0;
        m_ProgressTotalSteps = 0;
        m_ProgressLabel.clear();
    } catch (...) {
        // std::thread entry points cannot permit an exception to escape.
    }
    const auto failTask = [message](
        const std::shared_ptr<OpenGlTaskState>& state) noexcept {
        if (!state) return;
        try {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->success = false;
            try {
                state->error = message != nullptr
                    ? std::string("The OpenGL worker failed: ") + message
                    : "The OpenGL worker failed.";
            } catch (...) {
                state->error.clear();
            }
            state->completed = true;
        } catch (...) {
            return;
        }
        state->cv.notify_all();
    };
    failTask(activeTask);
    while (!abandonedTasks.empty()) {
        const auto state = std::move(abandonedTasks.front());
        abandonedTasks.pop();
        failTask(state);
    }
    m_Cv.notify_all();
    try {
        DrainDenoiseForShutdown();
        m_CalibrationPipeline.reset();
        m_PersistentPipeline.reset();
    } catch (...) {
    }
    glfwMakeContextCurrent(nullptr);
}

void EditorRenderWorker::RequestStopForShutdown() {
    Snapshot abandoned;
    std::queue<std::shared_ptr<OpenGlTaskState>> abandonedTasks;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_StopRequested = true;
        if (m_HasPending) {
            abandoned = std::move(m_Pending);
            m_Pending = {};
        }
        m_HasPending = false;
        abandonedTasks.swap(m_OpenGlTasks);
    }
    while (!abandonedTasks.empty()) {
        const auto state = std::move(abandonedTasks.front());
        abandonedTasks.pop();
        if (!state) continue;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->success = false;
            state->error = "The OpenGL worker is shutting down.";
            state->completed = true;
        }
        state->cv.notify_all();
    }
    m_Cv.notify_all();
}

void EditorRenderWorker::Shutdown() {
    RequestStopForShutdown();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
    if (m_WorkerWindow) {
        glfwDestroyWindow(m_WorkerWindow);
        m_WorkerWindow = nullptr;
    }
}

bool EditorRenderWorker::HasPendingOrBusyForShutdown() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_HasPending || !m_OpenGlTasks.empty() || m_Busy.load();
}

bool EditorRenderWorker::IsWaitingForRawDenoiseCompletion() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return !m_Rendering && HasDenoiseWorkLocked() && !m_HasPending &&
        m_OpenGlTasks.empty() && !m_ActiveOpenGlTask;
}

void EditorRenderWorker::InvalidateSnapshotsBefore(std::uint64_t generation) {
    Snapshot abandoned;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_InvalidBeforeGeneration = std::max(m_InvalidBeforeGeneration, generation);
        m_InvalidBeforeSchedulingSerial = std::max(
            m_InvalidBeforeSchedulingSerial, m_NextSchedulingSerial);
        if (m_HasPending && m_Pending.generation < m_InvalidBeforeGeneration) {
            abandoned = std::move(m_Pending);
            m_Pending = {};
            m_HasPending = false;
        }
        for (auto& [id, owner] : m_OwnerStates) {
            (void)id;
            owner.continuation.reset();
            owner.continuationReady = false;
            owner.resetDenoiseRequested = true;
        }
        if (m_Busy.load()) {
            m_ProgressLabel = "Cancelling stale render...";
        }
    }
}

void EditorRenderWorker::CancelActiveAndPending() {
    Snapshot abandoned;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_InvalidBeforeSchedulingSerial = std::max(
            m_InvalidBeforeSchedulingSerial, m_NextSchedulingSerial);
        if (m_HasPending) {
            abandoned = std::move(m_Pending);
            m_Pending = {};
            m_HasPending = false;
        }
        for (auto& [id, owner] : m_OwnerStates) {
            (void)id;
            owner.continuation.reset();
            owner.continuationReady = false;
            owner.resetDenoiseRequested = true;
        }
        if (m_Busy.load()) {
            m_ProgressLabel = "Cancelling stale render...";
        }
    }
}

void EditorRenderWorker::UpdateGraphContext(const Stack::GraphRendering::RequestTag& context) {
    Snapshot abandoned;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        if (m_LatestGraphRequest.enabled &&
            !Stack::GraphRendering::SameContext(m_LatestGraphRequest, context)) {
            m_InvalidBeforeSchedulingSerial = m_NextSchedulingSerial;
            if (m_HasPending) {
                abandoned = std::move(m_Pending);
                m_Pending = {};
                m_HasPending = false;
            }
            auto& owner = m_OwnerStates[0];
            owner.continuation.reset();
            owner.continuationReady = false;
            owner.resetDenoiseRequested = true;
        }
        m_LatestGraphRequest = context;
    }
}

bool EditorRenderWorker::IsAvailable() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_InitSucceeded && !m_StopRequested;
}

bool EditorRenderWorker::Submit(Snapshot snapshot) {
    const int progressTotal = EstimateRenderProgressStepCount(snapshot);
    Snapshot superseded;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        auto& owner = m_OwnerStates[snapshot.ownerId];
        if (m_StopRequested ||
            snapshot.generation < m_InvalidBeforeGeneration || owner.released ||
            snapshot.generation < owner.invalidBeforeGeneration) {
            return false;
        }
        snapshot.schedulingSerial = m_NextSchedulingSerial++;
        owner.latestGeneration = snapshot.generation;
        owner.latestSerial = snapshot.schedulingSerial;
        owner.latestGraphRequest = snapshot.graphRequest;
        owner.continuation.reset();
        owner.continuationReady = false;
        owner.resumeOriginalPurpose = false;
        const bool replacingInFlightWork = m_Busy.load() || m_HasPending;
        if (m_HasPending) {
            // A service continuation may be superseded before the worker
            // takes it. Preserve another owner's wakeup in that case.
            if (m_Pending.ownerId != snapshot.ownerId) {
                auto& previous = m_OwnerStates[m_Pending.ownerId];
                previous.denoisePurpose = m_Pending.rawRenderPurpose;
                previous.continuation = std::move(m_Pending);
                previous.continuationReady = true;
                previous.resumeOriginalPurpose = true;
            }
            superseded = std::move(m_Pending);
            m_Pending = {};
        }
        m_LatestGraphRequest = snapshot.graphRequest;
        m_Pending = std::move(snapshot);
        m_HasPending = true;
        m_ProgressCompletedSteps = 0;
        m_ProgressTotalSteps = progressTotal;
        try {
            m_ProgressLabel =
                replacingInFlightWork ? "Queued newer render..." : "Queued render...";
        } catch (...) {
            m_ProgressLabel.clear();
        }
    }
    m_Cv.notify_one();
    return true;
}

bool EditorRenderWorker::TryConsumeCompleted(Result& result) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (!m_CompletedTimings.empty()) {
        result = std::move(m_CompletedTimings.front());
        m_CompletedTimings.pop();
        return true;
    }
    if (!m_Completed.empty()) {
        result = std::move(m_Completed.front());
        m_Completed.pop_front();
        return true;
    }
    if (!m_FallbackCompleted.has_value()) {
        return false;
    }
    result = std::move(*m_FallbackCompleted);
    m_FallbackCompleted.reset();
    return true;
}

EditorRenderWorker::RenderProgress EditorRenderWorker::GetProgress() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    RenderProgress progress;
    progress.busy = m_Busy.load() || m_HasPending || !m_OpenGlTasks.empty();
    progress.completedSteps = m_ProgressCompletedSteps;
    progress.totalSteps = m_ProgressTotalSteps;
    progress.label = m_ProgressLabel;
    return progress;
}

void EditorRenderWorker::SetProgress(int completedSteps, int totalSteps, std::string label) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ProgressCompletedSteps = std::max(0, completedSteps);
    m_ProgressTotalSteps = std::max(1, totalSteps);
    m_ProgressLabel = std::move(label);
}

void EditorRenderWorker::AdvanceProgress(std::string label) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ProgressCompletedSteps = std::min(m_ProgressTotalSteps, m_ProgressCompletedSteps + 1);
    if (!label.empty()) {
        m_ProgressLabel = std::move(label);
    }
}

bool EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(
    std::uint64_t currentGeneration,
    bool stopRequested,
    bool hasPendingSnapshot,
    std::uint64_t pendingGeneration) {
    return stopRequested || (hasPendingSnapshot && pendingGeneration > currentGeneration);
}

std::string EditorRenderWorker::BuildDevelopCandidateProgressLabelForValidation(
    const std::string& candidateLabel,
    const std::string& candidateRevisionStage,
    int candidateIndex,
    int candidateCount) {
    return BuildDevelopCandidateProgressLabel(
        candidateLabel,
        candidateRevisionStage,
        candidateIndex,
        candidateCount);
}

bool EditorRenderWorker::ShouldAbortStaleSnapshot(
    const Snapshot& snapshot) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_StopRequested ||
        (m_HasPending &&
         m_Pending.schedulingSerial > snapshot.schedulingSerial &&
         (m_Pending.ownerId != snapshot.ownerId ||
          !Stack::GraphRendering::MayFinishActive(snapshot.graphRequest, m_Pending.graphRequest))) ||
        IsOwnerSnapshotInvalidLocked(snapshot.ownerId, snapshot.generation,
            snapshot.schedulingSerial, snapshot.rawRenderPurpose);
}

void EditorRenderWorker::ThreadMain() {
    bool initSucceeded = false;
    std::string initError;
    try {
        glfwMakeContextCurrent(m_WorkerWindow);
        if (!LoadGLFunctions()) {
            initError = "OpenGL function loading failed";
        } else {
            m_PersistentPipeline = std::make_unique<RenderPipeline>();
            m_PersistentPipeline->Initialize();
            initSucceeded = true;
        }
    } catch (const std::exception& e) {
        initError = e.what();
    } catch (...) {
        initError = "unknown exception";
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_InitSucceeded = initSucceeded;
        m_InitError = initError;
        m_InitComplete = true;
    }
    m_Cv.notify_all();

    if (!initSucceeded) {
        m_CalibrationPipeline.reset();
        m_PersistentPipeline.reset();
        glfwMakeContextCurrent(nullptr);
        return;
    }

    while (true) {
        PollViewportTimings();
        Snapshot snapshot;
        std::shared_ptr<OpenGlTaskState> openGlTask;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Cv.wait_for(lock, std::chrono::milliseconds(16), [this]() {
                return m_StopRequested || m_HasPending ||
                    !m_OpenGlTasks.empty();
            });
            if (m_StopRequested) {
                break;
            }
            PollDenoiseContinuationsLocked();
            const auto dedicated = m_OwnerStates.find(0);
            const bool denoiseCompletionReady =
                !m_HasPending &&
                m_OpenGlTasks.empty() &&
                dedicated != m_OwnerStates.end() &&
                dedicated->second.continuationReady &&
                dedicated->second.continuation.has_value();
            if (!m_OpenGlTasks.empty() && !m_HasPending) {
                openGlTask = std::move(m_OpenGlTasks.front());
                m_OpenGlTasks.pop();
                m_ActiveOpenGlTask = openGlTask;
            } else if (m_HasPending) {
                snapshot = std::move(m_Pending);
                m_Pending = {};
                m_HasPending = false;
            } else if (denoiseCompletionReady) {
                auto& owner = dedicated->second;
                snapshot = std::move(*owner.continuation);
                owner.continuation.reset();
                owner.continuationReady = false;
                if (!owner.resumeOriginalPurpose)
                    snapshot.rawRenderPurpose = RawRenderPurpose::DenoiseCompletion;
                owner.resumeOriginalPurpose = false;
                snapshot.schedulingSerial = m_NextSchedulingSerial++;
                owner.latestSerial = snapshot.schedulingSerial;
                owner.denoisePurpose = snapshot.rawRenderPurpose;
            } else {
                m_Busy = HasDenoiseWorkLocked();
                continue;
            }
            m_Rendering = !openGlTask;
            if (m_Rendering) {
                m_RenderingOwner = snapshot.ownerId;
            }
            m_Busy = true;
        }
        if (openGlTask) {
            bool success = false;
            std::string taskError;
            try {
                success = openGlTask->task(taskError);
                if (!success && taskError.empty())
                    taskError = "The OpenGL compute task failed.";
            } catch (const std::exception& exception) {
                taskError = exception.what();
            } catch (...) {
                taskError = "The OpenGL compute task failed unexpectedly.";
            }
            {
                std::lock_guard<std::mutex> lock(openGlTask->mutex);
                openGlTask->success = success;
                openGlTask->error = std::move(taskError);
                openGlTask->completed = true;
            }
            openGlTask->cv.notify_all();
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                m_ActiveOpenGlTask.reset();
                m_Busy = m_HasPending || !m_OpenGlTasks.empty() ||
                    HasDenoiseWorkLocked();
            }
            continue;
        }
        if (ShouldAbortStaleSnapshot(snapshot)) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            RetainDenoiseContinuationLocked(snapshot);
            m_Rendering = false;
            m_Busy = m_HasPending || !m_OpenGlTasks.empty() ||
                HasDenoiseWorkLocked();
            if (!m_Busy.load()) {
                m_ProgressCompletedSteps = m_ProgressTotalSteps;
                m_ProgressLabel = "Stale render cancelled.";
            }
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (DeferAuthoritativeRenderLocked(snapshot)) {
                m_Rendering = false;
                m_Busy = true;
                continue;
            }
        }
        SetProgress(0, EstimateRenderProgressStepCount(snapshot), "Starting render...");

        const auto workerStart = std::chrono::steady_clock::now();
        Result result = RenderSnapshot(snapshot);
        const auto workerEnd = std::chrono::steady_clock::now();
        result.telemetry.queueWaitMs =
            snapshot.telemetry.queuedAt.time_since_epoch().count() == 0
                ? 0.0
                : MillisecondsBetween(
                      snapshot.telemetry.queuedAt,
                      workerStart);
        if (m_PersistentPipeline && snapshot.rawRenderPurpose != RawRenderPurpose::ViewportCalibration &&
            !snapshot.rawWorkspace.sourceKey.empty()) {
            result.cacheStateMeasured = true;
            result.nativeCachedStages = m_PersistentPipeline->RawViewportCachedStages(snapshot.rawWorkspace.recipe, 0);
            result.cacheEdge = m_PersistentPipeline->GetRawViewportCacheEdge();
            result.cachedStages = m_PersistentPipeline->RawViewportCachedStages(snapshot.rawWorkspace.recipe, result.cacheEdge);

        }
        if (result.graphRequest.enabled && result.outputTexture.texture != 0) {
            // Graph geometry can change the final extent beyond RAW crop settings.
            result.rawWorkspace.expectedNativeOutputWidth = result.outputTexture.width;
            result.rawWorkspace.expectedNativeOutputHeight = result.outputTexture.height;
        }
        result.telemetry.workerTotalMs =
            MillisecondsBetween(workerStart, std::chrono::steady_clock::now());
        result.telemetry.workerStarted = true;
        result.telemetry.sourceTransferredBytes = snapshot.rawSessionCommand
            ? 0u
            : snapshot.sourcePixels.size();
        result.telemetry.publishedTextureBytes =
            result.outputTexture.texture != 0
                ? static_cast<std::size_t>(
                      std::max(0, result.outputTexture.width)) *
                    static_cast<std::size_t>(
                      std::max(0, result.outputTexture.height)) * 8u
                : 0u;
        result.telemetry.readbackTransferredBytes = result.pixels.size();
        for (const SharedTextureTile& tile : result.outputTiles.tiles) {
            result.telemetry.publishedTextureBytes +=
                static_cast<std::size_t>(std::max(0, tile.haloWidth)) *
                static_cast<std::size_t>(std::max(0, tile.haloHeight)) * 8u;
        }
        result.telemetry.superseded =
            result.error == "Render superseded by a newer snapshot.";
        CaptureViewportTiming(snapshot,result);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            RetainDenoiseContinuationLocked(snapshot);
            m_Rendering = false;
            if (result.rawRenderPurpose == RawRenderPurpose::ViewportCalibration && result.calibration.renderMs > 0.0 &&
                (result.telemetry.superseded || IsOwnerResultStaleLocked(result))) {
                // Numeric measurements have a longer lifetime than presentation
                // requests. Keep completed passes even if the next edit canceled
                // the remaining passes or superseded the ordinary result queue.
                try {
                    Result timing;
                    timing.ownerId = result.ownerId;
                    timing.rawRenderPurpose = RawRenderPurpose::ViewportCalibration;
                    timing.rawWorkspace.sourceKey = result.rawWorkspace.sourceKey;
                    timing.rawWorkspace.sourceHash = result.rawWorkspace.sourceHash;
                    timing.workloadKeys = result.workloadKeys;
                    timing.timingRepresentation=result.timingRepresentation;
                    timing.telemetry.timingHistoryEpoch=result.telemetry.timingHistoryEpoch;
                    timing.calibration = result.calibration;
                    timing.telemetry.superseded = true;
                    m_CompletedTimings.push(std::move(timing));
                } catch (const std::bad_alloc&) { /* Foreground work retains priority. */ }
            }
            const bool carriesCancellationAcknowledgement =
                result.rawWorkspace.preciseSolveResult.has_value() &&
                result.rawWorkspace.preciseSolveResult->canceled;
            const bool staleResult = IsOwnerResultStaleLocked(result);
            if (m_StopRequested ||
                (staleResult && !carriesCancellationAcknowledgement)) {
                ReleaseResultResources(result);
                m_Busy = m_HasPending || !m_OpenGlTasks.empty() ||
                    HasDenoiseWorkLocked();
                if (!m_Busy.load()) {
                    m_ProgressCompletedSteps = m_ProgressTotalSteps;
                    m_ProgressLabel =
                        m_StopRequested ? std::string{} : "Stale render cancelled.";
                }
                continue;
            }
            std::optional<Result> cancellationAcknowledgement;
            for (auto it = m_Completed.begin(); it != m_Completed.end();) {
                if (it->ownerId != result.ownerId) {
                    ++it;
                    continue;
                }
                Result stale = std::move(*it);
                it = m_Completed.erase(it);
                if (stale.rawRenderPurpose == RawRenderPurpose::ViewportCalibration && stale.calibration.renderMs > 0.0) {
                    stale.telemetry.superseded = true;
                    try { m_CompletedTimings.push(std::move(stale)); } catch (const std::bad_alloc&) {}
                    continue;
                }
                const bool staleIsCancellationAcknowledgement =
                    stale.rawWorkspace.preciseSolveResult.has_value() &&
                    stale.rawWorkspace.preciseSolveResult->canceled;
                if (!carriesCancellationAcknowledgement &&
                    staleIsCancellationAcknowledgement &&
                    (!cancellationAcknowledgement.has_value() ||
                        stale.generation >= cancellationAcknowledgement->generation)) {
                    if (cancellationAcknowledgement.has_value()) {
                        ReleaseResultResources(*cancellationAcknowledgement);
                    }
                    cancellationAcknowledgement = std::move(stale);
                } else {
                    ReleaseResultResources(stale);
                }
            }
            if (m_FallbackCompleted && m_FallbackCompleted->ownerId == result.ownerId) {
                ReleaseResultResources(*m_FallbackCompleted);
                m_FallbackCompleted.reset();
            }
            bool cancellationAcknowledgementQueued = false;
            bool resultQueued = false;
            bool resultStoredWithoutQueueAllocation = false;
            try {
                if (cancellationAcknowledgement.has_value()) {
                    m_Completed.push_back(
                        std::move(*cancellationAcknowledgement));
                    cancellationAcknowledgementQueued = true;
                }
                m_Completed.push_back(std::move(result));
                resultQueued = true;
            } catch (...) {
                if (!cancellationAcknowledgementQueued &&
                    cancellationAcknowledgement.has_value()) {
                    ReleaseResultResources(
                        *cancellationAcknowledgement);
                }
                try {
                    if (!m_FallbackCompleted) {
                        m_FallbackCompleted.emplace(std::move(result));
                        resultStoredWithoutQueueAllocation = true;
                    } else {
                        ReleaseResultResources(result);
                    }
                } catch (...) {
                    ReleaseResultResources(result);
                }
            }
            m_Busy = m_HasPending || !m_OpenGlTasks.empty() ||
                HasDenoiseWorkLocked();
            if (!m_Busy.load()) {
                m_ProgressCompletedSteps = m_ProgressTotalSteps;
                if (resultQueued || resultStoredWithoutQueueAllocation) {
                    m_ProgressLabel = "Render ready.";
                } else {
                    m_ProgressLabel.clear();
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        while (!m_CompletedTimings.empty()) m_CompletedTimings.pop();
        while (!m_CompletedViewportTimings.empty()) m_CompletedViewportTimings.pop();
        while (!m_Completed.empty()) {
            Result stale = std::move(m_Completed.front());
            m_Completed.pop_front();
            ReleaseResultResources(stale);
        }
        if (m_FallbackCompleted.has_value()) {
            ReleaseResultResources(*m_FallbackCompleted);
            m_FallbackCompleted.reset();
        }
        m_ProgressCompletedSteps = 0;
        m_ProgressTotalSteps = 0;
        m_ProgressLabel.clear();
        m_Rendering = false;
        m_Busy = false;
    }

    DrainDenoiseForShutdown();
    m_CalibrationPipeline.reset();
    for (auto& timing : m_PendingViewportTimings) Raw::ViewportGpuTiming::Release(timing.batch);
    m_PendingViewportTimings.clear();
    m_PersistentPipeline.reset();
    glfwMakeContextCurrent(nullptr);
}

EditorRenderWorker::Result EditorRenderWorker::RenderSnapshot(const Snapshot& snapshot) {
    if (snapshot.rawSessionCommand && snapshot.rawSessionTemplate) {
        const auto expansionBegin = std::chrono::steady_clock::now();
        Snapshot expanded = *snapshot.rawSessionTemplate;
        expanded.ownerId = snapshot.ownerId;
        expanded.generation = snapshot.generation;
        expanded.schedulingSerial = snapshot.schedulingSerial;
        expanded.lastAcceptedGeneration = snapshot.lastAcceptedGeneration;
        expanded.rawRenderPurpose = snapshot.rawRenderPurpose;
        expanded.telemetry = snapshot.telemetry;
        expanded.previewMaxDimension = snapshot.previewMaxDimension;
        expanded.rawWorkspace = snapshot.rawWorkspace;
        expanded.viewportTiling = snapshot.viewportTiling;
        expanded.previews = snapshot.previews;
        expanded.rawSessionTemplate.reset();
        expanded.rawSessionContractRevision =
            snapshot.rawSessionContractRevision;
        expanded.rawSessionCommand = true;

        expanded.graph.rawWorkspaceLocalRangeOverlayMode =
            expanded.rawWorkspace.localRangeOverlayMode;
        expanded.graph.rawWorkspaceLocalRangeTargetPreview =
            expanded.rawWorkspace.localRangeTargetPreview;
        expanded.graph.rawWorkspaceLocalRangeTargetSampleRequested =
            expanded.rawWorkspace.localRangeTargetSampleRequested;
        expanded.graph.rawWorkspaceLocalRangeTargetSampleU =
            expanded.rawWorkspace.localRangeTargetSampleU;
        expanded.graph.rawWorkspaceLocalRangeTargetSampleV =
            expanded.rawWorkspace.localRangeTargetSampleV;
        if (expanded.rawWorkspace.hasRecipe) {
            Stack::EditorRendering::ApplyRawRecipeOverlayToRenderGraph(
                expanded.graph,
                expanded.rawWorkspace.recipe,
                expanded.rawWorkspace.managedRawDecodeNodeId,
                expanded.rawWorkspace.managedToneCurveNodeId,
                expanded.rawWorkspace.managedViewTransformNodeId);
            expanded.rawWorkspace.graphScopeInputFingerprint =
                ResolveRawGraphScopeFingerprint(
                    expanded.rawWorkspace);
        }
        expanded.telemetry.sessionExpansionMs =
            MillisecondsBetween(
                expansionBegin,
                std::chrono::steady_clock::now());
        return RenderSnapshot(expanded);
    }

    Result result;
    result.ownerId = snapshot.ownerId;
    result.generation = snapshot.generation;
    result.graphRequest = snapshot.graphRequest;
    result.schedulingSerial = snapshot.schedulingSerial;
    result.lastAcceptedGeneration = snapshot.lastAcceptedGeneration;
    result.rawRenderPurpose = snapshot.rawRenderPurpose;
    result.telemetry = snapshot.telemetry;
    result.previewMaxDimension = snapshot.previewMaxDimension;
    result.rawWorkspace.sourceKey = snapshot.rawWorkspace.sourceKey;
    result.rawWorkspace.sourceHash = snapshot.rawWorkspace.sourceHash;
    result.rawWorkspace.recipeRevision =
        snapshot.rawWorkspace.recipeRevision;
    if (snapshot.rawWorkspace.hasRecipe) {
        const auto& recipe = snapshot.rawWorkspace.recipe;
        result.rawWorkspace.presentationFingerprint =
            Stack::EditorRendering::BuildRawPresentationFingerprint(recipe, snapshot.graph);
        const auto viewJson = recipe.viewTransform.layerJson.is_object() ? recipe.viewTransform.layerJson : nlohmann::json::object();
        result.rawWorkspace.viewportEncodedSrgb = Stack::RawRecipe::IsViewTransformEnabled(recipe) && viewJson.value("encodeSrgbOutput", recipe.technical.encodeSrgbOutput);
        result.rawWorkspace.viewportDiagnostic = recipe.rgbDenoise.diagnosticMode != Stack::RawRecipe::RawDenoiseDiagnosticMode::None ||
            (!snapshot.rawWorkspace.localRangeOverlayMode.empty() && snapshot.rawWorkspace.localRangeOverlayMode != "none") ||
            viewJson.value("debugFalseColor", false);
        const Stack::RawRecipe::RawCropRotationRecipe& crop =
            snapshot.rawWorkspace.recipe.cropRotation;
        const Stack::EditorRenderScheduling::RawPresentationExtent
            expectedExtent =
                Stack::EditorRenderScheduling::
                    ResolveExpectedRawPresentationExtent(
                        snapshot.rawWorkspace.fullFrameWidth,
                        snapshot.rawWorkspace.fullFrameHeight,
                        crop.rotationDegrees,
                        crop.cropEnabled,
                        crop.cropX,
                        crop.cropY,
                        crop.cropWidth,
                        crop.cropHeight);
        result.rawWorkspace.expectedNativeOutputWidth =
            expectedExtent.width;
        result.rawWorkspace.expectedNativeOutputHeight =
            expectedExtent.height;
    } else {
        result.rawWorkspace.expectedNativeOutputWidth =
            snapshot.rawWorkspace.fullFrameWidth;
        result.rawWorkspace.expectedNativeOutputHeight =
            snapshot.rawWorkspace.fullFrameHeight;
    }
    result.rawWorkspace.analysisCaptured = snapshot.rawWorkspace.analysisRequested;
    result.rawWorkspace.graphScopeInputFingerprint =
        ResolveRawGraphScopeFingerprint(
            snapshot.rawWorkspace);
    result.rawWorkspace.localRangeOverlayMode = snapshot.rawWorkspace.localRangeOverlayMode;
    result.rawWorkspace.localRangeTargetPreviewGeneration =
        snapshot.rawWorkspace.localRangeTargetPreview.generation;
    result.rawWorkspace.startPointCandidateRenderRequests =
        snapshot.rawWorkspace.startPointCandidateRenderRequests;
    result.rawWorkspace.localRangeTargetSample.u = snapshot.rawWorkspace.localRangeTargetSampleU;
    result.rawWorkspace.localRangeTargetSample.v = snapshot.rawWorkspace.localRangeTargetSampleV;
    result.rawWorkspace.cachePrewarmStage =
        snapshot.rawWorkspace.cachePrewarmStage;
    result.rawWorkspace.cachePrewarmFingerprint =
        snapshot.rawWorkspace.cachePrewarmFingerprint;

    try {
        const bool calibration = snapshot.rawRenderPurpose == RawRenderPurpose::ViewportCalibration;
        const bool privateOverview = snapshot.rawRenderPurpose == RawRenderPurpose::ViewportOverview &&
            snapshot.rawWorkspace.viewportDependencyEdge >= 0;
        if (!calibration && !privateOverview) m_CalibrationPipeline.reset();
        auto& owner = calibration || privateOverview ? m_CalibrationPipeline : m_PersistentPipeline;
        const bool calibrationResourcesUnprepared = calibration && !owner;
        if (!owner) {
            owner = std::make_unique<RenderPipeline>();
            owner->Initialize();
        }
        RenderPipeline& pipeline = *owner;
        if (!calibration && !privateOverview) BindDenoiseState(snapshot, pipeline);
        PrepareGraphResourceBudget(snapshot, pipeline, result);
        pipeline.BeginRawViewportTiming(!snapshot.rawWorkspace.sourceKey.empty());
        result.editStage = snapshot.rawWorkspace.editStage;
        if (snapshot.rawWorkspace.hasRecipe)
            result.workloadKeys = Raw::ViewportWorkloadKeys(snapshot.rawWorkspace.recipe);
        if (snapshot.rawWorkspace.graphWorkloadKeys.back())
            result.workloadKeys = snapshot.rawWorkspace.graphWorkloadKeys;
        if (snapshot.rawRenderPurpose==RawRenderPurpose::ViewportRefinement)
            result.workloadKeys=Raw::ViewportRefinementKeys(result.workloadKeys);
        std::size_t shape = 1;
        for (const auto& node : snapshot.graph.nodes) {
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,static_cast<int>(node.kind));
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,node.definitionId);
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,node.definitionVersion);
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,node.definitionHash);
            Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,static_cast<int>(node.technicalImageOperation));
            if (node.kind==RenderGraphNodeKind::Layer && node.nodeId!=snapshot.rawWorkspace.managedToneCurveNodeId &&
                node.nodeId!=snapshot.rawWorkspace.managedViewTransformNodeId)
                Stack::Renderer::RawDevelopmentCache::HashTypedJson(shape,node.layerJson);
            if (const auto& raw=node.rawDevelopment.embeddedRawData; raw) {
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,raw->metadata.mosaiced);
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,static_cast<int>(raw->metadata.pixelLayout));
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,static_cast<int>(raw->metadata.cfaPattern));
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,raw->metadata.rawWidth);
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,raw->metadata.rawHeight);
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,raw->reconstructedCameraRgb);
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,static_cast<int>(raw->normalizedMosaicInputContract));
                Stack::Renderer::RawDevelopmentCache::HashTypedValue(shape,!raw->linearFloatBuffer.empty());
            }
        }
        result.timingRepresentation = std::to_string(shape);
        if (!snapshot.graphRequest.enabled && snapshot.rawWorkspace.gpuWorkingBudgetBytes > 0) {
            pipeline.SetGraphCacheBudget(
                snapshot.rawWorkspace.gpuCacheBudgetBytes,
                snapshot.rawWorkspace.minimumRawStageCacheBytes);
        }
        const bool calibrationProxy = calibration && snapshot.previewMaxDimension > 0 &&
            snapshot.previewMaxDimension < std::max(snapshot.rawWorkspace.fullFrameWidth,
                snapshot.rawWorkspace.fullFrameHeight);
        const bool authoritativeOutput =
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport ||
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitInspection;
        // Seeded overviews reuse an existing reconstruction dependency. They
        // must not launch a second inference from their temporary pipeline.
        pipeline.SetRawRgbDenoiseAsyncEnabled(!authoritativeOutput && !calibration && !privateOverview);
        (void)pipeline.ConsumeRawRgbDenoiseAsyncCompletion();
        const int nativeEdge = std::max(snapshot.rawWorkspace.fullFrameWidth, snapshot.rawWorkspace.fullFrameHeight);
        const int processingEdge = nativeEdge > 0 && snapshot.previewMaxDimension >= nativeEdge ? 0 : snapshot.previewMaxDimension;
        pipeline.SetPreviewMaxDimension(processingEdge);
        if (!calibration && snapshot.rawWorkspace.hasRecipe) {
            const auto cached = pipeline.RawViewportCachedStages(snapshot.rawWorkspace.recipe,processingEdge);
            for (std::size_t i = 0; i < cached.size(); ++i)
                if (cached[i]) result.firstMeasuredStage = i + 1;
        }
        pipeline.SetRawViewportRequest(!snapshot.rawWorkspace.analysisRequested &&
            (snapshot.rawRenderPurpose == RawRenderPurpose::InteractivePresentation ||
             snapshot.rawRenderPurpose == RawRenderPurpose::ViewportRefinement)
            ? snapshot.rawWorkspace.viewport : Raw::ViewportRequest{});
        result.rawWorkspace.viewportGeneration = snapshot.rawWorkspace.viewport.generation;
        pipeline.SetRawDevelopmentInteractivePreview(
            calibration || (snapshot.rawRenderPurpose ==
                RawRenderPurpose::InteractivePresentation &&
            snapshot.telemetry.interactionActive));
        pipeline.SetRawDevelopmentColorWarpMaskPolicy(
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport ||
                    snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitInspection
                ? 2048
                : ((snapshot.telemetry.interactionActive || calibration) ? 384 : 1024),
            snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport);
        pipeline.SetRawDevelopmentViewportValidationEnabled(
            authoritativeOutput || snapshot.rawWorkspace.analysisRequested);
        pipeline.SetRawDevelopmentPreferredCacheInputStage(
            snapshot.rawWorkspace.preferredCacheInputStage);
        pipeline.SetRawDevelopmentGlobalExposureInteraction(
            snapshot.rawWorkspace.globalExposureInteractionActive);
        pipeline.SetRawDevelopmentAnalysisEnabled(snapshot.rawWorkspace.analysisRequested);
        pipeline.SetRawDevelopmentCachePrewarmRequest(
            snapshot.rawRenderPurpose == RawRenderPurpose::CachePrewarm &&
                snapshot.rawWorkspace.cachePrewarmStage.has_value(),
            snapshot.rawWorkspace.cachePrewarmStage.value_or(
                Stack::Renderer::RawDevelopmentCache::Stage::NeutralPlacement),
            snapshot.rawWorkspace.cachePrewarmStage && snapshot.rawWorkspace.hasRecipe
                ? Stack::Renderer::RawDevelopmentCache::BuildStageFingerprint(snapshot.rawWorkspace.recipe,
                    processingEdge, *snapshot.rawWorkspace.cachePrewarmStage) : snapshot.rawWorkspace.cachePrewarmFingerprint,
            snapshot.rawWorkspace.cachePrewarmByteBudget);
        pipeline.SetRawDevelopmentGraphScopeReadbackRequest(
            snapshot.rawWorkspace.graphScopeStage,
            snapshot.rawWorkspace.graphScopeStage == RawDevelopmentGraphScopeStage::None
                ? 0
                : std::clamp(
                    snapshot.rawWorkspace.graphScopeMaxDimension,
                    kRawWorkspaceGraphScopeMaxDimension,
                    2048));
        pipeline.SetRawDevelopmentGradingScopeReadbackRequest(
            snapshot.rawWorkspace.gradingScopeSource,
            snapshot.rawWorkspace.gradingScopeSource ==
                    RawDevelopmentGradingScopeSource::None
                ? 0
                : kRawWorkspaceGradingScopeMaxDimension,
            snapshot.generation,
            snapshot.rawWorkspace.sourceKey);
        auto collectReadyGradingScopePacket = [&]() {
            (void)pipeline.PollRawDevelopmentGradingScopeReadback();
            const RawDevelopmentGradingScopeReadback& packet =
                pipeline.GetRawDevelopmentGradingScopeReadback();
            if (packet.valid &&
                packet.source == snapshot.rawWorkspace.gradingScopeSource &&
                packet.sourceKey == snapshot.rawWorkspace.sourceKey) {
                result.rawWorkspace.gradingScopeVisualization = packet.visualization;
            }
        };
        collectReadyGradingScopePacket();
        const int totalProgressSteps = EstimateRenderProgressStepCount(snapshot);
        int progressCompleted = 0;
        auto reportProgress = [&](std::string label) {
            SetProgress(progressCompleted, totalProgressSteps, std::move(label));
        };
        auto finishProgressStep = [&](std::string label = {}) {
            progressCompleted = std::min(totalProgressSteps, progressCompleted + 1);
            SetProgress(progressCompleted, totalProgressSteps, std::move(label));
        };
        bool renderCancelled = false;
        auto shouldAbortStaleWork = [&]() {
            // Once any stage stops, this snapshot cannot become complete again.
            // The pending job can be withdrawn or replaced while the GPU stage
            // unwinds, so re-reading only the queue can misreport cancellation
            // as a render with no pixels.
            renderCancelled = renderCancelled || ShouldAbortStaleSnapshot(snapshot);
            return renderCancelled;
        };
        auto collectToneCurveAutoRewriteFeedback = [&]() {
            std::vector<ToneCurveAutoRewriteFeedback> feedbacks =
                pipeline.TakeToneCurveAutoRewriteFeedback();
            for (ToneCurveAutoRewriteFeedback& feedback : feedbacks) {
                if (!feedback.valid || feedback.nodeId <= 0) {
                    continue;
                }
                const auto existing = std::find_if(
                    result.toneCurveAutoRewrites.begin(),
                    result.toneCurveAutoRewrites.end(),
                    [&feedback](const ToneCurveAutoRewriteFeedback& candidate) {
                        return candidate.nodeId == feedback.nodeId;
                    });
                if (existing == result.toneCurveAutoRewrites.end()) {
                    result.toneCurveAutoRewrites.push_back(
                        std::move(feedback));
                } else {
                    *existing = std::move(feedback);
                }
            }
        };
        pipeline.SetRenderCancellationContext(
            snapshot.generation,
            shouldAbortStaleWork);
        auto reportSupersededWork = [&]() {
            reportProgress("Newer render queued; skipping stale feedback...");
        };
        auto resolveGraphImageSourceBuffer = [&](int sourceNodeId,
                                               SharedPixelBuffer& outBuffer,
                                               int& outWidth,
                                               int& outHeight,
                                               int& outChannels) -> bool {
            if (sourceNodeId <= 0) {
                return false;
            }
            const auto sourceIt = std::find_if(
                snapshot.graph.nodes.begin(),
                snapshot.graph.nodes.end(),
                [sourceNodeId](const RenderGraphNode& node) { return node.nodeId == sourceNodeId; });
            if (sourceIt == snapshot.graph.nodes.end() ||
                sourceIt->kind != RenderGraphNodeKind::Image ||
                sourceIt->image.pixels.empty() ||
                sourceIt->image.width <= 0 ||
                sourceIt->image.height <= 0) {
                return false;
            }

            outBuffer = sourceIt->image.pixels;
            outWidth = sourceIt->image.width;
            outHeight = sourceIt->image.height;
            outChannels = std::max(1, sourceIt->image.channels);
            return true;
        };
        auto loadSourceBuffer = [&](const SharedPixelBuffer& sourceBuffer,
                                    int sourceWidth,
                                    int sourceHeight,
                                    int sourceChannels) {
            pipeline.LoadSourceFromSharedPixels(
                sourceBuffer,
                sourceWidth,
                sourceHeight,
                sourceChannels);
        };
        reportProgress("Preparing render...");

        auto renderDevelopCandidateRequests = [&]() {
            RenderDevelopCandidateRequests(snapshot, pipeline, result, totalProgressSteps, progressCompleted);
        };

        auto renderPreviewRequests = [&]() {
            if (snapshot.previews.empty()) {
                return;
            }
            if (shouldAbortStaleWork()) {
                reportSupersededWork();
                return;
            }

            result.previews.reserve(snapshot.previews.size());
            RenderGraphSnapshot previewGraph = snapshot.graph;
            previewGraph.nodes.reserve(previewGraph.nodes.size() + 2u);
            previewGraph.links.reserve(previewGraph.links.size() + 2u);
            const Stack::Renderer::GraphExecution::GraphTopologyIndex
                previewTopology =
                    Stack::Renderer::GraphExecution::BuildGraphTopologyIndex(
                        previewGraph);
            auto findTransientNodeId =
                [&previewTopology](std::int64_t firstCandidate) {
                    for (std::int64_t candidate = firstCandidate;
                         candidate >=
                             static_cast<std::int64_t>(
                                 std::numeric_limits<int>::min());
                         --candidate) {
                        const int nodeId = static_cast<int>(candidate);
                        if (previewTopology.nodes.find(nodeId) ==
                            previewTopology.nodes.end()) {
                            return nodeId;
                        }
                    }
                    return 0;
                };
            const int frequencyTransformNodeId =
                findTransientNodeId(-1);
            const int frequencyViewNodeId =
                findTransientNodeId(
                    static_cast<std::int64_t>(
                        frequencyTransformNodeId) - 1);
            int previewIndex = 0;
            const int previewCount = static_cast<int>(snapshot.previews.size());
            const auto previewBegin = std::chrono::steady_clock::now();
            for (const PreviewRequest& request : snapshot.previews) {
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                reportProgress(
                    "Rendering preview " +
                    std::to_string(previewIndex + 1) +
                    "/" +
                    std::to_string(previewCount) +
                    "...");
                PreviewResult previewResult;
                previewResult.rawLayerId = request.rawLayerId;
                previewResult.rawMaskId = request.rawMaskId;
                previewResult.previewNodeId = request.previewNodeId;
                previewResult.dirtyGeneration = request.dirtyGeneration;

                const bool useRequestSourcePixels =
                    request.width > 0 &&
                    request.height > 0;
                SharedPixelBuffer sourceBuffer = useRequestSourcePixels
                    ? request.sourcePixels
                    : snapshot.sourcePixels;
                int sourceWidth = useRequestSourcePixels ? request.width : snapshot.width;
                int sourceHeight = useRequestSourcePixels ? request.height : snapshot.height;
                int sourceChannels = useRequestSourcePixels ? request.channels : snapshot.channels;
                if ((sourceWidth <= 0 || sourceHeight <= 0) && request.sourceNodeId > 0) {
                    resolveGraphImageSourceBuffer(
                        request.sourceNodeId,
                        sourceBuffer,
                        sourceWidth,
                        sourceHeight,
                        sourceChannels);
                }
                if (sourceWidth <= 0 || sourceHeight <= 0) {
                    previewResult.error = "No source image.";
                    result.previews.push_back(std::move(previewResult));
                    ++previewIndex;
                    finishProgressStep("Preview skipped.");
                    continue;
                }

                const bool layerThumbnail = !request.rawLayerId.empty();
                pipeline.SetPreviewMaxDimension(layerThumbnail ? 256 : processingEdge);
                loadSourceBuffer(sourceBuffer, sourceWidth, sourceHeight, sourceChannels);
                bool temporaryFrequencyView = false;
                if (layerThumbnail) {
                    auto thumbnailGraph = snapshot.graph;
                    if (!Stack::Project::ConfigureRawLayerThumbnail(thumbnailGraph,
                            request.rawLayerId, request.rawMaskId, previewResult.error)) {
                        result.previews.push_back(std::move(previewResult));
                        ++previewIndex;
                        finishProgressStep("Layer thumbnail skipped.");
                        continue;
                    }
                    pipeline.ExecuteGraph(thumbnailGraph);
                } else if (request.frequencySpectrumInput) {
                    if (frequencyTransformNodeId == 0 ||
                        frequencyViewNodeId == 0) {
                        previewResult.error =
                            "No transient node IDs are available.";
                        result.previews.push_back(
                            std::move(previewResult));
                        ++previewIndex;
                        finishProgressStep("Preview skipped.");
                        continue;
                    }
                    RenderGraphNode transform;
                    transform.nodeId = frequencyTransformNodeId;
                    transform.kind = RenderGraphNodeKind::FrequencyFft;
                    transform.frequencyFftSettings.edgePolicy =
                        request.frequencyEdgePolicy;
                    RenderGraphNode view;
                    view.nodeId = frequencyViewNodeId;
                    view.kind = RenderGraphNodeKind::SpectrumView;
                    view.spectrumViewSettings.mode =
                        RenderSpectrumViewMode::Magnitude;
                    view.spectrumViewSettings.centerDc = true;
                    previewGraph.nodes.push_back(std::move(transform));
                    previewGraph.nodes.push_back(std::move(view));
                    previewGraph.links.push_back(RenderGraphLink{
                        request.sourceNodeId,
                        request.sourceSocketId,
                        frequencyTransformNodeId,
                        EditorNodeGraph::kChannelInputSocketId
                    });
                    previewGraph.links.push_back(RenderGraphLink{
                        frequencyTransformNodeId,
                        EditorNodeGraph::kSpectrumOutputSocketId,
                        frequencyViewNodeId,
                        EditorNodeGraph::kSpectrumInputSocketId
                    });
                    previewGraph.outputNodeId =
                        frequencyViewNodeId;
                    previewGraph.outputSocketId =
                        EditorNodeGraph::kImageOutputSocketId;
                    temporaryFrequencyView = true;
                } else {
                    previewGraph.outputNodeId =
                        request.sourceNodeId;
                    previewGraph.outputSocketId =
                        request.sourceSocketId;
                }
                if (temporaryFrequencyView) {
                    pipeline.ExecuteGraph(previewGraph);
                    previewGraph.nodes.resize(
                        previewTopology.nodeCount);
                    previewGraph.links.resize(
                        previewTopology.linkCount);
                } else if (!layerThumbnail) {
                    pipeline.ExecuteGraph(
                        previewGraph,
                        previewTopology);
                }
                collectToneCurveAutoRewriteFeedback();
                previewResult.pixels = request.scopeAnalysis
                    ? pipeline.GetScopesPixels(previewResult.width, previewResult.height)
                    : pipeline.GetPreviewPixels(previewResult.width, previewResult.height, layerThumbnail ? 128 : 512);
                if (request.scopeAnalysis && !previewResult.pixels.empty()) {
                    previewResult.scopeData = AnalyzeGraphScopePixels(
                        previewResult.pixels, previewResult.width, previewResult.height);
                }
                previewResult.success = !previewResult.pixels.empty();
                if (!previewResult.success) {
                    previewResult.error = RenderFailureReason(pipeline,
                        "Could not read the rendered preview image.");
                }
                result.previews.push_back(std::move(previewResult));
                ++result.renderedPreviewCount;
                ++previewIndex;
                finishProgressStep("Preview rendered.");
            }
            result.previewRenderMs += MillisecondsBetween(previewBegin, std::chrono::steady_clock::now());
            pipeline.SetPreviewMaxDimension(processingEdge);
        };

        // A precise RAW solve is an isolated job, not a background replacement
        // for the live viewport render. Running the normal RAW graph first in
        // this shared context can overlap the UI-owned RAW pipeline and has
        // caused driver-level failures. Keep the current preview untouched,
        // evaluate only isolated pipelines here, then return the verified
        // recipe record to the main thread for its one atomic commit.
        if (snapshot.rawWorkspace.preciseSolveRequest.has_value()) {
            Stack::PreciseIntegration::NativeSolveCallbacks preciseCallbacks;
            preciseCallbacks.shouldCancel = shouldAbortStaleWork;
            preciseCallbacks.reportProgress =
                [&](const std::string& label, int completed, int total) {
                    SetProgress(completed, std::max(1, total), label);
                };
            result.rawWorkspace.preciseSolveResult =
                Stack::PreciseIntegration::RunNativePreciseSolve(
                    *snapshot.rawWorkspace.preciseSolveRequest,
                    preciseCallbacks);
            result.success = true;
            SetProgress(
                52,
                52,
                result.rawWorkspace.preciseSolveResult->canceled
                    ? "Precise Starting Point canceled."
                    : (result.rawWorkspace.preciseSolveResult->candidate.valid
                        ? "Precise candidate verified."
                        : "Precise solve failed safely."));
            return result;
        }

        if (!snapshot.compositeOutputs.empty()) {
            result.success = true;
            result.compositeOutputs.reserve(snapshot.compositeOutputs.size());
            // The graph payload can include embedded image buffers and large
            // node settings. Composite rendering changes only its requested
            // Output identity, so copy it once for the batch rather than once
            // per canvas item.
            RenderGraphSnapshot compositeGraph = snapshot.graph;
            const Stack::Renderer::GraphExecution::GraphTopologyIndex
                compositeTopology =
                    Stack::Renderer::GraphExecution::BuildGraphTopologyIndex(
                        compositeGraph);
            int compositeIndex = 0;
            const int compositeCount = static_cast<int>(snapshot.compositeOutputs.size());
            const auto compositeBegin = std::chrono::steady_clock::now();
            for (const CompositeOutputRequest& request : snapshot.compositeOutputs) {
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                reportProgress(
                    "Rendering canvas output " +
                    std::to_string(compositeIndex + 1) +
                    "/" +
                    std::to_string(compositeCount) +
                    "...");
                CompositeOutputResult outputResult;
                outputResult.outputNodeId = request.outputNodeId;
                outputResult.dirtyGeneration = request.dirtyGeneration;
                outputResult.chainFingerprint = request.chainFingerprint;
                SharedPixelBuffer sourceBuffer = request.sourcePixels;
                int sourceWidth = request.width;
                int sourceHeight = request.height;
                int sourceChannels = request.channels;
                if ((sourceWidth <= 0 || sourceHeight <= 0) && request.sourceNodeId > 0) {
                    resolveGraphImageSourceBuffer(
                        request.sourceNodeId,
                        sourceBuffer,
                        sourceWidth,
                        sourceHeight,
                        sourceChannels);
                }
                if (sourceWidth <= 0 || sourceHeight <= 0) {
                    outputResult.error = "No source image.";
                    result.success = false;
                    result.compositeOutputs.push_back(std::move(outputResult));
                    ++compositeIndex;
                    finishProgressStep("Canvas output skipped.");
                    continue;
                }

                loadSourceBuffer(sourceBuffer, sourceWidth, sourceHeight, sourceChannels);
                compositeGraph.outputNodeId = request.outputNodeId;
                compositeGraph.outputSocketId =
                    EditorNodeGraph::kImageOutputSocketId;
                pipeline.ExecuteGraph(
                    compositeGraph,
                    compositeTopology);
                collectToneCurveAutoRewriteFeedback();
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                outputResult.pixels = pipeline.GetOutputPixels(outputResult.width, outputResult.height);
                outputResult.success = !outputResult.pixels.empty();
                if (outputResult.success && request.preparePixels) {
                    Stack::EditorRendering::PreparedCompositePixels prepared;
                    outputResult.success = Stack::EditorRendering::PrepareCompositePixels(
                        std::move(outputResult.pixels), outputResult.width, outputResult.height,
                        request.trimPadding, request.keepFullFrame, prepared);
                    if (outputResult.success) {
                        outputResult.pixels = std::move(prepared.pixels);
                        outputResult.width = prepared.width;
                        outputResult.height = prepared.height;
                        outputResult.pixelsPrepared = true;
                    } else {
                        outputResult.error = "Canvas output preparation failed.";
                    }
                }
                if (!outputResult.success) {
                    if (outputResult.error.empty()) outputResult.error = RenderFailureReason(pipeline,
                        "Could not read the rendered canvas image.");
                    result.success = false;
                }
                result.compositeOutputs.push_back(std::move(outputResult));
                ++result.renderedCompositeCount;
                ++compositeIndex;
                finishProgressStep("Canvas output rendered.");
            }
            result.compositeRenderMs += MillisecondsBetween(compositeBegin, std::chrono::steady_clock::now());
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }

        if (!snapshot.outputConnected) {
            result.success = snapshot.rawWorkspace.sourceKey.empty() ||
                !RawRenderPurposeMayPublishPresentation(
                    snapshot.rawRenderPurpose);
            if (!result.success) {
                result.error =
                    "RAW presentation has no connected graph output.";
            }
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }
        if (snapshot.width <= 0 || snapshot.height <= 0) {
            result.error = "No source image.";
            finishProgressStep("No source image.");
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }
        loadSourceBuffer(snapshot.sourcePixels, snapshot.width, snapshot.height, snapshot.channels);
        if (snapshot.rawRenderPurpose == RawRenderPurpose::ViewportCalibration) {
            result.calibration.edge = snapshot.previewMaxDimension;
            result.calibration.changingStage = static_cast<std::size_t>(snapshot.rawWorkspace.editStage);
            const bool firstUse = snapshot.rawWorkspace.calibrationFirstUse || calibrationResourcesUnprepared;
            const int passes = firstUse ? 4 : 3;
            const bool reuseReconstruction = Stack::RawRecipe::IsRgbDenoiseActive(snapshot.rawWorkspace.recipe.rgbDenoise);
            std::array<Raw::ViewportStageCosts, 3> warmStages {};
            std::array<double, 3> warmTimes {};
            int warmCount = 0;
            if (reuseReconstruction) result.calibration.firstMeasuredStage = 2;
            for (int pass = 0; pass < passes; ++pass) {
                if (shouldAbortStaleWork()) return result;
                SetProgress(pass, passes, "Measuring RAW viewport, pass " + std::to_string(pass + 1) + "/" + std::to_string(passes));
                pipeline.PrepareRawCalibrationPass(snapshot.rawWorkspace.editStage);
                if (reuseReconstruction && (!m_PersistentPipeline || !pipeline.SeedViewportDependency(
                    *m_PersistentPipeline,snapshot.rawWorkspace.recipe,processingEdge,snapshot.rawWorkspace.viewportDependencyEdge))) {
                    result.error = "No matching completed denoise input for background verification.";
                    return result;
                }
                pipeline.BeginRawViewportTiming(true);
                const auto begin = std::chrono::steady_clock::now();
                pipeline.ExecuteGraph(snapshot.graph);
                pipeline.m_RawViewportGpuTiming.Finish();
                if (shouldAbortStaleWork()) return result;
                GLsync fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                if (!fence) { result.error = "Calibration fence failed."; return result; }
                glFlush();
                GLenum status = GL_TIMEOUT_EXPIRED;
                while (status == GL_TIMEOUT_EXPIRED && !shouldAbortStaleWork())
                    status = glClientWaitSync(fence, 0, 1000000);
                glDeleteSync(fence);
                if (shouldAbortStaleWork()) return result;
                if (status == GL_WAIT_FAILED || pipeline.GetOutputTexture() == 0 ||
                    pipeline.GetLastGraphExecutionStats().allocationFailed) {
                    result.error = "Calibration render failed.";
                    return result;
                }
                const double elapsed = MillisecondsBetween(begin, std::chrono::steady_clock::now());
                const auto stageCosts = pipeline.CollectRawViewportTiming(shouldAbortStaleWork);
                if (firstUse && pass == 0) {
                    result.calibration.startupMs = elapsed;
                    result.calibration.startupStages = stageCosts;
                } else {
                    warmTimes[warmCount] = elapsed;
                    warmStages[warmCount++] = stageCosts;
                    auto median = [warmCount](std::array<double,3> values) {
                        std::sort(values.begin(), values.begin() + warmCount);
                        return values[(warmCount-1)/2];
                    };
                    result.calibration.renderMs = median(warmTimes);
                    for (std::size_t i = 0; i < stageCosts.size(); ++i)
                        result.calibration.stages[i] = median({warmStages[0][i],warmStages[1][i],warmStages[2][i]});
                }
            }
            result.calibration.edge = snapshot.previewMaxDimension > 0 ? snapshot.previewMaxDimension :
                std::max(snapshot.rawWorkspace.fullFrameWidth, snapshot.rawWorkspace.fullFrameHeight);
            result.calibration.native =
                pipeline.GetCanvasWidth() == result.rawWorkspace.expectedNativeOutputWidth &&
                pipeline.GetCanvasHeight() == result.rawWorkspace.expectedNativeOutputHeight;
            result.mainGraphStats = pipeline.GetLastGraphExecutionStats();
            result.success = result.calibration.renderMs > 0.0;
            SetProgress(passes, passes, "RAW viewport timing measured");
            return result;
        }


        if (!snapshot.graph.nodes.empty()) {
            GLint maxTextureSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
            const ViewportTilingSettings tilingSettings =
                RenderTiling::NormalizeSettings(snapshot.viewportTiling, maxTextureSize);
            const RenderGraphRegionPlan regionPlan = RenderTiling::PlanGraphRegions(
                snapshot.graph, snapshot.width, snapshot.height);
            result.mainRegionPlanAvailable = true;
            result.mainRegionPlanTileable = regionPlan.tileable;
            result.mainRegionPlanHaloX = regionPlan.requiredHaloX;
            result.mainRegionPlanHaloY = regionPlan.requiredHaloY;
            result.mainRegionPlanReason = regionPlan.reason;
            const bool rawWorkspaceMainOutput = !snapshot.rawWorkspace.sourceKey.empty();
            const bool canTileGraph =
                !rawWorkspaceMainOutput &&
                RenderTiling::ShouldUseTiling(tilingSettings, snapshot.width, snapshot.height, maxTextureSize) &&
                regionPlan.valid && regionPlan.tileable;
            if (canTileGraph) {
                std::vector<RenderTileRect> tiles =
                    RenderTiling::PlanTiles(
                        snapshot.width,
                        snapshot.height,
                        tilingSettings,
                        regionPlan.requiredHaloX,
                        regionPlan.requiredHaloY);
                if (!tiles.empty()) {
                    reportProgress("Rendering tiled main output...");
                    const auto mainRenderBegin = std::chrono::steady_clock::now();
                    Stack::EditorRenderWorkerTiles::TileGraphBatch
                        tileGraphBatch(
                            snapshot.graph,
                            snapshot.width,
                            snapshot.height);
                    result.outputTiles.fullWidth = snapshot.width;
                    result.outputTiles.fullHeight = snapshot.height;
                    result.outputTiles.tiled = true;
                    result.outputTiles.debugOverlay = tilingSettings.debugOverlay;
                    result.outputTiles.tiles.reserve(tiles.size());
                    auto releasePartialTiles = [&]() {
                        for (SharedTextureTile& partialTile : result.outputTiles.tiles) {
                            if (partialTile.texture != 0) {
                                glDeleteTextures(1, &partialTile.texture);
                                partialTile.texture = 0;
                            }
                        }
                        result.outputTiles.tiles.clear();
                        result.outputTiles.complete = false;
                        result.outputTiles.readyFence = nullptr;
                    };
                    const TileIterationResult tileIteration = RenderTiling::IterateTiles(
                        tiles,
                        shouldAbortStaleWork,
                        [&](const RenderTileRect& tile, std::size_t tileIndex) {
                        reportProgress(
                            "Rendering tile " +
                            std::to_string(tileIndex + 1) +
                            "/" +
                            std::to_string(static_cast<int>(tiles.size())) +
                            "...");
                        SharedPixelBuffer tileSource;
                        if (!snapshot.sourcePixels.empty()) {
                            tileSource =
                                Stack::EditorRenderWorkerTiles::
                                    CropSharedPixelBuffer(
                                snapshot.sourcePixels,
                                snapshot.width,
                                snapshot.height,
                                snapshot.channels,
                                tile.haloX,
                                tile.haloY,
                                tile.haloWidth,
                                tile.haloHeight);
                            if (tileSource.empty()) {
                                result.error = "Failed to crop source tile.";
                                return false;
                            }
                        }
                        loadSourceBuffer(tileSource, tile.haloWidth, tile.haloHeight, snapshot.channels);
                        if (!tileGraphBatch.Prepare(tile)) {
                            result.error =
                                "Failed to prepare tiled graph images.";
                            return false;
                        }
                        pipeline.ExecuteGraph(
                            tileGraphBatch.Graph(),
                            tileGraphBatch.Topology());
                        int tileTextureW = 0;
                        int tileTextureH = 0;
                        const unsigned int tileTexture =
                            pipeline.PublishSharedOutputTexture(tileTextureW, tileTextureH);
                        if (tileTexture == 0 || tileTextureW <= 0 || tileTextureH <= 0) {
                            if (tileTexture != 0) {
                                glDeleteTextures(1, &tileTexture);
                            }
                            const std::string& denoiseError =
                                pipeline.GetLastRawRgbDenoiseError();
                            result.error = denoiseError.empty()
                                ? "Tiled render produced an empty tile."
                                : denoiseError;
                            return false;
                        }
                        SharedTextureTile sharedTile;
                        sharedTile.texture = tileTexture;
                        sharedTile.x = tile.x;
                        sharedTile.y = tile.y;
                        sharedTile.width = tile.width;
                        sharedTile.height = tile.height;
                        sharedTile.haloX = tile.haloX;
                        sharedTile.haloY = tile.haloY;
                        sharedTile.haloWidth = tileTextureW;
                        sharedTile.haloHeight = tileTextureH;
                        result.outputTiles.tiles.push_back(sharedTile);
                        return true;
                    });
                    if (tileIteration.status == TileIterationStatus::Canceled) {
                        reportSupersededWork();
                        result.error = "Render superseded by a newer snapshot.";
                        releasePartialTiles();
                        return result;
                    }
                    result.mainRenderMs = MillisecondsBetween(mainRenderBegin, std::chrono::steady_clock::now());
                    result.mainGraphStats = pipeline.GetLastGraphExecutionStats();
                    result.outputTiles.complete =
                        tileIteration.status == TileIterationStatus::Completed &&
                        result.outputTiles.tiles.size() == tiles.size() &&
                        result.error.empty();
                    result.success = result.outputTiles.complete && !result.outputTiles.tiles.empty();
                    if (result.success) {
                        result.outputTiles.readyFence =
                            CreateSharedTextureFence();
                        result.success =
                            result.outputTiles.readyFence != nullptr;
                        if (!result.success) {
                            result.error =
                                "Could not synchronize tiled output "
                                "between render contexts.";
                        }
                    } else if (result.error.empty()) {
                        result.error = "Tiled render failed.";
                    }
                    if (!result.success) {
                        releasePartialTiles();
                    }
                    finishProgressStep(result.success ? "Tiled main output rendered." : "Tiled render failed.");
                    renderDevelopCandidateRequests();
                    renderPreviewRequests();
                    return result;
                }
            }

            if (shouldAbortStaleWork()) {
                reportSupersededWork();
                result.error = "Render superseded by a newer snapshot.";
                return result;
            }
            reportProgress("Rendering main output...");
            const auto mainRenderBegin = std::chrono::steady_clock::now();
            if (privateOverview) {
                pipeline.PrepareRawCalibrationPass();
                if (!m_PersistentPipeline || !pipeline.SeedViewportDependency(*m_PersistentPipeline,
                    snapshot.rawWorkspace.recipe,processingEdge,snapshot.rawWorkspace.viewportDependencyEdge)) {
                    result.error = "The completed denoise input is no longer available for the overview.";
                    return result;
                }
            }
            pipeline.ExecuteGraph(snapshot.graph);
            pipeline.m_RawViewportGpuTiming.Finish();
            result.mainRenderMs = MillisecondsBetween(mainRenderBegin, std::chrono::steady_clock::now());
            result.mainGraphStats = pipeline.GetLastGraphExecutionStats();
            if (shouldAbortStaleWork()) {
                finishProgressStep("Newer render queued.");
                result.error = "Render superseded by a newer snapshot.";
                return result;
            }
            collectReadyGradingScopePacket();
            collectToneCurveAutoRewriteFeedback();
            if (rawWorkspaceMainOutput) {
                if (!snapshot.rawWorkspace.analysisRequested &&
                    snapshot.rawWorkspace.graphScopeStage != RawDevelopmentGraphScopeStage::None) {
                    result.rawWorkspace.graphScopeReadback = pipeline.GetRawDevelopmentGraphScopeReadback();
                }
                if (snapshot.rawWorkspace.analysisRequested) {
                    CaptureRawWorkspaceViewTransformInputStats(
                        pipeline,
                        snapshot,
                        result);
                    if (!snapshot.rawWorkspace.startPointCandidateRenderRequests.empty()) {
                        CaptureRawWorkspaceAutoBaseRecommendations(pipeline, snapshot, result);
                    }
                    if (shouldAbortStaleWork()) {
                        finishProgressStep("Newer render queued.");
                        result.error = "Render superseded by a newer snapshot.";
                        return result;
                    }
                }
                if (snapshot.rawRenderPurpose ==
                        RawRenderPurpose::ExplicitExport) {
                    result.pixels = pipeline.GetOutputPixelsTiledPbo(
                        result.width,
                        result.height);
                    result.success =
                        Stack::PixelBuffer::HasCompletePixelBuffer(
                            result.pixels.size(),
                            result.width,
                            result.height,
                            4);
                } else if (snapshot.rawRenderPurpose ==
                        RawRenderPurpose::CachePrewarm) {
                    result.rawWorkspace.cachePrewarmCompleted =
                        pipeline.WasRawDevelopmentCachePrewarmCompleted();
                    result.success =
                        result.rawWorkspace.cachePrewarmCompleted;
                    if (!result.success) {
                        result.error =
                            "RAW cache prewarm was canceled or its semantic target was not produced.";
                    }
                } else if (!RawRenderPurposeMayPublishPresentation(
                        snapshot.rawRenderPurpose) && snapshot.rawRenderPurpose != RawRenderPurpose::ViewportOverview) {
                    CaptureRawWorkspaceLocalRangeTargetSample(pipeline, result);
                    CaptureRawWorkspaceLocalRangeOverlay(pipeline, result);
                    // Auxiliary packets are intentionally unable to carry an
                    // adoptable main presentation. Their own overlay texture,
                    // if any, is fenced independently by the capture helper.
                    result.success = true;
                } else {
                    result.rawWorkspace.viewportRegion = pipeline.GetRawViewportRegion();
                    result.outputTexture.texture =
                        pipeline.PublishSharedOutputTexture(
                            result.outputTexture.width,
                            result.outputTexture.height,
                            true, &result.error);
                    result.success = result.outputTexture.texture != 0;
                    if (result.success) {
                        CaptureRawWorkspaceLocalRangeTargetSample(pipeline, result);
                        CaptureRawWorkspaceLocalRangeOverlay(pipeline, result);
                        result.success =
                            FenceSharedTexture(result.outputTexture);
                    }
                }
            } else {
                result.outputTexture.texture = pipeline.PublishSharedOutputTexture(
                    result.outputTexture.width, result.outputTexture.height, false, &result.error);
                result.success =
                    FenceSharedTexture(result.outputTexture);
            }
            if (!result.success && result.error.empty()) {
                result.error = RenderFailureReason(pipeline,
                    snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport
                        ? "Could not read the rendered image for export."
                        : "Could not synchronize the rendered image with the preview.");
            }
            finishProgressStep(result.success ? "Main output rendered." : "Main output failed.");
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }

        std::vector<RenderLayerStep> steps;
        steps.reserve(snapshot.layerSteps.empty() ? snapshot.layers.size() : snapshot.layerSteps.size());

        const auto addLayerStep = [&](const nlohmann::json& layerJson, int maskNodeId) {
            const std::string type = layerJson.value("type", std::string());
            std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
            if (!layer) {
                return;
            }
            layer->InitializeGL();
            layer->Deserialize(layerJson);
            RenderLayerStep step;
            step.layer = std::move(layer);
            step.maskNodeId = maskNodeId;
            steps.push_back(std::move(step));
        };

        if (!snapshot.layerSteps.empty()) {
            for (const nlohmann::json& stepJson : snapshot.layerSteps) {
                if (!stepJson.is_object()) {
                    continue;
                }
                addLayerStep(stepJson.value("layer", nlohmann::json::object()), stepJson.value("maskNodeId", -1));
            }
        } else {
            for (const nlohmann::json& layerJson : snapshot.layers) {
                addLayerStep(layerJson, -1);
            }
        }

        if (shouldAbortStaleWork()) {
            reportSupersededWork();
            result.error = "Render superseded by a newer snapshot.";
            return result;
        }
        reportProgress("Rendering layer stack...");
        pipeline.ExecuteMasked(steps, snapshot.masks);
        if (shouldAbortStaleWork()) {
            finishProgressStep("Newer render queued.");
            result.error = "Render superseded by a newer snapshot.";
            return result;
        }
        collectReadyGradingScopePacket();
        if (!snapshot.rawWorkspace.sourceKey.empty()) {
            if (!snapshot.rawWorkspace.analysisRequested &&
                    snapshot.rawWorkspace.graphScopeStage != RawDevelopmentGraphScopeStage::None) {
                result.rawWorkspace.graphScopeReadback = pipeline.GetRawDevelopmentGraphScopeReadback();
            }
            if (snapshot.rawWorkspace.analysisRequested) {
                CaptureRawWorkspaceViewTransformInputStats(
                    pipeline,
                    snapshot,
                    result);
                if (!snapshot.rawWorkspace.startPointCandidateRenderRequests.empty()) {
                    CaptureRawWorkspaceAutoBaseRecommendations(pipeline, snapshot, result);
                }
                if (shouldAbortStaleWork()) {
                    finishProgressStep("Newer render queued.");
                    result.error = "Render superseded by a newer snapshot.";
                    return result;
                }
            }
            if (snapshot.rawRenderPurpose ==
                    RawRenderPurpose::ExplicitExport) {
                result.pixels = pipeline.GetOutputPixelsTiledPbo(
                    result.width,
                    result.height);
                result.success =
                    Stack::PixelBuffer::HasCompletePixelBuffer(
                        result.pixels.size(),
                        result.width,
                        result.height,
                        4);
            } else if (snapshot.rawRenderPurpose ==
                    RawRenderPurpose::CachePrewarm) {
                result.rawWorkspace.cachePrewarmCompleted =
                    pipeline.WasRawDevelopmentCachePrewarmCompleted();
                result.success =
                    result.rawWorkspace.cachePrewarmCompleted;
                if (!result.success) {
                    result.error =
                        "RAW cache prewarm was canceled or its semantic target was not produced.";
                }
            } else if (!RawRenderPurposeMayPublishPresentation(
                    snapshot.rawRenderPurpose)) {
                CaptureRawWorkspaceLocalRangeTargetSample(pipeline, result);
                CaptureRawWorkspaceLocalRangeOverlay(pipeline, result);
                result.success = true;
            } else {
                result.outputTexture.texture =
                    pipeline.PublishSharedOutputTexture(
                        result.outputTexture.width,
                        result.outputTexture.height,
                        true, &result.error);
                result.success = result.outputTexture.texture != 0;
                if (result.success) {
                    CaptureRawWorkspaceLocalRangeTargetSample(pipeline, result);
                    CaptureRawWorkspaceLocalRangeOverlay(pipeline, result);
                    result.success =
                        FenceSharedTexture(result.outputTexture);
                }
            }
        } else {
            result.outputTexture.texture = pipeline.PublishSharedOutputTexture(
                result.outputTexture.width, result.outputTexture.height, false, &result.error);
            result.success =
                FenceSharedTexture(result.outputTexture);
        }
        if (!result.success && result.error.empty()) {
            result.error = RenderFailureReason(pipeline,
                snapshot.rawRenderPurpose == RawRenderPurpose::ExplicitExport
                    ? "Could not read the rendered image for export."
                    : "Could not synchronize the rendered image with the preview.");
        }
        finishProgressStep(result.success ? "Layer stack rendered." : "Layer stack failed.");
        renderDevelopCandidateRequests();
        renderPreviewRequests();
    } catch (const std::bad_alloc&) {
        result.success = false;
        try {
            result.error =
                "Render worker exhausted host memory.";
        } catch (...) {
            result.error.clear();
        }
        std::cerr
            << "[EditorRenderWorker] RenderSnapshot exhausted host memory"
            << " generation=" << snapshot.generation
            << "\n";
    } catch (const std::exception& e) {
        result.success = false;
        try {
            result.error = e.what();
        } catch (...) {
            result.error.clear();
        }
        std::cerr
            << "[EditorRenderWorker] RenderSnapshot failed"
            << " generation=" << snapshot.generation
            << " rawSourceKey=" << snapshot.rawWorkspace.sourceKey
            << " previewMax=" << snapshot.previewMaxDimension
            << " outputConnected=" << (snapshot.outputConnected ? 1 : 0)
            << " graphNodes=" << snapshot.graph.nodes.size()
            << " previews=" << snapshot.previews.size()
            << " compositeOutputs=" << snapshot.compositeOutputs.size()
            << " error=" << result.error
            << "\n";
    } catch (...) {
        result.success = false;
        try {
            result.error = "Unknown render worker failure.";
        } catch (...) {
            result.error.clear();
        }
        std::cerr
            << "[EditorRenderWorker] RenderSnapshot failed"
            << " generation=" << snapshot.generation
            << " rawSourceKey=" << snapshot.rawWorkspace.sourceKey
            << " previewMax=" << snapshot.previewMaxDimension
            << " outputConnected=" << (snapshot.outputConnected ? 1 : 0)
            << " graphNodes=" << snapshot.graph.nodes.size()
            << " previews=" << snapshot.previews.size()
            << " compositeOutputs=" << snapshot.compositeOutputs.size()
            << " error=unknown exception\n";
    }

    return result;
}
