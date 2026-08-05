#include "Raw/MultiFrameDenoise/Contracts.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Raw::Mfd {
namespace {

std::int64_t PositiveModulo2(std::int64_t value) {
    const std::int64_t result = value % 2;
    return result < 0 ? result + 2 : result;
}

std::size_t OffsetIndex(std::int64_t x, std::int64_t y) {
    return static_cast<std::size_t>(PositiveModulo2(y) * 2 + PositiveModulo2(x));
}

CfaPattern PatternFromSites(const std::array<CfaSite, 4>& sites) {
    const auto color = [](CfaSite site) {
        if (site == CfaSite::Red) return 0;
        if (site == CfaSite::Blue) return 2;
        return 1;
    };
    const std::array<int, 4> colors {
        color(sites[0]), color(sites[1]), color(sites[2]), color(sites[3])
    };
    if (colors == std::array<int, 4>{ 0, 1, 1, 2 }) return CfaPattern::RGGB;
    if (colors == std::array<int, 4>{ 2, 1, 1, 0 }) return CfaPattern::BGGR;
    if (colors == std::array<int, 4>{ 1, 2, 0, 1 }) return CfaPattern::GBRG;
    if (colors == std::array<int, 4>{ 1, 0, 2, 1 }) return CfaPattern::GRBG;
    return CfaPattern::Unknown;
}

std::uint64_t AxisPlaneSize(std::uint64_t rawSize, std::int32_t offset) {
    const std::uint64_t positiveOffset = static_cast<std::uint64_t>(std::max(0, offset));
    if (rawSize <= positiveOffset) return 0;
    return ((rawSize - 1u - positiveOffset) / 2u) + 1u;
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0 || extent.height == 0) {
        count = 0;
        return true;
    }
    if (extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return false;
    }
    const std::uint64_t product = extent.width * extent.height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

bool Finite(double value) {
    return std::isfinite(value);
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

template <typename T>
bool ReadRequired(const nlohmann::json& object, const char* key, T& result) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) return false;
    try {
        result = it->get<T>();
        return true;
    } catch (...) {
        return false;
    }
}

bool ReadRadiometric(
    const nlohmann::json& value,
    RawRadiometricParameters& p,
    std::string* error) {
    if (!value.is_object()) return Fail(error, "MFD radiometric parameters must be an object.");
#define MFD_READ_RAD(name) if (!ReadRequired(value, #name, p.name)) return Fail(error, "Missing or invalid MFD radiometric parameter: " #name);
    MFD_READ_RAD(preserveNegativeValues)
    MFD_READ_RAD(saturationDnMargin)
    MFD_READ_RAD(saturationNoiseSigmaMargin)
    MFD_READ_RAD(exposureIrlsIterations)
    MFD_READ_RAD(exposureHuberDelta)
    MFD_READ_RAD(exposureFitSignalMin)
    MFD_READ_RAD(exposureFitSignalMax)
    MFD_READ_RAD(exposureFitMinimumSamples)
    MFD_READ_RAD(exposureFitMinimumFraction)
    MFD_READ_RAD(fittedScaleMin)
    MFD_READ_RAD(fittedScaleMax)
    MFD_READ_RAD(metadataDeviationWarningEv)
    MFD_READ_RAD(cfaScaleDisagreementFraction)
    MFD_READ_RAD(maximumComparisonGain)
#undef MFD_READ_RAD
    return true;
}

bool ReadRegistration(
    const nlohmann::json& value,
    RegistrationParameters& p,
    std::uint32_t schemaVersion,
    std::string* error) {
    if (!value.is_object()) return Fail(error, "MFD registration parameters must be an object.");
#define MFD_READ_REG(name) if (!ReadRequired(value, #name, p.name)) return Fail(error, "Missing or invalid MFD registration parameter: " #name);
    MFD_READ_REG(pyramidLevels)
    MFD_READ_REG(globalWarpModel)
    MFD_READ_REG(sameCfaSampler)
    MFD_READ_REG(pyramidKernel)
    MFD_READ_REG(affineIterationsPerLevel)
    MFD_READ_REG(affineRotationLimitDegrees)
    MFD_READ_REG(affineSingularValueMin)
    MFD_READ_REG(affineSingularValueMax)
    MFD_READ_REG(minimumGlobalOverlapFraction)
    MFD_READ_REG(finestPatchPlanePixels)
    MFD_READ_REG(finestStridePlanePixels)
    MFD_READ_REG(coarsePatchLevelPixels)
    MFD_READ_REG(searchRadiiFineToCoarse)
    MFD_READ_REG(registrationHuberDelta)
    MFD_READ_REG(subpixelIterations)
    MFD_READ_REG(maximumSubpixelStepPlanePixels)
    MFD_READ_REG(maximumDistanceFromDiscreteSeedPlanePixels)
    MFD_READ_REG(positionalConfidenceScaleRawPixels)
    MFD_READ_REG(warpCovarianceFloorRawPixels)
    MFD_READ_REG(warpCovarianceEigenvalueMinRawPixels)
    MFD_READ_REG(warpCovarianceEigenvalueMaxRawPixels)
    MFD_READ_REG(uniquenessTransitionMin)
    MFD_READ_REG(uniquenessTransitionMax)
    MFD_READ_REG(forwardBackwardCovarianceFloorRawPixels)
    MFD_READ_REG(forwardBackwardMahalanobisHardLimit)
    MFD_READ_REG(forwardBackwardEuclideanHardLimitRawPixels)
    MFD_READ_REG(motionDisagreementConfidenceScaleRawPixels)
    MFD_READ_REG(motionDisagreementHardLimitRawPixels)
    MFD_READ_REG(flatSafeCovarianceRawPixels)
    MFD_READ_REG(flatSafeParentResidualLimit)
    MFD_READ_REG(flatSafePhotometricCostLimit)
    MFD_READ_REG(flatSafeMinimumValidFraction)
    MFD_READ_REG(flatSafeNeighborDifferenceLimitRawPixels)
    MFD_READ_REG(keysBicubicParameter)
    if (schemaVersion >= 2u) {
        MFD_READ_REG(minimumTileValidFraction)
        MFD_READ_REG(minimumStructuredSamples)
        MFD_READ_REG(localHessianConditionLimit)
        MFD_READ_REG(localHessianAbsoluteDamping)
        MFD_READ_REG(localCovarianceRegularization)
        MFD_READ_REG(localNumericalVarianceFloor)
        MFD_READ_REG(subpixelConvergencePlanePixels)
        MFD_READ_REG(maximumSubpixelCostIncreases)
        MFD_READ_REG(cappedResidualSquared)
        MFD_READ_REG(flatSafeUnobservableSigmaRawPixels)
        MFD_READ_REG(covarianceResidualScaleFloor)
        MFD_READ_REG(candidateTieTolerance)
        MFD_READ_REG(interpolationWeightEpsilon)
    }
#undef MFD_READ_REG
    return true;
}

bool ReadReliability(
    const nlohmann::json& value,
    ReliabilityParameters& p,
    std::uint32_t schemaVersion,
    std::string* error) {
    if (!value.is_object()) return Fail(error, "MFD reliability parameters must be an object.");
#define MFD_READ_REL(name) if (!ReadRequired(value, #name, p.name)) return Fail(error, "Missing or invalid MFD reliability parameter: " #name);
    MFD_READ_REL(storageFormat)
    MFD_READ_REL(patchSupportBayerCells)
    MFD_READ_REL(patchMinimumValidResiduals)
    MFD_READ_REL(patchFullWeightSigma)
    MFD_READ_REL(patchZeroWeightSigma)
    MFD_READ_REL(minimumFilterRadiusCells)
    MFD_READ_REL(reliabilityRemapMin)
    MFD_READ_REL(reliabilityRemapMax)
    MFD_READ_REL(trustedPixelFullWeightSigma)
    MFD_READ_REL(trustedPixelZeroWeightSigma)
    MFD_READ_REL(lowConfidencePixelFullWeightSigma)
    MFD_READ_REL(lowConfidencePixelZeroWeightSigma)
    MFD_READ_REL(absoluteSafetyDnMultiplier)
    MFD_READ_REL(absoluteSafetyNoiseSigmaMultiplier)
    MFD_READ_REL(absoluteSafetyRelativeFraction)
    MFD_READ_REL(noiseModelConfidence)
    if (schemaVersion >= 3u) {
        MFD_READ_REL(storageTileCells)
        MFD_READ_REL(frameUsableReliabilityThreshold)
        MFD_READ_REL(frameUsableMaximumFraction)
        MFD_READ_REL(frameUsableMinimumCells)
        MFD_READ_REL(frameUsableMinimumFraction)
    }
#undef MFD_READ_REL
    return true;
}

bool ReadFusion(
    const nlohmann::json& value,
    FusionParameters& p,
    std::string* error) {
    if (!value.is_object()) return Fail(error, "MFD fusion parameters must be an object.");
#define MFD_READ_FUS(name) if (!ReadRequired(value, #name, p.name)) return Fail(error, "Missing or invalid MFD fusion parameter: " #name);
    MFD_READ_FUS(accumulatorPrecision)
    MFD_READ_FUS(oneAlternateWeightCapRelativeToReference)
    MFD_READ_FUS(lowConfidenceTotalWeightCapRelativeToReference)
    MFD_READ_FUS(exactFallbackAlternateToReferenceRatio)
    MFD_READ_FUS(numericalVarianceFloor)
    MFD_READ_FUS(referenceDefectMinimumAlternates)
    MFD_READ_FUS(referenceDefectMinimumGate)
    MFD_READ_FUS(referenceDefectAgreementSigma)
    MFD_READ_FUS(outputTileRawPixels)
#undef MFD_READ_FUS
    return true;
}

} // namespace

bool CfaLayout::TryCreate(CfaPattern activeAreaPattern, CfaLayout& result) {
    CfaLayout value;
    value.m_ActiveAreaPattern = activeAreaPattern;
    switch (activeAreaPattern) {
        case CfaPattern::RGGB:
            value.m_SitesByOffset = {
                CfaSite::Red, CfaSite::Green0, CfaSite::Green1, CfaSite::Blue
            };
            break;
        case CfaPattern::BGGR:
            value.m_SitesByOffset = {
                CfaSite::Blue, CfaSite::Green0, CfaSite::Green1, CfaSite::Red
            };
            break;
        case CfaPattern::GBRG:
            value.m_SitesByOffset = {
                CfaSite::Green0, CfaSite::Blue, CfaSite::Red, CfaSite::Green1
            };
            break;
        case CfaPattern::GRBG:
            value.m_SitesByOffset = {
                CfaSite::Green0, CfaSite::Red, CfaSite::Blue, CfaSite::Green1
            };
            break;
        case CfaPattern::Unknown:
        default:
            return false;
    }
    result = value;
    return true;
}

bool CfaLayout::IsValid() const {
    return m_ActiveAreaPattern != CfaPattern::Unknown;
}

CfaPattern CfaLayout::ActiveAreaPattern() const {
    return m_ActiveAreaPattern;
}

CfaSite CfaLayout::SiteAt(std::int64_t rawX, std::int64_t rawY) const {
    return m_SitesByOffset[OffsetIndex(rawX, rawY)];
}

CfaOffset CfaLayout::OffsetFor(CfaSite site) const {
    for (std::size_t index = 0; index < m_SitesByOffset.size(); ++index) {
        if (m_SitesByOffset[index] == site) {
            return {
                static_cast<std::int32_t>(index % 2u),
                static_cast<std::int32_t>(index / 2u)
            };
        }
    }
    return {};
}

PixelExtent CfaLayout::PlaneExtent(CfaSite site, PixelExtent rawExtent) const {
    const CfaOffset offset = OffsetFor(site);
    return {
        AxisPlaneSize(rawExtent.width, offset.x),
        AxisPlaneSize(rawExtent.height, offset.y)
    };
}

CfaLayout CfaLayout::ShiftedToActiveArea(
    std::int64_t activeLeft,
    std::int64_t activeTop) const {
    CfaLayout shifted;
    for (std::int64_t y = 0; y < 2; ++y) {
        for (std::int64_t x = 0; x < 2; ++x) {
            shifted.m_SitesByOffset[OffsetIndex(x, y)] =
                SiteAt(activeLeft + x, activeTop + y);
        }
    }
    shifted.m_ActiveAreaPattern = PatternFromSites(shifted.m_SitesByOffset);
    return shifted;
}

CfaPlaneCoordinate CfaLayout::RawToPlane(
    RawCoordinate raw,
    CfaSite site) const {
    const CfaOffset offset = OffsetFor(site);
    return {
        (raw.x - static_cast<double>(offset.x)) * 0.5,
        (raw.y - static_cast<double>(offset.y)) * 0.5,
        site
    };
}

RawCoordinate CfaLayout::PlaneToRaw(CfaPlaneCoordinate plane) const {
    const CfaOffset offset = OffsetFor(plane.site);
    return {
        plane.x * 2.0 + static_cast<double>(offset.x),
        plane.y * 2.0 + static_cast<double>(offset.y)
    };
}

CfaPyramidCoordinate CfaLayout::PlaneToPyramid(
    CfaPlaneCoordinate plane,
    std::uint32_t level) const {
    const double scale = std::ldexp(1.0, static_cast<int>(std::min(level, 1023u)));
    return { plane.x / scale, plane.y / scale, plane.site, level };
}

CfaPlaneCoordinate CfaLayout::PyramidToPlane(CfaPyramidCoordinate pyramid) const {
    const double scale = std::ldexp(1.0, static_cast<int>(std::min(pyramid.level, 1023u)));
    return { pyramid.x * scale, pyramid.y * scale, pyramid.site };
}

RawCoordinate CfaLayout::PlanePixelToRaw(CfaPlanePixel plane) const {
    return PlaneToRaw({
        static_cast<double>(plane.x),
        static_cast<double>(plane.y),
        plane.site
    });
}

const char* CfaSiteName(CfaSite site) {
    switch (site) {
        case CfaSite::Red: return "R";
        case CfaSite::Green0: return "G0";
        case CfaSite::Green1: return "G1";
        case CfaSite::Blue: return "B";
    }
    return "Unknown";
}

bool ExtractCfaPlane(
    const std::vector<float>& packedMosaic,
    PixelExtent rawExtent,
    const CfaLayout& layout,
    CfaSite site,
    std::vector<float>& plane,
    std::string* error) {
    std::size_t rawCount = 0;
    if (!layout.IsValid() || !CheckedSampleCount(rawExtent, rawCount) ||
        rawCount != packedMosaic.size()) {
        return Fail(error, "Packed mosaic dimensions do not match the supplied CFA layout.");
    }
    const PixelExtent planeExtent = layout.PlaneExtent(site, rawExtent);
    std::size_t planeCount = 0;
    if (!CheckedSampleCount(planeExtent, planeCount)) {
        return Fail(error, "CFA plane dimensions exceed the addressable sample count.");
    }
    plane.resize(planeCount);
    for (std::uint64_t y = 0; y < planeExtent.height; ++y) {
        for (std::uint64_t x = 0; x < planeExtent.width; ++x) {
            const RawCoordinate raw = layout.PlanePixelToRaw({
                static_cast<std::int64_t>(x),
                static_cast<std::int64_t>(y),
                site
            });
            const auto rawX = static_cast<std::uint64_t>(raw.x);
            const auto rawY = static_cast<std::uint64_t>(raw.y);
            plane[static_cast<std::size_t>(y * planeExtent.width + x)] =
                packedMosaic[static_cast<std::size_t>(rawY * rawExtent.width + rawX)];
        }
    }
    return true;
}

bool TryReadExactSameCfaSample(
    const std::vector<float>& packedMosaic,
    PixelExtent rawExtent,
    const CfaLayout& layout,
    CfaPlanePixel coordinate,
    float& sample) {
    if (coordinate.x < 0 || coordinate.y < 0 || !layout.IsValid()) return false;
    std::size_t rawCount = 0;
    if (!CheckedSampleCount(rawExtent, rawCount) || rawCount != packedMosaic.size()) return false;
    const PixelExtent planeExtent = layout.PlaneExtent(coordinate.site, rawExtent);
    if (static_cast<std::uint64_t>(coordinate.x) >= planeExtent.width ||
        static_cast<std::uint64_t>(coordinate.y) >= planeExtent.height) {
        return false;
    }
    const RawCoordinate raw = layout.PlanePixelToRaw(coordinate);
    const auto rawX = static_cast<std::uint64_t>(raw.x);
    const auto rawY = static_cast<std::uint64_t>(raw.y);
    if (layout.SiteAt(static_cast<std::int64_t>(rawX), static_cast<std::int64_t>(rawY)) !=
        coordinate.site) {
        return false;
    }
    sample = packedMosaic[static_cast<std::size_t>(rawY * rawExtent.width + rawX)];
    return true;
}

NormalizedMosaicContract CanonicalNormalizedMosaicContract() {
    return {};
}

OutputContract CanonicalOutputContract() {
    return {};
}

bool IsCanonical(const NormalizedMosaicContract& contract) {
    return contract.version == 1u &&
        contract.sampleDomain == NormalizedSampleDomain::LinearizedBlackSubtractedWhiteNormalized &&
        contract.gainDomain == CalibrationGainDomain::PreReferenceGain &&
        contract.packedBayer &&
        contract.preserveNegativeValues &&
        contract.separateSaturationMask &&
        !contract.whiteBalanceApplied &&
        !contract.demosaiced &&
        !contract.geometricResamplingApplied;
}

bool IsCanonical(const OutputContract& contract) {
    return contract.version == 1u &&
        IsCanonical(contract.mosaic) &&
        contract.preserveReferenceDimensions &&
        contract.preserveReferenceCfaLayout &&
        contract.referenceExposureDomain &&
        contract.referenceMomentAnchored;
}

const char* DecisionReasonName(DecisionReason reason) {
    switch (reason) {
        case DecisionReason::None: return "none";
        case DecisionReason::ReferenceUnreadable: return "reference-unreadable";
        case DecisionReason::ReferenceIncompatible: return "reference-incompatible";
        case DecisionReason::AlternateUnreadable: return "alternate-unreadable";
        case DecisionReason::AlternateIncompatible: return "alternate-incompatible";
        case DecisionReason::UnsupportedRawOperation: return "unsupported-raw-operation";
        case DecisionReason::NoiseModelUnavailable: return "noise-model-unavailable";
        case DecisionReason::GlobalRegistrationFailed: return "global-registration-failed";
        case DecisionReason::RadiometricScaleFailed: return "radiometric-scale-failed";
        case DecisionReason::LocalRegistrationFailed: return "local-registration-failed";
        case DecisionReason::InvalidSourceSupport: return "invalid-source-support";
        case DecisionReason::AlternateClipped: return "alternate-clipped";
        case DecisionReason::AlternateDefective: return "alternate-defective";
        case DecisionReason::InvalidNumericInput: return "invalid-numeric-input";
        case DecisionReason::ForwardBackwardFailure: return "forward-backward-failure";
        case DecisionReason::MotionFieldAmbiguous: return "motion-field-ambiguous";
        case DecisionReason::ReferenceClipped: return "reference-clipped";
        case DecisionReason::InvalidReferenceGain: return "invalid-reference-gain";
        case DecisionReason::NoValidCandidate: return "no-valid-candidate";
        case DecisionReason::AlternateWeightInsufficient: return "alternate-weight-insufficient";
        case DecisionReason::AllAlternatesRejected: return "all-alternates-rejected";
        case DecisionReason::NumericalFallback: return "numerical-fallback";
        case DecisionReason::ReferenceDefectDeferred: return "reference-defect-deferred";
        case DecisionReason::CacheCorrupt: return "cache-corrupt";
        case DecisionReason::OutOfMemory: return "out-of-memory";
        case DecisionReason::Canceled: return "canceled";
    }
    return "none";
}

DecisionDisposition DispositionFor(DecisionReason reason) {
    switch (reason) {
        case DecisionReason::None:
            return DecisionDisposition::None;
        case DecisionReason::ReferenceUnreadable:
        case DecisionReason::ReferenceIncompatible:
        case DecisionReason::OutOfMemory:
            return DecisionDisposition::HardNodeError;
        case DecisionReason::AlternateUnreadable:
        case DecisionReason::AlternateIncompatible:
        case DecisionReason::UnsupportedRawOperation:
        case DecisionReason::GlobalRegistrationFailed:
        case DecisionReason::RadiometricScaleFailed:
        case DecisionReason::LocalRegistrationFailed:
        case DecisionReason::CacheCorrupt:
            return DecisionDisposition::SkipAlternate;
        case DecisionReason::InvalidSourceSupport:
        case DecisionReason::AlternateClipped:
        case DecisionReason::AlternateDefective:
        case DecisionReason::InvalidNumericInput:
        case DecisionReason::ForwardBackwardFailure:
        case DecisionReason::MotionFieldAmbiguous:
            return DecisionDisposition::RejectAlternateSample;
        case DecisionReason::NoiseModelUnavailable:
        case DecisionReason::ReferenceClipped:
        case DecisionReason::InvalidReferenceGain:
        case DecisionReason::NoValidCandidate:
        case DecisionReason::AlternateWeightInsufficient:
        case DecisionReason::AllAlternatesRejected:
        case DecisionReason::NumericalFallback:
            return DecisionDisposition::ExactReferenceFallback;
        case DecisionReason::ReferenceDefectDeferred:
            return DecisionDisposition::DeferReferenceDefectRepair;
        case DecisionReason::Canceled:
            return DecisionDisposition::Canceled;
    }
    return DecisionDisposition::None;
}

bool IsExactReferenceFallbackReason(DecisionReason reason) {
    return DispositionFor(reason) == DecisionDisposition::ExactReferenceFallback;
}

ExactReferenceFallback MakeExactReferenceFallback(
    const std::vector<float>& referenceNormalizedMosaic,
    DecisionReason reason) {
    ExactReferenceFallback result;
    result.normalizedMosaic = referenceNormalizedMosaic;
    result.reason = reason;
    result.exactReferenceCopy = IsExactReferenceFallbackReason(reason);
    return result;
}

nlohmann::json SerializeParameters(const Parameters& p) {
    return {
        { "schemaVersion", p.schemaVersion },
        { "algorithmId", p.algorithmId },
        { "algorithmVersion", p.algorithmVersion },
        { "normalizedMosaicContractId", p.normalizedMosaicContractId },
        { "outputContractId", p.outputContractId },
        { "radiometric", {
            { "preserveNegativeValues", p.radiometric.preserveNegativeValues },
            { "saturationDnMargin", p.radiometric.saturationDnMargin },
            { "saturationNoiseSigmaMargin", p.radiometric.saturationNoiseSigmaMargin },
            { "exposureIrlsIterations", p.radiometric.exposureIrlsIterations },
            { "exposureHuberDelta", p.radiometric.exposureHuberDelta },
            { "exposureFitSignalMin", p.radiometric.exposureFitSignalMin },
            { "exposureFitSignalMax", p.radiometric.exposureFitSignalMax },
            { "exposureFitMinimumSamples", p.radiometric.exposureFitMinimumSamples },
            { "exposureFitMinimumFraction", p.radiometric.exposureFitMinimumFraction },
            { "fittedScaleMin", p.radiometric.fittedScaleMin },
            { "fittedScaleMax", p.radiometric.fittedScaleMax },
            { "metadataDeviationWarningEv", p.radiometric.metadataDeviationWarningEv },
            { "cfaScaleDisagreementFraction", p.radiometric.cfaScaleDisagreementFraction },
            { "maximumComparisonGain", p.radiometric.maximumComparisonGain }
        } },
        { "registration", {
            { "pyramidLevels", p.registration.pyramidLevels },
            { "globalWarpModel", p.registration.globalWarpModel },
            { "sameCfaSampler", p.registration.sameCfaSampler },
            { "pyramidKernel", p.registration.pyramidKernel },
            { "affineIterationsPerLevel", p.registration.affineIterationsPerLevel },
            { "affineRotationLimitDegrees", p.registration.affineRotationLimitDegrees },
            { "affineSingularValueMin", p.registration.affineSingularValueMin },
            { "affineSingularValueMax", p.registration.affineSingularValueMax },
            { "minimumGlobalOverlapFraction", p.registration.minimumGlobalOverlapFraction },
            { "finestPatchPlanePixels", p.registration.finestPatchPlanePixels },
            { "finestStridePlanePixels", p.registration.finestStridePlanePixels },
            { "coarsePatchLevelPixels", p.registration.coarsePatchLevelPixels },
            { "searchRadiiFineToCoarse", p.registration.searchRadiiFineToCoarse },
            { "registrationHuberDelta", p.registration.registrationHuberDelta },
            { "subpixelIterations", p.registration.subpixelIterations },
            { "maximumSubpixelStepPlanePixels", p.registration.maximumSubpixelStepPlanePixels },
            { "maximumDistanceFromDiscreteSeedPlanePixels", p.registration.maximumDistanceFromDiscreteSeedPlanePixels },
            { "positionalConfidenceScaleRawPixels", p.registration.positionalConfidenceScaleRawPixels },
            { "warpCovarianceFloorRawPixels", p.registration.warpCovarianceFloorRawPixels },
            { "warpCovarianceEigenvalueMinRawPixels", p.registration.warpCovarianceEigenvalueMinRawPixels },
            { "warpCovarianceEigenvalueMaxRawPixels", p.registration.warpCovarianceEigenvalueMaxRawPixels },
            { "uniquenessTransitionMin", p.registration.uniquenessTransitionMin },
            { "uniquenessTransitionMax", p.registration.uniquenessTransitionMax },
            { "forwardBackwardCovarianceFloorRawPixels", p.registration.forwardBackwardCovarianceFloorRawPixels },
            { "forwardBackwardMahalanobisHardLimit", p.registration.forwardBackwardMahalanobisHardLimit },
            { "forwardBackwardEuclideanHardLimitRawPixels", p.registration.forwardBackwardEuclideanHardLimitRawPixels },
            { "motionDisagreementConfidenceScaleRawPixels", p.registration.motionDisagreementConfidenceScaleRawPixels },
            { "motionDisagreementHardLimitRawPixels", p.registration.motionDisagreementHardLimitRawPixels },
            { "flatSafeCovarianceRawPixels", p.registration.flatSafeCovarianceRawPixels },
            { "flatSafeParentResidualLimit", p.registration.flatSafeParentResidualLimit },
            { "flatSafePhotometricCostLimit", p.registration.flatSafePhotometricCostLimit },
            { "flatSafeMinimumValidFraction", p.registration.flatSafeMinimumValidFraction },
            { "flatSafeNeighborDifferenceLimitRawPixels", p.registration.flatSafeNeighborDifferenceLimitRawPixels },
            { "keysBicubicParameter", p.registration.keysBicubicParameter },
            { "minimumTileValidFraction", p.registration.minimumTileValidFraction },
            { "minimumStructuredSamples", p.registration.minimumStructuredSamples },
            { "localHessianConditionLimit", p.registration.localHessianConditionLimit },
            { "localHessianAbsoluteDamping", p.registration.localHessianAbsoluteDamping },
            { "localCovarianceRegularization", p.registration.localCovarianceRegularization },
            { "localNumericalVarianceFloor", p.registration.localNumericalVarianceFloor },
            { "subpixelConvergencePlanePixels", p.registration.subpixelConvergencePlanePixels },
            { "maximumSubpixelCostIncreases", p.registration.maximumSubpixelCostIncreases },
            { "cappedResidualSquared", p.registration.cappedResidualSquared },
            { "flatSafeUnobservableSigmaRawPixels", p.registration.flatSafeUnobservableSigmaRawPixels },
            { "covarianceResidualScaleFloor", p.registration.covarianceResidualScaleFloor },
            { "candidateTieTolerance", p.registration.candidateTieTolerance },
            { "interpolationWeightEpsilon", p.registration.interpolationWeightEpsilon }
        } },
        { "reliability", {
            { "storageFormat", p.reliability.storageFormat },
            { "storageTileCells", p.reliability.storageTileCells },
            { "patchSupportBayerCells", p.reliability.patchSupportBayerCells },
            { "patchMinimumValidResiduals", p.reliability.patchMinimumValidResiduals },
            { "patchFullWeightSigma", p.reliability.patchFullWeightSigma },
            { "patchZeroWeightSigma", p.reliability.patchZeroWeightSigma },
            { "minimumFilterRadiusCells", p.reliability.minimumFilterRadiusCells },
            { "reliabilityRemapMin", p.reliability.reliabilityRemapMin },
            { "reliabilityRemapMax", p.reliability.reliabilityRemapMax },
            { "trustedPixelFullWeightSigma", p.reliability.trustedPixelFullWeightSigma },
            { "trustedPixelZeroWeightSigma", p.reliability.trustedPixelZeroWeightSigma },
            { "lowConfidencePixelFullWeightSigma", p.reliability.lowConfidencePixelFullWeightSigma },
            { "lowConfidencePixelZeroWeightSigma", p.reliability.lowConfidencePixelZeroWeightSigma },
            { "absoluteSafetyDnMultiplier", p.reliability.absoluteSafetyDnMultiplier },
            { "absoluteSafetyNoiseSigmaMultiplier", p.reliability.absoluteSafetyNoiseSigmaMultiplier },
            { "absoluteSafetyRelativeFraction", p.reliability.absoluteSafetyRelativeFraction },
            { "frameUsableReliabilityThreshold", p.reliability.frameUsableReliabilityThreshold },
            { "frameUsableMaximumFraction", p.reliability.frameUsableMaximumFraction },
            { "frameUsableMinimumCells", p.reliability.frameUsableMinimumCells },
            { "frameUsableMinimumFraction", p.reliability.frameUsableMinimumFraction },
            { "noiseModelConfidence", p.reliability.noiseModelConfidence }
        } },
        { "fusion", {
            { "accumulatorPrecision", p.fusion.accumulatorPrecision },
            { "oneAlternateWeightCapRelativeToReference", p.fusion.oneAlternateWeightCapRelativeToReference },
            { "lowConfidenceTotalWeightCapRelativeToReference", p.fusion.lowConfidenceTotalWeightCapRelativeToReference },
            { "exactFallbackAlternateToReferenceRatio", p.fusion.exactFallbackAlternateToReferenceRatio },
            { "numericalVarianceFloor", p.fusion.numericalVarianceFloor },
            { "referenceDefectMinimumAlternates", p.fusion.referenceDefectMinimumAlternates },
            { "referenceDefectMinimumGate", p.fusion.referenceDefectMinimumGate },
            { "referenceDefectAgreementSigma", p.fusion.referenceDefectAgreementSigma },
            { "outputTileRawPixels", p.fusion.outputTileRawPixels }
        } }
    };
}

bool DeserializeParameters(
    const nlohmann::json& value,
    Parameters& parameters,
    std::string* error) {
    if (!value.is_object()) return Fail(error, "MFD parameter payload must be an object.");
    Parameters parsed;
    if (!ReadRequired(value, "schemaVersion", parsed.schemaVersion) ||
        !ReadRequired(value, "algorithmId", parsed.algorithmId) ||
        !ReadRequired(value, "algorithmVersion", parsed.algorithmVersion) ||
        !ReadRequired(value, "normalizedMosaicContractId", parsed.normalizedMosaicContractId) ||
        !ReadRequired(value, "outputContractId", parsed.outputContractId)) {
        return Fail(error, "MFD parameter identity is missing or invalid.");
    }
    const auto radiometric = value.find("radiometric");
    const auto registration = value.find("registration");
    const auto reliability = value.find("reliability");
    const auto fusion = value.find("fusion");
    const std::uint32_t serializedSchemaVersion = parsed.schemaVersion;
    if (serializedSchemaVersion != kLegacyParameterSchemaVersion &&
        serializedSchemaVersion != kPhase5ParameterSchemaVersion &&
        serializedSchemaVersion != kParameterSchemaVersion) {
        return Fail(error, "MFD parameter schema version is unsupported.");
    }
    if (radiometric == value.end() ||
        !ReadRadiometric(*radiometric, parsed.radiometric, error) ||
        registration == value.end() ||
        !ReadRegistration(
            *registration,
            parsed.registration,
            serializedSchemaVersion,
            error) ||
        reliability == value.end() ||
        !ReadReliability(
            *reliability,
            parsed.reliability,
            serializedSchemaVersion,
            error) ||
        fusion == value.end() ||
        !ReadFusion(*fusion, parsed.fusion, error)) {
        return false;
    }
    parsed.schemaVersion = kParameterSchemaVersion;
    if (!ValidateParameters(parsed, error)) return false;
    parameters = std::move(parsed);
    return true;
}

bool ValidateParameters(const Parameters& p, std::string* error) {
    if (p.schemaVersion != kParameterSchemaVersion ||
        p.algorithmId != kAlgorithmId ||
        p.algorithmVersion != kAlgorithmVersion ||
        p.normalizedMosaicContractId != kNormalizedMosaicContractId ||
        p.outputContractId != kOutputContractId) {
        return Fail(error, "MFD parameter identity does not match the supported RA-CFA V1 contract.");
    }
    const auto positive = [](double value) { return Finite(value) && value > 0.0; };
    const auto unit = [](double value) { return Finite(value) && value >= 0.0 && value <= 1.0; };
    const auto ascending = [](double low, double high) {
        return Finite(low) && Finite(high) && low < high;
    };
    if (!p.radiometric.preserveNegativeValues ||
        !positive(p.radiometric.saturationDnMargin) ||
        !positive(p.radiometric.saturationNoiseSigmaMargin) ||
        p.radiometric.exposureIrlsIterations == 0u ||
        !positive(p.radiometric.exposureHuberDelta) ||
        !ascending(p.radiometric.exposureFitSignalMin, p.radiometric.exposureFitSignalMax) ||
        !unit(p.radiometric.exposureFitSignalMin) ||
        !unit(p.radiometric.exposureFitSignalMax) ||
        p.radiometric.exposureFitMinimumSamples == 0u ||
        !unit(p.radiometric.exposureFitMinimumFraction) ||
        !positive(p.radiometric.fittedScaleMin) ||
        !ascending(p.radiometric.fittedScaleMin, p.radiometric.fittedScaleMax) ||
        !positive(p.radiometric.metadataDeviationWarningEv) ||
        !unit(p.radiometric.cfaScaleDisagreementFraction) ||
        !positive(p.radiometric.maximumComparisonGain)) {
        return Fail(error, "MFD radiometric parameters violate the RA-CFA V1 contract.");
    }
    double kernelSum = 0.0;
    for (double value : p.registration.pyramidKernel) {
        if (!Finite(value) || value < 0.0) {
            return Fail(error, "MFD pyramid kernel must contain finite nonnegative coefficients.");
        }
        kernelSum += value;
    }
    const bool searchRadiiValid = std::all_of(
        p.registration.searchRadiiFineToCoarse.begin(),
        p.registration.searchRadiiFineToCoarse.end(),
        [](std::uint32_t radius) { return radius > 0u; });
    if (p.registration.pyramidLevels != p.registration.searchRadiiFineToCoarse.size() ||
        p.registration.globalWarpModel != "affine" ||
        p.registration.sameCfaSampler != "keys-bicubic" ||
        !searchRadiiValid ||
        std::abs(kernelSum - 1.0) > 1.0e-12 ||
        p.registration.affineIterationsPerLevel == 0u ||
        !positive(p.registration.affineRotationLimitDegrees) ||
        !positive(p.registration.affineSingularValueMin) ||
        !ascending(p.registration.affineSingularValueMin, p.registration.affineSingularValueMax) ||
        !unit(p.registration.minimumGlobalOverlapFraction) ||
        p.registration.finestPatchPlanePixels == 0u ||
        p.registration.finestStridePlanePixels == 0u ||
        p.registration.coarsePatchLevelPixels == 0u ||
        !positive(p.registration.registrationHuberDelta) ||
        p.registration.subpixelIterations == 0u ||
        !positive(p.registration.maximumSubpixelStepPlanePixels) ||
        !positive(p.registration.maximumDistanceFromDiscreteSeedPlanePixels) ||
        !positive(p.registration.positionalConfidenceScaleRawPixels) ||
        !positive(p.registration.warpCovarianceFloorRawPixels) ||
        !ascending(
            p.registration.warpCovarianceEigenvalueMinRawPixels,
            p.registration.warpCovarianceEigenvalueMaxRawPixels) ||
        !ascending(p.registration.uniquenessTransitionMin, p.registration.uniquenessTransitionMax) ||
        !positive(p.registration.forwardBackwardCovarianceFloorRawPixels) ||
        !positive(p.registration.forwardBackwardMahalanobisHardLimit) ||
        !positive(p.registration.forwardBackwardEuclideanHardLimitRawPixels) ||
        !positive(p.registration.motionDisagreementConfidenceScaleRawPixels) ||
        !positive(p.registration.motionDisagreementHardLimitRawPixels) ||
        !positive(p.registration.flatSafeCovarianceRawPixels) ||
        !positive(p.registration.flatSafeParentResidualLimit) ||
        !positive(p.registration.flatSafePhotometricCostLimit) ||
        !unit(p.registration.flatSafeMinimumValidFraction) ||
        !positive(p.registration.flatSafeNeighborDifferenceLimitRawPixels) ||
        !Finite(p.registration.keysBicubicParameter) ||
        p.registration.keysBicubicParameter < -1.0 ||
        p.registration.keysBicubicParameter > 0.0 ||
        !unit(p.registration.minimumTileValidFraction) ||
        p.registration.minimumTileValidFraction <= 0.0 ||
        p.registration.minimumStructuredSamples == 0u ||
        !positive(p.registration.localHessianConditionLimit) ||
        !positive(p.registration.localHessianAbsoluteDamping) ||
        !positive(p.registration.localCovarianceRegularization) ||
        !positive(p.registration.localNumericalVarianceFloor) ||
        !positive(p.registration.subpixelConvergencePlanePixels) ||
        p.registration.maximumSubpixelCostIncreases == 0u ||
        !positive(p.registration.cappedResidualSquared) ||
        !positive(p.registration.flatSafeUnobservableSigmaRawPixels) ||
        !positive(p.registration.covarianceResidualScaleFloor) ||
        !positive(p.registration.candidateTieTolerance) ||
        !positive(p.registration.interpolationWeightEpsilon)) {
        return Fail(error, "MFD registration parameters violate the RA-CFA V1 contract.");
    }
    if (p.reliability.storageFormat != "uint16-unorm" ||
        p.reliability.storageTileCells == 0u ||
        p.reliability.patchSupportBayerCells == 0u ||
        (p.reliability.patchSupportBayerCells % 2u) == 0u ||
        p.reliability.patchMinimumValidResiduals == 0u ||
        !ascending(p.reliability.patchFullWeightSigma, p.reliability.patchZeroWeightSigma) ||
        p.reliability.minimumFilterRadiusCells == 0u ||
        !ascending(p.reliability.reliabilityRemapMin, p.reliability.reliabilityRemapMax) ||
        !unit(p.reliability.reliabilityRemapMin) ||
        !unit(p.reliability.reliabilityRemapMax) ||
        !ascending(
            p.reliability.trustedPixelFullWeightSigma,
            p.reliability.trustedPixelZeroWeightSigma) ||
        !ascending(
            p.reliability.lowConfidencePixelFullWeightSigma,
            p.reliability.lowConfidencePixelZeroWeightSigma) ||
        !positive(p.reliability.absoluteSafetyDnMultiplier) ||
        !positive(p.reliability.absoluteSafetyNoiseSigmaMultiplier) ||
        !unit(p.reliability.absoluteSafetyRelativeFraction) ||
        !unit(p.reliability.frameUsableReliabilityThreshold) ||
        !unit(p.reliability.frameUsableMaximumFraction) ||
        p.reliability.frameUsableMinimumCells == 0u ||
        !unit(p.reliability.frameUsableMinimumFraction)) {
        return Fail(error, "MFD reliability parameters violate the RA-CFA V1 contract.");
    }
    for (double value : p.reliability.noiseModelConfidence) {
        if (!unit(value)) return Fail(error, "MFD noise-model confidence must remain in [0,1].");
    }
    if (p.fusion.accumulatorPrecision != "float64" ||
        !positive(p.fusion.oneAlternateWeightCapRelativeToReference) ||
        !positive(p.fusion.lowConfidenceTotalWeightCapRelativeToReference) ||
        p.fusion.lowConfidenceTotalWeightCapRelativeToReference <
            p.fusion.oneAlternateWeightCapRelativeToReference ||
        !unit(p.fusion.exactFallbackAlternateToReferenceRatio) ||
        !positive(p.fusion.numericalVarianceFloor) ||
        p.fusion.referenceDefectMinimumAlternates < 3u ||
        !unit(p.fusion.referenceDefectMinimumGate) ||
        !positive(p.fusion.referenceDefectAgreementSigma) ||
        p.fusion.outputTileRawPixels == 0u) {
        return Fail(error, "MFD fusion parameters violate the RA-CFA V1 contract.");
    }
    return true;
}

} // namespace Raw::Mfd
