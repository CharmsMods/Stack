#pragma once

#include "Raw/MultiFrame/HuberEstimator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace Raw::MultiFrame {

inline constexpr std::uint32_t kPreparedTileEstimatorVersion = 1;
inline constexpr const char* kPreparedTileEstimatorId =
    "stack-multiframe-prepared-tile-estimator-v1";

enum class PreparedTileEstimatorProfile : std::uint8_t {
    FixedGaussian = 0,
    SharedPilotHuber
};

struct PreparedTileEstimatorConfiguration {
    std::uint32_t contractVersion = kPreparedTileEstimatorVersion;
    std::string contractId = kPreparedTileEstimatorId;
    PreparedTileEstimatorProfile profile =
        PreparedTileEstimatorProfile::FixedGaussian;
    FixedGaussianWeightSemantics baseWeightSemantics =
        FixedGaussianWeightSemantics::BinaryInclusion;
    std::string modelIdentity;
    std::string geometryIdentity;
    std::string pilotIdentity;
    double numericalVarianceFloor = 1.0e-12;
    double huberThreshold = 1.345;
    std::uint32_t maximumHuberIterations = 12;
    double absoluteHuberTolerance = 1.0e-12;
    double relativeHuberTolerance = 1.0e-10;
};

struct PreparedTileEstimatorSource {
    const PreparedMeasurementSource* prepared = nullptr;
    const NoiseModelReference* noise = nullptr;
    double exposureScale = 1.0;
    std::array<double, 4> blackOffsetByPhase {};
    double reliability = 1.0;
    double varianceInflation = 1.0;
    std::uint64_t sourceOrdinal = 0;
};

struct PreparedTileEstimatorRequest {
    PreparedTileEstimatorConfiguration configuration;
    std::vector<PreparedTileEstimatorSource> sources;
    std::size_t outputReferenceSourceIndex = 0;
    std::uint32_t tileX = 0;
    std::uint32_t tileY = 0;
    // Pilot and output share Virtual Bayer units. The executor converts the
    // pilot to the reference comparison domain before evaluating variance.
    std::vector<double> sharedPilotVirtualBayer;
};

struct PreparedTileEstimatorResult {
    std::uint32_t contractVersion = kPreparedTileEstimatorVersion;
    std::string contractId = kPreparedTileEstimatorId;
    PreparedTileEstimatorProfile profile =
        PreparedTileEstimatorProfile::FixedGaussian;
    std::uint64_t originX = 0;
    std::uint64_t originY = 0;
    PixelExtent extent;
    std::vector<double> virtualBayer;
    std::vector<double> variance;
    std::vector<double> modelQuadraticVariance;
    std::vector<double> policyConditionalSamplingVariance;
    std::vector<std::uint8_t> policyConditionalSamplingVarianceAvailable;
    std::vector<double> robustLinearizedVariance;
    std::vector<std::uint8_t> robustLinearizedVarianceAvailable;
    std::vector<double> finiteSampleCorrectedRobustVariance;
    std::vector<std::uint8_t> finiteSampleCorrectedRobustVarianceAvailable;
    std::vector<double> effectiveSupport;
    std::vector<FixedGaussianEstimateState> state;
    std::vector<std::uint64_t> ownerSourceOrdinal;
    std::vector<double> ownerWeightFraction;
    std::vector<double> minimumRobustFactor;
    std::uint64_t estimatedPixelCount = 0;
    std::uint64_t boundedOnlyPixelCount = 0;
    std::uint64_t unresolvedPixelCount = 0;
    std::uint64_t estimateOutsideBoundsPixelCount = 0;
    std::uint64_t robustAttenuatedSampleCount = 0;
    bool allHuberPixelsConverged = true;
};

bool EstimatePreparedMeasurementTile(
    const PreparedTileEstimatorRequest& request,
    PreparedTileEstimatorResult& result,
    std::string* error = nullptr);

} // namespace Raw::MultiFrame
