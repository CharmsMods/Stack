#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

bool SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool Finite(double value) {
    return std::isfinite(value);
}

bool KnownSite(CfaSite site) {
    switch (site) {
        case CfaSite::Red:
        case CfaSite::Green0:
        case CfaSite::Green1:
        case CfaSite::Blue:
            return true;
    }
    return false;
}

std::size_t SiteIndex(CfaSite site) {
    switch (site) {
        case CfaSite::Red: return 0u;
        case CfaSite::Green0: return 1u;
        case CfaSite::Green1: return 2u;
        case CfaSite::Blue: return 3u;
    }
    return 0u;
}

bool FailSample(
    SameCfaSampleResult& result,
    SameCfaSampleFailure failure,
    const std::string& message,
    std::string* error) {
    const CfaSite site = result.site;
    const RawCoordinate sourceRaw = result.sourceRaw;
    const CfaPlaneCoordinate sourcePlane = result.sourcePlane;
    result = {};
    result.site = site;
    result.sourceRaw = sourceRaw;
    result.sourcePlane = sourcePlane;
    result.failure = failure;
    result.message = message;
    return SetError(error, message);
}

bool CheckedPixelCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return false;
    }
    const std::uint64_t product = extent.width * extent.height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

bool ValidPreparedFrameContract(const PreparedRawFrame& frame) {
    CfaLayout layout;
    if (frame.contractVersion != kPreparationContractVersion ||
        frame.contractId != kPreparationContractId ||
        frame.activeExtent.width == 0u || frame.activeExtent.height == 0u ||
        frame.tileRawPixels == 0u || frame.tileColumns == 0u ||
        frame.tileRows == 0u ||
        !CfaLayout::TryCreate(frame.activeCfaPattern, layout)) {
        return false;
    }
    const std::uint64_t expectedColumns =
        (frame.activeExtent.width - 1u) / frame.tileRawPixels + 1u;
    const std::uint64_t expectedRows =
        (frame.activeExtent.height - 1u) / frame.tileRawPixels + 1u;
    return expectedColumns == frame.tileColumns &&
        expectedRows == frame.tileRows;
}

struct CachedPreparedTile {
    std::uint32_t tileX = 0u;
    std::uint32_t tileY = 0u;
    PreparedRawTile tile;
};

bool LoadPreparedTap(
    const PreparedRawFrame& frame,
    NormalizedTileCache& tileCache,
    std::uint64_t rawX,
    std::uint64_t rawY,
    std::vector<CachedPreparedTile>& loadedTiles,
    SameCfaTapInput& tap,
    SameCfaSampleResult& result,
    std::string* error) {
    const std::uint64_t tileX64 = rawX / frame.tileRawPixels;
    const std::uint64_t tileY64 = rawY / frame.tileRawPixels;
    if (tileX64 >= frame.tileColumns || tileY64 >= frame.tileRows ||
        tileX64 > std::numeric_limits<std::uint32_t>::max() ||
        tileY64 > std::numeric_limits<std::uint32_t>::max()) {
        return FailSample(
            result,
            SameCfaSampleFailure::FootprintOutsideActiveArea,
            "MFD same-CFA tap lies outside the prepared tile grid.",
            error);
    }
    const auto tileX = static_cast<std::uint32_t>(tileX64);
    const auto tileY = static_cast<std::uint32_t>(tileY64);
    CachedPreparedTile* cached = nullptr;
    for (CachedPreparedTile& candidate : loadedTiles) {
        if (candidate.tileX == tileX && candidate.tileY == tileY) {
            cached = &candidate;
            break;
        }
    }
    if (!cached) {
        CachedPreparedTile loaded;
        loaded.tileX = tileX;
        loaded.tileY = tileY;
        std::string tileError;
        const TileCacheReadStatus status = ReadPreparedTile(
            frame,
            tileCache,
            tileX,
            tileY,
            loaded.tile,
            &tileError);
        if (status != TileCacheReadStatus::Hit) {
            return FailSample(
                result,
                SameCfaSampleFailure::TileUnavailable,
                "MFD same-CFA footprint tile is unavailable: " + tileError,
                error);
        }
        loadedTiles.push_back(std::move(loaded));
        cached = &loadedTiles.back();
    }

    const PreparedRawTile& tile = cached->tile;
    const std::uint64_t expectedOriginX =
        static_cast<std::uint64_t>(tileX) * frame.tileRawPixels;
    const std::uint64_t expectedOriginY =
        static_cast<std::uint64_t>(tileY) * frame.tileRawPixels;
    const PixelExtent expectedExtent {
        std::min<std::uint64_t>(
            frame.tileRawPixels,
            frame.activeExtent.width - expectedOriginX),
        std::min<std::uint64_t>(
            frame.tileRawPixels,
            frame.activeExtent.height - expectedOriginY)
    };
    std::size_t expectedCount = 0u;
    if (tile.tileX != tileX || tile.tileY != tileY ||
        tile.originX != expectedOriginX || tile.originY != expectedOriginY ||
        tile.extent.width != expectedExtent.width ||
        tile.extent.height != expectedExtent.height ||
        !CheckedPixelCount(tile.extent, expectedCount) ||
        tile.normalizedMosaic.size() != expectedCount ||
        tile.comparisonGain.size() != expectedCount ||
        tile.sampleFlags.size() != expectedCount ||
        rawX < tile.originX || rawY < tile.originY) {
        return FailSample(
            result,
            SameCfaSampleFailure::MalformedTile,
            "MFD same-CFA footprint tile does not match its prepared-frame contract.",
            error);
    }
    const std::uint64_t localX = rawX - tile.originX;
    const std::uint64_t localY = rawY - tile.originY;
    if (localX >= tile.extent.width || localY >= tile.extent.height) {
        return FailSample(
            result,
            SameCfaSampleFailure::MalformedTile,
            "MFD same-CFA tap is outside its declared prepared tile.",
            error);
    }
    const std::size_t index = static_cast<std::size_t>(
        localY * tile.extent.width + localX);
    tap.normalizedSample = tile.normalizedMosaic[index];
    tap.comparisonGain = tile.comparisonGain[index];
    tap.sampleFlags = tile.sampleFlags[index];
    return true;
}

} // namespace

const char* SameCfaSampleFailureName(SameCfaSampleFailure failure) {
    switch (failure) {
        case SameCfaSampleFailure::None: return "none";
        case SameCfaSampleFailure::InvalidRequest: return "invalid-request";
        case SameCfaSampleFailure::FootprintOutsideActiveArea: return "footprint-outside-active-area";
        case SameCfaSampleFailure::TileUnavailable: return "tile-unavailable";
        case SameCfaSampleFailure::MalformedTile: return "malformed-tile";
        case SameCfaSampleFailure::NonFiniteTap: return "non-finite-tap";
        case SameCfaSampleFailure::NonPositiveGain: return "non-positive-gain";
        case SameCfaSampleFailure::ExplicitDecoderClip: return "explicit-decoder-clip";
        case SameCfaSampleFailure::SaturatedTap: return "saturated-tap";
        case SameCfaSampleFailure::DefectiveTap: return "defective-tap";
        case SameCfaSampleFailure::DecoderRepairedTap: return "decoder-repaired-tap";
        case SameCfaSampleFailure::InvalidNoiseModel: return "invalid-noise-model";
        case SameCfaSampleFailure::InvalidVariance: return "invalid-variance";
    }
    return "invalid-request";
}

double KeysBicubicKernel(double distance, double parameter) {
    if (!Finite(distance) || !Finite(parameter)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double t = std::abs(distance);
    if (t <= 1.0) {
        return (parameter + 2.0) * t * t * t -
            (parameter + 3.0) * t * t + 1.0;
    }
    if (t < 2.0) {
        return parameter * t * t * t -
            5.0 * parameter * t * t +
            8.0 * parameter * t - 4.0 * parameter;
    }
    return 0.0;
}

double KeysBicubicKernelDerivative(double distance, double parameter) {
    if (!Finite(distance) || !Finite(parameter)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double t = std::abs(distance);
    if (t == 0.0 || t >= 2.0) return 0.0;
    const double sign = distance < 0.0 ? -1.0 : 1.0;
    if (t <= 1.0) {
        return sign * (
            3.0 * (parameter + 2.0) * t * t -
            2.0 * (parameter + 3.0) * t);
    }
    return sign * (
        3.0 * parameter * t * t -
        10.0 * parameter * t + 8.0 * parameter);
}

bool BuildKeysBicubicFootprint(
    CfaPlaneCoordinate coordinate,
    KeysBicubicFootprint& footprint,
    std::string* error) {
    footprint = {};
    footprint.coordinate = coordinate;
    if (!KnownSite(coordinate.site) || !Finite(coordinate.x) ||
        !Finite(coordinate.y)) {
        return SetError(error, "MFD Keys footprint coordinate is invalid.");
    }
    const double floorX = std::floor(coordinate.x);
    const double floorY = std::floor(coordinate.y);
    constexpr double minimumBase = -9007199254740988.0;
    constexpr double maximumBase = 9007199254740988.0;
    if (floorX < minimumBase || floorX > maximumBase ||
        floorY < minimumBase || floorY > maximumBase) {
        return SetError(error, "MFD Keys footprint coordinate exceeds integer index range.");
    }
    const auto baseX = static_cast<std::int64_t>(floorX);
    const auto baseY = static_cast<std::int64_t>(floorY);
    std::size_t index = 0u;
    for (std::int64_t offsetY = -1; offsetY <= 2; ++offsetY) {
        const std::int64_t tapY = baseY + offsetY;
        const double distanceY = coordinate.y - static_cast<double>(tapY);
        const double weightY = KeysBicubicKernel(distanceY);
        const double derivativeY = KeysBicubicKernelDerivative(distanceY);
        for (std::int64_t offsetX = -1; offsetX <= 2; ++offsetX) {
            const std::int64_t tapX = baseX + offsetX;
            const double distanceX = coordinate.x - static_cast<double>(tapX);
            const double weightX = KeysBicubicKernel(distanceX);
            const double derivativeX = KeysBicubicKernelDerivative(distanceX);
            footprint.taps[index] = { tapX, tapY, coordinate.site };
            footprint.coefficients[index] = weightX * weightY;
            footprint.derivativeXPlane[index] = derivativeX * weightY;
            footprint.derivativeYPlane[index] = weightX * derivativeY;
            footprint.coefficientSum += footprint.coefficients[index];
            footprint.derivativeXSum += footprint.derivativeXPlane[index];
            footprint.derivativeYSum += footprint.derivativeYPlane[index];
            ++index;
        }
    }
    const auto finiteArray = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](double value) {
            return Finite(value);
        });
    };
    if (!finiteArray(footprint.coefficients) ||
        !finiteArray(footprint.derivativeXPlane) ||
        !finiteArray(footprint.derivativeYPlane) ||
        !Finite(footprint.coefficientSum) ||
        !Finite(footprint.derivativeXSum) ||
        !Finite(footprint.derivativeYSum) ||
        std::abs(footprint.coefficientSum - 1.0) > 1.0e-12 ||
        std::abs(footprint.derivativeXSum) > 1.0e-12 ||
        std::abs(footprint.derivativeYSum) > 1.0e-12) {
        return SetError(error, "MFD Keys footprint violates coefficient or derivative conservation.");
    }
    return true;
}

bool ValidateKeysBicubicFootprint(
    const KeysBicubicFootprint& footprint,
    PixelExtent planeExtent,
    std::string* error) {
    if (footprint.contractVersion != kSameCfaSamplerContractVersion ||
        footprint.contractId != kSameCfaSamplerContractId ||
        !KnownSite(footprint.coordinate.site) ||
        planeExtent.width == 0u || planeExtent.height == 0u ||
        planeExtent.width > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) ||
        planeExtent.height > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max())) {
        return SetError(error, "MFD Keys footprint contract or plane extent is invalid.");
    }
    for (const CfaPlanePixel& tap : footprint.taps) {
        if (tap.site != footprint.coordinate.site || tap.x < 0 || tap.y < 0 ||
            static_cast<std::uint64_t>(tap.x) >= planeExtent.width ||
            static_cast<std::uint64_t>(tap.y) >= planeExtent.height) {
            return SetError(error, "MFD Keys footprint is not fully inside the active CFA plane.");
        }
    }
    return true;
}

bool EvaluateSameCfaFootprintScalar(
    const KeysBicubicFootprint& footprint,
    const std::array<SameCfaTapInput, kSameCfaTapCount>& taps,
    const SiteNoiseProfile& noiseProfile,
    double usableCodeSpanDn,
    const SameCfaScalarParameters& parameters,
    SameCfaSampleResult& result,
    std::string* error) {
    result = {};
    result.site = footprint.coordinate.site;
    result.sourcePlane = footprint.coordinate;
    result.coefficientSum = footprint.coefficientSum;
    if (footprint.contractVersion != kSameCfaSamplerContractVersion ||
        footprint.contractId != kSameCfaSamplerContractId ||
        !KnownSite(footprint.coordinate.site) ||
        !Finite(usableCodeSpanDn) || usableCodeSpanDn <= 0.0 ||
        !Finite(parameters.exposureScale) || parameters.exposureScale <= 0.0 ||
        !Finite(parameters.exposureScaleVariance) ||
        parameters.exposureScaleVariance < 0.0 ||
        !Finite(parameters.referenceComparisonPilot) ||
        !Finite(parameters.numericalVarianceFloor) ||
        parameters.numericalVarianceFloor <= 0.0 ||
        std::abs(footprint.coefficientSum - 1.0) > 1.0e-12 ||
        std::abs(footprint.derivativeXSum) > 1.0e-12 ||
        std::abs(footprint.derivativeYSum) > 1.0e-12) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidRequest,
            "MFD scalar same-CFA sampler request is invalid.",
            error);
    }
    for (std::size_t index = 0u; index < footprint.taps.size(); ++index) {
        if (footprint.taps[index].site != footprint.coordinate.site ||
            !Finite(footprint.coefficients[index]) ||
            !Finite(footprint.derivativeXPlane[index]) ||
            !Finite(footprint.derivativeYPlane[index])) {
            return FailSample(
                result,
                SameCfaSampleFailure::InvalidRequest,
                "MFD scalar same-CFA footprint contains invalid taps or coefficients.",
                error);
        }
    }
    std::string noiseError;
    if (!ValidateSiteNoiseProfile(noiseProfile, &noiseError) ||
        !noiseProfile.quantizationIncluded) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidNoiseModel,
            "MFD scalar same-CFA sampler noise profile is invalid: " + noiseError,
            error);
    }

    std::vector<InterpolationVarianceTerm> interpolationTerms;
    interpolationTerms.reserve(kSameCfaTapCount);
    double value = 0.0;
    double darkVariance = 0.0;
    double gradientXPlane = 0.0;
    double gradientYPlane = 0.0;
    for (std::size_t index = 0u; index < taps.size(); ++index) {
        const SameCfaTapInput& tap = taps[index];
        if (HasSampleFlag(tap.sampleFlags, PreparedSampleFlag::ExplicitDecoderClip)) {
            return FailSample(
                result,
                SameCfaSampleFailure::ExplicitDecoderClip,
                "MFD same-CFA footprint touches an explicitly clipped decoder sample.",
                error);
        }
        if (HasSampleFlag(tap.sampleFlags, PreparedSampleFlag::Saturated)) {
            return FailSample(
                result,
                SameCfaSampleFailure::SaturatedTap,
                "MFD same-CFA footprint touches a saturated sample.",
                error);
        }
        if (HasSampleFlag(tap.sampleFlags, PreparedSampleFlag::Defective)) {
            return FailSample(
                result,
                SameCfaSampleFailure::DefectiveTap,
                "MFD same-CFA footprint touches a defective sample.",
                error);
        }
        if (HasSampleFlag(tap.sampleFlags, PreparedSampleFlag::DecoderRepaired)) {
            return FailSample(
                result,
                SameCfaSampleFailure::DecoderRepairedTap,
                "MFD same-CFA footprint touches a decoder-repaired sample.",
                error);
        }
        if (!Finite(tap.normalizedSample)) {
            return FailSample(
                result,
                SameCfaSampleFailure::NonFiniteTap,
                "MFD same-CFA footprint contains a non-finite sensor sample.",
                error);
        }
        if (!Finite(tap.comparisonGain) || tap.comparisonGain <= 0.0) {
            return FailSample(
                result,
                SameCfaSampleFailure::NonPositiveGain,
                "MFD same-CFA footprint contains a nonpositive comparison gain.",
                error);
        }
        const double gain = parameters.exposureScale * tap.comparisonGain;
        if (!Finite(gain) || gain <= 0.0) {
            return FailSample(
                result,
                SameCfaSampleFailure::NonPositiveGain,
                "MFD same-CFA exposure and pointwise gain overflowed.",
                error);
        }
        GainPropagatedVariance propagated;
        std::string varianceError;
        if (!PropagateKnownGainVariance(
                noiseProfile,
                gain,
                parameters.referenceComparisonPilot,
                usableCodeSpanDn,
                propagated,
                &varianceError)) {
            return FailSample(
                result,
                SameCfaSampleFailure::InvalidVariance,
                "MFD same-CFA tap variance is invalid: " + varianceError,
                error);
        }
        const double comparisonSample = gain * tap.normalizedSample;
        const double coefficient = footprint.coefficients[index];
        value += coefficient * comparisonSample;
        darkVariance += coefficient * coefficient * propagated.darkVariance;
        gradientXPlane +=
            footprint.derivativeXPlane[index] * comparisonSample;
        gradientYPlane +=
            footprint.derivativeYPlane[index] * comparisonSample;
        interpolationTerms.push_back({
            coefficient,
            propagated.variance,
            gain,
            usableCodeSpanDn
        });
    }

    double interpolationVariance = 0.0;
    double effectiveDnStepSquared = 0.0;
    std::string varianceError;
    if (!PropagateInterpolationVariance(
            interpolationTerms,
            interpolationVariance,
            effectiveDnStepSquared,
            &varianceError)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidVariance,
            "MFD same-CFA interpolation variance is invalid: " + varianceError,
            error);
    }
    const RawSignalGradient gradientRaw {
        gradientXPlane * 0.5,
        gradientYPlane * 0.5
    };
    double registrationVariance = 0.0;
    if (!RegistrationUncertaintyVariance(
            gradientRaw,
            parameters.warpCovariance,
            registrationVariance,
            &varianceError)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidVariance,
            "MFD same-CFA registration variance is invalid: " + varianceError,
            error);
    }
    double scaleVariance = 0.0;
    if (!ExposureScaleUncertaintyVariance(
            value,
            parameters.exposureScale,
            parameters.exposureScaleVariance,
            scaleVariance,
            &varianceError)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidVariance,
            "MFD same-CFA exposure variance is invalid: " + varianceError,
            error);
    }
    const double residualVariance = ResidualModelVariance(
        noiseProfile,
        parameters.referenceComparisonPilot);
    EffectiveVariance effective;
    if (!ComposeEffectiveVariance(
            interpolationVariance,
            registrationVariance,
            residualVariance,
            scaleVariance,
            darkVariance,
            effectiveDnStepSquared,
            parameters.numericalVarianceFloor,
            effective,
            &varianceError) ||
        !Finite(value) || !Finite(gradientXPlane) ||
        !Finite(gradientYPlane)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidVariance,
            "MFD same-CFA effective variance or sampled value is invalid: " + varianceError,
            error);
    }

    result.valid = true;
    result.failure = SameCfaSampleFailure::None;
    result.message = "MFD same-CFA scalar sample is valid.";
    result.value = value;
    result.interpolationVariance = interpolationVariance;
    result.registrationVariance = registrationVariance;
    result.exposureScaleVariance = scaleVariance;
    result.residualModelVariance = residualVariance;
    result.gateVariance = effective.gateVariance;
    result.fusionVariance = effective.fusionVariance;
    result.darkVariance = darkVariance;
    result.effectiveDnStep = std::sqrt(effectiveDnStepSquared);
    result.divisionVarianceFloor = effective.divisionVarianceFloor;
    result.gradientPlane = { gradientXPlane, gradientYPlane };
    result.gradientRaw = gradientRaw;
    return true;
}

bool SamplePreparedFrameSameCfaScalar(
    const PreparedRawFrame& frame,
    NormalizedTileCache& tileCache,
    const NoiseModel& noiseModel,
    RawCoordinate sourceRaw,
    CfaSite site,
    const SameCfaScalarParameters& parameters,
    SameCfaSampleResult& result,
    std::string* error) {
    result = {};
    result.site = site;
    result.sourceRaw = sourceRaw;
    if (!ValidPreparedFrameContract(frame) || !KnownSite(site) ||
        !Finite(sourceRaw.x) || !Finite(sourceRaw.y)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidRequest,
            "MFD prepared same-CFA sampler request is invalid.",
            error);
    }
    std::string modelError;
    if (noiseModel.quality == NoiseModelQuality::Unavailable ||
        !ValidateNoiseModel(noiseModel, &modelError) ||
        noiseModel.preparedFrameCacheKey != frame.cacheKey) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidNoiseModel,
            "MFD prepared same-CFA sampler requires a noise model bound to this prepared frame: " + modelError,
            error);
    }
    CfaLayout layout;
    if (!CfaLayout::TryCreate(frame.activeCfaPattern, layout)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidRequest,
            "MFD prepared same-CFA sampler has no valid Bayer layout.",
            error);
    }
    const CfaPlaneCoordinate sourcePlane = layout.RawToPlane(sourceRaw, site);
    result.sourcePlane = sourcePlane;
    KeysBicubicFootprint footprint;
    std::string footprintError;
    if (!BuildKeysBicubicFootprint(sourcePlane, footprint, &footprintError)) {
        return FailSample(
            result,
            SameCfaSampleFailure::InvalidRequest,
            footprintError,
            error);
    }
    const PixelExtent planeExtent = layout.PlaneExtent(site, frame.activeExtent);
    if (!ValidateKeysBicubicFootprint(
            footprint,
            planeExtent,
            &footprintError)) {
        return FailSample(
            result,
            SameCfaSampleFailure::FootprintOutsideActiveArea,
            footprintError,
            error);
    }

    std::array<SameCfaTapInput, kSameCfaTapCount> taps;
    std::vector<CachedPreparedTile> loadedTiles;
    loadedTiles.reserve(4u);
    for (std::size_t index = 0u; index < footprint.taps.size(); ++index) {
        const CfaPlanePixel& planeTap = footprint.taps[index];
        const RawCoordinate rawTap = layout.PlanePixelToRaw(planeTap);
        if (!Finite(rawTap.x) || !Finite(rawTap.y) ||
            rawTap.x < 0.0 || rawTap.y < 0.0 ||
            rawTap.x >= static_cast<double>(frame.activeExtent.width) ||
            rawTap.y >= static_cast<double>(frame.activeExtent.height)) {
            return FailSample(
                result,
                SameCfaSampleFailure::FootprintOutsideActiveArea,
                "MFD same-CFA footprint maps outside the active RAW mosaic.",
                error);
        }
        const auto rawX = static_cast<std::uint64_t>(rawTap.x);
        const auto rawY = static_cast<std::uint64_t>(rawTap.y);
        if (layout.SiteAt(
                static_cast<std::int64_t>(rawX),
                static_cast<std::int64_t>(rawY)) != site ||
            !LoadPreparedTap(
                frame,
                tileCache,
                rawX,
                rawY,
                loadedTiles,
                taps[index],
                result,
                error)) {
            if (result.failure != SameCfaSampleFailure::None) return false;
            return FailSample(
                result,
                SameCfaSampleFailure::InvalidRequest,
                "MFD same-CFA footprint crossed CFA site parity.",
                error);
        }
    }
    const std::size_t siteIndex = SiteIndex(site);
    SameCfaSampleResult evaluated;
    if (!EvaluateSameCfaFootprintScalar(
            footprint,
            taps,
            noiseModel.sites[siteIndex],
            frame.calibration.usableSpanByCfaSite[siteIndex],
            parameters,
            evaluated,
            error)) {
        evaluated.sourceRaw = sourceRaw;
        result = std::move(evaluated);
        return false;
    }
    evaluated.sourceRaw = sourceRaw;
    result = std::move(evaluated);
    return true;
}

} // namespace Raw::Mfd
