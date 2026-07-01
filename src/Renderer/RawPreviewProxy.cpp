#include "Renderer/RawPreviewProxy.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

int VisibleWidth(const Raw::RawMetadata& metadata) {
    return metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
}

int VisibleHeight(const Raw::RawMetadata& metadata) {
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
}

bool BuildMosaicPreviewRawData(
    const Raw::RawImageData& source,
    int previewMaxDimension,
    Raw::RawImageData& preview) {
    if (source.metadata.pixelLayout != Raw::RawPixelLayout::MosaicBayer) {
        return false;
    }
    if (source.metadata.dngGainMapCount > 0 || !source.metadata.dngGainMaps.empty()) {
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
    preview.rawBuffer.assign(
        static_cast<std::size_t>(previewWidth) * static_cast<std::size_t>(previewHeight),
        0);
    preview.linearUInt16Buffer.clear();
    preview.linearFloatBuffer.clear();

    const int cropX = std::max(0, source.metadata.leftMargin);
    const int cropY = std::max(0, source.metadata.topMargin);
    for (int y = 0; y < previewHeight; ++y) {
        const int sourceY = NearestSourceCoordinateWithParity(
            y,
            previewHeight,
            sourceVisibleHeight,
            y & 1);
        const int rawY = std::clamp(cropY + sourceY, 0, sourceRawHeight - 1);
        for (int x = 0; x < previewWidth; ++x) {
            const int sourceX = NearestSourceCoordinateWithParity(
                x,
                previewWidth,
                sourceVisibleWidth,
                x & 1);
            const int rawX = std::clamp(cropX + sourceX, 0, sourceRawWidth - 1);
            preview.rawBuffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(previewWidth) +
                static_cast<std::size_t>(x)] =
                source.rawBuffer[static_cast<std::size_t>(rawY) * static_cast<std::size_t>(sourceRawWidth) +
                    static_cast<std::size_t>(rawX)];
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
        !rawData.linearFloatBuffer.empty();
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
    key += ":samples:";
    key += std::to_string(rawData.rawBuffer.size());
    key += ",";
    key += std::to_string(rawData.linearUInt16Buffer.size());
    key += ",";
    key += std::to_string(rawData.linearFloatBuffer.size());
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
    summary.dngGainMapCount = rawData.metadata.dngGainMapCount;
    return summary;
}

} // namespace Stack::Renderer::RawPreviewProxy
