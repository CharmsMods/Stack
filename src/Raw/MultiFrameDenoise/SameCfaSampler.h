#pragma once

#include "Raw/MultiFrameDenoise/NoiseModel.h"

#include <array>
#include <cstdint>
#include <string>

namespace Raw::Mfd {

inline constexpr std::uint32_t kSameCfaSamplerContractVersion = 1;
inline constexpr const char* kSameCfaSamplerContractId =
    "ra-cfa-keys-bicubic-sampler-v1";
inline constexpr double kKeysBicubicParameter = -0.5;
inline constexpr std::size_t kSameCfaTapCount = 16;

enum class SameCfaSampleFailure : std::uint8_t {
    None = 0,
    InvalidRequest,
    FootprintOutsideActiveArea,
    TileUnavailable,
    MalformedTile,
    NonFiniteTap,
    NonPositiveGain,
    ExplicitDecoderClip,
    SaturatedTap,
    DefectiveTap,
    DecoderRepairedTap,
    InvalidNoiseModel,
    InvalidVariance
};

const char* SameCfaSampleFailureName(SameCfaSampleFailure failure);

double KeysBicubicKernel(
    double distance,
    double parameter = kKeysBicubicParameter);

double KeysBicubicKernelDerivative(
    double distance,
    double parameter = kKeysBicubicParameter);

struct KeysBicubicFootprint {
    std::uint32_t contractVersion = kSameCfaSamplerContractVersion;
    std::string contractId = kSameCfaSamplerContractId;
    CfaPlaneCoordinate coordinate;
    std::array<CfaPlanePixel, kSameCfaTapCount> taps;
    std::array<double, kSameCfaTapCount> coefficients {};
    std::array<double, kSameCfaTapCount> derivativeXPlane {};
    std::array<double, kSameCfaTapCount> derivativeYPlane {};
    double coefficientSum = 0.0;
    double derivativeXSum = 0.0;
    double derivativeYSum = 0.0;
};

bool BuildKeysBicubicFootprint(
    CfaPlaneCoordinate coordinate,
    KeysBicubicFootprint& footprint,
    std::string* error = nullptr);

bool ValidateKeysBicubicFootprint(
    const KeysBicubicFootprint& footprint,
    PixelExtent planeExtent,
    std::string* error = nullptr);

struct SameCfaTapInput {
    double normalizedSample = 0.0;
    double comparisonGain = 1.0;
    std::uint8_t sampleFlags = 0u;
};

struct SameCfaScalarParameters {
    double exposureScale = 1.0;
    double exposureScaleVariance = 0.0;
    double referenceComparisonPilot = 0.0;
    SymmetricRawCovariance warpCovariance;
    double numericalVarianceFloor = 1.0e-12;
};

struct PlaneSignalGradient {
    double dxPerPlanePixel = 0.0;
    double dyPerPlanePixel = 0.0;
};

struct SameCfaSampleResult {
    bool valid = false;
    SameCfaSampleFailure failure = SameCfaSampleFailure::None;
    std::string message;
    CfaSite site = CfaSite::Red;
    RawCoordinate sourceRaw;
    CfaPlaneCoordinate sourcePlane;
    double value = 0.0;
    double interpolationVariance = 0.0;
    double registrationVariance = 0.0;
    double exposureScaleVariance = 0.0;
    double residualModelVariance = 0.0;
    double gateVariance = 0.0;
    double fusionVariance = 0.0;
    double darkVariance = 0.0;
    double effectiveDnStep = 0.0;
    double divisionVarianceFloor = 0.0;
    PlaneSignalGradient gradientPlane;
    RawSignalGradient gradientRaw;
    double coefficientSum = 0.0;
};

bool EvaluateSameCfaFootprintScalar(
    const KeysBicubicFootprint& footprint,
    const std::array<SameCfaTapInput, kSameCfaTapCount>& taps,
    const SiteNoiseProfile& noiseProfile,
    double usableCodeSpanDn,
    const SameCfaScalarParameters& parameters,
    SameCfaSampleResult& result,
    std::string* error = nullptr);

bool SamplePreparedFrameSameCfaScalar(
    const PreparedRawFrame& frame,
    NormalizedTileCache& tileCache,
    const NoiseModel& noiseModel,
    RawCoordinate sourceRaw,
    CfaSite site,
    const SameCfaScalarParameters& parameters,
    SameCfaSampleResult& result,
    std::string* error = nullptr);

} // namespace Raw::Mfd
