#include "LocalMotionPatch.h"

#include "MotionCovariance.h"
#include "SameCfaSampler.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <utility>

namespace Raw::Mfd::Detail {
namespace {
constexpr std::array<CfaSite, 4> kSites {
    CfaSite::Red, CfaSite::Green0, CfaSite::Green1, CfaSite::Blue
};
bool Finite(double v) { return std::isfinite(v); }
bool Finite(RawCoordinate v) { return Finite(v.x) && Finite(v.y); }
std::size_t SiteIndex(CfaSite site) { return static_cast<std::size_t>(site); }
std::size_t PixelIndex(PixelExtent e, std::uint64_t x, std::uint64_t y) {
    return static_cast<std::size_t>(y * e.width + x);
}
bool CheckedSampleCount(PixelExtent e, std::size_t& count) {
    if (e.width == 0u || e.height == 0u) { count = 0u; return true; }
    if (e.width > std::numeric_limits<std::uint64_t>::max() / e.height ||
        e.width * e.height > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(e.width * e.height); return true;
}
double SquaredNorm(RawCoordinate v) { return v.x * v.x + v.y * v.y; }
RawCoordinate Add(RawCoordinate a, RawCoordinate b) { return {a.x + b.x, a.y + b.y}; }
RawCoordinate Subtract(RawCoordinate a, RawCoordinate b) { return {a.x - b.x, a.y - b.y}; }
RawCoordinate Scale(RawCoordinate a, double b) { return {a.x * b, a.y * b}; }

double HuberLoss(double residual, double delta) {
    const double magnitude = std::abs(residual);
    if (magnitude <= delta) return 0.5 * residual * residual;
    return delta * (magnitude - 0.5 * delta);
}

double HuberWeight(double residual, double delta) {
    const double magnitude = std::abs(residual);
    if (magnitude <= delta || magnitude <= 1.0e-30) return 1.0;
    return delta / magnitude;
}

double Median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values[middle];
    if ((values.size() & 1u) != 0u) return upper;
    std::nth_element(values.begin(), values.begin() + middle - 1u, values.end());
    return 0.5 * (values[middle - 1u] + upper);
}

struct LevelSample {
    double value = 0.0;
    double variance = 0.0;
    double gradientX = 0.0;
    double gradientY = 0.0;
};

// Select one common domain before fitting, rather than keeping every seed
// sample and then immobilizing an otherwise useful match at an image/mask
// edge. The box contains the complete allowed translation disk. All Keys
// footprints that any trial can use must remain measured and finite.
bool HasRefinementSupport(const CfaPyramidLevel& level, double x, double y, double radius) {
    if (!Finite(x) || !Finite(y) || !Finite(radius) || radius < 0.0 ||
        x - radius < 1.0 || y - radius < 1.0 ||
        x + radius >= static_cast<double>(level.extent.width) - 2.0 ||
        y + radius >= static_cast<double>(level.extent.height) - 2.0) return false;
    const auto firstX = static_cast<std::int64_t>(std::floor(x - radius)) - 1;
    const auto firstY = static_cast<std::int64_t>(std::floor(y - radius)) - 1;
    const auto lastX = static_cast<std::int64_t>(std::floor(x + radius)) + 2;
    const auto lastY = static_cast<std::int64_t>(std::floor(y + radius)) + 2;
    for (auto row = firstY; row <= lastY; ++row) {
        for (auto column = firstX; column <= lastX; ++column) {
            const auto index = PixelIndex(level.extent,
                static_cast<std::uint64_t>(column), static_cast<std::uint64_t>(row));
            if (level.validMask[index] == 0u || !Finite(level.signal[index]) ||
                !Finite(level.variance[index]) || level.variance[index] < 0.0) return false;
        }
    }
    return true;
}

template<bool gradients>
bool SampleLevelKeys(
    const CfaPyramidLevel& level,
    double x,
    double y,
    double keysParameter,
    LevelSample& sample) {
    // EvaluatePatch validates the immutable level once per patch. Candidate
    // scoring needs values and variance but no interpolation derivatives.
    if (!Finite(x) || !Finite(y)) return false;
    const std::int64_t baseX = static_cast<std::int64_t>(std::floor(x));
    const std::int64_t baseY = static_cast<std::int64_t>(std::floor(y));
    if (baseX < 1 || baseY < 1 ||
        baseX + 2 >= static_cast<std::int64_t>(level.extent.width) ||
        baseY + 2 >= static_cast<std::int64_t>(level.extent.height)) return false;
    std::array<double,4> weightsX{},derivativesX{};
    for (std::int64_t offsetX = -1; offsetX <= 2; ++offsetX) {
        const double distance=x-static_cast<double>(baseX+offsetX);
        weightsX[offsetX+1]=KeysBicubicKernel(distance,keysParameter);
        if constexpr(gradients) derivativesX[offsetX+1]=KeysBicubicKernelDerivative(distance,keysParameter);
    }
    sample = {};
    for (std::int64_t offsetY = -1; offsetY <= 2; ++offsetY) {
        const std::int64_t tapY = baseY + offsetY;
        const double wy = KeysBicubicKernel(
            y - static_cast<double>(tapY), keysParameter);
        double dwy=0;
        if constexpr(gradients) dwy = KeysBicubicKernelDerivative(
            y - static_cast<double>(tapY), keysParameter);
        for (std::int64_t offsetX = -1; offsetX <= 2; ++offsetX) {
            const std::int64_t tapX = baseX + offsetX;
            const std::size_t index = PixelIndex(
                level.extent,
                static_cast<std::uint64_t>(tapX),
                static_cast<std::uint64_t>(tapY));
            if (level.validMask[index] == 0u ||
                !Finite(level.signal[index]) ||
                !Finite(level.variance[index]) ||
                level.variance[index] < 0.0) {
                return false;
            }
            const double wx = weightsX[offsetX+1];
            const double coefficient = wx * wy;
            sample.value += coefficient * level.signal[index];
            sample.variance += coefficient * coefficient * level.variance[index];
            if constexpr(gradients) {
                sample.gradientX += derivativesX[offsetX+1] * wy * level.signal[index];
                sample.gradientY += wx * dwy * level.signal[index];
            }
        }
    }
    return Finite(sample.value) && Finite(sample.variance) &&
        sample.variance >= 0.0 && Finite(sample.gradientX) &&
        Finite(sample.gradientY);
}

} // namespace

bool ValidateLevel(const CfaPyramidLevel& level) {
    std::size_t count = 0u;
    return CheckedSampleCount(level.extent, count) && count > 0u &&
        level.signal.size() == count &&
        level.variance.size() == count &&
        level.validMask.size() == count &&
        Finite(level.rawPixelsPerLevelPixel) &&
        level.rawPixelsPerLevelPixel > 0.0;
}

bool PatchReferenceGeometry::Prepare(const LocalMotionDirectionRequest& input, RawCoordinate center,
    std::uint32_t levelIndex, std::uint32_t patchLevelPixels) {
    request = nullptr;
    validSampleCount = 0u;
    samples.clear();
    constexpr std::size_t maximumBytes = 128u * 1024u;
    const std::uint64_t area = std::uint64_t(patchLevelPixels) * patchLevelPixels;
    if (!input.reference || !input.source || !Finite(center) || area == 0u ||
        area > maximumBytes / (kSites.size() * sizeof(Sample))) return false;
    const double levelScale = std::ldexp(1.0, levelIndex);
    for (CfaSite site : kSites) {
        auto& geometry = sites[SiteIndex(site)];
        geometry.reference = FindCfaPyramidLevel(*input.reference, site, levelIndex);
        geometry.source = FindCfaPyramidLevel(*input.source, site, levelIndex);
        if (!geometry.reference || !geometry.source ||
            !ValidateLevel(*geometry.reference) || !ValidateLevel(*geometry.source)) return false;
        geometry.sourceOffset = input.source->layout.OffsetFor(site);
    }
    try {
        samples.resize(static_cast<std::size_t>(area * kSites.size()));
    } catch (const std::bad_alloc&) {
        return false; // Reuse is optional; retain the uncached computation.
    }
    for (CfaSite site : kSites) {
        const auto& reference = *sites[SiteIndex(site)].reference;
        const auto centerPlane = input.reference->layout.RawToPlane(center, site);
        const auto startX = static_cast<std::int64_t>(std::floor(
            centerPlane.x / levelScale - 0.5 * static_cast<double>(patchLevelPixels)));
        const auto startY = static_cast<std::int64_t>(std::floor(
            centerPlane.y / levelScale - 0.5 * static_cast<double>(patchLevelPixels)));
        for (std::uint32_t y = 0u; y < patchLevelPixels; ++y) {
            const auto referenceY = startY + y;
            if (referenceY < 0 || referenceY >= static_cast<std::int64_t>(reference.extent.height)) continue;
            for (std::uint32_t x = 0u; x < patchLevelPixels; ++x) {
                const auto referenceX = startX + x;
                if (referenceX < 0 || referenceX >= static_cast<std::int64_t>(reference.extent.width)) continue;
                const auto index = PixelIndex(reference.extent,
                    static_cast<std::uint64_t>(referenceX), static_cast<std::uint64_t>(referenceY));
                if (reference.validMask[index] == 0u || !Finite(reference.signal[index]) ||
                    !Finite(reference.variance[index]) || reference.variance[index] < 0.0) continue;
                const CfaPlaneCoordinate plane {
                    static_cast<double>(referenceX) * levelScale,
                    static_cast<double>(referenceY) * levelScale, site
                };
                auto& sample = samples[SiteIndex(site) * area + y * patchLevelPixels + x];
                sample.mappedSourceRaw = input.globalWarp.Map(input.reference->layout.PlaneToRaw(plane));
                sample.signal = reference.signal[index];
                sample.variance = reference.variance[index];
                ++validSampleCount;
            }
        }
    }
    request = &input;
    centerRaw = center;
    level = levelIndex;
    patch = patchLevelPixels;
    return true;
}

bool PatchReferenceGeometry::Matches(const LocalMotionDirectionRequest& input, RawCoordinate center,
    std::uint32_t levelIndex, std::uint32_t patchLevelPixels) const {
    return request == &input && centerRaw.x == center.x && centerRaw.y == center.y &&
        level == levelIndex && patch == patchLevelPixels;
}

bool EvaluatePatch(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    std::uint32_t levelIndex,
    RawCoordinate residualRaw,
    std::uint32_t patchLevelPixels,
    bool calculateNormal,
    bool calculateFlatPhotometricCost,
    PatchStatistics& statistics,
    const PatchReferenceGeometry* geometry,
    PatchEvaluation evaluation,
    FixedPatchSupport* support,
    PatchCostLimit* costLimit) {
    // Reset every statistic to its default without discarding patch scratch.
    auto residuals = std::move(statistics.residuals);
    statistics = {};
    statistics.residuals = std::move(residuals);
    statistics.residuals.clear();
    if (costLimit) {
        costLimit->exceeded = false;
        costLimit->unavailable = false;
        if (evaluation != PatchEvaluation::FixedObjective || calculateNormal ||
            calculateFlatPhotometricCost || std::isnan(costLimit->maximumMeanCost) ||
            costLimit->maximumMeanCost < 0.0 ||
            !Finite(request.options.registration.registrationHuberDelta) ||
            request.options.registration.registrationHuberDelta <= 0.0 ||
            !Finite(request.options.registration.candidateTieTolerance) ||
            request.options.registration.candidateTieTolerance < 0.0) return false;
    }
    if (!request.reference || !request.source || patchLevelPixels == 0u ||
        !Finite(centerRaw) || !Finite(residualRaw) ||
        !Finite(request.exposureScale) || request.exposureScale <= 0.0) {
        return false;
    }
    const double levelScale = std::ldexp(1.0, levelIndex);
    const double rawPixelsPerLevelPixel = std::ldexp(2.0, levelIndex);
    if (geometry && !geometry->Matches(request, centerRaw, levelIndex, patchLevelPixels)) geometry = nullptr;
    const auto area = static_cast<std::uint64_t>(patchLevelPixels) * patchLevelPixels;
    if (area > std::numeric_limits<std::size_t>::max() / kSites.size()) return false;
    statistics.nominalCount = area * kSites.size();
    const bool fixedSupport = evaluation == PatchEvaluation::FixedObjective ||
        evaluation == PatchEvaluation::PhysicalOnFixedSupport;
    if (evaluation != PatchEvaluation::Native && !support) return false;
    if (evaluation == PatchEvaluation::CaptureSupport) {
        support->denominator.assign(static_cast<std::size_t>(statistics.nominalCount), 0.0);
        support->validCount = 0u;
    } else if (fixedSupport && (support->denominator.size() != statistics.nominalCount ||
        support->validCount == 0u)) {
        return false;
    }
    // Matching geometry has already validated both levels and every reference
    // tap. No candidate displacement can recover an entirely invalid patch.
    // Keep the same empty statistics as the normal zero-valid-sample exit.
    if (geometry && geometry->validSampleCount == 0u) return true;
    statistics.residuals.reserve(
        calculateNormal ? static_cast<std::size_t>(statistics.nominalCount) : 0u);
    double robustCostSum = 0.0;
    double cappedSquaredSum = 0.0;
    std::vector<double> flatRatios;
    if (calculateFlatPhotometricCost) {
        flatRatios.reserve(static_cast<std::size_t>(statistics.nominalCount));
    }

    for (CfaSite site : kSites) {
        const auto* siteGeometry = geometry ? &geometry->sites[SiteIndex(site)] : nullptr;
        const CfaPyramidLevel* referenceLevel = siteGeometry ? siteGeometry->reference : FindCfaPyramidLevel(
            *request.reference, site, levelIndex);
        const CfaPyramidLevel* sourceLevel = siteGeometry ? siteGeometry->source : FindCfaPyramidLevel(
            *request.source, site, levelIndex);
        if (!siteGeometry && (!referenceLevel || !sourceLevel ||
            !ValidateLevel(*referenceLevel) || !ValidateLevel(*sourceLevel))) {
            return false;
        }
        const CfaPlaneCoordinate centerPlane = siteGeometry ? CfaPlaneCoordinate{} :
            request.reference->layout.RawToPlane(centerRaw, site);
        const double centerLevelX = centerPlane.x / levelScale;
        const double centerLevelY = centerPlane.y / levelScale;
        const std::int64_t startX = static_cast<std::int64_t>(
            std::floor(centerLevelX - 0.5 * static_cast<double>(patchLevelPixels)));
        const std::int64_t startY = static_cast<std::int64_t>(
            std::floor(centerLevelY - 0.5 * static_cast<double>(patchLevelPixels)));
        for (std::uint32_t patchY = 0u; patchY < patchLevelPixels; ++patchY) {
            if (costLimit && request.shouldCancel && request.shouldCancel()) return false;
            const std::int64_t referenceY = startY + patchY;
            if (!siteGeometry && (referenceY < 0 ||
                referenceY >= static_cast<std::int64_t>(referenceLevel->extent.height))) {
                continue;
            }
            for (std::uint32_t patchX = 0u; patchX < patchLevelPixels; ++patchX) {
                const auto supportIndex =
                    (SiteIndex(site) * std::size_t(patchLevelPixels) + patchY) * patchLevelPixels + patchX;
                // A candidate cannot improve by changing which measurements
                // contribute. Initially unavailable samples stay unavailable.
                if (fixedSupport && support->denominator[supportIndex] <= 0.0) continue;
                double referenceSignal, referenceVariance;
                RawCoordinate sourceRaw;
                if (siteGeometry) {
                    const auto& sample = geometry->samples[
                        (SiteIndex(site) * std::size_t(patchLevelPixels) + patchY) * patchLevelPixels + patchX];
                    if (sample.variance < 0.0) continue;
                    referenceSignal = sample.signal;
                    referenceVariance = sample.variance;
                    sourceRaw = sample.mappedSourceRaw;
                } else {
                    const std::int64_t referenceX = startX + patchX;
                    if (referenceX < 0 ||
                        referenceX >= static_cast<std::int64_t>(referenceLevel->extent.width)) {
                        continue;
                    }
                    const std::size_t referenceIndex = PixelIndex(
                        referenceLevel->extent,
                        static_cast<std::uint64_t>(referenceX),
                        static_cast<std::uint64_t>(referenceY));
                    if (referenceLevel->validMask[referenceIndex] == 0u ||
                        !Finite(referenceLevel->signal[referenceIndex]) ||
                        !Finite(referenceLevel->variance[referenceIndex]) ||
                        referenceLevel->variance[referenceIndex] < 0.0) {
                        continue;
                    }
                    const CfaPlaneCoordinate referencePlane {
                        static_cast<double>(referenceX) * levelScale,
                        static_cast<double>(referenceY) * levelScale,
                        site
                    };
                    const RawCoordinate referenceRaw =
                        request.reference->layout.PlaneToRaw(referencePlane);
                    sourceRaw = request.globalWarp.Map(referenceRaw);
                    referenceSignal = referenceLevel->signal[referenceIndex];
                    referenceVariance = referenceLevel->variance[referenceIndex];
                }
                sourceRaw = Add(sourceRaw, residualRaw);
                const CfaPlaneCoordinate sourcePlane = siteGeometry ? CfaPlaneCoordinate {
                    (sourceRaw.x - static_cast<double>(siteGeometry->sourceOffset.x)) * 0.5,
                    (sourceRaw.y - static_cast<double>(siteGeometry->sourceOffset.y)) * 0.5, site
                } : request.source->layout.RawToPlane(sourceRaw, site);
                if (evaluation == PatchEvaluation::CaptureSupport &&
                    !HasRefinementSupport(*sourceLevel, sourcePlane.x / levelScale, sourcePlane.y / levelScale,
                        request.options.registration.maximumDistanceFromDiscreteSeedPlanePixels / levelScale)) continue;
                LevelSample sourceSample;
                const bool sampled = (calculateNormal || calculateFlatPhotometricCost)
                    ? SampleLevelKeys<true>(*sourceLevel,sourcePlane.x/levelScale,sourcePlane.y/levelScale,
                        request.options.registration.keysBicubicParameter,sourceSample)
                    : SampleLevelKeys<false>(*sourceLevel,sourcePlane.x/levelScale,sourcePlane.y/levelScale,
                        request.options.registration.keysBicubicParameter,sourceSample);
                if (!sampled) {
                    if (costLimit) { costLimit->unavailable = true; continue; }
                    if (fixedSupport) return false;
                    continue;
                }
                const double variance =
                    referenceVariance +
                    request.exposureScale * request.exposureScale *
                        sourceSample.variance +
                    request.options.registration.localNumericalVarianceFloor;
                if (!Finite(variance) || variance <= 0.0) {
                    if (costLimit) { costLimit->unavailable = true; continue; }
                    if (fixedSupport) return false;
                    continue;
                }
                const double denominator = evaluation == PatchEvaluation::FixedObjective
                    ? support->denominator[supportIndex] : std::sqrt(variance);
                const double residual =
                    (referenceSignal -
                        request.exposureScale * sourceSample.value) /
                    denominator;
                if (!Finite(residual)) {
                    if (costLimit) { costLimit->unavailable = true; continue; }
                    if (fixedSupport) return false;
                    continue;
                }
                if (evaluation == PatchEvaluation::CaptureSupport) {
                    support->denominator[supportIndex] = denominator;
                }
                ++statistics.validCount;
                // Color contradictions must remain visible even when both
                // candidates exceed the robust solver's ordinary cost cap.
                statistics.siteSquared[SiteIndex(site)] += std::min(residual * residual,1e12);
                ++statistics.siteCount[SiteIndex(site)];
                robustCostSum += HuberLoss(
                    residual, request.options.registration.registrationHuberDelta);
                // Every remaining or unavailable contribution is nonnegative.
                // Once this lower bound exceeds the complete-domain limit,
                // the candidate cannot reduce the inherited uniqueness. The
                // tolerance makes pruning conservative near a numerical tie.
                if (costLimit && robustCostSum >
                    (costLimit->maximumMeanCost + request.options.registration.candidateTieTolerance) *
                        static_cast<double>(support->validCount)) {
                    costLimit->exceeded = true;
                    return true; // statistics.valid remains false.
                }
                cappedSquaredSum += std::min(
                    residual * residual,
                    request.options.registration.cappedResidualSquared);

                if (calculateFlatPhotometricCost) {
                    const double gradientRawX =
                        request.exposureScale * sourceSample.gradientX /
                        rawPixelsPerLevelPixel;
                    const double gradientRawY =
                        request.exposureScale * sourceSample.gradientY /
                        rawPixelsPerLevelPixel;
                    const double flatVariance =
                        request.options.registration.flatSafeCovarianceRawPixels *
                        request.options.registration.flatSafeCovarianceRawPixels *
                        (gradientRawX * gradientRawX +
                         gradientRawY * gradientRawY);
                    flatRatios.push_back(flatVariance / variance);
                }

                if (calculateNormal) {
                    const double jacobianX =
                        -request.exposureScale * sourceSample.gradientX /
                        denominator;
                    const double jacobianY =
                        -request.exposureScale * sourceSample.gradientY /
                        denominator;
                    const double weight = HuberWeight(
                        residual, request.options.registration.registrationHuberDelta);
                    statistics.hessianXX += weight * jacobianX * jacobianX;
                    statistics.hessianXY += weight * jacobianX * jacobianY;
                    statistics.hessianYY += weight * jacobianY * jacobianY;
                    statistics.gradientX += weight * jacobianX * residual;
                    statistics.gradientY += weight * jacobianY * residual;
                    statistics.residuals.push_back(residual);
                }
            }
        }
    }
    statistics.validFraction = statistics.nominalCount == 0u
        ? 0.0
        : static_cast<double>(statistics.validCount) /
            static_cast<double>(statistics.nominalCount);
    if (evaluation == PatchEvaluation::CaptureSupport) support->validCount = statistics.validCount;
    if ((costLimit && costLimit->unavailable) ||
        (fixedSupport && statistics.validCount != support->validCount)) return false;
    if (statistics.validCount == 0u) return true;
    statistics.robustCost = robustCostSum /
        static_cast<double>(statistics.validCount);
    statistics.cappedChiSquared = cappedSquaredSum /
        static_cast<double>(statistics.validCount);
    statistics.flatPhotometricCost = calculateFlatPhotometricCost
        ? Median(std::move(flatRatios))
        : 0.0;
    statistics.valid = Finite(statistics.robustCost) &&
        Finite(statistics.cappedChiSquared) &&
        (!calculateFlatPhotometricCost ||
         Finite(statistics.flatPhotometricCost));
    return true;
}

bool HessianEigenvalues(const PatchStatistics& statistics, double& low, double& high) {
    const double trace = statistics.hessianXX + statistics.hessianYY;
    const double difference = statistics.hessianXX - statistics.hessianYY;
    const double radius = std::sqrt(std::max(
        0.0,
        0.25 * difference * difference +
            statistics.hessianXY * statistics.hessianXY));
    low = 0.5 * trace - radius;
    high = 0.5 * trace + radius;
    return Finite(low) && Finite(high);
}

bool SolveNormalStep(
    const PatchStatistics& statistics,
    double damping,
    RawCoordinate& stepPlane) {
    const double xx = statistics.hessianXX + damping;
    const double xy = statistics.hessianXY;
    const double yy = statistics.hessianYY + damping;
    const double determinant = xx * yy - xy * xy;
    if (!Finite(determinant) || determinant <= 1.0e-20) return false;
    stepPlane.x = -(
        yy * statistics.gradientX - xy * statistics.gradientY) / determinant;
    stepPlane.y = -(
        -xy * statistics.gradientX + xx * statistics.gradientY) / determinant;
    return Finite(stepPlane);
}

bool RefineTileSubpixel(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    RawCoordinate discreteResidualRaw,
    RefinedTile& refined,
    const PatchStatistics* initialStatistics,
    FixedPatchSupport* initialSupport) {
    refined = {};
    if (request.shouldCancel && request.shouldCancel()) return false;
    RawCoordinate currentRaw = discreteResidualRaw;
    PatchStatistics current;
    FixedPatchSupport ownedSupport;
    FixedPatchSupport* support = initialSupport ? initialSupport : &ownedSupport;
    // Keep one objective for the entire bounded solve. Native interpolation
    // changes source variance with sample phase; recomputing its denominator
    // while using a signal-only Jacobian would optimize a different objective.
    // Huber influence weights still follow the current residual normally.
    if (initialStatistics && initialSupport) current = *initialStatistics;
    else if (!EvaluatePatch(
            request, centerRaw, 0u, currentRaw,
            request.options.registration.finestPatchPlanePixels,
            true, false, current, nullptr, PatchEvaluation::CaptureSupport, support)) {
        return false;
    }
    if (!current.valid || current.validCount != support->validCount ||
        current.validFraction < request.options.registration.minimumTileValidFraction ||
        current.validCount < request.options.registration.minimumStructuredSamples) {
        return false;
    }
    // Two reusable residual buffers, fixed denominators and one optional
    // reference cache are bounded per worker. Allocation failure propagates;
    // it never selects an easier alignment/quality policy.
    {
        PatchStatistics proposed;
        PatchReferenceGeometry referenceGeometry;
        const PatchReferenceGeometry* geometry = nullptr;
        bool preparedGeometry = false;
        bool moved = false;
        const auto& parameters = request.options.registration;
        // The old failure allowance retried an unchanged full/half step. Spend
        // that work on additional halvings instead, with a hard bounded tail.
        const std::uint32_t trialLimit = 2u + std::min(6u, parameters.maximumSubpixelCostIncreases);
        for (std::uint32_t iteration = 0u; iteration < parameters.subpixelIterations; ++iteration) {
            if (request.shouldCancel && request.shouldCancel()) return false;
            const double diagonalScale = std::max({current.hessianXX, current.hessianYY, 1.0});
            RawCoordinate stepPlane;
            if (!SolveNormalStep(current, parameters.localHessianAbsoluteDamping * diagonalScale, stepPlane)) break;
            const double stepLength = std::sqrt(SquaredNorm(stepPlane));
            if (stepLength > parameters.maximumSubpixelStepPlanePixels) {
                stepPlane = Scale(stepPlane, parameters.maximumSubpixelStepPlanePixels / stepLength);
            }
            if (std::sqrt(SquaredNorm(stepPlane)) < parameters.subpixelConvergencePlanePixels) break;
            const double directionalDerivative =
                current.gradientX * stepPlane.x + current.gradientY * stepPlane.y;
            if (!Finite(directionalDerivative) || directionalDerivative >= 0.0) break;
            if (!preparedGeometry) {
                preparedGeometry = true;
                if (referenceGeometry.Prepare(request, centerRaw, 0u, parameters.finestPatchPlanePixels)) {
                    geometry = &referenceGeometry;
                }
            }
            bool accepted = false;
            double stepFraction = 1.0;
            for (std::uint32_t trial = 0u; trial < trialLimit; ++trial, stepFraction *= 0.5) {
                if (request.shouldCancel && request.shouldCancel()) return false;
                const RawCoordinate proposedRaw = Add(currentRaw, Scale(stepPlane, 2.0 * stepFraction));
                if (std::sqrt(SquaredNorm(Subtract(proposedRaw, discreteResidualRaw))) >
                    2.0 * parameters.maximumDistanceFromDiscreteSeedPlanePixels) continue;
                // Every trial uses exactly the seed's sample identities and
                // noise denominators. A clipped/invalid/lost footprint rejects
                // the trial rather than reducing its mean cost artificially.
                if (!EvaluatePatch(request, centerRaw, 0u, proposedRaw,
                        parameters.finestPatchPlanePixels, true, false, proposed, geometry,
                        PatchEvaluation::FixedObjective, support) || !proposed.valid) continue;
                constexpr double sufficientDecrease = 1.0e-4;
                const double armijo = sufficientDecrease * stepFraction * directionalDerivative /
                    static_cast<double>(current.validCount);
                if (proposed.robustCost > current.robustCost + armijo + parameters.candidateTieTolerance) continue;
                currentRaw = proposedRaw;
                std::swap(current, proposed);
                accepted = true;
                moved = true;
                break;
            }
            // Repeating the same normal equations after all smaller proposals
            // failed cannot add information. Preserve the last accepted fit.
            if (!accepted) break;
        }
        if (moved) {
            // Reliability and uncertainty consume physical sensor variances at
            // the accepted coordinate, on the same measurement support. The
            // frozen optimization objective is not reported as a likelihood.
            if (request.shouldCancel && request.shouldCancel()) return false;
            if (!EvaluatePatch(request, centerRaw, 0u, currentRaw,
                    parameters.finestPatchPlanePixels, true, false, proposed, geometry,
                    PatchEvaluation::PhysicalOnFixedSupport, support) || !proposed.valid) return false;
            std::swap(current, proposed);
        }
    }
    if (std::sqrt(SquaredNorm(Subtract(currentRaw, discreteResidualRaw))) >
        2.0 * request.options.registration.maximumDistanceFromDiscreteSeedPlanePixels + 1.0e-9) return false;
    double low = 0.0;
    double high = 0.0;
    if (!HessianEigenvalues(current, low, high)) return false;
    refined.hessianCondition = low > 1.0e-20
        ? high / low
        : std::numeric_limits<double>::infinity();

    std::vector<double> centeredResiduals = current.residuals;
    const double residualMedian = Median(centeredResiduals);
    for (double& residual : centeredResiduals) {
        residual = std::abs(residual - residualMedian);
    }
    const double robustScale = 1.4826 * Median(std::move(centeredResiduals));
    const double scaleSquared = std::max(
        request.options.registration.covarianceResidualScaleFloor,
        robustScale * robustScale);
    const double xx = current.hessianXX +
        request.options.registration.localCovarianceRegularization;
    const double xy = current.hessianXY;
    const double yy = current.hessianYY +
        request.options.registration.localCovarianceRegularization;
    const double determinant = xx * yy - xy * xy;
    if (Finite(determinant) && determinant > 1.0e-20) {
        SymmetricRawCovariance covariance {
            4.0 * scaleSquared * yy / determinant,
            -4.0 * scaleSquared * xy / determinant,
            4.0 * scaleSquared * xx / determinant
        };
        if (!TryClampMotionCovariance(
                covariance,
                request.options.registration.warpCovarianceEigenvalueMinRawPixels,
                request.options.registration.warpCovarianceEigenvalueMaxRawPixels,
                refined.localCovarianceRaw)) {
            return false;
        }
        double covarianceLow = 0.0;
        double covarianceHigh = 0.0;
        if (!TryMotionCovarianceEigenvalues(
                refined.localCovarianceRaw, covarianceLow, covarianceHigh)) {
            return false;
        }
        refined.positionSigmaRaw = std::sqrt(std::max(
            0.0,
            0.5 * (refined.localCovarianceRaw.xxRawPixelsSquared +
                refined.localCovarianceRaw.yyRawPixelsSquared)));
    }
    refined.observable = low > 1.0e-20 &&
        refined.hessianCondition <=
            request.options.registration.localHessianConditionLimit &&
        refined.positionSigmaRaw <=
            request.options.registration.flatSafeUnobservableSigmaRawPixels;
    refined.valid = true;
    refined.residualRaw = currentRaw;
    refined.statistics = std::move(current);
    return true;
}


} // namespace Raw::Mfd::Detail
