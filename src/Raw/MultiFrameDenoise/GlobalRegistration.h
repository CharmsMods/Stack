#pragma once

#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kGlobalRegistrationContractVersion = 1;
inline constexpr const char* kGlobalRegistrationContractId =
    "ra-cfa-global-registration-exposure-v1";

struct GlobalPlanePair {
    CfaSite site = CfaSite::Red;
    PixelExtent extent;
    std::vector<double> reference;
    std::vector<double> alternate;
    std::vector<std::uint8_t> validMask;
    double rawPixelsPerPlanePixel = 2.0;
    double userWeight = 1.0;
};

struct PhaseCorrelationOptions {
    double minimumValidFraction = 0.50;
    double minimumTextureVariance = 1.0e-8;
    double minimumPeakToSidelobeRatio = 8.0;
    double minimumPeakUniqueness = 0.02;
    std::uint32_t sidelobeExclusionRadius = 2u;
    double minimumOverlapFraction = 0.50;
    double maximumPlaneDisagreementRawPixels = 2.0;
    double spectrumMagnitudeEpsilon = 1.0e-12;
    std::uint64_t maximumPaddedSampleCount = 1ull << 24u;
};

struct PhasePlaneDiagnostics {
    CfaSite site = CfaSite::Red;
    bool usable = false;
    std::uint64_t validSampleCount = 0u;
    double validFraction = 0.0;
    double referenceTextureVariance = 0.0;
    double alternateTextureVariance = 0.0;
    double weight = 0.0;
    CfaPlaneCoordinate translationPlane;
    RawCoordinate translationRaw;
    double peak = 0.0;
    double peakToSidelobeRatio = 0.0;
    double uniqueness = 0.0;
    std::string reason;
};

enum class PhaseTranslationFailure : std::uint8_t {
    None = 0,
    InvalidInput,
    InsufficientCoverage,
    InsufficientTexture,
    TransformTooLarge,
    AmbiguousPeak,
    InsufficientOverlap,
    PlaneDisagreement,
    NumericalFailure
};

const char* PhaseTranslationFailureName(PhaseTranslationFailure failure);

struct PhaseTranslationResult {
    bool accepted = false;
    PhaseTranslationFailure failure = PhaseTranslationFailure::None;
    std::string message;
    RawCoordinate translationRaw;
    double peak = 0.0;
    double peakToSidelobeRatio = 0.0;
    double uniqueness = 0.0;
    double overlapFraction = 0.0;
    std::vector<PhasePlaneDiagnostics> planes;
};

bool EstimateMultichannelPhaseTranslation(
    const std::vector<GlobalPlanePair>& planes,
    const PhaseCorrelationOptions& options,
    PhaseTranslationResult& result,
    std::string* error = nullptr);

struct ExposureSample {
    double referenceValue = 0.0;
    double alternateValue = 0.0;
    double referenceVariance = 0.0;
    double alternateVariance = 0.0;
    double usableCodeSpanDn = 1.0;
    double offsetVariance = 0.0;
    CfaSite site = CfaSite::Red;
    bool valid = true;
};

struct ExposureMetadataPrior {
    bool available = false;
    bool trustworthy = false;
    double scale = 1.0;
};

struct ProvisionalExposureResult {
    bool valid = false;
    bool usedMetadata = false;
    double scale = 1.0;
    std::uint64_t ratioCount = 0u;
    std::string message;
};

bool EstimateProvisionalExposureScale(
    const std::vector<ExposureSample>& samples,
    const ExposureMetadataPrior& metadata,
    const RawRadiometricParameters& parameters,
    ProvisionalExposureResult& result,
    std::string* error = nullptr);

enum class ExposureFitFailure : std::uint8_t {
    None = 0,
    InvalidInput,
    InsufficientSamples,
    SingularFit,
    ScaleOutOfRange,
    MetadataDeviation,
    CfaScaleDisagreement,
    BlackOffset,
    NumericalFailure
};

const char* ExposureFitFailureName(ExposureFitFailure failure);

struct ExposureSiteDiagnostics {
    CfaSite site = CfaSite::Red;
    std::uint64_t sampleCount = 0u;
    double scale = 1.0;
    double intercept = 0.0;
    double interceptLimit = 0.0;
    bool valid = false;
};

struct ExposureFitResult {
    bool accepted = false;
    ExposureFitFailure failure = ExposureFitFailure::None;
    std::string message;
    bool initializedFromMetadata = false;
    std::uint64_t eligibleSampleCount = 0u;
    double initialScale = 1.0;
    double scale = 1.0;
    double scaleVariance = 0.0;
    double robustStandardizedResidualScale = 0.0;
    double metadataDeviationEv = 0.0;
    double normalDenominator = 0.0;
    std::array<ExposureSiteDiagnostics, 4> sites;
};

bool FitGlobalExposureScale(
    const std::vector<ExposureSample>& samples,
    std::uint64_t imageRawPixelCount,
    const ExposureMetadataPrior& metadata,
    const RawRadiometricParameters& parameters,
    ExposureFitResult& result,
    std::string* error = nullptr);

struct AffineModel {
    RawCoordinate centerRaw;
    std::array<double, 4> linear { 1.0, 0.0, 0.0, 1.0 };
    RawCoordinate translationRaw;

    RawCoordinate Map(RawCoordinate referenceRaw) const;
};

struct AffineReferenceSample {
    RawCoordinate referenceRaw;
    CfaSite site = CfaSite::Red;
    double referenceValue = 0.0;
    double referenceVariance = 0.0;
    bool valid = true;
};

struct AffineSourceSample {
    double value = 0.0;
    RawSignalGradient gradientRaw;
    double variance = 0.0;
};

using AffineSourceEvaluator = std::function<bool(
    CfaSite site,
    RawCoordinate sourceRaw,
    AffineSourceSample& sample)>;

struct AffineRefinementOptions {
    PixelExtent referenceExtent;
    PixelExtent alternateExtent;
    double exposureScale = 1.0;
    std::uint32_t iterations = 8u;
    double huberDelta = 2.5;
    double relativeDamping = 1.0e-6;
    double absoluteDamping = 1.0e-9;
    double maximumNormalCondition = 1.0e12;
    double minimumOverlapFraction = 0.50;
    double singularValueMinimum = 0.95;
    double singularValueMaximum = 1.05;
    double determinantMinimum = 0.90;
    double determinantMaximum = 1.10;
    double maximumRotationDegrees = 5.0;
    double numericalVarianceFloor = 1.0e-12;
    std::uint64_t minimumSamples = 64u;
};

enum class AffineRefinementFailure : std::uint8_t {
    None = 0,
    InvalidInput,
    InsufficientSamples,
    SingularNormalMatrix,
    NoImprovement,
    ImplausibleLinearPart,
    InsufficientOverlap,
    NumericalFailure
};

const char* AffineRefinementFailureName(AffineRefinementFailure failure);

struct AffineRefinementResult {
    bool acceptedAffine = false;
    bool translationFallbackAvailable = false;
    AffineRefinementFailure failure = AffineRefinementFailure::None;
    std::string message;
    AffineModel model;
    std::array<double, 36> parameterCovariance {};
    std::uint64_t validSampleCount = 0u;
    std::uint32_t acceptedIterations = 0u;
    double initialObjective = 0.0;
    double finalObjective = 0.0;
    double normalCondition = 0.0;
    double singularValueMinimum = 0.0;
    double singularValueMaximum = 0.0;
    double determinant = 0.0;
    double rotationDegrees = 0.0;
    double overlapFraction = 0.0;
};

bool RefineGlobalAffine(
    const std::vector<AffineReferenceSample>& referenceSamples,
    const AffineSourceEvaluator& sourceEvaluator,
    const AffineModel& translationSeed,
    const AffineRefinementOptions& options,
    AffineRefinementResult& result,
    std::string* error = nullptr);

enum class GlobalFrameAlignmentChoice : std::uint8_t {
    Reject = 0,
    Translation,
    Affine,
    Identity
};

struct GlobalFrameAlignmentDecision {
    GlobalFrameAlignmentChoice choice = GlobalFrameAlignmentChoice::Reject;
    bool accepted = false;
    std::string message;
    AffineModel model;
    double exposureScale = 1.0;
    double exposureScaleVariance = 0.0;
};

GlobalFrameAlignmentDecision DecideGlobalFrameAlignment(
    const PhaseTranslationResult& translation,
    const ExposureFitResult& exposure,
    const AffineRefinementResult& affine,
    RawCoordinate referenceCenter);

} // namespace Raw::Mfd
