#include "ClassicalRgbDenoiseLayer.h"

#include "Editor/EditorModule.h"
#include "Renderer/FullscreenQuad.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

struct ClassicalRgbDenoiseLayer::FloatImage {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;

    bool IsValid() const {
        return width > 0 &&
            height > 0 &&
            rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
    }
};

namespace {

const char* kCopyVert = R"(
#version 130
in vec2 aPos;
in vec2 aTexCoord;
out vec2 vUV;
void main() {
    vUV = aTexCoord;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* kCopyFrag = R"(
#version 130
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uInputTex;
void main() {
    FragColor = texture(uInputTex, vUV);
}
)";

constexpr double kLargeCpuRunMegapixels = 1.0;

struct QualityPlan {
    int candidates = 12;
    int previewEdge = 560;
    int top = 3;
};

struct CandidateParams {
    int id = 0;
    std::string name;
    int radius = 1;
    int iterations = 1;
    float microStrength = 0.0f;
    float lumaStrength = 0.0f;
    float chromaStrength = 0.0f;
    float lowStrength = 0.0f;
    float detailProtect = 0.0f;
    float detailRestore = 0.0f;
    float shadowBoost = 0.0f;
    float sharpen = 0.0f;
    float anchor = 0.0f;
    float sigmaL = 0.05f;
};

struct CandidateMetrics {
    float score = 0.0f;
    float noiseReduction = 0.0f;
    float detailPenalty = 0.0f;
    float residualPenalty = 0.0f;
    float colorPenalty = 0.0f;
};

struct ScoredCandidate {
    CandidateParams params;
    CandidateMetrics metrics;
};

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float Smoothstep(float edge0, float edge1, float x) {
    if (std::abs(edge1 - edge0) < 1e-6f) {
        return x >= edge1 ? 1.0f : 0.0f;
    }
    const float t = Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

int ClampCoord(int value, int limit) {
    return std::clamp(value, 0, std::max(0, limit - 1));
}

std::size_t PixelIndex(const ClassicalRgbDenoiseLayer::FloatImage& image, int x, int y) {
    const int clampedX = ClampCoord(x, image.width);
    const int clampedY = ClampCoord(y, image.height);
    return (static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(clampedX)) * 4u;
}

float Luminance(const float r, const float g, const float b) {
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

float LuminanceAt(const ClassicalRgbDenoiseLayer::FloatImage& image, int x, int y) {
    const std::size_t index = PixelIndex(image, x, y);
    return Luminance(image.rgba[index + 0], image.rgba[index + 1], image.rgba[index + 2]);
}

std::array<float, 3> RgbToYcc(float r, float g, float b) {
    const float y = Luminance(r, g, b);
    return { y, (b - y) * 0.564f, (r - y) * 0.713f };
}

std::array<float, 3> YccToRgb(float y, float cb, float cr) {
    return {
        y + 1.403f * cr,
        y - 0.344f * cb - 0.714f * cr,
        y + 1.773f * cb
    };
}

float GradientAt(const ClassicalRgbDenoiseLayer::FloatImage& image, int x, int y) {
    const float left = LuminanceAt(image, x - 1, y);
    const float right = LuminanceAt(image, x + 1, y);
    const float up = LuminanceAt(image, x, y - 1);
    const float down = LuminanceAt(image, x, y + 1);
    const float center = LuminanceAt(image, x, y);
    const float average = (left + right + up + down) * 0.25f;
    return std::abs(left - right) + std::abs(up - down) + std::abs(center - average) * 1.6f;
}

std::array<float, 3> NeighborAverageRgb(const ClassicalRgbDenoiseLayer::FloatImage& image, int x, int y) {
    const std::size_t left = PixelIndex(image, x - 1, y);
    const std::size_t right = PixelIndex(image, x + 1, y);
    const std::size_t up = PixelIndex(image, x, y - 1);
    const std::size_t down = PixelIndex(image, x, y + 1);
    return {
        (image.rgba[left + 0] + image.rgba[right + 0] + image.rgba[up + 0] + image.rgba[down + 0]) * 0.25f,
        (image.rgba[left + 1] + image.rgba[right + 1] + image.rgba[up + 1] + image.rgba[down + 1]) * 0.25f,
        (image.rgba[left + 2] + image.rgba[right + 2] + image.rgba[up + 2] + image.rgba[down + 2]) * 0.25f
    };
}

QualityPlan QualityPlanForMode(const int mode) {
    switch (mode) {
        case 0: return { 6, 430, 2 };
        case 2: return { 18, 680, 3 };
        case 3: return { 24, 760, 4 };
        default: return { 12, 560, 3 };
    }
}

std::string QualityModeLabel(const int mode) {
    switch (mode) {
        case 0: return "Fast";
        case 2: return "Quality";
        case 3: return "Deep";
        default: return "Balanced";
    }
}

void HashCombine(std::uint64_t& seed, const std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
}

void HashCombineFloat(std::uint64_t& seed, const float value) {
    HashCombine(seed, static_cast<std::uint64_t>(std::hash<float>{}(value)));
}

std::filesystem::path StatusPathForNode(const int nodeId) {
    std::error_code ec;
    const std::filesystem::path cwd = std::filesystem::current_path(ec);
    const std::filesystem::path root = ec ? std::filesystem::path(".") : cwd;
    return root / ("classical_rgb_denoise_node_" + std::to_string(nodeId) + ".status");
}

ClassicalRgbDenoiseLayer::FloatImage ResizeToMaxEdge(
    const ClassicalRgbDenoiseLayer::FloatImage& source,
    const int maxEdge) {
    if (!source.IsValid() || maxEdge <= 0) {
        return source;
    }

    const int longestEdge = std::max(source.width, source.height);
    if (longestEdge <= maxEdge) {
        return source;
    }

    const float scale = static_cast<float>(maxEdge) / static_cast<float>(longestEdge);
    const int targetWidth = std::max(1, static_cast<int>(std::round(static_cast<float>(source.width) * scale)));
    const int targetHeight = std::max(1, static_cast<int>(std::round(static_cast<float>(source.height) * scale)));

    ClassicalRgbDenoiseLayer::FloatImage result;
    result.width = targetWidth;
    result.height = targetHeight;
    result.rgba.assign(static_cast<std::size_t>(targetWidth) * static_cast<std::size_t>(targetHeight) * 4u, 0.0f);

    for (int y = 0; y < targetHeight; ++y) {
        const float srcY = (static_cast<float>(y) + 0.5f) * static_cast<float>(source.height) / static_cast<float>(targetHeight) - 0.5f;
        const int y0 = std::clamp(static_cast<int>(std::floor(srcY)), 0, source.height - 1);
        const int y1 = std::clamp(y0 + 1, 0, source.height - 1);
        const float fy = srcY - static_cast<float>(y0);
        for (int x = 0; x < targetWidth; ++x) {
            const float srcX = (static_cast<float>(x) + 0.5f) * static_cast<float>(source.width) / static_cast<float>(targetWidth) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(srcX)), 0, source.width - 1);
            const int x1 = std::clamp(x0 + 1, 0, source.width - 1);
            const float fx = srcX - static_cast<float>(x0);

            const std::size_t src00 = PixelIndex(source, x0, y0);
            const std::size_t src10 = PixelIndex(source, x1, y0);
            const std::size_t src01 = PixelIndex(source, x0, y1);
            const std::size_t src11 = PixelIndex(source, x1, y1);
            const std::size_t dst = PixelIndex(result, x, y);
            for (int channel = 0; channel < 4; ++channel) {
                const float top = Lerp(source.rgba[src00 + channel], source.rgba[src10 + channel], fx);
                const float bottom = Lerp(source.rgba[src01 + channel], source.rgba[src11 + channel], fx);
                result.rgba[dst + channel] = Lerp(top, bottom, fy);
            }
        }
    }

    return result;
}

std::vector<CandidateParams> BuildCandidates(
    const int qualityMode,
    const int iterations,
    const bool conservative,
    const float strength,
    const float fineNoise,
    const float chroma,
    const float detailProtect,
    const float shadowBoost,
    const float sharpen) {
    const QualityPlan plan = QualityPlanForMode(qualityMode);
    const int count = plan.candidates;
    const char* names[] = {
        "micro-clean",
        "shadow-silk",
        "grain-lift",
        "edge-safe",
        "texture-guard",
        "clean-pass",
        "fine-pass",
        "balanced"
    };

    std::vector<CandidateParams> candidates;
    candidates.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float t = count == 1 ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
        const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(i) * 2.17f);
        const float zig = static_cast<float>(i % 5) / 4.0f;
        const int radius = 1 + ((i * 2 + (i > count / 2 ? 1 : 0)) % 6);
        const float lumaScale = Lerp(0.58f, 1.28f, t * 0.68f + wave * 0.32f);
        const float microScale = Lerp(0.72f, 1.55f, wave * 0.45f + (1.0f - zig) * 0.25f + t * 0.30f);
        const float chromaScale = Lerp(0.72f, 1.32f, zig * 0.55f + (1.0f - t) * 0.25f + wave * 0.20f);
        const float lowScale = Lerp(0.07f, 0.34f, t * 0.60f + zig * 0.40f);
        const float detailJitter = Lerp(-0.16f, 0.08f, wave);
        const float sigma = Lerp(0.048f, 0.16f, 0.2f + t * 0.52f + zig * 0.28f);

        CandidateParams params;
        params.id = i + 1;
        params.name = std::string(names[i % static_cast<int>(std::size(names))]) + " " + std::to_string(i + 1);
        params.radius = radius;
        params.iterations = std::max(1, iterations + ((i % 4 == 0) ? 1 : 0) - ((i % 7 == 0) ? 1 : 0));
        params.microStrength = Clamp01(fineNoise * microScale + strength * 0.18f);
        params.lumaStrength = Clamp01(strength * lumaScale);
        params.chromaStrength = Clamp01(chroma * chromaScale);
        params.lowStrength = std::clamp(strength * lowScale, 0.0f, 0.5f);
        params.detailProtect = std::clamp(detailProtect + detailJitter, 0.12f, 0.95f);
        params.detailRestore = std::clamp(0.05f + detailProtect * 0.22f + wave * 0.08f, 0.0f, 0.42f);
        params.shadowBoost = Clamp01(shadowBoost);
        params.sharpen = std::clamp(sharpen * Lerp(0.18f, 0.72f, wave), 0.0f, 0.65f);
        params.anchor = std::clamp(detailProtect * Lerp(0.15f, 0.48f, 1.0f - t) + (conservative ? 0.05f : 0.0f), 0.02f, 0.58f);
        params.sigmaL = sigma;
        candidates.push_back(params);
    }

    if (candidates.size() > 8u) {
        candidates.resize(8u);
    }
    return candidates;
}

ClassicalRgbDenoiseLayer::FloatImage BilateralChroma(
    const ClassicalRgbDenoiseLayer::FloatImage& source,
    const int radius,
    const float strength,
    const float sigmaL,
    const float shadowBoost) {
    ClassicalRgbDenoiseLayer::FloatImage result = source;
    const float sigmaS = std::max(0.75f, static_cast<float>(radius) * 0.55f);
    const float twoSigmaS = 2.0f * sigmaS * sigmaS;
    const float safeSigmaL = std::max(0.012f, sigmaL);
    const float twoSigmaL = 2.0f * safeSigmaL * safeSigmaL;

    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const std::size_t index = PixelIndex(source, x, y);
            const auto centerYcc = RgbToYcc(source.rgba[index + 0], source.rgba[index + 1], source.rgba[index + 2]);
            float cbSum = 0.0f;
            float crSum = 0.0f;
            float weightSum = 0.0f;

            for (int yy = -radius; yy <= radius; ++yy) {
                for (int xx = -radius; xx <= radius; ++xx) {
                    const float dist2 = static_cast<float>(xx * xx + yy * yy);
                    if (dist2 > static_cast<float>(radius * radius)) {
                        continue;
                    }
                    const std::size_t sampleIndex = PixelIndex(source, x + xx, y + yy);
                    const auto sampleYcc = RgbToYcc(source.rgba[sampleIndex + 0], source.rgba[sampleIndex + 1], source.rgba[sampleIndex + 2]);
                    const float dy = sampleYcc[0] - centerYcc[0];
                    const float weight = std::exp(-dist2 / twoSigmaS) * std::exp(-(dy * dy) / twoSigmaL);
                    cbSum += sampleYcc[1] * weight;
                    crSum += sampleYcc[2] * weight;
                    weightSum += weight;
                }
            }

            const float gradient = GradientAt(source, x, y);
            const float edge = Smoothstep(0.045f, 0.22f, gradient);
            const float shadow = 1.0f - centerYcc[0];
            const float accept = Clamp01(strength * (0.82f + shadow * shadowBoost * 0.95f) * (1.0f - edge * 0.36f));
            const float filteredCb = cbSum / std::max(weightSum, 1e-6f);
            const float filteredCr = crSum / std::max(weightSum, 1e-6f);
            const auto rgb = YccToRgb(centerYcc[0], Lerp(centerYcc[1], filteredCb, accept), Lerp(centerYcc[2], filteredCr, accept));
            result.rgba[index + 0] = Clamp01(rgb[0]);
            result.rgba[index + 1] = Clamp01(rgb[1]);
            result.rgba[index + 2] = Clamp01(rgb[2]);
        }
    }

    return result;
}

ClassicalRgbDenoiseLayer::FloatImage MicroGrain(
    const ClassicalRgbDenoiseLayer::FloatImage& source,
    const float strength,
    const float detailProtect,
    const float shadowBoost) {
    ClassicalRgbDenoiseLayer::FloatImage result = source;

    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const std::size_t index = PixelIndex(source, x, y);
            const auto centerYcc = RgbToYcc(source.rgba[index + 0], source.rgba[index + 1], source.rgba[index + 2]);
            float ySum = 0.0f;
            float cbSum = 0.0f;
            float crSum = 0.0f;
            float weightSum = 0.0f;

            for (int yy = -2; yy <= 2; ++yy) {
                for (int xx = -2; xx <= 2; ++xx) {
                    const std::size_t sampleIndex = PixelIndex(source, x + xx, y + yy);
                    const auto sampleYcc = RgbToYcc(source.rgba[sampleIndex + 0], source.rgba[sampleIndex + 1], source.rgba[sampleIndex + 2]);
                    const float dist2 = static_cast<float>(xx * xx + yy * yy);
                    const float dy = sampleYcc[0] - centerYcc[0];
                    const float weight = std::exp(-dist2 / 3.0f) * std::exp(-(dy * dy) / 0.0108f);
                    ySum += sampleYcc[0] * weight;
                    cbSum += sampleYcc[1] * weight;
                    crSum += sampleYcc[2] * weight;
                    weightSum += weight;
                }
            }

            const float filteredY = ySum / std::max(weightSum, 1e-6f);
            const float filteredCb = cbSum / std::max(weightSum, 1e-6f);
            const float filteredCr = crSum / std::max(weightSum, 1e-6f);
            const float gradient = GradientAt(source, x, y);
            const float edge = Smoothstep(0.052f, 0.22f, gradient);
            const float shadow = 1.0f - centerYcc[0];
            const float micro = std::abs(centerYcc[0] - filteredY);
            const float structure = Smoothstep(0.05f, 0.18f, micro + gradient * 0.46f);
            const float flatGate = 1.0f - edge * (0.48f + 0.32f * detailProtect);
            const float accept = std::clamp(
                strength * (0.62f + shadow * shadowBoost * 0.7f) * flatGate * (1.0f - structure * detailProtect * 0.42f),
                0.0f,
                0.92f);
            const auto rgb = YccToRgb(
                Lerp(centerYcc[0], filteredY, accept),
                Lerp(centerYcc[1], filteredCb, accept * 0.22f),
                Lerp(centerYcc[2], filteredCr, accept * 0.22f));
            result.rgba[index + 0] = Clamp01(rgb[0]);
            result.rgba[index + 1] = Clamp01(rgb[1]);
            result.rgba[index + 2] = Clamp01(rgb[2]);
        }
    }

    return result;
}

ClassicalRgbDenoiseLayer::FloatImage BilateralLuma(
    const ClassicalRgbDenoiseLayer::FloatImage& source,
    const int radius,
    const float strength,
    const float sigmaL,
    const float detailProtect,
    const float shadowBoost) {
    ClassicalRgbDenoiseLayer::FloatImage result = source;
    const float sigmaS = std::max(0.75f, static_cast<float>(radius) * 0.58f);
    const float twoSigmaS = 2.0f * sigmaS * sigmaS;
    const float safeSigmaL = std::max(0.014f, sigmaL);
    const float twoSigmaL = 2.0f * safeSigmaL * safeSigmaL;

    for (int y = 0; y < source.height; ++y) {
        for (int x = 0; x < source.width; ++x) {
            const std::size_t index = PixelIndex(source, x, y);
            const auto centerYcc = RgbToYcc(source.rgba[index + 0], source.rgba[index + 1], source.rgba[index + 2]);
            float ySum = 0.0f;
            float weightSum = 0.0f;

            for (int yy = -radius; yy <= radius; ++yy) {
                for (int xx = -radius; xx <= radius; ++xx) {
                    const float dist2 = static_cast<float>(xx * xx + yy * yy);
                    if (dist2 > static_cast<float>(radius * radius)) {
                        continue;
                    }
                    const float sampleLuma = LuminanceAt(source, x + xx, y + yy);
                    const float dy = sampleLuma - centerYcc[0];
                    const float weight = std::exp(-dist2 / twoSigmaS) * std::exp(-(dy * dy) / twoSigmaL);
                    ySum += sampleLuma * weight;
                    weightSum += weight;
                }
            }

            const float gradient = GradientAt(source, x, y);
            const float edge = Smoothstep(0.055f, 0.22f, gradient);
            const float shadow = 1.0f - centerYcc[0];
            const float accept = Clamp01(
                strength * (0.78f + shadow * shadowBoost * 1.05f) *
                (1.0f - edge * (0.52f + 0.34f * detailProtect)));
            const float filteredY = ySum / std::max(weightSum, 1e-6f);
            const auto rgb = YccToRgb(Lerp(centerYcc[0], filteredY, accept), centerYcc[1], centerYcc[2]);
            result.rgba[index + 0] = Clamp01(rgb[0]);
            result.rgba[index + 1] = Clamp01(rgb[1]);
            result.rgba[index + 2] = Clamp01(rgb[2]);
        }
    }

    return result;
}

ClassicalRgbDenoiseLayer::FloatImage FinalRestore(
    const ClassicalRgbDenoiseLayer::FloatImage& original,
    const ClassicalRgbDenoiseLayer::FloatImage& denoised,
    const float detailRestore,
    const float sharpen,
    const float anchor) {
    ClassicalRgbDenoiseLayer::FloatImage result = denoised;

    for (int y = 0; y < original.height; ++y) {
        for (int x = 0; x < original.width; ++x) {
            const std::size_t index = PixelIndex(original, x, y);
            const float gradient = GradientAt(original, x, y);
            const float centerLuma = LuminanceAt(original, x, y);
            const float averageLuma =
                (LuminanceAt(original, x - 1, y) +
                    LuminanceAt(original, x + 1, y) +
                    LuminanceAt(original, x, y - 1) +
                    LuminanceAt(original, x, y + 1)) * 0.25f;
            const float highPass = std::abs(centerLuma - averageLuma);
            const float protect = Smoothstep(0.058f, 0.22f, gradient + highPass * 0.34f);
            const auto originalBlur = NeighborAverageRgb(original, x, y);
            const auto denoisedBlur = NeighborAverageRgb(denoised, x, y);
            const float edgeOnly = Smoothstep(0.07f, 0.24f, gradient);

            for (int channel = 0; channel < 3; ++channel) {
                float value = denoised.rgba[index + channel] +
                    (original.rgba[index + channel] - originalBlur[channel]) * detailRestore * protect;
                value += (value - denoisedBlur[channel]) * sharpen * edgeOnly;
                value = Lerp(value, original.rgba[index + channel], protect * anchor * 0.18f);
                result.rgba[index + channel] = Clamp01(value);
            }
            result.rgba[index + 3] = original.rgba[index + 3];
        }
    }

    return result;
}

ClassicalRgbDenoiseLayer::FloatImage ProcessCandidate(
    const ClassicalRgbDenoiseLayer::FloatImage& original,
    const CandidateParams& params) {
    ClassicalRgbDenoiseLayer::FloatImage current = original;
    current = BilateralChroma(current, std::min(5, params.radius + 1), params.chromaStrength, params.sigmaL * 1.5f, params.shadowBoost);
    if (params.microStrength > 0.005f) {
        current = MicroGrain(current, params.microStrength, params.detailProtect, params.shadowBoost);
    }
    if (params.lowStrength > 0.005f) {
        current = BilateralLuma(current, std::min(6, params.radius + 2), params.lowStrength, params.sigmaL * 2.1f, params.detailProtect, params.shadowBoost);
    }
    const int iterations = std::max(1, params.iterations);
    for (int i = 0; i < iterations; ++i) {
        const float iterationStrength = params.lumaStrength * (0.82f + static_cast<float>(i) * 0.08f) / std::sqrt(static_cast<float>(iterations));
        current = BilateralLuma(current, std::min(6, params.radius + (i % 2)), iterationStrength, params.sigmaL, params.detailProtect, params.shadowBoost);
    }
    if (params.microStrength > 0.005f) {
        current = MicroGrain(current, params.microStrength * 0.55f, params.detailProtect, params.shadowBoost);
    }
    return FinalRestore(original, current, params.detailRestore, params.sharpen, params.anchor);
}

CandidateMetrics ScoreCandidate(
    const ClassicalRgbDenoiseLayer::FloatImage& original,
    const ClassicalRgbDenoiseLayer::FloatImage& candidate,
    const float strength,
    const float fineNoise,
    const bool conservative) {
    CandidateMetrics metrics;
    if (!original.IsValid() || !candidate.IsValid() || original.width != candidate.width || original.height != candidate.height) {
        return metrics;
    }

    const int step = std::max(1, std::max(original.width, original.height) / 520);
    float originalHighPass = 0.0f;
    float candidateHighPass = 0.0f;
    float edgeLoss = 0.0f;
    float edgeNorm = 0.0f;
    float residualEdge = 0.0f;
    float colorShift = 0.0f;
    float samples = 0.0f;

    for (int y = 1; y < original.height - 1; y += step) {
        for (int x = 1; x < original.width - 1; x += step) {
            const float originalLuma = LuminanceAt(original, x, y);
            const float candidateLuma = LuminanceAt(candidate, x, y);
            const float originalAvg =
                (LuminanceAt(original, x - 1, y) +
                    LuminanceAt(original, x + 1, y) +
                    LuminanceAt(original, x, y - 1) +
                    LuminanceAt(original, x, y + 1)) * 0.25f;
            const float candidateAvg =
                (LuminanceAt(candidate, x - 1, y) +
                    LuminanceAt(candidate, x + 1, y) +
                    LuminanceAt(candidate, x, y - 1) +
                    LuminanceAt(candidate, x, y + 1)) * 0.25f;
            const float originalHighPassValue = std::abs(originalLuma - originalAvg);
            const float candidateHighPassValue = std::abs(candidateLuma - candidateAvg);
            const float originalGradient =
                std::abs(LuminanceAt(original, x - 1, y) - LuminanceAt(original, x + 1, y)) +
                std::abs(LuminanceAt(original, x, y - 1) - LuminanceAt(original, x, y + 1)) +
                originalHighPassValue * 1.4f;
            const float candidateGradient =
                std::abs(LuminanceAt(candidate, x - 1, y) - LuminanceAt(candidate, x + 1, y)) +
                std::abs(LuminanceAt(candidate, x, y - 1) - LuminanceAt(candidate, x, y + 1)) +
                candidateHighPassValue * 1.4f;
            const float flat = 1.0f - Smoothstep(0.025f, 0.14f, originalGradient + originalHighPassValue);
            const float edge = Smoothstep(0.04f, 0.16f, originalGradient + originalHighPassValue);
            originalHighPass += originalHighPassValue * flat;
            candidateHighPass += candidateHighPassValue * flat;
            edgeLoss += std::max(0.0f, originalGradient - candidateGradient) * edge;
            edgeNorm += originalGradient * edge;
            residualEdge += std::abs(originalLuma - candidateLuma) * edge;

            const std::size_t originalIndex = PixelIndex(original, x, y);
            const std::size_t candidateIndex = PixelIndex(candidate, x, y);
            const float originalR = original.rgba[originalIndex + 0];
            const float originalG = original.rgba[originalIndex + 1];
            const float originalB = original.rgba[originalIndex + 2];
            const float candidateR = candidate.rgba[candidateIndex + 0];
            const float candidateG = candidate.rgba[candidateIndex + 1];
            const float candidateB = candidate.rgba[candidateIndex + 2];
            colorShift +=
                (std::abs((originalR - originalG) - (candidateR - candidateG)) +
                    std::abs((originalB - originalG) - (candidateB - candidateG))) *
                (0.4f + 0.6f * edge);
            samples += 1.0f;
        }
    }

    const float noiseReduction = (originalHighPass - candidateHighPass) / std::max(originalHighPass, 1e-6f);
    const float detailPenalty = edgeLoss / std::max(edgeNorm, 1e-6f);
    const float residualPenalty = residualEdge / std::max(edgeNorm + 0.03f, 1e-6f);
    const float colorPenalty = colorShift / std::max(samples, 1.0f);
    const float target = 0.28f + strength * 0.55f + fineNoise * 0.18f;
    const float under = std::max(0.0f, target * 0.48f - noiseReduction);
    const float over = std::max(0.0f, noiseReduction - (target + 0.28f));
    const float conservativePenalty = conservative ? 1.18f : 1.0f;

    metrics.score = noiseReduction * 2.15f -
        detailPenalty * 1.72f * conservativePenalty -
        residualPenalty * 0.25f * conservativePenalty -
        colorPenalty * 0.5f -
        over * 0.82f -
        under * 1.25f;
    metrics.noiseReduction = noiseReduction;
    metrics.detailPenalty = detailPenalty;
    metrics.residualPenalty = residualPenalty;
    metrics.colorPenalty = colorPenalty;
    return metrics;
}

ClassicalRgbDenoiseLayer::FloatImage BlendFullResults(
    const ClassicalRgbDenoiseLayer::FloatImage& original,
    const std::vector<ScoredCandidate>& winners,
    const std::vector<ClassicalRgbDenoiseLayer::FloatImage>& renderedWinners,
    const float strength,
    const float fineNoise,
    const float outputMix,
    const float detailProtect,
    const float sharpen,
    const float grain,
    const bool conservative) {
    ClassicalRgbDenoiseLayer::FloatImage result = original;
    if (winners.empty() || renderedWinners.empty()) {
        return result;
    }

    std::vector<float> weights;
    weights.reserve(winners.size());
    float minScore = std::numeric_limits<float>::max();
    for (const ScoredCandidate& winner : winners) {
        minScore = std::min(minScore, winner.metrics.score);
    }
    float weightSum = 0.0f;
    for (const ScoredCandidate& winner : winners) {
        const float weight = std::pow(std::max(0.04f, winner.metrics.score - minScore + 0.1f), 1.18f);
        weights.push_back(weight);
        weightSum += weight;
    }
    weightSum = std::max(weightSum, 1e-6f);

    std::uint32_t seed = conservative ? 0x37911a55u : 0x0dff72b3u;
    auto randomUnit = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed) / 4294967296.0f;
    };

    const ClassicalRgbDenoiseLayer::FloatImage& sharpenReference = renderedWinners.front();
    for (int y = 0; y < original.height; ++y) {
        for (int x = 0; x < original.width; ++x) {
            const std::size_t index = PixelIndex(original, x, y);
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            for (std::size_t k = 0; k < renderedWinners.size(); ++k) {
                const float weight = weights[k] / weightSum;
                r += renderedWinners[k].rgba[index + 0] * weight;
                g += renderedWinners[k].rgba[index + 1] * weight;
                b += renderedWinners[k].rgba[index + 2] * weight;
            }

            const float gradient = std::abs(LuminanceAt(original, x - 1, y) - LuminanceAt(original, x + 1, y)) +
                std::abs(LuminanceAt(original, x, y - 1) - LuminanceAt(original, x, y + 1));
            const float highPass = std::abs(
                LuminanceAt(original, x, y) -
                (LuminanceAt(original, x - 1, y) +
                    LuminanceAt(original, x + 1, y) +
                    LuminanceAt(original, x, y - 1) +
                    LuminanceAt(original, x, y + 1)) * 0.25f);
            const float structure = Smoothstep(0.055f, 0.24f, gradient + std::max(0.0f, highPass - 0.018f) * 0.35f);
            const float flat = 1.0f - Smoothstep(0.018f, 0.13f, gradient);
            const float accept = Clamp01(
                outputMix * (0.52f + strength * 0.52f + fineNoise * 0.22f) *
                (1.0f - structure * (0.18f + detailProtect * 0.55f)) *
                (0.86f + flat * 0.18f));
            r = Lerp(original.rgba[index + 0], r, accept);
            g = Lerp(original.rgba[index + 1], g, accept);
            b = Lerp(original.rgba[index + 2], b, accept);

            const float anchor = std::clamp(structure * detailProtect * (conservative ? 0.22f : 0.15f), 0.0f, 0.38f);
            r = Lerp(r, original.rgba[index + 0], anchor);
            g = Lerp(g, original.rgba[index + 1], anchor);
            b = Lerp(b, original.rgba[index + 2], anchor);

            if (sharpen > 0.001f) {
                const auto blur = NeighborAverageRgb(sharpenReference, x, y);
                const float edgeOnly = Smoothstep(0.07f, 0.24f, gradient);
                const float amount = sharpen * 0.42f * edgeOnly;
                r += (r - blur[0]) * amount;
                g += (g - blur[1]) * amount;
                b += (b - blur[2]) * amount;
            }

            if (grain > 0.001f) {
                const float luminance = Luminance(r, g, b);
                const float amount = grain * 0.0165f * (0.45f + 0.55f * (1.0f - luminance)) * (0.35f + 0.65f * flat);
                const float noise = (randomUnit() + randomUnit() + randomUnit() - 1.5f) * amount;
                r += noise;
                g += noise;
                b += noise;
            }

            result.rgba[index + 0] = Clamp01(r);
            result.rgba[index + 1] = Clamp01(g);
            result.rgba[index + 2] = Clamp01(b);
            result.rgba[index + 3] = original.rgba[index + 3];
        }
    }

    return result;
}

} // namespace

ClassicalRgbDenoiseLayer::~ClassicalRgbDenoiseLayer() {
    if (m_CopyProgram) {
        glDeleteProgram(m_CopyProgram);
    }
    if (m_ResultTexture) {
        glDeleteTextures(1, &m_ResultTexture);
    }
}

void ClassicalRgbDenoiseLayer::InitializeGL() {
    if (m_CopyProgram == 0) {
        m_CopyProgram = GLHelpers::CreateShaderProgram(kCopyVert, kCopyFrag);
    }
}

void ClassicalRgbDenoiseLayer::Execute(unsigned int inputTexture, int width, int height, FullscreenQuad& quad) {
    m_LastInputWidth = width;
    m_LastInputHeight = height;
    if (m_HasCachedResult && (m_ResultWidth != width || m_ResultHeight != height)) {
        ClearCachedResult("Cache cleared: image dimensions changed");
    }

    if (!m_DenoiseEnabled) {
        m_LastExecutionStatus = "Bypassed: node disabled";
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    if (!m_RunRequested && m_RunRequestRevision > m_HandledRunRequestRevision) {
        m_RunRequested = true;
    }

    const std::uint64_t cacheKey = BuildCacheKey(inputTexture, width, height);
    if (!m_RunRequested && m_HasCachedResult && m_ResultTexture != 0 && m_CachedResultKey == cacheKey) {
        m_LastExecutionStatus = "Cached result";
        PublishRenderStatus();
        DrawCopy(m_ResultTexture, quad);
        return;
    }

    if (!m_RunRequested) {
        m_LastExecutionStatus = m_HasCachedResult ? "Ready: press Run Denoise to update cache" : "Ready: press Run Denoise";
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    m_RunRequested = false;
    m_HandledRunRequestRevision = m_RunRequestRevision;
    if (CpuRunNeedsConfirmation(width, height) && !m_AllowLargeCpuRunOnce && !m_RunRequestAllowLargeCpu) {
        m_LastExecutionStatus = "CPU large-image guard: press Run Large CPU Denoise";
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }
    m_AllowLargeCpuRunOnce = false;
    m_LastError.clear();
    m_LastExecutionStatus = "Reading source image";
    PublishRenderStatus();

    FloatImage original;
    if (!ReadTextureToImage(inputTexture, width, height, original)) {
        m_LastExecutionStatus = "Processing failed: texture readback";
        m_LastError = m_LastExecutionStatus;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    const QualityPlan qualityPlan = QualityPlanForMode(m_QualityMode);
    std::vector<CandidateParams> candidates = BuildCandidates(
        m_QualityMode,
        m_Iterations,
        m_Conservative,
        m_Strength,
        m_FineNoise,
        m_Chroma,
        m_DetailProtect,
        m_ShadowBoost,
        m_Sharpen);
    m_LastPreviewCandidates = static_cast<int>(candidates.size());

    FloatImage preview = ResizeToMaxEdge(original, qualityPlan.previewEdge);
    std::vector<ScoredCandidate> scored;
    scored.reserve(candidates.size());
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        m_LastExecutionStatus = "Preview sweep " + std::to_string(i + 1) + "/" + std::to_string(candidates.size()) + ": " + candidates[i].name;
        PublishRenderStatus();
        FloatImage previewResult = ProcessCandidate(preview, candidates[i]);
        ScoredCandidate scoredCandidate;
        scoredCandidate.params = candidates[i];
        scoredCandidate.metrics = ScoreCandidate(preview, previewResult, m_Strength, m_FineNoise, m_Conservative);
        scored.push_back(std::move(scoredCandidate));
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredCandidate& a, const ScoredCandidate& b) {
        return a.metrics.score > b.metrics.score;
    });
    if (scored.empty()) {
        m_LastExecutionStatus = "Processing failed: no candidates";
        m_LastError = m_LastExecutionStatus;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    const int winnerCount = std::min(qualityPlan.top, static_cast<int>(scored.size()));
    m_LastWinnerCount = winnerCount;
    m_LastBestScore = scored.front().metrics.score;
    std::vector<FloatImage> fullResults;
    fullResults.reserve(static_cast<std::size_t>(winnerCount));
    std::vector<ScoredCandidate> winners;
    winners.reserve(static_cast<std::size_t>(winnerCount));
    std::string winnerSummary;
    for (int i = 0; i < winnerCount; ++i) {
        winners.push_back(scored[static_cast<std::size_t>(i)]);
        if (!winnerSummary.empty()) {
            winnerSummary += ", ";
        }
        winnerSummary += scored[static_cast<std::size_t>(i)].params.name;
        m_LastExecutionStatus = "Full render " + std::to_string(i + 1) + "/" + std::to_string(winnerCount) + ": " + scored[static_cast<std::size_t>(i)].params.name;
        PublishRenderStatus();
        fullResults.push_back(ProcessCandidate(original, scored[static_cast<std::size_t>(i)].params));
    }
    m_LastWinnerSummary = winnerSummary;

    m_LastExecutionStatus = "Blending winning renders";
    PublishRenderStatus();
    FloatImage blended = BlendFullResults(
        original,
        winners,
        fullResults,
        m_Strength,
        m_FineNoise,
        m_OutputMix,
        m_DetailProtect,
        m_Sharpen,
        m_Grain,
        m_Conservative);

    const auto stop = std::chrono::steady_clock::now();
    m_LastRunSeconds = std::chrono::duration<double>(stop - start).count();
    if (!UploadAndDrawResult(blended, quad)) {
        m_LastExecutionStatus = "Processing failed: upload";
        m_LastError = m_LastExecutionStatus;
        PublishRenderStatus();
        DrawCopy(inputTexture, quad);
        return;
    }

    m_HasCachedResult = true;
    m_CachedResultKey = cacheKey;
    m_RunRequestAllowLargeCpu = false;
    m_LastExecutionStatus = "Cached result";
    PublishRenderStatus();
}

void ClassicalRgbDenoiseLayer::DrawCopy(unsigned int inputTexture, FullscreenQuad& quad) {
    if (m_CopyProgram == 0) {
        InitializeGL();
    }
    glUseProgram(m_CopyProgram);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);
    glUniform1i(glGetUniformLocation(m_CopyProgram, "uInputTex"), 0);
    quad.Draw();
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

bool ClassicalRgbDenoiseLayer::ReadTextureToImage(unsigned int inputTexture, int width, int height, FloatImage& outImage) {
    if (inputTexture == 0 || width <= 0 || height <= 0) {
        return false;
    }

    GLint prevReadFbo = 0;
    GLint prevDrawFbo = 0;
    GLint prevFbo = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);

    GLuint readFbo = 0;
    glGenFramebuffers(1, &readFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFbo);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, inputTexture, 0);
    if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glDeleteFramebuffers(1, &readFbo);
        return false;
    }

    std::vector<float> bottomLeft(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0.0f);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, bottomLeft.data());

    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    glDeleteFramebuffers(1, &readFbo);

    outImage.width = width;
    outImage.height = height;
    outImage.rgba.assign(bottomLeft.size(), 0.0f);
    for (int y = 0; y < height; ++y) {
        const int sourceY = height - 1 - y;
        const std::size_t dst = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4u;
        const std::size_t src = static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) * 4u;
        std::copy(
            bottomLeft.begin() + static_cast<std::ptrdiff_t>(src),
            bottomLeft.begin() + static_cast<std::ptrdiff_t>(src + static_cast<std::size_t>(width) * 4u),
            outImage.rgba.begin() + static_cast<std::ptrdiff_t>(dst));
    }
    return outImage.IsValid();
}

bool ClassicalRgbDenoiseLayer::UploadAndDrawResult(const FloatImage& image, FullscreenQuad& quad) {
    if (!image.IsValid()) {
        return false;
    }

    if (m_ResultTexture == 0 || m_ResultWidth != image.width || m_ResultHeight != image.height) {
        if (m_ResultTexture) {
            glDeleteTextures(1, &m_ResultTexture);
            m_ResultTexture = 0;
        }
        glGenTextures(1, &m_ResultTexture);
        glBindTexture(GL_TEXTURE_2D, m_ResultTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, image.width, image.height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        m_ResultWidth = image.width;
        m_ResultHeight = image.height;
    } else {
        glBindTexture(GL_TEXTURE_2D, m_ResultTexture);
    }

    std::vector<float> bottomLeft(image.rgba.size(), 0.0f);
    for (int y = 0; y < image.height; ++y) {
        const int dstY = image.height - 1 - y;
        const std::size_t src = static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4u;
        const std::size_t dst = static_cast<std::size_t>(dstY) * static_cast<std::size_t>(image.width) * 4u;
        std::copy(
            image.rgba.begin() + static_cast<std::ptrdiff_t>(src),
            image.rgba.begin() + static_cast<std::ptrdiff_t>(src + static_cast<std::size_t>(image.width) * 4u),
            bottomLeft.begin() + static_cast<std::ptrdiff_t>(dst));
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.width, image.height, GL_RGBA, GL_FLOAT, bottomLeft.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    DrawCopy(m_ResultTexture, quad);
    return true;
}

std::uint64_t ClassicalRgbDenoiseLayer::BuildCacheKey(unsigned int inputTexture, int width, int height) const {
    std::uint64_t seed = 0x434c415353444e49ull;
    HashCombine(seed, static_cast<std::uint64_t>(inputTexture));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, width)));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, height)));
    HashCombine(seed, static_cast<std::uint64_t>(m_DenoiseEnabled ? 1u : 0u));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, m_QualityMode)));
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, m_Iterations)));
    HashCombine(seed, static_cast<std::uint64_t>(m_Conservative ? 1u : 0u));
    HashCombineFloat(seed, m_Strength);
    HashCombineFloat(seed, m_FineNoise);
    HashCombineFloat(seed, m_OutputMix);
    HashCombineFloat(seed, m_Chroma);
    HashCombineFloat(seed, m_DetailProtect);
    HashCombineFloat(seed, m_ShadowBoost);
    HashCombineFloat(seed, m_Sharpen);
    HashCombineFloat(seed, m_Grain);
    HashCombine(seed, static_cast<std::uint64_t>(std::max(0, m_RunRequestRevision)));
    HashCombine(seed, static_cast<std::uint64_t>(m_RunRequestAllowLargeCpu ? 1u : 0u));
    return seed;
}

bool ClassicalRgbDenoiseLayer::CpuRunNeedsConfirmation(int width, int height) const {
    if (width <= 0 || height <= 0) {
        return false;
    }
    const double megapixels = (static_cast<double>(width) * static_cast<double>(height)) / 1000000.0;
    return megapixels > kLargeCpuRunMegapixels;
}

void ClassicalRgbDenoiseLayer::MarkDenoiseUiDirty(EditorModule* editor) const {
    if (!editor) {
        return;
    }
    if (m_ActiveUiNodeId >= 0) {
        editor->MarkRenderDirty(m_ActiveUiNodeId);
    } else {
        editor->MarkSelectedLayerRenderDirty();
    }
}

void ClassicalRgbDenoiseLayer::PublishRenderStatus() const {
    if (m_RenderNodeId < 0) {
        return;
    }
    std::ofstream output(StatusPathForNode(m_RenderNodeId), std::ios::trunc);
    if (!output) {
        return;
    }
    output << "status=" << m_LastExecutionStatus << "\n";
    output << "error=" << m_LastError << "\n";
    output << "seconds=" << m_LastRunSeconds << "\n";
    output << "width=" << m_LastInputWidth << "\n";
    output << "height=" << m_LastInputHeight << "\n";
    output << "previewCandidates=" << m_LastPreviewCandidates << "\n";
    output << "winnerCount=" << m_LastWinnerCount << "\n";
    output << "bestScore=" << m_LastBestScore << "\n";
    output << "winners=" << m_LastWinnerSummary << "\n";
}

void ClassicalRgbDenoiseLayer::RefreshPublishedRenderStatus() {
    const int nodeId = m_ActiveUiNodeId >= 0 ? m_ActiveUiNodeId : m_RenderNodeId;
    if (nodeId < 0) {
        return;
    }
    std::ifstream input(StatusPathForNode(nodeId));
    if (!input) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "status" && !value.empty()) {
            m_LastExecutionStatus = value;
        } else if (key == "error") {
            m_LastError = value;
        } else if (key == "seconds") {
            try { m_LastRunSeconds = std::stod(value); } catch (...) {}
        } else if (key == "width") {
            try { m_LastInputWidth = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "height") {
            try { m_LastInputHeight = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "previewCandidates") {
            try { m_LastPreviewCandidates = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "winnerCount") {
            try { m_LastWinnerCount = std::max(0, std::stoi(value)); } catch (...) {}
        } else if (key == "bestScore") {
            try { m_LastBestScore = std::stof(value); } catch (...) {}
        } else if (key == "winners") {
            m_LastWinnerSummary = value;
        }
    }
}

void ClassicalRgbDenoiseLayer::ClearCachedResult(const char* status) {
    if (m_ResultTexture) {
        glDeleteTextures(1, &m_ResultTexture);
        m_ResultTexture = 0;
    }
    m_ResultWidth = 0;
    m_ResultHeight = 0;
    m_CachedResultKey = 0;
    m_HasCachedResult = false;
    m_RunRequested = false;
    m_AllowLargeCpuRunOnce = false;
    m_LastRunSeconds = 0.0;
    m_LastPreviewCandidates = 0;
    m_LastWinnerCount = 0;
    m_LastBestScore = 0.0f;
    m_LastWinnerSummary.clear();
    m_LastError.clear();
    m_HandledRunRequestRevision = 0;
    if (status) {
        m_LastExecutionStatus = status;
    }
}

void ClassicalRgbDenoiseLayer::RenderUI() {
    RenderUI(nullptr);
}

void ClassicalRgbDenoiseLayer::RenderUI(EditorModule* editor) {
    RefreshPublishedRenderStatus();
    const float controlWidth = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    if (editor && editor->SelectedLayerInputContainsViewTransform()) {
        ImGui::TextWrapped("Warning: this node appears after View Transform. Classical RGB denoise works best before display compression.");
    }
    ImGui::TextWrapped("This node runs a preview candidate sweep, scores the denoise against the incoming image, and caches a blended full-resolution result when you press Run Denoise.");
    ImGui::TextWrapped("Unlike the website importer, this node keeps the graph raster size unchanged and uses the incoming node image as the anchor.");

    ImGuiExtras::RichSectionLabel("SWEEP", 4.0f);
    const char* qualityLabels[] = { "Fast", "Balanced", "Quality", "Deep" };
    ImGuiExtras::NodeCombo("Quality Mode", "##ClassicalRgbQuality", &m_QualityMode, qualityLabels, IM_ARRAYSIZE(qualityLabels), controlWidth);
    ImGuiExtras::NodeSliderInt("Iterations / Candidate", "##ClassicalRgbIterations", &m_Iterations, 1, 6, "%d", controlWidth);
    ImGuiExtras::NodeCheckbox("Conservative Selection", "##ClassicalRgbConservative", &m_Conservative, controlWidth);
    m_QualityMode = std::clamp(m_QualityMode, 0, 3);
    m_Iterations = std::clamp(m_Iterations, 1, 6);
    const QualityPlan plan = QualityPlanForMode(m_QualityMode);
    ImGui::TextDisabled("Mode: %s  |  Preview edge: %d px  |  CPU candidates: %d  |  Full winners: %d",
        QualityModeLabel(m_QualityMode).c_str(),
        plan.previewEdge,
        std::min(plan.candidates, 8),
        plan.top);

    auto percentSlider = [&](const char* label, const char* id, float& value) {
        float percent = value * 100.0f;
        const bool changed = ImGuiExtras::NodeSliderFloat(label, id, &percent, 0.0f, 100.0f, "%.0f %%", controlWidth);
        value = Clamp01(percent / 100.0f);
        return changed;
    };

    ImGuiExtras::RichSectionLabel("CHARACTER", 4.0f);
    percentSlider("Overall Strength", "##ClassicalRgbStrength", m_Strength);
    percentSlider("Fine Grain Removal", "##ClassicalRgbFineNoise", m_FineNoise);
    percentSlider("Visible Denoise Mix", "##ClassicalRgbOutputMix", m_OutputMix);
    percentSlider("Chroma Cleanup", "##ClassicalRgbChroma", m_Chroma);
    percentSlider("Detail Protection", "##ClassicalRgbDetailProtect", m_DetailProtect);
    percentSlider("Shadow Help", "##ClassicalRgbShadowBoost", m_ShadowBoost);
    percentSlider("Final Detail Snap", "##ClassicalRgbSharpen", m_Sharpen);
    percentSlider("Natural Grain Return", "##ClassicalRgbGrain", m_Grain);

    ImGuiExtras::RichSectionLabel("EXECUTION", 4.0f);
    const bool enableChanged = ImGuiExtras::NodeCheckbox("Enable", "##ClassicalRgbEnabled", &m_DenoiseEnabled, controlWidth);
    const double megapixels = (static_cast<double>(std::max(0, m_LastInputWidth)) * static_cast<double>(std::max(0, m_LastInputHeight))) / 1000000.0;
    ImGui::TextDisabled("Image: %d x %d (%.2f MP)", std::max(0, m_LastInputWidth), std::max(0, m_LastInputHeight), megapixels);
    ImGui::TextDisabled("Cache: %s", m_HasCachedResult ? "cached result available" : "empty");
    ImGui::TextDisabled("Last preview sweep: %d candidate(s)", m_LastPreviewCandidates);
    ImGui::TextDisabled("Last winner count: %d", m_LastWinnerCount);
    ImGui::TextDisabled("Last best score: %.4f", m_LastBestScore);
    ImGui::TextDisabled("Last run time: %.2f s", m_LastRunSeconds);
    if (!m_LastWinnerSummary.empty()) {
        ImGui::TextWrapped("Last winners: %s", m_LastWinnerSummary.c_str());
    }
    if (ImGui::Button(m_HasCachedResult ? "Refresh Denoise" : "Run Denoise")) {
        ++m_RunRequestRevision;
        m_RunRequestAllowLargeCpu = false;
        m_RunRequested = true;
        m_AllowLargeCpuRunOnce = false;
        m_LastExecutionStatus = "Queued";
        MarkDenoiseUiDirty(editor);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Cached Result")) {
        ClearCachedResult("Cached result cleared");
        MarkDenoiseUiDirty(editor);
    }
    if (CpuRunNeedsConfirmation(m_LastInputWidth, m_LastInputHeight) ||
        m_LastExecutionStatus.find("Run Large CPU Denoise") != std::string::npos ||
        m_LastExecutionStatus.find("large-image guard") != std::string::npos) {
        ImGui::TextWrapped("Full-resolution CPU renders can stall the editor above %.2f MP. Confirm the large run if you want the full website-style sweep on a bigger image.", kLargeCpuRunMegapixels);
        if (ImGui::Button("Run Large CPU Denoise")) {
            ++m_RunRequestRevision;
            m_RunRequestAllowLargeCpu = true;
            m_RunRequested = true;
            m_AllowLargeCpuRunOnce = true;
            m_LastExecutionStatus = "Queued large CPU run";
            MarkDenoiseUiDirty(editor);
        }
    }
    if (!m_LastError.empty()) {
        ImGui::TextWrapped("Last error: %s", m_LastError.c_str());
    }
    ImGui::TextWrapped("Status: %s", m_LastExecutionStatus.c_str());

    if (enableChanged) {
        MarkDenoiseUiDirty(editor);
    }
}

NodeSurfaceSpec ClassicalRgbDenoiseLayer::GetNodeSurfaceSpec() const {
    NodeSurfaceSpec spec;
    spec.presentation = NodeSurfacePresentation::RichExpandedSurface;
    spec.density = NodeSurfaceDensity::Dense;
    spec.preferredWidth = 420.0f;
    spec.maxWidth = 520.0f;
    return spec;
}

void ClassicalRgbDenoiseLayer::RenderExpandedNodeSurface(EditorModule* editor, const NodeSurfaceContext& context) {
    m_ActiveUiNodeId = context.nodeId;
    m_RenderNodeId = context.nodeId;
    RenderUI(editor);
    m_ActiveUiNodeId = -1;
}

json ClassicalRgbDenoiseLayer::Serialize() const {
    return {
        { "type", "ClassicalRgbDenoise" },
        { "enabled", m_DenoiseEnabled },
        { "qualityMode", m_QualityMode },
        { "iterations", m_Iterations },
        { "conservative", m_Conservative },
        { "strength", m_Strength },
        { "fineNoise", m_FineNoise },
        { "outputMix", m_OutputMix },
        { "chroma", m_Chroma },
        { "detailProtect", m_DetailProtect },
        { "shadowBoost", m_ShadowBoost },
        { "sharpen", m_Sharpen },
        { "grain", m_Grain },
        { "runRequestRevision", m_RunRequestRevision },
        { "runRequestAllowLargeCpu", m_RunRequestAllowLargeCpu },
        { "renderNodeId", m_RenderNodeId }
    };
}

void ClassicalRgbDenoiseLayer::Deserialize(const json& j) {
    m_DenoiseEnabled = j.value("enabled", m_DenoiseEnabled);
    m_QualityMode = std::clamp(j.value("qualityMode", m_QualityMode), 0, 3);
    m_Iterations = std::clamp(j.value("iterations", m_Iterations), 1, 6);
    m_Conservative = j.value("conservative", m_Conservative);
    m_Strength = Clamp01(j.value("strength", m_Strength));
    m_FineNoise = Clamp01(j.value("fineNoise", m_FineNoise));
    m_OutputMix = Clamp01(j.value("outputMix", m_OutputMix));
    m_Chroma = Clamp01(j.value("chroma", m_Chroma));
    m_DetailProtect = Clamp01(j.value("detailProtect", m_DetailProtect));
    m_ShadowBoost = Clamp01(j.value("shadowBoost", m_ShadowBoost));
    m_Sharpen = Clamp01(j.value("sharpen", m_Sharpen));
    m_Grain = Clamp01(j.value("grain", m_Grain));
    m_RunRequestRevision = std::max(0, j.value("runRequestRevision", m_RunRequestRevision));
    m_RunRequestAllowLargeCpu = j.value("runRequestAllowLargeCpu", m_RunRequestAllowLargeCpu);
    m_RenderNodeId = j.value("renderNodeId", m_RenderNodeId);
    ClearCachedResult("Cache cleared after load");
}
