#include "Raw/MultiFrameDenoise/LocalMotion.h"

#include "Raw/MultiFrameDenoise/SameCfaSampler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

namespace Raw::Mfd {
namespace {

constexpr std::array<CfaSite, 4> kSites {
    CfaSite::Red,
    CfaSite::Green0,
    CfaSite::Green1,
    CfaSite::Blue
};

bool Finite(double value) {
    return std::isfinite(value);
}

bool Finite(RawCoordinate value) {
    return Finite(value.x) && Finite(value.y);
}

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
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
    if (extent.width == 0u || extent.height == 0u) {
        count = 0u;
        return true;
    }
    if (extent.width > std::numeric_limits<std::uint64_t>::max() / extent.height) {
        return false;
    }
    const std::uint64_t product = extent.width * extent.height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

std::size_t PixelIndex(PixelExtent extent, std::uint64_t x, std::uint64_t y) {
    return static_cast<std::size_t>(y * extent.width + x);
}

std::int64_t ReflectIndex(std::int64_t coordinate, std::int64_t size) {
    if (size <= 1) return 0;
    while (coordinate < 0 || coordinate >= size) {
        if (coordinate < 0) coordinate = -coordinate;
        if (coordinate >= size) coordinate = 2 * size - 2 - coordinate;
    }
    return coordinate;
}

double Clamp01(double value) {
    return std::max(0.0, std::min(1.0, value));
}

double Smootherstep5(double value) {
    const double x = Clamp01(value);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}

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

double SquaredNorm(RawCoordinate value) {
    return value.x * value.x + value.y * value.y;
}

RawCoordinate Add(RawCoordinate a, RawCoordinate b) {
    return { a.x + b.x, a.y + b.y };
}

RawCoordinate Subtract(RawCoordinate a, RawCoordinate b) {
    return { a.x - b.x, a.y - b.y };
}

RawCoordinate Scale(RawCoordinate value, double scale) {
    return { value.x * scale, value.y * scale };
}

class PersistentParallelExecutor {
public:
    explicit PersistentParallelExecutor(std::uint32_t workerCount) {
        const std::uint32_t backgroundCount =
            std::max(1u, workerCount) - 1u;
        m_Workers.reserve(backgroundCount);
        try {
            for (std::uint32_t index = 0u; index < backgroundCount; ++index) {
                m_Workers.emplace_back([this]() { WorkerLoop(); });
            }
        } catch (...) {
            StopWorkers();
            throw;
        }
    }

    ~PersistentParallelExecutor() {
        StopWorkers();
    }

private:
    void StopWorkers() noexcept {
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Stop = true;
            ++m_Generation;
        }
        m_WorkAvailable.notify_all();
        for (std::thread& worker : m_Workers) {
            if (worker.joinable()) worker.join();
        }
    }

public:
    template <typename Function>
    void Run(std::size_t count, Function&& function) {
        if (count == 0u) return;
        if (m_Workers.empty()) {
            for (std::size_t index = 0u; index < count; ++index) {
                function(index);
            }
            return;
        }
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_Function = std::forward<Function>(function);
            m_Count = count;
            m_Next.store(0u, std::memory_order_relaxed);
            m_RemainingWorkers = m_Workers.size();
            m_Exception = nullptr;
            ++m_Generation;
        }
        m_WorkAvailable.notify_all();
        ProcessAvailable();
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_WorkComplete.wait(lock, [this]() {
            return m_RemainingWorkers == 0u;
        });
        m_Function = {};
        if (m_Exception) std::rethrow_exception(m_Exception);
    }

private:
    void ProcessAvailable() {
        while (true) {
            const std::size_t index =
                m_Next.fetch_add(1u, std::memory_order_relaxed);
            if (index >= m_Count) break;
            try {
                m_Function(index);
            } catch (...) {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if (!m_Exception) m_Exception = std::current_exception();
            }
        }
    }

    void WorkerLoop() {
        std::uint64_t observedGeneration = 0u;
        while (true) {
            {
                std::unique_lock<std::mutex> lock(m_Mutex);
                m_WorkAvailable.wait(lock, [&]() {
                    return m_Stop || m_Generation != observedGeneration;
                });
                if (m_Stop) return;
                observedGeneration = m_Generation;
            }
            ProcessAvailable();
            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if (--m_RemainingWorkers == 0u) {
                    m_WorkComplete.notify_one();
                }
            }
        }
    }

    std::mutex m_Mutex;
    std::condition_variable m_WorkAvailable;
    std::condition_variable m_WorkComplete;
    std::vector<std::thread> m_Workers;
    std::function<void(std::size_t)> m_Function;
    std::atomic<std::size_t> m_Next { 0u };
    std::size_t m_Count = 0u;
    std::size_t m_RemainingWorkers = 0u;
    std::uint64_t m_Generation = 0u;
    bool m_Stop = false;
    std::exception_ptr m_Exception;
};

SymmetricRawCovariance AddCovariance(
    SymmetricRawCovariance a,
    SymmetricRawCovariance b) {
    return {
        a.xxRawPixelsSquared + b.xxRawPixelsSquared,
        a.xyRawPixelsSquared + b.xyRawPixelsSquared,
        a.yyRawPixelsSquared + b.yyRawPixelsSquared
    };
}

SymmetricRawCovariance ScaleCovariance(
    SymmetricRawCovariance covariance,
    double scale) {
    return {
        covariance.xxRawPixelsSquared * scale,
        covariance.xyRawPixelsSquared * scale,
        covariance.yyRawPixelsSquared * scale
    };
}

SymmetricRawCovariance OuterProduct(RawCoordinate value) {
    return {
        value.x * value.x,
        value.x * value.y,
        value.y * value.y
    };
}

bool CovarianceEigenvalues(
    SymmetricRawCovariance covariance,
    double& low,
    double& high) {
    if (!Finite(covariance.xxRawPixelsSquared) ||
        !Finite(covariance.xyRawPixelsSquared) ||
        !Finite(covariance.yyRawPixelsSquared)) {
        return false;
    }
    const double trace = covariance.xxRawPixelsSquared +
        covariance.yyRawPixelsSquared;
    const double difference = covariance.xxRawPixelsSquared -
        covariance.yyRawPixelsSquared;
    const double radius = std::sqrt(std::max(
        0.0,
        0.25 * difference * difference +
            covariance.xyRawPixelsSquared * covariance.xyRawPixelsSquared));
    low = 0.5 * trace - radius;
    high = 0.5 * trace + radius;
    return Finite(low) && Finite(high);
}

bool ClampCovariance(
    SymmetricRawCovariance input,
    double minimumSigma,
    double maximumSigma,
    SymmetricRawCovariance& result) {
    double low = 0.0;
    double high = 0.0;
    if (!CovarianceEigenvalues(input, low, high)) return false;
    const double minimum = minimumSigma * minimumSigma;
    const double maximum = maximumSigma * maximumSigma;
    if (high < -1.0e-10 || low < -std::max(1.0e-10, 1.0e-8 * std::abs(high))) {
        return false;
    }
    const double angle = 0.5 * std::atan2(
        2.0 * input.xyRawPixelsSquared,
        input.xxRawPixelsSquared - input.yyRawPixelsSquared);
    const double clampedHigh = std::max(minimum, std::min(maximum, high));
    const double clampedLow = std::max(minimum, std::min(maximum, low));
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    result.xxRawPixelsSquared =
        clampedHigh * cosine * cosine + clampedLow * sine * sine;
    result.xyRawPixelsSquared =
        (clampedHigh - clampedLow) * cosine * sine;
    result.yyRawPixelsSquared =
        clampedHigh * sine * sine + clampedLow * cosine * cosine;
    return Finite(result.xxRawPixelsSquared) &&
        Finite(result.xyRawPixelsSquared) &&
        Finite(result.yyRawPixelsSquared);
}

bool InvertCovariance(
    SymmetricRawCovariance covariance,
    SymmetricRawCovariance& inverse) {
    const double determinant =
        covariance.xxRawPixelsSquared * covariance.yyRawPixelsSquared -
        covariance.xyRawPixelsSquared * covariance.xyRawPixelsSquared;
    if (!Finite(determinant) || determinant <= 1.0e-20) return false;
    inverse.xxRawPixelsSquared = covariance.yyRawPixelsSquared / determinant;
    inverse.xyRawPixelsSquared = -covariance.xyRawPixelsSquared / determinant;
    inverse.yyRawPixelsSquared = covariance.xxRawPixelsSquared / determinant;
    return Finite(inverse.xxRawPixelsSquared) &&
        Finite(inverse.xyRawPixelsSquared) &&
        Finite(inverse.yyRawPixelsSquared);
}

double QuadraticForm(RawCoordinate vector, SymmetricRawCovariance matrix) {
    return vector.x * vector.x * matrix.xxRawPixelsSquared +
        2.0 * vector.x * vector.y * matrix.xyRawPixelsSquared +
        vector.y * vector.y * matrix.yyRawPixelsSquared;
}

bool ValidateLevel(const CfaPyramidLevel& level) {
    std::size_t count = 0u;
    return CheckedSampleCount(level.extent, count) && count > 0u &&
        level.signal.size() == count &&
        level.variance.size() == count &&
        level.validMask.size() == count &&
        Finite(level.rawPixelsPerLevelPixel) &&
        level.rawPixelsPerLevelPixel > 0.0;
}

struct LevelSample {
    double value = 0.0;
    double variance = 0.0;
    double gradientX = 0.0;
    double gradientY = 0.0;
};

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

bool GlobalCovarianceAt(
    const LocalMotionDirectionRequest& request,
    RawCoordinate coordinate,
    SymmetricRawCovariance& covariance) {
    covariance = {};
    if (!request.globalCovariance) return true;
    return request.globalCovariance(coordinate, covariance) &&
        Finite(covariance.xxRawPixelsSquared) &&
        Finite(covariance.xyRawPixelsSquared) &&
        Finite(covariance.yyRawPixelsSquared);
}

} // namespace

const char* MotionNodeStateName(MotionNodeState state) {
    switch (state) {
        case MotionNodeState::Structured: return "structured";
        case MotionNodeState::FlatSafe: return "flat-safe";
        case MotionNodeState::Rejected: return "rejected";
    }
    return "rejected";
}

const char* MotionNodeRejectReasonName(MotionNodeRejectReason reason) {
    switch (reason) {
        case MotionNodeRejectReason::None: return "none";
        case MotionNodeRejectReason::InsufficientCoverage: return "insufficient-coverage";
        case MotionNodeRejectReason::NoDiscreteCandidate: return "no-discrete-candidate";
        case MotionNodeRejectReason::AmbiguousMatch: return "ambiguous-match";
        case MotionNodeRejectReason::SubpixelFailure: return "subpixel-failure";
        case MotionNodeRejectReason::UnobservableUnsafe: return "unobservable-unsafe";
        case MotionNodeRejectReason::InvalidCovariance: return "invalid-covariance";
        case MotionNodeRejectReason::ForwardBackwardUnavailable: return "forward-backward-unavailable";
        case MotionNodeRejectReason::ForwardBackwardFailure: return "forward-backward-failure";
        case MotionNodeRejectReason::SourceBorder: return "source-border";
        case MotionNodeRejectReason::MotionFieldAmbiguous: return "motion-field-ambiguous";
        case MotionNodeRejectReason::NumericalFailure: return "numerical-failure";
    }
    return "numerical-failure";
}

const char* LocalMotionFailureName(LocalMotionFailure failure) {
    switch (failure) {
        case LocalMotionFailure::None: return "none";
        case LocalMotionFailure::InvalidInput: return "invalid-input";
        case LocalMotionFailure::PyramidMismatch: return "pyramid-mismatch";
        case LocalMotionFailure::NoGrid: return "no-grid";
        case LocalMotionFailure::NoUsableNodes: return "no-usable-nodes";
        case LocalMotionFailure::NumericalFailure: return "numerical-failure";
    }
    return "numerical-failure";
}

const CfaPyramidLevel* FindCfaPyramidLevel(
    const CfaPlanePyramid& pyramid,
    CfaSite site,
    std::uint32_t level) {
    const std::vector<CfaPyramidLevel>& levels = pyramid.planes[SiteIndex(site)];
    if (level >= levels.size() || levels[level].level != level) return nullptr;
    return &levels[level];
}

bool BuildCfaPlanePyramid(
    const CfaLayout& layout,
    PixelExtent rawExtent,
    const std::array<CfaPyramidBasePlane, 4>& basePlanes,
    const RegistrationParameters& parameters,
    CfaPlanePyramid& result,
    std::string* error) {
    result = {};
    if (!layout.IsValid() || rawExtent.width == 0u || rawExtent.height == 0u ||
        parameters.pyramidLevels == 0u ||
        parameters.pyramidLevels > parameters.searchRadiiFineToCoarse.size()) {
        return Fail(error, "MFD CFA pyramid request is invalid.");
    }
    double kernelSum = 0.0;
    for (double coefficient : parameters.pyramidKernel) {
        if (!Finite(coefficient) || coefficient < 0.0) {
            return Fail(error, "MFD CFA pyramid kernel is invalid.");
        }
        kernelSum += coefficient;
    }
    if (std::abs(kernelSum - 1.0) > 1.0e-12) {
        return Fail(error, "MFD CFA pyramid kernel must sum to one.");
    }

    result.layout = layout;
    result.rawExtent = rawExtent;
    result.levelCount = parameters.pyramidLevels;
    for (CfaSite site : kSites) {
        const CfaPyramidBasePlane& base = basePlanes[SiteIndex(site)];
        const PixelExtent expected = layout.PlaneExtent(site, rawExtent);
        std::size_t count = 0u;
        if (base.site != site || base.extent.width != expected.width ||
            base.extent.height != expected.height ||
            !CheckedSampleCount(base.extent, count) || count == 0u ||
            base.signal.size() != count || base.variance.size() != count ||
            (!base.validMask.empty() && base.validMask.size() != count)) {
            return Fail(error, "MFD CFA pyramid base plane does not match the CFA layout.");
        }
        CfaPyramidLevel level;
        level.site = site;
        level.extent = base.extent;
        level.signal = base.signal;
        level.variance = base.variance;
        level.validMask = base.validMask.empty()
            ? std::vector<std::uint8_t>(count, 1u)
            : base.validMask;
        for (std::size_t index = 0u; index < count; ++index) {
            if (!Finite(level.signal[index]) || !Finite(level.variance[index]) ||
                level.variance[index] < 0.0) {
                level.validMask[index] = 0u;
            }
        }
        result.planes[SiteIndex(site)].push_back(std::move(level));

        for (std::uint32_t levelIndex = 1u;
             levelIndex < parameters.pyramidLevels;
             ++levelIndex) {
            const CfaPyramidLevel& previous =
                result.planes[SiteIndex(site)].back();
            CfaPyramidLevel next;
            next.site = site;
            next.level = levelIndex;
            next.rawPixelsPerLevelPixel = std::ldexp(2.0, levelIndex);
            next.extent = {
                (previous.extent.width + 1u) / 2u,
                (previous.extent.height + 1u) / 2u
            };
            std::size_t nextCount = 0u;
            if (!CheckedSampleCount(next.extent, nextCount) || nextCount == 0u) {
                return Fail(error, "MFD CFA pyramid level dimensions overflowed.");
            }
            next.signal.assign(nextCount, 0.0);
            next.variance.assign(nextCount, 0.0);
            next.validMask.assign(nextCount, 0u);
            for (std::uint64_t y = 0u; y < next.extent.height; ++y) {
                for (std::uint64_t x = 0u; x < next.extent.width; ++x) {
                    const std::int64_t centerX = static_cast<std::int64_t>(2u * x);
                    const std::int64_t centerY = static_cast<std::int64_t>(2u * y);
                    double signal = 0.0;
                    double variance = 0.0;
                    bool valid = centerX >= 2 && centerY >= 2 &&
                        centerX + 2 < static_cast<std::int64_t>(previous.extent.width) &&
                        centerY + 2 < static_cast<std::int64_t>(previous.extent.height);
                    for (std::int64_t kernelY = -2; kernelY <= 2; ++kernelY) {
                        const double weightY = parameters.pyramidKernel[
                            static_cast<std::size_t>(kernelY + 2)];
                        const std::int64_t sampleY = ReflectIndex(
                            centerY + kernelY,
                            static_cast<std::int64_t>(previous.extent.height));
                        for (std::int64_t kernelX = -2; kernelX <= 2; ++kernelX) {
                            const double weightX = parameters.pyramidKernel[
                                static_cast<std::size_t>(kernelX + 2)];
                            const std::int64_t sampleX = ReflectIndex(
                                centerX + kernelX,
                                static_cast<std::int64_t>(previous.extent.width));
                            const std::size_t sourceIndex = PixelIndex(
                                previous.extent,
                                static_cast<std::uint64_t>(sampleX),
                                static_cast<std::uint64_t>(sampleY));
                            const double coefficient = weightX * weightY;
                            signal += coefficient * previous.signal[sourceIndex];
                            variance += coefficient * coefficient *
                                previous.variance[sourceIndex];
                            valid = valid && previous.validMask[sourceIndex] != 0u;
                        }
                    }
                    const std::size_t targetIndex = PixelIndex(next.extent, x, y);
                    next.signal[targetIndex] = signal;
                    next.variance[targetIndex] = variance;
                    next.validMask[targetIndex] =
                        valid && Finite(signal) && Finite(variance) && variance >= 0.0
                        ? 1u
                        : 0u;
                }
            }
            result.planes[SiteIndex(site)].push_back(std::move(next));
        }
    }
    return true;
}

namespace {

struct PatchStatistics {
    bool valid = false;
    std::uint64_t validCount = 0u;
    std::uint64_t nominalCount = 0u;
    double validFraction = 0.0;
    double robustCost = std::numeric_limits<double>::infinity();
    double cappedChiSquared = std::numeric_limits<double>::infinity();
    double flatPhotometricCost = std::numeric_limits<double>::infinity();
    double hessianXX = 0.0;
    double hessianXY = 0.0;
    double hessianYY = 0.0;
    double gradientX = 0.0;
    double gradientY = 0.0;
    std::vector<double> residuals;
    std::array<double,4> siteSquared {};
    std::array<std::uint64_t,4> siteCount {};
};

bool EvaluatePatch(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    std::uint32_t levelIndex,
    RawCoordinate residualRaw,
    std::uint32_t patchLevelPixels,
    bool calculateNormal,
    bool calculateFlatPhotometricCost,
    PatchStatistics& statistics) {
    statistics = {};
    if (!request.reference || !request.source || patchLevelPixels == 0u ||
        !Finite(centerRaw) || !Finite(residualRaw) ||
        !Finite(request.exposureScale) || request.exposureScale <= 0.0) {
        return false;
    }
    const double levelScale = std::ldexp(1.0, levelIndex);
    const double rawPixelsPerLevelPixel = std::ldexp(2.0, levelIndex);
    statistics.nominalCount = static_cast<std::uint64_t>(patchLevelPixels) *
        patchLevelPixels * kSites.size();
    statistics.residuals.reserve(
        calculateNormal ? static_cast<std::size_t>(statistics.nominalCount) : 0u);
    double robustCostSum = 0.0;
    double cappedSquaredSum = 0.0;
    std::vector<double> flatRatios;
    if (calculateFlatPhotometricCost) {
        flatRatios.reserve(static_cast<std::size_t>(statistics.nominalCount));
    }

    for (CfaSite site : kSites) {
        const CfaPyramidLevel* referenceLevel = FindCfaPyramidLevel(
            *request.reference, site, levelIndex);
        const CfaPyramidLevel* sourceLevel = FindCfaPyramidLevel(
            *request.source, site, levelIndex);
        if (!referenceLevel || !sourceLevel ||
            !ValidateLevel(*referenceLevel) || !ValidateLevel(*sourceLevel)) {
            return false;
        }
        const CfaPlaneCoordinate centerPlane = request.reference->layout.RawToPlane(
            centerRaw, site);
        const double centerLevelX = centerPlane.x / levelScale;
        const double centerLevelY = centerPlane.y / levelScale;
        const std::int64_t startX = static_cast<std::int64_t>(
            std::floor(centerLevelX - 0.5 * static_cast<double>(patchLevelPixels)));
        const std::int64_t startY = static_cast<std::int64_t>(
            std::floor(centerLevelY - 0.5 * static_cast<double>(patchLevelPixels)));
        for (std::uint32_t patchY = 0u; patchY < patchLevelPixels; ++patchY) {
            const std::int64_t referenceY = startY + patchY;
            if (referenceY < 0 ||
                referenceY >= static_cast<std::int64_t>(referenceLevel->extent.height)) {
                continue;
            }
            for (std::uint32_t patchX = 0u; patchX < patchLevelPixels; ++patchX) {
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
                RawCoordinate sourceRaw = request.globalWarp.Map(referenceRaw);
                sourceRaw = Add(sourceRaw, residualRaw);
                const CfaPlaneCoordinate sourcePlane =
                    request.source->layout.RawToPlane(sourceRaw, site);
                LevelSample sourceSample;
                const bool sampled = (calculateNormal || calculateFlatPhotometricCost)
                    ? SampleLevelKeys<true>(*sourceLevel,sourcePlane.x/levelScale,sourcePlane.y/levelScale,
                        request.options.registration.keysBicubicParameter,sourceSample)
                    : SampleLevelKeys<false>(*sourceLevel,sourcePlane.x/levelScale,sourcePlane.y/levelScale,
                        request.options.registration.keysBicubicParameter,sourceSample);
                if (!sampled) {
                    continue;
                }
                const double variance =
                    referenceLevel->variance[referenceIndex] +
                    request.exposureScale * request.exposureScale *
                        sourceSample.variance +
                    request.options.registration.localNumericalVarianceFloor;
                if (!Finite(variance) || variance <= 0.0) continue;
                const double denominator = std::sqrt(variance);
                const double residual =
                    (referenceLevel->signal[referenceIndex] -
                        request.exposureScale * sourceSample.value) /
                    denominator;
                if (!Finite(residual)) continue;
                ++statistics.validCount;
                // Color contradictions must remain visible even when both
                // candidates exceed the robust solver's ordinary cost cap.
                statistics.siteSquared[SiteIndex(site)] += std::min(residual * residual,1e12);
                ++statistics.siteCount[SiteIndex(site)];
                robustCostSum += HuberLoss(
                    residual, request.options.registration.registrationHuberDelta);
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

struct Candidate {
    RawCoordinate residualRaw;
    PatchStatistics statistics;
};

bool BetterCandidate(
    const Candidate& candidate,
    const Candidate& incumbent,
    RawCoordinate predictionRaw,
    double tolerance) {
    if (candidate.statistics.robustCost <
        incumbent.statistics.robustCost - tolerance) {
        return true;
    }
    if (candidate.statistics.robustCost >
        incumbent.statistics.robustCost + tolerance) {
        return false;
    }
    const double candidatePredictionDistance = SquaredNorm(
        Subtract(candidate.residualRaw, predictionRaw));
    const double incumbentPredictionDistance = SquaredNorm(
        Subtract(incumbent.residualRaw, predictionRaw));
    if (candidatePredictionDistance < incumbentPredictionDistance - tolerance) {
        return true;
    }
    if (candidatePredictionDistance > incumbentPredictionDistance + tolerance) {
        return false;
    }
    const double candidateMagnitude = SquaredNorm(candidate.residualRaw);
    const double incumbentMagnitude = SquaredNorm(incumbent.residualRaw);
    if (candidateMagnitude < incumbentMagnitude - tolerance) return true;
    if (candidateMagnitude > incumbentMagnitude + tolerance) return false;
    if (candidate.residualRaw.y < incumbent.residualRaw.y - tolerance) return true;
    if (candidate.residualRaw.y > incumbent.residualRaw.y + tolerance) return false;
    return candidate.residualRaw.x < incumbent.residualRaw.x - tolerance;
}

void AddUniqueSeed(std::vector<RawCoordinate>& seeds, RawCoordinate seed) {
    for (RawCoordinate existing : seeds) {
        if (std::abs(existing.x - seed.x) <= 1.0e-9 &&
            std::abs(existing.y - seed.y) <= 1.0e-9) {
            return;
        }
    }
    seeds.push_back(seed);
}

void BuildDiscreteCandidates(
    const LocalMotionDirectionRequest& request,
    std::uint32_t level,
    const std::vector<RawCoordinate>& seedsRaw,
    std::vector<Candidate>& candidates) {
    candidates.clear();
    const double rawPerLevelPixel = std::ldexp(2.0, level);
    const std::uint32_t radius =
        request.options.registration.searchRadiiFineToCoarse[level];
    for (RawCoordinate seed : seedsRaw) {
        for (std::int32_t dy = -static_cast<std::int32_t>(radius);
             dy <= static_cast<std::int32_t>(radius);
             ++dy) {
            for (std::int32_t dx = -static_cast<std::int32_t>(radius);
                 dx <= static_cast<std::int32_t>(radius);
                 ++dx) {
                const RawCoordinate residual {
                    seed.x + static_cast<double>(dx) * rawPerLevelPixel,
                    seed.y + static_cast<double>(dy) * rawPerLevelPixel
                };
                const bool duplicate = std::any_of(
                    candidates.begin(), candidates.end(),
                    [residual](const Candidate& existing) {
                        return std::abs(
                                   existing.residualRaw.x - residual.x) <=
                                1.0e-9 &&
                            std::abs(
                                   existing.residualRaw.y - residual.y) <=
                                1.0e-9;
                    });
                if (duplicate) continue;
                Candidate candidate;
                candidate.residualRaw = residual;
                candidates.push_back(std::move(candidate));
            }
        }
    }
}

bool SelectBestAndSecond(
    const std::vector<Candidate>& candidates,
    RawCoordinate predictionRaw,
    double tolerance,
    Candidate& best,
    Candidate& second) {
    std::size_t bestIndex = candidates.size();
    for (std::size_t index = 0u; index < candidates.size(); ++index) {
        if (!candidates[index].statistics.valid) continue;
        if (bestIndex == candidates.size() ||
            BetterCandidate(
                candidates[index], candidates[bestIndex], predictionRaw,
                tolerance)) {
            bestIndex = index;
        }
    }
    if (bestIndex == candidates.size()) return false;
    best = candidates[bestIndex];
    constexpr double minimumSecondDistanceRaw = 2.0;
    std::size_t secondIndex = candidates.size();
    for (std::size_t index = 0u; index < candidates.size(); ++index) {
        if (index == bestIndex || !candidates[index].statistics.valid ||
            std::sqrt(SquaredNorm(Subtract(
                candidates[index].residualRaw, best.residualRaw))) + 1.0e-12 <
                minimumSecondDistanceRaw) {
            continue;
        }
        if (secondIndex == candidates.size() ||
            BetterCandidate(
                candidates[index], candidates[secondIndex], predictionRaw,
                tolerance)) {
            secondIndex = index;
        }
    }
    second = {};
    second.statistics.robustCost = std::numeric_limits<double>::infinity();
    if (secondIndex != candidates.size()) second = candidates[secondIndex];
    return true;
}

bool VerifyAcceleratedCandidates(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    std::uint32_t level,
    RawCoordinate predictionRaw,
    std::vector<Candidate>& candidates,
    Candidate& best,
    Candidate& second) {
    std::vector<std::size_t> ranked;
    ranked.reserve(candidates.size());
    for (std::size_t index = 0u; index < candidates.size(); ++index) {
        if (candidates[index].statistics.valid &&
            Finite(candidates[index].statistics.robustCost)) {
            ranked.push_back(index);
        }
    }
    if (ranked.empty()) return false;
    std::stable_sort(
        ranked.begin(), ranked.end(),
        [&](std::size_t left, std::size_t right) {
            return BetterCandidate(
                candidates[left], candidates[right], predictionRaw,
                request.options.registration.candidateTieTolerance);
        });

    // FP32 GPU scores only form a shortlist.  Re-evaluate a minimum rank
    // window, every numerically close candidate, and enough spatially distinct
    // candidates in the normative FP64 implementation.  Ambiguous/flat cost
    // surfaces deliberately verify more work rather than trading safety for
    // speed.
    constexpr std::size_t minimumRankedVerification = 16u;
    constexpr std::size_t minimumSeparatedVerification = 12u;
    const double acceleratedBest =
        candidates[ranked.front()].statistics.robustCost;
    const double guard = std::max(
        5.0e-4, 5.0e-3 * std::abs(acceleratedBest));
    std::vector<std::uint8_t> verify(candidates.size(), 0u);
    std::size_t separated = 0u;
    const RawCoordinate acceleratedBestResidual =
        candidates[ranked.front()].residualRaw;
    for (std::size_t rank = 0u; rank < ranked.size(); ++rank) {
        const std::size_t index = ranked[rank];
        const bool isSeparated = std::sqrt(SquaredNorm(Subtract(
            candidates[index].residualRaw, acceleratedBestResidual))) +
                1.0e-12 >=
            2.0;
        const bool close =
            candidates[index].statistics.robustCost <= acceleratedBest + guard;
        if (rank < minimumRankedVerification || close ||
            (isSeparated && separated < minimumSeparatedVerification)) {
            verify[index] = 1u;
            if (isSeparated) ++separated;
        }
    }

    const std::uint32_t patch = level == 0u
        ? request.options.registration.finestPatchPlanePixels
        : request.options.registration.coarsePatchLevelPixels;
    for (std::size_t index = 0u; index < candidates.size(); ++index) {
        if (verify[index] == 0u) {
            candidates[index].statistics.valid = false;
            continue;
        }
        PatchStatistics authoritative;
        if (!EvaluatePatch(
                request, centerRaw, level, candidates[index].residualRaw,
                patch, false, false, authoritative) ||
            !authoritative.valid ||
            authoritative.validFraction <
                request.options.registration.minimumTileValidFraction) {
            candidates[index].statistics = {};
            continue;
        }
        candidates[index].statistics = std::move(authoritative);
    }
    return SelectBestAndSecond(
        candidates, predictionRaw,
        request.options.registration.candidateTieTolerance, best, second);
}

bool SearchTileAtLevel(
    const LocalMotionDirectionRequest& request,
    RawCoordinate centerRaw,
    std::uint32_t level,
    const std::vector<RawCoordinate>& seedsRaw,
    RawCoordinate predictionRaw,
    Candidate& best,
    Candidate& second) {
    const double rawPerLevelPixel = std::ldexp(2.0, level);
    const std::uint32_t radius =
        request.options.registration.searchRadiiFineToCoarse[level];
    const std::uint32_t patch = level == 0u
        ? request.options.registration.finestPatchPlanePixels
        : request.options.registration.coarsePatchLevelPixels;
    std::vector<Candidate> candidates;
    for (RawCoordinate seed : seedsRaw) {
        for (std::int32_t dy = -static_cast<std::int32_t>(radius);
             dy <= static_cast<std::int32_t>(radius);
             ++dy) {
            for (std::int32_t dx = -static_cast<std::int32_t>(radius);
                 dx <= static_cast<std::int32_t>(radius);
                 ++dx) {
                const RawCoordinate residual {
                    seed.x + static_cast<double>(dx) * rawPerLevelPixel,
                    seed.y + static_cast<double>(dy) * rawPerLevelPixel
                };
                bool duplicate = false;
                for (const Candidate& existing : candidates) {
                    if (std::abs(existing.residualRaw.x - residual.x) <= 1.0e-9 &&
                        std::abs(existing.residualRaw.y - residual.y) <= 1.0e-9) {
                        duplicate = true;
                        break;
                    }
                }
                if (duplicate) continue;
                Candidate candidate;
                candidate.residualRaw = residual;
                if (!EvaluatePatch(
                        request,
                        centerRaw,
                        level,
                        residual,
                        patch,
                        false,
                        false,
                        candidate.statistics) ||
                    !candidate.statistics.valid ||
                    candidate.statistics.validFraction <
                        request.options.registration.minimumTileValidFraction) {
                    continue;
                }
                candidates.push_back(std::move(candidate));
            }
        }
    }
    if (candidates.empty()) return false;
    std::size_t bestIndex = 0u;
    for (std::size_t index = 1u; index < candidates.size(); ++index) {
        if (BetterCandidate(
                candidates[index],
                candidates[bestIndex],
                predictionRaw,
                request.options.registration.candidateTieTolerance)) {
            bestIndex = index;
        }
    }
    best = candidates[bestIndex];
    const double minimumSecondDistanceRaw = 2.0;
    second = {};
    second.statistics.robustCost = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0u; index < candidates.size(); ++index) {
        if (index == bestIndex ||
            std::sqrt(SquaredNorm(Subtract(
                candidates[index].residualRaw,
                best.residualRaw))) + 1.0e-12 < minimumSecondDistanceRaw) {
            continue;
        }
        if (!Finite(second.statistics.robustCost) ||
            BetterCandidate(
                candidates[index],
                second,
                predictionRaw,
                request.options.registration.candidateTieTolerance)) {
            second = candidates[index];
        }
    }
    return true;
}

struct RefinedTile {
    bool valid = false;
    bool observable = false;
    RawCoordinate residualRaw;
    PatchStatistics statistics;
    SymmetricRawCovariance localCovarianceRaw;
    double hessianCondition = std::numeric_limits<double>::infinity();
    double positionSigmaRaw = std::numeric_limits<double>::infinity();
};

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
    RefinedTile& refined) {
    refined = {};
    RawCoordinate currentRaw = discreteResidualRaw;
    PatchStatistics current;
    if (!EvaluatePatch(
            request,
            centerRaw,
            0u,
            currentRaw,
            request.options.registration.finestPatchPlanePixels,
            true,
            false,
            current) ||
        !current.valid ||
        current.validFraction <
            request.options.registration.minimumTileValidFraction ||
        current.validCount < static_cast<std::uint64_t>(
            request.options.registration.minimumStructuredSamples)) {
        return false;
    }
    std::uint32_t costIncreases = 0u;
    for (std::uint32_t iteration = 0u;
         iteration < request.options.registration.subpixelIterations;
         ++iteration) {
        const double diagonalScale = std::max(
            { current.hessianXX, current.hessianYY, 1.0 });
        RawCoordinate stepPlane;
        if (!SolveNormalStep(
                current,
                request.options.registration.localHessianAbsoluteDamping *
                    diagonalScale,
                stepPlane)) {
            break;
        }
        const double stepLength = std::sqrt(SquaredNorm(stepPlane));
        if (stepLength >
            request.options.registration.maximumSubpixelStepPlanePixels) {
            stepPlane = Scale(
                stepPlane,
                request.options.registration.maximumSubpixelStepPlanePixels /
                    stepLength);
        }
        if (std::sqrt(SquaredNorm(stepPlane)) <
            request.options.registration.subpixelConvergencePlanePixels) {
            break;
        }
        RawCoordinate proposedRaw = Add(currentRaw, Scale(stepPlane, 2.0));
        if (std::sqrt(SquaredNorm(Subtract(
                proposedRaw, discreteResidualRaw))) >
            2.0 * request.options.registration.
                maximumDistanceFromDiscreteSeedPlanePixels) {
            break;
        }
        PatchStatistics proposed;
        bool proposedValid = EvaluatePatch(
                request,
                centerRaw,
                0u,
                proposedRaw,
                request.options.registration.finestPatchPlanePixels,
                true,
                false,
                proposed) &&
            proposed.valid &&
            proposed.validFraction >=
                request.options.registration.minimumTileValidFraction;
        if (!proposedValid ||
            proposed.robustCost > current.robustCost +
                request.options.registration.candidateTieTolerance) {
            proposedRaw = Add(currentRaw, Scale(stepPlane, 1.0));
            proposedValid = EvaluatePatch(
                    request,
                    centerRaw,
                    0u,
                    proposedRaw,
                    request.options.registration.finestPatchPlanePixels,
                    true,
                    false,
                    proposed) &&
                proposed.valid &&
                proposed.validFraction >=
                    request.options.registration.minimumTileValidFraction;
        }
        if (!proposedValid ||
            proposed.robustCost > current.robustCost +
                request.options.registration.candidateTieTolerance) {
            ++costIncreases;
            if (costIncreases >= request.options.registration.
                    maximumSubpixelCostIncreases) {
                break;
            }
            continue;
        }
        currentRaw = proposedRaw;
        current = std::move(proposed);
    }

    if (std::sqrt(SquaredNorm(Subtract(currentRaw, discreteResidualRaw))) >
        2.0 * request.options.registration.
            maximumDistanceFromDiscreteSeedPlanePixels + 1.0e-9) {
        return false;
    }
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
        if (!ClampCovariance(
                covariance,
                request.options.registration.warpCovarianceEigenvalueMinRawPixels,
                request.options.registration.warpCovarianceEigenvalueMaxRawPixels,
                refined.localCovarianceRaw)) {
            return false;
        }
        double covarianceLow = 0.0;
        double covarianceHigh = 0.0;
        if (!CovarianceEigenvalues(
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

double ResidualConfidence(double cappedChiSquared) {
    return std::exp(-0.5 * std::max(cappedChiSquared - 1.0, 0.0));
}

double CoverageConfidence(double validFraction) {
    return Smootherstep5((validFraction - 0.70) / (0.95 - 0.70));
}

double UniquenessConfidence(
    double uniqueness,
    const RegistrationParameters& parameters) {
    return Smootherstep5(
        (uniqueness - parameters.uniquenessTransitionMin) /
        (parameters.uniquenessTransitionMax -
            parameters.uniquenessTransitionMin));
}

double PrecisionConfidence(
    double sigmaRaw,
    const RegistrationParameters& parameters) {
    const double ratio = sigmaRaw / parameters.positionalConfidenceScaleRawPixels;
    return 1.0 / (1.0 + ratio * ratio);
}

struct LevelNodeState {
    bool valid = false;
    RawCoordinate residualRaw;
    RawCoordinate predictionBeforeFineRaw;
    Candidate best;
    Candidate second;
};

bool CompatiblePyramids(
    const CfaPlanePyramid& reference,
    const CfaPlanePyramid& source,
    std::uint32_t requiredLevels) {
    if (!reference.layout.IsValid() || !source.layout.IsValid() ||
        reference.levelCount < requiredLevels ||
        source.levelCount < requiredLevels ||
        reference.rawExtent.width == 0u || reference.rawExtent.height == 0u ||
        source.rawExtent.width == 0u || source.rawExtent.height == 0u) {
        return false;
    }
    for (CfaSite site : kSites) {
        for (std::uint32_t level = 0u; level < requiredLevels; ++level) {
            const CfaPyramidLevel* referenceLevel = FindCfaPyramidLevel(
                reference, site, level);
            const CfaPyramidLevel* sourceLevel = FindCfaPyramidLevel(
                source, site, level);
            if (!referenceLevel || !sourceLevel ||
                !ValidateLevel(*referenceLevel) || !ValidateLevel(*sourceLevel)) {
                return false;
            }
        }
    }
    return true;
}

void RecountGrid(LocalMotionGrid& grid) {
    grid.structuredCount = 0u;
    grid.flatSafeCount = 0u;
    grid.rejectedCount = 0u;
    for (const MotionNode& node : grid.nodes) {
        switch (node.state) {
            case MotionNodeState::Structured: ++grid.structuredCount; break;
            case MotionNodeState::FlatSafe: ++grid.flatSafeCount; break;
            case MotionNodeState::Rejected: ++grid.rejectedCount; break;
        }
    }
    grid.valid = grid.structuredCount + grid.flatSafeCount > 0u;
    grid.failure = grid.valid
        ? LocalMotionFailure::None
        : LocalMotionFailure::NoUsableNodes;
}

} // namespace

bool EstimateLocalMotionDirection(
    const LocalMotionDirectionRequest& request,
    LocalMotionGrid& result,
    std::string* error) {
    result = {};
    if (!request.reference || !request.source ||
        !Finite(request.exposureScale) || request.exposureScale <= 0.0 ||
        request.options.registration.pyramidLevels == 0u ||
        request.options.registration.pyramidLevels >
            request.options.registration.searchRadiiFineToCoarse.size() ||
        request.options.registration.minimumTileValidFraction <= 0.0 ||
        request.options.registration.minimumTileValidFraction > 1.0) {
        result.failure = LocalMotionFailure::InvalidInput;
        result.message = "MFD local-motion request is invalid.";
        return Fail(error, result.message);
    }
    if (!CompatiblePyramids(
            *request.reference,
            *request.source,
            request.options.registration.pyramidLevels)) {
        result.failure = LocalMotionFailure::PyramidMismatch;
        result.message = "MFD local-motion pyramids are missing or incompatible.";
        return Fail(error, result.message);
    }

    result.referenceRawExtent = request.reference->rawExtent;
    result.sourceRawExtent = request.source->rawExtent;
    result.globalWarp = request.globalWarp;
    const double patchRaw =
        2.0 * request.options.registration.finestPatchPlanePixels;
    const double spacingRaw =
        2.0 * request.options.registration.finestStridePlanePixels;
    const double marginRaw = 0.5 * patchRaw + 4.0;
    result.originRawX = marginRaw;
    result.originRawY = marginRaw;
    result.spacingRawX = spacingRaw;
    result.spacingRawY = spacingRaw;
    if (spacingRaw <= 0.0 ||
        static_cast<double>(result.referenceRawExtent.width) <= 2.0 * marginRaw ||
        static_cast<double>(result.referenceRawExtent.height) <= 2.0 * marginRaw) {
        result.failure = LocalMotionFailure::NoGrid;
        result.message = "MFD local-motion image is too small for the configured tile support.";
        return Fail(error, result.message);
    }
    result.width = static_cast<std::uint32_t>(std::floor(
        (static_cast<double>(result.referenceRawExtent.width - 1u) -
            2.0 * marginRaw) / spacingRaw)) + 1u;
    result.height = static_cast<std::uint32_t>(std::floor(
        (static_cast<double>(result.referenceRawExtent.height - 1u) -
            2.0 * marginRaw) / spacingRaw)) + 1u;
    if (result.width == 0u || result.height == 0u ||
        static_cast<std::uint64_t>(result.width) * result.height >
            std::numeric_limits<std::size_t>::max()) {
        result.failure = LocalMotionFailure::NoGrid;
        result.message = "MFD local-motion grid dimensions are invalid.";
        return Fail(error, result.message);
    }
    const std::size_t nodeCount = static_cast<std::size_t>(
        static_cast<std::uint64_t>(result.width) * result.height);
    result.nodes.resize(nodeCount);
    std::vector<LevelNodeState> prior(nodeCount);
    std::vector<LevelNodeState> current(nodeCount);
    auto observedAt=std::chrono::steady_clock::time_point{};
    const auto observeSearch=[&] {try {
        if(!request.reportObservation)return;
        const auto now=std::chrono::steady_clock::now();
        if(now-observedAt<std::chrono::milliseconds(100))return;
        observedAt=now;
        // Only completed wavefronts are inspected; no worker owns these nodes.
        LocalMotionGrid sample;sample.referenceRawExtent=result.referenceRawExtent;
        sample.sourceRawExtent=result.sourceRawExtent;sample.globalWarp=result.globalWarp;
        const auto stride=std::max<std::size_t>(1,(nodeCount+383)/384);
        for(std::size_t i=0;i<nodeCount;i+=stride)if(current[i].valid) {
            MotionNode node;node.centerRaw={result.originRawX+(i%result.width)*spacingRaw,
                result.originRawY+(i/result.width)*spacingRaw};
            node.residualRaw=current[i].residualRaw;node.state=MotionNodeState::Structured;
            sample.nodes.push_back(node);
        }
        if(!sample.nodes.empty())request.reportObservation(sample,true);
        }catch(...) { /* Optional observers cannot invalidate a motion solution. */ }
    };
    std::unique_ptr<PersistentParallelExecutor> parallel;
    if (request.workerCount > 1u) {
        try {
            parallel = std::make_unique<PersistentParallelExecutor>(
                request.workerCount);
        } catch (...) {
            // Thread creation is an optimization. The exact single-worker
            // path remains available under process or OS thread pressure.
            parallel.reset();
        }
    }
    const std::uint64_t totalProgressRows =
        static_cast<std::uint64_t>(result.height) *
        (static_cast<std::uint64_t>(
            request.options.registration.pyramidLevels) + 2u);
    std::uint64_t completedProgressRows = 0u;
    const auto reportProgress = [&]() {
        if (request.reportProgress) {
            request.reportProgress(totalProgressRows == 0u
                ? 1.0
                : static_cast<double>(completedProgressRows) /
                    static_cast<double>(totalProgressRows));
        }
    };
    reportProgress();

    for (std::int32_t levelSigned =
            static_cast<std::int32_t>(
                request.options.registration.pyramidLevels) - 1;
         levelSigned >= 0;
        --levelSigned) {
        const std::uint32_t level = static_cast<std::uint32_t>(levelSigned);
        std::fill(current.begin(), current.end(), LevelNodeState {});
        if (request.evaluateDiscreteCandidates) {
            struct AcceleratedNodeBatch {
                std::size_t index = 0u;
                RawCoordinate centerRaw;
                RawCoordinate predictionRaw;
                std::size_t scoreOffset = 0u;
                std::vector<Candidate> candidates;
            };
            const std::uint64_t diagonalCount =
                static_cast<std::uint64_t>(result.width) + result.height - 1u;
            const std::uint64_t progressBase = completedProgressRows;
            for (std::uint64_t diagonal = 0u;
                 diagonal < diagonalCount;
                 ++diagonal) {
                if (request.shouldCancel && request.shouldCancel()) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message =
                        "MFD local-motion processing was canceled.";
                    return Fail(error, result.message);
                }
                std::vector<AcceleratedNodeBatch> nodes;
                std::vector<LocalMotionDiscreteCandidate> accelerated;
                const std::uint32_t firstX = diagonal >= result.height
                    ? static_cast<std::uint32_t>(diagonal - result.height + 1u)
                    : 0u;
                const std::uint32_t lastX = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(diagonal, result.width - 1u));
                nodes.reserve(lastX - firstX + 1u);
                for (std::uint32_t gridX = firstX;
                     gridX <= lastX;
                     ++gridX) {
                    const std::uint32_t gridY =
                        static_cast<std::uint32_t>(diagonal) - gridX;
                    const std::size_t index = static_cast<std::size_t>(
                        gridY * result.width + gridX);
                    AcceleratedNodeBatch node;
                    node.index = index;
                    node.centerRaw = {
                        result.originRawX + gridX * result.spacingRawX,
                        result.originRawY + gridY * result.spacingRawY
                    };
                    if (level + 1u <
                            request.options.registration.pyramidLevels &&
                        prior[index].valid) {
                        node.predictionRaw = prior[index].residualRaw;
                    }
                    std::vector<RawCoordinate> seeds;
                    AddUniqueSeed(seeds, node.predictionRaw);
                    if (level + 1u <
                            request.options.registration.pyramidLevels &&
                        prior[index].valid &&
                        Finite(prior[index].second.statistics.robustCost)) {
                        AddUniqueSeed(
                            seeds, prior[index].second.residualRaw);
                    }
                    if (gridX > 0u && current[index - 1u].valid) {
                        AddUniqueSeed(
                            seeds, current[index - 1u].residualRaw);
                    }
                    if (gridY > 0u &&
                        current[index - result.width].valid) {
                        AddUniqueSeed(
                            seeds,
                            current[index - result.width].residualRaw);
                    }
                    if (gridX > 0u && gridY > 0u &&
                        current[index - result.width - 1u].valid) {
                        AddUniqueSeed(
                            seeds,
                            current[index - result.width - 1u].residualRaw);
                    }
                    BuildDiscreteCandidates(
                        request, level, seeds, node.candidates);
                    node.scoreOffset = accelerated.size();
                    const std::uint32_t patch = level == 0u
                        ? request.options.registration.finestPatchPlanePixels
                        : request.options.registration.coarsePatchLevelPixels;
                    for (const Candidate& candidate : node.candidates) {
                        accelerated.push_back({
                            node.centerRaw,
                            candidate.residualRaw,
                            level,
                            patch
                        });
                    }
                    nodes.push_back(std::move(node));
                }

                std::vector<LocalMotionDiscreteScore> scores;
                std::string acceleratorError;
                if (!request.evaluateDiscreteCandidates(
                        request, accelerated, scores, acceleratorError) ||
                    scores.size() != accelerated.size()) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message = acceleratorError.empty()
                        ? "The local-motion accelerator returned an invalid score batch."
                        : acceleratorError;
                    return Fail(error, result.message);
                }
                const auto verifyNode = [&](std::size_t nodeIndex) {
                    AcceleratedNodeBatch& node = nodes[nodeIndex];
                    for (std::size_t candidateIndex = 0u;
                         candidateIndex < node.candidates.size();
                         ++candidateIndex) {
                        const LocalMotionDiscreteScore& score =
                            scores[node.scoreOffset + candidateIndex];
                        PatchStatistics& statistics =
                            node.candidates[candidateIndex].statistics;
                        statistics.valid = score.valid &&
                            Finite(score.validFraction) &&
                            Finite(score.robustCost) &&
                            Finite(score.cappedChiSquared);
                        statistics.validCount = score.validCount;
                        statistics.nominalCount =
                            static_cast<std::uint64_t>(
                                accelerated[node.scoreOffset + candidateIndex].
                                    patchLevelPixels) *
                            accelerated[node.scoreOffset + candidateIndex].
                                patchLevelPixels *
                            kSites.size();
                        statistics.validFraction = score.validFraction;
                        statistics.robustCost = score.robustCost;
                        statistics.cappedChiSquared = score.cappedChiSquared;
                    }
                    Candidate best;
                    Candidate second;
                    if (!VerifyAcceleratedCandidates(
                            request,
                            node.centerRaw,
                            level,
                            node.predictionRaw,
                            node.candidates,
                            best,
                            second)) {
                        return;
                    }
                    LevelNodeState& state = current[node.index];
                    state.valid = true;
                    state.residualRaw = best.residualRaw;
                    state.predictionBeforeFineRaw = level == 0u
                        ? node.predictionRaw
                        : best.residualRaw;
                    state.best = std::move(best);
                    state.second = std::move(second);
                };
                try {
                    if (parallel) {
                        parallel->Run(nodes.size(), verifyNode);
                    } else {
                        for (std::size_t nodeIndex = 0u;
                             nodeIndex < nodes.size();
                             ++nodeIndex) {
                            verifyNode(nodeIndex);
                        }
                    }
                } catch (const std::bad_alloc&) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message =
                        "CPU verification of GPU local-motion scores exceeded available memory.";
                    return Fail(error, result.message);
                } catch (...) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message =
                        "CPU verification of GPU local-motion scores failed unexpectedly.";
                    return Fail(error, result.message);
                }
                observeSearch();
                if (request.reportProgress) {
                    request.reportProgress(
                        static_cast<double>(progressBase) /
                            static_cast<double>(totalProgressRows) +
                        (static_cast<double>(result.height) /
                            static_cast<double>(totalProgressRows)) *
                            (static_cast<double>(diagonal + 1u) /
                                static_cast<double>(diagonalCount)));
                }
            }
            completedProgressRows += result.height;
            reportProgress();
        } else {
            const std::uint64_t diagonalCount =
                static_cast<std::uint64_t>(result.width) + result.height - 1u;
            const std::uint64_t progressBase = completedProgressRows;
            for (std::uint64_t diagonal = 0u;
                 diagonal < diagonalCount;
                 ++diagonal) {
                if (request.shouldCancel && request.shouldCancel()) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message =
                        "MFD local-motion processing was canceled.";
                    return Fail(error, result.message);
                }
                const std::uint32_t firstX = diagonal >= result.height
                    ? static_cast<std::uint32_t>(diagonal - result.height + 1u)
                    : 0u;
                const std::uint32_t lastX = static_cast<std::uint32_t>(
                    std::min<std::uint64_t>(diagonal, result.width - 1u));
                const std::size_t nodeCountOnDiagonal = lastX - firstX + 1u;
                const auto searchNode = [&](std::size_t nodeIndex) {
                    const std::uint32_t gridX =
                        firstX + static_cast<std::uint32_t>(nodeIndex);
                    const std::uint32_t gridY =
                        static_cast<std::uint32_t>(diagonal) - gridX;
                    const std::size_t index = static_cast<std::size_t>(
                        gridY * result.width + gridX);
                    const RawCoordinate center {
                        result.originRawX + gridX * result.spacingRawX,
                        result.originRawY + gridY * result.spacingRawY
                    };
                    RawCoordinate prediction {};
                    if (level + 1u <
                            request.options.registration.pyramidLevels &&
                        prior[index].valid) {
                        prediction = prior[index].residualRaw;
                    }
                    std::vector<RawCoordinate> seeds;
                    AddUniqueSeed(seeds, prediction);
                    if (level + 1u <
                            request.options.registration.pyramidLevels &&
                        prior[index].valid &&
                        Finite(prior[index].second.statistics.robustCost)) {
                        AddUniqueSeed(
                            seeds, prior[index].second.residualRaw);
                    }
                    if (gridX > 0u && current[index - 1u].valid) {
                        AddUniqueSeed(
                            seeds, current[index - 1u].residualRaw);
                    }
                    if (gridY > 0u &&
                        current[index - result.width].valid) {
                        AddUniqueSeed(
                            seeds,
                            current[index - result.width].residualRaw);
                    }
                    if (gridX > 0u && gridY > 0u &&
                        current[index - result.width - 1u].valid) {
                        AddUniqueSeed(
                            seeds,
                            current[index - result.width - 1u].residualRaw);
                    }
                    Candidate best;
                    Candidate second;
                    if (!SearchTileAtLevel(
                            request,
                            center,
                            level,
                            seeds,
                            prediction,
                            best,
                            second)) {
                        return;
                    }
                    current[index].valid = true;
                    current[index].residualRaw = best.residualRaw;
                    current[index].predictionBeforeFineRaw = level == 0u
                        ? prediction
                        : best.residualRaw;
                    current[index].best = std::move(best);
                    current[index].second = std::move(second);
                };
                try {
                    if (parallel) {
                        parallel->Run(nodeCountOnDiagonal, searchNode);
                    } else {
                        for (std::size_t nodeIndex = 0u;
                             nodeIndex < nodeCountOnDiagonal;
                             ++nodeIndex) {
                            searchNode(nodeIndex);
                        }
                    }
                } catch (const std::bad_alloc&) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message =
                        "CPU local-motion search exceeded available memory.";
                    return Fail(error, result.message);
                } catch (...) {
                    result.failure = LocalMotionFailure::NumericalFailure;
                    result.message =
                        "CPU local-motion search failed unexpectedly.";
                    return Fail(error, result.message);
                }
                observeSearch();
                if (request.reportProgress) {
                    request.reportProgress(
                        static_cast<double>(progressBase) /
                            static_cast<double>(totalProgressRows) +
                        (static_cast<double>(result.height) /
                            static_cast<double>(totalProgressRows)) *
                            (static_cast<double>(diagonal + 1u) /
                                static_cast<double>(diagonalCount)));
                }
            }
            completedProgressRows += result.height;
            reportProgress();
        }
        prior.swap(current);
    }

    const std::uint64_t refinementProgressBase = completedProgressRows;
    std::atomic<std::uint64_t> refinedRows { 0u };
    std::atomic<bool> refinementCanceled { false };
    std::mutex refinementProgressMutex;
    const auto refineRow = [&](std::size_t rowIndex) {
        const std::uint32_t gridY = static_cast<std::uint32_t>(rowIndex);
        if (request.shouldCancel && request.shouldCancel()) {
            refinementCanceled.store(true, std::memory_order_relaxed);
            return;
        }
        for (std::uint32_t gridX = 0u; gridX < result.width; ++gridX) {
            const std::size_t index = static_cast<std::size_t>(
                gridY * result.width + gridX);
            MotionNode& node = result.nodes[index];
            node.gridX = gridX;
            node.gridY = gridY;
            node.centerRaw = {
                result.originRawX + gridX * result.spacingRawX,
                result.originRawY + gridY * result.spacingRawY
            };
            if (!prior[index].valid) {
                node.rejectReason = MotionNodeRejectReason::NoDiscreteCandidate;
                continue;
            }
            node.discreteResidualRaw = prior[index].best.residualRaw;
            node.secondBestCost = prior[index].second.statistics.robustCost;
            node.robustCost = prior[index].best.statistics.robustCost;
            node.validFraction = prior[index].best.statistics.validFraction;
            if (!Finite(node.secondBestCost)) {
                node.rejectReason = MotionNodeRejectReason::AmbiguousMatch;
                continue;
            }
            node.uniqueness = std::max(
                0.0,
                (node.secondBestCost - node.robustCost) /
                    (node.robustCost + request.options.registration.
                        localNumericalVarianceFloor));

            RefinedTile refined;
            if (!RefineTileSubpixel(
                    request,
                    node.centerRaw,
                    node.discreteResidualRaw,
                    refined)) {
                node.rejectReason = MotionNodeRejectReason::SubpixelFailure;
                continue;
            }
            node.residualRaw = refined.residualRaw;
            node.robustCost = refined.statistics.robustCost;
            node.validFraction = refined.statistics.validFraction;
            node.cappedChiSquared = refined.statistics.cappedChiSquared;
            node.hessianCondition = refined.hessianCondition;
            node.positionalSigmaRaw = refined.positionSigmaRaw;
            SymmetricRawCovariance globalCovariance;
            if (!GlobalCovarianceAt(request, node.centerRaw, globalCovariance)) {
                node.rejectReason = MotionNodeRejectReason::InvalidCovariance;
                continue;
            }
            const double floorVariance =
                request.options.registration.warpCovarianceFloorRawPixels *
                request.options.registration.warpCovarianceFloorRawPixels;

            if (refined.observable) {
                if (node.uniqueness <=
                    request.options.registration.uniquenessTransitionMin) {
                    node.rejectReason = MotionNodeRejectReason::AmbiguousMatch;
                    continue;
                }
                SymmetricRawCovariance total = AddCovariance(
                    refined.localCovarianceRaw, globalCovariance);
                total.xxRawPixelsSquared += floorVariance;
                total.yyRawPixelsSquared += floorVariance;
                if (!ClampCovariance(
                        total,
                        request.options.registration.
                            warpCovarianceEigenvalueMinRawPixels,
                        request.options.registration.
                            warpCovarianceEigenvalueMaxRawPixels,
                        node.covarianceRaw)) {
                    node.rejectReason = MotionNodeRejectReason::InvalidCovariance;
                    continue;
                }
                node.positionalSigmaRaw = std::sqrt(std::max(
                    0.0,
                    0.5 * (node.covarianceRaw.xxRawPixelsSquared +
                        node.covarianceRaw.yyRawPixelsSquared)));
                node.state = MotionNodeState::Structured;
                node.confidence =
                    ResidualConfidence(node.cappedChiSquared) *
                    PrecisionConfidence(
                        node.positionalSigmaRaw,
                        request.options.registration) *
                    UniquenessConfidence(
                        node.uniqueness,
                        request.options.registration) *
                    CoverageConfidence(node.validFraction);
                continue;
            }

            PatchStatistics flat;
            if (!EvaluatePatch(
                    request,
                    node.centerRaw,
                    0u,
                    prior[index].predictionBeforeFineRaw,
                    request.options.registration.finestPatchPlanePixels,
                    false,
                    true,
                    flat) ||
                !flat.valid ||
                flat.validFraction <
                    request.options.registration.flatSafeMinimumValidFraction ||
                flat.validCount != flat.nominalCount ||
                flat.cappedChiSquared >
                    request.options.registration.flatSafeParentResidualLimit ||
                flat.flatPhotometricCost >
                    request.options.registration.flatSafePhotometricCostLimit) {
                node.rejectReason = MotionNodeRejectReason::UnobservableUnsafe;
                continue;
            }
            node.residualRaw = prior[index].predictionBeforeFineRaw;
            node.validFraction = flat.validFraction;
            node.robustCost = flat.robustCost;
            node.cappedChiSquared = flat.cappedChiSquared;
            node.uniqueness = 1.0;
            const double flatVariance =
                request.options.registration.flatSafeCovarianceRawPixels *
                request.options.registration.flatSafeCovarianceRawPixels;
            SymmetricRawCovariance flatCovariance {
                flatVariance, 0.0, flatVariance
            };
            flatCovariance = AddCovariance(flatCovariance, globalCovariance);
            flatCovariance.xxRawPixelsSquared += floorVariance;
            flatCovariance.yyRawPixelsSquared += floorVariance;
            if (!ClampCovariance(
                    flatCovariance,
                    request.options.registration.warpCovarianceEigenvalueMinRawPixels,
                    request.options.registration.warpCovarianceEigenvalueMaxRawPixels,
                    node.covarianceRaw)) {
                node.rejectReason = MotionNodeRejectReason::InvalidCovariance;
                continue;
            }
            node.positionalSigmaRaw = std::sqrt(std::max(
                0.0,
                0.5 * (node.covarianceRaw.xxRawPixelsSquared +
                    node.covarianceRaw.yyRawPixelsSquared)));
            node.state = MotionNodeState::FlatSafe;
            node.confidence = ResidualConfidence(node.cappedChiSquared) *
                CoverageConfidence(node.validFraction);
        }
        const std::uint64_t completed =
            refinedRows.fetch_add(1u, std::memory_order_relaxed) + 1u;
        if (request.reportProgress) {
            std::lock_guard<std::mutex> lock(refinementProgressMutex);
            request.reportProgress(
                static_cast<double>(refinementProgressBase + completed) /
                static_cast<double>(totalProgressRows));
        }
    };
    try {
        if (parallel) {
            parallel->Run(result.height, refineRow);
        } else {
            for (std::uint32_t gridY = 0u;
                 gridY < result.height;
                 ++gridY) {
                refineRow(gridY);
            }
        }
    } catch (const std::bad_alloc&) {
        result.failure = LocalMotionFailure::NumericalFailure;
        result.message =
            "MFD local-motion subpixel refinement exceeded available memory.";
        return Fail(error, result.message);
    } catch (...) {
        result.failure = LocalMotionFailure::NumericalFailure;
        result.message =
            "MFD local-motion subpixel refinement failed unexpectedly.";
        return Fail(error, result.message);
    }
    if (refinementCanceled.load(std::memory_order_relaxed)) {
        result.failure = LocalMotionFailure::NumericalFailure;
        result.message = "MFD local-motion processing was canceled.";
        return Fail(error, result.message);
    }
    completedProgressRows += result.height;
    reportProgress();

    for (MotionNode& node : result.nodes) {
        if (node.gridX == 0u) {
            if (request.shouldCancel && request.shouldCancel()) {
                result.failure = LocalMotionFailure::NumericalFailure;
                result.message = "MFD local-motion processing was canceled.";
                return Fail(error, result.message);
            }
            completedProgressRows =
                totalProgressRows - result.height + node.gridY;
            reportProgress();
        }
        if (node.state != MotionNodeState::FlatSafe) continue;
        std::vector<RawCoordinate> structuredNeighbors;
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const std::int32_t neighborX =
                    static_cast<std::int32_t>(node.gridX) + dx;
                const std::int32_t neighborY =
                    static_cast<std::int32_t>(node.gridY) + dy;
                if (neighborX < 0 || neighborY < 0 ||
                    neighborX >= static_cast<std::int32_t>(result.width) ||
                    neighborY >= static_cast<std::int32_t>(result.height)) {
                    continue;
                }
                const MotionNode& neighbor = result.nodes[static_cast<std::size_t>(
                    neighborY * static_cast<std::int32_t>(result.width) + neighborX)];
                if (neighbor.state != MotionNodeState::Structured) continue;
                structuredNeighbors.push_back(neighbor.residualRaw);
            }
        }
        RawCoordinate prediction {};
        if (!structuredNeighbors.empty()) {
            for (RawCoordinate neighbor : structuredNeighbors) {
                prediction = Add(prediction, neighbor);
            }
            prediction = Scale(
                prediction, 1.0 / static_cast<double>(structuredNeighbors.size()));
        }
        bool neighborAccepted = true;
        for (RawCoordinate neighbor : structuredNeighbors) {
            if (std::sqrt(SquaredNorm(Subtract(neighbor, prediction))) >
                request.options.registration.
                    flatSafeNeighborDifferenceLimitRawPixels) {
                neighborAccepted = false;
            }
        }
        PatchStatistics predictedFlat;
        const bool predictionSafe = neighborAccepted &&
            EvaluatePatch(
                request,
                node.centerRaw,
                0u,
                prediction,
                request.options.registration.finestPatchPlanePixels,
                false,
                true,
                predictedFlat) &&
            predictedFlat.valid &&
            predictedFlat.validCount == predictedFlat.nominalCount &&
            predictedFlat.validFraction >=
                request.options.registration.flatSafeMinimumValidFraction &&
            predictedFlat.cappedChiSquared <=
                request.options.registration.flatSafeParentResidualLimit &&
            predictedFlat.flatPhotometricCost <=
                request.options.registration.flatSafePhotometricCostLimit;
        if (!predictionSafe) {
            node.state = MotionNodeState::Rejected;
            node.rejectReason = MotionNodeRejectReason::UnobservableUnsafe;
            node.confidence = 0.0;
        } else {
            node.residualRaw = prediction;
            node.robustCost = predictedFlat.robustCost;
            node.validFraction = predictedFlat.validFraction;
            node.cappedChiSquared = predictedFlat.cappedChiSquared;
            node.confidence = ResidualConfidence(node.cappedChiSquared) *
                CoverageConfidence(node.validFraction);
        }
    }
    completedProgressRows = totalProgressRows;
    reportProgress();

    RecountGrid(result);
    if (!result.valid) {
        result.message = "MFD local registration found no usable motion nodes.";
        return Fail(error, result.message);
    }
    result.message = "MFD local registration completed.";
    return true;
}

bool EvaluateLocalMotionField(
    const LocalMotionGrid& grid,
    RawCoordinate referenceRaw,
    const LocalMotionOptions& options,
    LocalMotionFieldSample& result,
    std::string* error) {
    result = {};
    result.referenceRaw = referenceRaw;
    if (!grid.valid || grid.width == 0u || grid.height == 0u ||
        grid.nodes.size() != static_cast<std::size_t>(
            static_cast<std::uint64_t>(grid.width) * grid.height) ||
        !Finite(referenceRaw) || grid.spacingRawX <= 0.0 ||
        grid.spacingRawY <= 0.0) {
        result.rejectReason = MotionNodeRejectReason::NumericalFailure;
        result.message = "MFD local-motion field is invalid.";
        return Fail(error, result.message);
    }
    const double gridX = (referenceRaw.x - grid.originRawX) / grid.spacingRawX;
    const double gridY = (referenceRaw.y - grid.originRawY) / grid.spacingRawY;
    if (gridX < -1.0e-12 || gridY < -1.0e-12 ||
        gridX > static_cast<double>(grid.width - 1u) + 1.0e-12 ||
        gridY > static_cast<double>(grid.height - 1u) + 1.0e-12) {
        result.rejectReason = MotionNodeRejectReason::SourceBorder;
        result.message = "MFD local-motion coordinate lies outside the motion grid.";
        return Fail(error, result.message);
    }
    const std::uint32_t x0 = grid.width == 1u
        ? 0u
        : std::min<std::uint32_t>(
            static_cast<std::uint32_t>(std::floor(std::max(0.0, gridX))),
            grid.width - 2u);
    const std::uint32_t y0 = grid.height == 1u
        ? 0u
        : std::min<std::uint32_t>(
            static_cast<std::uint32_t>(std::floor(std::max(0.0, gridY))),
            grid.height - 2u);
    const std::uint32_t x1 = grid.width == 1u ? 0u : x0 + 1u;
    const std::uint32_t y1 = grid.height == 1u ? 0u : y0 + 1u;
    const double tx = grid.width == 1u ? 0.0 : Clamp01(gridX - x0);
    const double ty = grid.height == 1u ? 0.0 : Clamp01(gridY - y0);
    const std::array<std::uint32_t, 4> nodeX { x0, x1, x0, x1 };
    const std::array<std::uint32_t, 4> nodeY { y0, y0, y1, y1 };
    const std::array<double, 4> bilinear {
        (1.0 - tx) * (1.0 - ty),
        tx * (1.0 - ty),
        (1.0 - tx) * ty,
        tx * ty
    };
    std::array<double, 4> alpha {};
    double alphaSum = 0.0;
    double gridConfidence = 0.0;
    RawCoordinate mean {};
    for (std::size_t corner = 0u; corner < 4u; ++corner) {
        const MotionNode& node = grid.nodes[static_cast<std::size_t>(
            nodeY[corner] * grid.width + nodeX[corner])];
        const double confidence = node.state == MotionNodeState::Rejected
            ? 0.0
            : Clamp01(node.confidence);
        alpha[corner] = bilinear[corner] * confidence;
        alphaSum += alpha[corner];
        gridConfidence += bilinear[corner] * confidence;
        mean.x += alpha[corner] * node.residualRaw.x;
        mean.y += alpha[corner] * node.residualRaw.y;
    }
    if (!Finite(alphaSum) || alphaSum <=
        options.registration.interpolationWeightEpsilon) {
        result.rejectReason = MotionNodeRejectReason::InsufficientCoverage;
        result.message = "MFD local-motion interpolation has no confident support.";
        return Fail(error, result.message);
    }
    mean = Scale(mean, 1.0 / alphaSum);
    SymmetricRawCovariance covariance {};
    SymmetricRawCovariance disagreement {};
    for (std::size_t corner = 0u; corner < 4u; ++corner) {
        if (alpha[corner] <= 0.0) continue;
        const MotionNode& node = grid.nodes[static_cast<std::size_t>(
            nodeY[corner] * grid.width + nodeX[corner])];
        const double normalizedWeight = alpha[corner] / alphaSum;
        const SymmetricRawCovariance scatter = OuterProduct(
            Subtract(node.residualRaw, mean));
        covariance = AddCovariance(
            covariance,
            ScaleCovariance(
                AddCovariance(node.covarianceRaw, scatter),
                normalizedWeight));
        disagreement = AddCovariance(
            disagreement,
            ScaleCovariance(scatter, normalizedWeight));
    }
    double disagreementLow = 0.0;
    double disagreementHigh = 0.0;
    if (!CovarianceEigenvalues(
            disagreement, disagreementLow, disagreementHigh)) {
        result.rejectReason = MotionNodeRejectReason::InvalidCovariance;
        result.message = "MFD local-motion disagreement covariance is invalid.";
        return Fail(error, result.message);
    }
    result.disagreementSigmaRaw = std::sqrt(std::max(0.0, disagreementHigh));
    result.residualRaw = mean;
    result.disagreementCovarianceRaw = disagreement;
    result.gridConfidence = Clamp01(gridConfidence);
    if (result.disagreementSigmaRaw >
        options.registration.motionDisagreementHardLimitRawPixels) {
        result.rejectReason = MotionNodeRejectReason::MotionFieldAmbiguous;
        result.message = "MFD local-motion nodes disagree beyond the hard limit.";
        return Fail(error, result.message);
    }
    if (!ClampCovariance(
            covariance,
            options.registration.warpCovarianceEigenvalueMinRawPixels,
            options.registration.warpCovarianceEigenvalueMaxRawPixels,
            result.covarianceRaw)) {
        result.rejectReason = MotionNodeRejectReason::InvalidCovariance;
        result.message = "MFD interpolated local-motion covariance is invalid.";
        return Fail(error, result.message);
    }
    const double disagreementRatio = result.disagreementSigmaRaw /
        options.registration.motionDisagreementConfidenceScaleRawPixels;
    result.alignmentConfidence = result.gridConfidence *
        std::exp(-0.5 * disagreementRatio * disagreementRatio);
    result.sourceRaw = Add(grid.globalWarp.Map(referenceRaw), mean);
    if (!Finite(result.sourceRaw) || result.sourceRaw.x < 0.0 ||
        result.sourceRaw.y < 0.0 ||
        result.sourceRaw.x > static_cast<double>(grid.sourceRawExtent.width - 1u) ||
        result.sourceRaw.y > static_cast<double>(grid.sourceRawExtent.height - 1u)) {
        result.rejectReason = MotionNodeRejectReason::SourceBorder;
        result.message = "MFD local-motion warp leaves the source active area.";
        return Fail(error, result.message);
    }
    result.valid = true;
    result.rejectReason = MotionNodeRejectReason::None;
    result.message = "MFD local-motion field sample is valid.";
    return true;
}

bool InvertAffineModel(
    const AffineModel& model,
    AffineModel& inverse,
    std::string* error) {
    const double determinant =
        model.linear[0] * model.linear[3] -
        model.linear[1] * model.linear[2];
    if (!Finite(determinant) || std::abs(determinant) <= 1.0e-12 ||
        !Finite(model.centerRaw) || !Finite(model.translationRaw)) {
        return Fail(error, "MFD affine model cannot be inverted safely.");
    }
    inverse = {};
    inverse.centerRaw = model.centerRaw;
    inverse.linear = {
        model.linear[3] / determinant,
        -model.linear[1] / determinant,
        -model.linear[2] / determinant,
        model.linear[0] / determinant
    };
    inverse.translationRaw = {
        -(inverse.linear[0] * model.translationRaw.x +
            inverse.linear[1] * model.translationRaw.y),
        -(inverse.linear[2] * model.translationRaw.x +
            inverse.linear[3] * model.translationRaw.y)
    };
    return Finite(inverse.translationRaw);
}

bool RefineBidirectionalMotionPoint(const BidirectionalLocalMotionRequest& request,
    RawCoordinate referenceRaw,RawCoordinate sourceRaw,MotionNode& result) {
    result={};
    if(!request.reference||!request.alternate||!Finite(request.exposureScale)||
       request.exposureScale<=0||!Finite(referenceRaw)||!Finite(sourceRaw)||
       (request.shouldCancel&&request.shouldCancel()))return false;
    LocalMotionDirectionRequest forward;
    forward.reference=request.reference;forward.source=request.alternate;
    forward.globalWarp=request.referenceToAlternate;forward.exposureScale=request.exposureScale;
    forward.options=request.options;forward.globalCovariance=request.forwardGlobalCovariance;
    forward.shouldCancel=request.shouldCancel;
    const auto seed=Subtract(sourceRaw,forward.globalWarp.Map(referenceRaw));
    PatchStatistics before;
    if(!EvaluatePatch(forward,referenceRaw,0,seed,forward.options.registration.finestPatchPlanePixels,
        true,false,before)||!before.valid||before.robustCost<.6)return false;
    RefinedTile refined;
    if(!RefineTileSubpixel(forward,referenceRaw,seed,refined)||!refined.observable||
       refined.statistics.robustCost>=before.robustCost*.95||
       refined.statistics.validCount<before.validCount)return false;
    const auto correction=Subtract(refined.residualRaw,seed);
    const double displacement=std::sqrt(SquaredNorm(correction));
    if(displacement<.02||displacement>1.5)return false;
    for(unsigned c=0;c<4;++c) {
        if(!before.siteCount[c]||refined.statistics.siteCount[c]<before.siteCount[c])return false;
        if(refined.statistics.siteSquared[c]/refined.statistics.siteCount[c]>
            before.siteSquared[c]/before.siteCount[c]*1.02+.02)return false;
    }
    if(request.shouldCancel&&request.shouldCancel())return false;
    LocalMotionDirectionRequest reverse;
    reverse.reference=request.alternate;reverse.source=request.reference;
    if(!InvertAffineModel(forward.globalWarp,reverse.globalWarp))return false;
    reverse.exposureScale=1/request.exposureScale;reverse.options=request.options;
    reverse.globalCovariance=request.reverseGlobalCovariance;reverse.shouldCancel=request.shouldCancel;
    const auto mapped=Add(forward.globalWarp.Map(referenceRaw),refined.residualRaw);
    RefinedTile reversed;
    if(!RefineTileSubpixel(reverse,mapped,Subtract(referenceRaw,reverse.globalWarp.Map(mapped)),reversed)||
       !reversed.observable)return false;
    const auto closure=Subtract(Add(reverse.globalWarp.Map(mapped),reversed.residualRaw),referenceRaw);
    auto covariance=AddCovariance(refined.localCovarianceRaw,reversed.localCovarianceRaw);
    const auto& p=request.options.registration;
    const double floor=p.forwardBackwardCovarianceFloorRawPixels*p.forwardBackwardCovarianceFloorRawPixels;
    covariance.xxRawPixelsSquared+=floor;covariance.yyRawPixelsSquared+=floor;
    SymmetricRawCovariance inverse;
    if(!InvertCovariance(covariance,inverse))return false;
    const double mahalanobis=std::max(0.,QuadraticForm(closure,inverse));
    if(!Finite(mahalanobis)||mahalanobis>p.forwardBackwardMahalanobisHardLimit||
       std::sqrt(SquaredNorm(closure))>p.forwardBackwardEuclideanHardLimitRawPixels)return false;
    result.centerRaw=referenceRaw;result.residualRaw=refined.residualRaw;
    result.discreteResidualRaw=seed;result.covarianceRaw=refined.localCovarianceRaw;
    result.state=MotionNodeState::Structured;result.rejectReason=MotionNodeRejectReason::None;
    result.robustCost=refined.statistics.robustCost;result.secondBestCost=before.robustCost;
    result.validFraction=refined.statistics.validFraction;result.cappedChiSquared=refined.statistics.cappedChiSquared;
    result.confidence=ResidualConfidence(result.cappedChiSquared)*CoverageConfidence(result.validFraction)*
        std::exp(-.5*std::max(mahalanobis-2.,0.));
    result.forwardBackwardErrorRaw=closure;result.forwardBackwardMahalanobis=mahalanobis;
    result.forwardBackwardEuclideanRaw=std::sqrt(SquaredNorm(closure));
    return result.confidence>.2;
}

bool EstimateBidirectionalLocalMotion(
    const BidirectionalLocalMotionRequest& request,
    BidirectionalLocalMotionResult& result,
    std::string* error) {
    result = {};
    if (!request.reference || !request.alternate ||
        !Finite(request.exposureScale) || request.exposureScale <= 0.0) {
        result.failure = LocalMotionFailure::InvalidInput;
        result.message = "MFD bidirectional local-motion request is invalid.";
        return Fail(error, result.message);
    }
    if (request.shouldCancel && request.shouldCancel()) {
        result.failure = LocalMotionFailure::NumericalFailure;
        result.message = "MFD bidirectional local-motion processing was canceled.";
        return Fail(error, result.message);
    }
    if (request.reportProgress) request.reportProgress(0.0);

    AffineModel inverseGlobal;
    std::string localError;
    if (!InvertAffineModel(
            request.referenceToAlternate, inverseGlobal, &localError)) {
        result.failure = LocalMotionFailure::InvalidInput;
        result.message = localError;
        return Fail(error, result.message);
    }

    LocalMotionDirectionRequest forwardRequest;
    forwardRequest.reference = request.reference;
    forwardRequest.source = request.alternate;
    forwardRequest.globalWarp = request.referenceToAlternate;
    forwardRequest.exposureScale = request.exposureScale;
    forwardRequest.globalCovariance = request.forwardGlobalCovariance;
    forwardRequest.options = request.options;
    forwardRequest.workerCount = request.evaluateDiscreteCandidates
        ? std::max(1u, request.workerCount)
        : std::max(1u, request.workerCount / 2u);
    forwardRequest.evaluateDiscreteCandidates =
        request.evaluateDiscreteCandidates;
    forwardRequest.shouldCancel = request.shouldCancel;
    forwardRequest.reportObservation = request.reportObservation;
    LocalMotionDirectionRequest reverseRequest;
    reverseRequest.reference = request.alternate;
    reverseRequest.source = request.reference;
    reverseRequest.globalWarp = inverseGlobal;
    reverseRequest.exposureScale = 1.0 / request.exposureScale;
    reverseRequest.globalCovariance = request.reverseGlobalCovariance;
    reverseRequest.options = request.options;
    reverseRequest.workerCount = forwardRequest.workerCount;
    reverseRequest.evaluateDiscreteCandidates =
        request.evaluateDiscreteCandidates;
    reverseRequest.shouldCancel = request.shouldCancel;
    bool forwardValid = false;
    bool reverseValid = false;
    std::string forwardError;
    std::string reverseError;
    if (request.workerCount >= 2u &&
        !request.evaluateDiscreteCandidates) {
        std::atomic<double> forwardFraction { 0.0 };
        std::atomic<double> reverseFraction { 0.0 };
        std::mutex progressMutex;
        const auto reportCombined = [&]() {
            if (!request.reportProgress) return;
            std::lock_guard<std::mutex> lock(progressMutex);
            request.reportProgress(0.45 * (
                forwardFraction.load(std::memory_order_relaxed) +
                reverseFraction.load(std::memory_order_relaxed)));
        };
        forwardRequest.reportProgress = [&](double fraction) {
            forwardFraction.store(
                std::clamp(fraction, 0.0, 1.0),
                std::memory_order_relaxed);
            reportCombined();
        };
        reverseRequest.reportProgress = [&](double fraction) {
            reverseFraction.store(
                std::clamp(fraction, 0.0, 1.0),
                std::memory_order_relaxed);
            reportCombined();
        };

        std::thread reverseWorker;
        try {
            reverseWorker = std::thread([&]() {
                try {
                    reverseValid = EstimateLocalMotionDirection(
                        reverseRequest, result.reverse, &reverseError);
                } catch (const std::bad_alloc&) {
                    reverseError =
                        "MFD reverse local registration allocation failed.";
                } catch (...) {
                    reverseError =
                        "MFD reverse local registration failed unexpectedly.";
                }
            });
        } catch (...) {
            // Thread creation is an optimization only. Preserve the exact
            // sequential reference path when the host cannot create one.
        }
        if (reverseWorker.joinable()) {
            try {
                forwardValid = EstimateLocalMotionDirection(
                    forwardRequest, result.forward, &forwardError);
            } catch (const std::bad_alloc&) {
                forwardError =
                    "MFD forward local registration allocation failed.";
            } catch (...) {
                forwardError =
                    "MFD forward local registration failed unexpectedly.";
            }
            reverseWorker.join();
        } else {
            forwardRequest.reportProgress = [&request](double fraction) {
                if (request.reportProgress) {
                    request.reportProgress(
                        0.45 * std::clamp(fraction, 0.0, 1.0));
                }
            };
            reverseRequest.reportProgress = [&request](double fraction) {
                if (request.reportProgress) {
                    request.reportProgress(
                        0.45 + 0.45 * std::clamp(fraction, 0.0, 1.0));
                }
            };
            forwardValid = EstimateLocalMotionDirection(
                forwardRequest, result.forward, &forwardError);
            if (forwardValid) {
                reverseValid = EstimateLocalMotionDirection(
                    reverseRequest, result.reverse, &reverseError);
            }
        }
    } else {
        forwardRequest.reportProgress = [&request](double fraction) {
            if (request.reportProgress) {
                request.reportProgress(
                    0.45 * std::clamp(fraction, 0.0, 1.0));
            }
        };
        reverseRequest.reportProgress = [&request](double fraction) {
            if (request.reportProgress) {
                request.reportProgress(
                    0.45 + 0.45 * std::clamp(fraction, 0.0, 1.0));
            }
        };
        forwardValid = EstimateLocalMotionDirection(
            forwardRequest, result.forward, &forwardError);
        if (forwardValid) {
            reverseValid = EstimateLocalMotionDirection(
                reverseRequest, result.reverse, &reverseError);
        }
    }
    if (!forwardValid) {
        result.failure = result.forward.failure;
        result.message =
            "MFD forward local registration failed: " + forwardError;
        return Fail(error, result.message);
    }
    if (!reverseValid) {
        result.failure = result.reverse.failure;
        result.message =
            "MFD reverse local registration failed: " + reverseError;
        return Fail(error, result.message);
    }

    const double closureFloorVariance =
        request.options.registration.forwardBackwardCovarianceFloorRawPixels *
        request.options.registration.forwardBackwardCovarianceFloorRawPixels;
    const std::size_t closureNodeCount = result.forward.nodes.size();
    for (std::size_t closureIndex = 0u;
         closureIndex < closureNodeCount;
         ++closureIndex) {
        if (request.shouldCancel && request.shouldCancel()) {
            result.failure = LocalMotionFailure::NumericalFailure;
            result.message = "MFD bidirectional closure processing was canceled.";
            return Fail(error, result.message);
        }
        if (request.reportProgress) {
            request.reportProgress(
                0.9 + 0.1 * static_cast<double>(closureIndex) /
                    static_cast<double>(std::max<std::size_t>(1u, closureNodeCount)));
        }
        MotionNode& node = result.forward.nodes[closureIndex];
        if (node.state != MotionNodeState::Structured) continue;
        const RawCoordinate alternateRaw = Add(
            result.forward.globalWarp.Map(node.centerRaw), node.residualRaw);
        LocalMotionFieldSample reverseSample;
        localError.clear();
        if (!EvaluateLocalMotionField(
                result.reverse,
                alternateRaw,
                request.options,
                reverseSample,
                &localError)) {
            node.state = MotionNodeState::Rejected;
            node.rejectReason = MotionNodeRejectReason::ForwardBackwardUnavailable;
            node.confidence = 0.0;
            ++result.closureRejectedCount;
            continue;
        }
        const RawCoordinate closureError = Subtract(
            reverseSample.sourceRaw, node.centerRaw);
        SymmetricRawCovariance closureCovariance = AddCovariance(
            node.covarianceRaw, reverseSample.covarianceRaw);
        closureCovariance.xxRawPixelsSquared += closureFloorVariance;
        closureCovariance.yyRawPixelsSquared += closureFloorVariance;
        SymmetricRawCovariance closureInverse;
        if (!InvertCovariance(closureCovariance, closureInverse)) {
            node.state = MotionNodeState::Rejected;
            node.rejectReason = MotionNodeRejectReason::InvalidCovariance;
            node.confidence = 0.0;
            ++result.closureRejectedCount;
            continue;
        }
        node.forwardBackwardErrorRaw = closureError;
        node.forwardBackwardEuclideanRaw = std::sqrt(SquaredNorm(closureError));
        node.forwardBackwardMahalanobis = std::max(
            0.0, QuadraticForm(closureError, closureInverse));
        if (!Finite(node.forwardBackwardMahalanobis) ||
            node.forwardBackwardMahalanobis >
                request.options.registration.
                    forwardBackwardMahalanobisHardLimit ||
            node.forwardBackwardEuclideanRaw >
                request.options.registration.
                    forwardBackwardEuclideanHardLimitRawPixels) {
            node.state = MotionNodeState::Rejected;
            node.rejectReason = MotionNodeRejectReason::ForwardBackwardFailure;
            node.confidence = 0.0;
            ++result.closureRejectedCount;
            continue;
        }
        const double closureConfidence = std::exp(
            -0.5 * std::max(node.forwardBackwardMahalanobis - 2.0, 0.0));
        node.confidence *= closureConfidence;
        ++result.closureAcceptedCount;
    }
    if (request.reportProgress) request.reportProgress(1.0);
    RecountGrid(result.forward);
    if (!result.forward.valid) {
        result.failure = LocalMotionFailure::NoUsableNodes;
        result.message = "MFD forward/backward closure rejected every forward node.";
        return Fail(error, result.message);
    }
    result.valid = true;
    result.failure = LocalMotionFailure::None;
    result.message = "MFD bidirectional local registration completed.";
    return true;
}

} // namespace Raw::Mfd
