#include "Raw/MultiFrame/PreparedTileEstimator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Raw::MultiFrame {
namespace {

bool SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool ValidProfile(PreparedTileEstimatorProfile profile) {
    return profile == PreparedTileEstimatorProfile::FixedGaussian ||
        profile == PreparedTileEstimatorProfile::SharedPilotHuber;
}

bool ValidWeightSemantics(FixedGaussianWeightSemantics semantics) {
    return semantics == FixedGaussianWeightSemantics::BinaryInclusion ||
        semantics ==
            FixedGaussianWeightSemantics::CalibratedPrecisionMultiplier ||
        semantics == FixedGaussianWeightSemantics::PolicyAttenuation;
}

bool SameDomain(
    const CfaDomainIdentity& left,
    const CfaDomainIdentity& right) {
    return left.activePattern == right.activePattern &&
        left.sensorActiveArea.top == right.sensorActiveArea.top &&
        left.sensorActiveArea.left == right.sensorActiveArea.left &&
        left.sensorActiveArea.bottom == right.sensorActiveArea.bottom &&
        left.sensorActiveArea.right == right.sensorActiveArea.right &&
        left.activeExtent.width == right.activeExtent.width &&
        left.activeExtent.height == right.activeExtent.height &&
        left.phaseByParity == right.phaseByParity &&
        left.packedBayer == right.packedBayer &&
        left.preserveSignedValues == right.preserveSignedValues &&
        left.preservePositiveOverrange ==
            right.preservePositiveOverrange;
}

bool ValidateConfiguration(
    const PreparedTileEstimatorConfiguration& configuration,
    std::string* error) {
    if (configuration.contractVersion != kPreparedTileEstimatorVersion ||
        configuration.contractId != kPreparedTileEstimatorId ||
        !ValidProfile(configuration.profile) ||
        !ValidWeightSemantics(configuration.baseWeightSemantics)) {
        return SetError(error,
            "Prepared-tile estimator contract identity or profile is unsupported.");
    }
    if (configuration.modelIdentity.empty() ||
        configuration.geometryIdentity.empty() ||
        configuration.pilotIdentity.empty() ||
        !std::isfinite(configuration.numericalVarianceFloor) ||
        configuration.numericalVarianceFloor <= 0.0) {
        return SetError(error,
            "Prepared-tile estimator configuration is incomplete.");
    }
    if (configuration.profile ==
        PreparedTileEstimatorProfile::SharedPilotHuber &&
        (!std::isfinite(configuration.huberThreshold) ||
         configuration.huberThreshold <= 0.0 ||
         configuration.maximumHuberIterations == 0u ||
         !std::isfinite(configuration.absoluteHuberTolerance) ||
         configuration.absoluteHuberTolerance < 0.0 ||
         !std::isfinite(configuration.relativeHuberTolerance) ||
         configuration.relativeHuberTolerance < 0.0)) {
        return SetError(error,
            "Prepared-tile Huber settings are invalid.");
    }
    return true;
}

bool CheckedPixelCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() /
            extent.height) {
        return false;
    }
    count = static_cast<std::size_t>(extent.width * extent.height);
    return true;
}

bool TransformEvidenceToComparisonDomain(
    const SampleEvidence& source,
    double comparisonGain,
    SampleEvidence& destination,
    std::string* error) {
    destination = source;
    if (destination.bounds.lowerInclusive) {
        *destination.bounds.lowerInclusive *= comparisonGain;
        if (!std::isfinite(*destination.bounds.lowerInclusive)) {
            return SetError(error,
                "Prepared-tile lower evidence bound overflowed comparison space.");
        }
    }
    if (destination.bounds.upperInclusive) {
        *destination.bounds.upperInclusive *= comparisonGain;
        if (!std::isfinite(*destination.bounds.upperInclusive)) {
            return SetError(error,
                "Prepared-tile upper evidence bound overflowed comparison space.");
        }
    }
    return ValidateSampleEvidence(destination, error);
}

double FrozenComparisonVariance(
    const SiteNoiseProfile& profile,
    double comparisonGain,
    double predictedComparisonSignal,
    double varianceInflation,
    double numericalFloor) {
    const double nonnegativePilot = std::max(0.0, predictedComparisonSignal);
    const double sensorVariance = comparisonGain * profile.shotScale *
            nonnegativePilot +
        comparisonGain * comparisonGain * profile.offsetVariance;
    const double residualVariance =
        profile.residualModelTau0 * profile.residualModelTau0 +
        profile.residualModelTau1 * profile.residualModelTau1 *
            nonnegativePilot * nonnegativePilot;
    const double value = varianceInflation *
        (sensorVariance + residualVariance);
    return std::max(numericalFloor, value);
}

} // namespace

bool EstimatePreparedMeasurementTile(
    const PreparedTileEstimatorRequest& request,
    PreparedTileEstimatorResult& result,
    std::string* error) {
    if (!ValidateConfiguration(request.configuration, error)) return false;
    if (request.sources.empty() ||
        request.outputReferenceSourceIndex >= request.sources.size()) {
        return SetError(error,
            "Prepared-tile estimator requires sources and a valid output reference.");
    }

    const PreparedTileEstimatorSource& referenceInput =
        request.sources[request.outputReferenceSourceIndex];
    if (!referenceInput.prepared ||
        !ValidatePreparedMeasurementSource(*referenceInput.prepared, error)) {
        return SetError(error,
            "Prepared-tile output-reference source is invalid.");
    }
    const CfaDomainIdentity& domain = referenceInput.prepared->domain;

    std::vector<PreparedMeasurementTile> tiles(request.sources.size());
    for (std::size_t sourceIndex = 0;
         sourceIndex < request.sources.size();
         ++sourceIndex) {
        const PreparedTileEstimatorSource& input = request.sources[sourceIndex];
        if (!input.prepared || !input.noise ||
            !ValidatePreparedMeasurementSource(*input.prepared, error) ||
            !ValidateNoiseModelReference(*input.noise, error) ||
            input.noise->quality == NoiseModelQuality::Unavailable ||
            !SameDomain(domain, input.prepared->domain) ||
            !std::isfinite(input.exposureScale) ||
            input.exposureScale <= 0.0 ||
            !std::all_of(input.blackOffsetByPhase.begin(),
                input.blackOffsetByPhase.end(),
                [](double value) { return std::isfinite(value); }) ||
            !std::isfinite(input.reliability) ||
            input.reliability < 0.0 || input.reliability > 1.0 ||
            !std::isfinite(input.varianceInflation) ||
            input.varianceInflation < 1.0) {
            return SetError(error,
                "Prepared-tile estimator source calibration or contract is invalid.");
        }
        const TileReadStatus readStatus = input.prepared->tiles.Read(
            request.tileX, request.tileY, tiles[sourceIndex], error);
        if (readStatus != TileReadStatus::Hit) {
            return SetError(error,
                std::string("Prepared-tile source read failed with status ") +
                std::to_string(static_cast<unsigned int>(readStatus)) + ".");
        }
        if (!ValidatePreparedMeasurementTile(tiles[sourceIndex], error)) {
            return false;
        }
        if (sourceIndex > 0u &&
            (tiles[sourceIndex].originX != tiles[0].originX ||
             tiles[sourceIndex].originY != tiles[0].originY ||
             tiles[sourceIndex].extent.width != tiles[0].extent.width ||
             tiles[sourceIndex].extent.height != tiles[0].extent.height)) {
            return SetError(error,
                "Prepared-tile source extents do not match.");
        }
    }

    std::size_t pixelCount = 0;
    if (!CheckedPixelCount(tiles[0].extent, pixelCount) ||
        request.sharedPilotVirtualBayer.size() != pixelCount) {
        return SetError(error,
            "Prepared-tile shared pilot does not match the tile extent.");
    }
    for (const double pilot : request.sharedPilotVirtualBayer) {
        if (!std::isfinite(pilot)) {
            return SetError(error,
                "Prepared-tile shared pilot contains a non-finite value.");
        }
    }

    PreparedTileEstimatorResult working;
    working.profile = request.configuration.profile;
    working.originX = tiles[0].originX;
    working.originY = tiles[0].originY;
    working.extent = tiles[0].extent;
    working.virtualBayer.assign(pixelCount,
        std::numeric_limits<double>::quiet_NaN());
    working.variance.assign(pixelCount,
        std::numeric_limits<double>::infinity());
    working.modelQuadraticVariance.assign(pixelCount,
        std::numeric_limits<double>::quiet_NaN());
    working.policyConditionalSamplingVariance.assign(pixelCount,
        std::numeric_limits<double>::quiet_NaN());
    working.policyConditionalSamplingVarianceAvailable.assign(pixelCount, 0u);
    working.robustLinearizedVariance.assign(pixelCount,
        std::numeric_limits<double>::quiet_NaN());
    working.robustLinearizedVarianceAvailable.assign(pixelCount, 0u);
    working.finiteSampleCorrectedRobustVariance.assign(pixelCount,
        std::numeric_limits<double>::quiet_NaN());
    working.finiteSampleCorrectedRobustVarianceAvailable.assign(pixelCount, 0u);
    working.effectiveSupport.assign(pixelCount, 0.0);
    working.state.assign(pixelCount,
        FixedGaussianEstimateState::Unresolved);
    working.ownerSourceOrdinal.assign(pixelCount,
        std::numeric_limits<std::uint64_t>::max());
    working.ownerWeightFraction.assign(pixelCount, 0.0);
    working.minimumRobustFactor.assign(pixelCount, 1.0);

    std::vector<FixedGaussianObservation> observations(request.sources.size());
    std::vector<double> frozenVariance(request.sources.size(), 0.0);
    for (std::uint64_t localY = 0;
         localY < working.extent.height;
         ++localY) {
        for (std::uint64_t localX = 0;
             localX < working.extent.width;
             ++localX) {
            const std::size_t pixel = static_cast<std::size_t>(
                localY * working.extent.width + localX);
            const CfaPhase phase = PhaseAt(domain,
                working.originX + localX,
                working.originY + localY);
            const std::size_t phaseIndex = static_cast<std::size_t>(phase);
            const double referenceGain =
                tiles[request.outputReferenceSourceIndex]
                    .comparisonGain[pixel];
            if (!std::isfinite(referenceGain) || referenceGain <= 0.0) {
                return SetError(error,
                    "Prepared-tile reference comparison gain is invalid.");
            }
            const double pilotComparison =
                request.sharedPilotVirtualBayer[pixel] * referenceGain;

            for (std::size_t sourceIndex = 0;
                 sourceIndex < request.sources.size();
                 ++sourceIndex) {
                const PreparedTileEstimatorSource& input =
                    request.sources[sourceIndex];
                const PreparedMeasurementTile& tile = tiles[sourceIndex];
                const double gain = tile.comparisonGain[pixel];
                const SiteNoiseProfile& noise =
                    input.noise->sites[phaseIndex];
                FixedGaussianObservation observation;
                observation.preparedValue =
                    gain * static_cast<double>(tile.normalizedMosaic[pixel]);
                observation.blackOffset = input.blackOffsetByPhase[phaseIndex];
                observation.exposureScale = input.exposureScale;
                observation.reliability = input.reliability;
                observation.sourceOrdinal = input.sourceOrdinal;
                if (!TransformEvidenceToComparisonDomain(
                        tile.sampleEvidence[pixel], gain,
                        observation.evidence, error)) {
                    return false;
                }
                observation.frozenVariance = FrozenComparisonVariance(
                    noise,
                    gain,
                    input.exposureScale * pilotComparison,
                    input.varianceInflation,
                    request.configuration.numericalVarianceFloor);
                if (!std::isfinite(observation.frozenVariance) ||
                    observation.frozenVariance <= 0.0) {
                    return SetError(error,
                        "Prepared-tile frozen variance is invalid.");
                }
                observations[sourceIndex] = observation;
                frozenVariance[sourceIndex] = observation.frozenVariance;
            }

            double comparisonEstimate = 0.0;
            double comparisonVariance = 0.0;
            double modelComparisonVariance = 0.0;
            double policyComparisonVariance = 0.0;
            bool policyComparisonVarianceAvailable = false;
            double robustComparisonVariance = 0.0;
            bool robustComparisonVarianceAvailable = false;
            double finiteSampleComparisonVariance = 0.0;
            bool finiteSampleComparisonVarianceAvailable = false;
            double effectiveSupport = 0.0;
            FixedGaussianEstimateState estimateState =
                FixedGaussianEstimateState::Unresolved;
            bool outsideBounds = false;
            std::vector<double> robustFactors(request.sources.size(), 1.0);
            if (request.configuration.profile ==
                PreparedTileEstimatorProfile::FixedGaussian) {
                FixedGaussianConfiguration configuration;
                configuration.phase = phase;
                configuration.weightSemantics =
                    request.configuration.baseWeightSemantics;
                configuration.modelIdentity =
                    request.configuration.modelIdentity;
                configuration.geometryIdentity =
                    request.configuration.geometryIdentity;
                FixedGaussianEstimate estimate;
                if (!EstimateFixedGaussian(configuration, observations,
                        estimate, nullptr, error)) {
                    return false;
                }
                comparisonEstimate = estimate.estimate;
                comparisonVariance = estimate.conditionalSamplingVariance;
                effectiveSupport = estimate.effectiveSupport;
                estimateState = estimate.state;
                outsideBounds = estimate.estimateOutsideBounds;
            } else {
                SharedPilotHuberConfiguration configuration;
                configuration.phase = phase;
                configuration.baseWeightSemantics =
                    request.configuration.baseWeightSemantics;
                configuration.modelIdentity =
                    request.configuration.modelIdentity;
                configuration.geometryIdentity =
                    request.configuration.geometryIdentity;
                configuration.pilotIdentity =
                    request.configuration.pilotIdentity;
                configuration.initialPilot = pilotComparison;
                configuration.huberThreshold =
                    request.configuration.huberThreshold;
                configuration.maximumIterations =
                    request.configuration.maximumHuberIterations;
                configuration.absoluteTolerance =
                    request.configuration.absoluteHuberTolerance;
                configuration.relativeTolerance =
                    request.configuration.relativeHuberTolerance;
                SharedPilotHuberEstimate estimate;
                if (!EstimateSharedPilotHuber(configuration, observations,
                        estimate, error)) {
                    return false;
                }
                comparisonEstimate = estimate.estimate;
                modelComparisonVariance =
                    estimate.conditionalQuadraticVariance;
                policyComparisonVariance =
                    estimate.policyConditionalSamplingVariance;
                policyComparisonVarianceAvailable =
                    estimate.policyConditionalSamplingVarianceAvailable;
                robustComparisonVariance = estimate.robustLinearizedVariance;
                robustComparisonVarianceAvailable =
                    estimate.robustLinearizedVarianceAvailable;
                finiteSampleComparisonVariance =
                    estimate.finiteSampleCorrectedRobustVariance;
                finiteSampleComparisonVarianceAvailable =
                    estimate.finiteSampleCorrectedRobustVarianceAvailable;
                comparisonVariance =
                    estimate.publishedConservativeVarianceAvailable
                    ? estimate.publishedConservativeVariance
                    : estimate.conditionalQuadraticVariance;
                effectiveSupport = estimate.effectiveSupport;
                estimateState = estimate.state;
                outsideBounds = estimate.estimateOutsideBounds;
                working.allHuberPixelsConverged &=
                    estimate.state != FixedGaussianEstimateState::Estimate ||
                    estimate.converged;
                for (std::size_t sourceIndex = 0;
                     sourceIndex < estimate.observationDiagnostics.size();
                     ++sourceIndex) {
                    const auto& diagnostic =
                        estimate.observationDiagnostics[sourceIndex];
                    robustFactors[sourceIndex] = diagnostic.hasNumericResidual
                        ? diagnostic.robustFactor
                        : 0.0;
                    if (diagnostic.hasNumericResidual &&
                        diagnostic.robustFactor < 1.0 - 1.0e-12) {
                        ++working.robustAttenuatedSampleCount;
                    }
                }
            }

            working.state[pixel] = estimateState;
            if (estimateState == FixedGaussianEstimateState::Estimate) {
                working.virtualBayer[pixel] =
                    comparisonEstimate / referenceGain;
                working.variance[pixel] =
                    comparisonVariance / (referenceGain * referenceGain);
                if (request.configuration.profile ==
                    PreparedTileEstimatorProfile::SharedPilotHuber) {
                    working.modelQuadraticVariance[pixel] =
                        modelComparisonVariance /
                        (referenceGain * referenceGain);
                    if (policyComparisonVarianceAvailable) {
                        working.policyConditionalSamplingVariance[pixel] =
                            policyComparisonVariance /
                            (referenceGain * referenceGain);
                        working.policyConditionalSamplingVarianceAvailable[pixel] = 1u;
                    }
                    if (finiteSampleComparisonVarianceAvailable) {
                        working.finiteSampleCorrectedRobustVariance[pixel] =
                            finiteSampleComparisonVariance /
                            (referenceGain * referenceGain);
                        working.finiteSampleCorrectedRobustVarianceAvailable[pixel] = 1u;
                    }
                }
                if (robustComparisonVarianceAvailable) {
                    working.robustLinearizedVariance[pixel] =
                        robustComparisonVariance /
                        (referenceGain * referenceGain);
                    working.robustLinearizedVarianceAvailable[pixel] = 1u;
                }
                working.effectiveSupport[pixel] = effectiveSupport;
                ++working.estimatedPixelCount;
                if (outsideBounds)
                    ++working.estimateOutsideBoundsPixelCount;

                double totalWeight = 0.0;
                double ownerWeight = -1.0;
                std::size_t owner = 0u;
                double minimumRobust = 1.0;
                for (std::size_t sourceIndex = 0;
                     sourceIndex < observations.size();
                     ++sourceIndex) {
                    const auto& observation = observations[sourceIndex];
                    if (!IsNumericMeasurement(observation.evidence) ||
                        observation.reliability <= 0.0) {
                        continue;
                    }
                    const double weight = observation.reliability *
                        robustFactors[sourceIndex] *
                        observation.exposureScale *
                        observation.exposureScale /
                        frozenVariance[sourceIndex];
                    totalWeight += weight;
                    minimumRobust = std::min(
                        minimumRobust, robustFactors[sourceIndex]);
                    if (weight > ownerWeight) {
                        ownerWeight = weight;
                        owner = sourceIndex;
                    }
                }
                if (totalWeight > 0.0 && ownerWeight >= 0.0) {
                    working.ownerSourceOrdinal[pixel] =
                        request.sources[owner].sourceOrdinal;
                    working.ownerWeightFraction[pixel] =
                        ownerWeight / totalWeight;
                }
                working.minimumRobustFactor[pixel] = minimumRobust;
            } else if (estimateState ==
                FixedGaussianEstimateState::BoundedOnly) {
                ++working.boundedOnlyPixelCount;
            } else {
                ++working.unresolvedPixelCount;
            }
        }
    }

    result = std::move(working);
    if (error) error->clear();
    return true;
}

} // namespace Raw::MultiFrame
