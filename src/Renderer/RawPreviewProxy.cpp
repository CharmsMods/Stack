#include "Renderer/RawPreviewProxy.h"

#include "Raw/RawProcessingMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace {

int VisibleWidth(const Raw::RawMetadata& metadata) {
    if (metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer) {
        const Raw::RawSensorRect active = Raw::Processing::ResolveActiveArea(metadata);
        return std::max(0, active.right - active.left);
    }
    return metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
}

int VisibleHeight(const Raw::RawMetadata& metadata) {
    if (metadata.pixelLayout == Raw::RawPixelLayout::MosaicBayer) {
        const Raw::RawSensorRect active = Raw::Processing::ResolveActiveArea(metadata);
        return std::max(0, active.bottom - active.top);
    }
    return metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
}

int ScalePreviewDimension(int value, int longestSide, int previewMaxDimension, bool forceEven) {
    int scaled = std::max(1, static_cast<int>(
        (static_cast<long long>(value) * static_cast<long long>(previewMaxDimension) + longestSide / 2) /
        std::max(1, longestSide)));
    scaled = std::min(scaled, value);
    if (forceEven && value >= 2) {
        scaled = std::max(2, scaled);
        scaled &= ~1;
        if (scaled <= 0) {
            scaled = 2;
        }
        if (scaled > value) {
            scaled = value & ~1;
        }
    }
    return std::max(1, scaled);
}

bool ResolvePreviewVisibleSize(
    const Raw::RawMetadata& metadata,
    int previewMaxDimension,
    bool forceEven,
    int& outWidth,
    int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    const int sourceWidth = VisibleWidth(metadata);
    const int sourceHeight = VisibleHeight(metadata);
    if (previewMaxDimension <= 0 || sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }
    if (forceEven && (sourceWidth < 2 || sourceHeight < 2)) {
        return false;
    }

    const int longestSide = std::max(sourceWidth, sourceHeight);
    if (longestSide <= previewMaxDimension) {
        return false;
    }

    outWidth = ScalePreviewDimension(sourceWidth, longestSide, previewMaxDimension, forceEven);
    outHeight = ScalePreviewDimension(sourceHeight, longestSide, previewMaxDimension, forceEven);
    return outWidth > 0 && outHeight > 0 &&
        (outWidth != sourceWidth || outHeight != sourceHeight);
}

int NearestSourceCoordinate(int destination, int destinationSize, int sourceSize) {
    if (destinationSize <= 1 || sourceSize <= 1) {
        return 0;
    }
    const double scaled =
        ((static_cast<double>(destination) + 0.5) * static_cast<double>(sourceSize) /
            static_cast<double>(destinationSize)) - 0.5;
    return std::clamp(static_cast<int>(std::lround(scaled)), 0, sourceSize - 1);
}

int NearestSourceCoordinateWithParity(
    int destination,
    int destinationSize,
    int sourceSize,
    int parity) {
    int coordinate = NearestSourceCoordinate(destination, destinationSize, sourceSize);
    if ((coordinate & 1) == parity) {
        return coordinate;
    }

    const int lower = coordinate - 1;
    const int upper = coordinate + 1;
    const bool lowerValid = lower >= 0 && ((lower & 1) == parity);
    const bool upperValid = upper < sourceSize && ((upper & 1) == parity);
    if (lowerValid && upperValid) {
        const int lowerDistance = std::abs(coordinate - lower);
        const int upperDistance = std::abs(upper - coordinate);
        return lowerDistance <= upperDistance ? lower : upper;
    }
    if (lowerValid) {
        return lower;
    }
    if (upperValid) {
        return upper;
    }
    return std::clamp(coordinate, 0, sourceSize - 1);
}

std::vector<std::pair<int, double>> CfaSublatticeContributions(
    int destination,
    int destinationSize,
    int sourceSize) {
    std::vector<std::pair<int, double>> contributions;
    const int parity = destination & 1;
    const int destinationCount = parity == 0 ? (destinationSize + 1) / 2 : destinationSize / 2;
    const int sourceCount = parity == 0 ? (sourceSize + 1) / 2 : sourceSize / 2;
    if (destinationCount <= 0 || sourceCount <= 0) {
        return contributions;
    }

    const int destinationIndex = destination / 2;
    const double sourceStart =
        static_cast<double>(destinationIndex) * static_cast<double>(sourceCount) /
        static_cast<double>(destinationCount);
    const double sourceEnd =
        static_cast<double>(destinationIndex + 1) * static_cast<double>(sourceCount) /
        static_cast<double>(destinationCount);
    const int first = std::max(0, static_cast<int>(std::floor(sourceStart)));
    const int last = std::min(sourceCount, static_cast<int>(std::ceil(sourceEnd)));
    for (int sourceIndex = first; sourceIndex < last; ++sourceIndex) {
        const double overlap =
            std::max(0.0, std::min(sourceEnd, static_cast<double>(sourceIndex + 1)) -
                std::max(sourceStart, static_cast<double>(sourceIndex)));
        if (overlap <= 0.0) {
            continue;
        }
        const int coordinate = parity + sourceIndex * 2;
        if (coordinate >= 0 && coordinate < sourceSize) {
            contributions.emplace_back(coordinate, overlap);
        }
    }
    return contributions;
}

void NormalizePreviewMetadata(
    const Raw::RawMetadata& sourceMetadata,
    int previewWidth,
    int previewHeight,
    Raw::RawMetadata& previewMetadata) {
    previewMetadata = sourceMetadata;
    previewMetadata.rawWidth = previewWidth;
    previewMetadata.rawHeight = previewHeight;
    previewMetadata.visibleWidth = previewWidth;
    previewMetadata.visibleHeight = previewHeight;
    previewMetadata.leftMargin = 0;
    previewMetadata.topMargin = 0;
    previewMetadata.dngGainMaps.clear();
    previewMetadata.dngGainMapCount = 0;
    previewMetadata.dngBlackLevelDeltaH.clear();
    previewMetadata.dngBlackLevelDeltaV.clear();
    previewMetadata.dngActiveArea = { 0, 0, previewHeight, previewWidth };
    previewMetadata.hasDngActiveArea = true;
    previewMetadata.dngMaskedAreas.clear();
}

bool BuildMosaicPreviewRawData(
    const Raw::RawImageData& source,
    int previewMaxDimension,
    Raw::RawImageData& preview) {
    if (source.metadata.pixelLayout != Raw::RawPixelLayout::MosaicBayer) {
        return false;
    }
    if (source.metadata.dngGainMapCount > 0 ||
        !source.metadata.dngGainMaps.empty() ||
        !source.metadata.dngLinearizationTable.empty() ||
        !source.metadata.dngBlackLevelDeltaH.empty() ||
        !source.metadata.dngBlackLevelDeltaV.empty() ||
        source.metadata.dngBlackLevelRepeatDim[0] > 2 ||
        source.metadata.dngBlackLevelRepeatDim[1] > 2 ||
        (source.metadata.dngCfaRepeatPatternDim[0] > 0 &&
            (source.metadata.dngCfaRepeatPatternDim[0] != 2 ||
                source.metadata.dngCfaRepeatPatternDim[1] != 2))) {
        return false;
    }

    int previewWidth = 0;
    int previewHeight = 0;
    if (!ResolvePreviewVisibleSize(source.metadata, previewMaxDimension, true, previewWidth, previewHeight)) {
        return false;
    }

    const int sourceRawWidth = source.metadata.rawWidth;
    const int sourceRawHeight = source.metadata.rawHeight;
    const int sourceVisibleWidth = VisibleWidth(source.metadata);
    const int sourceVisibleHeight = VisibleHeight(source.metadata);
    const std::size_t sourceRawSize =
        static_cast<std::size_t>(sourceRawWidth) * static_cast<std::size_t>(sourceRawHeight);
    if (sourceRawWidth <= 0 ||
        sourceRawHeight <= 0 ||
        sourceVisibleWidth <= 0 ||
        sourceVisibleHeight <= 0 ||
        source.rawBuffer.size() < sourceRawSize) {
        return false;
    }

    NormalizePreviewMetadata(source.metadata, previewWidth, previewHeight, preview.metadata);
    if (preview.metadata.hasDngNoiseProfile &&
        sourceVisibleWidth > 0 &&
        sourceVisibleHeight > 0) {
        // Each proxy CFA sample is an area-weighted average of same-plane
        // sensor samples. For independent sensor noise, averaging N samples
        // divides both S and O in variance = S*x + O by N. A single global
        // coefficient cannot represent fractional edge footprints exactly,
        // so use the image-area ratio as the deterministic proxy estimate.
        const double varianceScale = std::clamp(
            static_cast<double>(previewWidth) *
                static_cast<double>(previewHeight) /
                (static_cast<double>(sourceVisibleWidth) *
                    static_cast<double>(sourceVisibleHeight)),
            0.0,
            1.0);
        for (Raw::DngNoiseProfilePlane& plane :
             preview.metadata.dngNoiseProfile) {
            plane.shotScale *= varianceScale;
            plane.readNoiseVariance *= varianceScale;
        }
    }
    preview.rawBuffer.assign(
        static_cast<std::size_t>(previewWidth) * static_cast<std::size_t>(previewHeight),
        0);
    preview.linearUInt16Buffer.clear();
    preview.linearFloatBuffer.clear();

    const Raw::RawSensorRect activeArea = Raw::Processing::ResolveActiveArea(source.metadata);
    const int cropX = activeArea.left;
    const int cropY = activeArea.top;
    for (int y = 0; y < previewHeight; ++y) {
        const std::vector<std::pair<int, double>> sourceRows =
            CfaSublatticeContributions(y, previewHeight, sourceVisibleHeight);
        for (int x = 0; x < previewWidth; ++x) {
            const std::vector<std::pair<int, double>> sourceColumns =
                CfaSublatticeContributions(x, previewWidth, sourceVisibleWidth);
            double weightedSum = 0.0;
            double weightSum = 0.0;
            for (const auto& [sourceY, rowWeight] : sourceRows) {
                const int rawY = std::clamp(cropY + sourceY, 0, sourceRawHeight - 1);
                for (const auto& [sourceX, columnWeight] : sourceColumns) {
                    const int rawX = std::clamp(cropX + sourceX, 0, sourceRawWidth - 1);
                    const double weight = rowWeight * columnWeight;
                    weightedSum +=
                        static_cast<double>(source.rawBuffer[
                            static_cast<std::size_t>(rawY) * static_cast<std::size_t>(sourceRawWidth) +
                            static_cast<std::size_t>(rawX)]) *
                        weight;
                    weightSum += weight;
                }
            }
            if (weightSum <= 0.0) {
                const int sourceY = NearestSourceCoordinateWithParity(
                    y, previewHeight, sourceVisibleHeight, y & 1);
                const int sourceX = NearestSourceCoordinateWithParity(
                    x, previewWidth, sourceVisibleWidth, x & 1);
                weightedSum = source.rawBuffer[
                    static_cast<std::size_t>(std::clamp(cropY + sourceY, 0, sourceRawHeight - 1)) *
                        static_cast<std::size_t>(sourceRawWidth) +
                    static_cast<std::size_t>(std::clamp(cropX + sourceX, 0, sourceRawWidth - 1))];
                weightSum = 1.0;
            }
            preview.rawBuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(previewWidth) +
                static_cast<std::size_t>(x)] =
                static_cast<std::uint16_t>(std::clamp(
                    std::lround(weightedSum / weightSum),
                    0l,
                    static_cast<long>(std::numeric_limits<std::uint16_t>::max())));
        }
    }
    return true;
}

bool BuildLinearPreviewRawData(
    const Raw::RawImageData& source,
    int previewMaxDimension,
    Raw::RawImageData& preview) {
    if (source.metadata.pixelLayout != Raw::RawPixelLayout::LinearRgb) {
        return false;
    }

    int previewWidth = 0;
    int previewHeight = 0;
    if (!ResolvePreviewVisibleSize(source.metadata, previewMaxDimension, false, previewWidth, previewHeight)) {
        return false;
    }

    const int sourceWidth = VisibleWidth(source.metadata);
    const int sourceHeight = VisibleHeight(source.metadata);
    const int sourceChannels = std::clamp(source.metadata.linearChannels, 3, 4);
    if (sourceWidth <= 0 || sourceHeight <= 0 || sourceChannels < 3) {
        return false;
    }

    const std::size_t sourcePixelCount =
        static_cast<std::size_t>(sourceWidth) * static_cast<std::size_t>(sourceHeight);
    const std::size_t sourceSampleCount = sourcePixelCount * static_cast<std::size_t>(sourceChannels);

    NormalizePreviewMetadata(source.metadata, previewWidth, previewHeight, preview.metadata);
    preview.metadata.linearChannels = sourceChannels;
    preview.rawBuffer.clear();
    preview.linearUInt16Buffer.clear();
    preview.linearFloatBuffer.clear();

    const std::size_t previewSampleCount =
        static_cast<std::size_t>(previewWidth) *
        static_cast<std::size_t>(previewHeight) *
        static_cast<std::size_t>(sourceChannels);
    if (!source.linearUInt16Buffer.empty()) {
        if (source.linearUInt16Buffer.size() < sourceSampleCount) {
            return false;
        }
        preview.linearUInt16Buffer.assign(previewSampleCount, 0);
        for (int y = 0; y < previewHeight; ++y) {
            const int sourceY = NearestSourceCoordinate(y, previewHeight, sourceHeight);
            for (int x = 0; x < previewWidth; ++x) {
                const int sourceX = NearestSourceCoordinate(x, previewWidth, sourceWidth);
                const std::size_t sourceIndex =
                    (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(sourceWidth) +
                        static_cast<std::size_t>(sourceX)) *
                    static_cast<std::size_t>(sourceChannels);
                const std::size_t previewIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(previewWidth) +
                        static_cast<std::size_t>(x)) *
                    static_cast<std::size_t>(sourceChannels);
                for (int c = 0; c < sourceChannels; ++c) {
                    preview.linearUInt16Buffer[previewIndex + static_cast<std::size_t>(c)] =
                        source.linearUInt16Buffer[sourceIndex + static_cast<std::size_t>(c)];
                }
            }
        }
        return true;
    }

    if (source.linearFloatBuffer.size() < sourceSampleCount) {
        return false;
    }
    preview.linearFloatBuffer.assign(previewSampleCount, 0.0f);
    for (int y = 0; y < previewHeight; ++y) {
        const int sourceY = NearestSourceCoordinate(y, previewHeight, sourceHeight);
        for (int x = 0; x < previewWidth; ++x) {
            const int sourceX = NearestSourceCoordinate(x, previewWidth, sourceWidth);
            const std::size_t sourceIndex =
                (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(sourceWidth) +
                    static_cast<std::size_t>(sourceX)) *
                static_cast<std::size_t>(sourceChannels);
            const std::size_t previewIndex =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(previewWidth) +
                    static_cast<std::size_t>(x)) *
                static_cast<std::size_t>(sourceChannels);
            for (int c = 0; c < sourceChannels; ++c) {
                preview.linearFloatBuffer[previewIndex + static_cast<std::size_t>(c)] =
                    source.linearFloatBuffer[sourceIndex + static_cast<std::size_t>(c)];
            }
        }
    }
    return true;
}

} // namespace

namespace Stack::Renderer::RawPreviewProxy {

bool HasPixels(const Raw::RawImageData& rawData) {
    return !rawData.rawBuffer.empty() ||
        !rawData.linearUInt16Buffer.empty() ||
        !rawData.linearFloatBuffer.empty() ||
        (rawData.normalizedMosaicBuffer &&
         !rawData.normalizedMosaicBuffer->empty());
}

bool BuildPreviewRawData(const Raw::RawImageData& source, int previewMaxDimension, Raw::RawImageData& preview) {
    preview = Raw::RawImageData {};
    if (previewMaxDimension <= 0 || !HasPixels(source) || !source.metadata.error.empty()) {
        return false;
    }
    return source.metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb
        ? BuildLinearPreviewRawData(source, previewMaxDimension, preview)
        : BuildMosaicPreviewRawData(source, previewMaxDimension, preview);
}

std::string BuildCacheKey(
    const std::string& sourceCacheKey,
    const Raw::RawImageData& rawData,
    int previewMaxDimension) {
    const Raw::RawMetadata& metadata = rawData.metadata;
    std::string key = sourceCacheKey;
    key += "#preview:";
    key += std::to_string(previewMaxDimension);
    key += ":layout:";
    key += std::to_string(static_cast<int>(metadata.pixelLayout));
    key += ":raw:";
    key += std::to_string(metadata.rawWidth);
    key += "x";
    key += std::to_string(metadata.rawHeight);
    key += ":visible:";
    key += std::to_string(VisibleWidth(metadata));
    key += "x";
    key += std::to_string(VisibleHeight(metadata));
    key += ":crop:";
    key += std::to_string(metadata.leftMargin);
    key += ",";
    key += std::to_string(metadata.topMargin);
    key += ":active:";
    key += metadata.hasDngActiveArea ? "1:" : "0:";
    key += std::to_string(metadata.dngActiveArea.top);
    key += ",";
    key += std::to_string(metadata.dngActiveArea.left);
    key += ",";
    key += std::to_string(metadata.dngActiveArea.bottom);
    key += ",";
    key += std::to_string(metadata.dngActiveArea.right);
    key += ":samples:";
    key += std::to_string(rawData.rawBuffer.size());
    key += ",";
    key += std::to_string(rawData.linearUInt16Buffer.size());
    key += ",";
    key += std::to_string(rawData.linearFloatBuffer.size());
    key += ",";
    key += std::to_string(
        rawData.normalizedMosaicBuffer
            ? rawData.normalizedMosaicBuffer->size()
            : 0u);
    key += ":normalized-hash:";
    key += std::to_string(rawData.normalizedMosaicContentHash);
    return key;
}

Summary Summarize(const Raw::RawImageData& rawData, bool usedProxy) {
    Summary summary;
    summary.usedProxy = usedProxy;
    summary.rawWidth = rawData.metadata.rawWidth;
    summary.rawHeight = rawData.metadata.rawHeight;
    summary.visibleWidth = VisibleWidth(rawData.metadata);
    summary.visibleHeight = VisibleHeight(rawData.metadata);
    summary.rawSampleCount = rawData.rawBuffer.size();
    summary.linearUInt16SampleCount = rawData.linearUInt16Buffer.size();
    summary.linearFloatSampleCount = rawData.linearFloatBuffer.size();
    summary.normalizedMosaicSampleCount =
        rawData.normalizedMosaicBuffer
            ? rawData.normalizedMosaicBuffer->size()
            : 0u;
    summary.dngGainMapCount = rawData.metadata.dngGainMapCount;
    return summary;
}

} // namespace Stack::Renderer::RawPreviewProxy
