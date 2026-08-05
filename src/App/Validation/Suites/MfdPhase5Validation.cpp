#include "App/Validation/ValidationSuites.h"

#include "Raw/MultiFrameDenoise/LocalMotion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace Stack::Validation {
namespace {

using SignalFunction = std::function<double(
    Raw::Mfd::RawCoordinate,
    Raw::Mfd::CfaSite)>;

bool Check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "MFD Phase 5 validation failed: " << message << std::endl;
    }
    return condition;
}

double SceneSignal(
    Raw::Mfd::RawCoordinate coordinate,
    Raw::Mfd::CfaSite site) {
    const double siteBias = 0.006 * static_cast<double>(
        static_cast<std::uint8_t>(site));
    return 0.43 + siteBias +
        0.11 * std::sin(0.071 * coordinate.x + 0.043 * coordinate.y) +
        0.09 * std::cos(0.037 * coordinate.x - 0.083 * coordinate.y) +
        0.04 * std::sin(0.129 * coordinate.x + 0.113 * coordinate.y) +
        0.025 * std::sin(0.317 * coordinate.x + 0.271 * coordinate.y) +
        0.022 * std::cos(0.239 * coordinate.x - 0.349 * coordinate.y) +
        0.015 * std::sin(0.511 * coordinate.x + 0.173 * coordinate.y);
}

Raw::Mfd::LocalMotionOptions TestOptions() {
    Raw::Mfd::LocalMotionOptions options;
    options.registration.finestPatchPlanePixels = 16u;
    options.registration.finestStridePlanePixels = 16u;
    options.registration.coarsePatchLevelPixels = 8u;
    options.registration.minimumStructuredSamples = 128u;
    options.registration.positionalConfidenceScaleRawPixels = 0.50;
    return options;
}

bool MakePyramid(
    Raw::Mfd::PixelExtent rawExtent,
    const SignalFunction& signal,
    double variance,
    Raw::Mfd::CfaPlanePyramid& pyramid,
    const Raw::Mfd::LocalMotionOptions& options,
    const std::function<bool(Raw::Mfd::RawCoordinate)>& valid = {}) {
    Raw::Mfd::CfaLayout layout;
    if (!Raw::Mfd::CfaLayout::TryCreate(Raw::CfaPattern::RGGB, layout)) return false;
    const std::array<Raw::Mfd::CfaSite, 4> sites {
        Raw::Mfd::CfaSite::Red,
        Raw::Mfd::CfaSite::Green0,
        Raw::Mfd::CfaSite::Green1,
        Raw::Mfd::CfaSite::Blue
    };
    std::array<Raw::Mfd::CfaPyramidBasePlane, 4> base;
    for (std::size_t siteIndex = 0u; siteIndex < sites.size(); ++siteIndex) {
        Raw::Mfd::CfaPyramidBasePlane& plane = base[siteIndex];
        plane.site = sites[siteIndex];
        plane.extent = layout.PlaneExtent(plane.site, rawExtent);
        const std::size_t count = static_cast<std::size_t>(
            plane.extent.width * plane.extent.height);
        plane.signal.resize(count);
        plane.variance.assign(count, variance);
        plane.validMask.assign(count, 1u);
        for (std::uint64_t y = 0u; y < plane.extent.height; ++y) {
            for (std::uint64_t x = 0u; x < plane.extent.width; ++x) {
                const Raw::Mfd::RawCoordinate raw = layout.PlaneToRaw({
                    static_cast<double>(x),
                    static_cast<double>(y),
                    plane.site
                });
                const std::size_t index = static_cast<std::size_t>(
                    y * plane.extent.width + x);
                plane.signal[index] = signal(raw, plane.site);
                if (valid && !valid(raw)) plane.validMask[index] = 0u;
            }
        }
    }
    std::string error;
    return Raw::Mfd::BuildCfaPlanePyramid(
        layout, rawExtent, base, options.registration, pyramid, &error);
}

Raw::Mfd::AffineModel IdentityModel(Raw::Mfd::PixelExtent extent) {
    Raw::Mfd::AffineModel model;
    model.centerRaw = {
        0.5 * static_cast<double>(extent.width - 1u),
        0.5 * static_cast<double>(extent.height - 1u)
    };
    return model;
}

bool ValidatePyramidConstruction() {
    bool ok = true;
    const Raw::Mfd::PixelExtent extent { 320u, 320u };
    Raw::Mfd::LocalMotionOptions options = TestOptions();
    Raw::Mfd::CfaPlanePyramid pyramid;
    ok &= Check(MakePyramid(
            extent,
            [](Raw::Mfd::RawCoordinate coordinate, Raw::Mfd::CfaSite site) {
                return 0.2 + 0.001 * coordinate.x + 0.002 * coordinate.y +
                    0.01 * static_cast<double>(static_cast<std::uint8_t>(site));
            },
            0.004,
            pyramid,
            options),
        "four-level CFA intensity/variance pyramid could not be built");
    if (!ok) return false;
    const Raw::Mfd::CfaPyramidLevel* base =
        Raw::Mfd::FindCfaPyramidLevel(pyramid, Raw::Mfd::CfaSite::Red, 0u);
    const Raw::Mfd::CfaPyramidLevel* level1 =
        Raw::Mfd::FindCfaPyramidLevel(pyramid, Raw::Mfd::CfaSite::Red, 1u);
    const Raw::Mfd::CfaPyramidLevel* level3 =
        Raw::Mfd::FindCfaPyramidLevel(pyramid, Raw::Mfd::CfaSite::Red, 3u);
    ok &= Check(base && level1 && level3 && pyramid.levelCount == 4u &&
            level1->extent.width == 80u && level3->extent.width == 20u &&
            level3->rawPixelsPerLevelPixel == 16.0,
        "CFA pyramid level dimensions or raw-pixel scale are wrong");
    if (!base || !level1 || !level3) return false;
    const double oneDimensionalSquaredSum = 70.0 / 256.0;
    const double expectedVariance = 0.004 * oneDimensionalSquaredSum *
        oneDimensionalSquaredSum;
    const std::size_t center = static_cast<std::size_t>(
        20u * level1->extent.width + 20u);
    ok &= Check(std::abs(level1->variance[center] - expectedVariance) < 1.0e-12,
        "pyramid variance did not use squared Gaussian coefficients");
    ok &= Check(level1->validMask[0u] == 0u &&
            level1->validMask[center] != 0u && level3->validMask[0u] == 0u,
        "reflected pyramid samples leaked confidence through the eroded mask");
    return ok;
}

struct RegistrationFixture {
    Raw::Mfd::PixelExtent extent { 320u, 320u };
    Raw::Mfd::LocalMotionOptions options = TestOptions();
    Raw::Mfd::CfaPlanePyramid reference;
    Raw::Mfd::CfaPlanePyramid alternate;
};

bool MakeTranslationFixture(
    Raw::Mfd::RawCoordinate translation,
    RegistrationFixture& fixture,
    double variance = 0.00008) {
    const bool referenceBuilt = MakePyramid(
        fixture.extent,
        SceneSignal,
        variance,
        fixture.reference,
        fixture.options);
    const bool alternateBuilt = MakePyramid(
        fixture.extent,
        [translation](Raw::Mfd::RawCoordinate coordinate, Raw::Mfd::CfaSite site) {
            return SceneSignal({
                coordinate.x - translation.x,
                coordinate.y - translation.y
            }, site);
        },
        variance,
        fixture.alternate,
        fixture.options);
    return referenceBuilt && alternateBuilt;
}

bool CovarianceContains(
    const Raw::Mfd::MotionNode& node,
    Raw::Mfd::RawCoordinate truth,
    double limit) {
    const double xx = node.covarianceRaw.xxRawPixelsSquared;
    const double xy = node.covarianceRaw.xyRawPixelsSquared;
    const double yy = node.covarianceRaw.yyRawPixelsSquared;
    const double determinant = xx * yy - xy * xy;
    if (!(determinant > 0.0)) return false;
    const double dx = node.residualRaw.x - truth.x;
    const double dy = node.residualRaw.y - truth.y;
    const double mahalanobis =
        (yy * dx * dx - 2.0 * xy * dx * dy + xx * dy * dy) /
        determinant;
    return std::isfinite(mahalanobis) && mahalanobis <= limit;
}

bool ValidateHierarchicalFlowAndCovariance() {
    bool ok = true;
    const Raw::Mfd::RawCoordinate truth { 11.35, -7.20 };
    RegistrationFixture fixture;
    ok &= Check(MakeTranslationFixture(truth, fixture),
        "synthetic translated CFA pyramids could not be built");
    Raw::Mfd::LocalMotionDirectionRequest request;
    request.reference = &fixture.reference;
    request.source = &fixture.alternate;
    request.globalWarp = IdentityModel(fixture.extent);
    request.options = fixture.options;
    Raw::Mfd::LocalMotionGrid grid;
    std::string error;
    ok &= Check(Raw::Mfd::EstimateLocalMotionDirection(
            request, grid, &error),
        "hierarchical local registration failed: " + error);
    if (!grid.valid) return false;
    std::vector<double> endpointErrors;
    std::size_t covarianceCovered = 0u;
    for (const Raw::Mfd::MotionNode& node : grid.nodes) {
        if (node.state != Raw::Mfd::MotionNodeState::Structured) continue;
        endpointErrors.push_back(std::hypot(
            node.residualRaw.x - truth.x,
            node.residualRaw.y - truth.y));
        if (CovarianceContains(node, truth, 5.991)) ++covarianceCovered;
        double determinant =
            node.covarianceRaw.xxRawPixelsSquared *
                node.covarianceRaw.yyRawPixelsSquared -
            node.covarianceRaw.xyRawPixelsSquared *
                node.covarianceRaw.xyRawPixelsSquared;
        ok &= Check(determinant > 0.0 && node.confidence >= 0.0 &&
                node.confidence <= 1.0,
            "structured tile covariance is not positive definite");
    }
    ok &= Check(endpointErrors.size() >= 4u,
        "hierarchical search produced too few structured tiles");
    if (!endpointErrors.empty()) {
        std::sort(endpointErrors.begin(), endpointErrors.end());
        const double median = endpointErrors[endpointErrors.size() / 2u];
        const double p95 = endpointErrors[static_cast<std::size_t>(
            0.95 * static_cast<double>(endpointErrors.size() - 1u))];
        if (!(median < 0.30 && p95 < 0.65)) {
            std::cerr << "MFD Phase 5 flow diagnostic: nodes="
                      << endpointErrors.size() << ", median=" << median
                      << ", p95=" << p95 << std::endl;
        }
        ok &= Check(median < 0.30 && p95 < 0.65,
            "synthetic local-flow endpoint error exceeds the Phase 5 guardrail");
        ok &= Check(static_cast<double>(covarianceCovered) /
                static_cast<double>(endpointErrors.size()) >= 0.75,
            "tile covariance is systematically optimistic on synthetic flow");
    }

    Raw::Mfd::LocalMotionFieldSample sample;
    double maximumSeamError = 0.0;
    double maximumStep = 0.0;
    Raw::Mfd::RawCoordinate previous {};
    bool hasPrevious = false;
    for (double x = grid.originRawX;
         x <= grid.originRawX +
            (grid.width - 1u) * grid.spacingRawX + 1.0e-9;
         x += 4.0) {
        error.clear();
        if (!Raw::Mfd::EvaluateLocalMotionField(
                grid,
                { x, grid.originRawY + 0.5 * (grid.height - 1u) * grid.spacingRawY },
                fixture.options,
                sample,
                &error)) {
            continue;
        }
        maximumSeamError = std::max(maximumSeamError, std::hypot(
            sample.residualRaw.x - truth.x,
            sample.residualRaw.y - truth.y));
        if (hasPrevious) {
            maximumStep = std::max(maximumStep, std::hypot(
                sample.residualRaw.x - previous.x,
                sample.residualRaw.y - previous.y));
        }
        previous = sample.residualRaw;
        hasPrevious = true;
    }
    if (!(hasPrevious && maximumSeamError < 0.70 && maximumStep < 0.20)) {
        std::cerr << "MFD Phase 5 interpolation diagnostic: max-error="
                  << maximumSeamError << ", max-step=" << maximumStep
                  << std::endl;
    }
    ok &= Check(hasPrevious && maximumSeamError < 0.70 && maximumStep < 0.20,
        "confidence interpolation introduced a static-image tile seam");
    return ok;
}

bool ValidateFlatSafeAndAperturePolicy() {
    bool ok = true;
    RegistrationFixture flat;
    const SignalFunction flatSignal = [](
        Raw::Mfd::RawCoordinate,
        Raw::Mfd::CfaSite site) {
        return 0.32 + 0.002 * static_cast<double>(
            static_cast<std::uint8_t>(site));
    };
    ok &= Check(MakePyramid(
            flat.extent, flatSignal, 0.0001, flat.reference, flat.options) &&
            MakePyramid(
                flat.extent, flatSignal, 0.0001, flat.alternate, flat.options),
        "flat-safe fixture could not be built");
    Raw::Mfd::LocalMotionDirectionRequest request;
    request.reference = &flat.reference;
    request.source = &flat.alternate;
    request.globalWarp = IdentityModel(flat.extent);
    request.globalWarp.translationRaw = { 4.0, -2.0 };
    request.options = flat.options;
    Raw::Mfd::LocalMotionGrid flatGrid;
    std::string error;
    ok &= Check(Raw::Mfd::EstimateLocalMotionDirection(
            request, flatGrid, &error) && flatGrid.flatSafeCount > 0u &&
            flatGrid.structuredCount == 0u,
        "noise-consistent flat sky/wall did not enter FlatSafe: " + error);
    for (const Raw::Mfd::MotionNode& node : flatGrid.nodes) {
        if (node.state != Raw::Mfd::MotionNodeState::FlatSafe) continue;
        if (!(std::hypot(node.residualRaw.x, node.residualRaw.y) < 1.0e-12 &&
                node.covarianceRaw.xxRawPixelsSquared >= 1.0)) {
            std::cerr << "MFD Phase 5 FlatSafe diagnostic: grid=("
                      << node.gridX << "," << node.gridY << "), residual=("
                      << node.residualRaw.x << "," << node.residualRaw.y
                      << "), covariance-xx="
                      << node.covarianceRaw.xxRawPixelsSquared << std::endl;
        }
        ok &= Check(std::hypot(node.residualRaw.x, node.residualRaw.y) < 1.0e-12 &&
                node.covarianceRaw.xxRawPixelsSquared >= 1.0,
            "FlatSafe did not retain the global prediction and broad covariance");
    }

    RegistrationFixture aperture;
    const SignalFunction stripe = [](
        Raw::Mfd::RawCoordinate coordinate,
        Raw::Mfd::CfaSite) {
        return 0.45 + 0.20 * std::sin(0.18 * coordinate.x);
    };
    ok &= Check(MakePyramid(
            aperture.extent, stripe, 1.0e-6,
            aperture.reference, aperture.options) &&
            MakePyramid(
                aperture.extent,
                [](Raw::Mfd::RawCoordinate coordinate, Raw::Mfd::CfaSite site) {
                    return 0.45 + 0.20 * std::sin(0.18 * coordinate.x) +
                        0.0 * static_cast<double>(static_cast<std::uint8_t>(site));
                },
                1.0e-6,
                aperture.alternate,
                aperture.options),
        "aperture-problem fixture could not be built");
    request.reference = &aperture.reference;
    request.source = &aperture.alternate;
    request.globalWarp = IdentityModel(aperture.extent);
    request.options = aperture.options;
    Raw::Mfd::LocalMotionGrid apertureGrid;
    error.clear();
    const bool apertureAccepted = Raw::Mfd::EstimateLocalMotionDirection(
        request, apertureGrid, &error);
    ok &= Check(!apertureAccepted || apertureGrid.flatSafeCount == 0u,
        "a high-gradient aperture problem was incorrectly classified FlatSafe");
    return ok;
}

bool ValidatePeriodicUniqueness() {
    RegistrationFixture fixture;
    const SignalFunction periodic = [](
        Raw::Mfd::RawCoordinate coordinate,
        Raw::Mfd::CfaSite) {
        return 0.45 + 0.18 * std::sin(3.14159265358979323846 * coordinate.x / 4.0) +
            0.16 * std::cos(3.14159265358979323846 * coordinate.y / 4.0);
    };
    bool ok = Check(MakePyramid(
            fixture.extent, periodic, 0.00002,
            fixture.reference, fixture.options) &&
            MakePyramid(
                fixture.extent, periodic, 0.00002,
                fixture.alternate, fixture.options),
        "periodic-texture fixture could not be built");
    Raw::Mfd::LocalMotionDirectionRequest request;
    request.reference = &fixture.reference;
    request.source = &fixture.alternate;
    request.globalWarp = IdentityModel(fixture.extent);
    request.options = fixture.options;
    Raw::Mfd::LocalMotionGrid first;
    Raw::Mfd::LocalMotionGrid second;
    std::string error;
    const bool firstAccepted = Raw::Mfd::EstimateLocalMotionDirection(
        request, first, &error);
    error.clear();
    const bool secondAccepted = Raw::Mfd::EstimateLocalMotionDirection(
        request, second, &error);
    ok &= Check(firstAccepted == secondAccepted &&
            first.nodes.size() == second.nodes.size(),
        "periodic candidate handling is nondeterministic");
    std::size_t ambiguous = 0u;
    for (std::size_t index = 0u; index < first.nodes.size(); ++index) {
        ok &= Check(first.nodes[index].state == second.nodes[index].state &&
                first.nodes[index].residualRaw.x == second.nodes[index].residualRaw.x &&
                first.nodes[index].residualRaw.y == second.nodes[index].residualRaw.y,
            "periodic tie-breaking changed between identical runs");
        if (first.nodes[index].rejectReason ==
            Raw::Mfd::MotionNodeRejectReason::AmbiguousMatch) {
            ++ambiguous;
        }
    }
    if (!(ambiguous > 0u && first.structuredCount == 0u)) {
        std::cerr << "MFD Phase 5 periodic diagnostic: ambiguous=" << ambiguous
                  << ", structured=" << first.structuredCount
                  << ", flat=" << first.flatSafeCount << std::endl;
    }
    ok &= Check(ambiguous > 0u && first.structuredCount == 0u,
        "periodic best/second-best ambiguity produced confident structured flow");
    return ok;
}

Raw::Mfd::LocalMotionGrid MakeManualBoundaryGrid(
    Raw::Mfd::RawCoordinate left,
    Raw::Mfd::RawCoordinate right,
    Raw::Mfd::RawCoordinate globalTranslation = {}) {
    Raw::Mfd::LocalMotionGrid grid;
    grid.valid = true;
    grid.referenceRawExtent = { 200u, 200u };
    grid.sourceRawExtent = { 200u, 200u };
    grid.globalWarp = IdentityModel(grid.referenceRawExtent);
    grid.globalWarp.translationRaw = globalTranslation;
    grid.width = 2u;
    grid.height = 2u;
    grid.originRawX = 50.0;
    grid.originRawY = 50.0;
    grid.spacingRawX = 100.0;
    grid.spacingRawY = 100.0;
    grid.nodes.resize(4u);
    for (std::uint32_t y = 0u; y < 2u; ++y) {
        for (std::uint32_t x = 0u; x < 2u; ++x) {
            Raw::Mfd::MotionNode& node = grid.nodes[y * 2u + x];
            node.gridX = x;
            node.gridY = y;
            node.centerRaw = { 50.0 + 100.0 * x, 50.0 + 100.0 * y };
            node.residualRaw = x == 0u ? left : right;
            node.covarianceRaw = { 0.01, 0.0, 0.01 };
            node.confidence = 1.0;
            node.state = Raw::Mfd::MotionNodeState::Structured;
        }
    }
    grid.structuredCount = 4u;
    return grid;
}

bool ValidateDepthBoundaryAndSourceBorder() {
    bool ok = true;
    Raw::Mfd::LocalMotionOptions options = TestOptions();
    const Raw::Mfd::PixelExtent extent { 320u, 320u };
    const Raw::Mfd::RawCoordinate leftTruth { -3.0, 0.5 };
    const Raw::Mfd::RawCoordinate rightTruth { 3.0, -0.5 };
    Raw::Mfd::CfaPlanePyramid depthReference;
    Raw::Mfd::CfaPlanePyramid depthAlternate;
    ok &= Check(MakePyramid(
            extent, SceneSignal, 0.00008, depthReference, options) &&
            MakePyramid(
                extent,
                [leftTruth, rightTruth](
                    Raw::Mfd::RawCoordinate coordinate,
                    Raw::Mfd::CfaSite site) {
                    const Raw::Mfd::RawCoordinate displacement =
                        coordinate.x < 160.0 ? leftTruth : rightTruth;
                    return SceneSignal({
                        coordinate.x - displacement.x,
                        coordinate.y - displacement.y
                    }, site);
                },
                0.00008,
                depthAlternate,
                options),
        "two-depth CFA motion fixture could not be built");
    Raw::Mfd::LocalMotionDirectionRequest depthRequest;
    depthRequest.reference = &depthReference;
    depthRequest.source = &depthAlternate;
    depthRequest.globalWarp = IdentityModel(extent);
    depthRequest.options = options;
    Raw::Mfd::LocalMotionGrid depthGrid;
    std::string error;
    ok &= Check(Raw::Mfd::EstimateLocalMotionDirection(
            depthRequest, depthGrid, &error),
        "two-depth local registration failed: " + error);
    std::size_t leftCount = 0u;
    std::size_t rightCount = 0u;
    for (const Raw::Mfd::MotionNode& node : depthGrid.nodes) {
        if (node.state != Raw::Mfd::MotionNodeState::Structured) continue;
        if (node.centerRaw.x < 110.0) {
            ++leftCount;
            ok &= Check(std::hypot(
                    node.residualRaw.x - leftTruth.x,
                    node.residualRaw.y - leftTruth.y) < 0.75,
                "left depth layer did not retain its local translation");
        } else if (node.centerRaw.x > 210.0) {
            ++rightCount;
            ok &= Check(std::hypot(
                    node.residualRaw.x - rightTruth.x,
                    node.residualRaw.y - rightTruth.y) < 0.75,
                "right depth layer did not retain its local translation");
        }
    }
    ok &= Check(leftCount > 0u && rightCount > 0u,
        "two-depth fixture did not produce structured support on both layers");

    Raw::Mfd::LocalMotionGrid boundary = MakeManualBoundaryGrid(
        { -2.0, 0.0 }, { 2.0, 0.0 });
    Raw::Mfd::LocalMotionFieldSample sample;
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateLocalMotionField(
            boundary, { 100.0, 100.0 }, options, sample, &error) &&
            sample.rejectReason ==
                Raw::Mfd::MotionNodeRejectReason::MotionFieldAmbiguous &&
            sample.disagreementSigmaRaw >
                options.registration.motionDisagreementHardLimitRawPixels,
        "depth-boundary flow disagreement was averaged instead of rejected");

    Raw::Mfd::LocalMotionGrid border = MakeManualBoundaryGrid(
        { 0.0, 0.0 }, { 0.0, 0.0 }, { -100.0, 0.0 });
    error.clear();
    ok &= Check(!Raw::Mfd::EvaluateLocalMotionField(
            border, { 50.0, 100.0 }, options, sample, &error) &&
            sample.rejectReason == Raw::Mfd::MotionNodeRejectReason::SourceBorder,
        "source-active-area boundary did not hard-invalidate the warp");
    return ok;
}

bool ValidateReverseClosureAndOcclusion() {
    bool ok = true;
    const Raw::Mfd::RawCoordinate truth { 5.25, -3.50 };
    RegistrationFixture fixture;
    ok &= Check(MakeTranslationFixture(truth, fixture),
        "bidirectional translation fixture could not be built");
    Raw::Mfd::BidirectionalLocalMotionRequest request;
    request.reference = &fixture.reference;
    request.alternate = &fixture.alternate;
    request.referenceToAlternate = IdentityModel(fixture.extent);
    request.exposureScale = 1.0;
    request.options = fixture.options;
    Raw::Mfd::BidirectionalLocalMotionResult bidirectional;
    std::string error;
    ok &= Check(Raw::Mfd::EstimateBidirectionalLocalMotion(
            request, bidirectional, &error) &&
            bidirectional.closureAcceptedCount > 0u,
        "forward/reverse translation closure failed: " + error);
    for (const Raw::Mfd::MotionNode& node : bidirectional.forward.nodes) {
        if (node.state != Raw::Mfd::MotionNodeState::Structured) continue;
        ok &= Check(node.forwardBackwardEuclideanRaw < 0.75 &&
                node.forwardBackwardMahalanobis <=
                    fixture.options.registration.forwardBackwardMahalanobisHardLimit,
            "accepted forward node violates reverse-closure limits");
    }

    RegistrationFixture occlusion;
    const bool referenceBuilt = MakePyramid(
        occlusion.extent,
        SceneSignal,
        0.00008,
        occlusion.reference,
        occlusion.options);
    const bool alternateBuilt = MakePyramid(
        occlusion.extent,
        [truth](Raw::Mfd::RawCoordinate coordinate, Raw::Mfd::CfaSite site) {
            if (coordinate.x > 125.0 && coordinate.x < 205.0 &&
                coordinate.y > 70.0 && coordinate.y < 250.0) {
                return 0.30 + 0.17 * std::sin(
                    0.31 * coordinate.x - 0.27 * coordinate.y +
                    static_cast<double>(static_cast<std::uint8_t>(site)));
            }
            return SceneSignal({
                coordinate.x - truth.x,
                coordinate.y - truth.y
            }, site);
        },
        0.00008,
        occlusion.alternate,
        occlusion.options);
    ok &= Check(referenceBuilt && alternateBuilt,
        "forward/backward occlusion fixture could not be built");
    request.reference = &occlusion.reference;
    request.alternate = &occlusion.alternate;
    request.referenceToAlternate = IdentityModel(occlusion.extent);
    request.options = occlusion.options;
    error.clear();
    Raw::Mfd::BidirectionalLocalMotionResult occluded;
    const bool occlusionAccepted = Raw::Mfd::EstimateBidirectionalLocalMotion(
        request, occluded, &error);
    ok &= Check(occlusionAccepted && occluded.closureAcceptedCount > 0u &&
            occluded.closureRejectedCount > 0u,
        "forward/backward consistency did not isolate a synthetic occlusion: " + error);
    return ok;
}

} // namespace

bool ValidateMfdPhase5LocalMotion() {
    bool ok = true;
    ok &= ValidatePyramidConstruction();
    ok &= ValidateHierarchicalFlowAndCovariance();
    ok &= ValidateFlatSafeAndAperturePolicy();
    ok &= ValidatePeriodicUniqueness();
    ok &= ValidateDepthBoundaryAndSourceBorder();
    ok &= ValidateReverseClosureAndOcclusion();
    if (ok) {
        std::cout << "MFD Phase 5 local motion/covariance validation passed."
                  << std::endl;
    }
    return ok;
}

} // namespace Stack::Validation
