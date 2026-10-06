#include "Renderer/RawPreviewProxy.h"

#include "Raw/RawProcessingMath.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
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

int ScalePreviewDimension(int value, int longestSide, int previewMaxDimension) {
    int scaled = std::max(1, static_cast<int>(
        (static_cast<long long>(value) * static_cast<long long>(previewMaxDimension) + longestSide / 2) /
        std::max(1, longestSide)));
    scaled = std::min(scaled, value);
    return std::max(1, scaled);
}

bool ResolvePreviewVisibleSize(
    const Raw::RawMetadata& metadata,
    int previewMaxDimension,
    int& outWidth,
    int& outHeight) {
    outWidth = 0;
    outHeight = 0;
    const int sourceWidth = VisibleWidth(metadata);
    const int sourceHeight = VisibleHeight(metadata);
    if (previewMaxDimension <= 0 || sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }
    const int longestSide = std::max(sourceWidth, sourceHeight);
    if (longestSide <= previewMaxDimension) {
        return false;
    }

    outWidth = ScalePreviewDimension(sourceWidth, longestSide, previewMaxDimension);
    outHeight = ScalePreviewDimension(sourceHeight, longestSide, previewMaxDimension);
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

void AssignPreviewIdentity(
    const Raw::RawImageData& source,
    int previewWidth,
    int previewHeight,
    std::string_view correctionVersion,
    Raw::RawImageData& preview) {
    const std::string& sourceIdentity = !source.contentIdentity.empty()
        ? source.contentIdentity
        : source.metadata.sourceContentSha256;
    preview.decoderIdentityVersion = source.decoderIdentityVersion;
    preview.contentIdentity = sourceIdentity + "#proxy:" +
        std::to_string(previewWidth) + "x" +
        std::to_string(previewHeight) + ":" +
        std::string(correctionVersion);
    preview.contentIdentityHash = static_cast<std::uint64_t>(
        std::hash<std::string>{}(preview.contentIdentity));
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
    previewMetadata.dngLinearizationTable.clear();
    previewMetadata.dngBlackLevelDeltaH.clear();
    previewMetadata.dngBlackLevelDeltaV.clear();
    previewMetadata.dngActiveArea = { 0, 0, previewHeight, previewWidth };
    previewMetadata.hasDngActiveArea = true;
    previewMetadata.dngMaskedAreas.clear();
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
    if (!ResolvePreviewVisibleSize(source.metadata, previewMaxDimension, previewWidth, previewHeight)) {
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
    preview.reconstructedCameraRgb=source.reconstructedCameraRgb;
    preview.metadata.linearChannels = sourceChannels;
    preview.rawBuffer.clear();
    preview.linearUInt16Buffer.clear();
    preview.linearFloatBuffer.clear();
    preview.outputCoverage.reset();

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
        AssignPreviewIdentity(
            source,
            previewWidth,
            previewHeight,
            "linear-nearest-v2",
            preview);
        return true;
    }

    if (source.linearFloatBuffer.size() < sourceSampleCount) {
        return false;
    }
    preview.linearFloatBuffer.assign(previewSampleCount, 0.0f);
    if(source.reconstructedCameraRgb) {
        std::shared_ptr<std::vector<float>> coverage;
        if(source.outputCoverage) {
            if(source.outputCoverage->size()!=sourcePixelCount)return false;
            coverage=std::make_shared<std::vector<float>>(std::size_t(previewWidth)*previewHeight);
            preview.outputCoverage=coverage;
        }
        // Average completed RGB measurements for the interactive overview.
        // Point sampling a high-resolution burst aliases fine detail and
        // presents an unrepresentative amount of noise.
        for(int y=0;y<previewHeight;++y)for(int x=0;x<previewWidth;++x) {
            const int x0=int(std::int64_t(x)*sourceWidth/previewWidth);
            const int x1=int(std::int64_t(x+1)*sourceWidth/previewWidth);
            const int y0=int(std::int64_t(y)*sourceHeight/previewHeight);
            const int y1=int(std::int64_t(y+1)*sourceHeight/previewHeight);
            std::array<double,4> sum{};
            double weight=0;
            for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx) {
                const auto p=std::size_t(sy)*sourceWidth+sx;const double a=source.outputCoverage?(*source.outputCoverage)[p]:1;
                weight+=a;for(int c=0;c<sourceChannels;++c)sum[c]+=source.linearFloatBuffer[p*sourceChannels+c]*a;
            }
            const double count=std::max(1e-9,weight);
            if(coverage)(*coverage)[std::size_t(y)*previewWidth+x]=float(weight/std::max(1,(x1-x0)*(y1-y0)));
            for(int c=0;c<sourceChannels;++c)preview.linearFloatBuffer[(std::size_t(y)*previewWidth+x)*sourceChannels+c]=float(sum[c]/count);
        }
        AssignPreviewIdentity(source,previewWidth,previewHeight,"reconstructed-coverage-area-v2",preview);
        return true;
    }
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
    AssignPreviewIdentity(
        source,
        previewWidth,
        previewHeight,
        "linear-nearest-v2",
        preview);
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

bool BuildPreviewRawData(
    const Raw::RawImageData& source,
    int previewMaxDimension,
    Raw::RawImageData& preview) {
    preview = Raw::RawImageData {};
    if (previewMaxDimension <= 0 || !HasPixels(source) || !source.metadata.error.empty()) {
        return false;
    }
    // Bayer data must remain at sensor resolution until after demosaic.
    // Reducing the CFA grid first destroys spatial/color detail and makes an
    // interaction-edge change trigger a source-wide CPU rebuild. Linear RAW
    // has already crossed the demosaic boundary and remains safe to proxy.
    if (source.metadata.pixelLayout != Raw::RawPixelLayout::LinearRgb) {
        return false;
    }
    return BuildLinearPreviewRawData(
        source, previewMaxDimension, preview);
}

std::string BuildCacheKey(
    const std::string& sourceCacheKey,
    const Raw::RawImageData& rawData,
    int previewMaxDimension) {
    const Raw::RawMetadata& metadata = rawData.metadata;
    std::string key = !rawData.contentIdentity.empty()
        ? rawData.contentIdentity
        : sourceCacheKey;
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
