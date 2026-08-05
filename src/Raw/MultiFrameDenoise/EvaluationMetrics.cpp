#include "Raw/MultiFrameDenoise/Evaluation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

namespace Raw::Mfd {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool Finite(double value) {
    return std::isfinite(value);
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    count = 0u;
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::uint64_t>::max() /
            extent.height) {
        return false;
    }
    const std::uint64_t samples = extent.width * extent.height;
    if (samples > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(samples);
    return true;
}

bool FiniteSamples(const std::vector<float>& samples) {
    return std::all_of(samples.begin(), samples.end(), [](float value) {
        return std::isfinite(value);
    });
}

bool UnitSamples(const std::vector<float>& samples) {
    return std::all_of(samples.begin(), samples.end(), [](float value) {
        return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
    });
}

bool OptionalSize(std::size_t expected, std::size_t actual) {
    return actual == 0u || actual == expected;
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

double Percentile(std::vector<double> values, double quantile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double position = std::clamp(quantile, 0.0, 1.0) *
        static_cast<double>(values.size() - 1u);
    const std::size_t low = static_cast<std::size_t>(std::floor(position));
    const std::size_t high = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(low);
    return values[low] * (1.0 - fraction) + values[high] * fraction;
}

bool Included(const std::vector<std::uint8_t>& mask, std::size_t index) {
    return mask.empty() || mask[index] != 0u;
}

struct ErrorAccumulator {
    std::uint64_t count = 0u;
    double sum = 0.0;
    double absoluteSum = 0.0;
    double squareSum = 0.0;

    void Add(double error) {
        ++count;
        sum += error;
        absoluteSum += std::abs(error);
        squareSum += error * error;
    }
};

struct CorrelationAccumulator {
    std::uint64_t count = 0u;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumYY = 0.0;
    double sumXY = 0.0;

    void Add(double x, double y) {
        ++count;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumYY += y * y;
        sumXY += x * y;
    }

    double Correlation() const {
        if (count < 2u) return 0.0;
        const double n = static_cast<double>(count);
        const double covariance = sumXY - sumX * sumY / n;
        const double varianceX = sumXX - sumX * sumX / n;
        const double varianceY = sumYY - sumY * sumY / n;
        if (varianceX <= 0.0 || varianceY <= 0.0) return 0.0;
        return covariance / std::sqrt(varianceX * varianceY);
    }
};

void FinalizeCfaAccumulator(
    CfaSite site,
    const ErrorAccumulator& accumulator,
    CfaErrorMetrics& result) {
    result.site = site;
    result.sampleCount = accumulator.count;
    if (accumulator.count == 0u) return;
    const double count = static_cast<double>(accumulator.count);
    result.meanBias = accumulator.sum / count;
    result.rmse = std::sqrt(accumulator.squareSum / count);
}

bool EvaluateRawMetrics(
    const MfdEvaluationInput& input,
    const CfaLayout& layout,
    RawFidelityMetrics& metrics) {
    ErrorAccumulator output;
    ErrorAccumulator reference;
    std::array<ErrorAccumulator, 4> cfa;
    std::vector<double> flatResiduals;
    std::vector<double> flatReferenceResiduals;
    flatResiduals.reserve(input.outputMosaic.size());
    flatReferenceResiduals.reserve(input.outputMosaic.size());
    double effectiveSampleSum = 0.0;
    std::uint64_t effectiveSampleCount = 0u;
    for (std::size_t index = 0u; index < input.outputMosaic.size(); ++index) {
        const double outputError =
            static_cast<double>(input.outputMosaic[index]) -
            input.noiseFreeReferenceMosaic[index];
        const double referenceError =
            static_cast<double>(input.noisyReferenceMosaic[index]) -
            input.noiseFreeReferenceMosaic[index];
        output.Add(outputError);
        reference.Add(referenceError);
        const std::uint64_t x =
            static_cast<std::uint64_t>(index) % input.extent.width;
        const std::uint64_t y =
            static_cast<std::uint64_t>(index) / input.extent.width;
        cfa[SiteIndex(layout.SiteAt(
            static_cast<std::int64_t>(x),
            static_cast<std::int64_t>(y)))].Add(outputError);
        if (Included(input.flatRegionMask, index)) {
            flatResiduals.push_back(outputError);
            flatReferenceResiduals.push_back(referenceError);
            if (input.fusionDiagnostics.size() == input.outputMosaic.size()) {
                effectiveSampleSum +=
                    input.fusionDiagnostics[index].effectiveSampleCount;
                ++effectiveSampleCount;
            }
        }
    }
    if (output.count == 0u) return false;
    const double count = static_cast<double>(output.count);
    metrics.available = true;
    metrics.sampleCount = output.count;
    metrics.meanBias = output.sum / count;
    metrics.meanAbsoluteError = output.absoluteSum / count;
    metrics.rmse = std::sqrt(output.squareSum / count);
    metrics.referenceRmse = std::sqrt(reference.squareSum / count);
    metrics.rmseImprovementRatio = metrics.rmse > 0.0
        ? metrics.referenceRmse / metrics.rmse
        : std::numeric_limits<double>::max();
    metrics.psnr = metrics.rmse > 0.0
        ? std::min(300.0, 20.0 * std::log10(1.0 / metrics.rmse))
        : 300.0;
    if (!flatResiduals.empty()) {
        const auto standardDeviation = [](const std::vector<double>& values) {
            double mean = 0.0;
            for (double value : values) mean += value;
            mean /= static_cast<double>(values.size());
            double squared = 0.0;
            for (double value : values) {
                const double centered = value - mean;
                squared += centered * centered;
            }
            return std::sqrt(squared / static_cast<double>(values.size()));
        };
        metrics.flatResidualStandardDeviation =
            standardDeviation(flatResiduals);
        metrics.referenceFlatResidualStandardDeviation =
            standardDeviation(flatReferenceResiduals);
        if (metrics.flatResidualStandardDeviation > 0.0) {
            metrics.measuredFlatNoiseReductionRatio =
                metrics.referenceFlatResidualStandardDeviation /
                metrics.flatResidualStandardDeviation;
        }
    }
    metrics.expectedFlatStandardDeviation =
        input.expectedOutputNoiseStandardDeviation;
    if (input.expectedOutputNoiseStandardDeviation > 0.0) {
        metrics.observedToExpectedNoiseRatio =
            metrics.flatResidualStandardDeviation /
            input.expectedOutputNoiseStandardDeviation;
    }
    if (effectiveSampleCount > 0u) {
        metrics.meanEffectiveSampleCount = effectiveSampleSum /
            static_cast<double>(effectiveSampleCount);
        metrics.idealIndependentNoiseReductionRatio =
            std::sqrt(metrics.meanEffectiveSampleCount);
        if (metrics.idealIndependentNoiseReductionRatio > 0.0) {
            metrics.measuredToIdealNoiseReductionRatio =
                metrics.measuredFlatNoiseReductionRatio /
                metrics.idealIndependentNoiseReductionRatio;
        }
    }
    CorrelationAccumulator horizontal;
    CorrelationAccumulator vertical;
    for (std::uint64_t y = 0u; y < input.extent.height; ++y) {
        for (std::uint64_t x = 0u; x < input.extent.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(
                y * input.extent.width + x);
            if (!Included(input.flatRegionMask, index)) continue;
            const double residual =
                static_cast<double>(input.outputMosaic[index]) -
                input.noiseFreeReferenceMosaic[index];
            if (x + 2u < input.extent.width) {
                const std::size_t neighbor = index + 2u;
                if (Included(input.flatRegionMask, neighbor)) {
                    horizontal.Add(
                        residual,
                        static_cast<double>(input.outputMosaic[neighbor]) -
                            input.noiseFreeReferenceMosaic[neighbor]);
                }
            }
            if (y + 2u < input.extent.height) {
                const std::size_t neighbor = index +
                    2u * static_cast<std::size_t>(input.extent.width);
                if (Included(input.flatRegionMask, neighbor)) {
                    vertical.Add(
                        residual,
                        static_cast<double>(input.outputMosaic[neighbor]) -
                            input.noiseFreeReferenceMosaic[neighbor]);
                }
            }
        }
    }
    metrics.horizontalSameCfaResidualCorrelation =
        horizontal.Correlation();
    metrics.verticalSameCfaResidualCorrelation = vertical.Correlation();
    metrics.byCfaSite.resize(4u);
    FinalizeCfaAccumulator(CfaSite::Red, cfa[0], metrics.byCfaSite[0]);
    FinalizeCfaAccumulator(CfaSite::Green0, cfa[1], metrics.byCfaSite[1]);
    FinalizeCfaAccumulator(CfaSite::Green1, cfa[2], metrics.byCfaSite[2]);
    FinalizeCfaAccumulator(CfaSite::Blue, cfa[3], metrics.byCfaSite[3]);
    return true;
}

bool EvaluateDetailMetrics(
    const MfdEvaluationInput& input,
    DetailRetentionMetrics& metrics) {
    if (input.extent.width < 5u || input.extent.height < 5u) return false;
    double truthGradient = 0.0;
    double outputGradient = 0.0;
    double truthHighFrequency = 0.0;
    double outputHighFrequency = 0.0;
    double maximumError = 0.0;
    for (std::uint64_t y = 2u; y + 2u < input.extent.height; ++y) {
        for (std::uint64_t x = 2u; x + 2u < input.extent.width; ++x) {
            const std::size_t center = static_cast<std::size_t>(
                y * input.extent.width + x);
            if (!Included(input.detailRegionMask, center)) continue;
            // Step by two raw pixels so every derivative remains on one CFA
            // site rather than measuring color differences between sites.
            const std::size_t left = center - 2u;
            const std::size_t right = center + 2u;
            const std::size_t up = center -
                2u * static_cast<std::size_t>(input.extent.width);
            const std::size_t down = center +
                2u * static_cast<std::size_t>(input.extent.width);
            const auto gradient = [&](const std::vector<float>& image) {
                const double dx = 0.25 *
                    (static_cast<double>(image[right]) - image[left]);
                const double dy = 0.25 *
                    (static_cast<double>(image[down]) - image[up]);
                return std::sqrt(dx * dx + dy * dy);
            };
            const auto laplacian = [&](const std::vector<float>& image) {
                return std::abs(
                    static_cast<double>(image[left]) + image[right] +
                    image[up] + image[down] - 4.0 * image[center]);
            };
            truthGradient += gradient(input.noiseFreeReferenceMosaic);
            outputGradient += gradient(input.outputMosaic);
            truthHighFrequency += laplacian(input.noiseFreeReferenceMosaic);
            outputHighFrequency += laplacian(input.outputMosaic);
            maximumError = std::max(
                maximumError,
                std::abs(static_cast<double>(input.outputMosaic[center]) -
                    input.noiseFreeReferenceMosaic[center]));
            ++metrics.evaluatedPixelCount;
        }
    }
    if (metrics.evaluatedPixelCount == 0u || truthGradient <= 1.0e-15 ||
        truthHighFrequency <= 1.0e-15) {
        return false;
    }
    metrics.available = true;
    metrics.gradientMagnitudeRetention = outputGradient / truthGradient;
    metrics.highFrequencyContrastRetention =
        outputHighFrequency / truthHighFrequency;
    metrics.maximumAbsoluteError = maximumError;
    return Finite(metrics.gradientMagnitudeRetention) &&
        Finite(metrics.highFrequencyContrastRetention);
}

bool EvaluateMotionMetrics(
    const MfdEvaluationInput& input,
    MotionMaskMetrics& metrics) {
    if (input.movingRegionMask.empty() ||
        input.fusionDiagnostics.size() != input.outputMosaic.size() ||
        input.alternateMomentMosaic.size() != input.outputMosaic.size()) {
        return false;
    }
    std::uint64_t accepted = 0u;
    std::uint64_t acceptedStatic = 0u;
    std::uint64_t movingAccepted = 0u;
    std::uint64_t leakage = 0u;
    double movingSquaredError = 0.0;
    for (std::size_t index = 0u; index < input.outputMosaic.size(); ++index) {
        const bool moving = input.movingRegionMask[index] != 0u;
        const bool alternateAccepted =
            input.fusionDiagnostics[index].contributingAlternateCount > 0u &&
            !input.fusionDiagnostics[index].exactReferenceCopy;
        if (moving) {
            ++metrics.movingPixelCount;
            if (alternateAccepted) ++movingAccepted;
            const double referenceError =
                static_cast<double>(input.outputMosaic[index]) -
                input.noiseFreeReferenceMosaic[index];
            movingSquaredError += referenceError * referenceError;
            const double alternateDistance = std::abs(
                static_cast<double>(input.outputMosaic[index]) -
                input.alternateMomentMosaic[index]);
            if (alternateDistance < std::abs(referenceError)) ++leakage;
        } else {
            ++metrics.staticPixelCount;
        }
        if (alternateAccepted) {
            ++accepted;
            if (!moving) ++acceptedStatic;
        }
    }
    if (metrics.movingPixelCount == 0u || metrics.staticPixelCount == 0u) {
        return false;
    }
    metrics.available = true;
    metrics.staticAcceptancePrecision = accepted > 0u
        ? static_cast<double>(acceptedStatic) / accepted
        : 1.0;
    metrics.staticAcceptanceRecall =
        static_cast<double>(acceptedStatic) / metrics.staticPixelCount;
    metrics.movingFalseMergeRate =
        static_cast<double>(movingAccepted) / metrics.movingPixelCount;
    metrics.movingRegionRmseToReferenceMoment = std::sqrt(
        movingSquaredError / metrics.movingPixelCount);
    metrics.temporalStateLeakageFraction =
        static_cast<double>(leakage) / metrics.movingPixelCount;
    return true;
}

bool EvaluateCalibrationMetrics(
    const std::vector<MotionCalibrationSample>& samples,
    CovarianceCalibrationMetrics& metrics) {
    if (samples.empty()) return false;
    std::vector<double> endpointErrors;
    endpointErrors.reserve(samples.size());
    std::array<std::uint64_t, 10> binCounts {};
    std::array<double, 10> binConfidence {};
    std::array<double, 10> binAccuracy {};
    std::uint64_t covered68 = 0u;
    std::uint64_t covered95 = 0u;
    std::uint64_t covered99 = 0u;
    std::uint64_t highConfidence = 0u;
    std::uint64_t catastrophic = 0u;
    for (const MotionCalibrationSample& sample : samples) {
        const double xx = sample.predictedCovariance.xxRawPixelsSquared;
        const double xy = sample.predictedCovariance.xyRawPixelsSquared;
        const double yy = sample.predictedCovariance.yyRawPixelsSquared;
        const double determinant = xx * yy - xy * xy;
        if (!Finite(sample.errorRawX) || !Finite(sample.errorRawY) ||
            !Finite(xx) || !Finite(xy) || !Finite(yy) ||
            !Finite(sample.confidence) || sample.confidence < 0.0 ||
            sample.confidence > 1.0 || xx <= 0.0 || yy <= 0.0 ||
            determinant <= 0.0) {
            return false;
        }
        const double mahalanobis =
            (yy * sample.errorRawX * sample.errorRawX -
                2.0 * xy * sample.errorRawX * sample.errorRawY +
                xx * sample.errorRawY * sample.errorRawY) /
            determinant;
        if (!Finite(mahalanobis) || mahalanobis < 0.0) return false;
        if (mahalanobis <= 2.30) ++covered68;
        if (mahalanobis <= 5.99) ++covered95;
        if (mahalanobis <= 9.21) ++covered99;
        const double endpoint = std::hypot(
            sample.errorRawX, sample.errorRawY);
        endpointErrors.push_back(endpoint);
        if (sample.confidence > 0.8) {
            ++highConfidence;
            if (endpoint > 1.0) ++catastrophic;
        }
        const std::size_t bin = std::min<std::size_t>(
            9u,
            static_cast<std::size_t>(sample.confidence * 10.0));
        ++binCounts[bin];
        binConfidence[bin] += sample.confidence;
        binAccuracy[bin] += endpoint <= 0.5 ? 1.0 : 0.0;
    }
    metrics.available = true;
    metrics.sampleCount = samples.size();
    const double count = static_cast<double>(samples.size());
    metrics.coverage68 = covered68 / count;
    metrics.coverage95 = covered95 / count;
    metrics.coverage99 = covered99 / count;
    metrics.coverage68AbsoluteError = std::abs(metrics.coverage68 - 0.68);
    metrics.coverage95AbsoluteError = std::abs(metrics.coverage95 - 0.95);
    metrics.coverage99AbsoluteError = std::abs(metrics.coverage99 - 0.99);
    metrics.medianEndpointErrorRawPixels =
        Percentile(endpointErrors, 0.50);
    metrics.percentile95EndpointErrorRawPixels =
        Percentile(endpointErrors, 0.95);
    metrics.highConfidenceCatastrophicErrorRate = highConfidence > 0u
        ? static_cast<double>(catastrophic) / highConfidence
        : 0.0;
    for (std::size_t bin = 0u; bin < binCounts.size(); ++bin) {
        if (binCounts[bin] == 0u) continue;
        const double binCount = static_cast<double>(binCounts[bin]);
        metrics.confidenceExpectedCalibrationError +=
            (binCount / count) * std::abs(
                binConfidence[bin] / binCount -
                binAccuracy[bin] / binCount);
    }
    return true;
}

bool ValidatePerformanceTrace(PerformanceMemoryTrace& performance) {
    if (!performance.available) return true;
    std::set<std::string> names;
    double stageTotal = 0.0;
    for (const PerformanceStageTrace& stage : performance.stages) {
        if (stage.stage.empty() || !Finite(stage.wallTimeMs) ||
            stage.wallTimeMs < 0.0 || !names.insert(stage.stage).second) {
            return false;
        }
        stageTotal += stage.wallTimeMs;
    }
    if (!Finite(performance.totalWallTimeMs) ||
        performance.totalWallTimeMs < 0.0 ||
        !Finite(performance.cancellationLatencyMs) ||
        performance.cancellationLatencyMs < 0.0 ||
        !Finite(performance.coldCacheWallTimeMs) ||
        performance.coldCacheWallTimeMs < 0.0 ||
        !Finite(performance.warmCacheWallTimeMs) ||
        performance.warmCacheWallTimeMs < 0.0) {
        return false;
    }
    if (performance.totalWallTimeMs == 0.0) {
        performance.totalWallTimeMs = stageTotal;
    }
    return !performance.stages.empty() &&
        performance.totalWallTimeMs > 0.0;
}

} // namespace

bool EvaluateMfdOutput(
    const MfdEvaluationInput& input,
    MfdEvaluationResult& result,
    std::string* error) {
    result = {};
    std::size_t sampleCount = 0u;
    CfaLayout layout;
    if (input.sampleId.empty() ||
        !CheckedSampleCount(input.extent, sampleCount) ||
        !CfaLayout::TryCreate(input.cfaPattern, layout) ||
        input.noiseFreeReferenceMosaic.size() != sampleCount ||
        input.noisyReferenceMosaic.size() != sampleCount ||
        input.outputMosaic.size() != sampleCount ||
        !OptionalSize(sampleCount, input.alternateMomentMosaic.size()) ||
        !OptionalSize(sampleCount, input.flatRegionMask.size()) ||
        !OptionalSize(sampleCount, input.detailRegionMask.size()) ||
        !OptionalSize(sampleCount, input.movingRegionMask.size()) ||
        !OptionalSize(sampleCount, input.reliabilityMap.size()) ||
        !OptionalSize(sampleCount, input.motionConfidenceMap.size()) ||
        !OptionalSize(sampleCount, input.fusionDiagnostics.size()) ||
        !FiniteSamples(input.noiseFreeReferenceMosaic) ||
        !FiniteSamples(input.noisyReferenceMosaic) ||
        !FiniteSamples(input.outputMosaic) ||
        (!input.alternateMomentMosaic.empty() &&
            !FiniteSamples(input.alternateMomentMosaic)) ||
        (!input.reliabilityMap.empty() &&
            !UnitSamples(input.reliabilityMap)) ||
        (!input.motionConfidenceMap.empty() &&
            !UnitSamples(input.motionConfidenceMap)) ||
        !Finite(input.expectedOutputNoiseStandardDeviation) ||
        input.expectedOutputNoiseStandardDeviation < 0.0) {
        return Fail(error, "MFD evaluation input contract is invalid.");
    }
    result.sampleId = input.sampleId;
    result.extent = input.extent;
    if (!EvaluateRawMetrics(input, layout, result.raw)) {
        return Fail(error, "MFD RAW fidelity evaluation failed.");
    }
    EvaluateDetailMetrics(input, result.detail);
    EvaluateMotionMetrics(input, result.motion);
    if (!input.motionCalibrationSamples.empty() &&
        !EvaluateCalibrationMetrics(
            input.motionCalibrationSamples, result.calibration)) {
        return Fail(error, "MFD motion calibration samples are invalid.");
    }
    result.performance = input.performance;
    if (!ValidatePerformanceTrace(result.performance)) {
        return Fail(error, "MFD performance trace is invalid.");
    }
    result.valid = true;
    result.message = "MFD corpus evaluation metrics are valid.";
    return true;
}

} // namespace Raw::Mfd
