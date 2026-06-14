#include "EditorRenderWorker.h"

#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Renderer/RenderPipeline.h"
#include "Renderer/GLLoader.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <iterator>
#include <memory>

namespace {

void ReleaseResultResources(EditorRenderWorker::Result& result) {
    if (result.outputTexture.readyFence) {
        glDeleteSync(result.outputTexture.readyFence);
        result.outputTexture.readyFence = nullptr;
    }
    if (result.outputTexture.texture != 0) {
        glDeleteTextures(1, &result.outputTexture.texture);
        result.outputTexture.texture = 0;
    }
}

int EstimateRenderProgressStepCount(const EditorRenderWorker::Snapshot& snapshot) {
    int steps = 0;
    if (!snapshot.compositeOutputs.empty()) {
        steps += static_cast<int>(snapshot.compositeOutputs.size());
    } else if (snapshot.outputConnected) {
        steps += 1;
    }
    steps += static_cast<int>(snapshot.developCandidateRenders.size());
    steps += static_cast<int>(snapshot.previews.size());
    return std::max(1, steps);
}

std::string TrimProgressText(std::string text, std::size_t maxLength) {
    if (text.size() <= maxLength) {
        return text;
    }
    if (maxLength <= 3) {
        return text.substr(0, maxLength);
    }
    return text.substr(0, maxLength - 3) + "...";
}

std::string BuildDevelopCandidateProgressLabel(
    const std::string& candidateLabel,
    const std::string& candidateRevisionStage,
    int candidateIndex,
    int candidateCount) {
    const int visibleIndex = std::max(1, candidateIndex + 1);
    const int visibleCount = std::max(visibleIndex, candidateCount);
    std::string label =
        "Measuring Develop feedback " +
        std::to_string(visibleIndex) +
        "/" +
        std::to_string(visibleCount);
    if (!candidateLabel.empty()) {
        label += ": " + TrimProgressText(candidateLabel, 48);
    }
    if (!candidateRevisionStage.empty()) {
        label += " [" + TrimProgressText(candidateRevisionStage, 24) + "]";
    }
    label += "...";
    return label;
}

float MillisecondsBetween(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

float DevelopRiskAbove(float value, float safeValue, float fullRiskValue) {
    if (fullRiskValue <= safeValue) {
        return value > safeValue ? 1.0f : 0.0f;
    }
    return std::clamp((value - safeValue) / (fullRiskValue - safeValue), 0.0f, 1.0f);
}

float DevelopRiskBelow(float value, float safeValue, float fullRiskValue) {
    if (safeValue <= fullRiskValue) {
        return value < safeValue ? 1.0f : 0.0f;
    }
    return std::clamp((safeValue - value) / (safeValue - fullRiskValue), 0.0f, 1.0f);
}

float ComputeDevelopLocalDamageRisk(
    float tileMean,
    float tileContrast,
    float tileShadowFraction,
    float tileHighlightFraction,
    float globalLowSaturationFraction) {
    const float highlightCrowding =
        std::clamp(
            DevelopRiskAbove(tileHighlightFraction, 0.58f, 0.92f) * 0.74f +
                DevelopRiskAbove(tileMean, 0.82f, 0.97f) * 0.26f,
            0.0f,
            1.0f);
    const float shadowCrowding =
        std::clamp(
            DevelopRiskAbove(tileShadowFraction, 0.72f, 0.94f) * 0.68f +
                DevelopRiskBelow(tileMean, 0.12f, 0.04f) * 0.32f,
            0.0f,
            1.0f);
    const float edgeStress = DevelopRiskAbove(tileContrast, 0.84f, 0.98f);
    const float flatGrayRisk =
        DevelopRiskBelow(tileContrast, 0.18f, 0.06f) *
        DevelopRiskAbove(tileMean, 0.16f, 0.42f) *
        DevelopRiskAbove(globalLowSaturationFraction, 0.70f, 0.95f);

    // This is a compact diagnostic map, not a full perceptual damage map. It
    // flags regional pressure that should make ranking/rejection more cautious.
    return std::clamp(
        std::max({ highlightCrowding, shadowCrowding, edgeStress * 0.78f, flatGrayRisk * 0.70f }),
        0.0f,
        1.0f);
}

float SmoothDevelopSubjectMetricWeight(float normalizedDistance, float feather) {
    const float featherWidth = std::max(0.02f, std::clamp(feather, 0.0f, 1.0f) * 0.85f);
    if (normalizedDistance <= 1.0f) {
        return 1.0f;
    }
    if (normalizedDistance >= 1.0f + featherWidth) {
        return 0.0f;
    }
    const float t = std::clamp((normalizedDistance - 1.0f) / featherWidth, 0.0f, 1.0f);
    const float smooth = t * t * (3.0f - 2.0f * t);
    return 1.0f - smooth;
}

float DevelopSubjectMetricDistanceToSegmentSq(
    float px,
    float py,
    const EditorRenderWorker::DevelopSubjectMetricPoint& a,
    const EditorRenderWorker::DevelopSubjectMetricPoint& b) {
    const float vx = b.x - a.x;
    const float vy = b.y - a.y;
    const float wx = px - a.x;
    const float wy = py - a.y;
    const float segmentLenSq = vx * vx + vy * vy;
    const float t = segmentLenSq > 0.0000001f
        ? std::clamp((wx * vx + wy * vy) / segmentLenSq, 0.0f, 1.0f)
        : 0.0f;
    const float closestX = a.x + vx * t;
    const float closestY = a.y + vy * t;
    const float dx = px - closestX;
    const float dy = py - closestY;
    return dx * dx + dy * dy;
}

struct DevelopSubjectMetricWeights {
    float important = 0.0f;
    float reveal = 0.0f;
    float protect = 0.0f;
    float preserveMood = 0.0f;
    float lowPriority = 0.0f;

    float Positive() const {
        return std::max({ important, reveal, protect, preserveMood });
    }

    float Any() const {
        return std::max(Positive(), lowPriority);
    }
};

void AddDevelopSubjectMetricModeWeight(
    DevelopSubjectMetricWeights& weights,
    int mode,
    bool lowPriority,
    float weight) {
    if (weight <= 0.001f) {
        return;
    }

    const int clampedMode = std::clamp(mode, 0, 4);
    if (lowPriority || clampedMode == 4) {
        weights.lowPriority = std::max(weights.lowPriority, weight);
        return;
    }

    switch (clampedMode) {
        case 1:
            weights.reveal = std::max(weights.reveal, weight);
            break;
        case 2:
            weights.protect = std::max(weights.protect, weight);
            break;
        case 3:
            weights.preserveMood = std::max(weights.preserveMood, weight);
            break;
        case 0:
        default:
            weights.important = std::max(weights.important, weight);
            break;
    }
}

DevelopSubjectMetricWeights ComputeDevelopSubjectMetricWeights(
    const EditorRenderWorker::DevelopSubjectMetricSampling& sampling,
    float nx,
    float ny) {
    DevelopSubjectMetricWeights weights;
    for (const EditorRenderWorker::DevelopSubjectMetricRegion& region : sampling.regions) {
        if (!region.enabled || region.strength <= 0.001f) {
            continue;
        }
        const float radiusX = std::clamp(region.radiusX, 0.005f, 1.0f);
        const float radiusY = std::clamp(region.radiusY, 0.005f, 1.0f);
        const float dx = (nx - std::clamp(region.centerX, 0.0f, 1.0f)) / radiusX;
        const float dy = (ny - std::clamp(region.centerY, 0.0f, 1.0f)) / radiusY;
        const float normalizedDistance = std::sqrt(dx * dx + dy * dy);
        const float weight =
            SmoothDevelopSubjectMetricWeight(normalizedDistance, region.feather) *
            std::clamp(region.strength, 0.0f, 1.0f);
        AddDevelopSubjectMetricModeWeight(weights, region.mode, region.lowPriority, weight);
    }

    for (const EditorRenderWorker::DevelopSubjectMetricStroke& stroke : sampling.strokes) {
        if (!stroke.enabled || stroke.strength <= 0.001f || stroke.points.empty()) {
            continue;
        }
        if (nx < stroke.minX || nx > stroke.maxX || ny < stroke.minY || ny > stroke.maxY) {
            continue;
        }

        float minDistanceSq = std::numeric_limits<float>::infinity();
        if (stroke.points.size() == 1) {
            const float dx = nx - stroke.points.front().x;
            const float dy = ny - stroke.points.front().y;
            minDistanceSq = dx * dx + dy * dy;
        } else {
            for (std::size_t i = 1; i < stroke.points.size(); ++i) {
                minDistanceSq = std::min(
                    minDistanceSq,
                    DevelopSubjectMetricDistanceToSegmentSq(nx, ny, stroke.points[i - 1], stroke.points[i]));
            }
        }

        const float radius = std::clamp(stroke.radius, 0.002f, 0.50f);
        const float normalizedDistance = std::sqrt(std::max(0.0f, minDistanceSq)) / radius;
        const float weight =
            SmoothDevelopSubjectMetricWeight(normalizedDistance, stroke.feather) *
            std::clamp(stroke.strength, 0.0f, 1.0f);
        AddDevelopSubjectMetricModeWeight(weights, stroke.mode, stroke.lowPriority, weight);
    }

    // Reduce/ignore strokes are user intent too. At overlap they soften positive
    // marks instead of becoming a hard subtraction mask.
    const float reduce = std::clamp(weights.lowPriority * 0.65f, 0.0f, 1.0f);
    weights.important = std::max(0.0f, weights.important - reduce);
    weights.reveal = std::max(0.0f, weights.reveal - reduce);
    weights.protect = std::max(0.0f, weights.protect - reduce);
    weights.preserveMood = std::max(0.0f, weights.preserveMood - reduce);
    return weights;
}

float DevelopWeightedHistogramPercentile(
    const std::array<double, 256>& histogram,
    double totalWeight,
    float percentile) {
    if (totalWeight <= 0.0) {
        return 0.0f;
    }
    const double target = std::clamp(static_cast<double>(percentile), 0.0, 1.0) * totalWeight;
    double cumulative = 0.0;
    for (int bucket = 0; bucket < 256; ++bucket) {
        cumulative += histogram[static_cast<std::size_t>(bucket)];
        if (cumulative >= target) {
            return static_cast<float>(bucket) / 255.0f;
        }
    }
    return 1.0f;
}

EditorRenderWorker::DevelopCandidateRenderMetrics AnalyzeDevelopCandidatePixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    const EditorRenderWorker::DevelopSubjectMetricSampling* subjectSampling) {
    EditorRenderWorker::DevelopCandidateRenderMetrics metrics;
    if (pixels.empty() || width <= 0 || height <= 0) {
        return metrics;
    }

    std::array<int, 256> histogram {};
    double lumaSum = 0.0;
    double redSum = 0.0;
    double greenSum = 0.0;
    double blueSum = 0.0;
    double saturationSum = 0.0;
    int shadowCount = 0;
    int highlightCount = 0;
    int clippedCount = 0;
    int lowSaturationCount = 0;
    int sampleCount = 0;
    const int safeWidth = std::max(0, width);
    const int safeHeight = std::max(0, height);
    std::array<double, 9> tileLumaSum {};
    std::array<int, 9> tileSampleCount {};
    std::array<int, 9> tileShadowCount {};
    std::array<int, 9> tileHighlightCount {};
    std::array<float, 9> tileMinLuma {};
    std::array<float, 9> tileMaxLuma {};
    tileMinLuma.fill(1.0f);
    const size_t pixelCount = std::min(
        pixels.size() / 4u,
        static_cast<size_t>(std::max(0, width) * std::max(0, height)));
    const bool subjectSamplingActive =
        subjectSampling &&
        subjectSampling->enabled &&
        (!subjectSampling->regions.empty() || !subjectSampling->strokes.empty());
    const int subjectStride =
        pixelCount >= 8000000u ? 5 :
        pixelCount >= 2000000u ? 3 :
        pixelCount >= 600000u ? 2 : 1;
    std::array<double, 256> subjectMarkedHistogram {};
    double subjectAnyWeightSum = 0.0;
    double subjectPositiveWeightSum = 0.0;
    double subjectImportantWeightSum = 0.0;
    double subjectRevealWeightSum = 0.0;
    double subjectProtectWeightSum = 0.0;
    double subjectMoodWeightSum = 0.0;
    double subjectLowPriorityWeightSum = 0.0;
    double subjectMarkedLumaSum = 0.0;
    double subjectMarkedShadowWeight = 0.0;
    double subjectMarkedHighlightWeight = 0.0;
    double subjectMarkedClippedWeight = 0.0;
    double subjectLowPriorityLumaSum = 0.0;
    double subjectLowPriorityBrightWeight = 0.0;
    int subjectMetricSampleCount = 0;
    int subjectMarkedSampleCount = 0;
    std::vector<float> lumaValues(pixelCount, -1.0f);
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const size_t offset = pixelIndex * 4u;
        const float r = static_cast<float>(pixels[offset + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[offset + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[offset + 2]) / 255.0f;
        const float a = static_cast<float>(pixels[offset + 3]) / 255.0f;
        if (a <= 0.0f) {
            continue;
        }
        const float luma = std::clamp(0.2126f * r + 0.7152f * g + 0.0722f * b, 0.0f, 1.0f);
        const float maxChannel = std::max({ r, g, b });
        const float minChannel = std::min({ r, g, b });
        const float saturation = maxChannel > 0.0001f
            ? std::clamp((maxChannel - minChannel) / maxChannel, 0.0f, 1.0f)
            : 0.0f;
        lumaValues[pixelIndex] = luma;
        const int bucket = std::clamp(static_cast<int>(std::lround(luma * 255.0f)), 0, 255);
        ++histogram[static_cast<size_t>(bucket)];
        lumaSum += luma;
        redSum += r;
        greenSum += g;
        blueSum += b;
        saturationSum += saturation;
        ++sampleCount;
        const int x = static_cast<int>(pixelIndex % static_cast<size_t>(safeWidth));
        const int y = static_cast<int>(pixelIndex / static_cast<size_t>(safeWidth));
        const int tileX = std::clamp((x * 3) / std::max(1, safeWidth), 0, 2);
        const int tileY = std::clamp((y * 3) / std::max(1, safeHeight), 0, 2);
        const int tileIndex = tileY * 3 + tileX;
        if (subjectSamplingActive &&
            (x % subjectStride) == 0 &&
            (y % subjectStride) == 0) {
            ++subjectMetricSampleCount;
            const float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(std::max(1, safeWidth));
            const float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(std::max(1, safeHeight));
            const DevelopSubjectMetricWeights subjectWeights =
                ComputeDevelopSubjectMetricWeights(*subjectSampling, nx, ny);
            const float positiveWeight = std::clamp(subjectWeights.Positive(), 0.0f, 1.0f);
            const float lowPriorityWeight = std::clamp(subjectWeights.lowPriority, 0.0f, 1.0f);
            const float anyWeight = std::clamp(std::max(positiveWeight, lowPriorityWeight), 0.0f, 1.0f);
            if (anyWeight > 0.001f) {
                ++subjectMarkedSampleCount;
                subjectAnyWeightSum += anyWeight;
            }
            if (positiveWeight > 0.001f) {
                subjectPositiveWeightSum += positiveWeight;
                subjectImportantWeightSum += subjectWeights.important;
                subjectRevealWeightSum += subjectWeights.reveal;
                subjectProtectWeightSum += subjectWeights.protect;
                subjectMoodWeightSum += subjectWeights.preserveMood;
                subjectMarkedLumaSum += luma * positiveWeight;
                subjectMarkedHistogram[static_cast<std::size_t>(bucket)] += positiveWeight;
                if (luma < 0.10f) {
                    subjectMarkedShadowWeight += positiveWeight;
                }
                if (luma > 0.90f) {
                    subjectMarkedHighlightWeight += positiveWeight;
                }
                if (maxChannel >= 0.995f || luma >= 0.985f) {
                    subjectMarkedClippedWeight += positiveWeight;
                }
            }
            if (lowPriorityWeight > 0.001f) {
                subjectLowPriorityWeightSum += lowPriorityWeight;
                subjectLowPriorityLumaSum += luma * lowPriorityWeight;
                if (luma > 0.58f) {
                    subjectLowPriorityBrightWeight += lowPriorityWeight;
                }
            }
        }
        tileLumaSum[static_cast<size_t>(tileIndex)] += luma;
        ++tileSampleCount[static_cast<size_t>(tileIndex)];
        tileMinLuma[static_cast<size_t>(tileIndex)] =
            std::min(tileMinLuma[static_cast<size_t>(tileIndex)], luma);
        tileMaxLuma[static_cast<size_t>(tileIndex)] =
            std::max(tileMaxLuma[static_cast<size_t>(tileIndex)], luma);
        if (luma < 0.10f) {
            ++shadowCount;
            ++tileShadowCount[static_cast<size_t>(tileIndex)];
        }
        if (luma > 0.90f) {
            ++highlightCount;
            ++tileHighlightCount[static_cast<size_t>(tileIndex)];
        }
        if (maxChannel >= 0.995f || luma >= 0.985f) {
            ++clippedCount;
        }
        if (luma > 0.12f && saturation < 0.06f) {
            ++lowSaturationCount;
        }
    }

    if (sampleCount <= 0) {
        return metrics;
    }

    auto percentileFromHistogram = [&](float percentile) {
        const int target = std::clamp(
            static_cast<int>(std::lround(static_cast<float>(sampleCount - 1) * percentile)),
            0,
            sampleCount - 1);
        int cumulative = 0;
        for (int bucket = 0; bucket < 256; ++bucket) {
            cumulative += histogram[static_cast<size_t>(bucket)];
            if (cumulative > target) {
                return static_cast<float>(bucket) / 255.0f;
            }
        }
        return 1.0f;
    };

    metrics.meanLuma = static_cast<float>(lumaSum / static_cast<double>(sampleCount));
    metrics.p10Luma = percentileFromHistogram(0.10f);
    metrics.medianLuma = percentileFromHistogram(0.50f);
    metrics.p90Luma = percentileFromHistogram(0.90f);
    metrics.shadowFraction = static_cast<float>(shadowCount) / static_cast<float>(sampleCount);
    metrics.highlightFraction = static_cast<float>(highlightCount) / static_cast<float>(sampleCount);
    metrics.clippedFraction = static_cast<float>(clippedCount) / static_cast<float>(sampleCount);
    metrics.contrastSpan = std::max(0.0f, metrics.p90Luma - metrics.p10Luma);
    metrics.meanRed = static_cast<float>(redSum / static_cast<double>(sampleCount));
    metrics.meanGreen = static_cast<float>(greenSum / static_cast<double>(sampleCount));
    metrics.meanBlue = static_cast<float>(blueSum / static_cast<double>(sampleCount));
    const float meanMaxChannel = std::max({ metrics.meanRed, metrics.meanGreen, metrics.meanBlue });
    const float meanMinChannel = std::min({ metrics.meanRed, metrics.meanGreen, metrics.meanBlue });
    metrics.warmCoolBias =
        std::clamp(
            (metrics.meanRed - metrics.meanBlue) /
                std::max(0.08f, metrics.meanRed + metrics.meanBlue),
            -1.0f,
            1.0f);
    metrics.magentaGreenBias =
        std::clamp(
            (((metrics.meanRed + metrics.meanBlue) * 0.5f) - metrics.meanGreen) /
                std::max(0.08f, metrics.meanRed + metrics.meanGreen + metrics.meanBlue),
            -1.0f,
            1.0f);
    metrics.channelImbalance =
        std::clamp((meanMaxChannel - meanMinChannel) / std::max(0.08f, meanMaxChannel), 0.0f, 1.0f);
    metrics.meanSaturation = static_cast<float>(saturationSum / static_cast<double>(sampleCount));
    metrics.lowSaturationFraction = static_cast<float>(lowSaturationCount) / static_cast<float>(sampleCount);
    const float highlightBandThreshold =
        std::clamp(std::max(0.54f, metrics.p90Luma * 0.88f), 0.50f, 0.88f);
    std::array<int, 9> tileHighlightBandCount {};
    double highlightBandLumaSum = 0.0;
    int highlightBandCount = 0;
    int highlightBandLowSaturationCount = 0;
    for (size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex) {
        const float luma = lumaValues[pixelIndex];
        if (luma < highlightBandThreshold) {
            continue;
        }
        const size_t offset = pixelIndex * 4u;
        const float a = static_cast<float>(pixels[offset + 3]) / 255.0f;
        if (a <= 0.0f) {
            continue;
        }
        const float r = static_cast<float>(pixels[offset + 0]) / 255.0f;
        const float g = static_cast<float>(pixels[offset + 1]) / 255.0f;
        const float b = static_cast<float>(pixels[offset + 2]) / 255.0f;
        const float maxChannel = std::max({ r, g, b });
        const float minChannel = std::min({ r, g, b });
        const float saturation = maxChannel > 0.0001f
            ? std::clamp((maxChannel - minChannel) / maxChannel, 0.0f, 1.0f)
            : 0.0f;
        const int x = static_cast<int>(pixelIndex % static_cast<size_t>(safeWidth));
        const int y = static_cast<int>(pixelIndex / static_cast<size_t>(safeWidth));
        const int tileX = std::clamp((x * 3) / std::max(1, safeWidth), 0, 2);
        const int tileY = std::clamp((y * 3) / std::max(1, safeHeight), 0, 2);
        ++tileHighlightBandCount[static_cast<size_t>(tileY * 3 + tileX)];
        highlightBandLumaSum += luma;
        ++highlightBandCount;
        if (saturation < 0.10f) {
            ++highlightBandLowSaturationCount;
        }
    }
    if (highlightBandCount > 0) {
        metrics.highlightBandFraction =
            static_cast<float>(highlightBandCount) / static_cast<float>(sampleCount);
        metrics.highlightMeanLuma =
            std::clamp(
                static_cast<float>(highlightBandLumaSum / static_cast<double>(highlightBandCount)),
                0.0f,
                1.0f);
        metrics.highlightLowSaturationFraction =
            static_cast<float>(highlightBandLowSaturationCount) / static_cast<float>(highlightBandCount);
    }
    // Compact color evidence for rendered comparisons. This is intentionally
    // conservative: strong warm/cool mood can be valid, while magenta/green or
    // single-channel imbalance is usually a more useful damage signal.
    metrics.colorCastRisk =
        std::clamp(
            std::max({
                DevelopRiskAbove(std::fabs(metrics.warmCoolBias), 0.76f, 0.96f) * 0.75f,
                DevelopRiskAbove(std::fabs(metrics.magentaGreenBias), 0.20f, 0.42f),
                DevelopRiskAbove(metrics.channelImbalance, 0.72f, 0.94f) * 0.65f }) *
                DevelopRiskAbove(metrics.meanLuma, 0.08f, 0.20f),
            0.0f,
            1.0f);
    float minTileMean = 1.0f;
    float maxTileMean = 0.0f;
    float localDamageRiskSum = 0.0f;
    int localDamageRiskCount = 0;
    float highlightStructureSum = 0.0f;
    int highlightStructureTileCount = 0;
    int populatedTileCount = 0;
    int localDarkTileCount = 0;
    int localBrightTileCount = 0;
    for (size_t tileIndex = 0; tileIndex < metrics.localMeanLuma.size(); ++tileIndex) {
        if (tileSampleCount[tileIndex] <= 0) {
            metrics.localMeanLuma[tileIndex] = metrics.meanLuma;
            metrics.localContrastSpan[tileIndex] = 0.0f;
            continue;
        }
        ++populatedTileCount;

        const float tileMean =
            static_cast<float>(tileLumaSum[tileIndex] / static_cast<double>(tileSampleCount[tileIndex]));
        const float tileContrast =
            std::max(0.0f, tileMaxLuma[tileIndex] - tileMinLuma[tileIndex]);
        const float tileShadow =
            static_cast<float>(tileShadowCount[tileIndex]) / static_cast<float>(tileSampleCount[tileIndex]);
        const float tileHighlight =
            static_cast<float>(tileHighlightCount[tileIndex]) / static_cast<float>(tileSampleCount[tileIndex]);
        const float tileHighlightBand =
            static_cast<float>(tileHighlightBandCount[tileIndex]) / static_cast<float>(tileSampleCount[tileIndex]);
        metrics.localMeanLuma[tileIndex] = tileMean;
        metrics.localContrastSpan[tileIndex] = tileContrast;
        minTileMean = std::min(minTileMean, tileMean);
        maxTileMean = std::max(maxTileMean, tileMean);
        if (tileMean < 0.22f || tileShadow > 0.36f) {
            ++localDarkTileCount;
        }
        if (tileMean > 0.62f || tileHighlight > 0.18f || tileHighlightBand > 0.18f) {
            ++localBrightTileCount;
        }
        metrics.localContrastPeak = std::max(metrics.localContrastPeak, tileContrast);
        metrics.localShadowPressure = std::max(metrics.localShadowPressure, tileShadow);
        metrics.localHighlightPressure = std::max(metrics.localHighlightPressure, tileHighlight);
        const bool meaningfulHighlightTile =
            tileHighlightBand > 0.08f ||
            (tileMean > highlightBandThreshold && tileContrast > 0.04f);
        if (meaningfulHighlightTile) {
            ++highlightStructureTileCount;
            highlightStructureSum += std::clamp(
                DevelopRiskAbove(tileContrast, 0.10f, 0.34f) * 0.72f +
                    DevelopRiskAbove(tileHighlightBand, 0.12f, 0.42f) * 0.28f,
                0.0f,
                1.0f);
        }
        const float localDamageRisk =
            ComputeDevelopLocalDamageRisk(
                tileMean,
                tileContrast,
                tileShadow,
                tileHighlight,
                metrics.lowSaturationFraction);
        metrics.localDamageRiskScore[tileIndex] = localDamageRisk;
        localDamageRiskSum += localDamageRisk;
        ++localDamageRiskCount;
        if (localDamageRisk > metrics.localDamageRiskPeak) {
            metrics.localDamageRiskPeak = localDamageRisk;
            metrics.localDamageRiskPeakTile = static_cast<int>(tileIndex);
        }
    }
    metrics.localLumaSpread = std::max(0.0f, maxTileMean - minTileMean);
    metrics.localEvSpreadStops = populatedTileCount > 0
        ? std::clamp(std::log2((maxTileMean + 0.025f) / (minTileMean + 0.025f)), 0.0f, 8.0f)
        : 0.0f;
    metrics.highlightTileCoverage = populatedTileCount > 0
        ? std::clamp(
            static_cast<float>(highlightStructureTileCount) / static_cast<float>(populatedTileCount),
            0.0f,
            1.0f)
        : 0.0f;
    metrics.highlightStructureScore = highlightStructureTileCount > 0
        ? std::clamp(highlightStructureSum / static_cast<float>(highlightStructureTileCount), 0.0f, 1.0f)
        : 0.0f;
    metrics.localDamageRiskMean = localDamageRiskCount > 0
        ? std::clamp(localDamageRiskSum / static_cast<float>(localDamageRiskCount), 0.0f, 1.0f)
        : 0.0f;
    metrics.centerMeanLuma = metrics.localMeanLuma[4];
    if (tileSampleCount[4] > 0) {
        metrics.centerShadowFraction =
            static_cast<float>(tileShadowCount[4]) / static_cast<float>(tileSampleCount[4]);
        metrics.centerHighlightFraction =
            static_cast<float>(tileHighlightCount[4]) / static_cast<float>(tileSampleCount[4]);
    }
    const float broadHighlightPresence = std::clamp(
        DevelopRiskAbove(metrics.highlightBandFraction, 0.12f, 0.36f) * 0.55f +
            DevelopRiskAbove(metrics.localHighlightPressure, 0.28f, 0.66f) * 0.30f +
            DevelopRiskAbove(metrics.centerHighlightFraction, 0.12f, 0.42f) * 0.15f,
        0.0f,
        1.0f);
    const float highlightDimness =
        metrics.highlightBandFraction > 0.0f
            ? DevelopRiskBelow(metrics.highlightMeanLuma, 0.70f, 0.48f)
            : 0.0f;
    const float highlightGrayness =
        DevelopRiskAbove(metrics.highlightLowSaturationFraction, 0.42f, 0.82f);
    const float highlightSeparationLoss = std::max(
        DevelopRiskBelow(metrics.contrastSpan, 0.36f, 0.16f),
        DevelopRiskBelow(metrics.localLumaSpread, 0.18f, 0.06f));
    metrics.highlightGrayRisk = std::clamp(
        broadHighlightPresence *
            (highlightDimness * 0.42f +
             highlightGrayness * 0.30f +
             highlightSeparationLoss * 0.20f +
             metrics.localDamageRiskMean * 0.08f),
        0.0f,
        1.0f);
    const float meaningfulHighlightArea =
        DevelopRiskAbove(metrics.highlightBandFraction, 0.10f, 0.32f);
    const float meaningfulHighlightCoverage =
        DevelopRiskAbove(metrics.highlightTileCoverage, 0.18f, 0.56f);
    const float tinySpecularDiscount =
        DevelopRiskBelow(metrics.highlightBandFraction, 0.12f, 0.03f) *
        DevelopRiskBelow(metrics.highlightTileCoverage, 0.22f, 0.05f);
    metrics.meaningfulHighlightPressure = std::clamp(
        meaningfulHighlightArea * 0.38f +
            meaningfulHighlightCoverage * 0.32f +
            metrics.highlightStructureScore * 0.18f +
            DevelopRiskAbove(metrics.highlightMeanLuma, 0.58f, 0.78f) * 0.08f +
            metrics.highlightGrayRisk * 0.08f -
            tinySpecularDiscount * 0.28f,
        0.0f,
        1.0f);

    double edgeContrastSum = 0.0;
    int edgeSampleCount = 0;
    int haloRiskCount = 0;
    double shadowDiffSum = 0.0;
    int shadowDiffCount = 0;
    auto lumaAt = [&](int x, int y) -> float {
        if (x < 0 || y < 0 || x >= safeWidth || y >= safeHeight) {
            return -1.0f;
        }
        const size_t index = static_cast<size_t>(y) * static_cast<size_t>(safeWidth) + static_cast<size_t>(x);
        return index < lumaValues.size() ? lumaValues[index] : -1.0f;
    };
    for (int y = 1; y + 1 < safeHeight; ++y) {
        for (int x = 1; x + 1 < safeWidth; ++x) {
            const float center = lumaAt(x, y);
            const float left = lumaAt(x - 1, y);
            const float right = lumaAt(x + 1, y);
            const float up = lumaAt(x, y - 1);
            const float down = lumaAt(x, y + 1);
            if (center < 0.0f || left < 0.0f || right < 0.0f || up < 0.0f || down < 0.0f) {
                continue;
            }
            const float localMin = std::min({ center, left, right, up, down });
            const float localMax = std::max({ center, left, right, up, down });
            const float localRange = localMax - localMin;
            if (localRange > 0.18f) {
                edgeContrastSum += localRange;
                ++edgeSampleCount;
                const float neighborMean = 0.25f * (left + right + up + down);
                const bool centerOvershootsNeighbors =
                    (center > neighborMean + 0.22f && center > left && center > right && center > up && center > down) ||
                    (center < neighborMean - 0.22f && center < left && center < right && center < up && center < down);
                if (centerOvershootsNeighbors) {
                    ++haloRiskCount;
                }
            }
            if (center < 0.22f) {
                shadowDiffSum +=
                    std::fabs(center - left) +
                    std::fabs(center - right) +
                    std::fabs(center - up) +
                    std::fabs(center - down);
                shadowDiffCount += 4;
            }
        }
    }
    metrics.edgeContrast = edgeSampleCount > 0
        ? std::clamp(static_cast<float>(edgeContrastSum / static_cast<double>(edgeSampleCount)), 0.0f, 1.0f)
        : 0.0f;
    metrics.haloRiskFraction = edgeSampleCount > 0
        ? std::clamp(static_cast<float>(haloRiskCount) / static_cast<float>(edgeSampleCount), 0.0f, 1.0f)
        : 0.0f;
    metrics.shadowTextureRisk = shadowDiffCount > 0
        ? std::clamp(static_cast<float>(shadowDiffSum / static_cast<double>(shadowDiffCount)) / 0.18f, 0.0f, 1.0f)
        : 0.0f;
    const float localDarkTileCoverage = populatedTileCount > 0
        ? static_cast<float>(localDarkTileCount) / static_cast<float>(populatedTileCount)
        : 0.0f;
    const float localBrightTileCoverage = populatedTileCount > 0
        ? static_cast<float>(localBrightTileCount) / static_cast<float>(populatedTileCount)
        : 0.0f;
    const float localMixedRangeCoverage =
        std::clamp(std::min(localDarkTileCoverage, localBrightTileCoverage) * 2.0f, 0.0f, 1.0f);
    metrics.localEvConflict = std::clamp(
        DevelopRiskAbove(metrics.localEvSpreadStops, 1.35f, 4.20f) * 0.36f +
            localMixedRangeCoverage * 0.28f +
            DevelopRiskAbove(metrics.localLumaSpread, 0.28f, 0.62f) * 0.14f +
            metrics.localDamageRiskMean * 0.08f +
            DevelopRiskAbove(metrics.edgeContrast, 0.30f, 0.70f) * 0.08f +
            DevelopRiskAbove(metrics.haloRiskFraction, 0.04f, 0.18f) * 0.06f,
        0.0f,
        1.0f);
    metrics.localExposureHighlightCrowding = std::clamp(
        DevelopRiskAbove(metrics.localHighlightPressure, 0.34f, 0.82f) * 0.30f +
            DevelopRiskAbove(metrics.centerHighlightFraction, 0.18f, 0.58f) * 0.16f +
            DevelopRiskAbove(metrics.highlightBandFraction, 0.12f, 0.42f) * 0.18f +
            DevelopRiskAbove(metrics.clippedFraction, 0.004f, 0.024f) * 0.14f +
            metrics.meaningfulHighlightPressure * 0.14f +
            metrics.localDamageRiskPeak * 0.08f,
        0.0f,
        1.0f);
    metrics.localExposureShadowCrowding = std::clamp(
        DevelopRiskAbove(metrics.localShadowPressure, 0.48f, 0.88f) * 0.30f +
            DevelopRiskAbove(metrics.centerShadowFraction, 0.32f, 0.74f) * 0.16f +
            DevelopRiskAbove(metrics.shadowFraction, 0.44f, 0.84f) * 0.18f +
            metrics.shadowTextureRisk * 0.16f +
            metrics.localDamageRiskPeak * 0.10f +
            metrics.localEvConflict * 0.10f,
        0.0f,
        1.0f);
    metrics.localExposureHaloStress = std::clamp(
        DevelopRiskAbove(metrics.haloRiskFraction, 0.04f, 0.18f) * 0.36f +
            DevelopRiskAbove(metrics.edgeContrast, 0.34f, 0.72f) * 0.22f +
            DevelopRiskAbove(metrics.localContrastPeak, 0.74f, 0.96f) * 0.16f +
            metrics.localEvConflict * 0.14f +
            metrics.localDamageRiskPeak * 0.12f,
        0.0f,
        1.0f);
    metrics.localExposureFlatnessRisk = std::clamp(
        DevelopRiskBelow(metrics.contrastSpan, 0.30f, 0.12f) * 0.26f +
            DevelopRiskBelow(metrics.localContrastPeak, 0.34f, 0.14f) * 0.22f +
            DevelopRiskBelow(metrics.localLumaSpread, 0.16f, 0.04f) * 0.20f +
            DevelopRiskAbove(metrics.lowSaturationFraction, 0.70f, 0.94f) * 0.16f +
            metrics.highlightGrayRisk * 0.12f +
            metrics.localDamageRiskMean * 0.04f,
        0.0f,
        1.0f);
    metrics.localExposureDamageRisk = std::clamp(
        metrics.localExposureHighlightCrowding * 0.22f +
            metrics.localExposureShadowCrowding * 0.18f +
            metrics.localExposureHaloStress * 0.30f +
            metrics.localExposureFlatnessRisk * 0.16f +
            metrics.localDamageRiskPeak * 0.14f,
        0.0f,
        1.0f);
    const float centerContrast = metrics.localContrastSpan[4];
    const float centerDistinctness =
        DevelopRiskAbove(std::fabs(metrics.centerMeanLuma - metrics.meanLuma), 0.06f, 0.28f);
    const float centerStructure = DevelopRiskAbove(centerContrast, 0.08f, 0.34f);
    const float centerNotCrushed =
        1.0f - DevelopRiskBelow(metrics.centerMeanLuma, 0.12f, 0.02f);
    const float centerNotBlown =
        1.0f - DevelopRiskAbove(metrics.centerMeanLuma, 0.86f, 0.98f);
    const float centerUsableTone =
        std::clamp(centerNotCrushed * centerNotBlown, 0.0f, 1.0f);
    const float centerDarkRisk =
        DevelopRiskBelow(metrics.centerMeanLuma, 0.30f, 0.08f);
    const float centerHighlightRisk =
        DevelopRiskAbove(metrics.centerMeanLuma, 0.72f, 0.94f);
    // This is a weak composition/detail prior, not subject detection. It gives
    // Auto a named place to preserve "what likely matters" until Guide 05 adds
    // user-painted importance maps and richer scene understanding.
    metrics.subjectCenterPrior = std::clamp(
        0.18f +
            centerUsableTone * 0.18f +
            centerStructure * 0.22f +
            centerDistinctness * 0.18f +
            DevelopRiskAbove(metrics.localLumaSpread, 0.12f, 0.44f) * 0.08f +
            std::max(
                DevelopRiskAbove(metrics.centerShadowFraction, 0.20f, 0.62f),
                DevelopRiskAbove(metrics.centerHighlightFraction, 0.10f, 0.42f)) * 0.08f -
            metrics.localExposureFlatnessRisk * 0.06f,
        0.0f,
        1.0f);
    metrics.subjectReadabilityPressure = std::clamp(
        metrics.subjectCenterPrior *
            (centerDarkRisk * 0.34f +
             DevelopRiskAbove(metrics.centerShadowFraction, 0.26f, 0.70f) * 0.24f +
             DevelopRiskAbove(metrics.localShadowPressure, 0.46f, 0.86f) * 0.14f +
             metrics.localEvConflict * 0.10f +
             centerStructure * 0.06f -
             metrics.shadowTextureRisk * 0.12f -
             metrics.localExposureDamageRisk * 0.08f),
        0.0f,
        1.0f);
    metrics.subjectProtectionPressure = std::clamp(
        metrics.subjectCenterPrior *
            (centerHighlightRisk * 0.24f +
             DevelopRiskAbove(metrics.centerHighlightFraction, 0.12f, 0.46f) * 0.24f +
             metrics.meaningfulHighlightPressure * 0.16f +
             metrics.localExposureHighlightCrowding * 0.12f +
             DevelopRiskAbove(metrics.clippedFraction, 0.004f, 0.024f) * 0.12f +
             centerStructure * 0.06f -
             metrics.localExposureHaloStress * 0.06f),
        0.0f,
        1.0f);
    metrics.subjectMoodPreservationPressure = std::clamp(
        metrics.subjectCenterPrior *
            (centerDarkRisk * 0.24f +
             localBrightTileCoverage * 0.18f +
             DevelopRiskAbove(metrics.localLumaSpread, 0.24f, 0.62f) * 0.16f +
             metrics.shadowTextureRisk * 0.16f +
             metrics.localExposureShadowCrowding * 0.10f +
             metrics.localExposureHaloStress * 0.08f -
             metrics.subjectReadabilityPressure * 0.10f),
        0.0f,
        1.0f);
    metrics.subjectImportanceConfidence = std::clamp(
        metrics.subjectCenterPrior * 0.36f +
            centerStructure * 0.18f +
            centerDistinctness * 0.14f +
            std::max({
                metrics.subjectReadabilityPressure,
                metrics.subjectProtectionPressure,
                metrics.subjectMoodPreservationPressure }) * 0.20f +
            metrics.localEvConflict * 0.06f +
            metrics.meaningfulHighlightPressure * 0.06f,
        0.0f,
        1.0f);
    if (subjectSamplingActive && subjectMetricSampleCount > 0 && subjectAnyWeightSum > 0.001) {
        metrics.subjectMarkedSampleCount = subjectMarkedSampleCount;
        metrics.subjectMarkedCoverage = std::clamp(
            static_cast<float>(subjectAnyWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedPositiveCoverage = std::clamp(
            static_cast<float>(subjectPositiveWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedRevealCoverage = std::clamp(
            static_cast<float>(subjectRevealWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedProtectCoverage = std::clamp(
            static_cast<float>(subjectProtectWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedMoodCoverage = std::clamp(
            static_cast<float>(subjectMoodWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
        metrics.subjectMarkedLowPriorityCoverage = std::clamp(
            static_cast<float>(subjectLowPriorityWeightSum / static_cast<double>(subjectMetricSampleCount)),
            0.0f,
            1.0f);
    }
    if (subjectPositiveWeightSum > 0.001) {
        metrics.subjectMarkedMeanLuma = std::clamp(
            static_cast<float>(subjectMarkedLumaSum / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedShadowFraction = std::clamp(
            static_cast<float>(subjectMarkedShadowWeight / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedHighlightFraction = std::clamp(
            static_cast<float>(subjectMarkedHighlightWeight / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedClippedFraction = std::clamp(
            static_cast<float>(subjectMarkedClippedWeight / subjectPositiveWeightSum),
            0.0f,
            1.0f);
        const float subjectP10 = DevelopWeightedHistogramPercentile(
            subjectMarkedHistogram,
            subjectPositiveWeightSum,
            0.10f);
        const float subjectP90 = DevelopWeightedHistogramPercentile(
            subjectMarkedHistogram,
            subjectPositiveWeightSum,
            0.90f);
        metrics.subjectMarkedContrastSpan = std::max(0.0f, subjectP90 - subjectP10);
        const float toneFit =
            1.0f - std::clamp(std::fabs(metrics.subjectMarkedMeanLuma - 0.38f) / 0.38f, 0.0f, 1.0f);
        const float contrastSupport = std::clamp(metrics.subjectMarkedContrastSpan / 0.34f, 0.0f, 1.0f);
        metrics.subjectMarkedReadabilityScore = std::clamp(
            toneFit * 0.44f +
                contrastSupport * 0.20f +
                (1.0f - metrics.subjectMarkedShadowFraction) * 0.18f +
                (1.0f - metrics.subjectMarkedClippedFraction) * 0.18f -
                metrics.subjectMarkedHighlightFraction * 0.08f,
            0.0f,
            1.0f);
        metrics.subjectMarkedProtectionRisk = std::clamp(
            metrics.subjectMarkedClippedFraction * 0.70f +
                DevelopRiskAbove(metrics.subjectMarkedHighlightFraction, 0.22f, 0.62f) * 0.20f +
                DevelopRiskBelow(metrics.subjectMarkedContrastSpan, 0.06f, 0.02f) * 0.10f,
            0.0f,
            1.0f);
        metrics.subjectMarkedMoodPreservationScore = std::clamp(
            (1.0f - DevelopRiskAbove(metrics.subjectMarkedMeanLuma, 0.60f, 0.90f)) * 0.48f +
                (1.0f - metrics.subjectMarkedClippedFraction) * 0.24f +
                contrastSupport * 0.18f +
                (1.0f - metrics.subjectMarkedHighlightFraction) * 0.10f,
            0.0f,
            1.0f);
    }
    if (subjectLowPriorityWeightSum > 0.001) {
        metrics.subjectMarkedLowPriorityMeanLuma = std::clamp(
            static_cast<float>(subjectLowPriorityLumaSum / subjectLowPriorityWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedLowPriorityBrightFraction = std::clamp(
            static_cast<float>(subjectLowPriorityBrightWeight / subjectLowPriorityWeightSum),
            0.0f,
            1.0f);
        metrics.subjectMarkedLowPriorityPressure = std::clamp(
            metrics.subjectMarkedLowPriorityCoverage *
                (0.35f + metrics.subjectMarkedLowPriorityBrightFraction * 0.65f),
            0.0f,
            1.0f);
    }
    return metrics;
}

} // namespace

EditorRenderWorker::EditorRenderWorker() = default;

EditorRenderWorker::~EditorRenderWorker() {
    Shutdown();
}

EditorRenderWorker::DevelopCandidateRenderMetrics
EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    return AnalyzeDevelopCandidatePixels(pixels, width, height, nullptr);
}

EditorRenderWorker::DevelopCandidateRenderMetrics
EditorRenderWorker::AnalyzeDevelopCandidatePixelsForValidation(
    const std::vector<unsigned char>& pixels,
    int width,
    int height,
    const DevelopSubjectMetricSampling& subjectSampling) {
    return AnalyzeDevelopCandidatePixels(pixels, width, height, &subjectSampling);
}

float EditorRenderWorker::CompareDevelopCandidateRenderMetrics(
    const DevelopCandidateRenderMetrics& a,
    const DevelopCandidateRenderMetrics& b) {
    // Weighted compact distance for clustering rendered candidates that are visually redundant.
    float localMeanDistance = 0.0f;
    float localContrastDistance = 0.0f;
    float localDamageRiskDistance = 0.0f;
    for (size_t tileIndex = 0; tileIndex < a.localMeanLuma.size(); ++tileIndex) {
        localMeanDistance += std::fabs(a.localMeanLuma[tileIndex] - b.localMeanLuma[tileIndex]);
        localContrastDistance += std::fabs(a.localContrastSpan[tileIndex] - b.localContrastSpan[tileIndex]);
        localDamageRiskDistance += std::fabs(a.localDamageRiskScore[tileIndex] - b.localDamageRiskScore[tileIndex]);
    }
    localMeanDistance /= static_cast<float>(a.localMeanLuma.size());
    localContrastDistance /= static_cast<float>(a.localContrastSpan.size());
    localDamageRiskDistance /= static_cast<float>(a.localDamageRiskScore.size());
    return
        std::fabs(a.medianLuma - b.medianLuma) * 0.90f +
        std::fabs(a.meanLuma - b.meanLuma) * 0.45f +
        std::fabs(a.p10Luma - b.p10Luma) * 0.45f +
        std::fabs(a.p90Luma - b.p90Luma) * 0.45f +
        std::fabs(a.shadowFraction - b.shadowFraction) * 0.38f +
        std::fabs(a.highlightFraction - b.highlightFraction) * 0.38f +
        std::fabs(a.clippedFraction - b.clippedFraction) * 1.30f +
        std::fabs(a.contrastSpan - b.contrastSpan) * 0.55f +
        std::fabs(a.meanRed - b.meanRed) * 0.10f +
        std::fabs(a.meanGreen - b.meanGreen) * 0.10f +
        std::fabs(a.meanBlue - b.meanBlue) * 0.10f +
        std::fabs(a.warmCoolBias - b.warmCoolBias) * 0.05f +
        std::fabs(a.magentaGreenBias - b.magentaGreenBias) * 0.12f +
        std::fabs(a.channelImbalance - b.channelImbalance) * 0.08f +
        std::fabs(a.colorCastRisk - b.colorCastRisk) * 0.08f +
        std::fabs(a.meanSaturation - b.meanSaturation) * 0.28f +
        std::fabs(a.lowSaturationFraction - b.lowSaturationFraction) * 0.18f +
        std::fabs(a.highlightBandFraction - b.highlightBandFraction) * 0.18f +
        std::fabs(a.highlightMeanLuma - b.highlightMeanLuma) * 0.16f +
        std::fabs(a.highlightLowSaturationFraction - b.highlightLowSaturationFraction) * 0.12f +
        std::fabs(a.highlightGrayRisk - b.highlightGrayRisk) * 0.22f +
        std::fabs(a.highlightTileCoverage - b.highlightTileCoverage) * 0.12f +
        std::fabs(a.highlightStructureScore - b.highlightStructureScore) * 0.10f +
        std::fabs(a.meaningfulHighlightPressure - b.meaningfulHighlightPressure) * 0.18f +
        std::fabs(a.haloRiskFraction - b.haloRiskFraction) * 0.40f +
        std::fabs(a.shadowTextureRisk - b.shadowTextureRisk) * 0.18f +
        localMeanDistance * 0.35f +
        localContrastDistance * 0.12f +
        localDamageRiskDistance * 0.16f +
        std::fabs(a.localLumaSpread - b.localLumaSpread) * 0.20f +
        std::fabs(a.localEvSpreadStops - b.localEvSpreadStops) * 0.035f +
        std::fabs(a.localEvConflict - b.localEvConflict) * 0.18f +
        std::fabs(a.localHighlightPressure - b.localHighlightPressure) * 0.22f +
        std::fabs(a.localShadowPressure - b.localShadowPressure) * 0.14f +
        std::fabs(a.localDamageRiskPeak - b.localDamageRiskPeak) * 0.18f +
        std::fabs(a.localDamageRiskMean - b.localDamageRiskMean) * 0.10f +
        std::fabs(a.localExposureHighlightCrowding - b.localExposureHighlightCrowding) * 0.10f +
        std::fabs(a.localExposureShadowCrowding - b.localExposureShadowCrowding) * 0.10f +
        std::fabs(a.localExposureHaloStress - b.localExposureHaloStress) * 0.14f +
        std::fabs(a.localExposureFlatnessRisk - b.localExposureFlatnessRisk) * 0.10f +
        std::fabs(a.localExposureDamageRisk - b.localExposureDamageRisk) * 0.16f +
        std::fabs(a.subjectCenterPrior - b.subjectCenterPrior) * 0.08f +
        std::fabs(a.subjectReadabilityPressure - b.subjectReadabilityPressure) * 0.12f +
        std::fabs(a.subjectProtectionPressure - b.subjectProtectionPressure) * 0.12f +
        std::fabs(a.subjectMoodPreservationPressure - b.subjectMoodPreservationPressure) * 0.10f +
        std::fabs(a.subjectImportanceConfidence - b.subjectImportanceConfidence) * 0.08f +
        std::fabs(a.centerMeanLuma - b.centerMeanLuma) * 0.18f +
        std::fabs(a.subjectMarkedCoverage - b.subjectMarkedCoverage) * 0.08f +
        std::fabs(a.subjectMarkedPositiveCoverage - b.subjectMarkedPositiveCoverage) * 0.10f +
        std::fabs(a.subjectMarkedMeanLuma - b.subjectMarkedMeanLuma) * 0.16f +
        std::fabs(a.subjectMarkedReadabilityScore - b.subjectMarkedReadabilityScore) * 0.12f +
        std::fabs(a.subjectMarkedProtectionRisk - b.subjectMarkedProtectionRisk) * 0.14f +
        std::fabs(a.subjectMarkedMoodPreservationScore - b.subjectMarkedMoodPreservationScore) * 0.08f +
        std::fabs(a.subjectMarkedLowPriorityPressure - b.subjectMarkedLowPriorityPressure) * 0.08f;
}

bool EditorRenderWorker::Initialize(GLFWwindow* sharedWindow) {
    if (m_Thread.joinable()) {
        return true;
    }

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    m_WorkerWindow = glfwCreateWindow(16, 16, "Stack Editor Render Worker", nullptr, sharedWindow);
    if (!m_WorkerWindow) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_StopRequested = false;
        m_HasPending = false;
        m_InitComplete = false;
        m_InitSucceeded = false;
        m_InitError.clear();
        m_Busy = false;
    }
    m_Thread = std::thread([this]() { ThreadMain(); });

    {
        std::unique_lock<std::mutex> lock(m_Mutex);
        m_Cv.wait(lock, [this]() {
            return m_InitComplete;
        });
        if (m_InitSucceeded) {
            return true;
        }
        std::cerr << "[EditorRenderWorker] Failed to initialize worker pipeline";
        if (!m_InitError.empty()) {
            std::cerr << ": " << m_InitError;
        }
        std::cerr << "\n";
        m_StopRequested = true;
    }

    m_Cv.notify_all();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
    if (m_WorkerWindow) {
        glfwDestroyWindow(m_WorkerWindow);
        m_WorkerWindow = nullptr;
    }
    return false;
}

void EditorRenderWorker::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_StopRequested = true;
    }
    m_Cv.notify_all();
    if (m_Thread.joinable()) {
        m_Thread.join();
    }
    if (m_WorkerWindow) {
        glfwDestroyWindow(m_WorkerWindow);
        m_WorkerWindow = nullptr;
    }
}

void EditorRenderWorker::Submit(Snapshot snapshot) {
    const int progressTotal = EstimateRenderProgressStepCount(snapshot);
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const bool replacingInFlightWork = m_Busy.load() || m_HasPending;
        m_Pending = std::move(snapshot);
        m_HasPending = true;
        m_ProgressCompletedSteps = 0;
        m_ProgressTotalSteps = progressTotal;
        m_ProgressLabel = replacingInFlightWork ? "Queued newer render..." : "Queued render...";
    }
    m_Cv.notify_one();
}

bool EditorRenderWorker::TryConsumeCompleted(Result& result) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Completed.empty()) {
        return false;
    }
    result = std::move(m_Completed.front());
    m_Completed.pop();
    return true;
}

EditorRenderWorker::RenderProgress EditorRenderWorker::GetProgress() const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    RenderProgress progress;
    progress.busy = m_Busy.load() || m_HasPending;
    progress.completedSteps = m_ProgressCompletedSteps;
    progress.totalSteps = m_ProgressTotalSteps;
    progress.label = m_ProgressLabel;
    return progress;
}

void EditorRenderWorker::SetProgress(int completedSteps, int totalSteps, std::string label) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ProgressCompletedSteps = std::max(0, completedSteps);
    m_ProgressTotalSteps = std::max(1, totalSteps);
    m_ProgressLabel = std::move(label);
}

void EditorRenderWorker::AdvanceProgress(std::string label) {
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_ProgressCompletedSteps = std::min(m_ProgressTotalSteps, m_ProgressCompletedSteps + 1);
    if (!label.empty()) {
        m_ProgressLabel = std::move(label);
    }
}

bool EditorRenderWorker::ShouldAbortStaleSnapshotForValidation(
    std::uint64_t currentGeneration,
    bool stopRequested,
    bool hasPendingSnapshot,
    std::uint64_t pendingGeneration) {
    return stopRequested || (hasPendingSnapshot && pendingGeneration > currentGeneration);
}

std::string EditorRenderWorker::BuildDevelopCandidateProgressLabelForValidation(
    const std::string& candidateLabel,
    const std::string& candidateRevisionStage,
    int candidateIndex,
    int candidateCount) {
    return BuildDevelopCandidateProgressLabel(
        candidateLabel,
        candidateRevisionStage,
        candidateIndex,
        candidateCount);
}

bool EditorRenderWorker::ShouldAbortStaleSnapshot(std::uint64_t currentGeneration) const {
    std::lock_guard<std::mutex> lock(m_Mutex);
    return ShouldAbortStaleSnapshotForValidation(
        currentGeneration,
        m_StopRequested,
        m_HasPending,
        m_Pending.generation);
}

void EditorRenderWorker::ThreadMain() {
    bool initSucceeded = false;
    std::string initError;
    try {
        glfwMakeContextCurrent(m_WorkerWindow);
        if (!LoadGLFunctions()) {
            initError = "OpenGL function loading failed";
        } else {
            m_PersistentPipeline = std::make_unique<RenderPipeline>();
            m_PersistentPipeline->Initialize();
            initSucceeded = true;
        }
    } catch (const std::exception& e) {
        initError = e.what();
    } catch (...) {
        initError = "unknown exception";
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_InitSucceeded = initSucceeded;
        m_InitError = initError;
        m_InitComplete = true;
    }
    m_Cv.notify_all();

    if (!initSucceeded) {
        m_PersistentPipeline.reset();
        glfwMakeContextCurrent(nullptr);
        return;
    }

    while (true) {
        Snapshot snapshot;
        {
            std::unique_lock<std::mutex> lock(m_Mutex);
            m_Cv.wait(lock, [this]() {
                return m_StopRequested || m_HasPending;
            });
            if (m_StopRequested) {
                break;
            }
            snapshot = std::move(m_Pending);
            m_HasPending = false;
            m_Busy = true;
        }
        SetProgress(0, EstimateRenderProgressStepCount(snapshot), "Starting render...");

        Result result = RenderSnapshot(snapshot);
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            while (!m_Completed.empty()) {
                Result stale = std::move(m_Completed.front());
                m_Completed.pop();
                ReleaseResultResources(stale);
            }
            m_Completed.push(std::move(result));
            m_Busy = m_HasPending;
            if (!m_Busy.load()) {
                m_ProgressCompletedSteps = m_ProgressTotalSteps;
                m_ProgressLabel = "Render ready.";
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        while (!m_Completed.empty()) {
            Result stale = std::move(m_Completed.front());
            m_Completed.pop();
            ReleaseResultResources(stale);
        }
        m_ProgressCompletedSteps = 0;
        m_ProgressTotalSteps = 0;
        m_ProgressLabel.clear();
        m_Busy = false;
    }

    m_PersistentPipeline.reset();
    glfwMakeContextCurrent(nullptr);
}

EditorRenderWorker::Result EditorRenderWorker::RenderSnapshot(const Snapshot& snapshot) {
    Result result;
    result.generation = snapshot.generation;

    try {
        if (!m_PersistentPipeline) {
            m_PersistentPipeline = std::make_unique<RenderPipeline>();
            m_PersistentPipeline->Initialize();
        }
        RenderPipeline& pipeline = *m_PersistentPipeline;
        pipeline.SetPreviewMaxDimension(snapshot.previewMaxDimension);
        const int totalProgressSteps = EstimateRenderProgressStepCount(snapshot);
        int progressCompleted = 0;
        auto reportProgress = [&](std::string label) {
            SetProgress(progressCompleted, totalProgressSteps, std::move(label));
        };
        auto finishProgressStep = [&](std::string label = {}) {
            progressCompleted = std::min(totalProgressSteps, progressCompleted + 1);
            SetProgress(progressCompleted, totalProgressSteps, std::move(label));
        };
        auto shouldAbortStaleWork = [&]() {
            return ShouldAbortStaleSnapshot(snapshot.generation);
        };
        auto reportSupersededWork = [&]() {
            reportProgress("Newer render queued; skipping stale feedback...");
        };
        reportProgress("Preparing render...");

        auto renderDevelopCandidateRequests = [&]() {
            if (snapshot.developCandidateRenders.empty()) {
                return;
            }
            if (shouldAbortStaleWork()) {
                reportSupersededWork();
                return;
            }

            result.developCandidateRenders.reserve(snapshot.developCandidateRenders.size());
            const unsigned char* sourceData = snapshot.sourcePixels.empty() ? nullptr : snapshot.sourcePixels.data();
            int candidateIndex = 0;
            const int candidateCount = static_cast<int>(snapshot.developCandidateRenders.size());
            if (snapshot.width <= 0 || snapshot.height <= 0) {
                for (const DevelopCandidateRenderRequest& request : snapshot.developCandidateRenders) {
                    if (shouldAbortStaleWork()) {
                        reportSupersededWork();
                        break;
                    }
                    reportProgress(BuildDevelopCandidateProgressLabel(
                        request.candidateLabel,
                        request.candidateRevisionStage,
                        candidateIndex,
                        candidateCount));
                    DevelopCandidateRenderResult candidateResult;
                    candidateResult.developNodeId = request.developNodeId;
                    candidateResult.candidateId = request.candidateId;
                    candidateResult.candidateLabel = request.candidateLabel;
                    candidateResult.candidateRevisionStage = request.candidateRevisionStage;
                    candidateResult.activeRevisionStage = request.activeRevisionStage;
                    candidateResult.activeRefineIntent = request.activeRefineIntent;
                    candidateResult.stageSchedulerExpectedDirtyBoundary = request.stageSchedulerExpectedDirtyBoundary;
                    candidateResult.stageSchedulerReason = request.stageSchedulerReason;
                    candidateResult.dirtyGeneration = request.dirtyGeneration;
                    candidateResult.solveFingerprint = request.solveFingerprint;
                    candidateResult.rawDevelopInteractionSerial = request.rawDevelopInteractionSerial;
                    candidateResult.guidanceFingerprint = request.guidanceFingerprint;
                    candidateResult.solveScore = request.solveScore;
                    candidateResult.stageSchedulerOrder = request.stageSchedulerOrder;
                    candidateResult.stageSchedulerRank = request.stageSchedulerRank;
                    candidateResult.adaptiveRenderBudget = request.adaptiveRenderBudget;
                    candidateResult.adaptiveRenderBudgetVersion = request.adaptiveRenderBudgetVersion;
                    candidateResult.adaptiveRenderBudgetReason = request.adaptiveRenderBudgetReason;
                    candidateResult.adaptiveRenderBudgetContinuationDecision =
                        request.adaptiveRenderBudgetContinuationDecision;
                    candidateResult.adaptiveRenderBudgetConvergenceState =
                        request.adaptiveRenderBudgetConvergenceState;
                    candidateResult.adaptiveRenderBudgetConvergenceDecision =
                        request.adaptiveRenderBudgetConvergenceDecision;
                    candidateResult.adaptiveRenderBudgetConvergenceReason =
                        request.adaptiveRenderBudgetConvergenceReason;
                    candidateResult.activeStageMatch = request.activeStageMatch;
                    candidateResult.stageReservedRequest = request.stageReservedRequest;
                    candidateResult.activeRefineIntentMatch = request.activeRefineIntentMatch;
                    candidateResult.refineIntentReservedRequest = request.refineIntentReservedRequest;
                    candidateResult.adaptiveRenderBudgetExpanded =
                        request.adaptiveRenderBudgetExpanded;
                    candidateResult.adaptiveRenderBudgetNarrowed =
                        request.adaptiveRenderBudgetNarrowed;
                    candidateResult.metricReadbackMaxDimension = request.metricReadbackMaxDimension;
                    candidateResult.error = "No source image for candidate render.";
                    result.developCandidateRenders.push_back(std::move(candidateResult));
                    ++candidateIndex;
                    finishProgressStep("Develop feedback measured.");
                }
                return;
            }

            for (const DevelopCandidateRenderRequest& request : snapshot.developCandidateRenders) {
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                reportProgress(BuildDevelopCandidateProgressLabel(
                    request.candidateLabel,
                    request.candidateRevisionStage,
                    candidateIndex,
                    candidateCount));
                DevelopCandidateRenderResult candidateResult;
                candidateResult.developNodeId = request.developNodeId;
                candidateResult.candidateId = request.candidateId;
                candidateResult.candidateLabel = request.candidateLabel;
                candidateResult.candidateRevisionStage = request.candidateRevisionStage;
                candidateResult.activeRevisionStage = request.activeRevisionStage;
                candidateResult.activeRefineIntent = request.activeRefineIntent;
                candidateResult.stageSchedulerExpectedDirtyBoundary = request.stageSchedulerExpectedDirtyBoundary;
                candidateResult.stageSchedulerReason = request.stageSchedulerReason;
                candidateResult.dirtyGeneration = request.dirtyGeneration;
                candidateResult.solveFingerprint = request.solveFingerprint;
                candidateResult.rawDevelopInteractionSerial = request.rawDevelopInteractionSerial;
                candidateResult.guidanceFingerprint = request.guidanceFingerprint;
                candidateResult.solveScore = request.solveScore;
                candidateResult.stageSchedulerOrder = request.stageSchedulerOrder;
                candidateResult.stageSchedulerRank = request.stageSchedulerRank;
                candidateResult.adaptiveRenderBudget = request.adaptiveRenderBudget;
                candidateResult.adaptiveRenderBudgetVersion = request.adaptiveRenderBudgetVersion;
                candidateResult.adaptiveRenderBudgetReason = request.adaptiveRenderBudgetReason;
                candidateResult.adaptiveRenderBudgetContinuationDecision =
                    request.adaptiveRenderBudgetContinuationDecision;
                candidateResult.adaptiveRenderBudgetConvergenceState =
                    request.adaptiveRenderBudgetConvergenceState;
                candidateResult.adaptiveRenderBudgetConvergenceDecision =
                    request.adaptiveRenderBudgetConvergenceDecision;
                candidateResult.adaptiveRenderBudgetConvergenceReason =
                    request.adaptiveRenderBudgetConvergenceReason;
                candidateResult.activeStageMatch = request.activeStageMatch;
                candidateResult.stageReservedRequest = request.stageReservedRequest;
                candidateResult.activeRefineIntentMatch = request.activeRefineIntentMatch;
                candidateResult.refineIntentReservedRequest = request.refineIntentReservedRequest;
                candidateResult.adaptiveRenderBudgetExpanded =
                    request.adaptiveRenderBudgetExpanded;
                candidateResult.adaptiveRenderBudgetNarrowed =
                    request.adaptiveRenderBudgetNarrowed;
                candidateResult.metricReadbackMaxDimension = request.metricReadbackMaxDimension;

                auto nodeIt = std::find_if(
                    snapshot.graph.nodes.begin(),
                    snapshot.graph.nodes.end(),
                    [&request](const RenderGraphNode& node) {
                        return node.nodeId == request.developNodeId &&
                               node.kind == RenderGraphNodeKind::RawDevelop;
                    });
                if (nodeIt == snapshot.graph.nodes.end()) {
                    candidateResult.error = "Develop node was not present in the render snapshot.";
                    result.developCandidateRenders.push_back(std::move(candidateResult));
                    continue;
                }

                const std::size_t developNodeIndex =
                    static_cast<std::size_t>(std::distance(snapshot.graph.nodes.begin(), nodeIt));
                RenderGraphSnapshot candidateGraph = snapshot.graph;
                if (developNodeIndex >= candidateGraph.nodes.size()) {
                    candidateResult.error = "Develop node was not present in the copied render snapshot.";
                    result.developCandidateRenders.push_back(std::move(candidateResult));
                    continue;
                }
                RenderGraphNode& candidateNode = candidateGraph.nodes[developNodeIndex];
                candidateNode.rawDevelop = request.rawDevelop;
                candidateNode.requestRevision = std::max<std::uint64_t>(1, request.dirtyGeneration);

                const int syntheticOutputId =
                    -200000 - static_cast<int>(result.developCandidateRenders.size()) - 1;
                const int syntheticPreFinishOutputId = syntheticOutputId - 100000;
                RenderGraphNode finalOutputNode;
                finalOutputNode.nodeId = syntheticOutputId;
                finalOutputNode.kind = RenderGraphNodeKind::Output;
                candidateGraph.nodes.push_back(std::move(finalOutputNode));
                candidateGraph.links.push_back(RenderGraphLink{
                    request.developNodeId,
                    EditorNodeGraph::kImageOutputSocketId,
                    syntheticOutputId,
                    EditorNodeGraph::kImageInputSocketId
                });

                RenderGraphNode preFinishOutputNode;
                preFinishOutputNode.nodeId = syntheticPreFinishOutputId;
                preFinishOutputNode.kind = RenderGraphNodeKind::Output;
                candidateGraph.nodes.push_back(std::move(preFinishOutputNode));
                candidateGraph.links.push_back(RenderGraphLink{
                    request.developNodeId,
                    EditorNodeGraph::kPreFinishImageOutputSocketId,
                    syntheticPreFinishOutputId,
                    EditorNodeGraph::kImageInputSocketId
                });
                candidateGraph.outputSocketId.clear();

                auto renderCandidateSocket = [&](
                    int syntheticOutputId,
                    int& outWidth,
                    int& outHeight,
                    bool* outRawBaseCacheHit = nullptr,
                    bool* outPreFinishCacheHit = nullptr,
                    int metricReadbackMaxDimension = 0,
                    float* outGraphMs = nullptr,
                    float* outReadbackMs = nullptr) {
                    candidateGraph.outputNodeId = syntheticOutputId;

                    pipeline.LoadSourceFromPixels(sourceData, snapshot.width, snapshot.height, snapshot.channels);
                    const auto graphBegin = std::chrono::steady_clock::now();
                    pipeline.ExecuteGraph(candidateGraph);
                    const auto graphEnd = std::chrono::steady_clock::now();
                    if (outGraphMs) {
                        *outGraphMs = MillisecondsBetween(graphBegin, graphEnd);
                    }
                    if (outRawBaseCacheHit) {
                        *outRawBaseCacheHit = pipeline.WasGraphImageCacheHit(
                            request.developNodeId,
                            "__rawDevelopBase");
                    }
                    if (outPreFinishCacheHit) {
                        *outPreFinishCacheHit = pipeline.WasGraphImageCacheHit(
                            request.developNodeId,
                            EditorNodeGraph::kPreFinishImageOutputSocketId);
                    }
                    const auto readbackBegin = std::chrono::steady_clock::now();
                    std::vector<unsigned char> pixels = metricReadbackMaxDimension > 0
                        ? pipeline.GetOutputPixels(outWidth, outHeight, metricReadbackMaxDimension)
                        : pipeline.GetOutputPixels(outWidth, outHeight);
                    const auto readbackEnd = std::chrono::steady_clock::now();
                    if (outReadbackMs) {
                        *outReadbackMs = MillisecondsBetween(readbackBegin, readbackEnd);
                    }
                    return pixels;
                };

                const auto candidateBegin = std::chrono::steady_clock::now();
                std::vector<unsigned char> candidatePixels = renderCandidateSocket(
                    syntheticOutputId,
                    candidateResult.width,
                    candidateResult.height,
                    &candidateResult.rawBaseCacheHitDuringFinalRender,
                    &candidateResult.preFinishCacheHitDuringFinalRender,
                    request.metricReadbackMaxDimension,
                    &candidateResult.finalGraphMs,
                    &candidateResult.finalReadbackMs);
                candidateResult.success =
                    !candidatePixels.empty() &&
                    candidateResult.width > 0 &&
                    candidateResult.height > 0;
                candidateResult.metricsReadbackDownsampled =
                    candidateResult.success &&
                    request.metricReadbackMaxDimension > 0 &&
                    std::max(snapshot.width, snapshot.height) > request.metricReadbackMaxDimension;
                if (candidateResult.success) {
                    const auto analysisBegin = std::chrono::steady_clock::now();
                    candidateResult.metrics = AnalyzeDevelopCandidatePixels(
                        candidatePixels,
                        candidateResult.width,
                        candidateResult.height,
                        &request.subjectSampling);
                    const auto analysisEnd = std::chrono::steady_clock::now();
                    candidateResult.finalAnalysisMs =
                        MillisecondsBetween(analysisBegin, analysisEnd);
                } else {
                    candidateResult.error = "Candidate render produced no pixels.";
                }
                if (request.measurePreFinish && !shouldAbortStaleWork()) {
                    const auto preFinishReadbackBegin = std::chrono::steady_clock::now();
                    std::vector<unsigned char> preFinishPixels =
                        pipeline.GetCachedGraphImagePixels(
                            request.developNodeId,
                            EditorNodeGraph::kPreFinishImageOutputSocketId,
                            candidateResult.preFinishWidth,
                            candidateResult.preFinishHeight,
                            request.metricReadbackMaxDimension);
                    const auto preFinishReadbackEnd = std::chrono::steady_clock::now();
                    candidateResult.preFinishReadbackMs =
                        MillisecondsBetween(preFinishReadbackBegin, preFinishReadbackEnd);
                    candidateResult.preFinishReusedFromFinalRender = !preFinishPixels.empty();
                    if (preFinishPixels.empty()) {
                        preFinishPixels = renderCandidateSocket(
                            syntheticPreFinishOutputId,
                            candidateResult.preFinishWidth,
                            candidateResult.preFinishHeight,
                            nullptr,
                            nullptr,
                            request.metricReadbackMaxDimension,
                            &candidateResult.preFinishGraphMs,
                            &candidateResult.preFinishReadbackMs);
                    }
                    candidateResult.preFinishSuccess =
                        !preFinishPixels.empty() &&
                        candidateResult.preFinishWidth > 0 &&
                        candidateResult.preFinishHeight > 0;
                    candidateResult.preFinishMetricsReadbackDownsampled =
                        candidateResult.preFinishSuccess &&
                        request.metricReadbackMaxDimension > 0 &&
                        std::max(snapshot.width, snapshot.height) > request.metricReadbackMaxDimension;
                    if (candidateResult.preFinishSuccess) {
                        const auto preFinishAnalysisBegin = std::chrono::steady_clock::now();
                        candidateResult.preFinishMetrics = AnalyzeDevelopCandidatePixels(
                            preFinishPixels,
                            candidateResult.preFinishWidth,
                            candidateResult.preFinishHeight,
                            &request.subjectSampling);
                        const auto preFinishAnalysisEnd = std::chrono::steady_clock::now();
                        candidateResult.preFinishAnalysisMs =
                            MillisecondsBetween(preFinishAnalysisBegin, preFinishAnalysisEnd);
                    }
                }
                const auto candidateEnd = std::chrono::steady_clock::now();
                candidateResult.totalElapsedMs =
                    MillisecondsBetween(candidateBegin, candidateEnd);
                result.developCandidateRenders.push_back(std::move(candidateResult));
                ++candidateIndex;
                finishProgressStep("Develop feedback measured.");
            }
        };

        auto renderPreviewRequests = [&]() {
            if (snapshot.previews.empty()) {
                return;
            }
            if (shouldAbortStaleWork()) {
                reportSupersededWork();
                return;
            }

            result.previews.reserve(snapshot.previews.size());
            int previewIndex = 0;
            const int previewCount = static_cast<int>(snapshot.previews.size());
            for (const PreviewRequest& request : snapshot.previews) {
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                reportProgress(
                    "Rendering preview " +
                    std::to_string(previewIndex + 1) +
                    "/" +
                    std::to_string(previewCount) +
                    "...");
                PreviewResult previewResult;
                previewResult.previewNodeId = request.previewNodeId;
                previewResult.dirtyGeneration = request.dirtyGeneration;

                const unsigned char* sourceData = request.sourcePixels.empty() ? nullptr : request.sourcePixels.data();
                int sourceWidth = request.width;
                int sourceHeight = request.height;
                int sourceChannels = request.channels;
                if ((!sourceData || sourceWidth <= 0 || sourceHeight <= 0) && request.sourceNodeId > 0) {
                    const auto sourceIt = std::find_if(
                        snapshot.graph.nodes.begin(),
                        snapshot.graph.nodes.end(),
                        [&request](const RenderGraphNode& node) { return node.nodeId == request.sourceNodeId; });
                    if (sourceIt != snapshot.graph.nodes.end() &&
                        sourceIt->kind == RenderGraphNodeKind::Image &&
                        !sourceIt->image.pixels.empty() &&
                        sourceIt->image.width > 0 &&
                        sourceIt->image.height > 0) {
                        sourceData = sourceIt->image.pixels.data();
                        sourceWidth = sourceIt->image.width;
                        sourceHeight = sourceIt->image.height;
                        sourceChannels = std::max(1, sourceIt->image.channels);
                    }
                }
                if (!sourceData || sourceWidth <= 0 || sourceHeight <= 0) {
                    previewResult.error = "No source image.";
                    result.previews.push_back(std::move(previewResult));
                    ++previewIndex;
                    finishProgressStep("Preview skipped.");
                    continue;
                }

                pipeline.LoadSourceFromPixels(sourceData, sourceWidth, sourceHeight, sourceChannels);
                RenderGraphSnapshot graph = snapshot.graph;
                if (request.directSourceOutput || request.maskInput) {
                    graph.outputNodeId = request.sourceNodeId;
                    graph.outputSocketId = request.sourceSocketId;
                } else {
                    const int syntheticOutputId = -100000 - request.previewNodeId;
                    RenderGraphNode outputNode;
                    outputNode.nodeId = syntheticOutputId;
                    outputNode.kind = RenderGraphNodeKind::Output;
                    graph.nodes.push_back(std::move(outputNode));
                    graph.links.push_back(RenderGraphLink{
                        request.sourceNodeId,
                        request.sourceSocketId,
                        syntheticOutputId,
                        EditorNodeGraph::kImageInputSocketId
                    });
                    graph.outputNodeId = syntheticOutputId;
                }
                pipeline.ExecuteGraph(graph);
                const std::vector<ToneCurveAutoRewriteFeedback>& previewFeedback = pipeline.GetToneCurveAutoRewriteFeedback();
                result.toneCurveAutoRewrites.insert(
                    result.toneCurveAutoRewrites.end(),
                    previewFeedback.begin(),
                    previewFeedback.end());
                previewResult.pixels = pipeline.GetPreviewPixels(previewResult.width, previewResult.height, 512);
                previewResult.success = !previewResult.pixels.empty();
                if (!previewResult.success) {
                    previewResult.error = "Preview produced no pixels.";
                }
                result.previews.push_back(std::move(previewResult));
                ++previewIndex;
                finishProgressStep("Preview rendered.");
            }
        };

        if (!snapshot.compositeOutputs.empty()) {
            result.success = true;
            result.compositeOutputs.reserve(snapshot.compositeOutputs.size());
            int compositeIndex = 0;
            const int compositeCount = static_cast<int>(snapshot.compositeOutputs.size());
            for (const CompositeOutputRequest& request : snapshot.compositeOutputs) {
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                reportProgress(
                    "Rendering canvas output " +
                    std::to_string(compositeIndex + 1) +
                    "/" +
                    std::to_string(compositeCount) +
                    "...");
                CompositeOutputResult outputResult;
                outputResult.outputNodeId = request.outputNodeId;
                outputResult.dirtyGeneration = request.dirtyGeneration;
                outputResult.chainFingerprint = request.chainFingerprint;
                const unsigned char* sourceData = request.sourcePixels.empty() ? nullptr : request.sourcePixels.data();
                int sourceWidth = request.width;
                int sourceHeight = request.height;
                int sourceChannels = request.channels;
                if ((!sourceData || sourceWidth <= 0 || sourceHeight <= 0) && request.sourceNodeId > 0) {
                    const auto sourceIt = std::find_if(
                        snapshot.graph.nodes.begin(),
                        snapshot.graph.nodes.end(),
                        [&request](const RenderGraphNode& node) { return node.nodeId == request.sourceNodeId; });
                    if (sourceIt != snapshot.graph.nodes.end() &&
                        sourceIt->kind == RenderGraphNodeKind::Image &&
                        !sourceIt->image.pixels.empty() &&
                        sourceIt->image.width > 0 &&
                        sourceIt->image.height > 0) {
                        sourceData = sourceIt->image.pixels.data();
                        sourceWidth = sourceIt->image.width;
                        sourceHeight = sourceIt->image.height;
                        sourceChannels = std::max(1, sourceIt->image.channels);
                    }
                }
                if (!sourceData || sourceWidth <= 0 || sourceHeight <= 0) {
                    outputResult.error = "No source image.";
                    result.success = false;
                    result.compositeOutputs.push_back(std::move(outputResult));
                    ++compositeIndex;
                    finishProgressStep("Canvas output skipped.");
                    continue;
                }

                pipeline.LoadSourceFromPixels(
                    sourceData,
                    sourceWidth,
                    sourceHeight,
                    sourceChannels);
                RenderGraphSnapshot graph = snapshot.graph;
                graph.outputNodeId = request.outputNodeId;
                pipeline.ExecuteGraph(graph);
                const std::vector<ToneCurveAutoRewriteFeedback>& compositeFeedback = pipeline.GetToneCurveAutoRewriteFeedback();
                result.toneCurveAutoRewrites.insert(
                    result.toneCurveAutoRewrites.end(),
                    compositeFeedback.begin(),
                    compositeFeedback.end());
                if (shouldAbortStaleWork()) {
                    reportSupersededWork();
                    break;
                }
                outputResult.pixels = pipeline.GetOutputPixels(outputResult.width, outputResult.height);
                outputResult.success = !outputResult.pixels.empty();
                if (!outputResult.success) {
                    outputResult.error = "Render produced no pixels.";
                    result.success = false;
                }
                result.compositeOutputs.push_back(std::move(outputResult));
                ++compositeIndex;
                finishProgressStep("Canvas output rendered.");
            }
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }

        if (!snapshot.outputConnected) {
            result.success = true;
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }
        if (snapshot.width <= 0 || snapshot.height <= 0) {
            result.error = "No source image.";
            finishProgressStep("No source image.");
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }
        const unsigned char* sourceData = snapshot.sourcePixels.empty() ? nullptr : snapshot.sourcePixels.data();
        pipeline.LoadSourceFromPixels(sourceData, snapshot.width, snapshot.height, snapshot.channels);

        if (!snapshot.graph.nodes.empty()) {
            reportProgress("Rendering main output...");
            pipeline.ExecuteGraph(snapshot.graph);
            result.toneCurveAutoRewrites = pipeline.GetToneCurveAutoRewriteFeedback();
            if (shouldAbortStaleWork()) {
                finishProgressStep("Newer render queued.");
                result.error = "Render superseded by a newer snapshot.";
                return result;
            }
            result.outputTexture.texture = pipeline.PublishSharedOutputTexture(result.outputTexture.width, result.outputTexture.height);
            if (result.outputTexture.texture != 0) {
                result.outputTexture.readyFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                glFlush();
            }
            result.success = result.outputTexture.texture != 0;
            if (!result.success) {
                result.error = "Render produced no pixels.";
            }
            finishProgressStep("Main output rendered.");
            renderDevelopCandidateRequests();
            renderPreviewRequests();
            return result;
        }

        std::vector<RenderLayerStep> steps;
        steps.reserve(snapshot.layerSteps.empty() ? snapshot.layers.size() : snapshot.layerSteps.size());

        const auto addLayerStep = [&](const nlohmann::json& layerJson, int maskNodeId) {
            const std::string type = layerJson.value("type", std::string());
            std::shared_ptr<LayerBase> layer = LayerRegistry::CreateLayerFromTypeId(type);
            if (!layer) {
                return;
            }
            layer->InitializeGL();
            layer->Deserialize(layerJson);
            RenderLayerStep step;
            step.layer = std::move(layer);
            step.maskNodeId = maskNodeId;
            steps.push_back(std::move(step));
        };

        if (!snapshot.layerSteps.empty()) {
            for (const nlohmann::json& stepJson : snapshot.layerSteps) {
                if (!stepJson.is_object()) {
                    continue;
                }
                addLayerStep(stepJson.value("layer", nlohmann::json::object()), stepJson.value("maskNodeId", -1));
            }
        } else {
            for (const nlohmann::json& layerJson : snapshot.layers) {
                addLayerStep(layerJson, -1);
            }
        }

        reportProgress("Rendering layer stack...");
        pipeline.ExecuteMasked(steps, snapshot.masks);
        if (shouldAbortStaleWork()) {
            finishProgressStep("Newer render queued.");
            result.error = "Render superseded by a newer snapshot.";
            return result;
        }
        result.outputTexture.texture = pipeline.PublishSharedOutputTexture(result.outputTexture.width, result.outputTexture.height);
        if (result.outputTexture.texture != 0) {
            result.outputTexture.readyFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            glFlush();
        }
        result.success = result.outputTexture.texture != 0;
        if (!result.success) {
            result.error = "Render produced no pixels.";
        }
        finishProgressStep("Layer stack rendered.");
        renderDevelopCandidateRequests();
        renderPreviewRequests();
    } catch (const std::exception& e) {
        result.error = e.what();
    } catch (...) {
        result.error = "Unknown render worker failure.";
    }

    return result;
}
