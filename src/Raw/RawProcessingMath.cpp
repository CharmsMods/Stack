#include "RawProcessingMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace Raw::Processing {
namespace {

int PositiveModulo(int value, int modulus) {
    if (modulus <= 0) {
        return 0;
    }
    const int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

int PatternColor(CfaPattern pattern, int x, int y) {
    const int index = PositiveModulo(y, 2) * 2 + PositiveModulo(x, 2);
    switch (pattern) {
        case CfaPattern::RGGB: return std::array<int, 4>{ 0, 1, 1, 2 }[static_cast<std::size_t>(index)];
        case CfaPattern::BGGR: return std::array<int, 4>{ 2, 1, 1, 0 }[static_cast<std::size_t>(index)];
        case CfaPattern::GBRG: return std::array<int, 4>{ 1, 2, 0, 1 }[static_cast<std::size_t>(index)];
        case CfaPattern::GRBG: return std::array<int, 4>{ 1, 0, 2, 1 }[static_cast<std::size_t>(index)];
        case CfaPattern::Unknown:
        default:
            return 1;
    }
}

float SampleGainMap(
    const DngGainMapOpcode& map,
    int imageWidth,
    int imageHeight,
    int imageX,
    int imageY) {
    if (map.mapPointsH <= 0 || map.mapPointsV <= 0 || map.mapPlanes <= 0 ||
        !std::isfinite(map.mapSpacingH) || !std::isfinite(map.mapSpacingV) ||
        !std::isfinite(map.mapOriginH) || !std::isfinite(map.mapOriginV) ||
        map.mapSpacingH <= 0.0 || map.mapSpacingV <= 0.0 ||
        map.gains.empty()) {
        return 1.0f;
    }
    // The RAW engine uploads a single-plane CFA mosaic. Do not silently
    // apply an opcode whose declared plane range does not include that plane.
    if (map.plane > 0 || map.plane + map.planes <= 0) {
        return 1.0f;
    }
    if (imageY < map.top || imageY >= map.bottom ||
        imageX < map.left || imageX >= map.right ||
        ((imageY - map.top) % std::max(1, map.rowPitch)) != 0 ||
        ((imageX - map.left) % std::max(1, map.colPitch)) != 0) {
        return 1.0f;
    }

    const double normalizedX =
        (static_cast<double>(imageX) + 0.5) / static_cast<double>(std::max(1, imageWidth));
    const double normalizedY =
        (static_cast<double>(imageY) + 0.5) / static_cast<double>(std::max(1, imageHeight));
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
    const int plane = std::clamp(map.plane, 0, map.mapPlanes - 1);
    const auto at = [&](int row, int col) {
        const std::size_t index =
            (static_cast<std::size_t>(row) * static_cast<std::size_t>(map.mapPointsH) +
                static_cast<std::size_t>(col)) *
                static_cast<std::size_t>(map.mapPlanes) +
            static_cast<std::size_t>(plane);
        return index < map.gains.size() ? map.gains[index] : 1.0f;
    };
    const float a = at(y0, x0) * (1.0f - tx) + at(y0, x1) * tx;
    const float b = at(y1, x0) * (1.0f - tx) + at(y1, x1) * tx;
    return std::max(0.0f, a * (1.0f - ty) + b * ty);
}

int MirrorCfaCoordinate(int coordinate, int size) {
    if (coordinate >= 0 && coordinate < size) return coordinate;
    if (size <= 1) return 0;
    const int span = size - 1;
    const int phase = PositiveModulo(coordinate, 2 * span);
    return span - std::abs(phase - span);
}

float SampleCfaMirrored(const std::vector<float>& mosaic, int width, int height, int x, int y) {
    // Match the GPU's reflection without duplicating the boundary sample.
    // Even coordinate changes keep a requested red/green/blue sample in the
    // same Bayer phase, including kernels that extend past several corners.
    const int qx = MirrorCfaCoordinate(x, width);
    const int qy = MirrorCfaCoordinate(y, height);
    return mosaic[static_cast<std::size_t>(qy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(qx)];
}

float ApplyKernel(
    const std::vector<float>& mosaic,
    int width,
    int height,
    int x,
    int y,
    const std::array<float, 25>& kernel,
    bool transpose) {
    float sum = 0.0f;
    for (int ky = -2; ky <= 2; ++ky) {
        for (int kx = -2; kx <= 2; ++kx) {
            const int kernelX = transpose ? ky : kx;
            const int kernelY = transpose ? kx : ky;
            const std::size_t index =
                static_cast<std::size_t>(kernelY + 2) * 5u + static_cast<std::size_t>(kernelX + 2);
            sum += kernel[index] * SampleCfaMirrored(mosaic, width, height, x + kx, y + ky);
        }
    }
    return sum * 0.125f;
}

} // namespace

float SampleDngGainMap(
    const DngGainMapOpcode& map,
    int imageWidth,
    int imageHeight,
    int imageX,
    int imageY) {
    return SampleGainMap(
        map, imageWidth, imageHeight, imageX, imageY);
}

RawSensorRect ResolveActiveArea(const RawMetadata& metadata) {
    if (metadata.hasDngActiveArea &&
        metadata.dngActiveArea.right > metadata.dngActiveArea.left &&
        metadata.dngActiveArea.bottom > metadata.dngActiveArea.top) {
        RawSensorRect active = metadata.dngActiveArea;
        active.left = std::clamp(active.left, 0, std::max(0, metadata.rawWidth));
        active.top = std::clamp(active.top, 0, std::max(0, metadata.rawHeight));
        active.right = std::clamp(active.right, active.left, std::max(active.left, metadata.rawWidth));
        active.bottom = std::clamp(active.bottom, active.top, std::max(active.top, metadata.rawHeight));
        if (active.right > active.left && active.bottom > active.top) {
            return active;
        }
    }

    RawSensorRect active;
    active.left = std::max(0, metadata.leftMargin);
    active.top = std::max(0, metadata.topMargin);
    const int visibleWidth = metadata.visibleWidth > 0 ? metadata.visibleWidth : metadata.rawWidth - active.left;
    const int visibleHeight = metadata.visibleHeight > 0 ? metadata.visibleHeight : metadata.rawHeight - active.top;
    active.right = std::min(metadata.rawWidth, active.left + std::max(0, visibleWidth));
    active.bottom = std::min(metadata.rawHeight, active.top + std::max(0, visibleHeight));
    return active;
}

int CfaColorAt(const RawMetadata& metadata, const RawSensorRect& activeArea, int sensorX, int sensorY) {
    if (metadata.dngCfaRepeatPatternDim[0] == 2 && metadata.dngCfaRepeatPatternDim[1] == 2) {
        const int patternIndex =
            PositiveModulo(sensorY - activeArea.top, 2) * 2 +
            PositiveModulo(sensorX - activeArea.left, 2);
        const int plane = metadata.dngCfaPattern[static_cast<std::size_t>(patternIndex)];
        if (plane >= 0 && plane < static_cast<int>(metadata.dngCfaPlaneColor.size())) {
            const int color = metadata.dngCfaPlaneColor[static_cast<std::size_t>(plane)];
            if (color >= 0 && color < 3) {
                return color;
            }
        }
    }
    return PatternColor(metadata.cfaPattern, sensorX - activeArea.left, sensorY - activeArea.top);
}

float BlackLevelAt(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    const RawSensorRect& activeArea,
    int sensorX,
    int sensorY,
    int color) {
    if (settings.overrideBlackLevel) {
        return settings.blackLevelOverride;
    }

    float black = color >= 0 && color < 3 && metadata.perChannelBlack[static_cast<std::size_t>(color)] > 0.0f
        ? metadata.perChannelBlack[static_cast<std::size_t>(color)]
        : metadata.blackLevel;
    const int repeatRows = metadata.dngBlackLevelRepeatDim[0];
    const int repeatColumns = metadata.dngBlackLevelRepeatDim[1];
    if (repeatRows > 0 && repeatColumns > 0 && !metadata.dngBlackLevelValues.empty()) {
        const std::size_t index =
            static_cast<std::size_t>(PositiveModulo(sensorY - activeArea.top, repeatRows)) *
                static_cast<std::size_t>(repeatColumns) +
            static_cast<std::size_t>(PositiveModulo(sensorX - activeArea.left, repeatColumns));
        if (index < metadata.dngBlackLevelValues.size()) {
            black = metadata.dngBlackLevelValues[index];
        }
    }

    const int localX = sensorX - activeArea.left;
    const int localY = sensorY - activeArea.top;
    if (localX >= 0 && localX < static_cast<int>(metadata.dngBlackLevelDeltaH.size())) {
        black += metadata.dngBlackLevelDeltaH[static_cast<std::size_t>(localX)];
    }
    if (localY >= 0 && localY < static_cast<int>(metadata.dngBlackLevelDeltaV.size())) {
        black += metadata.dngBlackLevelDeltaV[static_cast<std::size_t>(localY)];
    }
    return black;
}

float WhiteLevelForColor(const RawMetadata& metadata, const RawDevelopSettings& settings, int color) {
    if (settings.overrideWhiteLevel) {
        return settings.whiteLevelOverride;
    }
    if (metadata.dngWhiteLevelValues.size() == 1) {
        return metadata.dngWhiteLevelValues.front();
    }
    if (color >= 0 && color < static_cast<int>(metadata.dngWhiteLevelValues.size())) {
        return metadata.dngWhiteLevelValues[static_cast<std::size_t>(color)];
    }
    return metadata.whiteLevel;
}

float LinearizeStoredSample(const RawMetadata& metadata, std::uint16_t storedSample) {
    if (metadata.dngLinearizationTable.empty()) {
        return static_cast<float>(storedSample);
    }
    const std::size_t index = std::min<std::size_t>(
        static_cast<std::size_t>(storedSample),
        metadata.dngLinearizationTable.size() - 1u);
    return static_cast<float>(metadata.dngLinearizationTable[index]);
}

float NormalizeStoredSample(
    const RawMetadata& metadata,
    const RawDevelopSettings& settings,
    const RawSensorRect& activeArea,
    int sensorX,
    int sensorY,
    std::uint16_t storedSample) {
    const int color = CfaColorAt(metadata, activeArea, sensorX, sensorY);
    const float black = BlackLevelAt(metadata, settings, activeArea, sensorX, sensorY, color);
    const float white = std::max(black + 1.0f, WhiteLevelForColor(metadata, settings, color));
    const float normalized = (LinearizeStoredSample(metadata, storedSample) - black) / (white - black);
    return std::min(normalized, 1.0f);
}

bool ResolveDngNoiseProfile(
    const RawMetadata& metadata,
    std::array<DngNoiseProfilePlane, 3>& colorPlanes) {
    colorPlanes = {};
    if (!metadata.hasDngNoiseProfile ||
        (metadata.dngNoiseProfile.size() != 1 &&
            metadata.dngNoiseProfile.size() < 3)) {
        return false;
    }

    const auto valid = [](const DngNoiseProfilePlane& plane) {
        return std::isfinite(plane.shotScale) &&
            std::isfinite(plane.readNoiseVariance) &&
            plane.shotScale > 0.0 &&
            plane.readNoiseVariance >= 0.0;
    };
    if (metadata.dngNoiseProfile.size() == 1) {
        if (!valid(metadata.dngNoiseProfile.front())) {
            return false;
        }
        colorPlanes.fill(metadata.dngNoiseProfile.front());
        return true;
    }

    std::array<bool, 3> found { false, false, false };
    const std::size_t planeCount = std::min(
        metadata.dngNoiseProfile.size(),
        metadata.dngCfaPlaneColor.size());
    for (std::size_t plane = 0; plane < planeCount; ++plane) {
        const int color = metadata.dngCfaPlaneColor[plane];
        if (color < 0 || color >= 3 || !valid(metadata.dngNoiseProfile[plane])) {
            continue;
        }
        colorPlanes[static_cast<std::size_t>(color)] =
            metadata.dngNoiseProfile[plane];
        found[static_cast<std::size_t>(color)] = true;
    }
    return std::all_of(found.begin(), found.end(), [](bool value) {
        return value;
    });
}

float DngNoiseVariance(
    const DngNoiseProfilePlane& profile,
    float normalizedSignal) {
    if (!std::isfinite(profile.shotScale) ||
        !std::isfinite(profile.readNoiseVariance) ||
        profile.shotScale <= 0.0 ||
        profile.readNoiseVariance < 0.0) {
        return 0.0f;
    }
    const double signal = std::clamp(
        static_cast<double>(normalizedSignal),
        0.0,
        1.0);
    return static_cast<float>(std::max(
        0.0,
        profile.shotScale * signal + profile.readNoiseVariance));
}

float NoiseAwareRangeWeight(
    float center,
    float neighbor,
    float centerVariance,
    float neighborVariance,
    float edgeProtection) {
    const float differenceSigma = std::sqrt(std::max(
        1.0e-12f,
        std::max(0.0f, centerVariance) +
            std::max(0.0f, neighborVariance)));
    // At low protection, samples separated by roughly three expected standard
    // deviations can still contribute. At maximum protection the support
    // tightens to one standard deviation.
    const float supportSigma =
        3.0f - 2.0f * std::clamp(edgeProtection, 0.0f, 1.0f);
    const float normalizedDifference =
        std::abs(neighbor - center) /
        std::max(1.0e-6f, differenceSigma * supportSigma);
    return std::exp(-0.5f * normalizedDifference * normalizedDifference);
}

bool BuildTruthfulNormalizedMosaic(
    const RawImageData& raw,
    const RawDevelopSettings& settings,
    std::vector<float>& normalized,
    std::string* error) {
    const RawMetadata& metadata = raw.metadata;
    const int width = metadata.rawWidth;
    const int height = metadata.rawHeight;
    const std::size_t expected =
        static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    if (width <= 0 || height <= 0 || raw.rawBuffer.size() < expected) {
        if (error) {
            *error = "Truthful RAW normalization requires a complete mosaic and valid dimensions.";
        }
        normalized.clear();
        return false;
    }

    normalized.resize(expected);
    const RawSensorRect active = ResolveActiveArea(metadata);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
            float value = NormalizeStoredSample(metadata, settings, active, x, y, raw.rawBuffer[index]);
            const int visibleX = x - active.left;
            const int visibleY = y - active.top;
            if (visibleX >= 0 && visibleY >= 0 &&
                x < active.right && y < active.bottom) {
                const int activeWidth = active.right - active.left;
                const int activeHeight = active.bottom - active.top;
                for (const DngGainMapOpcode& gainMap : metadata.dngGainMaps) {
                    // DNG requires clipping to the image's legal range after
                    // each OpcodeList2 operation, not only after the full list.
                    value = std::clamp(
                        value * SampleGainMap(
                            gainMap,
                            activeWidth,
                            activeHeight,
                            visibleX,
                            visibleY),
                        0.0f,
                        1.0f);
                }
            }
            normalized[index] = value;
        }
    }
    return true;
}

bool BuildTruthfulNoiseVarianceMosaic(
    const RawImageData& raw,
    const RawDevelopSettings& settings,
    std::vector<float>& noiseVariance,
    std::string* error) {
    const RawMetadata& metadata = raw.metadata;
    std::array<DngNoiseProfilePlane, 3> profiles {};
    if (!ResolveDngNoiseProfile(metadata, profiles)) {
        noiseVariance.clear();
        if (error) {
            *error = "DNG NoiseProfile is unavailable or invalid.";
        }
        return false;
    }

    const int width = metadata.rawWidth;
    const int height = metadata.rawHeight;
    const std::size_t expected =
        static_cast<std::size_t>(std::max(0, width)) *
        static_cast<std::size_t>(std::max(0, height));
    if (width <= 0 || height <= 0 || raw.rawBuffer.size() < expected) {
        noiseVariance.clear();
        if (error) {
            *error =
                "DNG noise variance requires a complete mosaic and valid dimensions.";
        }
        return false;
    }

    noiseVariance.resize(expected);
    const RawSensorRect active = ResolveActiveArea(metadata);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            const int color = CfaColorAt(metadata, active, x, y);
            float signal = NormalizeStoredSample(
                metadata,
                settings,
                active,
                x,
                y,
                raw.rawBuffer[index]);
            float variance = DngNoiseVariance(
                profiles[static_cast<std::size_t>(std::clamp(color, 0, 2))],
                signal);

            const int visibleX = x - active.left;
            const int visibleY = y - active.top;
            if (visibleX >= 0 && visibleY >= 0 &&
                x < active.right && y < active.bottom) {
                const int activeWidth = active.right - active.left;
                const int activeHeight = active.bottom - active.top;
                for (const DngGainMapOpcode& gainMap : metadata.dngGainMaps) {
                    const float gain = SampleGainMap(
                        gainMap,
                        activeWidth,
                        activeHeight,
                        visibleX,
                        visibleY);
                    signal = std::clamp(signal * gain, 0.0f, 1.0f);
                    variance = std::max(0.0f, variance * gain * gain);
                }
            }
            noiseVariance[index] = variance;
        }
    }
    return true;
}

bool ApplyWhiteBalanceToCfaMosaic(
    const RawMetadata& metadata,
    const std::array<float, 3>& multipliers,
    std::vector<float>& normalizedMosaic,
    std::string* error) {
    const int width = metadata.rawWidth;
    const int height = metadata.rawHeight;
    const std::size_t expected =
        static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height));
    if (width <= 0 || height <= 0 || normalizedMosaic.size() < expected) {
        if (error) {
            *error = "RAW-space white balance requires a complete normalized CFA mosaic.";
        }
        return false;
    }

    const RawSensorRect active = ResolveActiveArea(metadata);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int color = CfaColorAt(metadata, active, x, y);
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            normalizedMosaic[index] *= multipliers[
                static_cast<std::size_t>(std::clamp(color, 0, 2))];
        }
    }
    return true;
}

std::array<float, 3> DemosaicMalvarHeCutlerAt(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    int x,
    int y) {
    if (width <= 0 || height <= 0 ||
        mosaic.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return { 0.0f, 0.0f, 0.0f };
    }

    static constexpr std::array<float, 25> kGreenAtRedOrBlue = {
         0.0f,  0.0f, -1.0f,  0.0f,  0.0f,
         0.0f,  0.0f,  2.0f,  0.0f,  0.0f,
        -1.0f,  2.0f,  4.0f,  2.0f, -1.0f,
         0.0f,  0.0f,  2.0f,  0.0f,  0.0f,
         0.0f,  0.0f, -1.0f,  0.0f,  0.0f
    };
    static constexpr std::array<float, 25> kRedOrBlueAtGreenHorizontal = {
         0.0f,  0.0f,  0.5f,  0.0f,  0.0f,
         0.0f, -1.0f,  0.0f, -1.0f,  0.0f,
        -1.0f,  4.0f,  5.0f,  4.0f, -1.0f,
         0.0f, -1.0f,  0.0f, -1.0f,  0.0f,
         0.0f,  0.0f,  0.5f,  0.0f,  0.0f
    };
    static constexpr std::array<float, 25> kRedAtBlueOrBlueAtRed = {
         0.0f,  0.0f, -1.5f,  0.0f,  0.0f,
         0.0f,  2.0f,  0.0f,  2.0f,  0.0f,
        -1.5f,  0.0f,  6.0f,  0.0f, -1.5f,
         0.0f,  2.0f,  0.0f,  2.0f,  0.0f,
         0.0f,  0.0f, -1.5f,  0.0f,  0.0f
    };

    const int color = PatternColor(pattern, x, y);
    const float center = SampleCfaMirrored(mosaic, width, height, x, y);
    if (color == 0) {
        return {
            center,
            ApplyKernel(mosaic, width, height, x, y, kGreenAtRedOrBlue, false),
            ApplyKernel(mosaic, width, height, x, y, kRedAtBlueOrBlueAtRed, false)
        };
    }
    if (color == 2) {
        return {
            ApplyKernel(mosaic, width, height, x, y, kRedAtBlueOrBlueAtRed, false),
            ApplyKernel(mosaic, width, height, x, y, kGreenAtRedOrBlue, false),
            center
        };
    }

    const bool horizontalRed =
        PatternColor(pattern, x - 1, y) == 0 || PatternColor(pattern, x + 1, y) == 0;
    return {
        ApplyKernel(mosaic, width, height, x, y, kRedOrBlueAtGreenHorizontal, !horizontalRed),
        center,
        ApplyKernel(mosaic, width, height, x, y, kRedOrBlueAtGreenHorizontal, horizontalRed)
    };
}

std::array<float, 3> DemosaicNearestNeighborAt(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    int x,
    int y) {
    if (width <= 0 || height <= 0 ||
        mosaic.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return { 0.0f, 0.0f, 0.0f };
    }
    std::array<float, 3> result {};
    for (int wanted = 0; wanted < 3; ++wanted) {
        int bestDistance = 1000000;
        for (int radius = 0; radius <= 2; ++radius) {
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (std::abs(dx) != radius && std::abs(dy) != radius) continue;
                    if (PatternColor(pattern, x + dx, y + dy) != wanted) continue;
                    const int distance = std::abs(dx) + std::abs(dy);
                    if (distance < bestDistance) {
                        bestDistance = distance;
                        result[static_cast<std::size_t>(wanted)] =
                            SampleCfaMirrored(mosaic, width, height, x + dx, y + dy);
                    }
                }
            }
            if (bestDistance < 1000000) break;
        }
    }
    return result;
}

std::array<float, 3> DemosaicHamiltonAdamsAt(
    const std::vector<float>& mosaic,
    int width,
    int height,
    CfaPattern pattern,
    int x,
    int y) {
    if (width <= 0 || height <= 0 ||
        mosaic.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return { 0.0f, 0.0f, 0.0f };
    }
    const auto s = [&](int dx, int dy) { return SampleCfaMirrored(mosaic, width, height, x + dx, y + dy); };
    const int color = PatternColor(pattern, x, y);
    const float center = s(0, 0);
    std::array<float, 3> result { center, center, center };
    if (color == 0 || color == 2) {
        const float horizontal = 0.5f * (s(-1, 0) + s(1, 0));
        const float vertical = 0.5f * (s(0, -1) + s(0, 1));
        const float horizontalGradient = std::abs(s(-1, 0) - s(1, 0)) +
            std::abs(2.0f * center - s(-2, 0) - s(2, 0));
        const float verticalGradient = std::abs(s(0, -1) - s(0, 1)) +
            std::abs(2.0f * center - s(0, -2) - s(0, 2));
        result[1] = horizontalGradient <= verticalGradient ? horizontal : vertical;
        const float diagonal = 0.25f * (s(-1, -1) + s(1, -1) + s(-1, 1) + s(1, 1));
        result[static_cast<std::size_t>(color == 0 ? 2 : 0)] = diagonal;
        return result;
    }
    const bool horizontalRed = PatternColor(pattern, x - 1, y) == 0 || PatternColor(pattern, x + 1, y) == 0;
    result[0] = horizontalRed ? 0.5f * (s(-1, 0) + s(1, 0)) : 0.5f * (s(0, -1) + s(0, 1));
    result[2] = horizontalRed ? 0.5f * (s(0, -1) + s(0, 1)) : 0.5f * (s(-1, 0) + s(1, 0));
    return result;
}

float EncodeSrgb(float linearValue) {
    if (linearValue <= 0.0031308f) {
        return 12.92f * linearValue;
    }
    return 1.055f * std::pow(linearValue, 1.0f / 2.4f) - 0.055f;
}

std::array<float, 3> EncodeSrgb(const std::array<float, 3>& linearRgb) {
    return {
        EncodeSrgb(linearRgb[0]),
        EncodeSrgb(linearRgb[1]),
        EncodeSrgb(linearRgb[2])
    };
}

} // namespace Raw::Processing
