#include "RawGalleryThumbnailPixels.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Stack::RawWorkspace {
namespace {
float NormalizeRawValue(float value, const Raw::RawMetadata& metadata) {
    const float black = metadata.blackLevel;
    const float white = std::max(black + 1.0f, metadata.whiteLevel);
    return std::clamp((value - black) / (white - black), 0.0f, 1.0f);
}

const std::vector<float>* NormalizedMosaic(const Raw::RawImageData& raw) {
    return raw.normalizedMosaicInputContract != Raw::NormalizedMosaicInputContract::None
        ? raw.normalizedMosaicBuffer.get() : nullptr;
}

float MosaicValue(const Raw::RawImageData& raw, const std::vector<float>* normalized,
    std::size_t index) {
    // Floating-point DNGs already contain normalized sensor measurements.
    // Keep their signed/highlight range through thumbnail averaging; apply
    // the same display clamp and white balance as integer captures afterward.
    if (normalized) {
        const float value = (*normalized)[index];
        return std::isfinite(value) ? value : 0.0f;
    }
    return NormalizeRawValue(static_cast<float>(raw.rawBuffer[index]), raw.metadata);
}

unsigned char ToSrgbByte(float linear) {
    const float encoded = std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
    return static_cast<unsigned char>(std::clamp(encoded * 255.0f + 0.5f, 0.0f, 255.0f));
}

int CfaColorAt(const Raw::RawMetadata& metadata, int x, int y) {
    switch (metadata.cfaPattern) {
        case Raw::CfaPattern::RGGB:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 0 : 1) : ((x & 1) == 0 ? 1 : 2);
        case Raw::CfaPattern::BGGR:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 2 : 1) : ((x & 1) == 0 ? 1 : 0);
        case Raw::CfaPattern::GBRG:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 1 : 2) : ((x & 1) == 0 ? 0 : 1);
        case Raw::CfaPattern::GRBG:
            return (y & 1) == 0 ? ((x & 1) == 0 ? 1 : 0) : ((x & 1) == 0 ? 2 : 1);
        case Raw::CfaPattern::Unknown:
        default:
            return -1;
    }
}

bool BuildFastMosaicThumbnailPixels(const Raw::RawImageData& raw, int maxDimension, std::vector<unsigned char>& outPixels, int& outW, int& outH, const std::function<bool()>& shouldCancel) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int visibleW = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    const int visibleH = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    const int rawW = metadata.rawWidth;
    const int rawH = metadata.rawHeight;
    const auto* normalized = NormalizedMosaic(raw);
    if (rawW <= 0 || rawH <= 0 || visibleW <= 0 || visibleH <= 0 ||
        (normalized ? normalized->size() : raw.rawBuffer.size()) <
            static_cast<std::size_t>(rawW) * static_cast<std::size_t>(rawH)) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) / static_cast<float>(std::max(visibleW, visibleH));
    outW = std::max(1, static_cast<int>(std::floor(static_cast<float>(visibleW) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(static_cast<float>(visibleH) * std::min(1.0f, scale))));
    outPixels.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u, 255);

    const int left = std::clamp(metadata.leftMargin, 0, std::max(0, rawW - 1));
    const int top = std::clamp(metadata.topMargin, 0, std::max(0, rawH - 1));
    const float wbR = metadata.cameraWhiteBalance[0] > 0.001f ? metadata.cameraWhiteBalance[0] : 1.0f;
    const float wbG = metadata.cameraWhiteBalance[1] > 0.001f ? metadata.cameraWhiteBalance[1] : 1.0f;
    const float wbB = metadata.cameraWhiteBalance[2] > 0.001f ? metadata.cameraWhiteBalance[2] : 1.0f;
    const float wbScale = 1.0f / std::max(0.001f, wbG);

    for (int y = 0; y < outH; ++y) {
        if (shouldCancel && shouldCancel()) return false;
        for (int x = 0; x < outW; ++x) {
            const int centerX = left + std::clamp(static_cast<int>((static_cast<float>(x) + 0.5f) * static_cast<float>(visibleW) / static_cast<float>(outW)), 0, visibleW - 1);
            const int centerY = top + std::clamp(static_cast<int>((static_cast<float>(y) + 0.5f) * static_cast<float>(visibleH) / static_cast<float>(outH)), 0, visibleH - 1);
            const int baseX = std::clamp(centerX & ~1, 0, std::max(0, rawW - 2));
            const int baseY = std::clamp(centerY & ~1, 0, std::max(0, rawH - 2));

            float channels[3] = { 0.0f, 0.0f, 0.0f };
            int counts[3] = { 0, 0, 0 };
            for (int oy = 0; oy < 2; ++oy) {
                for (int ox = 0; ox < 2; ++ox) {
                    const int sx = std::clamp(baseX + ox, 0, rawW - 1);
                    const int sy = std::clamp(baseY + oy, 0, rawH - 1);
                    const int color = CfaColorAt(metadata, sx, sy);
                    const float value = MosaicValue(raw, normalized,
                        static_cast<std::size_t>(sy) * rawW + sx);
                    if (color >= 0 && color < 3) {
                        channels[color] += value;
                        ++counts[color];
                    } else {
                        channels[0] += value;
                        channels[1] += value;
                        channels[2] += value;
                        ++counts[0];
                        ++counts[1];
                        ++counts[2];
                    }
                }
            }

            float r = counts[0] > 0 ? channels[0] / static_cast<float>(counts[0]) : channels[1];
            float g = counts[1] > 0 ? channels[1] / static_cast<float>(counts[1]) : (r + channels[2]) * 0.5f;
            float b = counts[2] > 0 ? channels[2] / static_cast<float>(counts[2]) : g;
            r = std::clamp(r * wbR * wbScale, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b * wbB * wbScale, 0.0f, 1.0f);

            const std::size_t dst = (static_cast<std::size_t>(y) * outW + static_cast<std::size_t>(x)) * 4u;
            outPixels[dst + 0] = ToSrgbByte(r);
            outPixels[dst + 1] = ToSrgbByte(g);
            outPixels[dst + 2] = ToSrgbByte(b);
            outPixels[dst + 3] = 255;
        }
    }
    return true;
}

bool BuildFastLinearThumbnailPixels(const Raw::RawImageData& raw, int maxDimension, std::vector<unsigned char>& outPixels, int& outW, int& outH, const std::function<bool()>& shouldCancel) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int srcW = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth;
    const int srcH = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight;
    const int channels = std::clamp(metadata.linearChannels > 0 ? metadata.linearChannels : 3, 3, 4);
    if (srcW <= 0 || srcH <= 0) {
        return false;
    }

    const bool useUInt16 = raw.linearUInt16Buffer.size() >= static_cast<std::size_t>(srcW) * srcH * channels;
    const bool useFloat = raw.linearFloatBuffer.size() >= static_cast<std::size_t>(srcW) * srcH * channels;
    if (!useUInt16 && !useFloat) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) / static_cast<float>(std::max(srcW, srcH));
    outW = std::max(1, static_cast<int>(std::floor(static_cast<float>(srcW) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(static_cast<float>(srcH) * std::min(1.0f, scale))));
    outPixels.assign(static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u, 255);

    for (int y = 0; y < outH; ++y) {
        if (shouldCancel && shouldCancel()) return false;
        for (int x = 0; x < outW; ++x) {
            const int sx = std::clamp(static_cast<int>((static_cast<float>(x) + 0.5f) * static_cast<float>(srcW) / static_cast<float>(outW)), 0, srcW - 1);
            const int sy = std::clamp(static_cast<int>((static_cast<float>(y) + 0.5f) * static_cast<float>(srcH) / static_cast<float>(outH)), 0, srcH - 1);
            const std::size_t src = (static_cast<std::size_t>(sy) * srcW + static_cast<std::size_t>(sx)) * static_cast<std::size_t>(channels);
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            if (useUInt16) {
                r = static_cast<float>(raw.linearUInt16Buffer[src + 0]) / 65535.0f;
                g = static_cast<float>(raw.linearUInt16Buffer[src + 1]) / 65535.0f;
                b = static_cast<float>(raw.linearUInt16Buffer[src + 2]) / 65535.0f;
            } else {
                r = raw.linearFloatBuffer[src + 0];
                g = raw.linearFloatBuffer[src + 1];
                b = raw.linearFloatBuffer[src + 2];
            }

            const std::size_t dst = (static_cast<std::size_t>(y) * outW + static_cast<std::size_t>(x)) * 4u;
            outPixels[dst + 0] = ToSrgbByte(r);
            outPixels[dst + 1] = ToSrgbByte(g);
            outPixels[dst + 2] = ToSrgbByte(b);
            outPixels[dst + 3] = 255;
        }
    }
    return true;
}

bool BuildHighQualityMosaicThumbnailPixels(
    const Raw::RawImageData& raw,
    int maxDimension,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    const std::function<bool()>& shouldCancel) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int visibleW = metadata.visibleWidth > 0
        ? metadata.visibleWidth
        : metadata.rawWidth;
    const int visibleH = metadata.visibleHeight > 0
        ? metadata.visibleHeight
        : metadata.rawHeight;
    const int rawW = metadata.rawWidth;
    const int rawH = metadata.rawHeight;
    const auto* normalized = NormalizedMosaic(raw);
    if (rawW <= 0 || rawH <= 0 || visibleW <= 0 || visibleH <= 0 ||
        (normalized ? normalized->size() : raw.rawBuffer.size()) <
            static_cast<std::size_t>(rawW) * static_cast<std::size_t>(rawH)) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) /
        static_cast<float>(std::max(visibleW, visibleH));
    outW = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(visibleW) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(visibleH) * std::min(1.0f, scale))));
    outPixels.assign(
        static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u,
        255);

    const int left = std::clamp(metadata.leftMargin, 0, std::max(0, rawW - 1));
    const int top = std::clamp(metadata.topMargin, 0, std::max(0, rawH - 1));
    const int visibleRight = std::min(rawW, left + visibleW);
    const int visibleBottom = std::min(rawH, top + visibleH);
    const float wbR = metadata.cameraWhiteBalance[0] > 0.001f
        ? metadata.cameraWhiteBalance[0]
        : 1.0f;
    const float wbG = metadata.cameraWhiteBalance[1] > 0.001f
        ? metadata.cameraWhiteBalance[1]
        : 1.0f;
    const float wbB = metadata.cameraWhiteBalance[2] > 0.001f
        ? metadata.cameraWhiteBalance[2]
        : 1.0f;
    const float wbScale = 1.0f / std::max(0.001f, wbG);

    for (int y = 0; y < outH; ++y) {
        if (shouldCancel && shouldCancel()) return false;
        int sourceY0 = top + static_cast<int>(
            (static_cast<std::int64_t>(y) * visibleH) / outH);
        int sourceY1 = top + static_cast<int>(
            (static_cast<std::int64_t>(y + 1) * visibleH + outH - 1) / outH);
        sourceY0 = std::clamp(sourceY0, top, std::max(top, visibleBottom - 1));
        sourceY1 = std::clamp(sourceY1, sourceY0 + 1, visibleBottom);
        if (sourceY1 - sourceY0 < 2 && visibleBottom - top >= 2) {
            sourceY0 = std::clamp(sourceY0 - 1, top, visibleBottom - 2);
            sourceY1 = sourceY0 + 2;
        }
        for (int x = 0; x < outW; ++x) {
            int sourceX0 = left + static_cast<int>(
                (static_cast<std::int64_t>(x) * visibleW) / outW);
            int sourceX1 = left + static_cast<int>(
                (static_cast<std::int64_t>(x + 1) * visibleW + outW - 1) / outW);
            sourceX0 = std::clamp(sourceX0, left, std::max(left, visibleRight - 1));
            sourceX1 = std::clamp(sourceX1, sourceX0 + 1, visibleRight);
            if (sourceX1 - sourceX0 < 2 && visibleRight - left >= 2) {
                sourceX0 = std::clamp(sourceX0 - 1, left, visibleRight - 2);
                sourceX1 = sourceX0 + 2;
            }

            double channels[3] = { 0.0, 0.0, 0.0 };
            int counts[3] = { 0, 0, 0 };
            for (int sourceY = sourceY0; sourceY < sourceY1; ++sourceY) {
                for (int sourceX = sourceX0; sourceX < sourceX1; ++sourceX) {
                    const int color = CfaColorAt(metadata, sourceX, sourceY);
                    const float value = MosaicValue(raw, normalized,
                        static_cast<std::size_t>(sourceY) * rawW + sourceX);
                    if (color >= 0 && color < 3) {
                        channels[color] += value;
                        ++counts[color];
                    } else {
                        for (int channel = 0; channel < 3; ++channel) {
                            channels[channel] += value;
                            ++counts[channel];
                        }
                    }
                }
            }

            const float fallback = counts[1] > 0
                ? static_cast<float>(channels[1] / counts[1])
                : 0.0f;
            float r = counts[0] > 0
                ? static_cast<float>(channels[0] / counts[0])
                : fallback;
            float g = counts[1] > 0
                ? static_cast<float>(channels[1] / counts[1])
                : fallback;
            float b = counts[2] > 0
                ? static_cast<float>(channels[2] / counts[2])
                : fallback;
            r = std::clamp(r * wbR * wbScale, 0.0f, 1.0f);
            g = std::clamp(g, 0.0f, 1.0f);
            b = std::clamp(b * wbB * wbScale, 0.0f, 1.0f);

            const std::size_t destination =
                (static_cast<std::size_t>(y) * outW + x) * 4u;
            outPixels[destination + 0] = ToSrgbByte(r);
            outPixels[destination + 1] = ToSrgbByte(g);
            outPixels[destination + 2] = ToSrgbByte(b);
            outPixels[destination + 3] = 255;
        }
    }
    return true;
}

bool BuildHighQualityLinearThumbnailPixels(
    const Raw::RawImageData& raw,
    int maxDimension,
    std::vector<unsigned char>& outPixels,
    int& outW,
    int& outH,
    const std::function<bool()>& shouldCancel) {
    const Raw::RawMetadata& metadata = raw.metadata;
    const int sourceWidth = metadata.visibleWidth > 0
        ? metadata.visibleWidth
        : metadata.rawWidth;
    const int sourceHeight = metadata.visibleHeight > 0
        ? metadata.visibleHeight
        : metadata.rawHeight;
    const int channels = std::clamp(
        metadata.linearChannels > 0 ? metadata.linearChannels : 3,
        3,
        4);
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return false;
    }
    const bool useUInt16 = raw.linearUInt16Buffer.size() >=
        static_cast<std::size_t>(sourceWidth) * sourceHeight * channels;
    const bool useFloat = raw.linearFloatBuffer.size() >=
        static_cast<std::size_t>(sourceWidth) * sourceHeight * channels;
    if (!useUInt16 && !useFloat) {
        return false;
    }

    const float scale = static_cast<float>(maxDimension) /
        static_cast<float>(std::max(sourceWidth, sourceHeight));
    outW = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(sourceWidth) * std::min(1.0f, scale))));
    outH = std::max(1, static_cast<int>(std::floor(
        static_cast<float>(sourceHeight) * std::min(1.0f, scale))));
    outPixels.assign(
        static_cast<std::size_t>(outW) * static_cast<std::size_t>(outH) * 4u,
        255);

    for (int y = 0; y < outH; ++y) {
        if (shouldCancel && shouldCancel()) return false;
        const int sourceY0 = static_cast<int>(
            (static_cast<std::int64_t>(y) * sourceHeight) / outH);
        const int sourceY1 = std::max(
            sourceY0 + 1,
            static_cast<int>(
                (static_cast<std::int64_t>(y + 1) * sourceHeight) / outH));
        for (int x = 0; x < outW; ++x) {
            const int sourceX0 = static_cast<int>(
                (static_cast<std::int64_t>(x) * sourceWidth) / outW);
            const int sourceX1 = std::max(
                sourceX0 + 1,
                static_cast<int>(
                    (static_cast<std::int64_t>(x + 1) * sourceWidth) / outW));
            double red = 0.0;
            double green = 0.0;
            double blue = 0.0;
            int sampleCount = 0;
            for (int sourceY = sourceY0;
                 sourceY < std::min(sourceY1, sourceHeight);
                 ++sourceY) {
                for (int sourceX = sourceX0;
                     sourceX < std::min(sourceX1, sourceWidth);
                     ++sourceX) {
                    const std::size_t source =
                        (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) *
                        static_cast<std::size_t>(channels);
                    if (useUInt16) {
                        red += static_cast<double>(raw.linearUInt16Buffer[source + 0]) /
                            65535.0;
                        green += static_cast<double>(raw.linearUInt16Buffer[source + 1]) /
                            65535.0;
                        blue += static_cast<double>(raw.linearUInt16Buffer[source + 2]) /
                            65535.0;
                    } else {
                        red += raw.linearFloatBuffer[source + 0];
                        green += raw.linearFloatBuffer[source + 1];
                        blue += raw.linearFloatBuffer[source + 2];
                    }
                    ++sampleCount;
                }
            }
            const double inverseSamples = 1.0 /
                static_cast<double>(std::max(1, sampleCount));
            const std::size_t destination =
                (static_cast<std::size_t>(y) * outW + x) * 4u;
            outPixels[destination + 0] = ToSrgbByte(
                static_cast<float>(red * inverseSamples));
            outPixels[destination + 1] = ToSrgbByte(
                static_cast<float>(green * inverseSamples));
            outPixels[destination + 2] = ToSrgbByte(
                static_cast<float>(blue * inverseSamples));
            outPixels[destination + 3] = 255;
        }
    }
    return true;
}

} // namespace

bool BuildRawGalleryThumbnailPixels(const Raw::RawImageData& raw, int maxDimension,
    std::vector<unsigned char>& pixels, int& width, int& height,
    bool fastPreview, const std::function<bool()>& shouldCancel) {
    if (maxDimension <= 0 || (shouldCancel && shouldCancel())) return false;
    if (raw.metadata.pixelLayout == Raw::RawPixelLayout::LinearRgb) {
        return fastPreview
            ? BuildFastLinearThumbnailPixels(raw, maxDimension, pixels, width, height, shouldCancel)
            : BuildHighQualityLinearThumbnailPixels(raw, maxDimension, pixels, width, height, shouldCancel);
    }
    return fastPreview
        ? BuildFastMosaicThumbnailPixels(raw, maxDimension, pixels, width, height, shouldCancel)
        : BuildHighQualityMosaicThumbnailPixels(raw, maxDimension, pixels, width, height, shouldCancel);
}
} // namespace Stack::RawWorkspace
