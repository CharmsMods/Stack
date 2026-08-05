#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <string>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 3 validation failed: " << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double a, double b, double tolerance = 1.0e-10) {
    return std::abs(a - b) <= tolerance;
}

Raw::Mfd::SiteNoiseProfile MakeNoiseProfile() {
    Raw::Mfd::SiteNoiseProfile profile;
    profile.shotScale = 0.0035;
    profile.offsetVariance = 0.000020;
    profile.quantizationVariance = 1.0e-8;
    profile.quantizationIncluded = true;
    return profile;
}

std::array<Raw::Mfd::SameCfaTapInput, Raw::Mfd::kSameCfaTapCount>
MakeTaps(
    const Raw::Mfd::KeysBicubicFootprint& footprint,
    const std::function<double(double, double)>& function) {
    std::array<Raw::Mfd::SameCfaTapInput, Raw::Mfd::kSameCfaTapCount> taps;
    for (std::size_t index = 0u; index < taps.size(); ++index) {
        taps[index].normalizedSample = function(
            static_cast<double>(footprint.taps[index].x),
            static_cast<double>(footprint.taps[index].y));
    }
    return taps;
}

double WeightedValue(
    const Raw::Mfd::KeysBicubicFootprint& footprint,
    const std::array<Raw::Mfd::SameCfaTapInput,
        Raw::Mfd::kSameCfaTapCount>& taps) {
    double value = 0.0;
    for (std::size_t index = 0u; index < taps.size(); ++index) {
        value += footprint.coefficients[index] *
            taps[index].comparisonGain * taps[index].normalizedSample;
    }
    return value;
}

bool ValidateKernelAndPolynomialReproduction() {
    bool ok = true;
    Raw::Mfd::KeysBicubicFootprint footprint;
    std::string error;
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 5.25, 4.60, Raw::Mfd::CfaSite::Green0 },
            footprint,
            &error),
        "Keys footprint could not be constructed: " + error);
    if (!ok) return false;
    ok &= Check(NearlyEqual(footprint.coefficientSum, 1.0, 1.0e-12) &&
            NearlyEqual(footprint.derivativeXSum, 0.0, 1.0e-12) &&
            NearlyEqual(footprint.derivativeYSum, 0.0, 1.0e-12),
        "Keys coefficients or analytic derivatives do not conserve constants");

    Raw::Mfd::SameCfaScalarParameters parameters;
    parameters.referenceComparisonPilot = 0.30;
    Raw::Mfd::SameCfaSampleResult sampled;
    auto taps = MakeTaps(footprint, [](double, double) { return 0.37; });
    error.clear();
    ok &= Check(Raw::Mfd::EvaluateSameCfaFootprintScalar(
            footprint,
            taps,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            sampled,
            &error),
        "constant scalar sample failed: " + error);
    ok &= Check(NearlyEqual(sampled.value, 0.37, 1.0e-12) &&
            NearlyEqual(sampled.gradientPlane.dxPerPlanePixel, 0.0, 1.0e-12) &&
            NearlyEqual(sampled.gradientPlane.dyPerPlanePixel, 0.0, 1.0e-12),
        "Keys sampler did not reproduce a constant field exactly");

    const auto polynomial = [](double x, double y) {
        return 0.20 + 0.015 * x - 0.023 * y;
    };
    taps = MakeTaps(footprint, polynomial);
    error.clear();
    ok &= Check(Raw::Mfd::EvaluateSameCfaFootprintScalar(
            footprint,
            taps,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            sampled,
            &error),
        "affine polynomial scalar sample failed: " + error);
    ok &= Check(NearlyEqual(
            sampled.value,
            polynomial(footprint.coordinate.x, footprint.coordinate.y),
            1.0e-12) &&
            NearlyEqual(sampled.gradientPlane.dxPerPlanePixel, 0.015, 1.0e-12) &&
            NearlyEqual(sampled.gradientPlane.dyPerPlanePixel, -0.023, 1.0e-12) &&
            NearlyEqual(sampled.gradientRaw.dxPerRawPixel, 0.0075, 1.0e-12) &&
            NearlyEqual(sampled.gradientRaw.dyPerRawPixel, -0.0115, 1.0e-12),
        "Keys sampler did not reproduce an affine plane or raw-pixel derivative units");

    Raw::Mfd::KeysBicubicFootprint integerFootprint;
    error.clear();
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 5.0, 4.0, Raw::Mfd::CfaSite::Blue },
            integerFootprint,
            &error),
        "integer Keys footprint failed: " + error);
    std::size_t unitCoefficientCount = 0u;
    std::size_t unitCoefficientIndex = 0u;
    for (std::size_t index = 0u;
         index < integerFootprint.coefficients.size();
         ++index) {
        if (NearlyEqual(integerFootprint.coefficients[index], 1.0, 1.0e-12)) {
            ++unitCoefficientCount;
            unitCoefficientIndex = index;
        } else {
            ok &= Check(NearlyEqual(
                    integerFootprint.coefficients[index], 0.0, 1.0e-12),
                "integer sampling retained a nonzero neighboring coefficient");
        }
    }
    const auto integerTaps = MakeTaps(
        integerFootprint,
        [](double x, double y) { return 10.0 * y + x; });
    error.clear();
    ok &= Check(unitCoefficientCount == 1u &&
            integerFootprint.taps[unitCoefficientIndex].x == 5 &&
            integerFootprint.taps[unitCoefficientIndex].y == 4 &&
            Raw::Mfd::EvaluateSameCfaFootprintScalar(
                integerFootprint,
                integerTaps,
                MakeNoiseProfile(),
                4095.0,
                parameters,
                sampled,
                &error) &&
            NearlyEqual(sampled.value, 45.0, 1.0e-12),
        "Keys sampler is not exact at integer CFA-plane coordinates");
    return ok;
}

bool ValidateAnalyticDerivatives() {
    bool ok = true;
    constexpr double epsilon = 1.0e-6;
    const Raw::Mfd::CfaPlaneCoordinate coordinate {
        6.35, 5.40, Raw::Mfd::CfaSite::Red
    };
    Raw::Mfd::KeysBicubicFootprint center;
    Raw::Mfd::KeysBicubicFootprint plusX;
    Raw::Mfd::KeysBicubicFootprint minusX;
    Raw::Mfd::KeysBicubicFootprint plusY;
    Raw::Mfd::KeysBicubicFootprint minusY;
    std::string error;
    ok &= Check(
        Raw::Mfd::BuildKeysBicubicFootprint(coordinate, center, &error) &&
        Raw::Mfd::BuildKeysBicubicFootprint(
            { coordinate.x + epsilon, coordinate.y, coordinate.site },
            plusX,
            &error) &&
        Raw::Mfd::BuildKeysBicubicFootprint(
            { coordinate.x - epsilon, coordinate.y, coordinate.site },
            minusX,
            &error) &&
        Raw::Mfd::BuildKeysBicubicFootprint(
            { coordinate.x, coordinate.y + epsilon, coordinate.site },
            plusY,
            &error) &&
        Raw::Mfd::BuildKeysBicubicFootprint(
            { coordinate.x, coordinate.y - epsilon, coordinate.site },
            minusY,
            &error),
        "finite-difference footprints could not be constructed: " + error);
    if (!ok) return false;
    const auto function = [](double x, double y) {
        return std::sin(0.37 * x) + 0.2 * std::cos(0.51 * y) +
            0.01 * x * y;
    };
    const auto taps = MakeTaps(center, function);
    const double finiteX =
        (WeightedValue(plusX, taps) - WeightedValue(minusX, taps)) /
        (2.0 * epsilon);
    const double finiteY =
        (WeightedValue(plusY, taps) - WeightedValue(minusY, taps)) /
        (2.0 * epsilon);
    double analyticX = 0.0;
    double analyticY = 0.0;
    for (std::size_t index = 0u; index < taps.size(); ++index) {
        analyticX += center.derivativeXPlane[index] *
            taps[index].normalizedSample;
        analyticY += center.derivativeYPlane[index] *
            taps[index].normalizedSample;
    }
    ok &= Check(NearlyEqual(analyticX, finiteX, 2.0e-9) &&
            NearlyEqual(analyticY, finiteY, 2.0e-9),
        "analytic Keys derivatives disagree with central finite differences");
    return ok;
}

bool ValidateVarianceAgainstMonteCarlo() {
    Raw::Mfd::KeysBicubicFootprint footprint;
    std::string error;
    bool ok = true;
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 4.35, 3.65, Raw::Mfd::CfaSite::Green1 },
            footprint,
            &error),
        "Monte Carlo footprint failed: " + error);
    if (!ok) return false;
    auto taps = MakeTaps(footprint, [](double x, double y) {
        return 0.25 + 0.003 * x + 0.002 * y;
    });
    for (std::size_t index = 0u; index < taps.size(); ++index) {
        taps[index].comparisonGain = 0.85 + 0.025 * static_cast<double>(index);
    }
    Raw::Mfd::SameCfaScalarParameters parameters;
    parameters.exposureScale = 1.2;
    parameters.referenceComparisonPilot = 0.42;
    Raw::Mfd::SameCfaSampleResult analytic;
    error.clear();
    ok &= Check(Raw::Mfd::EvaluateSameCfaFootprintScalar(
            footprint,
            taps,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            analytic,
            &error),
        "analytic variance sample failed: " + error);
    if (!ok) return false;

    std::array<double, Raw::Mfd::kSameCfaTapCount> tapSigma {};
    for (std::size_t index = 0u; index < taps.size(); ++index) {
        Raw::Mfd::GainPropagatedVariance propagated;
        error.clear();
        ok &= Check(Raw::Mfd::PropagateKnownGainVariance(
                MakeNoiseProfile(),
                parameters.exposureScale * taps[index].comparisonGain,
                parameters.referenceComparisonPilot,
                4095.0,
                propagated,
                &error),
            "Monte Carlo tap variance failed: " + error);
        tapSigma[index] = std::sqrt(propagated.variance);
    }
    if (!ok) return false;

    std::mt19937 generator(0x4d464433u);
    std::normal_distribution<double> standardNormal(0.0, 1.0);
    constexpr std::uint32_t sampleCount = 100000u;
    double mean = 0.0;
    double m2 = 0.0;
    for (std::uint32_t sampleIndex = 0u;
         sampleIndex < sampleCount;
         ++sampleIndex) {
        double sample = 0.0;
        for (std::size_t tapIndex = 0u; tapIndex < taps.size(); ++tapIndex) {
            const double gain = parameters.exposureScale *
                taps[tapIndex].comparisonGain;
            sample += footprint.coefficients[tapIndex] *
                (gain * taps[tapIndex].normalizedSample +
                    tapSigma[tapIndex] * standardNormal(generator));
        }
        const double delta = sample - mean;
        mean += delta / static_cast<double>(sampleIndex + 1u);
        m2 += delta * (sample - mean);
    }
    const double measuredVariance =
        m2 / static_cast<double>(sampleCount - 1u);
    ok &= Check(std::abs(measuredVariance - analytic.interpolationVariance) /
            analytic.interpolationVariance < 0.02,
        "per-tap gain/interpolation variance disagrees with Monte Carlo");
    ok &= Check(analytic.gateVariance >= analytic.interpolationVariance &&
            analytic.fusionVariance >= analytic.gateVariance &&
            analytic.effectiveDnStep > 0.0 &&
            analytic.darkVariance > 0.0,
        "scalar sampler omitted required propagated variance diagnostics");
    return ok;
}

bool ValidateMasksBordersAndRinging() {
    bool ok = true;
    Raw::Mfd::KeysBicubicFootprint footprint;
    std::string error;
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 3.25, 2.50, Raw::Mfd::CfaSite::Blue },
            footprint,
            &error),
        "rejection footprint failed: " + error);
    ok &= Check(Raw::Mfd::ValidateKeysBicubicFootprint(
            footprint, { 8, 7 }, &error),
        "interior footprint was rejected: " + error);
    Raw::Mfd::KeysBicubicFootprint border;
    error.clear();
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 0.0, 0.0, Raw::Mfd::CfaSite::Blue },
            border,
            &error) &&
            !Raw::Mfd::ValidateKeysBicubicFootprint(
                border, { 8, 7 }, &error),
        "strict four-by-four border support was not enforced");
    if (!ok) return false;

    auto taps = MakeTaps(footprint, [](double, double) { return 0.3; });
    Raw::Mfd::SameCfaScalarParameters parameters;
    parameters.referenceComparisonPilot = 0.3;
    Raw::Mfd::SameCfaSampleResult result;
    const auto expectFailure = [&](std::uint8_t flags,
                                   Raw::Mfd::SameCfaSampleFailure expected,
                                   const std::string& label) {
        auto rejected = taps;
        rejected[0].sampleFlags = flags;
        std::string localError;
        const bool accepted = Raw::Mfd::EvaluateSameCfaFootprintScalar(
            footprint,
            rejected,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            result,
            &localError);
        return Check(!accepted && result.failure == expected,
            label + " footprint was not rejected deterministically");
    };
    ok &= expectFailure(
        Raw::Mfd::SampleFlagMask(
            Raw::Mfd::PreparedSampleFlag::ExplicitDecoderClip),
        Raw::Mfd::SameCfaSampleFailure::ExplicitDecoderClip,
        "explicitly clipped");
    ok &= expectFailure(
        Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::Saturated),
        Raw::Mfd::SameCfaSampleFailure::SaturatedTap,
        "saturated");
    ok &= expectFailure(
        Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::Defective),
        Raw::Mfd::SameCfaSampleFailure::DefectiveTap,
        "defective");
    ok &= expectFailure(
        Raw::Mfd::SampleFlagMask(
            Raw::Mfd::PreparedSampleFlag::DecoderRepaired),
        Raw::Mfd::SameCfaSampleFailure::DecoderRepairedTap,
        "decoder-repaired");

    Raw::Mfd::KeysBicubicFootprint integerFootprint;
    error.clear();
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 3.0, 3.0, Raw::Mfd::CfaSite::Red },
            integerFootprint,
            &error),
        "zero-weight rejection footprint failed: " + error);
    auto integerTaps = MakeTaps(
        integerFootprint,
        [](double, double) { return 0.2; });
    const auto zeroWeight = std::find_if(
        integerFootprint.coefficients.begin(),
        integerFootprint.coefficients.end(),
        [](double coefficient) { return coefficient == 0.0; });
    if (zeroWeight != integerFootprint.coefficients.end()) {
        integerTaps[static_cast<std::size_t>(
            zeroWeight - integerFootprint.coefficients.begin())].sampleFlags =
            Raw::Mfd::SampleFlagMask(Raw::Mfd::PreparedSampleFlag::Saturated);
    }
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateSameCfaFootprintScalar(
            integerFootprint,
            integerTaps,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            result,
            &error) &&
            result.failure == Raw::Mfd::SameCfaSampleFailure::SaturatedTap,
        "zero-weight taps bypassed strict whole-footprint mask validation");

    Raw::Mfd::KeysBicubicFootprint ringingFootprint;
    error.clear();
    ok &= Check(Raw::Mfd::BuildKeysBicubicFootprint(
            { 0.5, 3.25, Raw::Mfd::CfaSite::Green0 },
            ringingFootprint,
            &error),
        "ringing stress footprint failed: " + error);
    auto ringingTaps = MakeTaps(
        ringingFootprint,
        [](double x, double) { return x >= 2.0 ? 1.0 : 0.0; });
    error.clear();
    ok &= Check(Raw::Mfd::EvaluateSameCfaFootprintScalar(
            ringingFootprint,
            ringingTaps,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            result,
            &error) &&
            result.value < 0.0,
        "Keys ringing stress was silently clamped before fusion");
    for (std::size_t index = 0u; index < ringingTaps.size(); ++index) {
        if (ringingFootprint.taps[index].x >= 2) {
            ringingTaps[index].sampleFlags =
                Raw::Mfd::SampleFlagMask(
                    Raw::Mfd::PreparedSampleFlag::Saturated);
        }
    }
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateSameCfaFootprintScalar(
            ringingFootprint,
            ringingTaps,
            MakeNoiseProfile(),
            4095.0,
            parameters,
            result,
            &error) &&
            result.failure == Raw::Mfd::SameCfaSampleFailure::SaturatedTap,
        "clipped-edge ringing footprint was allowed to contribute");
    return ok;
}

Raw::Mfd::PreparedRawFrame MakePreparedFrame(Raw::CfaPattern pattern) {
    Raw::Mfd::PreparedRawFrame frame;
    frame.cacheKey = std::string(64u, 'c');
    frame.sourceContentSha256 = std::string(64u, 'd');
    frame.activeExtent = { 12, 10 };
    frame.activeCfaPattern = pattern;
    frame.tileRawPixels = 5u;
    frame.tileColumns = 3u;
    frame.tileRows = 2u;
    frame.calibration.maximumBlackByCfaSite.fill(64.0);
    frame.calibration.whiteLevelByCfaSite.fill(4159.0);
    frame.calibration.usableSpanByCfaSite.fill(4095.0);
    return frame;
}

std::size_t SiteIndex(Raw::Mfd::CfaSite site) {
    switch (site) {
        case Raw::Mfd::CfaSite::Red: return 0u;
        case Raw::Mfd::CfaSite::Green0: return 1u;
        case Raw::Mfd::CfaSite::Green1: return 2u;
        case Raw::Mfd::CfaSite::Blue: return 3u;
    }
    return 0u;
}

double SyntheticPreparedSample(
    const Raw::Mfd::CfaLayout& layout,
    std::uint64_t rawX,
    std::uint64_t rawY) {
    const Raw::Mfd::CfaSite site = layout.SiteAt(
        static_cast<std::int64_t>(rawX),
        static_cast<std::int64_t>(rawY));
    const Raw::Mfd::CfaPlaneCoordinate plane = layout.RawToPlane(
        { static_cast<double>(rawX), static_cast<double>(rawY) },
        site);
    return 0.10 * static_cast<double>(SiteIndex(site) + 1u) +
        0.004 * plane.x + 0.006 * plane.y;
}

bool PopulatePreparedCache(
    const Raw::Mfd::PreparedRawFrame& frame,
    Raw::Mfd::MemoryNormalizedTileCache& cache,
    std::string* error) {
    Raw::Mfd::CfaLayout layout;
    if (!Raw::Mfd::CfaLayout::TryCreate(frame.activeCfaPattern, layout)) {
        if (error) *error = "invalid synthetic layout";
        return false;
    }
    for (std::uint32_t tileY = 0u; tileY < frame.tileRows; ++tileY) {
        for (std::uint32_t tileX = 0u; tileX < frame.tileColumns; ++tileX) {
            Raw::Mfd::PreparedRawTile tile;
            tile.tileX = tileX;
            tile.tileY = tileY;
            tile.originX = static_cast<std::uint64_t>(tileX) *
                frame.tileRawPixels;
            tile.originY = static_cast<std::uint64_t>(tileY) *
                frame.tileRawPixels;
            tile.extent = {
                std::min<std::uint64_t>(
                    frame.tileRawPixels,
                    frame.activeExtent.width - tile.originX),
                std::min<std::uint64_t>(
                    frame.tileRawPixels,
                    frame.activeExtent.height - tile.originY)
            };
            const std::size_t count = static_cast<std::size_t>(
                tile.extent.width * tile.extent.height);
            tile.normalizedMosaic.resize(count);
            tile.comparisonGain.resize(count);
            tile.sampleFlags.assign(count, 0u);
            for (std::uint64_t y = 0u; y < tile.extent.height; ++y) {
                for (std::uint64_t x = 0u; x < tile.extent.width; ++x) {
                    const std::uint64_t rawX = tile.originX + x;
                    const std::uint64_t rawY = tile.originY + y;
                    const std::size_t index = static_cast<std::size_t>(
                        y * tile.extent.width + x);
                    tile.normalizedMosaic[index] = static_cast<float>(
                        SyntheticPreparedSample(layout, rawX, rawY));
                    tile.comparisonGain[index] = static_cast<float>(
                        1.0 + 0.001 * static_cast<double>(rawX));
                }
            }
            if (!cache.Write(frame.cacheKey, tile, error)) return false;
        }
    }
    return true;
}

Raw::Mfd::NoiseModel MakeResolvedNoiseModel(
    const Raw::Mfd::PreparedRawFrame& frame) {
    Raw::Mfd::NoiseModel model;
    model.preparedFrameCacheKey = frame.cacheKey;
    model.identitySha256 = std::string(64u, 'e');
    model.quality = Raw::Mfd::NoiseModelQuality::CalibratedCamera;
    model.sites.fill(MakeNoiseProfile());
    model.diagnostics.quality = model.quality;
    model.diagnostics.resolved = true;
    model.diagnostics.referenceOnly = false;
    model.diagnostics.greenSitesIndependent = true;
    for (std::size_t index = 0u; index < model.diagnostics.sites.size(); ++index) {
        model.diagnostics.sites[index].site = static_cast<Raw::Mfd::CfaSite>(index);
        model.diagnostics.sites[index].valid = true;
    }
    return model;
}

bool ValidatePreparedTilesAndAllCfaPatterns() {
    bool ok = true;
    const std::array<Raw::CfaPattern, 4> patterns {
        Raw::CfaPattern::RGGB,
        Raw::CfaPattern::BGGR,
        Raw::CfaPattern::GBRG,
        Raw::CfaPattern::GRBG
    };
    const std::array<Raw::Mfd::CfaSite, 4> sites {
        Raw::Mfd::CfaSite::Red,
        Raw::Mfd::CfaSite::Green0,
        Raw::Mfd::CfaSite::Green1,
        Raw::Mfd::CfaSite::Blue
    };
    for (Raw::CfaPattern pattern : patterns) {
        const Raw::Mfd::PreparedRawFrame frame = MakePreparedFrame(pattern);
        Raw::Mfd::MemoryNormalizedTileCache cache;
        std::string error;
        ok &= Check(PopulatePreparedCache(frame, cache, &error),
            "synthetic prepared cache failed: " + error);
        if (!ok) return false;
        Raw::Mfd::CfaLayout layout;
        Raw::Mfd::CfaLayout::TryCreate(pattern, layout);
        const Raw::Mfd::NoiseModel model = MakeResolvedNoiseModel(frame);
        for (Raw::Mfd::CfaSite site : sites) {
            const Raw::Mfd::CfaPlaneCoordinate plane {
                3.25, 2.50, site
            };
            const Raw::Mfd::RawCoordinate raw = layout.PlaneToRaw(plane);
            Raw::Mfd::SameCfaScalarParameters parameters;
            parameters.referenceComparisonPilot = 0.25;
            Raw::Mfd::SameCfaSampleResult result;
            error.clear();
            ok &= Check(Raw::Mfd::SamplePreparedFrameSameCfaScalar(
                    frame,
                    cache,
                    model,
                    raw,
                    site,
                    parameters,
                    result,
                    &error),
                "prepared sampler failed a CFA layout/site: " + error);
            ok &= Check(result.valid &&
                    result.sourcePlane.site == site &&
                    NearlyEqual(result.sourcePlane.x, plane.x, 1.0e-12) &&
                    NearlyEqual(result.sourcePlane.y, plane.y, 1.0e-12) &&
                    std::isfinite(result.value),
                "prepared sampler crossed CFA parity or lost coordinate units");

            const Raw::Mfd::RawCoordinate borderRaw = layout.PlaneToRaw({
                0.0, 0.0, site
            });
            error.clear();
            ok &= Check(!Raw::Mfd::SamplePreparedFrameSameCfaScalar(
                    frame,
                    cache,
                    model,
                    borderRaw,
                    site,
                    parameters,
                    result,
                    &error) &&
                    result.failure ==
                        Raw::Mfd::SameCfaSampleFailure::FootprintOutsideActiveArea,
                "prepared sampler invented border support for a CFA layout");
        }

        Raw::Mfd::NoiseModel wrongFrameModel = model;
        wrongFrameModel.preparedFrameCacheKey = std::string(64u, 'f');
        Raw::Mfd::SameCfaSampleResult result;
        Raw::Mfd::SameCfaScalarParameters parameters;
        error.clear();
        ok &= Check(!Raw::Mfd::SamplePreparedFrameSameCfaScalar(
                frame,
                cache,
                wrongFrameModel,
                layout.PlaneToRaw({ 3.25, 2.50, Raw::Mfd::CfaSite::Red }),
                Raw::Mfd::CfaSite::Red,
                parameters,
                result,
                &error) &&
                result.failure == Raw::Mfd::SameCfaSampleFailure::InvalidNoiseModel,
            "sampler accepted a noise model bound to another prepared frame");
    }
    return ok;
}

} // namespace

bool ValidateMfdPhase3SameCfaSampler() {
    bool ok = true;
    ok &= ValidateKernelAndPolynomialReproduction();
    ok &= ValidateAnalyticDerivatives();
    ok &= ValidateVarianceAgainstMonteCarlo();
    ok &= ValidateMasksBordersAndRinging();
    ok &= ValidatePreparedTilesAndAllCfaPatterns();
    if (ok) {
        std::cout << "MFD Phase 3 same-CFA sampler validation passed." << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
