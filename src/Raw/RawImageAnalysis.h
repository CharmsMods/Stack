#pragma once

#include "Raw/RawImageData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Stack::RawAnalysis {

enum class AnalysisStageStatus {
    Unavailable,
    Complete,
    Fallback
};

struct PercentileStats {
    bool valid = false;
    AnalysisStageStatus status = AnalysisStageStatus::Unavailable;

    float p001Ev = 0.0f;
    float p01Ev = 0.0f;
    float p05Ev = 0.0f;
    float p50Ev = 0.0f;
    float p95Ev = 0.0f;
    float p99Ev = 0.0f;
    float p999Ev = 0.0f;

    float p001Luma = 0.0f;
    float p01Luma = 0.0f;
    float p05Luma = 0.0f;
    float p50Luma = 0.0f;
    float p95Luma = 0.0f;
    float p99Luma = 0.0f;
    float p999Luma = 0.0f;

    float logAverageLuma = 0.0f;
    float dynamicRangeEv = 0.0f;
    float validPixelPercent = 0.0f;
    std::string statusMessage;
};

struct HighlightRiskReport {
    bool valid = false;
    AnalysisStageStatus sensorStatus = AnalysisStageStatus::Unavailable;
    AnalysisStageStatus displayStatus = AnalysisStageStatus::Unavailable;

    float anyChannelClipPercent = 0.0f;
    float allChannelClipPercent = 0.0f;
    float redClipPercent = 0.0f;
    float greenClipPercent = 0.0f;
    float blueClipPercent = 0.0f;

    float anyChannelNearNonlinearPercent = 0.0f;
    float displayClipPercent = 0.0f;
    float hdrPixelPercent = 0.0f;

    bool severeSensorClip = false;
    bool partialClipColorRisk = false;
    bool blocksPositiveRawExposure = true;
    std::string statusMessage;
};

struct RawMetadataSummary {
    bool hasCameraWhiteBalance = false;
    bool hasBaselineExposure = false;
    bool hasBaselineNoise = false;
    bool hasBaselineSharpness = false;
    bool hasActiveArea = false;
    bool hasMaskedAreas = false;
    bool hasLinearResponseLimit = false;

    float cameraWbR = 1.0f;
    float cameraWbG = 1.0f;
    float cameraWbB = 1.0f;
    float baselineExposureEv = 0.0f;
    float baselineNoise = 1.0f;
    float baselineSharpness = 1.0f;
    float iso = 0.0f;
    float shutterSeconds = 0.0f;
    float aperture = 0.0f;
    float linearResponseLimit = 1.0f;
};

struct CurrentFrameInputStats {
    bool valid = false;
    float p001Luma = 0.0f;
    float p01Luma = 0.0f;
    float p05Luma = 0.0f;
    float p50Luma = 0.0f;
    float p95Luma = 0.0f;
    float p99Luma = 0.0f;
    float p999Luma = 0.0f;
    float logAverageLuma = 0.0f;
    float dynamicRangeEv = 0.0f;
    float validPixelPercent = 0.0f;
    float hdrPixelPercent = 0.0f;
    float displayClipPercent = 0.0f;
};

struct RawImageAnalysis {
    bool valid = false;
    std::uint64_t sourceHash = 0;
    std::uint64_t recipeStageHash = 0;

    RawMetadataSummary metadata;
    PercentileStats technicalStats;
    PercentileStats currentFrameStats;
    HighlightRiskReport highlight;

    float invalidBorderPercent = 0.0f;
    float effectiveNoiseScore = 0.0f;

    std::string sourceKey;
    std::string statusMessage;
};

const char* AnalysisStageStatusLabel(AnalysisStageStatus status);
float SafeLog2Luma(float luma);
RawMetadataSummary BuildRawMetadataSummary(const Raw::RawMetadata& metadata);
PercentileStats BuildPercentileStatsFromLumas(
    std::vector<float> lumas,
    float validPixelPercent,
    AnalysisStageStatus status,
    std::string statusMessage = {});
RawImageAnalysis BuildCurrentFrameAnalysisFromCurrentFrameStats(
    const CurrentFrameInputStats& stats,
    const std::string& sourceKey,
    std::uint64_t sourceHash = 0,
    std::uint64_t recipeStageHash = 0);
RawImageAnalysis BuildUnavailableAnalysis(
    const std::string& sourceKey,
    std::string statusMessage);

} // namespace Stack::RawAnalysis
