#include "Raw/MultiFrameDenoise/NoiseModel.h"

#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <utility>

namespace Raw::Mfd {
namespace {

bool SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool Finite(double value) {
    return std::isfinite(value);
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
        });
}

std::size_t SiteIndex(CfaSite site) {
    switch (site) {
        case CfaSite::Red: return 0;
        case CfaSite::Green0: return 1;
        case CfaSite::Green1: return 2;
        case CfaSite::Blue: return 3;
    }
    return 0;
}

const std::array<CfaSite, 4>& AllSites() {
    static const std::array<CfaSite, 4> sites {
        CfaSite::Red,
        CfaSite::Green0,
        CfaSite::Green1,
        CfaSite::Blue
    };
    return sites;
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return static_cast<char>(character);
    });
    return value;
}

std::string HashCanonicalJson(const nlohmann::json& value) {
    const std::string canonical = value.dump();
    const std::vector<std::uint8_t> bytes(canonical.begin(), canonical.end());
    return Stack::RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

nlohmann::json SiteProfileJson(const SiteNoiseProfile& profile) {
    return {
        { "shotScale", profile.shotScale },
        { "offsetVariance", profile.offsetVariance },
        { "quantizationVariance", profile.quantizationVariance },
        { "residualModelTau0", profile.residualModelTau0 },
        { "residualModelTau1", profile.residualModelTau1 },
        { "quantizationIncluded", profile.quantizationIncluded }
    };
}

void InitializeSiteDiagnostics(NoiseModelDiagnostics& diagnostics) {
    for (CfaSite site : AllSites()) {
        diagnostics.sites[SiteIndex(site)].site = site;
    }
}

void FinalizeModelIdentity(
    const std::string& preparedCacheKey,
    NoiseModel& model) {
    model.preparedFrameCacheKey = preparedCacheKey;
    nlohmann::json sites = nlohmann::json::array();
    for (const SiteNoiseProfile& profile : model.sites) {
        sites.push_back(SiteProfileJson(profile));
    }
    model.identitySha256 = HashCanonicalJson({
        { "contractId", model.contractId },
        { "contractVersion", model.contractVersion },
        { "preparedCacheKey", model.preparedFrameCacheKey },
        { "quality", NoiseModelQualityName(model.quality) },
        { "source", model.diagnostics.source },
        { "sourceRecordId", model.diagnostics.sourceRecordId },
        { "sites", std::move(sites) }
    });
}

bool ApplyQuantizationPolicy(
    const PreparedRawFrame& preparedFrame,
    bool sourceIncludesQuantization,
    std::array<SiteNoiseProfile, 4>& sites,
    std::array<NoiseSiteDiagnostics, 4>& diagnostics,
    std::string* error) {
    for (CfaSite site : AllSites()) {
        const std::size_t index = SiteIndex(site);
        const double quantization = QuantizationVariance(
            preparedFrame.calibration.usableSpanByCfaSite[index]);
        if (!Finite(quantization) || quantization <= 0.0) {
            return SetError(error, "MFD noise model could not resolve a positive quantization variance.");
        }
        sites[index].quantizationVariance = quantization;
        if (!sourceIncludesQuantization) {
            sites[index].offsetVariance += quantization;
            diagnostics[index].quantizationAdded = true;
        }
        sites[index].quantizationIncluded = true;
        if (!ValidateSiteNoiseProfile(sites[index], error)) return false;
    }
    return true;
}

bool ResolveDngMetadataModel(
    const RawMetadata& metadata,
    const PreparedRawFrame& preparedFrame,
    const NoiseResolutionOptions& options,
    NoiseModel& model,
    std::string* error) {
    if (!metadata.hasDngNoiseProfile) {
        return SetError(error, "DNG NoiseProfile is unavailable.");
    }
    if (metadata.dngNoiseProfile.size() != 1u &&
        metadata.dngNoiseProfile.size() != 3u) {
        return SetError(error, "DNG NoiseProfile must provide one shared plane or three mapped color planes.");
    }

    std::array<DngNoiseProfilePlane, 3> rgb {};
    if (metadata.dngNoiseProfile.size() == 1u) {
        rgb.fill(metadata.dngNoiseProfile.front());
    } else {
        std::array<bool, 3> found { false, false, false };
        for (std::size_t plane = 0; plane < 3u; ++plane) {
            const int color = metadata.dngCfaPlaneColor[plane];
            if (color < 0 || color >= 3 || found[static_cast<std::size_t>(color)]) {
                return SetError(error, "DNG NoiseProfile color-plane mapping is invalid or duplicated.");
            }
            rgb[static_cast<std::size_t>(color)] =
                metadata.dngNoiseProfile[plane];
            found[static_cast<std::size_t>(color)] = true;
        }
        if (!std::all_of(found.begin(), found.end(), [](bool value) { return value; })) {
            return SetError(error, "DNG NoiseProfile does not map all RGB sensor colors.");
        }
    }

    const auto convert = [](const DngNoiseProfilePlane& source) {
        SiteNoiseProfile profile;
        profile.shotScale = source.shotScale;
        profile.offsetVariance = source.readNoiseVariance;
        return profile;
    };
    model = {};
    model.quality = NoiseModelQuality::TrustedMetadata;
    model.sites[0] = convert(rgb[0]);
    model.sites[1] = convert(rgb[1]);
    model.sites[2] = convert(rgb[1]);
    model.sites[3] = convert(rgb[2]);
    model.diagnostics.quality = model.quality;
    model.diagnostics.source = "DNG NoiseProfile";
    model.diagnostics.sourceRecordId = "embedded-frame-metadata";
    model.diagnostics.greenSitesIndependent = false;
    model.diagnostics.correlatedNoiseUnmodeled = true;
    InitializeSiteDiagnostics(model.diagnostics);
    model.diagnostics.sites[1].duplicatedFromSharedGreen = true;
    model.diagnostics.sites[2].duplicatedFromSharedGreen = true;
    if (!ApplyQuantizationPolicy(
            preparedFrame,
            options.metadataProfileIncludesQuantization,
            model.sites,
            model.diagnostics.sites,
            error)) {
        return false;
    }
    for (NoiseSiteDiagnostics& site : model.diagnostics.sites) {
        site.valid = true;
        site.reason = "trusted-metadata";
    }
    model.diagnostics.resolved = true;
    model.diagnostics.referenceOnly = false;
    model.diagnostics.warnings.push_back(
        "DNG metadata does not distinguish G0 and G1; the shared green profile is duplicated explicitly.");
    FinalizeModelIdentity(preparedFrame.cacheKey, model);
    return ValidateNoiseModel(model, error);
}

bool ParseCfaPattern(const std::string& value, CfaPattern& pattern) {
    const std::string lower = LowerAscii(value);
    if (lower.empty() || lower == "any" || lower == "unknown") {
        pattern = CfaPattern::Unknown;
        return true;
    }
    if (lower == "rggb") pattern = CfaPattern::RGGB;
    else if (lower == "bggr") pattern = CfaPattern::BGGR;
    else if (lower == "gbrg") pattern = CfaPattern::GBRG;
    else if (lower == "grbg") pattern = CfaPattern::GRBG;
    else return false;
    return true;
}

bool ReadFiniteNumber(
    const nlohmann::json& object,
    const char* key,
    double& value,
    bool required,
    double fallback = 0.0) {
    const auto found = object.find(key);
    if (found == object.end()) {
        if (required) return false;
        value = fallback;
        return true;
    }
    try {
        value = found->get<double>();
    } catch (...) {
        return false;
    }
    return Finite(value);
}

bool ReadCalibrationSite(
    const nlohmann::json& object,
    bool quantizationIncluded,
    SiteNoiseProfile& profile,
    std::string* error) {
    if (!object.is_object() ||
        !ReadFiniteNumber(object, "shotScale", profile.shotScale, true) ||
        !ReadFiniteNumber(object, "offsetVariance", profile.offsetVariance, true) ||
        !ReadFiniteNumber(object, "residualModelTau0", profile.residualModelTau0, false) ||
        !ReadFiniteNumber(object, "residualModelTau1", profile.residualModelTau1, false)) {
        return SetError(error, "MFD calibration site coefficients are missing or invalid.");
    }
    profile.quantizationIncluded = quantizationIncluded;
    if (!ValidateSiteNoiseProfile(profile, error)) return false;
    return true;
}

bool CalibrationRecordMatches(
    const NoiseCalibrationRecord& record,
    const RawMetadata& metadata,
    const NoiseResolutionOptions& options) {
    const NoiseCalibrationKey& key = record.key;
    if (LowerAscii(key.cameraMake) != LowerAscii(metadata.cameraMake) ||
        LowerAscii(key.cameraModel) != LowerAscii(metadata.cameraModel)) {
        return false;
    }
    if (!key.uniqueCameraModel.empty() &&
        LowerAscii(key.uniqueCameraModel) != LowerAscii(metadata.dngUniqueCameraModel)) {
        return false;
    }
    if (!key.cameraMode.empty() &&
        LowerAscii(key.cameraMode) != LowerAscii(options.cameraMode)) {
        return false;
    }
    if (key.bitDepth > 0 && key.bitDepth != metadata.bitDepth) return false;
    if (key.cfaPattern != CfaPattern::Unknown &&
        key.cfaPattern != metadata.cfaPattern) return false;
    if (key.isoMaximum > 0.0) {
        if (!metadata.hasIsoSpeed || !Finite(metadata.isoSpeed) ||
            metadata.isoSpeed < key.isoMinimum ||
            metadata.isoSpeed > key.isoMaximum) {
            return false;
        }
    }
    if (key.hasTemperatureRange) {
        if (!options.hasSensorTemperature ||
            options.sensorTemperatureC < key.temperatureMinimumC ||
            options.sensorTemperatureC > key.temperatureMaximumC) {
            return false;
        }
    }
    return true;
}

const NoiseCalibrationRecord* SelectCalibrationRecord(
    const NoiseCalibrationLibrary& library,
    const RawMetadata& metadata,
    const NoiseResolutionOptions& options) {
    const NoiseCalibrationRecord* best = nullptr;
    double bestIsoWidth = std::numeric_limits<double>::infinity();
    int bestSpecificity = -1;
    for (const NoiseCalibrationRecord& record : library.records) {
        if (!CalibrationRecordMatches(record, metadata, options)) continue;
        const NoiseCalibrationKey& key = record.key;
        int specificity = 0;
        specificity += !key.uniqueCameraModel.empty() ? 1 : 0;
        specificity += !key.cameraMode.empty() ? 1 : 0;
        specificity += key.bitDepth > 0 ? 1 : 0;
        specificity += key.cfaPattern != CfaPattern::Unknown ? 1 : 0;
        specificity += key.hasTemperatureRange ? 1 : 0;
        const double isoWidth = key.isoMaximum > 0.0
            ? key.isoMaximum - key.isoMinimum
            : std::numeric_limits<double>::infinity();
        if (!best || specificity > bestSpecificity ||
            (specificity == bestSpecificity && isoWidth < bestIsoWidth) ||
            (specificity == bestSpecificity && isoWidth == bestIsoWidth &&
                record.recordId < best->recordId)) {
            best = &record;
            bestSpecificity = specificity;
            bestIsoWidth = isoWidth;
        }
    }
    return best;
}

bool ResolveCalibrationModel(
    const NoiseCalibrationRecord& record,
    const PreparedRawFrame& preparedFrame,
    NoiseModel& model,
    std::string* error) {
    model = {};
    model.quality = NoiseModelQuality::CalibratedCamera;
    model.sites = record.sites;
    model.diagnostics.quality = model.quality;
    model.diagnostics.source = "offline flat/dark calibration";
    model.diagnostics.sourceRecordId = record.recordId;
    model.diagnostics.greenSitesIndependent = true;
    model.diagnostics.correlatedNoiseUnmodeled = true;
    InitializeSiteDiagnostics(model.diagnostics);
    for (CfaSite site : AllSites()) {
        const std::size_t index = SiteIndex(site);
        const bool sourceIncludesQuantization =
            model.sites[index].quantizationIncluded;
        const double quantization = QuantizationVariance(
            preparedFrame.calibration.usableSpanByCfaSite[index]);
        model.sites[index].quantizationVariance = quantization;
        if (!sourceIncludesQuantization) {
            model.sites[index].offsetVariance += quantization;
            model.diagnostics.sites[index].quantizationAdded = true;
        }
        model.sites[index].quantizationIncluded = true;
        if (!ValidateSiteNoiseProfile(model.sites[index], error)) return false;
        model.diagnostics.sites[index].valid = true;
        model.diagnostics.sites[index].reason = "calibrated-camera";
    }
    model.diagnostics.resolved = true;
    model.diagnostics.referenceOnly = false;
    FinalizeModelIdentity(preparedFrame.cacheKey, model);
    return ValidateNoiseModel(model, error);
}

double Quantile(std::vector<double> values, double quantile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(quantile, 0.0, 1.0) *
        static_cast<double>(values.size() - 1u);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = std::min(low + 1u, values.size() - 1u);
    const double t = position - static_cast<double>(low);
    return values[low] * (1.0 - t) + values[high] * t;
}

double MedianAbsoluteDeviationVariance(std::vector<double> residuals) {
    if (residuals.empty()) return 0.0;
    const double median = Quantile(residuals, 0.5);
    for (double& value : residuals) value = std::abs(value - median);
    const double mad = Quantile(std::move(residuals), 0.5);
    const double sigma = 1.482602218505602 * mad / std::sqrt(1.25);
    return sigma * sigma;
}

struct BlockNoiseSample {
    double mean = 0.0;
    double variance = 0.0;
};

bool SampleUsable(std::uint8_t flags) {
    return !HasSampleFlag(flags, PreparedSampleFlag::Saturated) &&
        !HasSampleFlag(flags, PreparedSampleFlag::Defective) &&
        !HasSampleFlag(flags, PreparedSampleFlag::DecoderRepaired) &&
        !HasSampleFlag(flags, PreparedSampleFlag::ExplicitDecoderClip);
}

bool ValidBurstOptions(const BurstNoiseEstimationOptions& options) {
    return options.blockRawPixels >= 8u &&
        (options.blockRawPixels % 2u) == 0u &&
        options.signalBinCount >= 3u &&
        options.minimumBlocksPerSite >= options.minimumPopulatedBins &&
        options.minimumBlocksPerBin > 0u &&
        options.minimumPopulatedBins >= 2u &&
        options.minimumPopulatedBins <= options.signalBinCount &&
        Finite(options.lowerEnvelopeQuantile) &&
        options.lowerEnvelopeQuantile >= 0.0 &&
        options.lowerEnvelopeQuantile <= 0.5 &&
        Finite(options.minimumSignal) &&
        Finite(options.maximumSignal) &&
        options.minimumSignal < options.maximumSignal &&
        Finite(options.minimumSignalCoverage) &&
        options.minimumSignalCoverage > 0.0 &&
        Finite(options.maximumGreenRelativeDisagreement) &&
        options.maximumGreenRelativeDisagreement >= 0.0 &&
        Finite(options.gradientAbsoluteLimit) &&
        options.gradientAbsoluteLimit > 0.0 &&
        Finite(options.gradientNoiseSigmaMultiplier) &&
        options.gradientNoiseSigmaMultiplier > 0.0;
}

bool ValidatePreparedNoiseInput(
    const PreparedRawFrame& frame,
    std::string* error) {
    CfaLayout layout;
    if (frame.contractVersion != kPreparationContractVersion ||
        frame.contractId != kPreparationContractId ||
        !LooksLikeSha256(frame.cacheKey) ||
        frame.activeExtent.width == 0u || frame.activeExtent.height == 0u ||
        frame.tileRawPixels == 0u || frame.tileColumns == 0u ||
        frame.tileRows == 0u ||
        !CfaLayout::TryCreate(frame.activeCfaPattern, layout)) {
        return SetError(error, "MFD noise resolution requires a valid prepared Bayer-frame contract.");
    }
    for (double usableSpan : frame.calibration.usableSpanByCfaSite) {
        if (!Finite(usableSpan) || usableSpan <= 0.0) {
            return SetError(error, "MFD noise resolution requires a positive usable DN span for every CFA site.");
        }
    }
    return true;
}

} // namespace

const char* NoiseModelQualityName(NoiseModelQuality quality) {
    switch (quality) {
        case NoiseModelQuality::TrustedMetadata: return "trusted-metadata";
        case NoiseModelQuality::CalibratedCamera: return "calibrated-camera";
        case NoiseModelQuality::EstimatedBurst: return "estimated-burst";
        case NoiseModelQuality::GenericLowConfidence: return "generic-low-confidence";
        case NoiseModelQuality::Unavailable: return "unavailable";
    }
    return "unavailable";
}

bool ValidateSiteNoiseProfile(
    const SiteNoiseProfile& profile,
    std::string* error) {
    if (!Finite(profile.shotScale) || profile.shotScale < 0.0 ||
        !Finite(profile.offsetVariance) || profile.offsetVariance < 0.0 ||
        !Finite(profile.quantizationVariance) || profile.quantizationVariance < 0.0 ||
        !Finite(profile.residualModelTau0) || profile.residualModelTau0 < 0.0 ||
        !Finite(profile.residualModelTau1) || profile.residualModelTau1 < 0.0 ||
        (profile.shotScale == 0.0 && profile.offsetVariance == 0.0)) {
        return SetError(error, "MFD noise coefficients must be finite, nonnegative, and nonzero as a model.");
    }
    if (profile.quantizationIncluded &&
        profile.offsetVariance + 1.0e-18 < profile.quantizationVariance) {
        return SetError(error, "MFD offset variance is below its included quantization variance.");
    }
    return true;
}

bool ValidateNoiseModel(
    const NoiseModel& model,
    std::string* error) {
    if (model.contractVersion != kNoiseModelContractVersion ||
        model.contractId != kNoiseModelContractId) {
        return SetError(error, "MFD noise-model identity is unsupported.");
    }
    if (model.quality == NoiseModelQuality::Unavailable) {
        return model.diagnostics.referenceOnly;
    }
    for (const SiteNoiseProfile& site : model.sites) {
        if (!ValidateSiteNoiseProfile(site, error) || !site.quantizationIncluded) {
            return SetError(error, "MFD resolved noise model is incomplete.");
        }
    }
    if (!LooksLikeSha256(model.preparedFrameCacheKey) ||
        !LooksLikeSha256(model.identitySha256) ||
        !model.diagnostics.resolved || model.diagnostics.referenceOnly ||
        model.diagnostics.quality != model.quality) {
        return SetError(error, "MFD resolved noise-model diagnostics or identity are incomplete.");
    }
    return true;
}

double NoiseModelConfidence(
    NoiseModelQuality quality,
    const Parameters& parameters) {
    const std::size_t index = static_cast<std::size_t>(quality);
    if (index >= parameters.reliability.noiseModelConfidence.size()) return 0.0;
    return parameters.reliability.noiseModelConfidence[index];
}

double NonnegativeShotPilot(double referenceComparisonSample) {
    return Finite(referenceComparisonSample)
        ? std::max(0.0, referenceComparisonSample)
        : std::numeric_limits<double>::quiet_NaN();
}

double QuantizationVariance(double usableCodeSpanDn) {
    if (!Finite(usableCodeSpanDn) || usableCodeSpanDn <= 0.0) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return 1.0 / (12.0 * usableCodeSpanDn * usableCodeSpanDn);
}

bool PropagateKnownGainVariance(
    const SiteNoiseProfile& profile,
    double comparisonGain,
    double commonComparisonDomainPilot,
    double usableCodeSpanDn,
    GainPropagatedVariance& result,
    std::string* error) {
    if (!ValidateSiteNoiseProfile(profile, error) ||
        !Finite(comparisonGain) || comparisonGain <= 0.0 ||
        !Finite(commonComparisonDomainPilot) ||
        !Finite(usableCodeSpanDn) || usableCodeSpanDn <= 0.0) {
        return SetError(error, "MFD gain-variance propagation received invalid inputs.");
    }
    const double pilot = NonnegativeShotPilot(commonComparisonDomainPilot);
    result.shotCoefficient = comparisonGain * profile.shotScale;
    result.offsetVariance =
        comparisonGain * comparisonGain * profile.offsetVariance;
    result.variance = result.shotCoefficient * pilot + result.offsetVariance;
    result.darkVariance = result.offsetVariance;
    result.effectiveDnStep = comparisonGain / usableCodeSpanDn;
    if (!Finite(result.variance) || result.variance < 0.0 ||
        !Finite(result.effectiveDnStep) || result.effectiveDnStep <= 0.0) {
        return SetError(error, "MFD gain-variance propagation overflowed or became negative.");
    }
    return true;
}

bool PropagateInterpolationVariance(
    const std::vector<InterpolationVarianceTerm>& terms,
    double& variance,
    double& effectiveDnStepSquared,
    std::string* error) {
    if (terms.empty()) {
        return SetError(error, "MFD interpolation variance requires at least one source term.");
    }
    variance = 0.0;
    effectiveDnStepSquared = 0.0;
    for (const InterpolationVarianceTerm& term : terms) {
        if (!Finite(term.coefficient) || !Finite(term.sampleVariance) ||
            term.sampleVariance < 0.0 || !Finite(term.comparisonGain) ||
            term.comparisonGain <= 0.0 || !Finite(term.usableCodeSpanDn) ||
            term.usableCodeSpanDn <= 0.0) {
            return SetError(error, "MFD interpolation variance contains an invalid source term.");
        }
        const double coefficientSquared = term.coefficient * term.coefficient;
        const double dnStep = term.comparisonGain / term.usableCodeSpanDn;
        variance += coefficientSquared * term.sampleVariance;
        effectiveDnStepSquared += coefficientSquared * dnStep * dnStep;
    }
    if (!Finite(variance) || variance < 0.0 ||
        !Finite(effectiveDnStepSquared) || effectiveDnStepSquared <= 0.0) {
        return SetError(error, "MFD interpolation variance overflowed or has no quantization support.");
    }
    return true;
}

bool RegistrationUncertaintyVariance(
    RawSignalGradient gradient,
    SymmetricRawCovariance covariance,
    double& variance,
    std::string* error) {
    if (!Finite(gradient.dxPerRawPixel) || !Finite(gradient.dyPerRawPixel) ||
        !Finite(covariance.xxRawPixelsSquared) ||
        !Finite(covariance.xyRawPixelsSquared) ||
        !Finite(covariance.yyRawPixelsSquared)) {
        return SetError(error, "MFD registration variance received non-finite inputs.");
    }
    constexpr double tolerance = 1.0e-12;
    if (covariance.xxRawPixelsSquared < -tolerance ||
        covariance.yyRawPixelsSquared < -tolerance) {
        return SetError(error, "MFD registration covariance has a materially negative diagonal.");
    }
    covariance.xxRawPixelsSquared = std::max(0.0, covariance.xxRawPixelsSquared);
    covariance.yyRawPixelsSquared = std::max(0.0, covariance.yyRawPixelsSquared);
    const double determinant =
        covariance.xxRawPixelsSquared * covariance.yyRawPixelsSquared -
        covariance.xyRawPixelsSquared * covariance.xyRawPixelsSquared;
    const double determinantTolerance = tolerance * std::max(
        1.0,
        covariance.xxRawPixelsSquared * covariance.yyRawPixelsSquared);
    if (determinant < -determinantTolerance) {
        return SetError(error, "MFD registration covariance is not positive semidefinite.");
    }
    if (determinant < 0.0) {
        covariance.xyRawPixelsSquared = std::copysign(
            std::sqrt(covariance.xxRawPixelsSquared *
                covariance.yyRawPixelsSquared),
            covariance.xyRawPixelsSquared);
    }
    variance =
        gradient.dxPerRawPixel * gradient.dxPerRawPixel *
            covariance.xxRawPixelsSquared +
        2.0 * gradient.dxPerRawPixel * gradient.dyPerRawPixel *
            covariance.xyRawPixelsSquared +
        gradient.dyPerRawPixel * gradient.dyPerRawPixel *
            covariance.yyRawPixelsSquared;
    if (!Finite(variance) || variance < -tolerance) {
        return SetError(error, "MFD registration uncertainty produced an invalid variance.");
    }
    variance = std::max(0.0, variance);
    return true;
}

bool ExposureScaleUncertaintyVariance(
    double interpolatedComparisonSample,
    double exposureScale,
    double exposureScaleVariance,
    double& variance,
    std::string* error) {
    if (!Finite(interpolatedComparisonSample) || !Finite(exposureScale) ||
        exposureScale <= 0.0 || !Finite(exposureScaleVariance) ||
        exposureScaleVariance < 0.0) {
        return SetError(error, "MFD exposure-scale uncertainty inputs are invalid.");
    }
    variance = exposureScaleVariance /
        (exposureScale * exposureScale) *
        interpolatedComparisonSample * interpolatedComparisonSample;
    return Finite(variance) && variance >= 0.0
        ? true
        : SetError(error, "MFD exposure-scale uncertainty overflowed.");
}

double ResidualModelVariance(
    const SiteNoiseProfile& profile,
    double commonComparisonDomainPilot) {
    if (!Finite(commonComparisonDomainPilot) ||
        !Finite(profile.residualModelTau0) ||
        !Finite(profile.residualModelTau1)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const double pilot = NonnegativeShotPilot(commonComparisonDomainPilot);
    return profile.residualModelTau0 * profile.residualModelTau0 +
        profile.residualModelTau1 * profile.residualModelTau1 * pilot * pilot;
}

bool ComposeEffectiveVariance(
    double interpolatedSampleVariance,
    double registrationVariance,
    double residualModelVariance,
    double exposureScaleVariance,
    double darkVariance,
    double effectiveDnStepSquared,
    double numericalVarianceFloor,
    EffectiveVariance& result,
    std::string* error) {
    const std::array<double, 7> values {
        interpolatedSampleVariance,
        registrationVariance,
        residualModelVariance,
        exposureScaleVariance,
        darkVariance,
        effectiveDnStepSquared,
        numericalVarianceFloor
    };
    if (!std::all_of(values.begin(), values.end(), [](double value) {
            return Finite(value) && value >= 0.0;
        }) || numericalVarianceFloor <= 0.0 || effectiveDnStepSquared <= 0.0) {
        return SetError(error, "MFD effective-variance components are invalid.");
    }
    result.gateVariance = interpolatedSampleVariance + registrationVariance +
        residualModelVariance + numericalVarianceFloor;
    result.fusionVariance = result.gateVariance + exposureScaleVariance;
    result.darkVariance = darkVariance;
    result.divisionVarianceFloor = std::max(
        numericalVarianceFloor,
        effectiveDnStepSquared / 12.0);
    if (!Finite(result.gateVariance) || !Finite(result.fusionVariance) ||
        result.gateVariance <= 0.0 ||
        result.fusionVariance < result.gateVariance) {
        return SetError(error, "MFD effective variance overflowed or violated gate/fusion ordering.");
    }
    return true;
}

bool FitPoissonGaussianProfile(
    const std::vector<NoiseCalibrationObservation>& observations,
    double minimumOffsetVariance,
    SiteNoiseProfile& profile,
    NoiseFitDiagnostics& diagnostics,
    std::string* error) {
    diagnostics = {};
    if (observations.size() < 2u || !Finite(minimumOffsetVariance) ||
        minimumOffsetVariance < 0.0) {
        return SetError(error, "MFD noise fit needs at least two observations and a valid variance floor.");
    }
    double sumWeight = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    diagnostics.signalMinimum = std::numeric_limits<double>::infinity();
    diagnostics.signalMaximum = -std::numeric_limits<double>::infinity();
    for (const NoiseCalibrationObservation& observation : observations) {
        if (!Finite(observation.meanSignal) || observation.meanSignal < 0.0 ||
            !Finite(observation.variance) || observation.variance < 0.0 ||
            !Finite(observation.weight) || observation.weight <= 0.0) {
            return SetError(error, "MFD noise fit observation is invalid.");
        }
        sumWeight += observation.weight;
        sumX += observation.weight * observation.meanSignal;
        sumY += observation.weight * observation.variance;
        sumXX += observation.weight * observation.meanSignal * observation.meanSignal;
        sumXY += observation.weight * observation.meanSignal * observation.variance;
        diagnostics.signalMinimum = std::min(
            diagnostics.signalMinimum, observation.meanSignal);
        diagnostics.signalMaximum = std::max(
            diagnostics.signalMaximum, observation.meanSignal);
    }
    const double denominator = sumWeight * sumXX - sumX * sumX;
    if (!Finite(denominator) || denominator <= 1.0e-18) {
        return SetError(error, "MFD noise fit has insufficient signal-level diversity.");
    }
    double shot = (sumWeight * sumXY - sumX * sumY) / denominator;
    double offset = (sumY - shot * sumX) / sumWeight;
    if (shot < 0.0) {
        shot = 0.0;
        offset = sumY / sumWeight;
        diagnostics.constrainedShotScale = true;
    }
    if (offset < minimumOffsetVariance) {
        offset = minimumOffsetVariance;
        double numerator = 0.0;
        double slopeDenominator = 0.0;
        for (const NoiseCalibrationObservation& observation : observations) {
            numerator += observation.weight * observation.meanSignal *
                (observation.variance - offset);
            slopeDenominator += observation.weight * observation.meanSignal *
                observation.meanSignal;
        }
        shot = slopeDenominator > 0.0
            ? std::max(0.0, numerator / slopeDenominator)
            : 0.0;
        diagnostics.constrainedOffsetVariance = true;
    }
    double squaredError = 0.0;
    for (const NoiseCalibrationObservation& observation : observations) {
        const double residual = observation.variance -
            (shot * observation.meanSignal + offset);
        squaredError += observation.weight * residual * residual;
    }
    diagnostics.observationCount = observations.size();
    diagnostics.rootMeanSquareError = std::sqrt(
        squaredError / std::max(sumWeight, 1.0e-18));
    profile = {};
    profile.shotScale = shot;
    profile.offsetVariance = offset;
    profile.quantizationVariance = minimumOffsetVariance;
    profile.quantizationIncluded = true;
    return ValidateSiteNoiseProfile(profile, error);
}

bool LoadNoiseCalibrationLibrary(
    const std::filesystem::path& path,
    NoiseCalibrationLibrary& library,
    std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return SetError(error, "MFD noise calibration file could not be opened.");
    }
    nlohmann::json root;
    try {
        input >> root;
    } catch (...) {
        return SetError(error, "MFD noise calibration file is not valid JSON.");
    }
    if (!root.is_object() ||
        root.value("schemaVersion", 0u) != kNoiseCalibrationSchemaVersion ||
        !root.contains("records") || !root["records"].is_array()) {
        return SetError(error, "MFD noise calibration schema is missing or unsupported.");
    }

    NoiseCalibrationLibrary parsed;
    std::set<std::string> recordIds;
    for (const nlohmann::json& value : root["records"]) {
        if (!value.is_object()) {
            return SetError(error, "MFD noise calibration record must be an object.");
        }
        NoiseCalibrationRecord record;
        try {
            record.recordId = value.at("recordId").get<std::string>();
            record.key.cameraMake = value.at("cameraMake").get<std::string>();
            record.key.cameraModel = value.at("cameraModel").get<std::string>();
            record.key.uniqueCameraModel = value.value("uniqueCameraModel", std::string());
            record.key.cameraMode = value.value("cameraMode", std::string());
            record.key.bitDepth = value.value("bitDepth", 0);
            record.key.isoMinimum = value.value("isoMinimum", 0.0);
            record.key.isoMaximum = value.value("isoMaximum", 0.0);
        } catch (...) {
            return SetError(error, "MFD noise calibration record identity is missing or invalid.");
        }
        if (record.recordId.empty() || record.key.cameraMake.empty() ||
            record.key.cameraModel.empty() || !recordIds.insert(record.recordId).second ||
            record.key.bitDepth < 0 || !Finite(record.key.isoMinimum) ||
            !Finite(record.key.isoMaximum) || record.key.isoMinimum < 0.0 ||
            record.key.isoMaximum < record.key.isoMinimum ||
            ((record.key.isoMinimum > 0.0) != (record.key.isoMaximum > 0.0))) {
            return SetError(error, "MFD noise calibration record identity or ISO range is invalid.");
        }
        std::string cfaPattern;
        try {
            cfaPattern = value.value("cfaPattern", std::string("any"));
        } catch (...) {
            return SetError(error, "MFD noise calibration CFA pattern is invalid.");
        }
        if (!ParseCfaPattern(cfaPattern, record.key.cfaPattern)) {
            return SetError(error, "MFD noise calibration CFA pattern is unsupported.");
        }
        const auto temperature = value.find("temperatureRangeC");
        if (temperature != value.end()) {
            if (!temperature->is_array() || temperature->size() != 2u) {
                return SetError(error, "MFD noise calibration temperature range is malformed.");
            }
            try {
                record.key.temperatureMinimumC = (*temperature)[0].get<double>();
                record.key.temperatureMaximumC = (*temperature)[1].get<double>();
            } catch (...) {
                return SetError(error, "MFD noise calibration temperature range is invalid.");
            }
            if (!Finite(record.key.temperatureMinimumC) ||
                !Finite(record.key.temperatureMaximumC) ||
                record.key.temperatureMinimumC > record.key.temperatureMaximumC) {
                return SetError(error, "MFD noise calibration temperature range is invalid.");
            }
            record.key.hasTemperatureRange = true;
        }
        bool quantizationIncluded = false;
        try {
            quantizationIncluded = value.at("quantizationIncluded").get<bool>();
        } catch (...) {
            return SetError(error, "MFD noise calibration must declare whether quantization was fitted.");
        }
        const auto sites = value.find("sites");
        if (sites == value.end() || !sites->is_object()) {
            return SetError(error, "MFD noise calibration site profiles are missing.");
        }
        static const std::array<const char*, 4> names { "R", "G0", "G1", "B" };
        for (std::size_t site = 0; site < names.size(); ++site) {
            const auto coefficients = sites->find(names[site]);
            if (coefficients == sites->end() ||
                !ReadCalibrationSite(
                    *coefficients,
                    quantizationIncluded,
                    record.sites[site],
                    error)) {
                if (error && error->empty()) {
                    *error = std::string("MFD noise calibration is missing site ") + names[site] + ".";
                }
                return false;
            }
        }
        parsed.records.push_back(std::move(record));
    }
    if (parsed.records.empty()) {
        return SetError(error, "MFD noise calibration library contains no records.");
    }
    library = std::move(parsed);
    return true;
}

bool EstimateNoiseModelFromPreparedFrame(
    const PreparedRawFrame& preparedFrame,
    NormalizedTileCache& preparedTileCache,
    const BurstNoiseEstimationOptions& options,
    NoiseModel& model,
    std::string* error) {
    if (!ValidBurstOptions(options)) {
        return SetError(error, "MFD burst noise-estimation options are invalid.");
    }
    if (!ValidatePreparedNoiseInput(preparedFrame, error)) return false;
    CfaLayout layout;
    if (!CfaLayout::TryCreate(preparedFrame.activeCfaPattern, layout)) {
        return SetError(error, "MFD burst noise estimation requires a valid Bayer layout.");
    }

    std::array<std::vector<BlockNoiseSample>, 4> blockSamples;
    std::array<std::uint64_t, 4> candidateBlocks {};
    const std::uint64_t blockSize = options.blockRawPixels;
    for (std::uint32_t tileY = 0; tileY < preparedFrame.tileRows; ++tileY) {
        for (std::uint32_t tileX = 0; tileX < preparedFrame.tileColumns; ++tileX) {
            PreparedRawTile tile;
            std::string tileError;
            if (ReadPreparedTile(
                    preparedFrame,
                    preparedTileCache,
                    tileX,
                    tileY,
                    tile,
                    &tileError) != TileCacheReadStatus::Hit) {
                return SetError(error, "MFD burst noise estimation could not read a prepared tile: " + tileError);
            }
            const std::uint64_t completeBlocksX = tile.extent.width / blockSize;
            const std::uint64_t completeBlocksY = tile.extent.height / blockSize;
            for (std::uint64_t blockY = 0; blockY < completeBlocksY; ++blockY) {
                for (std::uint64_t blockX = 0; blockX < completeBlocksX; ++blockX) {
                    const std::uint64_t originX = blockX * blockSize;
                    const std::uint64_t originY = blockY * blockSize;
                    for (CfaSite site : AllSites()) {
                        const std::size_t siteIndex = SiteIndex(site);
                        ++candidateBlocks[siteIndex];
                        std::vector<double> residuals;
                        residuals.reserve(static_cast<std::size_t>(blockSize * blockSize / 4u));
                        double sum = 0.0;
                        std::uint64_t count = 0;
                        double leftSum = 0.0;
                        double rightSum = 0.0;
                        double topSum = 0.0;
                        double bottomSum = 0.0;
                        std::uint64_t leftCount = 0;
                        std::uint64_t rightCount = 0;
                        std::uint64_t topCount = 0;
                        std::uint64_t bottomCount = 0;
                        for (std::uint64_t localY = 0; localY < blockSize; ++localY) {
                            for (std::uint64_t localX = 0; localX < blockSize; ++localX) {
                                const std::uint64_t x = originX + localX;
                                const std::uint64_t y = originY + localY;
                                const std::uint64_t activeX = tile.originX + x;
                                const std::uint64_t activeY = tile.originY + y;
                                if (layout.SiteAt(
                                        static_cast<std::int64_t>(activeX),
                                        static_cast<std::int64_t>(activeY)) != site) {
                                    continue;
                                }
                                const std::size_t index = static_cast<std::size_t>(
                                    y * tile.extent.width + x);
                                if (!SampleUsable(tile.sampleFlags[index])) continue;
                                const double value = tile.normalizedMosaic[index];
                                if (!Finite(value)) continue;
                                sum += value;
                                ++count;
                                if (localX < blockSize / 2u) {
                                    leftSum += value;
                                    ++leftCount;
                                } else {
                                    rightSum += value;
                                    ++rightCount;
                                }
                                if (localY < blockSize / 2u) {
                                    topSum += value;
                                    ++topCount;
                                } else {
                                    bottomSum += value;
                                    ++bottomCount;
                                }
                                if (localX < 2u || localY < 2u ||
                                    localX + 2u >= blockSize ||
                                    localY + 2u >= blockSize) {
                                    continue;
                                }
                                const std::array<std::size_t, 4> neighbors {
                                    static_cast<std::size_t>(y * tile.extent.width + (x - 2u)),
                                    static_cast<std::size_t>(y * tile.extent.width + (x + 2u)),
                                    static_cast<std::size_t>((y - 2u) * tile.extent.width + x),
                                    static_cast<std::size_t>((y + 2u) * tile.extent.width + x)
                                };
                                if (!std::all_of(neighbors.begin(), neighbors.end(),
                                        [&](std::size_t neighbor) {
                                            return SampleUsable(tile.sampleFlags[neighbor]) &&
                                                Finite(tile.normalizedMosaic[neighbor]);
                                        })) {
                                    continue;
                                }
                                const double neighborMean = 0.25 * (
                                    tile.normalizedMosaic[neighbors[0]] +
                                    tile.normalizedMosaic[neighbors[1]] +
                                    tile.normalizedMosaic[neighbors[2]] +
                                    tile.normalizedMosaic[neighbors[3]]);
                                residuals.push_back(value - neighborMean);
                            }
                        }
                        const std::uint64_t expectedSiteSamples =
                            blockSize * blockSize / 4u;
                        if (count < expectedSiteSamples * 3u / 4u ||
                            residuals.size() < 32u || leftCount == 0u ||
                            rightCount == 0u || topCount == 0u ||
                            bottomCount == 0u) {
                            continue;
                        }
                        const double mean = sum / static_cast<double>(count);
                        if (mean < options.minimumSignal ||
                            mean > options.maximumSignal) {
                            continue;
                        }
                        const double variance =
                            MedianAbsoluteDeviationVariance(std::move(residuals));
                        if (!Finite(variance) || variance <= 0.0) continue;
                        const double horizontalGradient = std::abs(
                            leftSum / static_cast<double>(leftCount) -
                            rightSum / static_cast<double>(rightCount));
                        const double verticalGradient = std::abs(
                            topSum / static_cast<double>(topCount) -
                            bottomSum / static_cast<double>(bottomCount));
                        const double gradient = std::max(
                            horizontalGradient, verticalGradient);
                        const double gradientLimit = std::max(
                            options.gradientAbsoluteLimit,
                            options.gradientNoiseSigmaMultiplier *
                                std::sqrt(variance / static_cast<double>(count)));
                        if (gradient > gradientLimit) continue;
                        blockSamples[siteIndex].push_back({ mean, variance });
                    }
                }
            }
        }
    }

    NoiseModel estimated;
    estimated.quality = NoiseModelQuality::EstimatedBurst;
    estimated.diagnostics.quality = estimated.quality;
    estimated.diagnostics.source = "reference lower-envelope block estimate";
    estimated.diagnostics.sourceRecordId = "deterministic-reference-estimate-v1";
    estimated.diagnostics.greenSitesIndependent = true;
    estimated.diagnostics.correlatedNoiseUnmodeled = true;
    InitializeSiteDiagnostics(estimated.diagnostics);
    for (CfaSite site : AllSites()) {
        const std::size_t siteIndex = SiteIndex(site);
        NoiseSiteDiagnostics& siteDiagnostics =
            estimated.diagnostics.sites[siteIndex];
        siteDiagnostics.candidateBlockCount = candidateBlocks[siteIndex];
        siteDiagnostics.acceptedBlockCount = blockSamples[siteIndex].size();
        if (blockSamples[siteIndex].size() < options.minimumBlocksPerSite) {
            siteDiagnostics.reason = "insufficient-flat-blocks";
            return SetError(error, "MFD burst noise estimate has too few accepted flat blocks.");
        }
        std::vector<std::vector<BlockNoiseSample>> bins(options.signalBinCount);
        for (const BlockNoiseSample& sample : blockSamples[siteIndex]) {
            const double normalized = std::clamp(
                (sample.mean - options.minimumSignal) /
                    (options.maximumSignal - options.minimumSignal),
                0.0,
                1.0);
            const std::size_t bin = std::min<std::size_t>(
                static_cast<std::size_t>(normalized * options.signalBinCount),
                options.signalBinCount - 1u);
            bins[bin].push_back(sample);
        }
        std::vector<NoiseCalibrationObservation> observations;
        for (const std::vector<BlockNoiseSample>& bin : bins) {
            if (bin.size() < options.minimumBlocksPerBin) continue;
            std::vector<double> variances;
            std::vector<double> means;
            variances.reserve(bin.size());
            means.reserve(bin.size());
            for (const BlockNoiseSample& sample : bin) {
                variances.push_back(sample.variance);
                means.push_back(sample.mean);
            }
            observations.push_back({
                Quantile(std::move(means), 0.5),
                Quantile(std::move(variances), options.lowerEnvelopeQuantile),
                static_cast<double>(bin.size())
            });
        }
        siteDiagnostics.populatedSignalBins = observations.size();
        if (observations.size() < options.minimumPopulatedBins) {
            siteDiagnostics.reason = "insufficient-signal-bins";
            return SetError(error, "MFD burst noise estimate lacks broad signal-bin support.");
        }
        const auto minmax = std::minmax_element(
            observations.begin(), observations.end(),
            [](const NoiseCalibrationObservation& a,
               const NoiseCalibrationObservation& b) {
                return a.meanSignal < b.meanSignal;
            });
        siteDiagnostics.signalMinimum = minmax.first->meanSignal;
        siteDiagnostics.signalMaximum = minmax.second->meanSignal;
        siteDiagnostics.signalCoverage =
            siteDiagnostics.signalMaximum - siteDiagnostics.signalMinimum;
        if (siteDiagnostics.signalCoverage < options.minimumSignalCoverage) {
            siteDiagnostics.reason = "insufficient-signal-coverage";
            return SetError(error, "MFD burst noise estimate has insufficient signal coverage.");
        }
        NoiseFitDiagnostics fit;
        const double quantization = QuantizationVariance(
            preparedFrame.calibration.usableSpanByCfaSite[siteIndex]);
        if (!FitPoissonGaussianProfile(
                observations,
                quantization,
                estimated.sites[siteIndex],
                fit,
                error)) {
            siteDiagnostics.reason = "fit-failed";
            return false;
        }
        siteDiagnostics.fitRootMeanSquareError = fit.rootMeanSquareError;
        siteDiagnostics.quantizationAdded = fit.constrainedOffsetVariance;
        siteDiagnostics.valid = true;
        siteDiagnostics.reason = "estimated-lower-envelope";
    }

    const auto greenVarianceAt = [&](std::size_t site, double signal) {
        return estimated.sites[site].shotScale * signal +
            estimated.sites[site].offsetVariance;
    };
    const double sharedGreenSignalMinimum = std::max(
        estimated.diagnostics.sites[1].signalMinimum,
        estimated.diagnostics.sites[2].signalMinimum);
    const double sharedGreenSignalMaximum = std::min(
        estimated.diagnostics.sites[1].signalMaximum,
        estimated.diagnostics.sites[2].signalMaximum);
    if (!Finite(sharedGreenSignalMinimum) ||
        !Finite(sharedGreenSignalMaximum) ||
        sharedGreenSignalMinimum > sharedGreenSignalMaximum) {
        return SetError(error, "MFD burst noise estimate has no shared G0/G1 signal range.");
    }
    const double representativeGreenSignal = std::clamp(
        0.18,
        sharedGreenSignalMinimum,
        sharedGreenSignalMaximum);
    for (double signal : {
            sharedGreenSignalMinimum,
            representativeGreenSignal }) {
        const double green0 = greenVarianceAt(1u, signal);
        const double green1 = greenVarianceAt(2u, signal);
        const double relative = std::abs(green0 - green1) /
            std::max(0.5 * (green0 + green1), 1.0e-18);
        if (!Finite(relative) ||
            relative > options.maximumGreenRelativeDisagreement) {
            return SetError(error, "MFD burst noise estimate has unstable G0/G1 profiles.");
        }
    }
    estimated.diagnostics.resolved = true;
    estimated.diagnostics.referenceOnly = false;
    estimated.diagnostics.warnings.push_back(
        "Burst-estimated noise is low confidence; correlated row/fixed-pattern noise remains unmodeled.");
    FinalizeModelIdentity(preparedFrame.cacheKey, estimated);
    if (!ValidateNoiseModel(estimated, error)) return false;
    model = std::move(estimated);
    return true;
}

NoiseResolutionResult ResolveNoiseModel(
    const RawMetadata& metadata,
    const PreparedRawFrame& preparedFrame,
    NormalizedTileCache* preparedTileCache,
    const NoiseResolutionOptions& options) {
    NoiseResolutionResult result;
    std::vector<std::string> warnings;
    std::string resolutionError;

    if (!ValidatePreparedNoiseInput(preparedFrame, &resolutionError)) {
        result.model = {};
        result.model.quality = NoiseModelQuality::Unavailable;
        result.model.diagnostics.quality = NoiseModelQuality::Unavailable;
        result.model.diagnostics.referenceOnly = true;
        result.model.diagnostics.source = "none";
        result.model.diagnostics.sourceRecordId = "invalid-preparation";
        result.model.diagnostics.warnings.push_back(resolutionError);
        InitializeSiteDiagnostics(result.model.diagnostics);
        FinalizeModelIdentity(preparedFrame.cacheKey, result.model);
        result.message = "MFD noise resolution rejected an invalid prepared-frame contract; processing must return the reference.";
        return result;
    }

    if (ResolveDngMetadataModel(
            metadata,
            preparedFrame,
            options,
            result.model,
            &resolutionError)) {
        result.resolved = true;
        result.message = "MFD noise model resolved from trusted frame metadata.";
        return result;
    }
    if (metadata.hasDngNoiseProfile) {
        warnings.push_back("Embedded NoiseProfile rejected: " + resolutionError);
    }

    if (!options.calibrationLibraryPath.empty()) {
        NoiseCalibrationLibrary library;
        resolutionError.clear();
        if (LoadNoiseCalibrationLibrary(
                options.calibrationLibraryPath, library, &resolutionError)) {
            const NoiseCalibrationRecord* record = SelectCalibrationRecord(
                library, metadata, options);
            if (record) {
                resolutionError.clear();
                if (ResolveCalibrationModel(
                        *record,
                        preparedFrame,
                        result.model,
                        &resolutionError)) {
                    result.model.diagnostics.warnings.insert(
                        result.model.diagnostics.warnings.end(),
                        warnings.begin(), warnings.end());
                    result.resolved = true;
                    result.message = "MFD noise model resolved from offline camera calibration.";
                    return result;
                }
                warnings.push_back("Matched calibration record rejected: " + resolutionError);
            } else {
                warnings.push_back("No offline noise calibration matched this camera/mode/ISO.");
            }
        } else {
            warnings.push_back("Offline noise calibration unavailable: " + resolutionError);
        }
    }

    if (options.enableBurstEstimate && preparedTileCache) {
        resolutionError.clear();
        if (EstimateNoiseModelFromPreparedFrame(
                preparedFrame,
                *preparedTileCache,
                options.burstEstimate,
                result.model,
                &resolutionError)) {
            result.model.diagnostics.warnings.insert(
                result.model.diagnostics.warnings.end(),
                warnings.begin(), warnings.end());
            result.resolved = true;
            result.message = "MFD noise model resolved from a conservative reference-frame estimate.";
            return result;
        }
        warnings.push_back("Reference burst estimate rejected: " + resolutionError);
    }

    if (options.enableGenericLowConfidence) {
        NoiseModel generic;
        generic.quality = NoiseModelQuality::GenericLowConfidence;
        generic.sites = options.genericLowConfidenceSites;
        generic.diagnostics.quality = generic.quality;
        generic.diagnostics.source = "explicit generic low-confidence profile";
        generic.diagnostics.sourceRecordId = "caller-supplied-generic-v1";
        generic.diagnostics.greenSitesIndependent = true;
        generic.diagnostics.correlatedNoiseUnmodeled = true;
        InitializeSiteDiagnostics(generic.diagnostics);
        bool valid = true;
        for (CfaSite site : AllSites()) {
            const std::size_t index = SiteIndex(site);
            const bool sourceIncludesQuantization =
                generic.sites[index].quantizationIncluded;
            const double quantization = QuantizationVariance(
                preparedFrame.calibration.usableSpanByCfaSite[index]);
            generic.sites[index].quantizationVariance = quantization;
            if (!sourceIncludesQuantization) {
                generic.sites[index].offsetVariance += quantization;
                generic.diagnostics.sites[index].quantizationAdded = true;
            }
            generic.sites[index].quantizationIncluded = true;
            std::string siteError;
            const bool siteValid =
                ValidateSiteNoiseProfile(generic.sites[index], &siteError);
            valid &= siteValid;
            generic.diagnostics.sites[index].valid = siteValid;
            generic.diagnostics.sites[index].reason = siteValid
                ? "explicit-generic"
                : siteError;
        }
        if (valid) {
            generic.diagnostics.resolved = true;
            generic.diagnostics.referenceOnly = false;
            generic.diagnostics.warnings = warnings;
            generic.diagnostics.warnings.push_back(
                "Generic noise profile requires strict gates and alternate-weight caps.");
            FinalizeModelIdentity(preparedFrame.cacheKey, generic);
            std::string genericError;
            if (ValidateNoiseModel(generic, &genericError)) {
                result.model = std::move(generic);
                result.resolved = true;
                result.message = "MFD noise model resolved from an explicit generic low-confidence profile.";
                return result;
            }
            warnings.push_back("Explicit generic low-confidence profile was rejected: " + genericError);
        } else {
            warnings.push_back("Explicit generic low-confidence profile was invalid.");
        }
    }

    result.model = {};
    result.model.quality = NoiseModelQuality::Unavailable;
    result.model.diagnostics.quality = NoiseModelQuality::Unavailable;
    result.model.diagnostics.resolved = false;
    result.model.diagnostics.referenceOnly = true;
    result.model.diagnostics.greenSitesIndependent = false;
    result.model.diagnostics.source = "none";
    result.model.diagnostics.sourceRecordId = "unavailable";
    result.model.diagnostics.warnings = std::move(warnings);
    InitializeSiteDiagnostics(result.model.diagnostics);
    FinalizeModelIdentity(preparedFrame.cacheKey, result.model);
    result.message = "No defensible MFD noise model is available; processing must return the reference.";
    return result;
}

void ApplyNoiseModelToPreparationOptions(
    const NoiseModel& model,
    PreparationOptions& options) {
    if (model.quality == NoiseModelQuality::Unavailable) {
        options.saturationStdDevAtWhite.fill(0.0);
        return;
    }
    for (CfaSite site : AllSites()) {
        const std::size_t index = SiteIndex(site);
        const SiteNoiseProfile& profile = model.sites[index];
        const double variance = profile.shotScale + profile.offsetVariance;
        options.saturationStdDevAtWhite[index] =
            Finite(variance) && variance >= 0.0 ? std::sqrt(variance) : 0.0;
    }
}

nlohmann::json SerializeNoiseModelDiagnostics(const NoiseModel& model) {
    nlohmann::json sites = nlohmann::json::array();
    for (std::size_t index = 0; index < model.sites.size(); ++index) {
        const NoiseSiteDiagnostics& diagnostics = model.diagnostics.sites[index];
        sites.push_back({
            { "site", CfaSiteName(diagnostics.site) },
            { "profile", SiteProfileJson(model.sites[index]) },
            { "candidateBlockCount", diagnostics.candidateBlockCount },
            { "acceptedBlockCount", diagnostics.acceptedBlockCount },
            { "populatedSignalBins", diagnostics.populatedSignalBins },
            { "signalMinimum", diagnostics.signalMinimum },
            { "signalMaximum", diagnostics.signalMaximum },
            { "signalCoverage", diagnostics.signalCoverage },
            { "fitRootMeanSquareError", diagnostics.fitRootMeanSquareError },
            { "duplicatedFromSharedGreen", diagnostics.duplicatedFromSharedGreen },
            { "quantizationAdded", diagnostics.quantizationAdded },
            { "valid", diagnostics.valid },
            { "reason", diagnostics.reason }
        });
    }
    return {
        { "contractId", model.contractId },
        { "contractVersion", model.contractVersion },
        { "preparedFrameCacheKey", model.preparedFrameCacheKey },
        { "identitySha256", model.identitySha256 },
        { "quality", NoiseModelQualityName(model.quality) },
        { "resolved", model.diagnostics.resolved },
        { "referenceOnly", model.diagnostics.referenceOnly },
        { "greenSitesIndependent", model.diagnostics.greenSitesIndependent },
        { "correlatedNoiseUnmodeled", model.diagnostics.correlatedNoiseUnmodeled },
        { "source", model.diagnostics.source },
        { "sourceRecordId", model.diagnostics.sourceRecordId },
        { "warnings", model.diagnostics.warnings },
        { "sites", std::move(sites) }
    };
}

} // namespace Raw::Mfd
