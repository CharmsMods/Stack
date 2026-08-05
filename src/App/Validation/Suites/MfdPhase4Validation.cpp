#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/GlobalRegistration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 4 validation failed: " << message << std::endl;
    }
    return condition;
}

bool NearlyEqual(double a, double b, double tolerance) {
    return std::abs(a - b) <= tolerance;
}

std::vector<Raw::Mfd::GlobalPlanePair> MakeTranslatedPlanes(
    double translationPlaneX,
    double translationPlaneY,
    bool lowTexture = false,
    bool periodic = false) {
    constexpr std::uint64_t width = 64u;
    constexpr std::uint64_t height = 64u;
    const std::array<Raw::Mfd::CfaSite, 4> sites {
        Raw::Mfd::CfaSite::Red,
        Raw::Mfd::CfaSite::Green0,
        Raw::Mfd::CfaSite::Green1,
        Raw::Mfd::CfaSite::Blue
    };
    std::vector<Raw::Mfd::GlobalPlanePair> planes;
    for (std::size_t siteIndex = 0u; siteIndex < sites.size(); ++siteIndex) {
        Raw::Mfd::GlobalPlanePair plane;
        plane.site = sites[siteIndex];
        plane.extent = { width, height };
        plane.reference.resize(width * height);
        plane.alternate.resize(width * height);
        plane.validMask.assign(width * height, 1u);
        plane.rawPixelsPerPlanePixel = 2.0;
        std::mt19937 generator(
            0x50484153u + static_cast<std::uint32_t>(siteIndex) * 977u);
        std::uniform_real_distribution<double> distribution(-0.25, 0.25);
        for (double& value : plane.reference) value = 0.45 + distribution(generator);
        const auto bilinearReference = [&](double x, double y) {
            if (x < 0.0 || y < 0.0 ||
                x > static_cast<double>(width - 1u) ||
                y > static_cast<double>(height - 1u)) {
                return 0.0;
            }
            const auto x0 = static_cast<std::uint64_t>(std::floor(x));
            const auto y0 = static_cast<std::uint64_t>(std::floor(y));
            const std::uint64_t x1 = std::min<std::uint64_t>(x0 + 1u, width - 1u);
            const std::uint64_t y1 = std::min<std::uint64_t>(y0 + 1u, height - 1u);
            const double tx = x - static_cast<double>(x0);
            const double ty = y - static_cast<double>(y0);
            const double top =
                (1.0 - tx) * plane.reference[y0 * width + x0] +
                tx * plane.reference[y0 * width + x1];
            const double bottom =
                (1.0 - tx) * plane.reference[y1 * width + x0] +
                tx * plane.reference[y1 * width + x1];
            return (1.0 - ty) * top + ty * bottom;
        };
        for (std::uint64_t y = 0u; y < height; ++y) {
            for (std::uint64_t x = 0u; x < width; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * width + x);
                if (lowTexture) {
                    plane.reference[index] = 0.25;
                    plane.alternate[index] = 0.25;
                } else if (periodic) {
                    plane.reference[index] =
                        ((x / 4u + y / 4u) & 1u) == 0u ? 0.2 : 0.8;
                    const auto shiftedX = static_cast<std::int64_t>(x) - 4;
                    plane.alternate[index] =
                        ((((shiftedX % 8) + 8) % 8) / 4 +
                            static_cast<std::int64_t>(y / 4u)) % 2 == 0
                        ? 0.2
                        : 0.8;
                } else {
                    plane.alternate[index] = bilinearReference(
                        static_cast<double>(x) - translationPlaneX,
                        static_cast<double>(y) - translationPlaneY);
                }
            }
        }
        planes.push_back(std::move(plane));
    }
    return planes;
}

bool ValidatePhaseTranslation() {
    bool ok = true;
    Raw::Mfd::PhaseCorrelationOptions options;
    options.minimumPeakToSidelobeRatio = 5.0;
    options.minimumPeakUniqueness = 0.01;
    for (const std::array<double, 2>& translation : {
            std::array<double, 2>{ -11.0, 7.0 },
            std::array<double, 2>{ 9.0, -10.0 },
            std::array<double, 2>{ 3.0, 4.0 } }) {
        Raw::Mfd::PhaseTranslationResult result;
        std::string error;
        const bool accepted = Raw::Mfd::EstimateMultichannelPhaseTranslation(
            MakeTranslatedPlanes(translation[0], translation[1]),
            options,
            result,
            &error);
        ok &= Check(accepted && result.accepted,
            "large-range multichannel translation failed: " + error);
        if (accepted) {
            const bool translationMatches = NearlyEqual(
                    result.translationRaw.x,
                    2.0 * translation[0],
                    0.45) &&
                NearlyEqual(
                        result.translationRaw.y,
                        2.0 * translation[1],
                        0.45) &&
                result.planes.size() == 4u;
            if (!translationMatches) {
                std::cerr << "MFD Phase 4 translation diagnostic: expected raw ("
                          << 2.0 * translation[0] << ", "
                          << 2.0 * translation[1] << "), measured ("
                          << result.translationRaw.x << ", "
                          << result.translationRaw.y << ")." << std::endl;
            }
            ok &= Check(translationMatches,
                "phase translation has wrong sign, scale, or CFA diagnostics");
        }
    }

    Raw::Mfd::PhaseTranslationResult subpixel;
    std::string error;
    const bool subpixelAccepted = Raw::Mfd::EstimateMultichannelPhaseTranslation(
        MakeTranslatedPlanes(2.35, -1.65),
        options,
        subpixel,
        &error);
    const bool subpixelMatches = subpixelAccepted &&
        NearlyEqual(subpixel.translationRaw.x, 4.70, 0.60) &&
        NearlyEqual(subpixel.translationRaw.y, -3.30, 0.60);
    if (subpixelAccepted && !subpixelMatches) {
        std::cerr << "MFD Phase 4 subpixel diagnostic: expected raw (4.7, -3.3), measured ("
                  << subpixel.translationRaw.x << ", "
                  << subpixel.translationRaw.y << ")." << std::endl;
    }
    ok &= Check(subpixelMatches,
        "quadratic phase peak did not resolve a subpixel translation: " + error);

    Raw::Mfd::PhaseTranslationResult lowTexture;
    error.clear();
    ok &= Check(!Raw::Mfd::EstimateMultichannelPhaseTranslation(
            MakeTranslatedPlanes(0.0, 0.0, true, false),
            options,
            lowTexture,
            &error) &&
            lowTexture.failure ==
                Raw::Mfd::PhaseTranslationFailure::InsufficientTexture,
        "low-texture planes produced a false global translation");

    Raw::Mfd::PhaseTranslationResult periodicA;
    Raw::Mfd::PhaseTranslationResult periodicB;
    error.clear();
    const bool periodicAcceptedA = Raw::Mfd::EstimateMultichannelPhaseTranslation(
        MakeTranslatedPlanes(0.0, 0.0, false, true),
        options,
        periodicA,
        &error);
    error.clear();
    const bool periodicAcceptedB = Raw::Mfd::EstimateMultichannelPhaseTranslation(
        MakeTranslatedPlanes(0.0, 0.0, false, true),
        options,
        periodicB,
        &error);
    ok &= Check(!periodicAcceptedA && !periodicAcceptedB &&
            periodicA.failure == periodicB.failure &&
            NearlyEqual(periodicA.translationRaw.x,
                periodicB.translationRaw.x, 0.0) &&
            NearlyEqual(periodicA.translationRaw.y,
                periodicB.translationRaw.y, 0.0),
        "periodic ambiguity was accepted or tie behavior was nondeterministic");
    return ok;
}

std::vector<Raw::Mfd::ExposureSample> MakeExposureSamples(
    double scale,
    bool movingContamination,
    bool blackOffset) {
    constexpr std::size_t count = 12000u;
    std::vector<Raw::Mfd::ExposureSample> samples;
    samples.reserve(count);
    for (std::size_t index = 0u; index < count; ++index) {
        const Raw::Mfd::CfaSite site = static_cast<Raw::Mfd::CfaSite>(index % 4u);
        const double unit = static_cast<double>((index * 7919u) % 10007u) /
            10007.0;
        const double alternate = 0.02 + 0.62 * unit;
        double reference = scale * alternate +
            0.0002 * std::sin(0.17 * static_cast<double>(index));
        if (movingContamination && index % 9u == 0u) {
            reference += (index % 18u == 0u) ? 0.18 : -0.14;
        }
        if (blackOffset && site == Raw::Mfd::CfaSite::Green1) {
            reference += 0.020;
        }
        Raw::Mfd::ExposureSample sample;
        sample.referenceValue = reference;
        sample.alternateValue = alternate;
        sample.referenceVariance = 0.000010;
        sample.alternateVariance = 0.000009;
        sample.usableCodeSpanDn = 4095.0;
        sample.offsetVariance = 0.000004;
        sample.site = site;
        samples.push_back(sample);
    }
    return samples;
}

bool ValidateExposureFitting() {
    bool ok = true;
    Raw::Mfd::RawRadiometricParameters parameters;
    Raw::Mfd::ExposureMetadataPrior metadata;
    metadata.available = true;
    metadata.trustworthy = true;
    metadata.scale = 1.24;
    Raw::Mfd::ProvisionalExposureResult provisional;
    std::string error;
    ok &= Check(Raw::Mfd::EstimateProvisionalExposureScale(
            MakeExposureSamples(1.25, false, false),
            metadata,
            parameters,
            provisional,
            &error) &&
            provisional.usedMetadata &&
            NearlyEqual(provisional.scale, 1.24, 1.0e-12),
        "trustworthy metadata did not initialize provisional exposure");

    metadata.available = false;
    error.clear();
    ok &= Check(Raw::Mfd::EstimateProvisionalExposureScale(
            MakeExposureSamples(1.25, false, false),
            metadata,
            parameters,
            provisional,
            &error) &&
            !provisional.usedMetadata &&
            NearlyEqual(provisional.scale, 1.25, 0.002),
        "deterministic median ratio did not initialize provisional exposure");

    metadata.available = true;
    metadata.trustworthy = true;
    metadata.scale = 1.24;
    Raw::Mfd::ExposureFitResult fit;
    error.clear();
    const bool fitAccepted = Raw::Mfd::FitGlobalExposureScale(
        MakeExposureSamples(1.25, true, false),
        12000u,
        metadata,
        parameters,
        fit,
        &error);
    ok &= Check(fitAccepted && fit.accepted &&
            NearlyEqual(fit.scale, 1.25, 0.006) &&
            fit.scaleVariance >= 0.0 &&
            fit.eligibleSampleCount >= 10000u,
        "robust exposure IRLS did not tolerate moving-object contamination: " + error);
    for (const Raw::Mfd::ExposureSiteDiagnostics& site : fit.sites) {
        ok &= Check(site.valid &&
                std::abs(site.scale / fit.scale - 1.0) <= 0.02 &&
                std::abs(site.intercept) <= site.interceptLimit,
            "accepted exposure fit lacks consistent per-CFA diagnostics");
    }

    Raw::Mfd::ExposureFitResult blackOffset;
    error.clear();
    ok &= Check(!Raw::Mfd::FitGlobalExposureScale(
            MakeExposureSamples(1.25, false, true),
            12000u,
            metadata,
            parameters,
            blackOffset,
            &error) &&
            (blackOffset.failure == Raw::Mfd::ExposureFitFailure::BlackOffset ||
                blackOffset.failure ==
                    Raw::Mfd::ExposureFitFailure::CfaScaleDisagreement),
        "per-CFA black-offset incompatibility was not rejected");
    return ok;
}

struct AnalyticImageSample {
    double value = 0.0;
    Raw::Mfd::RawSignalGradient gradient;
};

AnalyticImageSample AffineTexture(double x, double y) {
    AnalyticImageSample sample;
    sample.value = 0.40 +
        0.10 * std::sin(0.071 * x + 0.043 * y) +
        0.08 * std::cos(0.037 * x - 0.089 * y) +
        0.00025 * x + 0.00017 * y;
    sample.gradient.dxPerRawPixel =
        0.10 * 0.071 * std::cos(0.071 * x + 0.043 * y) -
        0.08 * 0.037 * std::sin(0.037 * x - 0.089 * y) +
        0.00025;
    sample.gradient.dyPerRawPixel =
        0.10 * 0.043 * std::cos(0.071 * x + 0.043 * y) +
        0.08 * 0.089 * std::sin(0.037 * x - 0.089 * y) +
        0.00017;
    return sample;
}

Raw::Mfd::AffineModel MakeTrueAffine() {
    const double angle = 2.0 * 3.14159265358979323846 / 180.0;
    const double scaleX = 1.012;
    const double scaleY = 0.993;
    Raw::Mfd::AffineModel model;
    model.centerRaw = { 63.5, 63.5 };
    model.linear = {
        scaleX * std::cos(angle),
        -scaleY * std::sin(angle) + 0.004,
        scaleX * std::sin(angle),
        scaleY * std::cos(angle)
    };
    model.translationRaw = { 3.4, -2.2 };
    return model;
}

bool ValidateAffineRefinementAndDecision() {
    bool ok = true;
    constexpr double exposureScale = 1.15;
    const Raw::Mfd::AffineModel truth = MakeTrueAffine();
    std::vector<Raw::Mfd::AffineReferenceSample> references;
    for (std::uint32_t y = 8u; y < 120u; y += 4u) {
        for (std::uint32_t x = 8u; x < 120u; x += 4u) {
            Raw::Mfd::AffineReferenceSample reference;
            reference.referenceRaw = {
                static_cast<double>(x),
                static_cast<double>(y)
            };
            reference.site = static_cast<Raw::Mfd::CfaSite>((x / 4u + y / 4u) % 4u);
            reference.referenceValue = exposureScale *
                AffineTexture(
                    truth.Map(reference.referenceRaw).x,
                    truth.Map(reference.referenceRaw).y).value;
            if ((x + y) % 36u == 0u) reference.referenceValue += 0.08;
            reference.referenceVariance = 0.000004;
            references.push_back(reference);
        }
    }
    Raw::Mfd::AffineSourceEvaluator evaluator = [](
        Raw::Mfd::CfaSite,
        Raw::Mfd::RawCoordinate coordinate,
        Raw::Mfd::AffineSourceSample& sample) {
        if (coordinate.x < 0.0 || coordinate.y < 0.0 ||
            coordinate.x > 127.0 || coordinate.y > 127.0) {
            return false;
        }
        const AnalyticImageSample analytic = AffineTexture(
            coordinate.x,
            coordinate.y);
        sample.value = analytic.value;
        sample.gradientRaw = analytic.gradient;
        sample.variance = 0.000004;
        return true;
    };
    Raw::Mfd::AffineModel seed;
    seed.centerRaw = truth.centerRaw;
    seed.translationRaw = { 3.0, -2.0 };
    Raw::Mfd::AffineRefinementOptions options;
    options.referenceExtent = { 128, 128 };
    options.alternateExtent = { 128, 128 };
    options.exposureScale = exposureScale;
    options.iterations = 12u;
    Raw::Mfd::AffineRefinementResult refined;
    std::string error;
    const bool affineAccepted = Raw::Mfd::RefineGlobalAffine(
        references,
        evaluator,
        seed,
        options,
        refined,
        &error);
    ok &= Check(affineAccepted && refined.acceptedAffine &&
            refined.finalObjective < refined.initialObjective &&
            refined.acceptedIterations > 0u,
        "robust affine refinement failed synthetic motion: " + error);
    if (affineAccepted) {
        for (std::size_t index = 0u; index < truth.linear.size(); ++index) {
            ok &= Check(NearlyEqual(
                    refined.model.linear[index], truth.linear[index], 0.004),
                "affine linear component did not converge to synthetic truth");
        }
        ok &= Check(NearlyEqual(
                refined.model.translationRaw.x,
                truth.translationRaw.x,
                0.35) &&
                NearlyEqual(
                    refined.model.translationRaw.y,
                    truth.translationRaw.y,
                    0.35) &&
                refined.overlapFraction >= 0.50 &&
                refined.normalCondition < options.maximumNormalCondition,
            "affine translation, overlap, or condition diagnostics are invalid");
        for (std::size_t diagonal = 0u; diagonal < 6u; ++diagonal) {
            ok &= Check(std::isfinite(
                    refined.parameterCovariance[diagonal * 6u + diagonal]) &&
                    refined.parameterCovariance[diagonal * 6u + diagonal] >= 0.0,
                "affine parameter covariance is missing or negative");
        }
    }

    Raw::Mfd::PhaseTranslationResult translation;
    translation.accepted = true;
    translation.translationRaw = seed.translationRaw;
    Raw::Mfd::ExposureFitResult exposure;
    exposure.accepted = true;
    exposure.scale = exposureScale;
    exposure.scaleVariance = 1.0e-6;
    Raw::Mfd::GlobalFrameAlignmentDecision decision =
        Raw::Mfd::DecideGlobalFrameAlignment(
            translation,
            exposure,
            refined,
            truth.centerRaw);
    ok &= Check(decision.accepted &&
            decision.choice == Raw::Mfd::GlobalFrameAlignmentChoice::Affine,
        "frame-level decision did not select an accepted affine model");

    Raw::Mfd::AffineRefinementResult fallback;
    fallback.translationFallbackAvailable = true;
    fallback.failure = Raw::Mfd::AffineRefinementFailure::ImplausibleLinearPart;
    decision = Raw::Mfd::DecideGlobalFrameAlignment(
        translation,
        exposure,
        fallback,
        truth.centerRaw);
    ok &= Check(decision.accepted &&
            decision.choice ==
                Raw::Mfd::GlobalFrameAlignmentChoice::Translation &&
            NearlyEqual(decision.model.translationRaw.x,
                translation.translationRaw.x, 0.0),
        "frame-level decision did not use an explicit translation fallback");
    translation.accepted = false;
    decision = Raw::Mfd::DecideGlobalFrameAlignment(
        translation,
        exposure,
        fallback,
        truth.centerRaw);
    ok &= Check(!decision.accepted &&
            decision.choice == Raw::Mfd::GlobalFrameAlignmentChoice::Reject,
        "frame-level decision silently used identity after translation failure");
    return ok;
}

} // namespace

bool ValidateMfdPhase4GlobalRegistration() {
    bool ok = true;
    ok &= ValidatePhaseTranslation();
    ok &= ValidateExposureFitting();
    ok &= ValidateAffineRefinementAndDecision();
    if (ok) {
        std::cout << "MFD Phase 4 global registration/exposure validation passed."
                  << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
