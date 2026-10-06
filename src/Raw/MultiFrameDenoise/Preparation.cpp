#include "Raw/MultiFrameDenoise/Preparation.h"

#include "Raw/RawProcessingMath.h"
#include "Raw/RawTechnicalEvidence.h"
#include "ThirdParty/json.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <system_error>
#include <utility>

namespace Raw::Mfd {
namespace {

constexpr std::array<char, 8> kTileMagic { 'S', 'T', 'K', 'M', 'F', 'D', '1', '\0' };
constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

bool SetError(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool IsCanceled(const PreparationOptions& options) {
    return options.shouldCancel && options.shouldCancel();
}

bool CheckedPixelCount(std::uint64_t width, std::uint64_t height, std::size_t& count) {
    if (width == 0 || height == 0) {
        count = 0;
        return true;
    }
    if (width > std::numeric_limits<std::uint64_t>::max() / height) return false;
    const std::uint64_t product = width * height;
    if (product > std::numeric_limits<std::size_t>::max()) return false;
    count = static_cast<std::size_t>(product);
    return true;
}

std::size_t SiteIndex(CfaSite site) {
    switch (site) {
        case CfaSite::Red: return 0;
        case CfaSite::Green0: return 1;
        case CfaSite::Green1: return 2;
        case CfaSite::Blue: return 3;
    }
    return 0;
}

int MetadataColorIndex(CfaSite site) {
    switch (site) {
        case CfaSite::Red: return 0;
        case CfaSite::Green0: return 1;
        case CfaSite::Green1: return 3;
        case CfaSite::Blue: return 2;
    }
    return 0;
}

bool LooksLikeSha256(const std::string& value) {
    return value.size() == 64u &&
        std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return (character >= '0' && character <= '9') ||
                (character >= 'a' && character <= 'f') ||
                (character >= 'A' && character <= 'F');
        });
}

std::string LowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        if (character >= 'A' && character <= 'Z') {
            return static_cast<char>(character - 'A' + 'a');
        }
        return static_cast<char>(character);
    });
    return value;
}

std::string HashBytes(const std::vector<std::uint8_t>& bytes) {
    return Stack::RawEvidence::ComputeSourceIdentity(bytes).sha256;
}

std::string HashMask(const std::vector<std::uint8_t>& values) {
    if (values.empty()) return "none";
    return HashBytes(values);
}

double BaseBlackForSite(const RawMetadata& metadata, CfaSite site) {
    const int colorIndex = MetadataColorIndex(site);
    if (colorIndex >= 0 &&
        colorIndex < static_cast<int>(metadata.perChannelBlack.size()) &&
        std::isfinite(metadata.perChannelBlack[static_cast<std::size_t>(colorIndex)]) &&
        metadata.perChannelBlack[static_cast<std::size_t>(colorIndex)] > 0.0f) {
        return metadata.perChannelBlack[static_cast<std::size_t>(colorIndex)];
    }
    return metadata.blackLevel;
}

double BlackAtActiveCoordinate(
    const RawMetadata& metadata,
    CfaSite site,
    std::uint64_t activeX,
    std::uint64_t activeY) {
    double black = BaseBlackForSite(metadata, site);
    const int repeatRows = metadata.dngBlackLevelRepeatDim[0];
    const int repeatColumns = metadata.dngBlackLevelRepeatDim[1];
    if (repeatRows > 0 && repeatColumns > 0 && !metadata.dngBlackLevelValues.empty()) {
        const std::size_t row = static_cast<std::size_t>(activeY % static_cast<std::uint64_t>(repeatRows));
        const std::size_t column = static_cast<std::size_t>(activeX % static_cast<std::uint64_t>(repeatColumns));
        black = metadata.dngBlackLevelValues[
            row * static_cast<std::size_t>(repeatColumns) + column];
    }
    if (!metadata.dngBlackLevelDeltaH.empty()) {
        black += metadata.dngBlackLevelDeltaH[static_cast<std::size_t>(activeX)];
    }
    if (!metadata.dngBlackLevelDeltaV.empty()) {
        black += metadata.dngBlackLevelDeltaV[static_cast<std::size_t>(activeY)];
    }
    return black;
}

double WhiteForSite(const RawMetadata& metadata, CfaSite site) {
    const auto& levels = metadata.dngWhiteLevelValues;
    if (levels.empty()) return metadata.whiteLevel;
    if (levels.size() == 1u) return levels.front();
    const int colorIndex = MetadataColorIndex(site);
    if (levels.size() == 3u) {
        const int rgbIndex = site == CfaSite::Blue ? 2 : (site == CfaSite::Red ? 0 : 1);
        return levels[static_cast<std::size_t>(rgbIndex)];
    }
    return levels[static_cast<std::size_t>(colorIndex)];
}

double Linearize(const RawMetadata& metadata, std::uint16_t storedSample) {
    if (metadata.dngLinearizationTable.empty()) {
        return static_cast<double>(storedSample);
    }
    return static_cast<double>(
        metadata.dngLinearizationTable[static_cast<std::size_t>(storedSample)]);
}

bool ValidGainMapShape(
    const DngGainMapOpcode& map,
    PixelExtent activeExtent,
    double maximumAcceptedGain,
    std::string* error) {
    if (map.top < 0 || map.left < 0 || map.top >= map.bottom || map.left >= map.right ||
        static_cast<std::uint64_t>(map.bottom) > activeExtent.height ||
        static_cast<std::uint64_t>(map.right) > activeExtent.width ||
        map.plane != 0 || map.planes != 1 || map.rowPitch <= 0 || map.colPitch <= 0 ||
        map.mapPointsV <= 0 || map.mapPointsH <= 0 || map.mapPlanes != 1 ||
        !std::isfinite(map.mapSpacingV) || !std::isfinite(map.mapSpacingH) ||
        !std::isfinite(map.mapOriginV) || !std::isfinite(map.mapOriginH) ||
        map.mapSpacingV <= 0.0 || map.mapSpacingH <= 0.0) {
        return SetError(error, "MFD GainMap metadata is malformed or does not target the packed Bayer plane.");
    }
    const std::uint64_t pointsV = static_cast<std::uint64_t>(map.mapPointsV);
    const std::uint64_t pointsH = static_cast<std::uint64_t>(map.mapPointsH);
    if (pointsV > std::numeric_limits<std::uint64_t>::max() / pointsH ||
        pointsV * pointsH != map.gains.size()) {
        return SetError(error, "MFD GainMap sample count does not match its declared grid.");
    }
    for (float value : map.gains) {
        if (!std::isfinite(value) || value <= 0.0f ||
            static_cast<double>(value) > maximumAcceptedGain) {
            return SetError(error, "MFD GainMap contains a nonpositive, non-finite, or excessive gain.");
        }
    }
    return true;
}

float SampleGainMap(
    const DngGainMapOpcode& map,
    PixelExtent activeExtent,
    std::uint64_t activeX,
    std::uint64_t activeY) {
    const int x = static_cast<int>(activeX);
    const int y = static_cast<int>(activeY);
    if (y < map.top || y >= map.bottom || x < map.left || x >= map.right ||
        ((y - map.top) % map.rowPitch) != 0 ||
        ((x - map.left) % map.colPitch) != 0) {
        return 1.0f;
    }

    const double normalizedX =
        (static_cast<double>(activeX) + 0.5) /
        static_cast<double>(activeExtent.width);
    const double normalizedY =
        (static_cast<double>(activeY) + 0.5) /
        static_cast<double>(activeExtent.height);
    const double gridX = (normalizedX - map.mapOriginH) / map.mapSpacingH;
    const double gridY = (normalizedY - map.mapOriginV) / map.mapSpacingV;
    const double clampedX = std::clamp(gridX, 0.0, static_cast<double>(map.mapPointsH - 1));
    const double clampedY = std::clamp(gridY, 0.0, static_cast<double>(map.mapPointsV - 1));
    const int x0 = static_cast<int>(std::floor(clampedX));
    const int y0 = static_cast<int>(std::floor(clampedY));
    const int x1 = std::min(x0 + 1, map.mapPointsH - 1);
    const int y1 = std::min(y0 + 1, map.mapPointsV - 1);
    const float tx = static_cast<float>(clampedX - static_cast<double>(x0));
    const float ty = static_cast<float>(clampedY - static_cast<double>(y0));
    const auto at = [&](int row, int column) {
        return map.gains[
            static_cast<std::size_t>(row) * static_cast<std::size_t>(map.mapPointsH) +
            static_cast<std::size_t>(column)];
    };
    const float top = at(y0, x0) * (1.0f - tx) + at(y0, x1) * tx;
    const float bottom = at(y1, x0) * (1.0f - tx) + at(y1, x1) * tx;
    return top * (1.0f - ty) + bottom * ty;
}

bool EvaluateValidatedPointwiseGain(
    const RawMetadata& metadata,
    PixelExtent activeExtent,
    std::uint64_t activeX,
    std::uint64_t activeY,
    double maximumAcceptedGain,
    float& gain,
    std::string* error) {
    double product = 1.0;
    for (const DngGainMapOpcode& map : metadata.dngGainMaps) {
        product *= static_cast<double>(SampleGainMap(
            map, activeExtent, activeX, activeY));
        if (!std::isfinite(product) || product <= 0.0 ||
            product > maximumAcceptedGain) {
            return SetError(error, "MFD combined pointwise calibration gain is invalid or excessive.");
        }
    }
    gain = static_cast<float>(product);
    return true;
}

bool ComputeMaximumBlackByCfaSite(
    const RawMetadata& metadata,
    const CfaLayout& layout,
    PixelExtent activeExtent,
    const PreparationOptions& options,
    std::array<double, 4>& maximumBlack,
    std::string* error) {
    const std::size_t repeatRows = static_cast<std::size_t>(
        std::max(1, metadata.dngBlackLevelRepeatDim[0]));
    const std::size_t repeatColumns = static_cast<std::size_t>(
        std::max(1, metadata.dngBlackLevelRepeatDim[1]));
    const double negativeInfinity = -std::numeric_limits<double>::infinity();
    std::vector<std::array<double, 2>> maximumHorizontal(
        repeatColumns, { negativeInfinity, negativeInfinity });
    std::vector<std::array<double, 2>> maximumVertical(
        repeatRows, { negativeInfinity, negativeInfinity });
    for (std::uint64_t x = 0; x < activeExtent.width; ++x) {
        if ((x & 0x3fffu) == 0u && IsCanceled(options)) {
            return SetError(error, "MFD RAW preparation was canceled.");
        }
        const std::size_t residue = static_cast<std::size_t>(x % repeatColumns);
        const std::size_t parity = static_cast<std::size_t>(x & 1u);
        const double delta = metadata.dngBlackLevelDeltaH.empty()
            ? 0.0
            : metadata.dngBlackLevelDeltaH[static_cast<std::size_t>(x)];
        maximumHorizontal[residue][parity] = std::max(
            maximumHorizontal[residue][parity], delta);
    }
    for (std::uint64_t y = 0; y < activeExtent.height; ++y) {
        if ((y & 0x3fffu) == 0u && IsCanceled(options)) {
            return SetError(error, "MFD RAW preparation was canceled.");
        }
        const std::size_t residue = static_cast<std::size_t>(y % repeatRows);
        const std::size_t parity = static_cast<std::size_t>(y & 1u);
        const double delta = metadata.dngBlackLevelDeltaV.empty()
            ? 0.0
            : metadata.dngBlackLevelDeltaV[static_cast<std::size_t>(y)];
        maximumVertical[residue][parity] = std::max(
            maximumVertical[residue][parity], delta);
    }

    maximumBlack.fill(negativeInfinity);
    const bool hasRepeatingBlack = !metadata.dngBlackLevelValues.empty();
    for (std::size_t repeatY = 0; repeatY < repeatRows; ++repeatY) {
        for (std::size_t repeatX = 0; repeatX < repeatColumns; ++repeatX) {
            for (std::size_t parityY = 0; parityY < 2u; ++parityY) {
                if (!std::isfinite(maximumVertical[repeatY][parityY])) continue;
                for (std::size_t parityX = 0; parityX < 2u; ++parityX) {
                    if (!std::isfinite(maximumHorizontal[repeatX][parityX])) continue;
                    const CfaSite site = layout.SiteAt(
                        static_cast<std::int64_t>(parityX),
                        static_cast<std::int64_t>(parityY));
                    const double base = hasRepeatingBlack
                        ? metadata.dngBlackLevelValues[
                            repeatY * repeatColumns + repeatX]
                        : BaseBlackForSite(metadata, site);
                    const double candidate = base +
                        maximumHorizontal[repeatX][parityX] +
                        maximumVertical[repeatY][parityY];
                    if (!std::isfinite(candidate)) {
                        return SetError(error, "MFD computed black level is non-finite.");
                    }
                    const std::size_t siteIndex = SiteIndex(site);
                    maximumBlack[siteIndex] = std::max(
                        maximumBlack[siteIndex], candidate);
                }
            }
        }
    }
    for (CfaSite site : {
             CfaSite::Red, CfaSite::Green0, CfaSite::Green1, CfaSite::Blue }) {
        const std::size_t siteIndex = SiteIndex(site);
        if (!std::isfinite(maximumBlack[siteIndex])) {
            maximumBlack[siteIndex] = BaseBlackForSite(metadata, site);
        }
    }
    return true;
}

nlohmann::json GainMapIdentity(const DngGainMapOpcode& map) {
    return {
        { "top", map.top }, { "left", map.left },
        { "bottom", map.bottom }, { "right", map.right },
        { "plane", map.plane }, { "planes", map.planes },
        { "rowPitch", map.rowPitch }, { "colPitch", map.colPitch },
        { "mapPointsV", map.mapPointsV }, { "mapPointsH", map.mapPointsH },
        { "mapPlanes", map.mapPlanes },
        { "mapSpacingV", map.mapSpacingV }, { "mapSpacingH", map.mapSpacingH },
        { "mapOriginV", map.mapOriginV }, { "mapOriginH", map.mapOriginH },
        { "gains", map.gains }
    };
}

std::string BuildPreparationCacheKey(
    const RawMetadata& metadata,
    const RawSensorRect& active,
    const RawCalibration& calibration,
    const PreparationOptions& options) {
    nlohmann::json gainMaps = nlohmann::json::array();
    for (const DngGainMapOpcode& map : metadata.dngGainMaps) {
        gainMaps.push_back(GainMapIdentity(map));
    }
    nlohmann::json operations = nlohmann::json::array();
    for (const RawOperationRecord& record : calibration.operations.records) {
        operations.push_back({
            { "stage", static_cast<int>(record.stage) },
            { "disposition", static_cast<int>(record.disposition) },
            { "name", record.name },
            { "count", record.count }
        });
    }
    const nlohmann::json identity = {
        { "contractId", kPreparationContractId },
        { "contractVersion", kPreparationContractVersion },
        { "tileFormatVersion", kNormalizedTileFormatVersion },
        { "sourceContentSha256", LowerAscii(metadata.sourceContentSha256) },
        { "sourceByteSize", metadata.sourceByteSize },
        { "decoderVersion", options.decoderVersion },
        { "cameraProfileVersion", options.cameraProfileVersion },
        { "rawDimensions", { metadata.rawWidth, metadata.rawHeight } },
        { "activeArea", { active.top, active.left, active.bottom, active.right } },
        { "cfaPattern", static_cast<int>(metadata.cfaPattern) },
        { "bitDepth", metadata.bitDepth },
        { "linearizationTable", metadata.dngLinearizationTable },
        { "blackLevel", metadata.blackLevel },
        { "perChannelBlack", metadata.perChannelBlack },
        { "blackRepeat", metadata.dngBlackLevelRepeatDim },
        { "blackValues", metadata.dngBlackLevelValues },
        { "blackDeltaH", metadata.dngBlackLevelDeltaH },
        { "blackDeltaV", metadata.dngBlackLevelDeltaV },
        { "whiteLevel", metadata.whiteLevel },
        { "whiteValues", metadata.dngWhiteLevelValues },
        { "maximumBlackByCfaSite", calibration.maximumBlackByCfaSite },
        { "usableSpanByCfaSite", calibration.usableSpanByCfaSite },
        { "operations", std::move(operations) },
        { "gainMaps", std::move(gainMaps) },
        { "profileGainTableMap", metadata.hasDngProfileGainTableMap },
        { "profileGainTableMap2", metadata.hasDngProfileGainTableMap2 },
        { "tileRawPixels", options.tileRawPixels },
        { "saturationDnMargin", options.saturationDnMargin },
        { "saturationNoiseSigmaMargin", options.saturationNoiseSigmaMargin },
        { "maximumComparisonGain", options.maximumComparisonGain },
        { "saturationStdDevAtWhite", options.saturationStdDevAtWhite },
        { "decoderSaturationMask", HashMask(options.decoderSaturationMask) },
        { "decoderDefectMask", HashMask(options.decoderDefectMask) },
        { "decoderRepairedMask", HashMask(options.decoderRepairedMask) }
    };
    const std::string canonical = identity.dump();
    return HashBytes(std::vector<std::uint8_t>(canonical.begin(), canonical.end()));
}

std::uint64_t HashTile(const PreparedRawTile& tile) {
    std::uint64_t hash = kFnvOffset;
    const auto mix = [&](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= kFnvPrime;
        }
    };
    mix(&tile.tileX, sizeof(tile.tileX));
    mix(&tile.tileY, sizeof(tile.tileY));
    mix(&tile.originX, sizeof(tile.originX));
    mix(&tile.originY, sizeof(tile.originY));
    mix(&tile.extent.width, sizeof(tile.extent.width));
    mix(&tile.extent.height, sizeof(tile.extent.height));
    if (!tile.normalizedMosaic.empty()) {
        mix(tile.normalizedMosaic.data(), tile.normalizedMosaic.size() * sizeof(float));
    }
    if (!tile.comparisonGain.empty()) {
        mix(tile.comparisonGain.data(), tile.comparisonGain.size() * sizeof(float));
    }
    if (!tile.sampleFlags.empty()) {
        mix(tile.sampleFlags.data(), tile.sampleFlags.size());
    }
    return hash;
}

template <typename T>
bool WritePod(std::ostream& stream, const T& value) {
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return static_cast<bool>(stream);
}

template <typename T>
bool ReadPod(std::istream& stream, T& value) {
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(stream);
}

bool ValidTileShape(const PreparedRawTile& tile, std::string* error) {
    std::size_t count = 0;
    if (!CheckedPixelCount(tile.extent.width, tile.extent.height, count) || count == 0u ||
        tile.normalizedMosaic.size() != count ||
        tile.comparisonGain.size() != count ||
        tile.sampleFlags.size() != count) {
        return SetError(error, "MFD normalized tile has inconsistent dimensions or payload lengths.");
    }
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(tile.normalizedMosaic[i]) ||
            !std::isfinite(tile.comparisonGain[i]) ||
            tile.comparisonGain[i] <= 0.0f) {
            return SetError(error, "MFD normalized tile contains an invalid sample or gain.");
        }
    }
    return true;
}

PreparationResult FailureResult(
    PreparationFailure failure,
    const std::string& message,
    PreparedRawFrame frame = {}) {
    PreparationResult result;
    result.failure = failure;
    result.message = message;
    result.frame = std::move(frame);
    return result;
}

void AccumulateFlags(const PreparedRawTile& tile, PreparedRawFrame& frame) {
    for (std::uint8_t flags : tile.sampleFlags) {
        if (HasSampleFlag(flags, PreparedSampleFlag::Saturated)) {
            ++frame.saturatedSampleCount;
        }
        if (HasSampleFlag(flags, PreparedSampleFlag::Defective)) {
            ++frame.defectiveSampleCount;
        }
        if (HasSampleFlag(flags, PreparedSampleFlag::DecoderRepaired)) {
            ++frame.decoderRepairedSampleCount;
        }
    }
}

} // namespace

std::uint8_t SampleFlagMask(PreparedSampleFlag flag) {
    return static_cast<std::uint8_t>(flag);
}

bool HasSampleFlag(std::uint8_t mask, PreparedSampleFlag flag) {
    return (mask & SampleFlagMask(flag)) != 0u;
}

RawOperationClassification ClassifyPreDemosaicOperations(
    const RawMetadata& metadata) {
    RawOperationClassification result;
    if (!metadata.dngLinearizationTable.empty()) {
        result.records.push_back({
            RawOperationStage::AsRead,
            RawOperationDisposition::AppliedPointwise,
            "DNG LinearizationTable",
            1u
        });
    }
    result.records.push_back({
        RawOperationStage::LinearRaw,
        RawOperationDisposition::AppliedPointwise,
        "Black subtraction and white normalization",
        1u
    });

    const auto countAt = [&](std::size_t index) {
        return std::max(0, metadata.dngOpcodeCount[index]);
    };
    const auto appliedAt = [&](std::size_t index) {
        return std::clamp(metadata.dngAppliedOpcodeCountByList[index], 0, countAt(index));
    };
    const int list1Applied = appliedAt(0);
    const int list1Unsupported = countAt(0) - list1Applied;
    if (list1Applied > 0) {
        result.records.push_back({
            RawOperationStage::AsRead,
            RawOperationDisposition::AppliedPointwise,
            "Decoder-applied DNG OpcodeList1 operations",
            static_cast<std::uint32_t>(list1Applied)
        });
    }
    if (list1Unsupported > 0) {
        result.compatible = false;
        result.records.push_back({
            RawOperationStage::AsRead,
            RawOperationDisposition::UnsupportedRequired,
            "Unsupported required DNG OpcodeList1 operations",
            static_cast<std::uint32_t>(list1Unsupported)
        });
        result.incompatibilities.push_back("unsupported-required-opcode-list1");
    }

    const int gainMapCount = static_cast<int>(metadata.dngGainMaps.size());
    if (gainMapCount > 0) {
        result.records.push_back({
            RawOperationStage::LinearRaw,
            RawOperationDisposition::CarriedAsSeparateGain,
            "DNG OpcodeList2 GainMap",
            static_cast<std::uint32_t>(gainMapCount)
        });
    }
    const int list2Unsupported = std::max(0, countAt(1) - gainMapCount);
    if (list2Unsupported > 0) {
        result.compatible = false;
        result.records.push_back({
            RawOperationStage::LinearRaw,
            RawOperationDisposition::UnsupportedRequired,
            "Unsupported required DNG OpcodeList2 operations",
            static_cast<std::uint32_t>(list2Unsupported)
        });
        result.incompatibilities.push_back("unsupported-required-opcode-list2");
    }

    if (countAt(2) > 0) {
        result.records.push_back({
            RawOperationStage::PostDemosaic,
            RawOperationDisposition::DeferredDownstream,
            "DNG OpcodeList3 operations",
            static_cast<std::uint32_t>(countAt(2))
        });
    }

    const int profileGainTableCount =
        (metadata.hasDngProfileGainTableMap ? 1 : 0) +
        (metadata.hasDngProfileGainTableMap2 ? 1 : 0);
    if (profileGainTableCount > 0) {
        result.compatible = false;
        result.records.push_back({
            RawOperationStage::LinearRaw,
            RawOperationDisposition::UnsupportedRequired,
            "Unsupported ProfileGainTableMap calibration",
            static_cast<std::uint32_t>(profileGainTableCount)
        });
        result.incompatibilities.push_back("unsupported-profile-gain-table-map");
    }
    return result;
}

PreparationOptions MakePreparationOptions(const Parameters& parameters) {
    PreparationOptions options;
    options.tileRawPixels = parameters.fusion.outputTileRawPixels;
    options.saturationDnMargin = parameters.radiometric.saturationDnMargin;
    options.saturationNoiseSigmaMargin =
        parameters.radiometric.saturationNoiseSigmaMargin;
    options.maximumComparisonGain =
        parameters.radiometric.maximumComparisonGain;
    return options;
}

std::string MemoryNormalizedTileCache::EntryKey(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY) {
    return cacheKey + ":" + std::to_string(tileY) + ":" + std::to_string(tileX);
}

TileCacheReadStatus MemoryNormalizedTileCache::Read(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY,
    PreparedRawTile& tile,
    std::string* error) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    const auto found = m_Tiles.find(EntryKey(cacheKey, tileX, tileY));
    if (found == m_Tiles.end()) return TileCacheReadStatus::Miss;
    tile = found->second;
    return ValidTileShape(tile, error)
        ? TileCacheReadStatus::Hit
        : TileCacheReadStatus::Corrupt;
}

bool MemoryNormalizedTileCache::Write(
    const std::string& cacheKey,
    const PreparedRawTile& tile,
    std::string* error) {
    if (!ValidTileShape(tile, error)) return false;
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Tiles[EntryKey(cacheKey, tile.tileX, tile.tileY)] = tile;
    return true;
}

std::size_t MemoryNormalizedTileCache::EntryCount() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Tiles.size();
}

DirectoryNormalizedTileCache::DirectoryNormalizedTileCache(std::filesystem::path root)
    : m_Root(std::move(root)) {
}

const std::filesystem::path& DirectoryNormalizedTileCache::Root() const {
    return m_Root;
}

std::filesystem::path DirectoryNormalizedTileCache::TilePath(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY) const {
    return m_Root / cacheKey /
        ("tile-" + std::to_string(tileY) + "-" + std::to_string(tileX) + ".mfdn");
}

TileCacheReadStatus DirectoryNormalizedTileCache::Read(
    const std::string& cacheKey,
    std::uint32_t tileX,
    std::uint32_t tileY,
    PreparedRawTile& tile,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey)) {
        SetError(error, "MFD tile cache key is not a SHA-256 identity.");
        return TileCacheReadStatus::IoError;
    }
    const std::filesystem::path path = TilePath(cacheKey, tileX, tileY);
    std::error_code existsError;
    const bool exists = std::filesystem::exists(path, existsError);
    if (existsError) {
        SetError(error, "MFD tile cache could not inspect the cache path.");
        return TileCacheReadStatus::IoError;
    }
    if (!exists) return TileCacheReadStatus::Miss;

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "MFD tile cache could not open a tile.");
        return TileCacheReadStatus::IoError;
    }
    std::array<char, 8> magic {};
    std::uint32_t version = 0;
    std::uint32_t storedTileX = 0;
    std::uint32_t storedTileY = 0;
    std::uint64_t originX = 0;
    std::uint64_t originY = 0;
    std::uint64_t width = 0;
    std::uint64_t height = 0;
    std::uint64_t sampleCount = 0;
    std::uint64_t expectedHash = 0;
    input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!input || magic != kTileMagic ||
        !ReadPod(input, version) || version != kNormalizedTileFormatVersion ||
        !ReadPod(input, storedTileX) || !ReadPod(input, storedTileY) ||
        !ReadPod(input, originX) || !ReadPod(input, originY) ||
        !ReadPod(input, width) || !ReadPod(input, height) ||
        !ReadPod(input, sampleCount) || !ReadPod(input, expectedHash) ||
        storedTileX != tileX || storedTileY != tileY) {
        SetError(error, "MFD normalized tile header is corrupt or incompatible.");
        return TileCacheReadStatus::Corrupt;
    }
    std::size_t checkedCount = 0;
    if (!CheckedPixelCount(width, height, checkedCount) || checkedCount == 0u ||
        sampleCount != checkedCount || sampleCount > (1ull << 30u)) {
        SetError(error, "MFD normalized tile declares an invalid payload size.");
        return TileCacheReadStatus::Corrupt;
    }

    PreparedRawTile loaded;
    loaded.tileX = storedTileX;
    loaded.tileY = storedTileY;
    loaded.originX = originX;
    loaded.originY = originY;
    loaded.extent = { width, height };
    loaded.normalizedMosaic.resize(checkedCount);
    loaded.comparisonGain.resize(checkedCount);
    loaded.sampleFlags.resize(checkedCount);
    input.read(
        reinterpret_cast<char*>(loaded.normalizedMosaic.data()),
        static_cast<std::streamsize>(checkedCount * sizeof(float)));
    input.read(
        reinterpret_cast<char*>(loaded.comparisonGain.data()),
        static_cast<std::streamsize>(checkedCount * sizeof(float)));
    input.read(
        reinterpret_cast<char*>(loaded.sampleFlags.data()),
        static_cast<std::streamsize>(checkedCount));
    if (!input || input.peek() != std::char_traits<char>::eof() ||
        HashTile(loaded) != expectedHash || !ValidTileShape(loaded, error)) {
        if (error && error->empty()) *error = "MFD normalized tile checksum or payload is corrupt.";
        return TileCacheReadStatus::Corrupt;
    }
    tile = std::move(loaded);
    return TileCacheReadStatus::Hit;
}

bool DirectoryNormalizedTileCache::Write(
    const std::string& cacheKey,
    const PreparedRawTile& tile,
    std::string* error) {
    if (!LooksLikeSha256(cacheKey)) {
        return SetError(error, "MFD tile cache key is not a SHA-256 identity.");
    }
    if (!ValidTileShape(tile, error)) return false;

    const std::filesystem::path path = TilePath(cacheKey, tile.tileX, tile.tileY);
    std::error_code directoryError;
    std::filesystem::create_directories(path.parent_path(), directoryError);
    if (directoryError) {
        return SetError(error, "MFD tile cache could not create its cache directory.");
    }

    static std::atomic<std::uint64_t> temporaryCounter { 0 };
    std::filesystem::path temporary = path;
    temporary += ".tmp-" + std::to_string(temporaryCounter.fetch_add(1u));
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    const std::uint64_t sampleCount = tile.normalizedMosaic.size();
    const std::uint64_t checksum = HashTile(tile);
    output.write(kTileMagic.data(), static_cast<std::streamsize>(kTileMagic.size()));
    if (!output ||
        !WritePod(output, kNormalizedTileFormatVersion) ||
        !WritePod(output, tile.tileX) || !WritePod(output, tile.tileY) ||
        !WritePod(output, tile.originX) || !WritePod(output, tile.originY) ||
        !WritePod(output, tile.extent.width) || !WritePod(output, tile.extent.height) ||
        !WritePod(output, sampleCount) || !WritePod(output, checksum)) {
        output.close();
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return SetError(error, "MFD tile cache could not write a tile header.");
    }
    output.write(
        reinterpret_cast<const char*>(tile.normalizedMosaic.data()),
        static_cast<std::streamsize>(tile.normalizedMosaic.size() * sizeof(float)));
    output.write(
        reinterpret_cast<const char*>(tile.comparisonGain.data()),
        static_cast<std::streamsize>(tile.comparisonGain.size() * sizeof(float)));
    output.write(
        reinterpret_cast<const char*>(tile.sampleFlags.data()),
        static_cast<std::streamsize>(tile.sampleFlags.size()));
    output.flush();
    const bool writeSucceeded = static_cast<bool>(output);
    output.close();
    if (!writeSucceeded) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return SetError(error, "MFD tile cache could not write a complete tile payload.");
    }

    std::error_code replaceError;
    if (std::filesystem::exists(path, replaceError) && !replaceError) {
        std::filesystem::remove(path, replaceError);
    }
    if (replaceError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return SetError(error, "MFD tile cache could not replace a corrupt tile.");
    }
    std::filesystem::rename(temporary, path, replaceError);
    if (replaceError) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        return SetError(error, "MFD tile cache could not atomically publish a tile.");
    }
    return true;
}

const char* PreparationFailureName(PreparationFailure failure) {
    switch (failure) {
        case PreparationFailure::None: return "none";
        case PreparationFailure::Canceled: return "canceled";
        case PreparationFailure::InvalidDimensions: return "invalid-dimensions";
        case PreparationFailure::IncompleteMosaic: return "incomplete-mosaic";
        case PreparationFailure::UnsupportedPixelLayout: return "unsupported-pixel-layout";
        case PreparationFailure::UnsupportedCfa: return "unsupported-cfa";
        case PreparationFailure::MalformedMetadata: return "malformed-metadata";
        case PreparationFailure::UnsupportedRequiredOperation: return "unsupported-required-operation";
        case PreparationFailure::InvalidGainMap: return "invalid-gain-map";
        case PreparationFailure::InvalidSourceIdentity: return "invalid-source-identity";
        case PreparationFailure::CacheIoError: return "cache-io-error";
    }
    return "none";
}

bool EvaluatePointwiseGain(
    const RawMetadata& metadata,
    PixelExtent activeExtent,
    std::uint64_t activeX,
    std::uint64_t activeY,
    double maximumAcceptedGain,
    float& gain,
    std::string* error) {
    if (activeExtent.width == 0 || activeExtent.height == 0 ||
        activeX >= activeExtent.width || activeY >= activeExtent.height ||
        !std::isfinite(maximumAcceptedGain) || maximumAcceptedGain <= 0.0) {
        return SetError(error, "MFD pointwise gain request is outside the active mosaic.");
    }
    for (const DngGainMapOpcode& map : metadata.dngGainMaps) {
        if (!ValidGainMapShape(map, activeExtent, maximumAcceptedGain, error)) return false;
    }
    return EvaluateValidatedPointwiseGain(
        metadata,
        activeExtent,
        activeX,
        activeY,
        maximumAcceptedGain,
        gain,
        error);
}

bool ReturnToPreGainDomain(
    float comparisonDomainSample,
    float referenceGain,
    float& preGainSample) {
    if (!std::isfinite(comparisonDomainSample) ||
        !std::isfinite(referenceGain) || referenceGain <= 0.0f) {
        return false;
    }
    const float result = comparisonDomainSample / referenceGain;
    if (!std::isfinite(result)) return false;
    preGainSample = result;
    return true;
}

PreparationResult PrepareRawFrame(
    const RawImageData& raw,
    const PreparationOptions& options,
    NormalizedTileCache& cache) {
    const RawMetadata& metadata = raw.metadata;
    if (IsCanceled(options)) {
        return FailureResult(PreparationFailure::Canceled, "MFD RAW preparation was canceled.");
    }
    if (metadata.rawWidth <= 0 || metadata.rawHeight <= 0) {
        return FailureResult(PreparationFailure::InvalidDimensions, "MFD RAW dimensions are invalid.");
    }
    std::size_t rawPixelCount = 0;
    if (!CheckedPixelCount(
            static_cast<std::uint64_t>(metadata.rawWidth),
            static_cast<std::uint64_t>(metadata.rawHeight),
            rawPixelCount)) {
        return FailureResult(PreparationFailure::InvalidDimensions, "MFD RAW dimensions overflow addressable memory.");
    }
    const bool normalizedMeasurement = raw.normalizedMosaicBuffer &&
        raw.normalizedMosaicInputContract != NormalizedMosaicInputContract::None;
    if ((!normalizedMeasurement && raw.rawBuffer.size() != rawPixelCount) ||
        (normalizedMeasurement && raw.normalizedMosaicBuffer->size() != rawPixelCount)) {
        return FailureResult(PreparationFailure::IncompleteMosaic,
            normalizedMeasurement
                ? "MultiFrame virtual Bayer preparation requires one complete normalized mosaic."
                : "MFD RAW preparation requires one complete stored Bayer mosaic.");
    }
    if (!metadata.mosaiced || metadata.pixelLayout != RawPixelLayout::MosaicBayer) {
        return FailureResult(PreparationFailure::UnsupportedPixelLayout, "MFD V1 accepts still-mosaiced Bayer RAW data only.");
    }
    CfaLayout layout;
    if (!CfaLayout::TryCreate(metadata.cfaPattern, layout)) {
        return FailureResult(PreparationFailure::UnsupportedCfa, "MFD V1 requires one of the four 2x2 Bayer layouts.");
    }
    if (!LooksLikeSha256(metadata.sourceContentSha256)) {
        return FailureResult(PreparationFailure::InvalidSourceIdentity, "MFD normalized cache requires the exact source-content SHA-256.");
    }
    if (options.tileRawPixels == 0u || options.tileRawPixels > 16384u ||
        !std::isfinite(options.saturationDnMargin) || options.saturationDnMargin <= 0.0 ||
        !std::isfinite(options.saturationNoiseSigmaMargin) ||
        options.saturationNoiseSigmaMargin <= 0.0 ||
        !std::isfinite(options.maximumComparisonGain) ||
        options.maximumComparisonGain <= 0.0 ||
        !std::all_of(
            options.saturationStdDevAtWhite.begin(),
            options.saturationStdDevAtWhite.end(),
            [](double value) { return std::isfinite(value) && value >= 0.0; })) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD RAW preparation options are invalid.");
    }
    const auto validOptionalMask = [&](const std::vector<std::uint8_t>& mask) {
        return mask.empty() || mask.size() == rawPixelCount;
    };
    if (!validOptionalMask(options.decoderSaturationMask) ||
        !validOptionalMask(options.decoderDefectMask) ||
        !validOptionalMask(options.decoderRepairedMask)) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD decoder masks do not match the stored sensor dimensions.");
    }

    const RawSensorRect active = Processing::ResolveActiveArea(metadata);
    if (active.left < 0 || active.top < 0 || active.right > metadata.rawWidth ||
        active.bottom > metadata.rawHeight || active.right <= active.left ||
        active.bottom <= active.top) {
        return FailureResult(PreparationFailure::InvalidDimensions, "MFD active sensor area is invalid.");
    }
    const PixelExtent activeExtent {
        static_cast<std::uint64_t>(active.right - active.left),
        static_cast<std::uint64_t>(active.bottom - active.top)
    };

    if (!std::isfinite(metadata.blackLevel) || !std::isfinite(metadata.whiteLevel) ||
        !std::all_of(
            metadata.perChannelBlack.begin(),
            metadata.perChannelBlack.end(),
            [](float value) { return std::isfinite(value); })) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD black or white metadata is non-finite.");
    }
    const int repeatRows = metadata.dngBlackLevelRepeatDim[0];
    const int repeatColumns = metadata.dngBlackLevelRepeatDim[1];
    if ((repeatRows == 0) != (repeatColumns == 0) || repeatRows < 0 || repeatColumns < 0) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD BlackLevelRepeatDim is malformed.");
    }
    if (repeatRows > 0) {
        if (static_cast<std::uint64_t>(repeatRows) > activeExtent.height ||
            static_cast<std::uint64_t>(repeatColumns) > activeExtent.width) {
            return FailureResult(PreparationFailure::MalformedMetadata, "MFD BlackLevelRepeatDim exceeds the active mosaic.");
        }
        const std::uint64_t repeatCount =
            static_cast<std::uint64_t>(repeatRows) * static_cast<std::uint64_t>(repeatColumns);
        if (repeatCount != metadata.dngBlackLevelValues.size()) {
            return FailureResult(PreparationFailure::MalformedMetadata, "MFD BlackLevel values do not match BlackLevelRepeatDim.");
        }
    } else if (!metadata.dngBlackLevelValues.empty()) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD BlackLevel values have no repeat dimensions.");
    }
    if ((!metadata.dngBlackLevelDeltaH.empty() &&
            metadata.dngBlackLevelDeltaH.size() != activeExtent.width) ||
        (!metadata.dngBlackLevelDeltaV.empty() &&
            metadata.dngBlackLevelDeltaV.size() != activeExtent.height)) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD BlackLevelDelta dimensions do not match the active area.");
    }
    const auto finiteValues = [](const std::vector<float>& values) {
        return std::all_of(values.begin(), values.end(), [](float value) {
            return std::isfinite(value);
        });
    };
    if (!finiteValues(metadata.dngBlackLevelValues) ||
        !finiteValues(metadata.dngBlackLevelDeltaH) ||
        !finiteValues(metadata.dngBlackLevelDeltaV) ||
        !finiteValues(metadata.dngWhiteLevelValues) ||
        (!metadata.dngWhiteLevelValues.empty() &&
            metadata.dngWhiteLevelValues.size() != 1u &&
            metadata.dngWhiteLevelValues.size() != 3u &&
            metadata.dngWhiteLevelValues.size() != 4u)) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD black/white arrays are malformed or unsupported.");
    }
    if (!metadata.dngLinearizationTable.empty()) {
        if (metadata.dngLinearizationTable.size() < 2u ||
            !std::is_sorted(
                metadata.dngLinearizationTable.begin(),
                metadata.dngLinearizationTable.end())) {
            return FailureResult(PreparationFailure::MalformedMetadata, "MFD LinearizationTable is incomplete or non-monotonic.");
        }
    }
    for (int count : metadata.dngOpcodeCount) {
        if (count < 0) {
            return FailureResult(PreparationFailure::MalformedMetadata, "MFD DNG opcode counts are malformed.");
        }
    }
    for (std::size_t list = 0; list < metadata.dngOpcodeCount.size(); ++list) {
        if (metadata.dngAppliedOpcodeCountByList[list] < 0 ||
            metadata.dngUnsupportedOpcodeCountByList[list] < 0 ||
            metadata.dngAppliedOpcodeCountByList[list] >
                metadata.dngOpcodeCount[list]) {
            return FailureResult(PreparationFailure::MalformedMetadata, "MFD DNG opcode accounting is inconsistent.");
        }
    }
    if (metadata.dngGainMapCount < 0 ||
        static_cast<std::size_t>(metadata.dngGainMapCount) != metadata.dngGainMaps.size() ||
        metadata.dngGainMapCount > metadata.dngOpcodeCount[1]) {
        return FailureResult(PreparationFailure::MalformedMetadata, "MFD DNG GainMap counts are inconsistent.");
    }

    PreparedRawFrame prepared;
    prepared.sourceContentSha256 = LowerAscii(metadata.sourceContentSha256);
    prepared.sourceByteSize = metadata.sourceByteSize;
    prepared.sensorActiveArea = active;
    prepared.activeExtent = activeExtent;
    prepared.activeCfaPattern = metadata.cfaPattern;
    prepared.tileRawPixels = options.tileRawPixels;
    prepared.tileColumns = static_cast<std::uint32_t>(
        (activeExtent.width + options.tileRawPixels - 1u) / options.tileRawPixels);
    prepared.tileRows = static_cast<std::uint32_t>(
        (activeExtent.height + options.tileRawPixels - 1u) / options.tileRawPixels);
    prepared.calibration.operations = ClassifyPreDemosaicOperations(metadata);
    if (!prepared.calibration.operations.compatible) {
        return FailureResult(
            PreparationFailure::UnsupportedRequiredOperation,
            "MFD RAW contains a required pre-demosaic operation that V1 cannot preserve safely.",
            std::move(prepared));
    }

    std::string gainError;
    for (const DngGainMapOpcode& map : metadata.dngGainMaps) {
        if (!ValidGainMapShape(
                map,
                activeExtent,
                options.maximumComparisonGain,
                &gainError)) {
            return FailureResult(PreparationFailure::InvalidGainMap, gainError, std::move(prepared));
        }
    }

    std::string blackError;
    if (!ComputeMaximumBlackByCfaSite(
            metadata,
            layout,
            activeExtent,
            options,
            prepared.calibration.maximumBlackByCfaSite,
            &blackError)) {
        const bool canceled = blackError == "MFD RAW preparation was canceled.";
        return FailureResult(
            canceled ? PreparationFailure::Canceled : PreparationFailure::MalformedMetadata,
            blackError,
            std::move(prepared));
    }
    for (CfaSite site : { CfaSite::Red, CfaSite::Green0, CfaSite::Green1, CfaSite::Blue }) {
        const std::size_t siteIndex = SiteIndex(site);
        const double white = WhiteForSite(metadata, site);
        const double span = white - prepared.calibration.maximumBlackByCfaSite[siteIndex];
        if (!std::isfinite(white) || !std::isfinite(span) || span <= 0.0) {
            return FailureResult(PreparationFailure::MalformedMetadata, "MFD white level does not exceed the maximum computed black level.", std::move(prepared));
        }
        prepared.calibration.whiteLevelByCfaSite[siteIndex] = white;
        prepared.calibration.usableSpanByCfaSite[siteIndex] = span;
    }

    if (!options.decoderDefectMask.empty() && !options.decoderRepairedMask.empty()) {
        prepared.defectMaskProvenance =
            DefectMaskProvenance::DecoderKnownDefectsAndRepairMap;
    } else if (!options.decoderDefectMask.empty()) {
        prepared.defectMaskProvenance = DefectMaskProvenance::DecoderKnownDefects;
    } else if (!options.decoderRepairedMask.empty()) {
        prepared.defectMaskProvenance = DefectMaskProvenance::DecoderRepairMap;
    }
    prepared.cacheKey = BuildPreparationCacheKey(
        metadata, active, prepared.calibration, options);
    if (!LooksLikeSha256(prepared.cacheKey)) {
        return FailureResult(PreparationFailure::InvalidSourceIdentity, "MFD could not build a normalized-cache identity.", std::move(prepared));
    }

    const std::uint64_t totalTiles =
        static_cast<std::uint64_t>(prepared.tileColumns) *
        static_cast<std::uint64_t>(prepared.tileRows);
    std::uint64_t completedTiles = 0u;
    if (options.reportProgress) {
        options.reportProgress(completedTiles, totalTiles);
    }
    for (std::uint32_t tileY = 0; tileY < prepared.tileRows; ++tileY) {
        for (std::uint32_t tileX = 0; tileX < prepared.tileColumns; ++tileX) {
            if (IsCanceled(options)) {
                return FailureResult(PreparationFailure::Canceled, "MFD RAW preparation was canceled.", std::move(prepared));
            }
            const std::uint64_t originX =
                static_cast<std::uint64_t>(tileX) * options.tileRawPixels;
            const std::uint64_t originY =
                static_cast<std::uint64_t>(tileY) * options.tileRawPixels;
            const PixelExtent tileExtent {
                std::min<std::uint64_t>(options.tileRawPixels, activeExtent.width - originX),
                std::min<std::uint64_t>(options.tileRawPixels, activeExtent.height - originY)
            };
            PreparedRawTile tile;
            std::string cacheError;
            TileCacheReadStatus readStatus = cache.Read(
                prepared.cacheKey, tileX, tileY, tile, &cacheError);
            const bool matchingHit = readStatus == TileCacheReadStatus::Hit &&
                tile.tileX == tileX && tile.tileY == tileY &&
                tile.originX == originX && tile.originY == originY &&
                tile.extent.width == tileExtent.width &&
                tile.extent.height == tileExtent.height;
            if (matchingHit) {
                ++prepared.cacheHitTileCount;
                AccumulateFlags(tile, prepared);
                ++completedTiles;
                if (options.reportProgress) {
                    options.reportProgress(completedTiles, totalTiles);
                }
                continue;
            }
            if (readStatus == TileCacheReadStatus::IoError) {
                return FailureResult(PreparationFailure::CacheIoError, cacheError, std::move(prepared));
            }

            std::size_t tilePixelCount = 0;
            if (!CheckedPixelCount(tileExtent.width, tileExtent.height, tilePixelCount)) {
                return FailureResult(PreparationFailure::InvalidDimensions, "MFD tile dimensions overflow addressable memory.", std::move(prepared));
            }
            tile = {};
            tile.tileX = tileX;
            tile.tileY = tileY;
            tile.originX = originX;
            tile.originY = originY;
            tile.extent = tileExtent;
            tile.normalizedMosaic.resize(tilePixelCount);
            tile.comparisonGain.resize(tilePixelCount);
            tile.sampleFlags.assign(tilePixelCount, 0u);

            for (std::uint64_t localY = 0; localY < tileExtent.height; ++localY) {
                const std::uint64_t activeY = originY + localY;
                const std::uint64_t sensorY = static_cast<std::uint64_t>(active.top) + activeY;
                for (std::uint64_t localX = 0; localX < tileExtent.width; ++localX) {
                    const std::uint64_t activeX = originX + localX;
                    const std::uint64_t sensorX = static_cast<std::uint64_t>(active.left) + activeX;
                    const std::size_t tileIndex =
                        static_cast<std::size_t>(localY * tileExtent.width + localX);
                    const std::size_t sensorIndex = static_cast<std::size_t>(
                        sensorY * static_cast<std::uint64_t>(metadata.rawWidth) + sensorX);
                    const CfaSite site = layout.SiteAt(
                        static_cast<std::int64_t>(activeX),
                        static_cast<std::int64_t>(activeY));
                    const std::size_t siteIndex = SiteIndex(site);
                    if (!normalizedMeasurement &&
                        !metadata.dngLinearizationTable.empty() &&
                        static_cast<std::size_t>(raw.rawBuffer[sensorIndex]) >=
                            metadata.dngLinearizationTable.size()) {
                        return FailureResult(PreparationFailure::MalformedMetadata, "MFD stored sample is outside the LinearizationTable.", std::move(prepared));
                    }
                    const double normalized = normalizedMeasurement
                        ? static_cast<double>((*raw.normalizedMosaicBuffer)[sensorIndex])
                        : (Linearize(metadata, raw.rawBuffer[sensorIndex]) -
                            BlackAtActiveCoordinate(
                                metadata, site, activeX, activeY)) /
                            prepared.calibration.usableSpanByCfaSite[siteIndex];
                    if (!std::isfinite(normalized)) {
                        return FailureResult(PreparationFailure::MalformedMetadata, "MFD normalization produced a non-finite sample.", std::move(prepared));
                    }
                    const float normalizedFloat = static_cast<float>(normalized);
                    if (!std::isfinite(normalizedFloat)) {
                        return FailureResult(PreparationFailure::MalformedMetadata, "MFD normalized sample exceeds the float32 contract.", std::move(prepared));
                    }
                    tile.normalizedMosaic[tileIndex] = normalizedFloat;

                    float gain = 1.0f;
                    if (!EvaluateValidatedPointwiseGain(
                            metadata,
                            activeExtent,
                            activeX,
                            activeY,
                            options.maximumComparisonGain,
                            gain,
                            &gainError)) {
                        return FailureResult(PreparationFailure::InvalidGainMap, gainError, std::move(prepared));
                    }
                    tile.comparisonGain[tileIndex] = gain;

                    const double span = prepared.calibration.usableSpanByCfaSite[siteIndex];
                    const double saturationMargin = std::max(
                        options.saturationDnMargin / span,
                        options.saturationNoiseSigmaMargin *
                            options.saturationStdDevAtWhite[siteIndex]);
                    std::uint8_t flags = 0u;
                    if (normalized >= 1.0 - saturationMargin) {
                        flags |= SampleFlagMask(PreparedSampleFlag::Saturated);
                    }
                    if (!options.decoderSaturationMask.empty() &&
                        options.decoderSaturationMask[sensorIndex] != 0u) {
                        flags |= SampleFlagMask(PreparedSampleFlag::Saturated);
                        flags |= SampleFlagMask(PreparedSampleFlag::ExplicitDecoderClip);
                    }
                    if (raw.multiFrameMeasurementSidecars) {
                        const auto& sidecars = *raw.multiFrameMeasurementSidecars;
                        if (sidecars.validity &&
                            sidecars.validity->size() == rawPixelCount &&
                            (*sidecars.validity)[sensorIndex] == 0u) {
                            flags |= SampleFlagMask(PreparedSampleFlag::Defective);
                        }
                        if (sidecars.clipping &&
                            sidecars.clipping->size() == rawPixelCount &&
                            (*sidecars.clipping)[sensorIndex] != 0u) {
                            flags |= SampleFlagMask(PreparedSampleFlag::Saturated);
                            flags |= SampleFlagMask(
                                PreparedSampleFlag::ExplicitDecoderClip);
                        }
                    }
                    if (!options.decoderDefectMask.empty() &&
                        options.decoderDefectMask[sensorIndex] != 0u) {
                        flags |= SampleFlagMask(PreparedSampleFlag::Defective);
                    }
                    if (!options.decoderRepairedMask.empty() &&
                        options.decoderRepairedMask[sensorIndex] != 0u) {
                        flags |= SampleFlagMask(PreparedSampleFlag::DecoderRepaired);
                    }
                    tile.sampleFlags[tileIndex] = flags;
                }
            }
            if (!cache.Write(prepared.cacheKey, tile, &cacheError)) {
                return FailureResult(PreparationFailure::CacheIoError, cacheError, std::move(prepared));
            }
            ++prepared.generatedTileCount;
            AccumulateFlags(tile, prepared);
            ++completedTiles;
            if (options.reportProgress) {
                options.reportProgress(completedTiles, totalTiles);
            }
        }
    }

    PreparationResult result;
    result.success = true;
    result.frame = std::move(prepared);
    result.message = "MFD RAW preparation completed without demosaicing or applying comparison gains to the normalized mosaic.";
    return result;
}

TileCacheReadStatus ReadPreparedTile(
    const PreparedRawFrame& frame,
    NormalizedTileCache& cache,
    std::uint32_t tileX,
    std::uint32_t tileY,
    PreparedRawTile& tile,
    std::string* error) {
    if (frame.contractVersion != kPreparationContractVersion ||
        frame.contractId != kPreparationContractId ||
        tileX >= frame.tileColumns || tileY >= frame.tileRows) {
        SetError(error, "MFD prepared-tile request does not match the preparation contract.");
        return TileCacheReadStatus::IoError;
    }
    return cache.Read(frame.cacheKey, tileX, tileY, tile, error);
}

} // namespace Raw::Mfd
