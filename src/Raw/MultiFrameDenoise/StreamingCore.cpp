#include "Raw/MultiFrameDenoise/Streaming.h"

#include "Raw/RawTechnicalEvidence.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <utility>
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

bool ValidStage(MfdCacheStage stage) {
    switch (stage) {
        case MfdCacheStage::DecodeNormalization:
        case MfdCacheStage::CalibrationNoise:
        case MfdCacheStage::CfaPyramid:
        case MfdCacheStage::PairwiseRegistration:
        case MfdCacheStage::LocalMotionReliability:
        case MfdCacheStage::FusedResult:
            return true;
    }
    return false;
}

void AppendLengthPrefixed(std::ostringstream& stream, const std::string& value) {
    stream << value.size() << ':' << value;
}

std::string HashText(const std::string& text) {
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return Stack::RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

bool CheckedRect(const RawPixelRect& rect) {
    return rect.width > 0u && rect.height > 0u &&
        rect.x <= std::numeric_limits<std::uint64_t>::max() - rect.width &&
        rect.y <= std::numeric_limits<std::uint64_t>::max() - rect.height;
}

void AddUniqueCoordinate(std::vector<double>& coordinates, double value) {
    if (!Finite(value)) return;
    coordinates.push_back(value);
}

void SortUniqueCoordinates(std::vector<double>& coordinates) {
    std::sort(coordinates.begin(), coordinates.end());
    coordinates.erase(
        std::unique(
            coordinates.begin(),
            coordinates.end(),
            [](double left, double right) {
                return std::abs(left - right) <= 1.0e-9;
            }),
        coordinates.end());
}

std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

std::uint64_t SaturatingMultiply(std::uint64_t left, std::uint64_t right) {
    if (left != 0u && right >
        std::numeric_limits<std::uint64_t>::max() / left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left * right;
}

bool AppendPlannedRect(
    const RawPixelRect& rect,
    std::size_t alternateCount,
    std::uint64_t memoryBudgetBytes,
    std::vector<FusionOutputTilePlan>& tiles,
    std::string* error) {
    const PixelExtent extent { rect.width, rect.height };
    const std::uint64_t estimated = EstimateFusionTileWorkingBytes(
        extent, alternateCount);
    if (estimated <= memoryBudgetBytes) {
        FusionOutputTilePlan tile;
        tile.outputRect = rect;
        tile.estimatedWorkingBytes = estimated;
        tiles.push_back(tile);
        return true;
    }
    if (rect.width == 1u && rect.height == 1u) {
        return Fail(
            error,
            "MFD memory budget cannot hold one scalar fusion sample.");
    }
    RawPixelRect first = rect;
    RawPixelRect second = rect;
    if (rect.width >= rect.height && rect.width > 1u) {
        first.width = rect.width / 2u;
        second.x = rect.x + first.width;
        second.width = rect.width - first.width;
    } else {
        first.height = rect.height / 2u;
        second.y = rect.y + first.height;
        second.height = rect.height - first.height;
    }
    return AppendPlannedRect(
            first, alternateCount, memoryBudgetBytes, tiles, error) &&
        AppendPlannedRect(
            second, alternateCount, memoryBudgetBytes, tiles, error);
}

} // namespace

const char* MfdCacheStageName(MfdCacheStage stage) {
    switch (stage) {
        case MfdCacheStage::DecodeNormalization: return "cache-a-decode-normalization";
        case MfdCacheStage::CalibrationNoise: return "cache-b-calibration-noise";
        case MfdCacheStage::CfaPyramid: return "cache-c-cfa-pyramid";
        case MfdCacheStage::PairwiseRegistration: return "cache-d-pairwise-registration";
        case MfdCacheStage::LocalMotionReliability:
            return "cache-e-local-motion-reliability";
        case MfdCacheStage::FusedResult: return "cache-f-fused-result";
    }
    return "invalid-cache-stage";
}

std::vector<std::string> RequiredMfdCacheDependencies(MfdCacheStage stage) {
    switch (stage) {
        case MfdCacheStage::DecodeNormalization:
            return {
                "source-content",
                "decoder-camera-profile",
                "raw-semantics",
                "opcode-policy",
                "defect-saturation-policy"
            };
        case MfdCacheStage::CalibrationNoise:
            return {
                "cache-a",
                "gain-noise-profile",
                "optical-black"
            };
        case MfdCacheStage::CfaPyramid:
            return {
                "cache-b",
                "pyramid-parameters",
                "proxy-defect-fill"
            };
        case MfdCacheStage::PairwiseRegistration:
            return {
                "reference-cache-c",
                "alternate-cache-c",
                "global-registration-parameters",
                "exposure-parameters"
            };
        case MfdCacheStage::LocalMotionReliability:
            return {
                "cache-d",
                "local-motion-parameters",
                "covariance-forward-backward",
                "reliability-parameters",
                "noise-quality-policy"
            };
        case MfdCacheStage::FusedResult:
            return {
                "ordered-frame-ids",
                "reference-frame-id",
                "ordered-cache-e-keys",
                "fusion-parameters",
                "output-contract"
            };
    }
    return {};
}

std::string EncodeOrderedIdentities(
    const std::vector<std::string>& identities) {
    std::ostringstream stream;
    stream << identities.size() << '|';
    for (const std::string& identity : identities) {
        AppendLengthPrefixed(stream, identity);
        stream << '|';
    }
    return stream.str();
}

bool BuildMfdCacheKey(
    const MfdCacheKeyRequest& request,
    std::string& cacheKey,
    std::string* error) {
    cacheKey.clear();
    if (!ValidStage(request.stage) ||
        request.contractVersion != kStreamingContractVersion) {
        return Fail(error, "MFD cache-key contract is invalid.");
    }
    std::vector<MfdCacheDependency> dependencies = request.dependencies;
    std::sort(
        dependencies.begin(),
        dependencies.end(),
        [](const MfdCacheDependency& left, const MfdCacheDependency& right) {
            return left.name < right.name;
        });
    for (std::size_t index = 0u; index < dependencies.size(); ++index) {
        if (dependencies[index].name.empty() ||
            dependencies[index].identity.empty() ||
            (index > 0u &&
                dependencies[index - 1u].name == dependencies[index].name)) {
            return Fail(error, "MFD cache dependencies are empty or duplicated.");
        }
    }
    const std::vector<std::string> required =
        RequiredMfdCacheDependencies(request.stage);
    for (const std::string& requiredName : required) {
        const auto found = std::lower_bound(
            dependencies.begin(),
            dependencies.end(),
            requiredName,
            [](const MfdCacheDependency& dependency, const std::string& name) {
                return dependency.name < name;
            });
        if (found == dependencies.end() || found->name != requiredName) {
            return Fail(
                error,
                "MFD cache key is missing dependency: " + requiredName);
        }
    }

    std::ostringstream canonical;
    AppendLengthPrefixed(canonical, kStreamingContractId);
    canonical << '|' << request.contractVersion << '|';
    AppendLengthPrefixed(canonical, MfdCacheStageName(request.stage));
    canonical << '|' << dependencies.size() << '|';
    for (const MfdCacheDependency& dependency : dependencies) {
        AppendLengthPrefixed(canonical, dependency.name);
        canonical << '=';
        AppendLengthPrefixed(canonical, dependency.identity);
        canonical << '|';
    }
    cacheKey = HashText(canonical.str());
    if (cacheKey.size() != 64u) {
        cacheKey.clear();
        return Fail(error, "MFD cache-key SHA-256 generation failed.");
    }
    return true;
}

bool PlanFusionSourceRoi(
    const FusionSourceRoiRequest& request,
    FusionSourceRoiPlan& plan,
    std::string* error) {
    plan = {};
    if (!CheckedRect(request.outputRect) || request.sourceExtent.width == 0u ||
        request.sourceExtent.height == 0u || !request.motionGrid ||
        !request.motionGrid->valid || request.samplerHaloRawPixels < 4u) {
        return Fail(error, "MFD source ROI request is invalid.");
    }
    const double left = static_cast<double>(request.outputRect.x);
    const double top = static_cast<double>(request.outputRect.y);
    const double right = static_cast<double>(
        request.outputRect.x + request.outputRect.width - 1u);
    const double bottom = static_cast<double>(
        request.outputRect.y + request.outputRect.height - 1u);
    std::vector<double> xs { left, right };
    std::vector<double> ys { top, bottom };
    const LocalMotionGrid& grid = *request.motionGrid;
    for (std::uint32_t x = 0u; x < grid.width; ++x) {
        const double coordinate =
            grid.originRawX + grid.spacingRawX * static_cast<double>(x);
        if (coordinate > left && coordinate < right) {
            AddUniqueCoordinate(xs, coordinate);
        }
    }
    for (std::uint32_t y = 0u; y < grid.height; ++y) {
        const double coordinate =
            grid.originRawY + grid.spacingRawY * static_cast<double>(y);
        if (coordinate > top && coordinate < bottom) {
            AddUniqueCoordinate(ys, coordinate);
        }
    }
    SortUniqueCoordinates(xs);
    SortUniqueCoordinates(ys);
    if (xs.empty() || ys.empty() ||
        xs.size() > std::numeric_limits<std::size_t>::max() / ys.size()) {
        return Fail(error, "MFD source ROI sample grid is invalid.");
    }

    double minimumX = std::numeric_limits<double>::infinity();
    double minimumY = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    double maximumY = -std::numeric_limits<double>::infinity();
    for (double y : ys) {
        for (double x : xs) {
            LocalMotionFieldSample motion;
            std::string ignored;
            if (!EvaluateLocalMotionField(
                    grid,
                    { x, y },
                    request.motionOptions,
                    motion,
                    &ignored) ||
                !motion.valid || !Finite(motion.sourceRaw.x) ||
                !Finite(motion.sourceRaw.y)) {
                plan.requiresSplit = true;
                plan.message =
                    "MFD source ROI bound is uncertain; split the output tile.";
                return true;
            }
            ++plan.evaluatedPointCount;
            minimumX = std::min(minimumX, motion.sourceRaw.x);
            minimumY = std::min(minimumY, motion.sourceRaw.y);
            maximumX = std::max(maximumX, motion.sourceRaw.x);
            maximumY = std::max(maximumY, motion.sourceRaw.y);
        }
    }
    if (!Finite(minimumX) || !Finite(minimumY) || !Finite(maximumX) ||
        !Finite(maximumY) || minimumX > maximumX || minimumY > maximumY) {
        return Fail(error, "MFD source ROI bound is non-finite.");
    }
    const double halo = static_cast<double>(
        request.samplerHaloRawPixels + request.motionBoundHaloRawPixels);
    const double startX = std::floor(minimumX - halo);
    const double startY = std::floor(minimumY - halo);
    const double endX = std::ceil(maximumX + halo) + 1.0;
    const double endY = std::ceil(maximumY + halo) + 1.0;
    if (!Finite(startX) || !Finite(startY) || !Finite(endX) || !Finite(endY)) {
        return Fail(error, "MFD source ROI expansion overflowed.");
    }
    const double clampedStartX = std::max(0.0, startX);
    const double clampedStartY = std::max(0.0, startY);
    const double clampedEndX = std::min(
        static_cast<double>(request.sourceExtent.width), endX);
    const double clampedEndY = std::min(
        static_cast<double>(request.sourceExtent.height), endY);
    if (clampedStartX >= clampedEndX || clampedStartY >= clampedEndY ||
        clampedStartX > static_cast<double>(
            std::numeric_limits<std::uint64_t>::max()) ||
        clampedStartY > static_cast<double>(
            std::numeric_limits<std::uint64_t>::max())) {
        plan.requiresSplit = true;
        plan.message = "MFD source ROI has no bounded source overlap.";
        return true;
    }
    plan.sourceRect.x = static_cast<std::uint64_t>(clampedStartX);
    plan.sourceRect.y = static_cast<std::uint64_t>(clampedStartY);
    plan.sourceRect.width = static_cast<std::uint64_t>(clampedEndX) -
        plan.sourceRect.x;
    plan.sourceRect.height = static_cast<std::uint64_t>(clampedEndY) -
        plan.sourceRect.y;
    plan.valid = CheckedRect(plan.sourceRect);
    plan.message = plan.valid
        ? "MFD source ROI is conservatively bounded."
        : "MFD source ROI is invalid.";
    return plan.valid || plan.requiresSplit;
}

std::uint64_t EstimateFusionTileWorkingBytes(
    PixelExtent extent,
    std::size_t alternateCount) {
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::uint64_t>::max() /
            extent.height) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t pixels = extent.width * extent.height;
    const std::uint64_t perPixel =
        sizeof(float) + sizeof(FusionPixelDiagnostics) +
        2u * (2u * sizeof(float) + sizeof(std::uint8_t));
    const std::uint64_t tileBytes = SaturatingMultiply(pixels, perPixel);
    const std::uint64_t candidateBytes = SaturatingMultiply(
        static_cast<std::uint64_t>(alternateCount),
        sizeof(FusionCandidateSample));
    return SaturatingAdd(
        64u * 1024u,
        SaturatingAdd(tileBytes, candidateBytes));
}

bool PlanFusionOutputTiles(
    PixelExtent rawExtent,
    const Parameters& parameters,
    std::size_t alternateCount,
    std::uint64_t memoryBudgetBytes,
    std::vector<FusionOutputTilePlan>& tiles,
    std::string* error) {
    tiles.clear();
    if (!ValidateParameters(parameters, error)) return false;
    if (rawExtent.width == 0u || rawExtent.height == 0u ||
        memoryBudgetBytes == 0u) {
        return Fail(error, "MFD output tile planning request is invalid.");
    }
    const std::uint64_t tileSize = parameters.fusion.outputTileRawPixels;
    for (std::uint64_t y = 0u; y < rawExtent.height;) {
        const std::uint64_t height = std::min(
            tileSize, rawExtent.height - y);
        for (std::uint64_t x = 0u; x < rawExtent.width;) {
            const std::uint64_t width = std::min(
                tileSize, rawExtent.width - x);
            if (!AppendPlannedRect(
                    { x, y, width, height },
                    alternateCount,
                    memoryBudgetBytes,
                    tiles,
                    error)) {
                tiles.clear();
                return false;
            }
            x += width;
        }
        y += height;
    }
    for (std::size_t index = 0u; index < tiles.size(); ++index) {
        tiles[index].ordinal = static_cast<std::uint64_t>(index);
    }
    return !tiles.empty();
}

MfdMemoryBudget::Reservation::Reservation(
    MfdMemoryBudget* owner,
    std::uint64_t bytes)
    : m_Owner(owner), m_Bytes(bytes) {
}

MfdMemoryBudget::Reservation::Reservation(Reservation&& other) noexcept
    : m_Owner(other.m_Owner), m_Bytes(other.m_Bytes) {
    other.m_Owner = nullptr;
    other.m_Bytes = 0u;
}

MfdMemoryBudget::Reservation& MfdMemoryBudget::Reservation::operator=(
    Reservation&& other) noexcept {
    if (this == &other) return *this;
    Reset();
    m_Owner = other.m_Owner;
    m_Bytes = other.m_Bytes;
    other.m_Owner = nullptr;
    other.m_Bytes = 0u;
    return *this;
}

MfdMemoryBudget::Reservation::~Reservation() {
    Reset();
}

std::uint64_t MfdMemoryBudget::Reservation::Bytes() const {
    return m_Bytes;
}

MfdMemoryBudget::Reservation::operator bool() const {
    return m_Owner != nullptr;
}

void MfdMemoryBudget::Reservation::Reset() {
    if (m_Owner) m_Owner->Release(m_Bytes);
    m_Owner = nullptr;
    m_Bytes = 0u;
}

MfdMemoryBudget::MfdMemoryBudget(std::uint64_t budgetBytes)
    : m_BudgetBytes(budgetBytes) {
}

bool MfdMemoryBudget::Acquire(
    std::uint64_t bytes,
    const std::function<bool()>& shouldCancel,
    Reservation& reservation) {
    reservation.Reset();
    if (bytes == 0u || bytes > m_BudgetBytes) return false;
    std::unique_lock<std::mutex> lock(m_Mutex);
    while (bytes > m_BudgetBytes - m_ResidentBytes) {
        if (shouldCancel && shouldCancel()) return false;
        ++m_WaitCount;
        m_Condition.wait_for(lock, std::chrono::milliseconds(5));
    }
    if (shouldCancel && shouldCancel()) return false;
    m_ResidentBytes += bytes;
    m_PeakResidentBytes = std::max(m_PeakResidentBytes, m_ResidentBytes);
    reservation = Reservation(this, bytes);
    return true;
}

std::uint64_t MfdMemoryBudget::BudgetBytes() const {
    return m_BudgetBytes;
}

std::uint64_t MfdMemoryBudget::ResidentBytes() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_ResidentBytes;
}

std::uint64_t MfdMemoryBudget::PeakResidentBytes() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_PeakResidentBytes;
}

std::uint64_t MfdMemoryBudget::WaitCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_WaitCount;
}

void MfdMemoryBudget::Release(std::uint64_t bytes) {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ResidentBytes = bytes > m_ResidentBytes
            ? 0u
            : m_ResidentBytes - bytes;
    }
    m_Condition.notify_all();
}

} // namespace Raw::Mfd
