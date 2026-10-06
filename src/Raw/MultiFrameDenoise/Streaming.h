#pragma once

#include "Raw/MultiFrameDenoise/Fusion.h"

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kStreamingContractVersion = 1;
inline constexpr std::uint32_t kFusionTileCacheFormatVersion = 3;
inline constexpr const char* kStreamingContractId =
    "ra-cfa-streaming-cache-publication-v1";

enum class MfdCacheStage : std::uint8_t {
    DecodeNormalization = 0,
    CalibrationNoise,
    CfaPyramid,
    PairwiseRegistration,
    LocalMotionReliability,
    FusedResult
};

const char* MfdCacheStageName(MfdCacheStage stage);

struct MfdCacheDependency {
    std::string name;
    std::string identity;
};

struct MfdCacheKeyRequest {
    MfdCacheStage stage = MfdCacheStage::DecodeNormalization;
    std::uint32_t contractVersion = kStreamingContractVersion;
    std::vector<MfdCacheDependency> dependencies;
};

std::vector<std::string> RequiredMfdCacheDependencies(MfdCacheStage stage);

std::string EncodeOrderedIdentities(
    const std::vector<std::string>& identities);

bool BuildMfdCacheKey(
    const MfdCacheKeyRequest& request,
    std::string& cacheKey,
    std::string* error = nullptr);

struct RawPixelRect {
    std::uint64_t x = 0u;
    std::uint64_t y = 0u;
    std::uint64_t width = 0u;
    std::uint64_t height = 0u;
};

struct FusionSourceRoiRequest {
    RawPixelRect outputRect;
    PixelExtent sourceExtent;
    const LocalMotionGrid* motionGrid = nullptr;
    LocalMotionOptions motionOptions;
    std::uint32_t samplerHaloRawPixels = 4u;
    std::uint32_t motionBoundHaloRawPixels = 0u;
};

struct FusionSourceRoiPlan {
    bool valid = false;
    bool requiresSplit = false;
    std::string message;
    RawPixelRect sourceRect;
    std::uint64_t evaluatedPointCount = 0u;
};

bool PlanFusionSourceRoi(
    const FusionSourceRoiRequest& request,
    FusionSourceRoiPlan& plan,
    std::string* error = nullptr);

struct FusionOutputTilePlan {
    std::uint64_t ordinal = 0u;
    RawPixelRect outputRect;
    std::uint64_t estimatedWorkingBytes = 0u;
};

std::uint64_t EstimateFusionTileWorkingBytes(
    PixelExtent extent,
    std::size_t alternateCount);

bool PlanFusionOutputTiles(
    PixelExtent rawExtent,
    const Parameters& parameters,
    std::size_t alternateCount,
    std::uint64_t memoryBudgetBytes,
    std::vector<FusionOutputTilePlan>& tiles,
    std::string* error = nullptr);

class MfdMemoryBudget {
public:
    class Reservation {
    public:
        Reservation() = default;
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept;
        Reservation& operator=(Reservation&& other) noexcept;
        ~Reservation();

        std::uint64_t Bytes() const;
        explicit operator bool() const;

    private:
        friend class MfdMemoryBudget;
        Reservation(MfdMemoryBudget* owner, std::uint64_t bytes);
        void Reset();

        MfdMemoryBudget* m_Owner = nullptr;
        std::uint64_t m_Bytes = 0u;
    };

    explicit MfdMemoryBudget(std::uint64_t budgetBytes);

    bool Acquire(
        std::uint64_t bytes,
        const std::function<bool()>& shouldCancel,
        Reservation& reservation);

    std::uint64_t BudgetBytes() const;
    std::uint64_t ResidentBytes() const;
    std::uint64_t PeakResidentBytes() const;
    std::uint64_t WaitCount() const;

private:
    void Release(std::uint64_t bytes);

    const std::uint64_t m_BudgetBytes;
    mutable std::mutex m_Mutex;
    std::condition_variable m_Condition;
    std::uint64_t m_ResidentBytes = 0u;
    std::uint64_t m_PeakResidentBytes = 0u;
    std::uint64_t m_WaitCount = 0u;
};

enum class FusionTileCacheReadStatus : std::uint8_t {
    Hit = 0,
    Miss,
    Corrupt,
    IoError
};

class FusionTileCache {
public:
    virtual ~FusionTileCache() = default;

    virtual FusionTileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        FusionTileResult& tile,
        std::string* error = nullptr) = 0;

    virtual bool Write(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        const FusionTileResult& tile,
        std::string* error = nullptr) = 0;

    virtual bool Erase(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        std::string* error = nullptr) = 0;
};

class MemoryFusionTileCache final : public FusionTileCache {
public:
    FusionTileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        FusionTileResult& tile,
        std::string* error = nullptr) override;

    bool Write(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        const FusionTileResult& tile,
        std::string* error = nullptr) override;

    bool Erase(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        std::string* error = nullptr) override;

    std::size_t EntryCount() const;

private:
    static std::string EntryKey(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal);

    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, FusionTileResult> m_Tiles;
};

class DirectoryFusionTileCache final : public FusionTileCache {
public:
    explicit DirectoryFusionTileCache(std::filesystem::path root);

    const std::filesystem::path& Root() const;
    std::filesystem::path TilePath(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal) const;

    FusionTileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        FusionTileResult& tile,
        std::string* error = nullptr) override;

    bool Write(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        const FusionTileResult& tile,
        std::string* error = nullptr) override;

    bool Erase(
        const std::string& cacheKey,
        std::uint64_t tileOrdinal,
        std::string* error = nullptr) override;

private:
    std::filesystem::path m_Root;
};

struct PublishedFusionResult {
    std::uint32_t contractVersion = kStreamingContractVersion;
    std::string contractId = kStreamingContractId;
    std::string fusedResultCacheKey;
    PixelExtent extent;
    std::vector<float> normalizedMosaic;
    std::vector<FusionPixelDiagnostics> diagnostics;
    std::uint64_t contentHash = 0u;
};

struct PublishedFusionSnapshot {
    std::uint64_t generation = 0u;
    std::shared_ptr<const PublishedFusionResult> result;
};

class AtomicFusionResultPublisher {
public:
    PublishedFusionSnapshot Snapshot() const;

    bool Publish(
        std::uint64_t expectedGeneration,
        PublishedFusionResult result,
        PublishedFusionSnapshot& published,
        std::string* error = nullptr);

private:
    mutable std::mutex m_Mutex;
    std::uint64_t m_Generation = 0u;
    std::shared_ptr<const PublishedFusionResult> m_Result;
};

using StreamingTileProcessor = std::function<bool(
    const FusionTileRequest& request,
    FusionTileResult& result,
    std::string* error)>;

enum class StreamingFusionStatus : std::uint8_t {
    Success = 0,
    Canceled,
    Failed,
    PublicationConflict
};

struct StreamingFusionDiagnostics {
    std::uint64_t plannedTileCount = 0u;
    std::uint64_t computedTileCount = 0u;
    std::uint64_t cacheHitTileCount = 0u;
    std::uint64_t cacheMissTileCount = 0u;
    std::uint64_t corruptCacheRecoveryCount = 0u;
    std::uint64_t cacheWriteFailureCount = 0u;
    std::uint64_t strictScalarRecoveryCount = 0u;
    std::uint64_t referenceTileFallbackCount = 0u;
    std::uint64_t peakWorkingBytes = 0u;
    std::uint64_t memoryWaitCount = 0u;
    std::uint32_t requestedWorkerCount = 0u;
    std::uint32_t effectiveWorkerCount = 0u;
};

struct StreamingFusionRequest {
    PixelExtent rawExtent;
    Parameters parameters;
    std::size_t alternateCount = 0u;
    std::string fusedResultCacheKey;
    std::uint64_t memoryBudgetBytes = 0u;
    std::uint32_t workerCount = 1u;
    FusionReferenceProvider referenceProvider;
    FusionCandidateProvider candidateProvider;
    StreamingTileProcessor primaryTileProcessor;
    StreamingTileProcessor strictScalarTileProcessor;
    FusionTileCache* tileCache = nullptr;
    AtomicFusionResultPublisher* publisher = nullptr;
    std::uint64_t expectedPublicationGeneration = 0u;
    std::function<bool()> shouldCancel;
    std::function<void(std::uint64_t, std::uint64_t)> reportProgress;
};

struct StreamingFusionResult {
    StreamingFusionStatus status = StreamingFusionStatus::Failed;
    std::string message;
    StreamingFusionDiagnostics diagnostics;
    PublishedFusionSnapshot published;
};

StreamingFusionResult ExecuteStreamingFusion(
    const StreamingFusionRequest& request);

} // namespace Raw::Mfd
