#pragma once

#include "Raw/MultiFrameDenoise/Preparation.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kNoiseModelContractVersion = 1;
inline constexpr std::uint32_t kNoiseCalibrationSchemaVersion = 1;
inline constexpr const char* kNoiseModelContractId =
    "ra-cfa-poisson-gaussian-noise-v1";

enum class NoiseModelQuality : std::uint8_t {
    TrustedMetadata = 0,
    CalibratedCamera = 1,
    EstimatedBurst = 2,
    GenericLowConfidence = 3,
    Unavailable = 4
};

const char* NoiseModelQualityName(NoiseModelQuality quality);

struct SiteNoiseProfile {
    double shotScale = 0.0;
    double offsetVariance = 0.0;
    double quantizationVariance = 0.0;
    double residualModelTau0 = 0.0;
    double residualModelTau1 = 0.0;
    bool quantizationIncluded = false;
};

bool ValidateSiteNoiseProfile(
    const SiteNoiseProfile& profile,
    std::string* error = nullptr);

struct NoiseSiteDiagnostics {
    CfaSite site = CfaSite::Red;
    std::uint64_t candidateBlockCount = 0;
    std::uint64_t acceptedBlockCount = 0;
    std::uint32_t populatedSignalBins = 0;
    double signalMinimum = 0.0;
    double signalMaximum = 0.0;
    double signalCoverage = 0.0;
    double fitRootMeanSquareError = 0.0;
    bool duplicatedFromSharedGreen = false;
    bool quantizationAdded = false;
    bool valid = false;
    std::string reason;
};

struct NoiseModelDiagnostics {
    NoiseModelQuality quality = NoiseModelQuality::Unavailable;
    bool resolved = false;
    bool referenceOnly = true;
    bool greenSitesIndependent = false;
    bool correlatedNoiseUnmodeled = true;
    std::string source;
    std::string sourceRecordId;
    std::vector<std::string> warnings;
    std::array<NoiseSiteDiagnostics, 4> sites;
};

struct NoiseModel {
    std::uint32_t contractVersion = kNoiseModelContractVersion;
    std::string contractId = kNoiseModelContractId;
    std::string preparedFrameCacheKey;
    std::string identitySha256;
    NoiseModelQuality quality = NoiseModelQuality::Unavailable;
    std::array<SiteNoiseProfile, 4> sites;
    NoiseModelDiagnostics diagnostics;
};

bool ValidateNoiseModel(
    const NoiseModel& model,
    std::string* error = nullptr);

double NoiseModelConfidence(
    NoiseModelQuality quality,
    const Parameters& parameters);

double NonnegativeShotPilot(double referenceComparisonSample);
double QuantizationVariance(double usableCodeSpanDn);

struct GainPropagatedVariance {
    double shotCoefficient = 0.0;
    double offsetVariance = 0.0;
    double variance = 0.0;
    double darkVariance = 0.0;
    double effectiveDnStep = 0.0;
};

bool PropagateKnownGainVariance(
    const SiteNoiseProfile& profile,
    double comparisonGain,
    double commonComparisonDomainPilot,
    double usableCodeSpanDn,
    GainPropagatedVariance& result,
    std::string* error = nullptr);

struct InterpolationVarianceTerm {
    double coefficient = 0.0;
    double sampleVariance = 0.0;
    double comparisonGain = 1.0;
    double usableCodeSpanDn = 1.0;
};

bool PropagateInterpolationVariance(
    const std::vector<InterpolationVarianceTerm>& terms,
    double& variance,
    double& effectiveDnStepSquared,
    std::string* error = nullptr);

struct RawSignalGradient {
    double dxPerRawPixel = 0.0;
    double dyPerRawPixel = 0.0;
};

struct SymmetricRawCovariance {
    double xxRawPixelsSquared = 0.0;
    double xyRawPixelsSquared = 0.0;
    double yyRawPixelsSquared = 0.0;
};

bool RegistrationUncertaintyVariance(
    RawSignalGradient gradient,
    SymmetricRawCovariance covariance,
    double& variance,
    std::string* error = nullptr);

bool ExposureScaleUncertaintyVariance(
    double interpolatedComparisonSample,
    double exposureScale,
    double exposureScaleVariance,
    double& variance,
    std::string* error = nullptr);

double ResidualModelVariance(
    const SiteNoiseProfile& profile,
    double commonComparisonDomainPilot);

struct EffectiveVariance {
    double gateVariance = 0.0;
    double fusionVariance = 0.0;
    double darkVariance = 0.0;
    double divisionVarianceFloor = 0.0;
};

bool ComposeEffectiveVariance(
    double interpolatedSampleVariance,
    double registrationVariance,
    double residualModelVariance,
    double exposureScaleVariance,
    double darkVariance,
    double effectiveDnStepSquared,
    double numericalVarianceFloor,
    EffectiveVariance& result,
    std::string* error = nullptr);

struct NoiseCalibrationObservation {
    double meanSignal = 0.0;
    double variance = 0.0;
    double weight = 1.0;
};

struct NoiseFitDiagnostics {
    std::uint64_t observationCount = 0;
    double signalMinimum = 0.0;
    double signalMaximum = 0.0;
    double rootMeanSquareError = 0.0;
    bool constrainedShotScale = false;
    bool constrainedOffsetVariance = false;
};

bool FitPoissonGaussianProfile(
    const std::vector<NoiseCalibrationObservation>& observations,
    double minimumOffsetVariance,
    SiteNoiseProfile& profile,
    NoiseFitDiagnostics& diagnostics,
    std::string* error = nullptr);

struct NoiseCalibrationKey {
    std::string cameraMake;
    std::string cameraModel;
    std::string uniqueCameraModel;
    std::string cameraMode;
    int bitDepth = 0;
    CfaPattern cfaPattern = CfaPattern::Unknown;
    double isoMinimum = 0.0;
    double isoMaximum = 0.0;
    bool hasTemperatureRange = false;
    double temperatureMinimumC = 0.0;
    double temperatureMaximumC = 0.0;
};

struct NoiseCalibrationRecord {
    std::string recordId;
    NoiseCalibrationKey key;
    std::array<SiteNoiseProfile, 4> sites;
};

struct NoiseCalibrationLibrary {
    std::uint32_t schemaVersion = kNoiseCalibrationSchemaVersion;
    std::vector<NoiseCalibrationRecord> records;
};

bool LoadNoiseCalibrationLibrary(
    const std::filesystem::path& path,
    NoiseCalibrationLibrary& library,
    std::string* error = nullptr);

struct BurstNoiseEstimationOptions {
    std::uint32_t blockRawPixels = 32;
    std::uint32_t signalBinCount = 8;
    std::uint32_t minimumBlocksPerSite = 24;
    std::uint32_t minimumBlocksPerBin = 2;
    std::uint32_t minimumPopulatedBins = 3;
    double lowerEnvelopeQuantile = 0.25;
    double minimumSignal = 0.0;
    double maximumSignal = 0.90;
    double minimumSignalCoverage = 0.10;
    double maximumGreenRelativeDisagreement = 0.50;
    double gradientAbsoluteLimit = 0.02;
    double gradientNoiseSigmaMultiplier = 4.0;
};

struct NoiseResolutionOptions {
    bool metadataProfileIncludesQuantization = true;
    std::filesystem::path calibrationLibraryPath;
    std::string cameraMode;
    bool hasSensorTemperature = false;
    double sensorTemperatureC = 0.0;
    bool enableBurstEstimate = true;
    BurstNoiseEstimationOptions burstEstimate;
    bool enableGenericLowConfidence = false;
    std::array<SiteNoiseProfile, 4> genericLowConfidenceSites;
};

struct NoiseResolutionResult {
    bool resolved = false;
    NoiseModel model;
    std::string message;
};

NoiseResolutionResult ResolveNoiseModel(
    const RawMetadata& metadata,
    const PreparedRawFrame& preparedFrame,
    NormalizedTileCache* preparedTileCache,
    const NoiseResolutionOptions& options = {});

bool EstimateNoiseModelFromPreparedFrame(
    const PreparedRawFrame& preparedFrame,
    NormalizedTileCache& preparedTileCache,
    const BurstNoiseEstimationOptions& options,
    NoiseModel& model,
    std::string* error = nullptr);

void ApplyNoiseModelToPreparationOptions(
    const NoiseModel& model,
    PreparationOptions& options);

nlohmann::json SerializeNoiseModelDiagnostics(const NoiseModel& model);

} // namespace Raw::Mfd
