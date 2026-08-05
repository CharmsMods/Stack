#include "Raw/MultiFrameDenoise/GlobalRegistration.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <numeric>
#include <utility>

namespace Raw::Mfd {
namespace {

using Complex = std::complex<double>;
constexpr double kPi = 3.141592653589793238462643383279502884;

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

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::size_t>::max() / extent.height) {
        return false;
    }
    const std::uint64_t product = extent.width * extent.height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

double Median(std::vector<double> values) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    const std::size_t middle = values.size() / 2u;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    const double upper = values[middle];
    if ((values.size() & 1u) != 0u) return upper;
    const double lower = *std::max_element(
        values.begin(), values.begin() + middle);
    return 0.5 * (lower + upper);
}

double RobustScale(const std::vector<double>& values) {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    const double median = Median(values);
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values) deviations.push_back(std::abs(value - median));
    return 1.482602218505602 * Median(std::move(deviations));
}

std::uint64_t NextPowerOfTwo(std::uint64_t value) {
    if (value == 0u || value > (1ull << 62u)) return 0u;
    --value;
    value |= value >> 1u;
    value |= value >> 2u;
    value |= value >> 4u;
    value |= value >> 8u;
    value |= value >> 16u;
    value |= value >> 32u;
    return value + 1u;
}

bool Fft1d(std::vector<Complex>& values, bool inverse) {
    const std::size_t size = values.size();
    if (size == 0u || (size & (size - 1u)) != 0u) return false;
    for (std::size_t index = 1u, reversed = 0u; index < size; ++index) {
        std::size_t bit = size >> 1u;
        while ((reversed & bit) != 0u) {
            reversed ^= bit;
            bit >>= 1u;
        }
        reversed ^= bit;
        if (index < reversed) std::swap(values[index], values[reversed]);
    }
    for (std::size_t length = 2u; length <= size; length <<= 1u) {
        const double angle = (inverse ? 2.0 : -2.0) * kPi /
            static_cast<double>(length);
        const Complex step(std::cos(angle), std::sin(angle));
        for (std::size_t base = 0u; base < size; base += length) {
            Complex twiddle(1.0, 0.0);
            for (std::size_t offset = 0u; offset < length / 2u; ++offset) {
                const Complex even = values[base + offset];
                const Complex odd = values[base + offset + length / 2u] * twiddle;
                values[base + offset] = even + odd;
                values[base + offset + length / 2u] = even - odd;
                twiddle *= step;
            }
        }
        if (length > size / 2u) break;
    }
    if (inverse) {
        const double reciprocal = 1.0 / static_cast<double>(size);
        for (Complex& value : values) value *= reciprocal;
    }
    return true;
}

bool Fft2d(
    std::vector<Complex>& values,
    std::size_t width,
    std::size_t height,
    bool inverse) {
    if (width == 0u || height == 0u || values.size() != width * height) {
        return false;
    }
    std::vector<Complex> scratch(std::max(width, height));
    for (std::size_t y = 0u; y < height; ++y) {
        std::copy_n(values.begin() + y * width, width, scratch.begin());
        scratch.resize(width);
        if (!Fft1d(scratch, inverse)) return false;
        std::copy_n(scratch.begin(), width, values.begin() + y * width);
        scratch.resize(std::max(width, height));
    }
    for (std::size_t x = 0u; x < width; ++x) {
        scratch.resize(height);
        for (std::size_t y = 0u; y < height; ++y) {
            scratch[y] = values[y * width + x];
        }
        if (!Fft1d(scratch, inverse)) return false;
        for (std::size_t y = 0u; y < height; ++y) {
            values[y * width + x] = scratch[y];
        }
        scratch.resize(std::max(width, height));
    }
    return true;
}

struct PlaneSpectrum {
    CfaSite site = CfaSite::Red;
    PixelExtent extent;
    std::size_t fftWidth = 0u;
    std::size_t fftHeight = 0u;
    double rawPixelsPerPlanePixel = 2.0;
    double validFraction = 0.0;
    std::uint64_t validSampleCount = 0u;
    double referenceVariance = 0.0;
    double alternateVariance = 0.0;
    double weight = 0.0;
    std::vector<Complex> normalizedCrossSpectrum;
};

bool BuildPlaneSpectrum(
    const GlobalPlanePair& pair,
    const PhaseCorrelationOptions& options,
    PlaneSpectrum& spectrum,
    PhasePlaneDiagnostics& diagnostics,
    std::string* error) {
    diagnostics = {};
    diagnostics.site = pair.site;
    std::size_t sampleCount = 0u;
    if (!KnownSite(pair.site) || !CheckedSampleCount(pair.extent, sampleCount) ||
        pair.reference.size() != sampleCount ||
        pair.alternate.size() != sampleCount ||
        (!pair.validMask.empty() && pair.validMask.size() != sampleCount) ||
        !Finite(pair.rawPixelsPerPlanePixel) ||
        pair.rawPixelsPerPlanePixel <= 0.0 ||
        !Finite(pair.userWeight) || pair.userWeight <= 0.0) {
        diagnostics.reason = "invalid-plane-contract";
        return SetError(error, "MFD phase-correlation plane contract is invalid.");
    }
    std::vector<double> referenceValues;
    std::vector<double> alternateValues;
    referenceValues.reserve(sampleCount);
    alternateValues.reserve(sampleCount);
    std::vector<std::uint8_t> commonValid(sampleCount, 0u);
    for (std::size_t index = 0u; index < sampleCount; ++index) {
        const bool valid = (pair.validMask.empty() || pair.validMask[index] != 0u) &&
            Finite(pair.reference[index]) && Finite(pair.alternate[index]);
        if (!valid) continue;
        commonValid[index] = 1u;
        referenceValues.push_back(pair.reference[index]);
        alternateValues.push_back(pair.alternate[index]);
    }
    diagnostics.validSampleCount = referenceValues.size();
    diagnostics.validFraction = static_cast<double>(referenceValues.size()) /
        static_cast<double>(sampleCount);
    if (diagnostics.validFraction < options.minimumValidFraction) {
        diagnostics.reason = "insufficient-valid-coverage";
        return SetError(error, "MFD phase-correlation plane has insufficient common coverage.");
    }
    const double referenceMean = Median(referenceValues);
    const double alternateMean = Median(alternateValues);
    double referenceVariance = 0.0;
    double alternateVariance = 0.0;
    for (std::size_t index = 0u; index < referenceValues.size(); ++index) {
        const double referenceDelta = referenceValues[index] - referenceMean;
        const double alternateDelta = alternateValues[index] - alternateMean;
        referenceVariance += referenceDelta * referenceDelta;
        alternateVariance += alternateDelta * alternateDelta;
    }
    referenceVariance /= static_cast<double>(referenceValues.size());
    alternateVariance /= static_cast<double>(alternateValues.size());
    diagnostics.referenceTextureVariance = referenceVariance;
    diagnostics.alternateTextureVariance = alternateVariance;
    if (!Finite(referenceVariance) || !Finite(alternateVariance) ||
        referenceVariance < options.minimumTextureVariance ||
        alternateVariance < options.minimumTextureVariance) {
        diagnostics.reason = "insufficient-texture";
        return SetError(error, "MFD phase-correlation plane has insufficient texture.");
    }

    if (pair.extent.width > (1ull << 61u) ||
        pair.extent.height > (1ull << 61u)) {
        diagnostics.reason = "transform-too-large";
        return SetError(error, "MFD phase-correlation dimensions exceed the safe FFT range.");
    }
    const std::uint64_t paddedWidth = NextPowerOfTwo(pair.extent.width * 2u);
    const std::uint64_t paddedHeight = NextPowerOfTwo(pair.extent.height * 2u);
    if (paddedWidth == 0u || paddedHeight == 0u ||
        paddedWidth > std::numeric_limits<std::size_t>::max() / paddedHeight ||
        paddedWidth * paddedHeight > options.maximumPaddedSampleCount) {
        diagnostics.reason = "transform-too-large";
        return SetError(error, "MFD phase-correlation padded transform is too large.");
    }
    const auto fftWidth = static_cast<std::size_t>(paddedWidth);
    const auto fftHeight = static_cast<std::size_t>(paddedHeight);
    std::vector<Complex> referenceSpectrum(fftWidth * fftHeight);
    std::vector<Complex> alternateSpectrum(fftWidth * fftHeight);
    for (std::uint64_t y = 0u; y < pair.extent.height; ++y) {
        const double windowY = pair.extent.height > 1u
            ? 0.5 * (1.0 - std::cos(
                2.0 * kPi * static_cast<double>(y) /
                static_cast<double>(pair.extent.height - 1u)))
            : 1.0;
        for (std::uint64_t x = 0u; x < pair.extent.width; ++x) {
            const std::size_t inputIndex = static_cast<std::size_t>(
                y * pair.extent.width + x);
            if (commonValid[inputIndex] == 0u) continue;
            const double windowX = pair.extent.width > 1u
                ? 0.5 * (1.0 - std::cos(
                    2.0 * kPi * static_cast<double>(x) /
                    static_cast<double>(pair.extent.width - 1u)))
                : 1.0;
            const double window = windowX * windowY;
            const std::size_t fftIndex = static_cast<std::size_t>(y) *
                fftWidth + static_cast<std::size_t>(x);
            referenceSpectrum[fftIndex] =
                window * (pair.reference[inputIndex] - referenceMean);
            alternateSpectrum[fftIndex] =
                window * (pair.alternate[inputIndex] - alternateMean);
        }
    }
    if (!Fft2d(referenceSpectrum, fftWidth, fftHeight, false) ||
        !Fft2d(alternateSpectrum, fftWidth, fftHeight, false)) {
        diagnostics.reason = "fft-failed";
        return SetError(error, "MFD phase-correlation FFT failed.");
    }
    spectrum = {};
    spectrum.site = pair.site;
    spectrum.extent = pair.extent;
    spectrum.fftWidth = fftWidth;
    spectrum.fftHeight = fftHeight;
    spectrum.rawPixelsPerPlanePixel = pair.rawPixelsPerPlanePixel;
    spectrum.validFraction = diagnostics.validFraction;
    spectrum.validSampleCount = diagnostics.validSampleCount;
    spectrum.referenceVariance = referenceVariance;
    spectrum.alternateVariance = alternateVariance;
    spectrum.weight = pair.userWeight * diagnostics.validFraction *
        std::sqrt(std::min(referenceVariance, alternateVariance));
    spectrum.normalizedCrossSpectrum.resize(fftWidth * fftHeight);
    for (std::size_t index = 0u;
         index < spectrum.normalizedCrossSpectrum.size();
         ++index) {
        const Complex cross = referenceSpectrum[index] *
            std::conj(alternateSpectrum[index]);
        const double magnitude = std::abs(cross);
        spectrum.normalizedCrossSpectrum[index] =
            magnitude > options.spectrumMagnitudeEpsilon
            ? cross / magnitude
            : Complex(0.0, 0.0);
    }
    diagnostics.weight = spectrum.weight;
    return true;
}

struct CorrelationPeak {
    bool valid = false;
    double shiftX = 0.0;
    double shiftY = 0.0;
    std::int64_t integerShiftX = 0;
    std::int64_t integerShiftY = 0;
    double peak = 0.0;
    double psr = 0.0;
    double uniqueness = 0.0;
    double overlap = 0.0;
};

std::size_t WrappedIndex(std::int64_t value, std::size_t size) {
    const std::int64_t signedSize = static_cast<std::int64_t>(size);
    std::int64_t wrapped = value % signedSize;
    if (wrapped < 0) wrapped += signedSize;
    return static_cast<std::size_t>(wrapped);
}

double CorrelationValue(
    const std::vector<Complex>& correlation,
    std::size_t fftWidth,
    std::size_t fftHeight,
    std::int64_t shiftX,
    std::int64_t shiftY) {
    const std::size_t x = WrappedIndex(shiftX, fftWidth);
    const std::size_t y = WrappedIndex(shiftY, fftHeight);
    return correlation[y * fftWidth + x].real();
}

bool FindCorrelationPeak(
    const std::vector<Complex>& normalizedSpectrum,
    PixelExtent extent,
    std::size_t fftWidth,
    std::size_t fftHeight,
    const PhaseCorrelationOptions& options,
    CorrelationPeak& peak) {
    if (normalizedSpectrum.size() != fftWidth * fftHeight) return false;
    std::vector<Complex> correlation = normalizedSpectrum;
    if (!Fft2d(correlation, fftWidth, fftHeight, true)) return false;
    double best = -std::numeric_limits<double>::infinity();
    std::int64_t bestX = 0;
    std::int64_t bestY = 0;
    double bestOverlap = 0.0;
    const auto maximumX = static_cast<std::int64_t>(extent.width - 1u);
    const auto maximumY = static_cast<std::int64_t>(extent.height - 1u);
    for (std::int64_t shiftY = -maximumY; shiftY <= maximumY; ++shiftY) {
        for (std::int64_t shiftX = -maximumX; shiftX <= maximumX; ++shiftX) {
            const double overlap =
                static_cast<double>(extent.width - std::abs(shiftX)) *
                static_cast<double>(extent.height - std::abs(shiftY)) /
                static_cast<double>(extent.width * extent.height);
            if (overlap < options.minimumOverlapFraction) continue;
            const double value = CorrelationValue(
                correlation, fftWidth, fftHeight, shiftX, shiftY);
            if (value > best) {
                best = value;
                bestX = shiftX;
                bestY = shiftY;
                bestOverlap = overlap;
            }
        }
    }
    if (!Finite(best)) return false;

    const auto quadraticOffset = [](double before, double center, double after) {
        const double denominator = before - 2.0 * center + after;
        if (!Finite(denominator) || std::abs(denominator) < 1.0e-18) return 0.0;
        return std::clamp(
            0.5 * (before - after) / denominator,
            -0.75,
            0.75);
    };
    const double subpixelX = quadraticOffset(
        CorrelationValue(correlation, fftWidth, fftHeight, bestX - 1, bestY),
        best,
        CorrelationValue(correlation, fftWidth, fftHeight, bestX + 1, bestY));
    const double subpixelY = quadraticOffset(
        CorrelationValue(correlation, fftWidth, fftHeight, bestX, bestY - 1),
        best,
        CorrelationValue(correlation, fftWidth, fftHeight, bestX, bestY + 1));

    double sidelobeSum = 0.0;
    double sidelobeSquared = 0.0;
    std::uint64_t sidelobeCount = 0u;
    double second = -std::numeric_limits<double>::infinity();
    for (std::int64_t shiftY = -maximumY; shiftY <= maximumY; ++shiftY) {
        for (std::int64_t shiftX = -maximumX; shiftX <= maximumX; ++shiftX) {
            const double overlap =
                static_cast<double>(extent.width - std::abs(shiftX)) *
                static_cast<double>(extent.height - std::abs(shiftY)) /
                static_cast<double>(extent.width * extent.height);
            if (overlap < options.minimumOverlapFraction) continue;
            if (std::abs(shiftX - bestX) <=
                    static_cast<std::int64_t>(options.sidelobeExclusionRadius) &&
                std::abs(shiftY - bestY) <=
                    static_cast<std::int64_t>(options.sidelobeExclusionRadius)) {
                continue;
            }
            const double value = CorrelationValue(
                correlation, fftWidth, fftHeight, shiftX, shiftY);
            sidelobeSum += value;
            sidelobeSquared += value * value;
            ++sidelobeCount;
            if (value > second) second = value;
        }
    }
    if (sidelobeCount == 0u) return false;
    const double mean = sidelobeSum / static_cast<double>(sidelobeCount);
    const double variance = std::max(
        0.0,
        sidelobeSquared / static_cast<double>(sidelobeCount) - mean * mean);
    const double standardDeviation = std::sqrt(variance);
    peak.valid = true;
    peak.integerShiftX = bestX;
    peak.integerShiftY = bestY;
    peak.shiftX = static_cast<double>(bestX) + subpixelX;
    peak.shiftY = static_cast<double>(bestY) + subpixelY;
    peak.peak = best;
    peak.psr = (best - mean) /
        std::max(standardDeviation, options.spectrumMagnitudeEpsilon);
    peak.uniqueness = (best - second) /
        std::max(std::abs(best), options.spectrumMagnitudeEpsilon);
    peak.overlap = bestOverlap;
    return Finite(peak.shiftX) && Finite(peak.shiftY) &&
        Finite(peak.psr) && Finite(peak.uniqueness);
}

bool ValidPhaseOptions(const PhaseCorrelationOptions& options) {
    return Finite(options.minimumValidFraction) &&
        options.minimumValidFraction > 0.0 &&
        options.minimumValidFraction <= 1.0 &&
        Finite(options.minimumTextureVariance) &&
        options.minimumTextureVariance > 0.0 &&
        Finite(options.minimumPeakToSidelobeRatio) &&
        options.minimumPeakToSidelobeRatio > 0.0 &&
        Finite(options.minimumPeakUniqueness) &&
        options.minimumPeakUniqueness >= 0.0 &&
        Finite(options.minimumOverlapFraction) &&
        options.minimumOverlapFraction > 0.0 &&
        options.minimumOverlapFraction <= 1.0 &&
        Finite(options.maximumPlaneDisagreementRawPixels) &&
        options.maximumPlaneDisagreementRawPixels > 0.0 &&
        Finite(options.spectrumMagnitudeEpsilon) &&
        options.spectrumMagnitudeEpsilon > 0.0 &&
        options.maximumPaddedSampleCount > 0u;
}

double HuberWeight(double residual, double delta) {
    const double magnitude = std::abs(residual);
    return magnitude <= delta || magnitude == 0.0
        ? 1.0
        : delta / magnitude;
}

double HuberLoss(double residual, double delta) {
    const double magnitude = std::abs(residual);
    return magnitude <= delta
        ? 0.5 * residual * residual
        : delta * (magnitude - 0.5 * delta);
}

bool ValidRadiometricParameters(const RawRadiometricParameters& parameters) {
    return parameters.exposureIrlsIterations > 0u &&
        Finite(parameters.exposureHuberDelta) &&
        parameters.exposureHuberDelta > 0.0 &&
        Finite(parameters.exposureFitSignalMin) &&
        Finite(parameters.exposureFitSignalMax) &&
        parameters.exposureFitSignalMin < parameters.exposureFitSignalMax &&
        parameters.exposureFitMinimumSamples > 0u &&
        Finite(parameters.exposureFitMinimumFraction) &&
        parameters.exposureFitMinimumFraction >= 0.0 &&
        parameters.exposureFitMinimumFraction <= 1.0 &&
        Finite(parameters.fittedScaleMin) &&
        Finite(parameters.fittedScaleMax) &&
        parameters.fittedScaleMin > 0.0 &&
        parameters.fittedScaleMin < parameters.fittedScaleMax &&
        Finite(parameters.metadataDeviationWarningEv) &&
        parameters.metadataDeviationWarningEv > 0.0 &&
        Finite(parameters.cfaScaleDisagreementFraction) &&
        parameters.cfaScaleDisagreementFraction >= 0.0;
}

bool EligibleExposureSample(
    const ExposureSample& sample,
    const RawRadiometricParameters& parameters) {
    return sample.valid && KnownSite(sample.site) &&
        Finite(sample.referenceValue) &&
        sample.referenceValue >= parameters.exposureFitSignalMin &&
        sample.referenceValue <= parameters.exposureFitSignalMax &&
        Finite(sample.alternateValue) && sample.alternateValue > 1.0e-12 &&
        Finite(sample.referenceVariance) && sample.referenceVariance >= 0.0 &&
        Finite(sample.alternateVariance) && sample.alternateVariance >= 0.0 &&
        Finite(sample.usableCodeSpanDn) && sample.usableCodeSpanDn > 0.0 &&
        Finite(sample.offsetVariance) && sample.offsetVariance >= 0.0;
}

bool FailExposure(
    ExposureFitResult& result,
    ExposureFitFailure failure,
    const std::string& message,
    std::string* error) {
    result.accepted = false;
    result.failure = failure;
    result.message = message;
    return SetError(error, message);
}

bool SolveLinear6(
    std::array<double, 36> matrix,
    std::array<double, 6> rhs,
    std::array<double, 6>& solution,
    double& condition) {
    double maximumPivot = 0.0;
    double minimumPivot = std::numeric_limits<double>::infinity();
    for (std::size_t column = 0u; column < 6u; ++column) {
        std::size_t pivotRow = column;
        double pivotMagnitude = std::abs(matrix[column * 6u + column]);
        for (std::size_t row = column + 1u; row < 6u; ++row) {
            const double candidate = std::abs(matrix[row * 6u + column]);
            if (candidate > pivotMagnitude) {
                pivotMagnitude = candidate;
                pivotRow = row;
            }
        }
        if (!Finite(pivotMagnitude) || pivotMagnitude <= 1.0e-18) return false;
        if (pivotRow != column) {
            for (std::size_t item = column; item < 6u; ++item) {
                std::swap(matrix[column * 6u + item],
                    matrix[pivotRow * 6u + item]);
            }
            std::swap(rhs[column], rhs[pivotRow]);
        }
        const double pivot = matrix[column * 6u + column];
        maximumPivot = std::max(maximumPivot, std::abs(pivot));
        minimumPivot = std::min(minimumPivot, std::abs(pivot));
        for (std::size_t row = column + 1u; row < 6u; ++row) {
            const double factor = matrix[row * 6u + column] / pivot;
            matrix[row * 6u + column] = 0.0;
            for (std::size_t item = column + 1u; item < 6u; ++item) {
                matrix[row * 6u + item] -= factor * matrix[column * 6u + item];
            }
            rhs[row] -= factor * rhs[column];
        }
    }
    for (std::size_t reverse = 0u; reverse < 6u; ++reverse) {
        const std::size_t row = 5u - reverse;
        double value = rhs[row];
        for (std::size_t column = row + 1u; column < 6u; ++column) {
            value -= matrix[row * 6u + column] * solution[column];
        }
        const double pivot = matrix[row * 6u + row];
        if (!Finite(pivot) || std::abs(pivot) <= 1.0e-18) return false;
        solution[row] = value / pivot;
    }
    condition = maximumPivot / std::max(minimumPivot, 1.0e-18);
    return std::all_of(solution.begin(), solution.end(), [](double value) {
        return Finite(value);
    }) && Finite(condition);
}

struct AffineNormalEvaluation {
    bool valid = false;
    std::uint64_t sampleCount = 0u;
    double objective = 0.0;
    std::array<double, 36> normal {};
    std::array<double, 6> gradient {};
    std::vector<double> standardizedResiduals;
};

AffineNormalEvaluation EvaluateAffineNormal(
    const std::vector<AffineReferenceSample>& referenceSamples,
    const AffineSourceEvaluator& sourceEvaluator,
    const AffineModel& model,
    const AffineRefinementOptions& options,
    bool buildNormal) {
    AffineNormalEvaluation evaluation;
    const double coordinateScale = std::max<double>(
        1.0,
        std::max(options.referenceExtent.width, options.referenceExtent.height));
    evaluation.standardizedResiduals.reserve(referenceSamples.size());
    for (const AffineReferenceSample& reference : referenceSamples) {
        if (!reference.valid || !KnownSite(reference.site) ||
            !Finite(reference.referenceRaw.x) ||
            !Finite(reference.referenceRaw.y) ||
            !Finite(reference.referenceValue) ||
            !Finite(reference.referenceVariance) ||
            reference.referenceVariance < 0.0) {
            continue;
        }
        const RawCoordinate sourceRaw = model.Map(reference.referenceRaw);
        AffineSourceSample source;
        if (!Finite(sourceRaw.x) || !Finite(sourceRaw.y) ||
            !sourceEvaluator(reference.site, sourceRaw, source) ||
            !Finite(source.value) || !Finite(source.variance) ||
            source.variance < 0.0 ||
            !Finite(source.gradientRaw.dxPerRawPixel) ||
            !Finite(source.gradientRaw.dyPerRawPixel)) {
            continue;
        }
        const double variance = reference.referenceVariance +
            options.exposureScale * options.exposureScale * source.variance +
            options.numericalVarianceFloor;
        if (!Finite(variance) || variance <= 0.0) continue;
        const double inverseSigma = 1.0 / std::sqrt(variance);
        const double residual = (reference.referenceValue -
            options.exposureScale * source.value) * inverseSigma;
        if (!Finite(residual)) continue;
        evaluation.objective += HuberLoss(residual, options.huberDelta);
        evaluation.standardizedResiduals.push_back(residual);
        ++evaluation.sampleCount;
        if (!buildNormal) continue;
        const double centeredX =
            (reference.referenceRaw.x - model.centerRaw.x) / coordinateScale;
        const double centeredY =
            (reference.referenceRaw.y - model.centerRaw.y) / coordinateScale;
        const double common = -options.exposureScale * inverseSigma;
        const double gradientX = common * source.gradientRaw.dxPerRawPixel;
        const double gradientY = common * source.gradientRaw.dyPerRawPixel;
        const std::array<double, 6> jacobian {
            gradientX * centeredX,
            gradientX * centeredY,
            gradientY * centeredX,
            gradientY * centeredY,
            gradientX,
            gradientY
        };
        const double robustWeight = HuberWeight(residual, options.huberDelta);
        for (std::size_t row = 0u; row < 6u; ++row) {
            evaluation.gradient[row] +=
                robustWeight * jacobian[row] * residual;
            for (std::size_t column = 0u; column < 6u; ++column) {
                evaluation.normal[row * 6u + column] +=
                    robustWeight * jacobian[row] * jacobian[column];
            }
        }
    }
    evaluation.valid = evaluation.sampleCount >= options.minimumSamples &&
        Finite(evaluation.objective);
    return evaluation;
}

AffineModel ApplyAffineDelta(
    const AffineModel& model,
    const std::array<double, 6>& delta,
    PixelExtent referenceExtent) {
    AffineModel updated = model;
    const double coordinateScale = std::max<double>(
        1.0,
        std::max(referenceExtent.width, referenceExtent.height));
    updated.linear[0] += delta[0] / coordinateScale;
    updated.linear[1] += delta[1] / coordinateScale;
    updated.linear[2] += delta[2] / coordinateScale;
    updated.linear[3] += delta[3] / coordinateScale;
    updated.translationRaw.x += delta[4];
    updated.translationRaw.y += delta[5];
    return updated;
}

double AffineOverlapFraction(
    const AffineModel& model,
    PixelExtent referenceExtent,
    PixelExtent alternateExtent) {
    if (referenceExtent.width == 0u || referenceExtent.height == 0u ||
        alternateExtent.width == 0u || alternateExtent.height == 0u) {
        return 0.0;
    }
    constexpr std::uint32_t grid = 64u;
    std::uint64_t inside = 0u;
    std::uint64_t total = 0u;
    for (std::uint32_t y = 0u; y < grid; ++y) {
        const double referenceY = (static_cast<double>(y) + 0.5) *
            static_cast<double>(referenceExtent.height) /
            static_cast<double>(grid) - 0.5;
        for (std::uint32_t x = 0u; x < grid; ++x) {
            const double referenceX = (static_cast<double>(x) + 0.5) *
                static_cast<double>(referenceExtent.width) /
                static_cast<double>(grid) - 0.5;
            const RawCoordinate source = model.Map({ referenceX, referenceY });
            if (source.x >= 0.0 && source.y >= 0.0 &&
                source.x <= static_cast<double>(alternateExtent.width - 1u) &&
                source.y <= static_cast<double>(alternateExtent.height - 1u)) {
                ++inside;
            }
            ++total;
        }
    }
    return static_cast<double>(inside) / static_cast<double>(total);
}

void AffineLinearDiagnostics(
    const AffineModel& model,
    double& singularMinimum,
    double& singularMaximum,
    double& determinant,
    double& rotationDegrees) {
    const double a = model.linear[0];
    const double b = model.linear[1];
    const double c = model.linear[2];
    const double d = model.linear[3];
    determinant = a * d - b * c;
    const double ata00 = a * a + c * c;
    const double ata01 = a * b + c * d;
    const double ata11 = b * b + d * d;
    const double trace = ata00 + ata11;
    const double discriminant = std::sqrt(std::max(
        0.0,
        (ata00 - ata11) * (ata00 - ata11) + 4.0 * ata01 * ata01));
    singularMaximum = std::sqrt(std::max(0.0, 0.5 * (trace + discriminant)));
    singularMinimum = std::sqrt(std::max(0.0, 0.5 * (trace - discriminant)));
    rotationDegrees = std::atan2(c - b, a + d) * 180.0 / kPi;
}

bool InvertNormal6(
    const std::array<double, 36>& normal,
    std::array<double, 36>& inverse,
    double damping,
    double& condition) {
    inverse.fill(0.0);
    double worstCondition = 0.0;
    for (std::size_t column = 0u; column < 6u; ++column) {
        std::array<double, 36> matrix = normal;
        for (std::size_t diagonal = 0u; diagonal < 6u; ++diagonal) {
            matrix[diagonal * 6u + diagonal] += damping;
        }
        std::array<double, 6> rhs {};
        rhs[column] = 1.0;
        std::array<double, 6> solution {};
        double columnCondition = 0.0;
        if (!SolveLinear6(matrix, rhs, solution, columnCondition)) return false;
        worstCondition = std::max(worstCondition, columnCondition);
        for (std::size_t row = 0u; row < 6u; ++row) {
            inverse[row * 6u + column] = solution[row];
        }
    }
    condition = worstCondition;
    return true;
}

bool ValidAffineOptions(const AffineRefinementOptions& options) {
    return options.referenceExtent.width > 0u &&
        options.referenceExtent.height > 0u &&
        options.alternateExtent.width > 0u &&
        options.alternateExtent.height > 0u &&
        Finite(options.exposureScale) && options.exposureScale > 0.0 &&
        options.iterations > 0u && Finite(options.huberDelta) &&
        options.huberDelta > 0.0 && Finite(options.relativeDamping) &&
        options.relativeDamping > 0.0 && Finite(options.absoluteDamping) &&
        options.absoluteDamping > 0.0 &&
        Finite(options.maximumNormalCondition) &&
        options.maximumNormalCondition > 1.0 &&
        Finite(options.minimumOverlapFraction) &&
        options.minimumOverlapFraction > 0.0 &&
        options.minimumOverlapFraction <= 1.0 &&
        Finite(options.singularValueMinimum) &&
        Finite(options.singularValueMaximum) &&
        options.singularValueMinimum > 0.0 &&
        options.singularValueMinimum < options.singularValueMaximum &&
        Finite(options.determinantMinimum) &&
        Finite(options.determinantMaximum) &&
        options.determinantMinimum > 0.0 &&
        options.determinantMinimum < options.determinantMaximum &&
        Finite(options.maximumRotationDegrees) &&
        options.maximumRotationDegrees > 0.0 &&
        Finite(options.numericalVarianceFloor) &&
        options.numericalVarianceFloor > 0.0 &&
        options.minimumSamples >= 6u;
}

} // namespace

const char* PhaseTranslationFailureName(PhaseTranslationFailure failure) {
    switch (failure) {
        case PhaseTranslationFailure::None: return "none";
        case PhaseTranslationFailure::InvalidInput: return "invalid-input";
        case PhaseTranslationFailure::InsufficientCoverage: return "insufficient-coverage";
        case PhaseTranslationFailure::InsufficientTexture: return "insufficient-texture";
        case PhaseTranslationFailure::TransformTooLarge: return "transform-too-large";
        case PhaseTranslationFailure::AmbiguousPeak: return "ambiguous-peak";
        case PhaseTranslationFailure::InsufficientOverlap: return "insufficient-overlap";
        case PhaseTranslationFailure::PlaneDisagreement: return "plane-disagreement";
        case PhaseTranslationFailure::NumericalFailure: return "numerical-failure";
    }
    return "invalid-input";
}

bool EstimateMultichannelPhaseTranslation(
    const std::vector<GlobalPlanePair>& planes,
    const PhaseCorrelationOptions& options,
    PhaseTranslationResult& result,
    std::string* error) {
    result = {};
    if (planes.empty() || !ValidPhaseOptions(options)) {
        result.failure = PhaseTranslationFailure::InvalidInput;
        result.message = "MFD phase-correlation request is invalid.";
        return SetError(error, result.message);
    }
    const PixelExtent commonExtent = planes.front().extent;
    const double commonRawScale = planes.front().rawPixelsPerPlanePixel;
    std::vector<PlaneSpectrum> usableSpectra;
    for (const GlobalPlanePair& plane : planes) {
        PhasePlaneDiagnostics diagnostics;
        PlaneSpectrum spectrum;
        std::string planeError;
        if (plane.extent.width != commonExtent.width ||
            plane.extent.height != commonExtent.height ||
            !Finite(plane.rawPixelsPerPlanePixel) ||
            std::abs(plane.rawPixelsPerPlanePixel - commonRawScale) > 1.0e-12) {
            diagnostics.site = plane.site;
            diagnostics.reason = "incompatible-plane-grid";
            result.planes.push_back(std::move(diagnostics));
            continue;
        }
        if (!BuildPlaneSpectrum(
                plane, options, spectrum, diagnostics, &planeError)) {
            result.planes.push_back(std::move(diagnostics));
            continue;
        }
        CorrelationPeak individualPeak;
        if (!FindCorrelationPeak(
                spectrum.normalizedCrossSpectrum,
                spectrum.extent,
                spectrum.fftWidth,
                spectrum.fftHeight,
                options,
                individualPeak)) {
            diagnostics.reason = "individual-peak-failed";
            result.planes.push_back(std::move(diagnostics));
            continue;
        }
        diagnostics.translationPlane = {
            -individualPeak.shiftX,
            -individualPeak.shiftY,
            plane.site
        };
        diagnostics.translationRaw = {
            -individualPeak.shiftX * commonRawScale,
            -individualPeak.shiftY * commonRawScale
        };
        diagnostics.peak = individualPeak.peak;
        diagnostics.peakToSidelobeRatio = individualPeak.psr;
        diagnostics.uniqueness = individualPeak.uniqueness;
        diagnostics.usable = individualPeak.psr >=
                options.minimumPeakToSidelobeRatio &&
            individualPeak.uniqueness >= options.minimumPeakUniqueness &&
            individualPeak.overlap >= options.minimumOverlapFraction;
        diagnostics.reason = diagnostics.usable
            ? "usable"
            : "ambiguous-individual-peak";
        result.planes.push_back(diagnostics);
        if (diagnostics.usable) usableSpectra.push_back(std::move(spectrum));
    }
    if (usableSpectra.size() < 2u) {
        const bool anyCoverage = std::any_of(
            result.planes.begin(), result.planes.end(),
            [](const PhasePlaneDiagnostics& diagnostics) {
                return diagnostics.validFraction > 0.0;
            });
        const bool anyTexture = std::any_of(
            result.planes.begin(), result.planes.end(),
            [](const PhasePlaneDiagnostics& diagnostics) {
                return diagnostics.referenceTextureVariance > 0.0 &&
                    diagnostics.alternateTextureVariance > 0.0;
            });
        result.failure = !anyCoverage
            ? PhaseTranslationFailure::InsufficientCoverage
            : (!anyTexture
                ? PhaseTranslationFailure::InsufficientTexture
                : PhaseTranslationFailure::AmbiguousPeak);
        result.message = "MFD multichannel phase correlation has fewer than two defensible CFA planes.";
        return SetError(error, result.message);
    }
    for (std::size_t first = 0u; first < result.planes.size(); ++first) {
        if (!result.planes[first].usable) continue;
        for (std::size_t second = first + 1u;
             second < result.planes.size();
             ++second) {
            if (!result.planes[second].usable) continue;
            const double dx = result.planes[first].translationRaw.x -
                result.planes[second].translationRaw.x;
            const double dy = result.planes[first].translationRaw.y -
                result.planes[second].translationRaw.y;
            if (std::hypot(dx, dy) >
                options.maximumPlaneDisagreementRawPixels) {
                result.failure = PhaseTranslationFailure::PlaneDisagreement;
                result.message = "MFD phase-correlation CFA planes disagree on the global translation.";
                return SetError(error, result.message);
            }
        }
    }

    const std::size_t fftWidth = usableSpectra.front().fftWidth;
    const std::size_t fftHeight = usableSpectra.front().fftHeight;
    std::vector<Complex> combined(fftWidth * fftHeight);
    double totalWeight = 0.0;
    for (const PlaneSpectrum& spectrum : usableSpectra) {
        totalWeight += spectrum.weight;
        for (std::size_t index = 0u; index < combined.size(); ++index) {
            combined[index] += spectrum.weight *
                spectrum.normalizedCrossSpectrum[index];
        }
    }
    if (!Finite(totalWeight) || totalWeight <= 0.0) {
        result.failure = PhaseTranslationFailure::NumericalFailure;
        result.message = "MFD phase-correlation plane weights are invalid.";
        return SetError(error, result.message);
    }
    for (Complex& value : combined) value /= totalWeight;
    CorrelationPeak combinedPeak;
    if (!FindCorrelationPeak(
            combined,
            commonExtent,
            fftWidth,
            fftHeight,
            options,
            combinedPeak)) {
        result.failure = PhaseTranslationFailure::NumericalFailure;
        result.message = "MFD combined phase-correlation peak could not be resolved.";
        return SetError(error, result.message);
    }
    result.translationRaw = {
        -combinedPeak.shiftX * commonRawScale,
        -combinedPeak.shiftY * commonRawScale
    };
    result.peak = combinedPeak.peak;
    result.peakToSidelobeRatio = combinedPeak.psr;
    result.uniqueness = combinedPeak.uniqueness;
    result.overlapFraction = combinedPeak.overlap;
    if (combinedPeak.psr < options.minimumPeakToSidelobeRatio ||
        combinedPeak.uniqueness < options.minimumPeakUniqueness) {
        result.failure = PhaseTranslationFailure::AmbiguousPeak;
        result.message = "MFD combined phase-correlation peak is ambiguous.";
        return SetError(error, result.message);
    }
    if (combinedPeak.overlap < options.minimumOverlapFraction) {
        result.failure = PhaseTranslationFailure::InsufficientOverlap;
        result.message = "MFD global translation has insufficient common overlap.";
        return SetError(error, result.message);
    }
    result.accepted = true;
    result.failure = PhaseTranslationFailure::None;
    result.message = "MFD multichannel phase-correlation translation is accepted as an initializer.";
    return true;
}

bool EstimateProvisionalExposureScale(
    const std::vector<ExposureSample>& samples,
    const ExposureMetadataPrior& metadata,
    const RawRadiometricParameters& parameters,
    ProvisionalExposureResult& result,
    std::string* error) {
    result = {};
    if (!ValidRadiometricParameters(parameters) ||
        (metadata.available && (!Finite(metadata.scale) || metadata.scale <= 0.0))) {
        result.message = "MFD provisional exposure request is invalid.";
        return SetError(error, result.message);
    }
    if (metadata.available && metadata.trustworthy) {
        result.valid = true;
        result.usedMetadata = true;
        result.scale = std::clamp(
            metadata.scale,
            parameters.fittedScaleMin,
            parameters.fittedScaleMax);
        result.message = "MFD provisional exposure uses trustworthy metadata.";
        return true;
    }
    std::vector<double> ratios;
    ratios.reserve(samples.size());
    for (const ExposureSample& sample : samples) {
        if (!EligibleExposureSample(sample, parameters)) continue;
        const double ratio = sample.referenceValue / sample.alternateValue;
        if (Finite(ratio) && ratio > 0.0) ratios.push_back(ratio);
    }
    if (ratios.empty()) {
        result.message = "MFD provisional exposure has no valid reference/alternate ratios.";
        return SetError(error, result.message);
    }
    result.valid = true;
    result.usedMetadata = false;
    result.ratioCount = ratios.size();
    result.scale = std::clamp(
        Median(std::move(ratios)),
        parameters.fittedScaleMin,
        parameters.fittedScaleMax);
    result.message = "MFD provisional exposure uses the deterministic median ratio.";
    return true;
}

const char* ExposureFitFailureName(ExposureFitFailure failure) {
    switch (failure) {
        case ExposureFitFailure::None: return "none";
        case ExposureFitFailure::InvalidInput: return "invalid-input";
        case ExposureFitFailure::InsufficientSamples: return "insufficient-samples";
        case ExposureFitFailure::SingularFit: return "singular-fit";
        case ExposureFitFailure::ScaleOutOfRange: return "scale-out-of-range";
        case ExposureFitFailure::MetadataDeviation: return "metadata-deviation";
        case ExposureFitFailure::CfaScaleDisagreement: return "cfa-scale-disagreement";
        case ExposureFitFailure::BlackOffset: return "black-offset";
        case ExposureFitFailure::NumericalFailure: return "numerical-failure";
    }
    return "invalid-input";
}

bool FitGlobalExposureScale(
    const std::vector<ExposureSample>& samples,
    std::uint64_t imageRawPixelCount,
    const ExposureMetadataPrior& metadata,
    const RawRadiometricParameters& parameters,
    ExposureFitResult& result,
    std::string* error) {
    result = {};
    for (std::size_t index = 0u; index < result.sites.size(); ++index) {
        result.sites[index].site = static_cast<CfaSite>(index);
    }
    if (imageRawPixelCount == 0u || !ValidRadiometricParameters(parameters)) {
        return FailExposure(
            result,
            ExposureFitFailure::InvalidInput,
            "MFD global exposure request is invalid.",
            error);
    }
    std::vector<const ExposureSample*> eligible;
    eligible.reserve(samples.size());
    for (const ExposureSample& sample : samples) {
        if (EligibleExposureSample(sample, parameters)) eligible.push_back(&sample);
    }
    result.eligibleSampleCount = eligible.size();
    const std::uint64_t fractionMinimum = static_cast<std::uint64_t>(std::ceil(
        parameters.exposureFitMinimumFraction *
        static_cast<double>(imageRawPixelCount)));
    const std::uint64_t requiredSamples = std::max(
        parameters.exposureFitMinimumSamples,
        fractionMinimum);
    if (eligible.size() < requiredSamples) {
        return FailExposure(
            result,
            ExposureFitFailure::InsufficientSamples,
            "MFD global exposure fit has insufficient eligible samples.",
            error);
    }
    ProvisionalExposureResult provisional;
    std::string provisionalError;
    if (!EstimateProvisionalExposureScale(
            samples,
            metadata,
            parameters,
            provisional,
            &provisionalError)) {
        return FailExposure(
            result,
            ExposureFitFailure::InvalidInput,
            provisionalError,
            error);
    }
    result.initializedFromMetadata = provisional.usedMetadata;
    result.initialScale = provisional.scale;
    double scale = provisional.scale;
    double denominator = 0.0;
    for (std::uint32_t iteration = 0u;
         iteration < parameters.exposureIrlsIterations;
         ++iteration) {
        double numerator = 0.0;
        denominator = 0.0;
        for (const ExposureSample* sample : eligible) {
            const double variance = sample->referenceVariance +
                scale * scale * sample->alternateVariance + 1.0e-12;
            if (!Finite(variance) || variance <= 0.0) continue;
            const double residual = (sample->referenceValue -
                scale * sample->alternateValue) / std::sqrt(variance);
            const double weight = HuberWeight(
                residual,
                parameters.exposureHuberDelta) / variance;
            numerator += weight * sample->alternateValue *
                sample->referenceValue;
            denominator += weight * sample->alternateValue *
                sample->alternateValue;
        }
        if (!Finite(numerator) || !Finite(denominator) ||
            denominator <= 1.0e-18) {
            return FailExposure(
                result,
                ExposureFitFailure::SingularFit,
                "MFD global exposure normal equation is singular.",
                error);
        }
        const double updated = numerator / denominator;
        if (!Finite(updated)) {
            return FailExposure(
                result,
                ExposureFitFailure::NumericalFailure,
                "MFD global exposure update is non-finite.",
                error);
        }
        if (updated < parameters.fittedScaleMin ||
            updated > parameters.fittedScaleMax) {
            result.scale = updated;
            return FailExposure(
                result,
                ExposureFitFailure::ScaleOutOfRange,
                "MFD global exposure scale is outside the V1 hard range.",
                error);
        }
        scale = updated;
    }
    result.scale = scale;
    result.normalDenominator = denominator;
    std::vector<double> residuals;
    residuals.reserve(eligible.size());
    std::array<std::vector<const ExposureSample*>, 4> bySite;
    for (const ExposureSample* sample : eligible) {
        const double variance = sample->referenceVariance +
            scale * scale * sample->alternateVariance + 1.0e-12;
        residuals.push_back((sample->referenceValue -
            scale * sample->alternateValue) / std::sqrt(variance));
        bySite[SiteIndex(sample->site)].push_back(sample);
    }
    result.robustStandardizedResidualScale = RobustScale(residuals);
    if (!Finite(result.robustStandardizedResidualScale)) {
        return FailExposure(
            result,
            ExposureFitFailure::NumericalFailure,
            "MFD global exposure residual scale is invalid.",
            error);
    }
    result.scaleVariance =
        result.robustStandardizedResidualScale *
        result.robustStandardizedResidualScale /
        std::max(denominator, 1.0e-18);
    if (!Finite(result.scaleVariance) || result.scaleVariance < 0.0) {
        return FailExposure(
            result,
            ExposureFitFailure::NumericalFailure,
            "MFD global exposure uncertainty is invalid.",
            error);
    }
    if (metadata.available && metadata.trustworthy) {
        result.metadataDeviationEv = std::abs(std::log2(scale / metadata.scale));
        if (!Finite(result.metadataDeviationEv) ||
            result.metadataDeviationEv > parameters.metadataDeviationWarningEv) {
            return FailExposure(
                result,
                ExposureFitFailure::MetadataDeviation,
                "MFD fitted exposure disagrees materially with trustworthy metadata.",
                error);
        }
    }

    for (std::size_t siteIndex = 0u; siteIndex < bySite.size(); ++siteIndex) {
        ExposureSiteDiagnostics& diagnostics = result.sites[siteIndex];
        const std::vector<const ExposureSample*>& siteSamples = bySite[siteIndex];
        diagnostics.sampleCount = siteSamples.size();
        if (siteSamples.size() < 3u) {
            return FailExposure(
                result,
                ExposureFitFailure::CfaScaleDisagreement,
                "MFD global exposure lacks per-CFA diagnostic support.",
                error);
        }
        double sumWeight = 0.0;
        double sumX = 0.0;
        double sumY = 0.0;
        double sumXX = 0.0;
        double sumXY = 0.0;
        double spanSum = 0.0;
        double offsetSum = 0.0;
        for (const ExposureSample* sample : siteSamples) {
            const double variance = sample->referenceVariance +
                scale * scale * sample->alternateVariance + 1.0e-12;
            const double residual = (sample->referenceValue -
                scale * sample->alternateValue) / std::sqrt(variance);
            const double weight = HuberWeight(
                residual,
                parameters.exposureHuberDelta) / variance;
            sumWeight += weight;
            sumX += weight * sample->alternateValue;
            sumY += weight * sample->referenceValue;
            sumXX += weight * sample->alternateValue * sample->alternateValue;
            sumXY += weight * sample->alternateValue * sample->referenceValue;
            spanSum += sample->usableCodeSpanDn;
            offsetSum += sample->offsetVariance;
        }
        const double fitDenominator = sumWeight * sumXX - sumX * sumX;
        if (!Finite(fitDenominator) || fitDenominator <= 1.0e-18) {
            return FailExposure(
                result,
                ExposureFitFailure::SingularFit,
                "MFD per-CFA exposure diagnostic is singular.",
                error);
        }
        diagnostics.scale =
            (sumWeight * sumXY - sumX * sumY) / fitDenominator;
        diagnostics.intercept =
            (sumY - diagnostics.scale * sumX) / sumWeight;
        const double meanSpan = spanSum / static_cast<double>(siteSamples.size());
        const double meanOffset = offsetSum / static_cast<double>(siteSamples.size());
        diagnostics.interceptLimit = std::max(
            4.0 / meanSpan,
            2.0 * std::sqrt(meanOffset));
        diagnostics.valid = Finite(diagnostics.scale) &&
            Finite(diagnostics.intercept) &&
            Finite(diagnostics.interceptLimit);
        if (!diagnostics.valid ||
            std::abs(diagnostics.scale / scale - 1.0) >
                parameters.cfaScaleDisagreementFraction) {
            return FailExposure(
                result,
                ExposureFitFailure::CfaScaleDisagreement,
                "MFD per-CFA diagnostic scales disagree with the common exposure scale.",
                error);
        }
        if (std::abs(diagnostics.intercept) > diagnostics.interceptLimit) {
            return FailExposure(
                result,
                ExposureFitFailure::BlackOffset,
                "MFD per-CFA diagnostic exposes an unresolved black offset.",
                error);
        }
    }
    result.accepted = true;
    result.failure = ExposureFitFailure::None;
    result.message = "MFD robust common exposure scale is accepted.";
    return true;
}

RawCoordinate AffineModel::Map(RawCoordinate referenceRaw) const {
    const double x = referenceRaw.x - centerRaw.x;
    const double y = referenceRaw.y - centerRaw.y;
    return {
        centerRaw.x + linear[0] * x + linear[1] * y + translationRaw.x,
        centerRaw.y + linear[2] * x + linear[3] * y + translationRaw.y
    };
}

const char* AffineRefinementFailureName(AffineRefinementFailure failure) {
    switch (failure) {
        case AffineRefinementFailure::None: return "none";
        case AffineRefinementFailure::InvalidInput: return "invalid-input";
        case AffineRefinementFailure::InsufficientSamples: return "insufficient-samples";
        case AffineRefinementFailure::SingularNormalMatrix: return "singular-normal-matrix";
        case AffineRefinementFailure::NoImprovement: return "no-improvement";
        case AffineRefinementFailure::ImplausibleLinearPart: return "implausible-linear-part";
        case AffineRefinementFailure::InsufficientOverlap: return "insufficient-overlap";
        case AffineRefinementFailure::NumericalFailure: return "numerical-failure";
    }
    return "invalid-input";
}

bool RefineGlobalAffine(
    const std::vector<AffineReferenceSample>& referenceSamples,
    const AffineSourceEvaluator& sourceEvaluator,
    const AffineModel& translationSeed,
    const AffineRefinementOptions& options,
    AffineRefinementResult& result,
    std::string* error) {
    result = {};
    result.model = translationSeed;
    if (!sourceEvaluator || !ValidAffineOptions(options) ||
        referenceSamples.size() < options.minimumSamples ||
        !Finite(translationSeed.centerRaw.x) ||
        !Finite(translationSeed.centerRaw.y) ||
        !Finite(translationSeed.translationRaw.x) ||
        !Finite(translationSeed.translationRaw.y) ||
        !std::all_of(
            translationSeed.linear.begin(),
            translationSeed.linear.end(),
            [](double value) { return Finite(value); })) {
        result.failure = AffineRefinementFailure::InvalidInput;
        result.message = "MFD affine-refinement request is invalid.";
        return SetError(error, result.message);
    }
    AffineNormalEvaluation current = EvaluateAffineNormal(
        referenceSamples,
        sourceEvaluator,
        result.model,
        options,
        true);
    result.validSampleCount = current.sampleCount;
    result.initialObjective = current.objective;
    result.finalObjective = current.objective;
    result.overlapFraction = AffineOverlapFraction(
        result.model,
        options.referenceExtent,
        options.alternateExtent);
    result.translationFallbackAvailable = current.valid &&
        result.overlapFraction >= options.minimumOverlapFraction;
    if (!current.valid) {
        result.failure = AffineRefinementFailure::InsufficientSamples;
        result.message = "MFD affine refinement has insufficient valid photometric samples.";
        return SetError(error, result.message);
    }

    for (std::uint32_t iteration = 0u;
         iteration < options.iterations;
         ++iteration) {
        bool acceptedStep = false;
        double dampingScale = 1.0;
        for (std::uint32_t attempt = 0u; attempt < 6u; ++attempt) {
            std::array<double, 36> damped = current.normal;
            for (std::size_t diagonal = 0u; diagonal < 6u; ++diagonal) {
                damped[diagonal * 6u + diagonal] +=
                    dampingScale * (
                        options.relativeDamping *
                            std::max(current.normal[diagonal * 6u + diagonal], 0.0) +
                        options.absoluteDamping);
            }
            std::array<double, 6> rhs {};
            for (std::size_t index = 0u; index < 6u; ++index) {
                rhs[index] = -current.gradient[index];
            }
            std::array<double, 6> delta {};
            double condition = 0.0;
            if (!SolveLinear6(damped, rhs, delta, condition) ||
                condition > options.maximumNormalCondition) {
                dampingScale *= 10.0;
                continue;
            }
            const AffineModel candidate = ApplyAffineDelta(
                result.model,
                delta,
                options.referenceExtent);
            AffineNormalEvaluation candidateEvaluation = EvaluateAffineNormal(
                referenceSamples,
                sourceEvaluator,
                candidate,
                options,
                false);
            if (candidateEvaluation.valid &&
                candidateEvaluation.objective + 1.0e-12 < current.objective) {
                result.model = candidate;
                result.normalCondition = condition;
                ++result.acceptedIterations;
                current = EvaluateAffineNormal(
                    referenceSamples,
                    sourceEvaluator,
                    result.model,
                    options,
                    true);
                result.finalObjective = current.objective;
                result.validSampleCount = current.sampleCount;
                acceptedStep = true;
                break;
            }
            dampingScale *= 10.0;
        }
        if (!acceptedStep) break;
    }
    if (result.acceptedIterations == 0u ||
        !Finite(result.finalObjective) ||
        result.finalObjective >= result.initialObjective) {
        result.failure = AffineRefinementFailure::NoImprovement;
        result.message = "MFD affine model did not improve the translation-only robust objective.";
        return SetError(error, result.message);
    }

    AffineLinearDiagnostics(
        result.model,
        result.singularValueMinimum,
        result.singularValueMaximum,
        result.determinant,
        result.rotationDegrees);
    if (!Finite(result.singularValueMinimum) ||
        !Finite(result.singularValueMaximum) ||
        !Finite(result.determinant) || !Finite(result.rotationDegrees) ||
        result.singularValueMinimum < options.singularValueMinimum ||
        result.singularValueMaximum > options.singularValueMaximum ||
        result.determinant < options.determinantMinimum ||
        result.determinant > options.determinantMaximum ||
        std::abs(result.rotationDegrees) > options.maximumRotationDegrees) {
        result.failure = AffineRefinementFailure::ImplausibleLinearPart;
        result.message = "MFD affine model violates scale, rotation, or determinant guards.";
        return SetError(error, result.message);
    }
    result.overlapFraction = AffineOverlapFraction(
        result.model,
        options.referenceExtent,
        options.alternateExtent);
    if (result.overlapFraction < options.minimumOverlapFraction) {
        result.failure = AffineRefinementFailure::InsufficientOverlap;
        result.message = "MFD affine model has insufficient final overlap.";
        return SetError(error, result.message);
    }

    const double residualScale = RobustScale(current.standardizedResiduals);
    std::array<double, 36> covarianceNormalized {};
    double covarianceCondition = 0.0;
    if (!Finite(residualScale) ||
        !InvertNormal6(
            current.normal,
            covarianceNormalized,
            options.absoluteDamping,
            covarianceCondition) ||
        covarianceCondition > options.maximumNormalCondition) {
        result.failure = AffineRefinementFailure::SingularNormalMatrix;
        result.message = "MFD affine covariance normal matrix is singular or ill-conditioned.";
        return SetError(error, result.message);
    }
    const double coordinateScale = std::max<double>(
        1.0,
        std::max(options.referenceExtent.width, options.referenceExtent.height));
    const std::array<double, 6> transformScale {
        1.0 / coordinateScale,
        1.0 / coordinateScale,
        1.0 / coordinateScale,
        1.0 / coordinateScale,
        1.0,
        1.0
    };
    const double covarianceScale = std::max(1.0, residualScale * residualScale);
    for (std::size_t row = 0u; row < 6u; ++row) {
        for (std::size_t column = 0u; column < 6u; ++column) {
            result.parameterCovariance[row * 6u + column] =
                covarianceScale * transformScale[row] *
                covarianceNormalized[row * 6u + column] *
                transformScale[column];
        }
    }
    result.normalCondition = std::max(
        result.normalCondition,
        covarianceCondition);
    result.acceptedAffine = true;
    result.failure = AffineRefinementFailure::None;
    result.message = "MFD robust global affine model is accepted.";
    return true;
}

GlobalFrameAlignmentDecision DecideGlobalFrameAlignment(
    const PhaseTranslationResult& translation,
    const ExposureFitResult& exposure,
    const AffineRefinementResult& affine,
    RawCoordinate referenceCenter) {
    GlobalFrameAlignmentDecision decision;
    decision.model.centerRaw = referenceCenter;
    if (!translation.accepted) {
        decision.message = "MFD frame rejected because global translation is not defensible.";
        return decision;
    }
    if (!exposure.accepted) {
        decision.message = "MFD frame rejected because radiometric matching is not defensible.";
        return decision;
    }
    decision.exposureScale = exposure.scale;
    decision.exposureScaleVariance = exposure.scaleVariance;
    if (affine.acceptedAffine) {
        decision.accepted = true;
        decision.choice = GlobalFrameAlignmentChoice::Affine;
        decision.model = affine.model;
        decision.message = "MFD frame accepted with global affine registration.";
        return decision;
    }
    if (affine.translationFallbackAvailable) {
        decision.accepted = true;
        decision.choice = GlobalFrameAlignmentChoice::Translation;
        decision.model.translationRaw = translation.translationRaw;
        decision.message = "MFD frame accepted with the translation fallback after affine rejection.";
        return decision;
    }
    decision.message = "MFD frame rejected because neither affine nor translation fallback is defensible.";
    return decision;
}

} // namespace Raw::Mfd
