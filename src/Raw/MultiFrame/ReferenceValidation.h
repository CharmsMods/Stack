#pragma once

#include "Raw/MultiFrame/Contracts.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

inline constexpr std::uint32_t kReferenceDemosaicVersion = 1;
inline constexpr const char* kReferenceDemosaicId =
    "stack-multiframe-fixed-linear-demosaic-v1";
inline constexpr std::uint32_t kReferenceMetricVersion = 1;
inline constexpr const char* kReferenceMetricId =
    "stack-multiframe-stage-metrics-v1";

enum class ReferenceRgbChannel : std::uint8_t {
    Red = 0,
    Green = 1,
    Blue = 2
};

struct ReferenceRgbImage {
    std::uint32_t demosaicVersion = kReferenceDemosaicVersion;
    std::string demosaicId = kReferenceDemosaicId;
    PixelExtent extent;
    std::vector<float> linearRgb;
    std::vector<std::uint8_t> validChannelMask;
    std::vector<std::array<std::uint16_t, 3>> supportCounts;
};

bool FixedReferenceDemosaic(
    const std::vector<float>& packedMosaic,
    const std::vector<SampleEvidence>& evidence,
    const CfaDomainIdentity& domain,
    ReferenceRgbImage& result,
    std::string* error = nullptr);

struct ScalarErrorMetrics {
    std::uint64_t sampleCount = 0;
    double meanAbsoluteError = 0.0;
    double rootMeanSquareError = 0.0;
    double maximumAbsoluteError = 0.0;
};

struct DarkArtifactMetrics {
    std::uint64_t connectedRegionCount = 0;
    std::uint64_t affectedPixelCount = 0;
    double minimumCandidateToReferenceRatio = 1.0;
    std::array<std::uint64_t, 4> affectedPhaseCounts {};
};

struct StageComparisonMetrics {
    std::uint32_t metricVersion = kReferenceMetricVersion;
    std::string metricId = kReferenceMetricId;
    std::array<ScalarErrorMetrics, 4> mosaicByPhase;
    ScalarErrorMetrics mosaicAll;
    std::array<ScalarErrorMetrics, 3> referenceRgbByChannel;
    std::uint64_t categoricalEvidenceMismatchCount = 0;
    std::uint64_t falseEqualitySupportCount = 0;
    std::uint64_t invalidOrClippedZeroCount = 0;
    std::uint64_t unresolvedCandidateCount = 0;
    DarkArtifactMetrics darkArtifacts;
};

struct StageComparisonRequest {
    CfaDomainIdentity domain;
    std::vector<float> latentMosaic;
    std::vector<SampleEvidence> expectedEvidence;
    std::vector<float> candidateMosaic;
    std::vector<SampleEvidence> candidateEvidence;
    double zeroTolerance = 1.0e-12;
    double darkArtifactRatio = 0.25;
    double minimumReferenceIntensity = 1.0e-6;
    std::uint32_t clippedProximityRadius = 2;
};

bool CompareReferenceStages(
    const StageComparisonRequest& request,
    StageComparisonMetrics& result,
    std::string* error = nullptr);

} // namespace Raw::MultiFrame
