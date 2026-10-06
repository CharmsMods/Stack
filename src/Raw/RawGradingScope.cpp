#include "Raw/RawGradingScope.h"
#include "Utils/PixelBufferUtils.h"

#include <algorithm>
#include <cmath>

namespace {

float GradingScopeSrgbEncode(float value) {
    if (!std::isfinite(value)) {
        return 0.0f;
    }
    const float magnitude = std::abs(value);
    const float encoded = magnitude <= 0.0031308f
        ? magnitude * 12.92f
        : 1.055f * std::pow(magnitude, 1.0f / 2.4f) - 0.055f;
    return value < 0.0f ? -encoded : encoded;
}

std::array<float, 3> GradingScopeAnalysisRgb(
    const RawDevelopmentGradingScopeReadback& readback,
    float red,
    float green,
    float blue) {
    if (!std::isfinite(red)) red = 0.0f;
    if (!std::isfinite(green)) green = 0.0f;
    if (!std::isfinite(blue)) blue = 0.0f;

    if (readback.sceneLinear &&
        readback.workingSpace == Raw::RawWorkingSpace::LinearRec2020D65) {
        const float sourceRed = red;
        const float sourceGreen = green;
        const float sourceBlue = blue;
        red =
            1.6604910f * sourceRed -
            0.5876411f * sourceGreen -
            0.0728499f * sourceBlue;
        green =
            -0.1245505f * sourceRed +
             1.1328999f * sourceGreen -
             0.0083494f * sourceBlue;
        blue =
            -0.0181508f * sourceRed -
             0.1005789f * sourceGreen +
             1.1187297f * sourceBlue;
    }
    if (readback.sceneLinear && !readback.encodedSrgb) {
        red = GradingScopeSrgbEncode(red);
        green = GradingScopeSrgbEncode(green);
        blue = GradingScopeSrgbEncode(blue);
    }
    return {
        std::clamp(red, 0.0f, 1.0f),
        std::clamp(green, 0.0f, 1.0f),
        std::clamp(blue, 0.0f, 1.0f)
    };
}

std::array<float, 2> GradingScopeVectorCoordinate(float red, float green, float blue) {
    const float luma =
        0.2126f * red + 0.7152f * green + 0.0722f * blue;
    const float cb = (blue - luma) / 1.8556f;
    const float cr = (red - luma) / 1.5748f;
    return std::array<float, 2>{
        std::clamp(0.5f + cb * 0.94f, 0.0f, 1.0f),
        std::clamp(0.5f - cr * 0.94f, 0.0f, 1.0f)};
}

void NormalizeGradingScopeDensity(std::vector<float>& density) {
    float maximum = 0.0f;
    for (float value : density) {
        maximum = std::max(maximum, std::max(0.0f, value));
    }
    const float denominator = std::log1p(maximum);
    if (denominator <= 0.0f) {
        return;
    }
    for (float& value : density) {
        value = std::clamp(
            std::log1p(std::max(0.0f, value)) / denominator,
            0.0f,
            1.0f);
    }
}


} // namespace

namespace Raw {

std::shared_ptr<const RawGradingScopeVisualization> BuildGradingScopeVisualization(
    const RawDevelopmentGradingScopeReadback& readback) {
    std::size_t elementCount = 0;
    if (!readback.valid || !Stack::PixelBuffer::TryComputePixelElementCount(
            readback.width, readback.height, 3, elementCount) ||
        readback.pixels.size() != elementCount) {
        return {};
    }

    auto result = std::make_shared<RawGradingScopeVisualization>();
    struct DensityBuffers {
        std::array<float, RawGradingScopeVisualization::kHistogramBins>& histogram;
        std::vector<float> vectorscopeDensity;
        std::vector<std::array<float, 3>> vectorscopeRgb;
        std::array<std::vector<float>, 3> paradeDensity;
    } visualization { result->histogram };
    constexpr int vectorResolution =
        RawGradingScopeVisualization::kVectorscopeResolution;
    constexpr int paradeColumns =
        RawGradingScopeVisualization::kParadeColumns;
    constexpr int paradeRows =
        RawGradingScopeVisualization::kParadeRows;
    visualization.vectorscopeDensity.assign(
        static_cast<std::size_t>(vectorResolution * vectorResolution),
        0.0f);
    visualization.vectorscopeRgb.assign(
        static_cast<std::size_t>(vectorResolution * vectorResolution),
        std::array<float, 3> { 0.0f, 0.0f, 0.0f });
    for (std::vector<float>& channel : visualization.paradeDensity) {
        channel.assign(
            static_cast<std::size_t>(paradeColumns * paradeRows),
            0.0f);
    }

    for (int y = 0; y < readback.height; ++y) {
        for (int x = 0; x < readback.width; ++x) {
            const std::size_t pixelIndex =
                static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(readback.width) +
                static_cast<std::size_t>(x);
            if (!readback.coverage.empty() && !(readback.coverage[pixelIndex] > 0.0f)) continue;
            const std::size_t base = pixelIndex * 3u;
            const std::array<float, 3> rgb = GradingScopeAnalysisRgb(
                readback,
                readback.pixels[base],
                readback.pixels[base + 1],
                readback.pixels[base + 2]);
            const float luma = std::clamp(
                0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2],
                0.0f,
                1.0f);
            const int histogramBin = std::clamp(
                static_cast<int>(std::lround(luma * 255.0f)),
                0,
                255);
            visualization.histogram[
                static_cast<std::size_t>(histogramBin)] += 1.0f;

            const auto chroma = GradingScopeVectorCoordinate(
                rgb[0], rgb[1], rgb[2]);
            const int vectorX = std::clamp(
                static_cast<int>(chroma[0] * vectorResolution),
                0,
                vectorResolution - 1);
            const int vectorY = std::clamp(
                static_cast<int>(chroma[1] * vectorResolution),
                0,
                vectorResolution - 1);
            const std::size_t vectorIndex = static_cast<std::size_t>(
                vectorY * vectorResolution + vectorX);
            visualization.vectorscopeDensity[vectorIndex] += 1.0f;
            std::array<float, 3>& vectorRgb =
                visualization.vectorscopeRgb[vectorIndex];
            for (int channel = 0; channel < 3; ++channel) {
                vectorRgb[static_cast<std::size_t>(channel)] +=
                    rgb[static_cast<std::size_t>(channel)];
            }

            const int paradeX = std::clamp(
                x * paradeColumns / std::max(1, readback.width),
                0,
                paradeColumns - 1);
            for (int channel = 0; channel < 3; ++channel) {
                const int paradeY = std::clamp(
                    static_cast<int>(std::lround(
                        (1.0f - rgb[static_cast<std::size_t>(channel)]) *
                        static_cast<float>(paradeRows - 1))),
                    0,
                    paradeRows - 1);
                visualization.paradeDensity[
                    static_cast<std::size_t>(channel)][
                        static_cast<std::size_t>(
                            paradeY * paradeColumns + paradeX)] += 1.0f;
            }
        }
    }

    float histogramMaximum = 0.0f;
    for (float value : visualization.histogram) {
        histogramMaximum = std::max(histogramMaximum, value);
    }
    const float histogramDenominator = std::log1p(histogramMaximum);
    if (histogramDenominator > 0.0f) {
        for (float& value : visualization.histogram) {
            value = std::clamp(
                std::log1p(std::max(0.0f, value)) /
                    histogramDenominator,
                0.0f,
                1.0f);
        }
    }
    for (std::size_t index = 0;
         index < visualization.vectorscopeDensity.size();
         ++index) {
        const float count = visualization.vectorscopeDensity[index];
        if (count <= 0.0f) {
            continue;
        }
        std::array<float, 3>& color = visualization.vectorscopeRgb[index];
        for (float& component : color) {
            component = std::clamp(component / count, 0.0f, 1.0f);
        }
        // A vectorscope conventionally communicates chroma with its trace
        // color. Normalize the retained image RGB by value so dark saturated
        // pixels stay visible, while neutral samples remain neutral.
        const float maximum = std::max({ color[0], color[1], color[2] });
        if (maximum > 0.001f) {
            for (float& component : color) {
                component /= maximum;
            }
        } else {
            color = { 0.92f, 0.94f, 0.96f };
        }
    }
    NormalizeGradingScopeDensity(visualization.vectorscopeDensity);
    float paradeMaximum = 0.0f;
    for (const std::vector<float>& channel : visualization.paradeDensity) {
        for (float value : channel) {
            paradeMaximum = std::max(paradeMaximum, value);
        }
    }
    const float paradeDenominator = std::log1p(paradeMaximum);
    if (paradeDenominator > 0.0f) {
        for (std::vector<float>& channel : visualization.paradeDensity) {
            for (float& value : channel) {
                value = std::clamp(
                    std::log1p(std::max(0.0f, value)) /
                        paradeDenominator,
                    0.0f,
                    1.0f);
            }
        }
    }
    for (int y = 0; y < vectorResolution; ++y) {
        for (int x = 0; x < vectorResolution; ++x) {
            const auto index = static_cast<std::size_t>(y * vectorResolution + x);
            const float density = visualization.vectorscopeDensity[index];
            const float px = (static_cast<float>(x) + 0.5f) / vectorResolution;
            const float py = (static_cast<float>(y) + 0.5f) / vectorResolution;
            const float dx = px * 2.0f - 1.0f;
            const float dy = py * 2.0f - 1.0f;
            if (density <= 0.0f || dx * dx + dy * dy > 1.0f) continue;
            const auto& color = visualization.vectorscopeRgb[index];
            result->vectorscopePoints.push_back({ px, py,
                { color[0], color[1], color[2], 0.06f + density * 0.76f } });
        }
    }
    constexpr std::array<std::array<float, 3>, 3> paradeColors {{
        { 1.0f, 0.20f, 0.16f }, { 0.20f, 1.0f, 0.34f }, { 0.20f, 0.42f, 1.0f }
    }};
    for (int channel = 0; channel < 3; ++channel) {
        const auto& color = paradeColors[channel];
        for (int y = 0; y < paradeRows; ++y) {
            for (int x = 0; x < paradeColumns; ++x) {
                const float density = visualization.paradeDensity[channel][
                    static_cast<std::size_t>(y * paradeColumns + x)];
                if (density <= 0.0f) continue;
                result->paradePoints.push_back({
                    (channel + (static_cast<float>(x) + 0.5f) / paradeColumns) / 3.0f,
                    (static_cast<float>(y) + 0.5f) / paradeRows,
                    { color[0], color[1], color[2], 0.06f + density * 0.68f } });
            }
        }
    }
    result->source = readback.source;
    result->sourceKey = readback.sourceKey;
    result->generation = readback.generation;
    return result;
}

} // namespace Raw
