#include "Raw/RawImageAnalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Stack::RawAnalysis {
namespace {

constexpr float kLumaEpsilon = 1.0e-8f;

float SafeLuma(float luma) {
    if (!std::isfinite(luma)) {
        return 0.0f;
    }
    return std::max(0.0f, luma);
}

float Percentile(const std::vector<float>& values, float percentile) {
    if (values.empty()) {
        return 0.0f;
    }
    const float clamped = std::clamp(percentile, 0.0f, 1.0f);
    const std::size_t index = static_cast<std::size_t>(
        std::round(clamped * static_cast<float>(values.size() - 1)));
    return values[index];
}

} // namespace

const char* AnalysisStageStatusLabel(AnalysisStageStatus status) {
    switch (status) {
        case AnalysisStageStatus::Complete: return "complete";
        case AnalysisStageStatus::Fallback: return "fallback";
        case AnalysisStageStatus::Unavailable:
        default:
            return "unavailable";
    }
}

float SafeLog2Luma(float luma) {
    return std::log2(std::max(kLumaEpsilon, SafeLuma(luma)));
}

RawMetadataSummary BuildRawMetadataSummary(const Raw::RawMetadata& metadata) {
    RawMetadataSummary summary;

    const auto finitePositive = [](float value) {
        return std::isfinite(value) && value > 0.0f;
    };
    const float cameraG = finitePositive(metadata.cameraWhiteBalance[1])
        ? metadata.cameraWhiteBalance[1]
        : 1.0f;
    const bool hasPositiveCameraWb =
        finitePositive(metadata.cameraWhiteBalance[0]) &&
        finitePositive(metadata.cameraWhiteBalance[1]) &&
        finitePositive(metadata.cameraWhiteBalance[2]);
    const bool cameraWbDiffersFromDefault =
        std::abs(metadata.cameraWhiteBalance[0] - 1.0f) > 0.001f ||
        std::abs(metadata.cameraWhiteBalance[1] - 1.0f) > 0.001f ||
        std::abs(metadata.cameraWhiteBalance[2] - 1.0f) > 0.001f;
    summary.hasCameraWhiteBalance =
        metadata.hasDngAsShotNeutral ||
        !metadata.whiteBalanceSource.empty() ||
        (hasPositiveCameraWb && cameraWbDiffersFromDefault);
    summary.cameraWbR = finitePositive(metadata.cameraWhiteBalance[0])
        ? metadata.cameraWhiteBalance[0] / cameraG
        : 1.0f;
    summary.cameraWbG = 1.0f;
    summary.cameraWbB = finitePositive(metadata.cameraWhiteBalance[2])
        ? metadata.cameraWhiteBalance[2] / cameraG
        : 1.0f;

    summary.hasBaselineExposure = metadata.hasDngBaselineExposure;
    summary.baselineExposureEv = metadata.dngBaselineExposure;
    summary.iso = metadata.isoSpeed;
    summary.shutterSeconds = metadata.exposureTimeSeconds;
    summary.aperture = metadata.apertureFNumber;
    summary.hasActiveArea =
        metadata.visibleWidth > 0 &&
        metadata.visibleHeight > 0 &&
        metadata.rawWidth > 0 &&
        metadata.rawHeight > 0;
    summary.hasLinearResponseLimit =
        std::isfinite(metadata.defaultWhiteClipPercent) &&
        metadata.defaultWhiteClipPercent > 0.0f;
    if (summary.hasLinearResponseLimit) {
        summary.linearResponseLimit =
            std::clamp(1.0f - metadata.defaultWhiteClipPercent * 0.01f, 0.0f, 1.0f);
    }
    return summary;
}

PercentileStats BuildPercentileStatsFromLumas(
    std::vector<float> lumas,
    float validPixelPercent,
    AnalysisStageStatus status,
    std::string statusMessage) {
    PercentileStats stats;
    stats.status = status;
    stats.statusMessage = std::move(statusMessage);
    stats.validPixelPercent = std::max(0.0f, validPixelPercent);
    if (lumas.empty()) {
        return stats;
    }

    float logSum = 0.0f;
    for (float& luma : lumas) {
        luma = SafeLuma(luma);
        logSum += std::log(kLumaEpsilon + luma);
    }
    std::sort(lumas.begin(), lumas.end());

    stats.valid = true;
    stats.p001Luma = Percentile(lumas, 0.001f);
    stats.p01Luma = Percentile(lumas, 0.01f);
    stats.p05Luma = Percentile(lumas, 0.05f);
    stats.p50Luma = Percentile(lumas, 0.50f);
    stats.p95Luma = Percentile(lumas, 0.95f);
    stats.p99Luma = Percentile(lumas, 0.99f);
    stats.p999Luma = Percentile(lumas, 0.999f);

    stats.p001Ev = SafeLog2Luma(stats.p001Luma);
    stats.p01Ev = SafeLog2Luma(stats.p01Luma);
    stats.p05Ev = SafeLog2Luma(stats.p05Luma);
    stats.p50Ev = SafeLog2Luma(stats.p50Luma);
    stats.p95Ev = SafeLog2Luma(stats.p95Luma);
    stats.p99Ev = SafeLog2Luma(stats.p99Luma);
    stats.p999Ev = SafeLog2Luma(stats.p999Luma);
    stats.logAverageLuma = std::exp(logSum / static_cast<float>(lumas.size()));
    stats.dynamicRangeEv = stats.p99Ev - stats.p01Ev;
    return stats;
}

RawImageAnalysis BuildCurrentFrameAnalysisFromCurrentFrameStats(
    const CurrentFrameInputStats& textureStats,
    const std::string& sourceKey,
    std::uint64_t sourceHash,
    std::uint64_t recipeStageHash) {
    RawImageAnalysis analysis;
    analysis.sourceKey = sourceKey;
    analysis.sourceHash = sourceHash;
    analysis.recipeStageHash = recipeStageHash;
    analysis.technicalStats.status = AnalysisStageStatus::Unavailable;
    analysis.technicalStats.statusMessage =
        "Technical analysis stage unavailable; current-frame diagnostics are safe for View Transform fitting only.";
    analysis.highlight.sensorStatus = AnalysisStageStatus::Unavailable;
    analysis.highlight.displayStatus = textureStats.valid
        ? AnalysisStageStatus::Complete
        : AnalysisStageStatus::Unavailable;
    analysis.highlight.blocksPositiveRawExposure = true;

    if (!textureStats.valid) {
        analysis.statusMessage = "Current-frame analysis unavailable.";
        analysis.highlight.statusMessage =
            "RAW sensor clipping unavailable; display clipping unavailable.";
        return analysis;
    }

    std::vector<float> lumas;
    lumas.reserve(7);
    lumas.push_back(textureStats.p001Luma);
    lumas.push_back(textureStats.p01Luma);
    lumas.push_back(textureStats.p05Luma);
    lumas.push_back(textureStats.p50Luma);
    lumas.push_back(textureStats.p95Luma);
    lumas.push_back(textureStats.p99Luma);
    lumas.push_back(textureStats.p999Luma);
    analysis.currentFrameStats = BuildPercentileStatsFromLumas(
        std::move(lumas),
        textureStats.validPixelPercent,
        AnalysisStageStatus::Complete,
        "Current-frame stats sample the scene-linear image immediately before View Transform.");

    analysis.currentFrameStats.p001Luma = textureStats.p001Luma;
    analysis.currentFrameStats.p01Luma = textureStats.p01Luma;
    analysis.currentFrameStats.p05Luma = textureStats.p05Luma;
    analysis.currentFrameStats.p50Luma = textureStats.p50Luma;
    analysis.currentFrameStats.p95Luma = textureStats.p95Luma;
    analysis.currentFrameStats.p99Luma = textureStats.p99Luma;
    analysis.currentFrameStats.p999Luma = textureStats.p999Luma;
    analysis.currentFrameStats.p001Ev = SafeLog2Luma(textureStats.p001Luma);
    analysis.currentFrameStats.p01Ev = SafeLog2Luma(textureStats.p01Luma);
    analysis.currentFrameStats.p05Ev = SafeLog2Luma(textureStats.p05Luma);
    analysis.currentFrameStats.p50Ev = SafeLog2Luma(textureStats.p50Luma);
    analysis.currentFrameStats.p95Ev = SafeLog2Luma(textureStats.p95Luma);
    analysis.currentFrameStats.p99Ev = SafeLog2Luma(textureStats.p99Luma);
    analysis.currentFrameStats.p999Ev = SafeLog2Luma(textureStats.p999Luma);
    analysis.currentFrameStats.logAverageLuma = textureStats.logAverageLuma;
    analysis.currentFrameStats.dynamicRangeEv = textureStats.dynamicRangeEv;

    analysis.highlight.valid = true;
    analysis.highlight.displayClipPercent = textureStats.displayClipPercent;
    analysis.highlight.hdrPixelPercent = textureStats.hdrPixelPercent;
    analysis.highlight.statusMessage =
        "RAW sensor clipping unavailable; display clipping sampled from current render.";

    analysis.valid = true;
    analysis.statusMessage =
        "Current-frame analysis ready. Technical RAW clipping/exposure analysis is unavailable in this pass.";
    return analysis;
}

RawImageAnalysis BuildUnavailableAnalysis(
    const std::string& sourceKey,
    std::string statusMessage) {
    RawImageAnalysis analysis;
    analysis.sourceKey = sourceKey;
    analysis.statusMessage = std::move(statusMessage);
    analysis.technicalStats.status = AnalysisStageStatus::Unavailable;
    analysis.currentFrameStats.status = AnalysisStageStatus::Unavailable;
    analysis.highlight.sensorStatus = AnalysisStageStatus::Unavailable;
    analysis.highlight.displayStatus = AnalysisStageStatus::Unavailable;
    analysis.highlight.blocksPositiveRawExposure = true;
    return analysis;
}

} // namespace Stack::RawAnalysis
