#pragma once

#include "Raw/RawImageData.h"
#include "ThirdParty/json.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kLegacyParameterSchemaVersion = 1;
inline constexpr std::uint32_t kPhase5ParameterSchemaVersion = 2;
inline constexpr std::uint32_t kParameterSchemaVersion = 3;
inline constexpr std::uint32_t kAlgorithmVersion = 1;
inline constexpr const char* kAlgorithmId = "ra-cfa";
inline constexpr const char* kAlgorithmVersionId = "ra-cfa-v1";
inline constexpr const char* kNormalizedMosaicContractId =
    "linearized-black-subtracted-white-normalized-bayer-v1";
inline constexpr const char* kOutputContractId =
    "reference-pre-gain-normalized-bayer-v1";

enum class CfaSite : std::uint8_t {
    Red,
    Green0,
    Green1,
    Blue
};

struct CfaOffset {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

struct PixelExtent {
    std::uint64_t width = 0;
    std::uint64_t height = 0;
};

struct RawCoordinate {
    double x = 0.0;
    double y = 0.0;
};

struct CfaPlaneCoordinate {
    double x = 0.0;
    double y = 0.0;
    CfaSite site = CfaSite::Red;
};

struct CfaPyramidCoordinate {
    double x = 0.0;
    double y = 0.0;
    CfaSite site = CfaSite::Red;
    std::uint32_t level = 0;
};

struct CfaPlanePixel {
    std::int64_t x = 0;
    std::int64_t y = 0;
    CfaSite site = CfaSite::Red;
};

class CfaLayout {
public:
    static bool TryCreate(CfaPattern activeAreaPattern, CfaLayout& result);

    bool IsValid() const;
    CfaPattern ActiveAreaPattern() const;
    CfaSite SiteAt(std::int64_t rawX, std::int64_t rawY) const;
    CfaOffset OffsetFor(CfaSite site) const;
    PixelExtent PlaneExtent(CfaSite site, PixelExtent rawExtent) const;

    CfaLayout ShiftedToActiveArea(
        std::int64_t activeLeft,
        std::int64_t activeTop) const;

    CfaPlaneCoordinate RawToPlane(
        RawCoordinate raw,
        CfaSite site) const;
    RawCoordinate PlaneToRaw(CfaPlaneCoordinate plane) const;
    CfaPyramidCoordinate PlaneToPyramid(
        CfaPlaneCoordinate plane,
        std::uint32_t level) const;
    CfaPlaneCoordinate PyramidToPlane(CfaPyramidCoordinate pyramid) const;
    RawCoordinate PlanePixelToRaw(CfaPlanePixel plane) const;

private:
    CfaPattern m_ActiveAreaPattern = CfaPattern::Unknown;
    std::array<CfaSite, 4> m_SitesByOffset {
        CfaSite::Red,
        CfaSite::Green0,
        CfaSite::Green1,
        CfaSite::Blue
    };
};

const char* CfaSiteName(CfaSite site);

bool ExtractCfaPlane(
    const std::vector<float>& packedMosaic,
    PixelExtent rawExtent,
    const CfaLayout& layout,
    CfaSite site,
    std::vector<float>& plane,
    std::string* error = nullptr);

bool TryReadExactSameCfaSample(
    const std::vector<float>& packedMosaic,
    PixelExtent rawExtent,
    const CfaLayout& layout,
    CfaPlanePixel coordinate,
    float& sample);

enum class NormalizedSampleDomain {
    LinearizedBlackSubtractedWhiteNormalized
};

enum class CalibrationGainDomain {
    PreReferenceGain
};

struct NormalizedMosaicContract {
    std::uint32_t version = 1;
    NormalizedSampleDomain sampleDomain =
        NormalizedSampleDomain::LinearizedBlackSubtractedWhiteNormalized;
    CalibrationGainDomain gainDomain = CalibrationGainDomain::PreReferenceGain;
    bool packedBayer = true;
    bool preserveNegativeValues = true;
    bool separateSaturationMask = true;
    bool whiteBalanceApplied = false;
    bool demosaiced = false;
    bool geometricResamplingApplied = false;
};

struct OutputContract {
    std::uint32_t version = 1;
    NormalizedMosaicContract mosaic;
    bool preserveReferenceDimensions = true;
    bool preserveReferenceCfaLayout = true;
    bool referenceExposureDomain = true;
    bool referenceMomentAnchored = true;
};

NormalizedMosaicContract CanonicalNormalizedMosaicContract();
OutputContract CanonicalOutputContract();
bool IsCanonical(const NormalizedMosaicContract& contract);
bool IsCanonical(const OutputContract& contract);

enum class DecisionReason {
    None,
    ReferenceUnreadable,
    ReferenceIncompatible,
    AlternateUnreadable,
    AlternateIncompatible,
    UnsupportedRawOperation,
    NoiseModelUnavailable,
    GlobalRegistrationFailed,
    RadiometricScaleFailed,
    LocalRegistrationFailed,
    InvalidSourceSupport,
    AlternateClipped,
    AlternateDefective,
    InvalidNumericInput,
    ForwardBackwardFailure,
    MotionFieldAmbiguous,
    ReferenceClipped,
    InvalidReferenceGain,
    NoValidCandidate,
    AlternateWeightInsufficient,
    AllAlternatesRejected,
    NumericalFallback,
    ReferenceDefectDeferred,
    CacheCorrupt,
    OutOfMemory,
    Canceled
};

enum class DecisionDisposition {
    None,
    HardNodeError,
    SkipAlternate,
    RejectAlternateSample,
    ExactReferenceFallback,
    DeferReferenceDefectRepair,
    Canceled
};

const char* DecisionReasonName(DecisionReason reason);
DecisionDisposition DispositionFor(DecisionReason reason);
bool IsExactReferenceFallbackReason(DecisionReason reason);

struct ExactReferenceFallback {
    std::vector<float> normalizedMosaic;
    DecisionReason reason = DecisionReason::None;
    bool exactReferenceCopy = false;
};

ExactReferenceFallback MakeExactReferenceFallback(
    const std::vector<float>& referenceNormalizedMosaic,
    DecisionReason reason);

struct RawRadiometricParameters {
    bool preserveNegativeValues = true;
    double saturationDnMargin = 4.0;
    double saturationNoiseSigmaMargin = 2.0;
    std::uint32_t exposureIrlsIterations = 3;
    double exposureHuberDelta = 2.5;
    double exposureFitSignalMin = 0.01;
    double exposureFitSignalMax = 0.85;
    std::uint64_t exposureFitMinimumSamples = 10000;
    double exposureFitMinimumFraction = 0.001;
    double fittedScaleMin = 0.5;
    double fittedScaleMax = 2.0;
    double metadataDeviationWarningEv = 0.5;
    double cfaScaleDisagreementFraction = 0.02;
    double maximumComparisonGain = 16.0;
};

struct RegistrationParameters {
    std::uint32_t pyramidLevels = 4;
    std::string globalWarpModel = "affine";
    std::string sameCfaSampler = "keys-bicubic";
    std::array<double, 5> pyramidKernel {
        1.0 / 16.0,
        4.0 / 16.0,
        6.0 / 16.0,
        4.0 / 16.0,
        1.0 / 16.0
    };
    std::uint32_t affineIterationsPerLevel = 8;
    double affineRotationLimitDegrees = 5.0;
    double affineSingularValueMin = 0.95;
    double affineSingularValueMax = 1.05;
    double minimumGlobalOverlapFraction = 0.50;
    std::uint32_t finestPatchPlanePixels = 32;
    std::uint32_t finestStridePlanePixels = 16;
    std::uint32_t coarsePatchLevelPixels = 16;
    std::array<std::uint32_t, 4> searchRadiiFineToCoarse { 1, 2, 2, 4 };
    double registrationHuberDelta = 2.5;
    std::uint32_t subpixelIterations = 3;
    double maximumSubpixelStepPlanePixels = 0.75;
    double maximumDistanceFromDiscreteSeedPlanePixels = 1.0;
    double positionalConfidenceScaleRawPixels = 0.35;
    double warpCovarianceFloorRawPixels = 0.05;
    double warpCovarianceEigenvalueMinRawPixels = 0.01;
    double warpCovarianceEigenvalueMaxRawPixels = 4.0;
    double uniquenessTransitionMin = 0.02;
    double uniquenessTransitionMax = 0.15;
    double forwardBackwardCovarianceFloorRawPixels = 0.10;
    double forwardBackwardMahalanobisHardLimit = 9.21;
    double forwardBackwardEuclideanHardLimitRawPixels = 2.0;
    double motionDisagreementConfidenceScaleRawPixels = 0.35;
    double motionDisagreementHardLimitRawPixels = 0.75;
    double flatSafeCovarianceRawPixels = 1.0;
    double flatSafeParentResidualLimit = 1.5;
    double flatSafePhotometricCostLimit = 0.25;
    double flatSafeMinimumValidFraction = 0.80;
    double flatSafeNeighborDifferenceLimitRawPixels = 0.50;
    double keysBicubicParameter = -0.5;
    double minimumTileValidFraction = 0.70;
    std::uint32_t minimumStructuredSamples = 64u;
    double localHessianConditionLimit = 1.0e4;
    double localHessianAbsoluteDamping = 1.0e-9;
    double localCovarianceRegularization = 1.0e-9;
    double localNumericalVarianceFloor = 1.0e-12;
    double subpixelConvergencePlanePixels = 0.01;
    std::uint32_t maximumSubpixelCostIncreases = 2u;
    double cappedResidualSquared = 16.0;
    double flatSafeUnobservableSigmaRawPixels = 1.0;
    double covarianceResidualScaleFloor = 1.0;
    double candidateTieTolerance = 1.0e-12;
    double interpolationWeightEpsilon = 1.0e-12;
};

struct ReliabilityParameters {
    std::string storageFormat = "uint16-unorm";
    std::uint32_t storageTileCells = 256u;
    std::uint32_t patchSupportBayerCells = 5;
    std::uint32_t patchMinimumValidResiduals = 50;
    double patchFullWeightSigma = 1.5;
    double patchZeroWeightSigma = 3.5;
    std::uint32_t minimumFilterRadiusCells = 1;
    double reliabilityRemapMin = 0.20;
    double reliabilityRemapMax = 0.80;
    double trustedPixelFullWeightSigma = 2.5;
    double trustedPixelZeroWeightSigma = 5.0;
    double lowConfidencePixelFullWeightSigma = 2.0;
    double lowConfidencePixelZeroWeightSigma = 4.0;
    double absoluteSafetyDnMultiplier = 4.0;
    double absoluteSafetyNoiseSigmaMultiplier = 4.0;
    double absoluteSafetyRelativeFraction = 0.10;
    double frameUsableReliabilityThreshold = 0.20;
    double frameUsableMaximumFraction = 0.25;
    std::uint64_t frameUsableMinimumCells = 256u;
    double frameUsableMinimumFraction = 0.01;
    std::array<double, 5> noiseModelConfidence { 1.0, 0.90, 0.65, 0.35, 0.0 };
};

struct FusionParameters {
    std::string accumulatorPrecision = "float64";
    double oneAlternateWeightCapRelativeToReference = 4.0;
    double lowConfidenceTotalWeightCapRelativeToReference = 8.0;
    double exactFallbackAlternateToReferenceRatio = 0.05;
    double numericalVarianceFloor = 1.0e-12;
    std::uint32_t referenceDefectMinimumAlternates = 3;
    double referenceDefectMinimumGate = 0.80;
    double referenceDefectAgreementSigma = 3.5;
    std::uint32_t outputTileRawPixels = 512;
};

struct Parameters {
    std::uint32_t schemaVersion = kParameterSchemaVersion;
    std::string algorithmId = kAlgorithmId;
    std::uint32_t algorithmVersion = kAlgorithmVersion;
    std::string normalizedMosaicContractId = kNormalizedMosaicContractId;
    std::string outputContractId = kOutputContractId;
    RawRadiometricParameters radiometric;
    RegistrationParameters registration;
    ReliabilityParameters reliability;
    FusionParameters fusion;
};

nlohmann::json SerializeParameters(const Parameters& parameters);
bool DeserializeParameters(
    const nlohmann::json& value,
    Parameters& parameters,
    std::string* error = nullptr);
bool ValidateParameters(const Parameters& parameters, std::string* error = nullptr);

} // namespace Raw::Mfd
