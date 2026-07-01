#include "Raw/RawAutoBase.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <limits>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace Stack::RawAutoBase {
namespace {

constexpr float kLumaEpsilon = 1.0e-8f;

float SafeChannel(float value) {
    return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
}

float Lerp(float a, float b, float t) {
    return a + (b - a) * std::clamp(t, 0.0f, 1.0f);
}

float SmoothStep(float edge0, float edge1, float value) {
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0f : 0.0f;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float Luma(float r, float g, float b) {
    return 0.2126f * SafeChannel(r) +
        0.7152f * SafeChannel(g) +
        0.0722f * SafeChannel(b);
}

float SafeLog2Luma(float luma) {
    return std::log2(std::max(kLumaEpsilon, luma));
}

float Chroma(float r, float g, float b) {
    const float maximum = std::max({ SafeChannel(r), SafeChannel(g), SafeChannel(b) });
    const float minimum = std::min({ SafeChannel(r), SafeChannel(g), SafeChannel(b) });
    if (maximum <= kLumaEpsilon) {
        return 0.0f;
    }
    return std::clamp((maximum - minimum) / maximum, 0.0f, 1.0f);
}

float PercentileSorted(const std::vector<float>& sorted, float percentile, float fallback = 0.0f) {
    if (sorted.empty()) {
        return fallback;
    }
    const float clamped = std::clamp(percentile, 0.0f, 1.0f);
    const std::size_t index = static_cast<std::size_t>(
        std::round(clamped * static_cast<float>(sorted.size() - 1)));
    return sorted[index];
}

float Median(std::vector<float> values, float fallback = 0.0f) {
    if (values.empty()) {
        return fallback;
    }
    std::sort(values.begin(), values.end());
    return PercentileSorted(values, 0.5f, fallback);
}

struct PixelData {
    bool valid = false;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float luma = 0.0f;
    float ev = 0.0f;
    float chroma = 0.0f;
    float variance = 0.0f;
};

struct ComponentSummary {
    bool valid = false;
    float areaPercent = 0.0f;
    float medianEv = 0.0f;
    float p25Ev = 0.0f;
    float p70Ev = 0.0f;
    float medianR = 0.0f;
    float medianG = 0.0f;
    float medianB = 0.0f;
    float chromaMedian = 0.0f;
};

bool ImageLooksUsable(const LocalSuggestionAnalysisImage& image) {
    return image.valid &&
        image.sceneLinearBeforeLocalRange &&
        image.width > 0 &&
        image.height > 0 &&
        image.pixels.size() >= static_cast<std::size_t>(image.width) *
            static_cast<std::size_t>(image.height);
}

std::vector<PixelData> BuildPixelData(
    const LocalSuggestionAnalysisImage& image,
    std::vector<float>& sortedEvs,
    std::vector<float>& sortedLumas,
    int& validCount) {
    const int width = image.width;
    const int height = image.height;
    std::vector<PixelData> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    sortedEvs.clear();
    sortedLumas.clear();
    validCount = 0;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const LocalSuggestionPixel& sample = image.pixels[index];
            PixelData& pixel = pixels[index];
            pixel.valid = sample.valid &&
                std::isfinite(sample.r) &&
                std::isfinite(sample.g) &&
                std::isfinite(sample.b);
            if (!pixel.valid) {
                continue;
            }
            pixel.r = SafeChannel(sample.r);
            pixel.g = SafeChannel(sample.g);
            pixel.b = SafeChannel(sample.b);
            pixel.luma = Luma(pixel.r, pixel.g, pixel.b);
            pixel.ev = SafeLog2Luma(pixel.luma);
            pixel.chroma = Chroma(pixel.r, pixel.g, pixel.b);
            sortedEvs.push_back(pixel.ev);
            sortedLumas.push_back(pixel.luma);
            ++validCount;
        }
    }

    std::sort(sortedEvs.begin(), sortedEvs.end());
    std::sort(sortedLumas.begin(), sortedLumas.end());

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            PixelData& pixel = pixels[index];
            if (!pixel.valid) {
                continue;
            }
            float sum = 0.0f;
            float sumSq = 0.0f;
            int count = 0;
            for (int yy = std::max(0, y - 1); yy <= std::min(height - 1, y + 1); ++yy) {
                for (int xx = std::max(0, x - 1); xx <= std::min(width - 1, x + 1); ++xx) {
                    const PixelData& neighbor = pixels[static_cast<std::size_t>(yy * width + xx)];
                    if (!neighbor.valid) {
                        continue;
                    }
                    sum += neighbor.luma;
                    sumSq += neighbor.luma * neighbor.luma;
                    ++count;
                }
            }
            if (count > 1) {
                const float mean = sum / static_cast<float>(count);
                pixel.variance = std::max(0.0f, sumSq / static_cast<float>(count) - mean * mean);
            }
        }
    }

    return pixels;
}

float HueBlueCyanScore(const PixelData& pixel) {
    const float maximumGb = std::max(pixel.g, pixel.b);
    const float blueDominance = Remap01(pixel.b - std::max(pixel.r, pixel.g), 0.02f, 0.25f);
    const float cyanBalance = 1.0f -
        std::clamp(std::abs(pixel.g - pixel.b) / std::max(maximumGb, kLumaEpsilon), 0.0f, 1.0f);
    return std::max(
        blueDominance,
        0.6f * cyanBalance * Remap01(pixel.b - pixel.r, 0.02f, 0.25f));
}

ComponentSummary SummarizeMask(
    const std::vector<PixelData>& pixels,
    const std::vector<bool>& mask,
    int validCount) {
    ComponentSummary summary;
    if (validCount <= 0 || pixels.empty() || mask.size() != pixels.size()) {
        return summary;
    }

    std::vector<float> evs;
    std::vector<float> rs;
    std::vector<float> gs;
    std::vector<float> bs;
    std::vector<float> chromas;
    evs.reserve(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i] || !pixels[i].valid) {
            continue;
        }
        evs.push_back(pixels[i].ev);
        rs.push_back(pixels[i].r);
        gs.push_back(pixels[i].g);
        bs.push_back(pixels[i].b);
        chromas.push_back(pixels[i].chroma);
    }
    if (evs.empty()) {
        return summary;
    }

    std::sort(evs.begin(), evs.end());
    summary.valid = true;
    summary.areaPercent = 100.0f * static_cast<float>(evs.size()) / static_cast<float>(validCount);
    summary.medianEv = PercentileSorted(evs, 0.5f);
    summary.p25Ev = PercentileSorted(evs, 0.25f);
    summary.p70Ev = PercentileSorted(evs, 0.70f);
    summary.medianR = Median(std::move(rs));
    summary.medianG = Median(std::move(gs));
    summary.medianB = Median(std::move(bs));
    summary.chromaMedian = Median(std::move(chromas));
    return summary;
}

void KeepConnectedComponents(
    std::vector<bool>& mask,
    int width,
    int height,
    int validCount,
    float minAreaPercent,
    bool requireTopTouch) {
    if (mask.empty() || width <= 0 || height <= 0 || validCount <= 0) {
        return;
    }
    std::vector<bool> keep(mask.size(), false);
    std::vector<bool> visited(mask.size(), false);
    std::vector<int> component;
    component.reserve(mask.size());
    std::deque<int> queue;
    const int minAreaPixels = static_cast<int>(std::ceil(
        minAreaPercent * 0.01f * static_cast<float>(validCount)));

    auto pushNeighbor = [&](int nx, int ny) {
        if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
            return;
        }
        const int n = ny * width + nx;
        if (!mask[static_cast<std::size_t>(n)] || visited[static_cast<std::size_t>(n)]) {
            return;
        }
        visited[static_cast<std::size_t>(n)] = true;
        queue.push_back(n);
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int start = y * width + x;
            if (!mask[static_cast<std::size_t>(start)] ||
                visited[static_cast<std::size_t>(start)]) {
                continue;
            }
            visited[static_cast<std::size_t>(start)] = true;
            queue.clear();
            component.clear();
            queue.push_back(start);
            bool touchesTop = false;
            while (!queue.empty()) {
                const int current = queue.front();
                queue.pop_front();
                component.push_back(current);
                const int cx = current % width;
                const int cy = current / width;
                touchesTop = touchesTop || cy == 0;
                pushNeighbor(cx - 1, cy);
                pushNeighbor(cx + 1, cy);
                pushNeighbor(cx, cy - 1);
                pushNeighbor(cx, cy + 1);
            }
            if (static_cast<int>(component.size()) >= minAreaPixels &&
                (!requireTopTouch || touchesTop)) {
                for (int index : component) {
                    keep[static_cast<std::size_t>(index)] = true;
                }
            }
        }
    }

    mask = std::move(keep);
}

std::vector<bool> BuildSkyMask(
    const std::vector<PixelData>& pixels,
    int width,
    int height,
    int validCount,
    float p50Ev,
    float p95Ev,
    float lowVar,
    float highVar) {
    std::vector<bool> mask(pixels.size(), false);
    for (int y = 0; y < height; ++y) {
        const float topPrior =
            1.0f - std::clamp(static_cast<float>(y) / std::max(1.0f, static_cast<float>(height) * 0.65f), 0.0f, 1.0f);
        for (int x = 0; x < width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * width + x);
            const PixelData& pixel = pixels[index];
            if (!pixel.valid) {
                continue;
            }
            const float brightPrior = Remap01(pixel.ev, p50Ev, p95Ev);
            const float smoothPrior = 1.0f - Remap01(pixel.variance, lowVar, highVar);
            const float skyScore =
                0.30f * topPrior +
                0.25f * brightPrior +
                0.25f * HueBlueCyanScore(pixel) +
                0.20f * smoothPrior;
            mask[index] = skyScore > 0.62f;
        }
    }
    KeepConnectedComponents(mask, width, height, validCount, 3.0f, true);
    return mask;
}

std::vector<bool> BuildFoliageMask(
    const std::vector<PixelData>& pixels,
    const std::vector<bool>& skyMask,
    int width,
    int height,
    int validCount,
    float textureMin) {
    std::vector<bool> mask(pixels.size(), false);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        const PixelData& pixel = pixels[i];
        if (!pixel.valid || (i < skyMask.size() && skyMask[i])) {
            continue;
        }
        const float greenDominance = pixel.g - std::max(pixel.r, pixel.b);
        const bool yellowGreen = pixel.g > pixel.b && pixel.g >= pixel.r * 0.75f;
        mask[i] =
            pixel.chroma > 0.10f &&
            greenDominance > 0.015f &&
            yellowGreen &&
            pixel.variance > textureMin;
    }
    KeepConnectedComponents(mask, width, height, validCount, 2.0f, false);
    return mask;
}

std::vector<bool> BuildShadowMask(
    const std::vector<PixelData>& pixels,
    float shadowThresholdEv) {
    std::vector<bool> mask(pixels.size(), false);
    for (std::size_t i = 0; i < pixels.size(); ++i) {
        mask[i] = pixels[i].valid && pixels[i].ev < shadowThresholdEv;
    }
    return mask;
}

float MedianEvForRegion(
    const std::vector<PixelData>& pixels,
    int width,
    int height,
    int x0,
    int y0,
    int x1,
    int y1,
    float fallback) {
    std::vector<float> evs;
    for (int y = std::max(0, y0); y < std::min(height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(width, x1); ++x) {
            const PixelData& pixel = pixels[static_cast<std::size_t>(y * width + x)];
            if (pixel.valid) {
                evs.push_back(pixel.ev);
            }
        }
    }
    return Median(std::move(evs), fallback);
}

float BrightTopAreaPercent(
    const std::vector<PixelData>& pixels,
    int width,
    int height,
    int validCount,
    float p95Ev) {
    if (validCount <= 0) {
        return 0.0f;
    }
    int brightCount = 0;
    const int topRows = std::max(1, static_cast<int>(std::round(static_cast<float>(height) * 0.35f)));
    for (int y = 0; y < topRows; ++y) {
        for (int x = 0; x < width; ++x) {
            const PixelData& pixel = pixels[static_cast<std::size_t>(y * width + x)];
            if (pixel.valid && pixel.ev >= p95Ev - 0.35f) {
                ++brightCount;
            }
        }
    }
    return 100.0f * static_cast<float>(brightCount) / static_cast<float>(validCount);
}

SuggestedLocalAdjustment MakeSuggestion(
    SuggestedLocalAdjustmentKind kind,
    float targetEv,
    float deltaEv,
    float widthEv,
    float feather,
    float confidence,
    float areaPercent,
    std::string label,
    std::string rationale) {
    SuggestedLocalAdjustment suggestion;
    suggestion.valid = true;
    suggestion.kind = kind;
    suggestion.targetEv = targetEv;
    suggestion.deltaEv = deltaEv;
    suggestion.widthEv = widthEv;
    suggestion.feather = feather;
    suggestion.confidence = std::clamp(confidence, 0.0f, 1.0f);
    suggestion.affectedAreaPercent = std::max(0.0f, areaPercent);
    suggestion.label = std::move(label);
    suggestion.rationale = std::move(rationale);
    return suggestion;
}

int SuggestionPriority(SuggestedLocalAdjustmentKind kind) {
    switch (kind) {
        case SuggestedLocalAdjustmentKind::OpenBacklitSubject: return 0;
        case SuggestedLocalAdjustmentKind::ProtectSky: return 1;
        case SuggestedLocalAdjustmentKind::OpenShadows: return 2;
        case SuggestedLocalAdjustmentKind::BrightenFoliage: return 3;
        case SuggestedLocalAdjustmentKind::RecoverHighlights: return 4;
        default: return 10;
    }
}

void AddSuggestion(std::vector<SuggestedLocalAdjustment>& suggestions, SuggestedLocalAdjustment suggestion) {
    if (!suggestion.valid || suggestion.confidence < 0.20f) {
        return;
    }
    suggestions.push_back(std::move(suggestion));
}

} // namespace

const char* SuggestedLocalAdjustmentKindLabel(SuggestedLocalAdjustmentKind kind) {
    switch (kind) {
        case SuggestedLocalAdjustmentKind::OpenShadows: return "Open shadows";
        case SuggestedLocalAdjustmentKind::ProtectSky: return "Protect sky";
        case SuggestedLocalAdjustmentKind::OpenBacklitSubject: return "Open backlit subject";
        case SuggestedLocalAdjustmentKind::RecoverHighlights: return "Protect display highlights";
        case SuggestedLocalAdjustmentKind::BrightenFoliage: return "Brighten foliage";
        default: return "Local adjustment";
    }
}

LocalSuggestionComponentReport AnalyzeLocalSuggestionComponents(
    const LocalSuggestionAnalysisImage& image,
    const Stack::RawAnalysis::RawImageAnalysis& analysis) {
    (void)analysis;
    LocalSuggestionComponentReport report;
    if (!ImageLooksUsable(image)) {
        report.statusMessage = image.statusMessage.empty()
            ? "Local suggestions need scene-linear RGB analysis before Local Range and View Transform."
            : image.statusMessage;
        return report;
    }

    std::vector<float> sortedEvs;
    std::vector<float> sortedLumas;
    int validCount = 0;
    const std::vector<PixelData> pixels =
        BuildPixelData(image, sortedEvs, sortedLumas, validCount);
    if (validCount <= 0 || sortedEvs.empty()) {
        report.statusMessage = "Local suggestion analysis found no valid scene-linear pixels.";
        return report;
    }

    const float p50Ev = PercentileSorted(sortedEvs, 0.50f);
    const float p95Ev = PercentileSorted(sortedEvs, 0.95f);
    std::vector<float> variances;
    variances.reserve(pixels.size());
    for (const PixelData& pixel : pixels) {
        if (pixel.valid) {
            variances.push_back(pixel.variance);
        }
    }
    std::sort(variances.begin(), variances.end());
    const float lowVar = PercentileSorted(variances, 0.25f, 0.00001f);
    const float highVar = std::max(lowVar + 0.00001f, PercentileSorted(variances, 0.85f, lowVar + 0.0001f));

    const std::vector<bool> skyMask = BuildSkyMask(
        pixels,
        image.width,
        image.height,
        validCount,
        p50Ev,
        p95Ev,
        lowVar,
        highVar);
    const ComponentSummary sky = SummarizeMask(pixels, skyMask, validCount);

    const std::vector<bool> foliageMask = BuildFoliageMask(
        pixels,
        skyMask,
        image.width,
        image.height,
        validCount,
        std::max(0.00001f, highVar * 0.12f));
    const ComponentSummary foliage = SummarizeMask(pixels, foliageMask, validCount);

    const std::vector<bool> shadowMask = BuildShadowMask(pixels, p50Ev - 2.0f);
    const ComponentSummary shadow = SummarizeMask(pixels, shadowMask, validCount);

    report.valid = true;
    report.validPixelPercent =
        100.0f * static_cast<float>(validCount) /
        static_cast<float>(image.width * image.height);
    report.skyAreaPercent = sky.areaPercent;
    report.skyMedianEv = sky.medianEv;
    report.skyP70Ev = sky.p70Ev;
    report.skyMedianR = sky.medianR;
    report.skyMedianG = sky.medianG;
    report.skyMedianB = sky.medianB;
    report.foliageAreaPercent = foliage.areaPercent;
    report.foliageMedianEv = foliage.medianEv;
    report.foliageMedianR = foliage.medianR;
    report.foliageMedianG = foliage.medianG;
    report.foliageMedianB = foliage.medianB;
    report.foliageChromaMedian = foliage.chromaMedian;
    report.shadowAreaPercent = shadow.areaPercent;
    report.shadowMedianEv = shadow.medianEv;
    report.shadowP25Ev = shadow.p25Ev;
    report.centerMedianEv = MedianEvForRegion(
        pixels,
        image.width,
        image.height,
        static_cast<int>(0.25f * image.width),
        static_cast<int>(0.35f * image.height),
        static_cast<int>(0.75f * image.width),
        static_cast<int>(0.85f * image.height),
        p50Ev);
    report.brightTopAreaPercent = BrightTopAreaPercent(
        pixels,
        image.width,
        image.height,
        validCount,
        p95Ev);
    const float brightBackgroundEv = sky.valid ? sky.medianEv : p95Ev;
    report.backlitContrastEv = brightBackgroundEv - report.centerMedianEv;
    report.statusMessage = "Local component analysis ready.";
    return report;
}

std::vector<SuggestedLocalAdjustment> BuildSuggestedLocalAdjustments(
    const Stack::RawAnalysis::RawImageAnalysis& analysis,
    const LocalSuggestionAnalysisImage& image,
    LocalSuggestionComponentReport* outReport) {
    LocalSuggestionComponentReport report = AnalyzeLocalSuggestionComponents(image, analysis);
    if (outReport) {
        *outReport = report;
    }
    std::vector<SuggestedLocalAdjustment> suggestions;
    if (!report.valid) {
        return suggestions;
    }

    const bool highlightRisk =
        analysis.highlight.anyChannelClipPercent > 0.05f ||
        analysis.highlight.partialClipColorRisk ||
        analysis.highlight.displayClipPercent > 1.0f ||
        analysis.highlight.hdrPixelPercent > 1.0f;

    const bool backlitBySky =
        report.skyAreaPercent > 10.0f &&
        report.backlitContrastEv > 2.5f;
    const bool backlitByBrightTop =
        report.brightTopAreaPercent > 15.0f &&
        report.backlitContrastEv > 3.0f;
    if (backlitBySky || backlitByBrightTop) {
        const float contrast = report.backlitContrastEv;
        const float delta = std::min(
            1.0f,
            Lerp(0.5f, 1.2f, Remap01(contrast, 2.5f, 5.0f)));
        AddSuggestion(
            suggestions,
            MakeSuggestion(
                SuggestedLocalAdjustmentKind::OpenBacklitSubject,
                report.centerMedianEv,
                delta,
                3.0f,
                0.75f,
                Remap01(contrast, 2.5f, 5.0f),
                report.skyAreaPercent > 0.0f ? report.skyAreaPercent : report.brightTopAreaPercent,
                "Open backlit subject",
                "Creates a broad Local Range lift around the dark center/foreground EV. RAW Exposure stays unchanged to protect bright background detail."));
    }

    if (report.skyAreaPercent > 8.0f && report.backlitContrastEv > 1.5f) {
        SuggestedLocalAdjustment sky = MakeSuggestion(
            SuggestedLocalAdjustmentKind::ProtectSky,
            report.skyP70Ev,
            -Lerp(0.3f, 0.8f, Remap01(report.skyAreaPercent, 10.0f, 40.0f)),
            1.5f,
            0.60f,
            std::min(
                Remap01(report.skyAreaPercent, 8.0f, 25.0f),
                Remap01(report.backlitContrastEv, 1.5f, 4.0f)),
            report.skyAreaPercent,
            "Protect sky",
            "Creates a color-qualified Local Range hold centered on the sky EV and blue/cyan scene color. This protects display highlights but does not reconstruct clipped RAW data.");
        sky.colorQualifierEnabled = true;
        sky.targetSceneR = report.skyMedianR;
        sky.targetSceneG = report.skyMedianG;
        sky.targetSceneB = report.skyMedianB;
        sky.colorWidth = 0.38f;
        sky.colorFeather = 0.45f;
        sky.neutralGuard = 0.03f;
        AddSuggestion(suggestions, sky);
    }

    const bool shadowSevere = report.shadowAreaPercent > 45.0f;
    const bool backlitShown = std::any_of(
        suggestions.begin(),
        suggestions.end(),
        [](const SuggestedLocalAdjustment& suggestion) {
            return suggestion.kind == SuggestedLocalAdjustmentKind::OpenBacklitSubject;
        });
    if (report.shadowAreaPercent > 20.0f && (!backlitShown || shadowSevere)) {
        const float baseDelta =
            Lerp(0.3f, 1.0f, Remap01(report.shadowAreaPercent, 20.0f, 60.0f));
        SuggestedLocalAdjustment shadow = MakeSuggestion(
            SuggestedLocalAdjustmentKind::OpenShadows,
            report.shadowMedianEv,
            highlightRisk ? std::min(baseDelta, 0.65f) : baseDelta,
            2.5f,
            0.70f,
            std::clamp(Remap01(report.shadowAreaPercent, 20.0f, 50.0f) - (highlightRisk ? 0.15f : 0.0f), 0.0f, 1.0f),
            report.shadowAreaPercent,
            "Open shadows",
            highlightRisk
                ? "Shadow lift suggested as a local adjustment because global RAW exposure has highlight risk."
                : "Creates a Local Range lift around the main shadow mass while keeping RAW Exposure unchanged.");
        AddSuggestion(suggestions, shadow);
    }

    if (report.foliageAreaPercent > 3.0f && report.foliageChromaMedian > 0.10f) {
        SuggestedLocalAdjustment foliage = MakeSuggestion(
            SuggestedLocalAdjustmentKind::BrightenFoliage,
            report.foliageMedianEv,
            Lerp(0.2f, 0.6f, Remap01(report.foliageAreaPercent, 4.0f, 25.0f)),
            1.5f,
            0.60f,
            std::min(
                Remap01(report.foliageAreaPercent, 3.0f, 15.0f),
                Remap01(report.foliageChromaMedian, 0.10f, 0.25f)),
            report.foliageAreaPercent,
            "Brighten foliage",
            "Creates a Local Range lift limited to the sampled green/yellow-green color range, so similarly bright sky pixels are less affected.");
        foliage.colorQualifierEnabled = true;
        foliage.targetSceneR = report.foliageMedianR;
        foliage.targetSceneG = report.foliageMedianG;
        foliage.targetSceneB = report.foliageMedianB;
        foliage.colorWidth = 0.26f;
        foliage.colorFeather = 0.35f;
        foliage.neutralGuard = 0.10f;
        AddSuggestion(suggestions, foliage);
    }

    if (analysis.highlight.displayClipPercent > 2.0f &&
        analysis.highlight.anyChannelClipPercent <= 0.10f &&
        !analysis.highlight.partialClipColorRisk) {
        AddSuggestion(
            suggestions,
            MakeSuggestion(
                SuggestedLocalAdjustmentKind::RecoverHighlights,
                std::max(report.skyP70Ev, report.centerMedianEv + 2.0f),
                -0.45f,
                1.4f,
                0.60f,
                Remap01(analysis.highlight.displayClipPercent, 2.0f, 8.0f),
                analysis.highlight.displayClipPercent,
                "Protect display highlights",
                "Adds a high-luma Local Range hold for recoverable display clipping. This does not claim to recover clipped RAW sensor data."));
    }

    std::sort(suggestions.begin(), suggestions.end(), [](const SuggestedLocalAdjustment& a, const SuggestedLocalAdjustment& b) {
        const int priorityA = SuggestionPriority(a.kind);
        const int priorityB = SuggestionPriority(b.kind);
        if (priorityA != priorityB) {
            return priorityA < priorityB;
        }
        return a.confidence > b.confidence;
    });

    if (suggestions.size() > 4) {
        suggestions.resize(4);
    }
    return suggestions;
}

bool ApplySuggestedLocalAdjustment(
    const SuggestedLocalAdjustment& suggestion,
    Stack::RawRecipe::RawDevelopmentRecipe& recipe) {
    if (!suggestion.valid ||
        !std::isfinite(suggestion.targetEv) ||
        !std::isfinite(suggestion.deltaEv)) {
        return false;
    }

    Stack::RawRecipe::RawLocalRangeRecipe localRange =
        Stack::RawRecipe::SanitizeLocalRangeRecipe(recipe.localRange);
    const float targetEv = std::clamp(
        suggestion.targetEv,
        localRange.minEv + 0.001f,
        localRange.maxEv - 0.001f);
    const float targetDelta = std::clamp(suggestion.deltaEv, -4.0f, 4.0f);

    for (std::size_t i = 1; i + 1 < localRange.points.size(); ++i) {
        if (std::abs(localRange.points[i].ev - targetEv) <= 0.25f) {
            return false;
        }
    }
    if (localRange.points.size() >= 12) {
        return false;
    }

    localRange.enabled = true;
    localRange.strength = std::max(localRange.strength, 1.0f);
    localRange.smoothness = std::max(localRange.smoothness, suggestion.feather);
    if (suggestion.protectHighlights) {
        localRange.highlightProtection = std::max(localRange.highlightProtection, 0.65f);
        localRange.edgeProtection = std::max(localRange.edgeProtection, 0.78f);
        localRange.detailProtection = std::max(localRange.detailProtection, 0.80f);
    }
    localRange.points.push_back({ targetEv, targetDelta });

    const float halfWidth = std::max(0.35f, suggestion.widthEv * 0.5f);
    if (localRange.points.size() < 12) {
        localRange.points.push_back({
            std::clamp(targetEv - halfWidth, localRange.minEv, localRange.maxEv),
            0.0f
        });
    }
    if (localRange.points.size() < 12) {
        localRange.points.push_back({
            std::clamp(targetEv + halfWidth, localRange.minEv, localRange.maxEv),
            0.0f
        });
    }

    if (suggestion.colorQualifierEnabled) {
        localRange.colorMaskEnabled = true;
        localRange.colorMaskTargetR = std::clamp(suggestion.targetSceneR, 0.0f, 32.0f);
        localRange.colorMaskTargetG = std::clamp(suggestion.targetSceneG, 0.0f, 32.0f);
        localRange.colorMaskTargetB = std::clamp(suggestion.targetSceneB, 0.0f, 32.0f);
        localRange.colorMaskHueWidth = std::clamp(suggestion.colorWidth, 0.02f, 1.20f);
        localRange.colorMaskFeather = std::clamp(suggestion.colorFeather, 0.0f, 1.0f);
        localRange.colorMaskMinChroma = std::clamp(suggestion.neutralGuard, 0.0f, 1.0f);
    }

    recipe.localRange = Stack::RawRecipe::SanitizeLocalRangeRecipe(localRange);
    return true;
}

} // namespace Stack::RawAutoBase
