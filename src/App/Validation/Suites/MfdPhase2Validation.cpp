#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/NoiseModel.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 2 validation failed: " << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double a, double b, double tolerance = 1.0e-10) {
    return std::abs(a - b) <= tolerance;
}

Raw::Mfd::PreparedRawFrame MakePreparedFrame(
    double usableSpan = 4095.0,
    char cacheIdentity = 'a') {
    Raw::Mfd::PreparedRawFrame frame;
    frame.cacheKey = std::string(64u, cacheIdentity);
    frame.sourceContentSha256 = std::string(64u, cacheIdentity);
    frame.sensorActiveArea = { 0, 0, 512, 512 };
    frame.activeExtent = { 512, 512 };
    frame.activeCfaPattern = Raw::CfaPattern::RGGB;
    frame.tileRawPixels = 512;
    frame.tileColumns = 1;
    frame.tileRows = 1;
    frame.calibration.maximumBlackByCfaSite.fill(64.0);
    frame.calibration.whiteLevelByCfaSite.fill(64.0 + usableSpan);
    frame.calibration.usableSpanByCfaSite.fill(usableSpan);
    return frame;
}

Raw::RawMetadata MakeMetadata() {
    Raw::RawMetadata metadata;
    metadata.cameraMake = "Stack Camera Co";
    metadata.cameraModel = "Photon 1";
    metadata.dngUniqueCameraModel = "Stack Photon 1";
    metadata.bitDepth = 14;
    metadata.cfaPattern = Raw::CfaPattern::RGGB;
    metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    metadata.mosaiced = true;
    metadata.isoSpeed = 800.0f;
    metadata.hasIsoSpeed = true;
    return metadata;
}

bool ValidateAnalyticVariancePropagation() {
    Raw::Mfd::SiteNoiseProfile profile;
    profile.shotScale = 0.01;
    profile.offsetVariance = 0.0001;
    profile.quantizationVariance = 1.0e-8;
    profile.quantizationIncluded = true;

    Raw::Mfd::GainPropagatedVariance propagated;
    std::string error;
    bool ok = true;
    ok &= Check(Raw::Mfd::PropagateKnownGainVariance(
            profile, 2.0, 0.6, 1000.0, propagated, &error),
        "gain propagation failed: " + error);
    ok &= Check(NearlyEqual(propagated.shotCoefficient, 0.02) &&
            NearlyEqual(propagated.offsetVariance, 0.0004) &&
            NearlyEqual(propagated.variance, 0.0124) &&
            NearlyEqual(propagated.effectiveDnStep, 0.002),
        "analytic gain propagation does not implement kS*nu + k^2*O");

    std::mt19937 generator(0x4d464432u);
    const double baseMean = 0.3;
    const double baseVariance =
        profile.shotScale * baseMean + profile.offsetVariance;
    std::normal_distribution<double> noise(0.0, std::sqrt(baseVariance));
    constexpr std::uint32_t sampleCount = 100000;
    double mean = 0.0;
    double m2 = 0.0;
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const double value = 2.0 * (baseMean + noise(generator));
        const double delta = value - mean;
        mean += delta / static_cast<double>(index + 1u);
        m2 += delta * (value - mean);
    }
    const double measuredVariance = m2 / static_cast<double>(sampleCount - 1u);
    ok &= Check(std::abs(measuredVariance - propagated.variance) /
            propagated.variance < 0.02,
        "Monte Carlo gain variance disagrees with analytic propagation");

    Raw::Mfd::GainPropagatedVariance negativePilot;
    error.clear();
    ok &= Check(Raw::Mfd::PropagateKnownGainVariance(
            profile, 2.0, -0.25, 1000.0, negativePilot, &error) &&
            NearlyEqual(negativePilot.variance, 0.0004),
        "negative measured signal was not paired with a nonnegative shot pilot");
    ok &= Check(std::isnan(Raw::Mfd::NonnegativeShotPilot(
            std::numeric_limits<double>::quiet_NaN())),
        "NaN shot pilot was silently converted to zero");
    return ok;
}

bool ValidateVarianceComponentsAndFloors() {
    bool ok = true;
    const double quantization = Raw::Mfd::QuantizationVariance(4095.0);
    ok &= Check(NearlyEqual(
            quantization,
            1.0 / (12.0 * 4095.0 * 4095.0),
            1.0e-18),
        "normalized one-DN quantization variance is wrong");

    std::vector<Raw::Mfd::InterpolationVarianceTerm> terms {
        { 0.75, 0.010, 1.0, 1000.0 },
        { -0.25, 0.020, 2.0, 1000.0 }
    };
    double interpolation = 0.0;
    double dnStepSquared = 0.0;
    std::string error;
    ok &= Check(Raw::Mfd::PropagateInterpolationVariance(
            terms, interpolation, dnStepSquared, &error),
        "interpolation variance failed: " + error);
    ok &= Check(NearlyEqual(
            interpolation,
            0.75 * 0.75 * 0.010 + 0.25 * 0.25 * 0.020),
        "interpolation variance did not square negative coefficients");
    ok &= Check(NearlyEqual(
            dnStepSquared,
            0.75 * 0.75 / 1000000.0 +
                0.25 * 0.25 * 4.0 / 1000000.0),
        "interpolated effective DN step is wrong");

    double registration = 0.0;
    error.clear();
    ok &= Check(Raw::Mfd::RegistrationUncertaintyVariance(
            { 0.2, -0.1 },
            { 0.25, 0.05, 0.16 },
            registration,
            &error),
        "registration variance failed: " + error);
    ok &= Check(NearlyEqual(
            registration,
            0.2 * 0.2 * 0.25 +
                2.0 * 0.2 * -0.1 * 0.05 +
                0.1 * 0.1 * 0.16),
        "registration gradient/covariance variance is wrong");
    error.clear();
    ok &= Check(!Raw::Mfd::RegistrationUncertaintyVariance(
            { 1.0, 1.0 }, { 1.0, 2.0, 1.0 }, registration, &error),
        "materially indefinite registration covariance was accepted");

    double scale = 0.0;
    error.clear();
    ok &= Check(Raw::Mfd::ExposureScaleUncertaintyVariance(
            0.4, 2.0, 0.01, scale, &error) &&
            NearlyEqual(scale, 0.0004),
        "exposure-scale uncertainty variance is wrong");

    Raw::Mfd::EffectiveVariance effective;
    error.clear();
    ok &= Check(Raw::Mfd::ComposeEffectiveVariance(
            interpolation,
            registration,
            0.00003,
            scale,
            0.0002,
            dnStepSquared,
            1.0e-12,
            effective,
            &error),
        "gate/fusion variance composition failed: " + error);
    ok &= Check(NearlyEqual(
            effective.gateVariance,
            interpolation + registration + 0.00003 + 1.0e-12) &&
            NearlyEqual(
                effective.fusionVariance,
                effective.gateVariance + scale),
        "exposure-scale uncertainty leaked into the gate variance or was omitted from fusion");
    ok &= Check(NearlyEqual(
            effective.divisionVarianceFloor,
            std::max(1.0e-12, dnStepSquared / 12.0)),
        "quantization-aware division floor was added incorrectly");
    return ok;
}

bool ValidateFlatDarkRegression() {
    const double expectedShot = 0.0045;
    const double expectedOffset = 0.000018;
    std::vector<Raw::Mfd::NoiseCalibrationObservation> observations;
    for (double mean : { 0.0, 0.02, 0.08, 0.18, 0.35, 0.60, 0.82 }) {
        observations.push_back({
            mean,
            expectedShot * mean + expectedOffset,
            mean == 0.0 ? 4.0 : 2.0
        });
    }
    Raw::Mfd::SiteNoiseProfile fitted;
    Raw::Mfd::NoiseFitDiagnostics diagnostics;
    std::string error;
    bool ok = true;
    ok &= Check(Raw::Mfd::FitPoissonGaussianProfile(
            observations,
            1.0e-8,
            fitted,
            diagnostics,
            &error),
        "flat/dark calibration regression failed: " + error);
    ok &= Check(NearlyEqual(fitted.shotScale, expectedShot, 1.0e-12) &&
            NearlyEqual(fitted.offsetVariance, expectedOffset, 1.0e-12),
        "flat/dark regression did not recover S/O coefficients");
    ok &= Check(diagnostics.observationCount == observations.size() &&
            diagnostics.signalMinimum == 0.0 &&
            diagnostics.signalMaximum == 0.82 &&
            diagnostics.rootMeanSquareError < 1.0e-12,
        "flat/dark regression diagnostics are incomplete");

    observations = {
        { 0.0, 0.0, 1.0 },
        { 0.5, 0.0001, 1.0 },
        { 1.0, 0.0002, 1.0 }
    };
    error.clear();
    ok &= Check(Raw::Mfd::FitPoissonGaussianProfile(
            observations,
            0.00005,
            fitted,
            diagnostics,
            &error) &&
            fitted.offsetVariance >= 0.00005 &&
            diagnostics.constrainedOffsetVariance,
        "quantization variance floor was not enforced exactly once during fitting");
    return ok;
}

struct TemporaryDirectory {
    std::filesystem::path path;

    TemporaryDirectory() = default;
    explicit TemporaryDirectory(std::filesystem::path directory)
        : path(std::move(directory)) {}
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&& other) noexcept
        : path(std::move(other.path)) {
        other.path.clear();
    }
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;

    ~TemporaryDirectory() {
        if (path.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

TemporaryDirectory MakeTemporaryDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    return TemporaryDirectory(
        std::filesystem::temp_directory_path() /
            ("stack-mfd-phase2-" + std::to_string(nonce)));
}

nlohmann::json CalibrationSite(
    double shot,
    double offset,
    double tau0 = 0.0,
    double tau1 = 0.0) {
    return {
        { "shotScale", shot },
        { "offsetVariance", offset },
        { "residualModelTau0", tau0 },
        { "residualModelTau1", tau1 }
    };
}

bool ValidateMetadataAndCalibrationHierarchy() {
    bool ok = true;
    Raw::Mfd::PreparedRawFrame prepared = MakePreparedFrame();
    Raw::RawMetadata metadata = MakeMetadata();
    metadata.hasDngNoiseProfile = true;
    metadata.dngNoiseProfile = {
        { 0.0040, 0.000020 },
        { 0.0035, 0.000018 },
        { 0.0048, 0.000025 }
    };
    Raw::Mfd::NoiseResolutionOptions options;
    options.enableBurstEstimate = false;
    const Raw::Mfd::NoiseResolutionResult trusted =
        Raw::Mfd::ResolveNoiseModel(metadata, prepared, nullptr, options);
    ok &= Check(trusted.resolved &&
            trusted.model.quality == Raw::Mfd::NoiseModelQuality::TrustedMetadata,
        "valid DNG NoiseProfile did not win the quality hierarchy");
    ok &= Check(NearlyEqual(
            trusted.model.sites[1].shotScale,
            trusted.model.sites[2].shotScale) &&
            trusted.model.diagnostics.sites[1].duplicatedFromSharedGreen &&
            trusted.model.diagnostics.sites[2].duplicatedFromSharedGreen &&
            !trusted.model.diagnostics.greenSitesIndependent,
        "shared DNG green profile was not duplicated and diagnosed explicitly");
    ok &= Check(Raw::Mfd::NoiseModelConfidence(
            trusted.model.quality, {}) == 1.0,
        "trusted metadata quality does not map to the serialized confidence hierarchy");

    metadata.hasDngNoiseProfile = false;
    metadata.dngNoiseProfile.clear();
    TemporaryDirectory temporary = MakeTemporaryDirectory();
    std::filesystem::create_directories(temporary.path);
    const std::filesystem::path calibrationPath = temporary.path / "noise-calibration.json";
    const nlohmann::json calibration = {
        { "schemaVersion", 1 },
        { "records", nlohmann::json::array({
            {
                { "recordId", "photon1-iso400-1600-v1" },
                { "cameraMake", "Stack Camera Co" },
                { "cameraModel", "Photon 1" },
                { "uniqueCameraModel", "Stack Photon 1" },
                { "cameraMode", "full-raw" },
                { "bitDepth", 14 },
                { "cfaPattern", "RGGB" },
                { "isoMinimum", 400.0 },
                { "isoMaximum", 1600.0 },
                { "quantizationIncluded", false },
                { "sites", {
                    { "R", CalibrationSite(0.0041, 0.000020) },
                    { "G0", CalibrationSite(0.0032, 0.000015) },
                    { "G1", CalibrationSite(0.0038, 0.000017) },
                    { "B", CalibrationSite(0.0050, 0.000026) }
                } }
            }
        }) }
    };
    {
        std::ofstream output(calibrationPath, std::ios::binary | std::ios::trunc);
        output << calibration.dump(2);
    }
    options.calibrationLibraryPath = calibrationPath;
    options.cameraMode = "full-raw";
    const Raw::Mfd::NoiseResolutionResult calibrated =
        Raw::Mfd::ResolveNoiseModel(metadata, prepared, nullptr, options);
    ok &= Check(calibrated.resolved &&
            calibrated.model.quality == Raw::Mfd::NoiseModelQuality::CalibratedCamera &&
            calibrated.model.diagnostics.sourceRecordId ==
                "photon1-iso400-1600-v1",
        "matching offline camera calibration did not resolve");
    ok &= Check(!NearlyEqual(
            calibrated.model.sites[1].shotScale,
            calibrated.model.sites[2].shotScale) &&
            calibrated.model.diagnostics.greenSitesIndependent,
        "offline calibration did not preserve independent G0/G1 profiles");
    ok &= Check(calibrated.model.diagnostics.sites[0].quantizationAdded &&
            calibrated.model.sites[0].offsetVariance > 0.000020,
        "offline profile omitted its declared quantization variance addition");

    Raw::RawMetadata invalidMetadata = MakeMetadata();
    invalidMetadata.hasDngNoiseProfile = true;
    invalidMetadata.dngNoiseProfile = { { -1.0, 0.0 } };
    options.calibrationLibraryPath.clear();
    const Raw::Mfd::NoiseResolutionResult unavailable =
        Raw::Mfd::ResolveNoiseModel(
            invalidMetadata, prepared, nullptr, options);
    ok &= Check(!unavailable.resolved &&
            unavailable.model.quality == Raw::Mfd::NoiseModelQuality::Unavailable &&
            unavailable.model.diagnostics.referenceOnly,
        "invalid/missing profiles did not degrade to explicit reference-only behavior");

    Raw::Mfd::SiteNoiseProfile genericSite;
    genericSite.shotScale = 0.005;
    genericSite.offsetVariance = 0.000030;
    options.enableGenericLowConfidence = true;
    options.genericLowConfidenceSites.fill(genericSite);
    const Raw::Mfd::NoiseResolutionResult generic =
        Raw::Mfd::ResolveNoiseModel(invalidMetadata, prepared, nullptr, options);
    ok &= Check(generic.resolved &&
            generic.model.quality ==
                Raw::Mfd::NoiseModelQuality::GenericLowConfidence &&
            !generic.model.diagnostics.warnings.empty(),
        "explicit generic profile did not remain last in the quality hierarchy");

    Raw::Mfd::PreparedRawFrame invalidPrepared = prepared;
    invalidPrepared.cacheKey = "not-a-preparation-identity";
    const Raw::Mfd::NoiseResolutionResult invalidPreparation =
        Raw::Mfd::ResolveNoiseModel(
            invalidMetadata, invalidPrepared, nullptr, options);
    ok &= Check(!invalidPreparation.resolved &&
            invalidPreparation.model.quality ==
                Raw::Mfd::NoiseModelQuality::Unavailable &&
            invalidPreparation.model.diagnostics.sourceRecordId ==
                "invalid-preparation",
        "invalid prepared-frame contracts were allowed into noise resolution");
    return ok;
}

std::size_t SyntheticSiteIndex(std::uint64_t x, std::uint64_t y) {
    if ((y & 1u) == 0u) return (x & 1u) == 0u ? 0u : 1u;
    return (x & 1u) == 0u ? 2u : 3u;
}

bool ValidateConservativeBurstEstimate() {
    Raw::Mfd::PreparedRawFrame frame = MakePreparedFrame(4095.0, 'b');
    Raw::Mfd::PreparedRawTile tile;
    tile.tileX = 0;
    tile.tileY = 0;
    tile.extent = frame.activeExtent;
    const std::size_t pixelCount = static_cast<std::size_t>(
        tile.extent.width * tile.extent.height);
    tile.normalizedMosaic.resize(pixelCount);
    tile.comparisonGain.assign(pixelCount, 1.0f);
    tile.sampleFlags.assign(pixelCount, 0u);
    const std::array<double, 4> shot { 0.0020, 0.0018, 0.0019, 0.0022 };
    const std::array<double, 4> offset { 0.000010, 0.000008, 0.0000085, 0.000012 };
    std::mt19937 generator(0x42525354u);
    for (std::uint64_t y = 0; y < tile.extent.height; ++y) {
        const std::uint32_t band = static_cast<std::uint32_t>(y / 64u);
        const double signal = 0.05 + 0.10 * static_cast<double>(band);
        for (std::uint64_t x = 0; x < tile.extent.width; ++x) {
            const std::size_t site = SyntheticSiteIndex(x, y);
            std::normal_distribution<double> noise(
                0.0,
                std::sqrt(shot[site] * signal + offset[site]));
            tile.normalizedMosaic[static_cast<std::size_t>(
                y * tile.extent.width + x)] =
                static_cast<float>(signal + noise(generator));
        }
    }
    Raw::Mfd::MemoryNormalizedTileCache cache;
    std::string error;
    bool ok = true;
    ok &= Check(cache.Write(frame.cacheKey, tile, &error),
        "synthetic burst-estimate tile could not be stored: " + error);
    Raw::Mfd::NoiseModel estimated;
    Raw::Mfd::BurstNoiseEstimationOptions options;
    error.clear();
    const bool estimateResolved =
        Raw::Mfd::EstimateNoiseModelFromPreparedFrame(
            frame, cache, options, estimated, &error);
    ok &= Check(estimateResolved,
        "conservative same-CFA burst estimate failed: " + error);
    if (!ok) return false;
    ok &= Check(estimated.quality ==
            Raw::Mfd::NoiseModelQuality::EstimatedBurst &&
            estimated.diagnostics.greenSitesIndependent &&
            estimated.identitySha256.size() == 64u,
        "burst estimate did not produce a stable independent per-CFA model");
    for (std::size_t site = 0; site < estimated.sites.size(); ++site) {
        ok &= Check(estimated.sites[site].shotScale >= 0.0 &&
                estimated.sites[site].offsetVariance >=
                    Raw::Mfd::QuantizationVariance(4095.0) &&
                estimated.diagnostics.sites[site].acceptedBlockCount >=
                    options.minimumBlocksPerSite &&
                estimated.diagnostics.sites[site].populatedSignalBins >=
                    options.minimumPopulatedBins,
            "burst estimate site lacks conservative coefficients or diagnostics");
    }

    Raw::Mfd::PreparationOptions preparationOptions;
    Raw::Mfd::ApplyNoiseModelToPreparationOptions(
        estimated, preparationOptions);
    ok &= Check(std::all_of(
            preparationOptions.saturationStdDevAtWhite.begin(),
            preparationOptions.saturationStdDevAtWhite.end(),
            [](double value) { return std::isfinite(value) && value > 0.0; }),
        "resolved noise model did not feed Phase 1 saturation guards");
    const nlohmann::json diagnostics =
        Raw::Mfd::SerializeNoiseModelDiagnostics(estimated);
    ok &= Check(diagnostics.value("quality", std::string()) ==
            "estimated-burst" &&
            diagnostics["sites"].size() == 4u,
        "serialized noise diagnostics are incomplete");
    return ok;
}

} // namespace

bool ValidateMfdPhase2NoiseModel() {
    bool ok = true;
    ok &= ValidateAnalyticVariancePropagation();
    ok &= ValidateVarianceComponentsAndFloors();
    ok &= ValidateFlatDarkRegression();
    ok &= ValidateMetadataAndCalibrationHierarchy();
    ok &= ValidateConservativeBurstEstimate();
    if (ok) {
        std::cout << "MFD Phase 2 noise-model validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
