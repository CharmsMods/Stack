#include "Raw/RawRestormerAdapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Stack::RawRestormer {
namespace {

using Vec3 = std::array<float, 3>;

float FiniteOr(float value, float fallback = 0.0f) {
    return std::isfinite(value) ? value : fallback;
}

float SrgbEncode(float linear) {
    linear = std::max(0.0f, FiniteOr(linear));
    if (linear <= 0.0031308f) {
        return 12.92f * linear;
    }
    return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

float SrgbDecode(float encoded) {
    encoded = std::clamp(FiniteOr(encoded), 0.0f, 1.0f);
    if (encoded <= 0.04045f) {
        return encoded / 12.92f;
    }
    return std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

Vec3 WorkingToLinearSrgb(const Vec3& value, Raw::RawWorkingSpace workingSpace) {
    if (workingSpace == Raw::RawWorkingSpace::LinearSrgbD65) {
        return value;
    }
    return {
        1.6604910f * value[0] - 0.5876411f * value[1] - 0.0728499f * value[2],
        -0.1245505f * value[0] + 1.1328999f * value[1] - 0.0083494f * value[2],
        -0.0181508f * value[0] - 0.1005789f * value[1] + 1.1187297f * value[2]
    };
}

Vec3 LinearSrgbToWorking(const Vec3& value, Raw::RawWorkingSpace workingSpace) {
    if (workingSpace == Raw::RawWorkingSpace::LinearSrgbD65) {
        return value;
    }
    return {
        0.6274039f * value[0] + 0.3292830f * value[1] + 0.0433131f * value[2],
        0.0690973f * value[0] + 0.9195404f * value[1] + 0.0113623f * value[2],
        0.0163914f * value[0] + 0.0880133f * value[1] + 0.8955953f * value[2]
    };
}

Vec3 CompressNegativeProxy(const Vec3& value) {
    const float luminance =
        0.2126f * value[0] + 0.7152f * value[1] + 0.0722f * value[2];
    const float neutral = std::max(0.0f, FiniteOr(luminance));
    const float minimum = std::min({ value[0], value[1], value[2] });
    float mixAmount = 0.0f;
    if (minimum < 0.0f) {
        const float denominator = neutral - minimum;
        mixAmount = denominator > 1.0e-8f
            ? std::clamp(-minimum / denominator, 0.0f, 1.0f)
            : 1.0f;
    }
    return {
        std::max(0.0f, value[0] + (neutral - value[0]) * mixAmount),
        std::max(0.0f, value[1] + (neutral - value[1]) * mixAmount),
        std::max(0.0f, value[2] + (neutral - value[2]) * mixAmount)
    };
}

Vec3 ProxyEncode(
    const Vec3& working,
    Raw::RawWorkingSpace workingSpace,
    float inputExposureGain) {
    Vec3 linearSrgb = WorkingToLinearSrgb(working, workingSpace);
    for (float& component : linearSrgb) {
        component *= inputExposureGain;
    }
    linearSrgb = CompressNegativeProxy(linearSrgb);
    Vec3 result {};
    for (int channel = 0; channel < 3; ++channel) {
        const float compressed =
            linearSrgb[channel] / (1.0f + linearSrgb[channel]);
        result[channel] = std::clamp(SrgbEncode(compressed), 0.0f, 1.0f);
    }
    return result;
}

Vec3 ProxyDecode(const float* encoded, float inputExposureGain) {
    Vec3 result {};
    for (int channel = 0; channel < 3; ++channel) {
        const float compressed = std::min(SrgbDecode(encoded[channel]), 0.999999f);
        result[channel] =
            (compressed / std::max(1.0f - compressed, 1.0e-6f)) /
            std::max(inputExposureGain, 1.0e-6f);
    }
    return result;
}

float ComputeInputExposureGain(
    const std::vector<float>& source,
    int width,
    int height,
    Raw::RawWorkingSpace workingSpace) {
    constexpr std::size_t kMaximumSamples = 65536U;
    constexpr float kTargetPercentileLuma = 0.18f;
    constexpr float kMinimumGain = 0.25f;
    constexpr float kMaximumGain = 32.0f;

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t stride =
        std::max<std::size_t>(1U, pixelCount / kMaximumSamples);
    std::vector<float> luminances;
    luminances.reserve(std::min(pixelCount, kMaximumSamples + 1U));
    for (std::size_t pixel = 0; pixel < pixelCount; pixel += stride) {
        const std::size_t index = pixel * 4U;
        const Vec3 working {
            FiniteOr(source[index + 0U]),
            FiniteOr(source[index + 1U]),
            FiniteOr(source[index + 2U])
        };
        const Vec3 linearSrgb = WorkingToLinearSrgb(working, workingSpace);
        const float luma =
            0.2126f * linearSrgb[0] +
            0.7152f * linearSrgb[1] +
            0.0722f * linearSrgb[2];
        if (std::isfinite(luma) && luma > 1.0e-6f) {
            luminances.push_back(luma);
        }
    }
    if (luminances.empty()) {
        return 1.0f;
    }

    // A slightly-above-median sample is stable on night photographs while
    // avoiding the behavior of exposing a small lamp or specular highlight as
    // the model's midtone. This gain exists only in the model proxy; it is
    // divided back out when the predicted residual returns to scene-linear RGB.
    const std::size_t percentileIndex = std::min(
        luminances.size() - 1U,
        static_cast<std::size_t>(
            0.60 * static_cast<double>(luminances.size() - 1U)));
    std::nth_element(
        luminances.begin(),
        luminances.begin() + percentileIndex,
        luminances.end());
    const float percentileLuma = luminances[percentileIndex];
    return std::clamp(
        kTargetPercentileLuma / std::max(percentileLuma, 1.0e-6f),
        kMinimumGain,
        kMaximumGain);
}

Vec3 ToOpponent(const Vec3& rgb) {
    const float y = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
    return { y, rgb[2] - y, rgb[0] - y };
}

Vec3 FromOpponent(const Vec3& opponent) {
    const float y = opponent[0];
    const float b = y + opponent[1];
    const float r = y + opponent[2];
    const float g =
        (y - 0.2126f * r - 0.0722f * b) / 0.7152f;
    return { r, g, b };
}

float Median(std::vector<float>& values) {
    if (values.empty()) {
        return 0.0f;
    }
    const std::size_t middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + middle, values.end());
    float result = values[middle];
    if ((values.size() & 1U) == 0U) {
        const float lower = *std::max_element(values.begin(), values.begin() + middle);
        result = 0.5f * (lower + result);
    }
    return result;
}

float Median9(std::array<float, 9> values) {
    std::nth_element(values.begin(), values.begin() + 4, values.end());
    return values[4];
}

float SmoothStep(float edge0, float edge1, float value) {
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float SceneLuma(
    const std::vector<float>& source,
    int width,
    int height,
    int x,
    int y,
    Raw::RawWorkingSpace workingSpace) {
    x = std::clamp(x, 0, width - 1);
    y = std::clamp(y, 0, height - 1);
    const std::size_t index =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) * 4U;
    const Vec3 rgb {
        source[index + 0],
        source[index + 1],
        source[index + 2]
    };
    const Vec3 linearSrgb = WorkingToLinearSrgb(rgb, workingSpace);
    return std::max(
        1.0e-6f,
        0.2126f * linearSrgb[0] +
        0.7152f * linearSrgb[1] +
        0.0722f * linearSrgb[2]);
}

float DetailAttenuation(
    const std::vector<float>& source,
    int width,
    int height,
    int x,
    int y,
    Raw::RawWorkingSpace workingSpace,
    float detailProtection) {
    const float center = std::log2(SceneLuma(
        source, width, height, x, y, workingSpace));
    float edge = 0.0f;
    edge = std::max(edge, std::abs(std::log2(SceneLuma(
        source, width, height, x - 1, y, workingSpace)) - center));
    edge = std::max(edge, std::abs(std::log2(SceneLuma(
        source, width, height, x + 1, y, workingSpace)) - center));
    edge = std::max(edge, std::abs(std::log2(SceneLuma(
        source, width, height, x, y - 1, workingSpace)) - center));
    edge = std::max(edge, std::abs(std::log2(SceneLuma(
        source, width, height, x, y + 1, workingSpace)) - center));
    const float structure = SmoothStep(0.025f, 0.30f, edge);
    return 1.0f - std::clamp(detailProtection, 0.0f, 1.0f) *
        structure * 0.92f;
}

bool DimensionsAreValid(
    const std::vector<float>& sourceRgba,
    const std::vector<float>* inputProxy,
    const std::vector<float>* outputProxy,
    int width,
    int height,
    std::string& error) {
    if (width <= 0 || height <= 0) {
        error = "Restormer adapter dimensions must be positive.";
        return false;
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (sourceRgba.size() != pixelCount * 4U) {
        error = "Restormer adapter source is not a complete RGBA float image.";
        return false;
    }
    if (inputProxy != nullptr && inputProxy->size() != pixelCount * 3U) {
        error = "Restormer input proxy has the wrong size.";
        return false;
    }
    if (outputProxy != nullptr && outputProxy->size() != pixelCount * 3U) {
        error = "Restormer output proxy has the wrong size.";
        return false;
    }
    return true;
}

} // namespace

AdapterResult BuildInputProxy(
    const std::vector<float>& sourceRgba,
    int width,
    int height,
    Raw::RawWorkingSpace workingSpace,
    std::vector<float>& outSrgbProxy) {
    AdapterResult result;
    if (!DimensionsAreValid(
            sourceRgba, nullptr, nullptr, width, height, result.error)) {
        return result;
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const float inputExposureGain = ComputeInputExposureGain(
        sourceRgba, width, height, workingSpace);
    outSrgbProxy.resize(pixelCount * 3U);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const Vec3 working {
            FiniteOr(sourceRgba[pixel * 4U + 0U]),
            FiniteOr(sourceRgba[pixel * 4U + 1U]),
            FiniteOr(sourceRgba[pixel * 4U + 2U])
        };
        const Vec3 proxy = ProxyEncode(
            working, workingSpace, inputExposureGain);
        outSrgbProxy[pixel * 3U + 0U] = proxy[0];
        outSrgbProxy[pixel * 3U + 1U] = proxy[1];
        outSrgbProxy[pixel * 3U + 2U] = proxy[2];
    }
    result.inputExposureGain = inputExposureGain;
    result.ok = true;
    return result;
}

AdapterResult ApplyOutput(
    const std::vector<float>& sourceRgba,
    const std::vector<float>& inputSrgbProxy,
    const std::vector<float>& modelOutputSrgbProxy,
    int width,
    int height,
    Raw::RawWorkingSpace workingSpace,
    const RawRecipe::RawRgbDenoiseRecipe& requestedSettings,
    std::vector<float>& outRgba) {
    AdapterResult result;
    if (!DimensionsAreValid(
            sourceRgba,
            &inputSrgbProxy,
            &modelOutputSrgbProxy,
            width,
            height,
            result.error)) {
        return result;
    }

    const RawRecipe::RawRgbDenoiseRecipe settings =
        RawRecipe::SanitizeRgbDenoiseRecipe(requestedSettings);
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const float inputExposureGain = ComputeInputExposureGain(
        sourceRgba, width, height, workingSpace);
    result.inputExposureGain = inputExposureGain;
    outRgba = sourceRgba;
    if (!settings.enabled ||
        (settings.colorNoise <= 0.0f && settings.luminanceNoise <= 0.0f)) {
        result.ok = true;
        return result;
    }

    std::vector<Vec3> opponentDelta(pixelCount);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const Vec3 inputLinear = ProxyDecode(
            &inputSrgbProxy[pixel * 3U], inputExposureGain);
        const Vec3 outputLinear = ProxyDecode(
            &modelOutputSrgbProxy[pixel * 3U], inputExposureGain);
        opponentDelta[pixel] = ToOpponent({
            outputLinear[0] - inputLinear[0],
            outputLinear[1] - inputLinear[1],
            outputLinear[2] - inputLinear[2]
        });
    }

    Vec3 dc {};
    if (settings.mapping == RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1) {
        for (int channel = 0; channel < 3; ++channel) {
            std::vector<float> values(pixelCount);
            for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
                values[pixel] = opponentDelta[pixel][channel];
            }
            dc[channel] = Median(values);
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t pixel =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            Vec3 delta = opponentDelta[pixel];

            if (settings.mapping ==
                RawRecipe::RawRgbDenoiseMapping::SceneLinearSafeV1) {
                for (int channel = 0; channel < 3; ++channel) {
                    delta[channel] -= dc[channel];
                    std::array<float, 9> localAbsolute {};
                    int localIndex = 0;
                    for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                        const int sampleY = std::clamp(y + offsetY, 0, height - 1);
                        for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                            const int sampleX =
                                std::clamp(x + offsetX, 0, width - 1);
                            const std::size_t sample =
                                static_cast<std::size_t>(sampleY) *
                                    static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(sampleX);
                            localAbsolute[localIndex++] = std::abs(
                                opponentDelta[sample][channel] - dc[channel]);
                        }
                    }
                    const float robustDeviation =
                        1.4826f * Median9(localAbsolute);
                    const float limit = std::max(4.0f * robustDeviation, 1.0e-6f);
                    delta[channel] = std::clamp(
                        delta[channel], -limit, limit);
                }
            }

            delta[0] *= settings.luminanceNoise;
            delta[1] *= settings.colorNoise;
            delta[2] *= settings.colorNoise;
            const float detailAttenuation = DetailAttenuation(
                sourceRgba,
                width,
                height,
                x,
                y,
                workingSpace,
                settings.detailProtection);
            for (float& component : delta) {
                component *= detailAttenuation;
            }

            Vec3 workingDelta =
                LinearSrgbToWorking(FromOpponent(delta), workingSpace);
            const std::size_t sourceIndex = pixel * 4U;
            for (int channel = 0; channel < 3; ++channel) {
                const float original = sourceRgba[sourceIndex + channel];
                const float headroomLimit =
                    8.0f + 4.0f * std::abs(FiniteOr(original));
                const float boundedDelta = std::clamp(
                    FiniteOr(workingDelta[channel]),
                    -headroomLimit,
                    headroomLimit);
                outRgba[sourceIndex + channel] =
                    FiniteOr(original + boundedDelta, original);
            }
            outRgba[sourceIndex + 3U] = sourceRgba[sourceIndex + 3U];
        }
    }

    result.ok = true;
    return result;
}

} // namespace Stack::RawRestormer
