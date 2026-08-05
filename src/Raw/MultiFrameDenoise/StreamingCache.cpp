#include "Raw/MultiFrameDenoise/Streaming.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <utility>

namespace Raw::Mfd {
namespace {

constexpr std::array<char, 8> kFusionTileMagic {
    'S', 'T', 'K', 'M', 'F', 'D', '8', '\0'
};
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint64_t kMaximumCachedTileSamples = 1ull << 20u;

bool SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f');
        });
}

template <typename T>
bool WritePod(std::ostream& output, const T& value) {
    output.write(
        reinterpret_cast<const char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(output);
}

template <typename T>
bool ReadPod(std::istream& input, T& value) {
    input.read(
        reinterpret_cast<char*>(&value),
        static_cast<std::streamsize>(sizeof(T)));
    return static_cast<bool>(input);
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t bytes) {
    const auto* values = static_cast<const std::uint8_t*>(data);
    for (std::size_t index = 0u; index < bytes; ++index) {
        hash ^= values[index];
        hash *= kFnvPrime;
    }
}

template <typename T>
void HashPod(std::uint64_t& hash, const T& value) {
    HashBytes(hash, &value, sizeof(T));
}

bool FiniteNonnegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

bool ValidDecisionReason(DecisionReason reason) {
    const int value = static_cast<int>(reason);
    return value >= static_cast<int>(DecisionReason::None) &&
        value <= static_cast<int>(DecisionReason::Canceled);
}

bool ValidFusionRejectReason(FusionRejectReason reason) {
    const int value = static_cast<int>(reason);
    return value >= static_cast<int>(FusionRejectReason::None) &&
        value <= static_cast<int>(
            FusionRejectReason::ReferenceDefectDisagreement);
}

std::uint8_t DiagnosticFlags(const FusionPixelDiagnostics& diagnostic) {
    return static_cast<std::uint8_t>(
        (diagnostic.exactReferenceCopy ? 1u : 0u) |
        (diagnostic.referenceIncluded ? 2u : 0u) |
        (diagnostic.referenceDefectReconstructed ? 4u : 0u) |
        (diagnostic.referenceDefectRepairDeferred ? 8u : 0u) |
        (diagnostic.lowConfidenceTotalCapApplied ? 16u : 0u));
}

void ApplyDiagnosticFlags(
    std::uint8_t flags,
    FusionPixelDiagnostics& diagnostic) {
    diagnostic.exactReferenceCopy = (flags & 1u) != 0u;
    diagnostic.referenceIncluded = (flags & 2u) != 0u;
    diagnostic.referenceDefectReconstructed = (flags & 4u) != 0u;
    diagnostic.referenceDefectRepairDeferred = (flags & 8u) != 0u;
    diagnostic.lowConfidenceTotalCapApplied = (flags & 16u) != 0u;
}

bool ValidDiagnostic(const FusionPixelDiagnostics& diagnostic) {
    return ValidDecisionReason(diagnostic.decisionReason) &&
        ValidFusionRejectReason(diagnostic.dominantRejectionReason) &&
        FiniteNonnegative(diagnostic.referenceWeight) &&
        FiniteNonnegative(diagnostic.alternateWeight) &&
        FiniteNonnegative(diagnostic.alternateToReferenceWeightRatio) &&
        FiniteNonnegative(diagnostic.effectiveSampleCount) &&
        FiniteNonnegative(diagnostic.outputVarianceComparisonDomain);
}

bool CheckedSampleCount(PixelExtent extent, std::size_t& count) {
    count = 0u;
    if (extent.width == 0u || extent.height == 0u ||
        extent.width > std::numeric_limits<std::uint64_t>::max() /
            extent.height) {
        return false;
    }
    const std::uint64_t samples = extent.width * extent.height;
    if (samples > std::numeric_limits<std::size_t>::max() ||
        samples > kMaximumCachedTileSamples) {
        return false;
    }
    count = static_cast<std::size_t>(samples);
    return true;
}

bool CheckedPublishedSampleCount(PixelExtent extent, std::size_t& count) {
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

bool ValidFusionTile(const FusionTileResult& tile, std::string* error) {
    std::size_t sampleCount = 0u;
    if (!tile.valid || !CheckedSampleCount(tile.extent, sampleCount) ||
        tile.originRawX > std::numeric_limits<std::uint64_t>::max() -
            tile.extent.width ||
        tile.originRawY > std::numeric_limits<std::uint64_t>::max() -
            tile.extent.height ||
        tile.normalizedMosaic.size() != sampleCount ||
        tile.diagnostics.size() != sampleCount ||
        tile.fusedPixelCount > sampleCount ||
        tile.exactReferencePixelCount > sampleCount ||
        tile.deferredReferenceDefectCount > sampleCount ||
        tile.reconstructedReferenceDefectCount > sampleCount ||
        tile.fusedPixelCount + tile.exactReferencePixelCount +
            tile.deferredReferenceDefectCount != sampleCount) {
        return SetError(error, "MFD fused tile shape or counters are invalid.");
    }
    for (float value : tile.normalizedMosaic) {
        if (!std::isfinite(value)) {
            return SetError(error, "MFD fused tile contains a non-finite sample.");
        }
    }
    for (const FusionPixelDiagnostics& diagnostic : tile.diagnostics) {
        if (!ValidDiagnostic(diagnostic)) {
            return SetError(error, "MFD fused tile diagnostics are invalid.");
        }
    }
    return true;
}

void HashDiagnostic(
    std::uint64_t& hash,
    const FusionPixelDiagnostics& diagnostic) {
    const std::uint8_t decision = static_cast<std::uint8_t>(
        diagnostic.decisionReason);
    const std::uint8_t rejection = static_cast<std::uint8_t>(
        diagnostic.dominantRejectionReason);
    const std::uint8_t flags = DiagnosticFlags(diagnostic);
    HashPod(hash, decision);
    HashPod(hash, rejection);
    HashPod(hash, flags);
    HashPod(hash, diagnostic.eligibleAlternateCount);
    HashPod(hash, diagnostic.contributingAlternateCount);
    HashPod(hash, diagnostic.rejectedAlternateCount);
    HashPod(hash, diagnostic.individuallyCappedAlternateCount);
    HashPod(hash, diagnostic.referenceWeight);
    HashPod(hash, diagnostic.alternateWeight);
    HashPod(hash, diagnostic.alternateToReferenceWeightRatio);
    HashPod(hash, diagnostic.effectiveSampleCount);
    HashPod(hash, diagnostic.outputVarianceComparisonDomain);
}

std::uint64_t HashFusionTile(const FusionTileResult& tile) {
    std::uint64_t hash = kFnvOffset;
    HashPod(hash, tile.originRawX);
    HashPod(hash, tile.originRawY);
    HashPod(hash, tile.extent.width);
    HashPod(hash, tile.extent.height);
    HashPod(hash, tile.fusedPixelCount);
    HashPod(hash, tile.exactReferencePixelCount);
    HashPod(hash, tile.reconstructedReferenceDefectCount);
    HashPod(hash, tile.deferredReferenceDefectCount);
    if (!tile.normalizedMosaic.empty()) {
        HashBytes(
            hash,
            tile.normalizedMosaic.data(),
            tile.normalizedMosaic.size() * sizeof(float));
    }
    for (const FusionPixelDiagnostics& diagnostic : tile.diagnostics) {
        HashDiagnostic(hash, diagnostic);
    }
    return hash;
}

bool WriteDiagnostic(
    std::ostream& output,
    const FusionPixelDiagnostics& diagnostic) {
    const std::uint8_t decision = static_cast<std::uint8_t>(
        diagnostic.decisionReason);
    const std::uint8_t rejection = static_cast<std::uint8_t>(
        diagnostic.dominantRejectionReason);
    const std::uint8_t flags = DiagnosticFlags(diagnostic);
    const std::uint8_t reserved = 0u;
    return WritePod(output, decision) && WritePod(output, rejection) &&
        WritePod(output, flags) && WritePod(output, reserved) &&
        WritePod(output, diagnostic.eligibleAlternateCount) &&
        WritePod(output, diagnostic.contributingAlternateCount) &&
        WritePod(output, diagnostic.rejectedAlternateCount) &&
        WritePod(output, diagnostic.individuallyCappedAlternateCount) &&
        WritePod(output, diagnostic.referenceWeight) &&
        WritePod(output, diagnostic.alternateWeight) &&
        WritePod(output, diagnostic.alternateToReferenceWeightRatio) &&
        WritePod(output, diagnostic.effectiveSampleCount) &&
        WritePod(output, diagnostic.outputVarianceComparisonDomain);
}

bool ReadDiagnostic(
    std::istream& input,
    FusionPixelDiagnostics& diagnostic) {
    std::uint8_t decision = 0u;
    std::uint8_t rejection = 0u;
    std::uint8_t flags = 0u;
    std::uint8_t reserved = 0u;
    if (!ReadPod(input, decision) || !ReadPod(input, rejection) ||
        !ReadPod(input, flags) || !ReadPod(input, reserved) || reserved != 0u ||
        !ReadPod(input, diagnostic.eligibleAlternateCount) ||
        !ReadPod(input, diagnostic.contributingAlternateCount) ||
        !ReadPod(input, diagnostic.rejectedAlternateCount) ||
        !ReadPod(input, diagnostic.individuallyCappedAlternateCount) ||
        !ReadPod(input, diagnostic.referenceWeight) ||
        !ReadPod(input, diagnostic.alternateWeight) ||
        !ReadPod(input, diagnostic.alternateToReferenceWeightRatio) ||
        !ReadPod(input, diagnostic.effectiveSampleCount) ||
        !ReadPod(input, diagnostic.outputVarianceComparisonDomain)) {
        return false;
    }
    diagnostic.decisionReason = static_cast<DecisionReason>(decision);
    diagnostic.dominantRejectionReason =
        static_cast<FusionRejectReason>(rejection);
    ApplyDiagnosticFlags(flags, diagnostic);
    return ValidDiagnostic(diagnostic);
}

std::uint64_t HashPublishedResult(const PublishedFusionResult& result) {
    std::uint64_t hash = kFnvOffset;
    HashBytes(hash, result.fusedResultCacheKey.data(),
        result.fusedResultCacheKey.size());
    HashPod(hash, result.extent.width);
    HashPod(hash, result.extent.height);
    if (!result.normalizedMosaic.empty()) {
        HashBytes(
            hash,
            result.normalizedMosaic.data(),
            result.normalizedMosaic.size() * sizeof(float));
    }
    for (const FusionPixelDiagnostics& diagnostic : result.diagnostics) {
        HashDiagnostic(hash, diagnostic);
    }
    return hash;
}

bool ValidPublishedResult(
    const PublishedFusionResult& result,
    std::string* error) {
    std::size_t sampleCount = 0u;
    if (result.contractVersion != kStreamingContractVersion ||
        result.contractId != kStreamingContractId ||
        !LooksLikeSha256(result.fusedResultCacheKey) ||
        !CheckedPublishedSampleCount(result.extent, sampleCount) ||
        result.normalizedMosaic.size() != sampleCount ||
        result.diagnostics.size() != sampleCount) {
        return SetError(error, "MFD published fusion result contract is invalid.");
    }
    for (float value : result.normalizedMosaic) {
        if (!std::isfinite(value)) {
            return SetError(error, "MFD published result contains non-finite data.");
        }
    }
    for (const FusionPixelDiagnostics& diagnostic : result.diagnostics) {
        if (!ValidDiagnostic(diagnostic)) {
            return SetError(error, "MFD published diagnostics are invalid.");
        }
    }
    return true;
}

} // namespace

std::string MemoryFusionTileCache::EntryKey(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal) {
    return cacheKey + ":" + std::to_string(tileOrdinal);
}

FusionTileCacheReadStatus MemoryFusionTileCache::Read(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal,
    FusionTileResult& tile,
    std::string* error) {
    tile = {};
    if (!LooksLikeSha256(cacheKey)) {
        SetError(error, "MFD fused tile cache key is invalid.");
        return FusionTileCacheReadStatus::IoError;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = m_Tiles.find(EntryKey(cacheKey, tileOrdinal));
    if (found == m_Tiles.end()) return FusionTileCacheReadStatus::Miss;
    if (!ValidFusionTile(found->second, error)) {
        return FusionTileCacheReadStatus::Corrupt;
    }
    tile = found->second;
    return FusionTileCacheReadStatus::Hit;
}

bool MemoryFusionTileCache::Write(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal,
    const FusionTileResult& tile,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey) || !ValidFusionTile(tile, error)) {
        return SetError(error, "MFD in-memory fused tile write is invalid.");
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Tiles[EntryKey(cacheKey, tileOrdinal)] = tile;
    return true;
}

bool MemoryFusionTileCache::Erase(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey)) {
        return SetError(error, "MFD in-memory fused tile erase key is invalid.");
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Tiles.erase(EntryKey(cacheKey, tileOrdinal));
    return true;
}

std::size_t MemoryFusionTileCache::EntryCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Tiles.size();
}

DirectoryFusionTileCache::DirectoryFusionTileCache(std::filesystem::path root)
    : m_Root(std::move(root)) {
}

const std::filesystem::path& DirectoryFusionTileCache::Root() const {
    return m_Root;
}

std::filesystem::path DirectoryFusionTileCache::TilePath(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal) const {
    return m_Root / cacheKey /
        ("tile-" + std::to_string(tileOrdinal) + ".mfdf");
}

FusionTileCacheReadStatus DirectoryFusionTileCache::Read(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal,
    FusionTileResult& tile,
    std::string* error) {
    tile = {};
    if (!LooksLikeSha256(cacheKey)) {
        SetError(error, "MFD fused tile cache key is invalid.");
        return FusionTileCacheReadStatus::IoError;
    }
    const std::filesystem::path path = TilePath(cacheKey, tileOrdinal);
    std::error_code filesystemError;
    if (!std::filesystem::exists(path, filesystemError)) {
        if (filesystemError) {
            SetError(error, "MFD fused tile cache could not inspect a tile.");
            return FusionTileCacheReadStatus::IoError;
        }
        return FusionTileCacheReadStatus::Miss;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "MFD fused tile cache could not open a tile.");
        return FusionTileCacheReadStatus::IoError;
    }
    std::array<char, 8> magic {};
    std::uint32_t version = 0u;
    std::uint64_t storedOrdinal = 0u;
    std::uint64_t originX = 0u;
    std::uint64_t originY = 0u;
    std::uint64_t width = 0u;
    std::uint64_t height = 0u;
    std::uint64_t sampleCount64 = 0u;
    std::uint64_t fusedCount = 0u;
    std::uint64_t exactCount = 0u;
    std::uint64_t reconstructedCount = 0u;
    std::uint64_t deferredCount = 0u;
    std::uint64_t expectedHash = 0u;
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != kFusionTileMagic ||
        !ReadPod(input, version) || version != kFusionTileCacheFormatVersion ||
        !ReadPod(input, storedOrdinal) || storedOrdinal != tileOrdinal ||
        !ReadPod(input, originX) || !ReadPod(input, originY) ||
        !ReadPod(input, width) || !ReadPod(input, height) ||
        !ReadPod(input, sampleCount64) || !ReadPod(input, fusedCount) ||
        !ReadPod(input, exactCount) || !ReadPod(input, reconstructedCount) ||
        !ReadPod(input, deferredCount) || !ReadPod(input, expectedHash)) {
        SetError(error, "MFD fused tile cache header is corrupt.");
        return FusionTileCacheReadStatus::Corrupt;
    }
    std::size_t sampleCount = 0u;
    if (!CheckedSampleCount({ width, height }, sampleCount) ||
        sampleCount64 != sampleCount) {
        SetError(error, "MFD fused tile cache payload size is invalid.");
        return FusionTileCacheReadStatus::Corrupt;
    }
    FusionTileResult loaded;
    loaded.valid = true;
    loaded.message = "MFD fused tile cache hit.";
    loaded.originRawX = originX;
    loaded.originRawY = originY;
    loaded.extent = { width, height };
    loaded.fusedPixelCount = fusedCount;
    loaded.exactReferencePixelCount = exactCount;
    loaded.reconstructedReferenceDefectCount = reconstructedCount;
    loaded.deferredReferenceDefectCount = deferredCount;
    try {
        loaded.normalizedMosaic.resize(sampleCount);
        loaded.diagnostics.resize(sampleCount);
    } catch (const std::bad_alloc&) {
        SetError(error, "MFD fused tile cache allocation failed.");
        return FusionTileCacheReadStatus::IoError;
    }
    input.read(
        reinterpret_cast<char*>(loaded.normalizedMosaic.data()),
        static_cast<std::streamsize>(sampleCount * sizeof(float)));
    for (FusionPixelDiagnostics& diagnostic : loaded.diagnostics) {
        if (!input || !ReadDiagnostic(input, diagnostic)) {
            SetError(error, "MFD fused tile cache diagnostics are corrupt.");
            return FusionTileCacheReadStatus::Corrupt;
        }
    }
    if (!input || input.peek() != std::char_traits<char>::eof() ||
        !ValidFusionTile(loaded, error) || HashFusionTile(loaded) != expectedHash) {
        if (error && error->empty()) {
            *error = "MFD fused tile cache checksum is corrupt.";
        }
        return FusionTileCacheReadStatus::Corrupt;
    }
    tile = std::move(loaded);
    return FusionTileCacheReadStatus::Hit;
}

bool DirectoryFusionTileCache::Write(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal,
    const FusionTileResult& tile,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey) || !ValidFusionTile(tile, error)) {
        return SetError(error, "MFD directory fused tile write is invalid.");
    }
    const std::filesystem::path path = TilePath(cacheKey, tileOrdinal);
    std::error_code filesystemError;
    std::filesystem::create_directories(path.parent_path(), filesystemError);
    if (filesystemError) {
        return SetError(error, "MFD fused tile cache directory could not be created.");
    }
    static std::atomic<std::uint64_t> temporaryCounter { 0u };
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + std::to_string(temporaryCounter.fetch_add(1u));
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const std::uint64_t sampleCount = tile.normalizedMosaic.size();
    const std::uint64_t checksum = HashFusionTile(tile);
    output.write(
        kFusionTileMagic.data(),
        static_cast<std::streamsize>(kFusionTileMagic.size()));
    const bool headerWritten = output &&
        WritePod(output, kFusionTileCacheFormatVersion) &&
        WritePod(output, tileOrdinal) && WritePod(output, tile.originRawX) &&
        WritePod(output, tile.originRawY) && WritePod(output, tile.extent.width) &&
        WritePod(output, tile.extent.height) && WritePod(output, sampleCount) &&
        WritePod(output, tile.fusedPixelCount) &&
        WritePod(output, tile.exactReferencePixelCount) &&
        WritePod(output, tile.reconstructedReferenceDefectCount) &&
        WritePod(output, tile.deferredReferenceDefectCount) &&
        WritePod(output, checksum);
    if (headerWritten) {
        output.write(
            reinterpret_cast<const char*>(tile.normalizedMosaic.data()),
            static_cast<std::streamsize>(
                tile.normalizedMosaic.size() * sizeof(float)));
        for (const FusionPixelDiagnostics& diagnostic : tile.diagnostics) {
            if (!WriteDiagnostic(output, diagnostic)) break;
        }
    }
    output.flush();
    const bool written = headerWritten && static_cast<bool>(output);
    output.close();
    if (!written) {
        std::filesystem::remove(temporary, filesystemError);
        return SetError(error, "MFD fused tile cache write was incomplete.");
    }
    if (std::filesystem::exists(path, filesystemError) && !filesystemError) {
        std::filesystem::remove(path, filesystemError);
    }
    if (filesystemError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return SetError(error, "MFD fused tile cache could not replace a tile.");
    }
    std::filesystem::rename(temporary, path, filesystemError);
    if (filesystemError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return SetError(error, "MFD fused tile cache publish failed.");
    }
    return true;
}

bool DirectoryFusionTileCache::Erase(
    const std::string& cacheKey,
    std::uint64_t tileOrdinal,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey)) {
        return SetError(error, "MFD fused tile cache erase key is invalid.");
    }
    std::error_code filesystemError;
    std::filesystem::remove(TilePath(cacheKey, tileOrdinal), filesystemError);
    if (filesystemError) {
        return SetError(error, "MFD fused tile cache erase failed.");
    }
    return true;
}

PublishedFusionSnapshot AtomicFusionResultPublisher::Snapshot() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return { m_Generation, m_Result };
}

bool AtomicFusionResultPublisher::Publish(
    std::uint64_t expectedGeneration,
    PublishedFusionResult result,
    PublishedFusionSnapshot& published,
    std::string* error) {
    published = {};
    if (!ValidPublishedResult(result, error)) return false;
    result.contentHash = HashPublishedResult(result);
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (expectedGeneration != m_Generation) {
        return SetError(error, "MFD fusion publication generation conflict.");
    }
    if (m_Generation == std::numeric_limits<std::uint64_t>::max()) {
        return SetError(error, "MFD fusion publication generation overflowed.");
    }
    try {
        m_Result = std::make_shared<const PublishedFusionResult>(
            std::move(result));
    } catch (const std::bad_alloc&) {
        return SetError(error, "MFD fusion publication allocation failed.");
    }
    ++m_Generation;
    published = { m_Generation, m_Result };
    return true;
}

} // namespace Raw::Mfd
