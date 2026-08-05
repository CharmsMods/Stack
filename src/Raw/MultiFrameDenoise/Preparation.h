#pragma once

#include "Raw/MultiFrameDenoise/Contracts.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Raw::Mfd {

inline constexpr std::uint32_t kPreparationContractVersion = 1;
inline constexpr std::uint32_t kNormalizedTileFormatVersion = 1;
inline constexpr const char* kPreparationContractId =
    "ra-cfa-raw-preparation-v1";

enum class PreparedSampleFlag : std::uint8_t {
    None = 0,
    Saturated = 1u << 0u,
    Defective = 1u << 1u,
    DecoderRepaired = 1u << 2u,
    ExplicitDecoderClip = 1u << 3u
};

std::uint8_t SampleFlagMask(PreparedSampleFlag flag);
bool HasSampleFlag(std::uint8_t mask, PreparedSampleFlag flag);

enum class DefectMaskProvenance {
    Unavailable,
    DecoderKnownDefects,
    DecoderRepairMap,
    DecoderKnownDefectsAndRepairMap
};

enum class RawOperationStage {
    AsRead,
    LinearRaw,
    PostDemosaic
};

enum class RawOperationDisposition {
    AppliedPointwise,
    CarriedAsSeparateGain,
    DeferredDownstream,
    UnsupportedRequired
};

struct RawOperationRecord {
    RawOperationStage stage = RawOperationStage::LinearRaw;
    RawOperationDisposition disposition =
        RawOperationDisposition::AppliedPointwise;
    std::string name;
    std::uint32_t count = 0;
};

struct RawOperationClassification {
    bool compatible = true;
    std::vector<RawOperationRecord> records;
    std::vector<std::string> incompatibilities;
};

RawOperationClassification ClassifyPreDemosaicOperations(
    const RawMetadata& metadata);

struct PreparationOptions {
    std::string decoderVersion = "stack-libraw-dng-supplement-v2";
    std::string cameraProfileVersion = "none";
    std::uint32_t tileRawPixels = 512;
    double saturationDnMargin = 4.0;
    double saturationNoiseSigmaMargin = 2.0;
    double maximumComparisonGain = 16.0;
    // CfaSite enum order: R, G0, G1, B. Phase 2 populates these values from
    // its resolved per-site noise model before final preparation.
    std::array<double, 4> saturationStdDevAtWhite { 0.0, 0.0, 0.0, 0.0 };

    // Optional sensor-coordinate masks. When present, each must contain exactly
    // rawWidth * rawHeight bytes. Nonzero means the condition is present.
    std::vector<std::uint8_t> decoderSaturationMask;
    std::vector<std::uint8_t> decoderDefectMask;
    std::vector<std::uint8_t> decoderRepairedMask;

    std::function<bool()> shouldCancel;
    std::function<void(std::uint64_t, std::uint64_t)> reportProgress;
};

PreparationOptions MakePreparationOptions(const Parameters& parameters);

struct RawCalibration {
    // All arrays use CfaSite enum order: R, G0, G1, B.
    std::array<double, 4> maximumBlackByCfaSite { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, 4> whiteLevelByCfaSite { 0.0, 0.0, 0.0, 0.0 };
    std::array<double, 4> usableSpanByCfaSite { 0.0, 0.0, 0.0, 0.0 };
    RawOperationClassification operations;
};

struct PreparedRawTile {
    std::uint32_t tileX = 0;
    std::uint32_t tileY = 0;
    std::uint64_t originX = 0;
    std::uint64_t originY = 0;
    PixelExtent extent;
    std::vector<float> normalizedMosaic;
    std::vector<float> comparisonGain;
    std::vector<std::uint8_t> sampleFlags;
};

enum class TileCacheReadStatus {
    Hit,
    Miss,
    Corrupt,
    IoError
};

class NormalizedTileCache {
public:
    virtual ~NormalizedTileCache() = default;

    virtual TileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY,
        PreparedRawTile& tile,
        std::string* error = nullptr) = 0;

    virtual bool Write(
        const std::string& cacheKey,
        const PreparedRawTile& tile,
        std::string* error = nullptr) = 0;
};

class MemoryNormalizedTileCache final : public NormalizedTileCache {
public:
    TileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY,
        PreparedRawTile& tile,
        std::string* error = nullptr) override;

    bool Write(
        const std::string& cacheKey,
        const PreparedRawTile& tile,
        std::string* error = nullptr) override;

    std::size_t EntryCount() const;

private:
    static std::string EntryKey(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY);

    mutable std::mutex m_Mutex;
    std::unordered_map<std::string, PreparedRawTile> m_Tiles;
};

class DirectoryNormalizedTileCache final : public NormalizedTileCache {
public:
    explicit DirectoryNormalizedTileCache(std::filesystem::path root);

    const std::filesystem::path& Root() const;

    TileCacheReadStatus Read(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY,
        PreparedRawTile& tile,
        std::string* error = nullptr) override;

    bool Write(
        const std::string& cacheKey,
        const PreparedRawTile& tile,
        std::string* error = nullptr) override;

private:
    std::filesystem::path TilePath(
        const std::string& cacheKey,
        std::uint32_t tileX,
        std::uint32_t tileY) const;

    std::filesystem::path m_Root;
};

enum class PreparationFailure {
    None,
    Canceled,
    InvalidDimensions,
    IncompleteMosaic,
    UnsupportedPixelLayout,
    UnsupportedCfa,
    MalformedMetadata,
    UnsupportedRequiredOperation,
    InvalidGainMap,
    InvalidSourceIdentity,
    CacheIoError
};

const char* PreparationFailureName(PreparationFailure failure);

struct PreparedRawFrame {
    std::uint32_t contractVersion = kPreparationContractVersion;
    std::string contractId = kPreparationContractId;
    std::string cacheKey;
    std::string sourceContentSha256;
    std::uint64_t sourceByteSize = 0;
    RawSensorRect sensorActiveArea;
    PixelExtent activeExtent;
    CfaPattern activeCfaPattern = CfaPattern::Unknown;
    std::uint32_t tileRawPixels = 0;
    std::uint32_t tileColumns = 0;
    std::uint32_t tileRows = 0;
    RawCalibration calibration;
    DefectMaskProvenance defectMaskProvenance =
        DefectMaskProvenance::Unavailable;
    std::uint64_t saturatedSampleCount = 0;
    std::uint64_t defectiveSampleCount = 0;
    std::uint64_t decoderRepairedSampleCount = 0;
    std::uint64_t cacheHitTileCount = 0;
    std::uint64_t generatedTileCount = 0;
};

struct PreparationResult {
    bool success = false;
    PreparationFailure failure = PreparationFailure::None;
    std::string message;
    PreparedRawFrame frame;
};

PreparationResult PrepareRawFrame(
    const RawImageData& raw,
    const PreparationOptions& options,
    NormalizedTileCache& cache);

TileCacheReadStatus ReadPreparedTile(
    const PreparedRawFrame& frame,
    NormalizedTileCache& cache,
    std::uint32_t tileX,
    std::uint32_t tileY,
    PreparedRawTile& tile,
    std::string* error = nullptr);

bool EvaluatePointwiseGain(
    const RawMetadata& metadata,
    PixelExtent activeExtent,
    std::uint64_t activeX,
    std::uint64_t activeY,
    double maximumAcceptedGain,
    float& gain,
    std::string* error = nullptr);

bool ReturnToPreGainDomain(
    float comparisonDomainSample,
    float referenceGain,
    float& preGainSample);

} // namespace Raw::Mfd
