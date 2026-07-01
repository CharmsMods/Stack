#include "Renderer/RenderPipeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

float Clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float SafeLog2(float value) {
    return std::log2(std::max(value, 0.000001f));
}

struct HdrMergeFeatureImage {
    int width = 0;
    int height = 0;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float clipRatio = 0.0f;
    float blackRatio = 0.0f;
    float detailEnergy = 0.0f;
    std::vector<float> values;
    std::vector<std::uint8_t> thresholdBits;
    std::vector<std::uint8_t> excludeBits;
};

struct HdrMergeAlignmentAnalysis {
    bool valid = false;
    int referenceIndex = 0;
    std::array<float, 3> offsetX { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> offsetY { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> confidence { 1.0f, 1.0f, 1.0f };
};

float MedianFloat(std::vector<float> values) {
    if (values.empty()) {
        return 0.0f;
    }
    std::sort(values.begin(), values.end());
    const std::size_t mid = values.size() / 2;
    if ((values.size() & 1u) != 0u) {
        return values[mid];
    }
    return 0.5f * (values[mid - 1] + values[mid]);
}

bool ReadTextureToFloatRgba(unsigned int texture, int width, int height, std::vector<float>& outPixels) {
    outPixels.clear();
    if (!texture || width <= 0 || height <= 0) {
        return false;
    }

    const unsigned int fbo = GLHelpers::CreateFBO(texture);
    if (!fbo) {
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    outPixels.assign(static_cast<std::size_t>(width * height * 4), 0.0f);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_FLOAT, outPixels.data());
    const GLenum readError = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    return readError == GL_NO_ERROR;
}

HdrMergeFeatureImage BuildHdrMergeFeatureImage(
    const std::vector<float>& rgbaPixels,
    int sourceWidth,
    int sourceHeight,
    float exposureEv,
    float clipThreshold,
    float blackThreshold,
    int maxDimension) {
    HdrMergeFeatureImage feature;
    if (rgbaPixels.empty() || sourceWidth <= 0 || sourceHeight <= 0) {
        return feature;
    }

    const float scale = std::min(1.0f, static_cast<float>(std::max(32, maxDimension)) / static_cast<float>(std::max(sourceWidth, sourceHeight)));
    feature.width = std::max(24, static_cast<int>(std::round(sourceWidth * scale)));
    feature.height = std::max(24, static_cast<int>(std::round(sourceHeight * scale)));
    feature.scaleX = static_cast<float>(sourceWidth) / static_cast<float>(feature.width);
    feature.scaleY = static_cast<float>(sourceHeight) / static_cast<float>(feature.height);

    std::vector<float> logLuma(static_cast<std::size_t>(feature.width * feature.height), 0.0f);
    std::vector<float> sourceLuma(static_cast<std::size_t>(feature.width * feature.height), 0.0f);
    feature.values.assign(static_cast<std::size_t>(feature.width * feature.height), 0.0f);
    feature.thresholdBits.assign(static_cast<std::size_t>(feature.width * feature.height), 0u);
    feature.excludeBits.assign(static_cast<std::size_t>(feature.width * feature.height), 0u);
    float clippedCount = 0.0f;
    float blackCount = 0.0f;
    const float exposureScale = std::exp2(-exposureEv);

    for (int y = 0; y < feature.height; ++y) {
        const int srcY = std::clamp(static_cast<int>(std::round((static_cast<float>(y) + 0.5f) * feature.scaleY - 0.5f)), 0, sourceHeight - 1);
        for (int x = 0; x < feature.width; ++x) {
            const int srcX = std::clamp(static_cast<int>(std::round((static_cast<float>(x) + 0.5f) * feature.scaleX - 0.5f)), 0, sourceWidth - 1);
            const std::size_t srcIndex = static_cast<std::size_t>((srcY * sourceWidth + srcX) * 4);
            const float r = std::max(0.0f, rgbaPixels[srcIndex + 0]);
            const float g = std::max(0.0f, rgbaPixels[srcIndex + 1]);
            const float b = std::max(0.0f, rgbaPixels[srcIndex + 2]);
            const float sourceMax = std::max(r, std::max(g, b));
            const float sceneLuma = std::max(0.0f, 0.2126f * r + 0.7152f * g + 0.0722f * b);
            const float normalizedLuma = std::max(0.0f, (0.2126f * r + 0.7152f * g + 0.0722f * b) * exposureScale);
            const std::size_t index = static_cast<std::size_t>(y * feature.width + x);
            sourceLuma[index] = sceneLuma;
            logLuma[index] = SafeLog2(normalizedLuma + 0.00003f);
            clippedCount += sourceMax >= clipThreshold ? 1.0f : 0.0f;
            blackCount += normalizedLuma <= blackThreshold ? 1.0f : 0.0f;
        }
    }

    const float pixelCount = static_cast<float>(feature.width * feature.height);
    feature.clipRatio = pixelCount > 0.0f ? clippedCount / pixelCount : 0.0f;
    feature.blackRatio = pixelCount > 0.0f ? blackCount / pixelCount : 0.0f;

    const float lumaMedian = MedianFloat(sourceLuma);
    const float excludeRange = std::max(lumaMedian * 0.08f, 0.0015f);
    for (std::size_t index = 0; index < sourceLuma.size(); ++index) {
        feature.thresholdBits[index] = sourceLuma[index] >= lumaMedian ? 1u : 0u;
        feature.excludeBits[index] = std::abs(sourceLuma[index] - lumaMedian) <= excludeRange ? 1u : 0u;
    }

    float detailEnergySum = 0.0f;
    for (int y = 0; y < feature.height; ++y) {
        for (int x = 0; x < feature.width; ++x) {
            float sum = 0.0f;
            float count = 0.0f;
            for (int oy = -1; oy <= 1; ++oy) {
                for (int ox = -1; ox <= 1; ++ox) {
                    const int sx = std::clamp(x + ox, 0, feature.width - 1);
                    const int sy = std::clamp(y + oy, 0, feature.height - 1);
                    sum += logLuma[static_cast<std::size_t>(sy * feature.width + sx)];
                    count += 1.0f;
                }
            }
            const std::size_t index = static_cast<std::size_t>(y * feature.width + x);
            feature.values[index] = logLuma[index] - (count > 0.0f ? sum / count : 0.0f);
            detailEnergySum += std::abs(feature.values[index]);
        }
    }
    feature.detailEnergy = pixelCount > 0.0f ? detailEnergySum / pixelCount : 0.0f;

    return feature;
}

float HdrMergeTranslationScore(
    const HdrMergeFeatureImage& reference,
    const HdrMergeFeatureImage& candidate,
    int shiftX,
    int shiftY) {
    if (reference.width != candidate.width || reference.height != candidate.height ||
        reference.values.empty() || candidate.values.empty()) {
        return std::numeric_limits<float>::infinity();
    }

    const int xStart = std::max(0, -shiftX);
    const int yStart = std::max(0, -shiftY);
    const int xEnd = std::min(reference.width, reference.width - shiftX);
    const int yEnd = std::min(reference.height, reference.height - shiftY);
    if (xEnd - xStart < reference.width / 2 || yEnd - yStart < reference.height / 2) {
        return std::numeric_limits<float>::infinity();
    }

    float sum = 0.0f;
    float count = 0.0f;
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            const std::size_t refIndex = static_cast<std::size_t>(y * reference.width + x);
            const std::size_t candIndex = static_cast<std::size_t>((y + shiftY) * candidate.width + (x + shiftX));
            sum += std::abs(reference.values[refIndex] - candidate.values[candIndex]);
            count += 1.0f;
        }
    }
    return count > 0.0f ? sum / count : std::numeric_limits<float>::infinity();
}

float HdrMergeThresholdBitmapScore(
    const HdrMergeFeatureImage& reference,
    const HdrMergeFeatureImage& candidate,
    int shiftX,
    int shiftY) {
    if (reference.width != candidate.width || reference.height != candidate.height ||
        reference.thresholdBits.empty() || candidate.thresholdBits.empty() ||
        reference.excludeBits.empty() || candidate.excludeBits.empty()) {
        return std::numeric_limits<float>::infinity();
    }

    const int xStart = std::max(0, -shiftX);
    const int yStart = std::max(0, -shiftY);
    const int xEnd = std::min(reference.width, reference.width - shiftX);
    const int yEnd = std::min(reference.height, reference.height - shiftY);
    if (xEnd - xStart < reference.width / 2 || yEnd - yStart < reference.height / 2) {
        return std::numeric_limits<float>::infinity();
    }

    float mismatchCount = 0.0f;
    float validCount = 0.0f;
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            const std::size_t refIndex = static_cast<std::size_t>(y * reference.width + x);
            const std::size_t candIndex = static_cast<std::size_t>((y + shiftY) * candidate.width + (x + shiftX));
            if (reference.excludeBits[refIndex] != 0u || candidate.excludeBits[candIndex] != 0u) {
                continue;
            }
            mismatchCount += reference.thresholdBits[refIndex] != candidate.thresholdBits[candIndex] ? 1.0f : 0.0f;
            validCount += 1.0f;
        }
    }

    const float minimumValid = static_cast<float>(reference.width * reference.height) * 0.25f;
    if (validCount < minimumValid) {
        return std::numeric_limits<float>::infinity();
    }
    return mismatchCount / validCount;
}

HdrMergeAlignmentAnalysis AnalyzeHdrMergeAlignment(
    const std::array<unsigned int, 3>& textures,
    const std::array<bool, 3>& activeInputs,
    int width,
    int height,
    const Raw::HdrMergeSettings& settings,
    const std::array<float, 3>& effectiveExposureEv,
    const std::array<float, 3>& referenceExposureDistance,
    const std::array<float, 3>& clipThreshold,
    const std::array<float, 3>& blackThreshold) {
    HdrMergeAlignmentAnalysis analysis;
    analysis.valid = true;

    const int activeCount = static_cast<int>(activeInputs[0]) + static_cast<int>(activeInputs[1]) + static_cast<int>(activeInputs[2]);
    if (!activeInputs[0] || !activeInputs[1] || activeCount < 2) {
        analysis.valid = false;
        return analysis;
    }

    int requestedReference = 0;
    switch (settings.referenceMode) {
        case Raw::HdrMergeReferenceMode::Frame2: requestedReference = 1; break;
        case Raw::HdrMergeReferenceMode::Frame3: requestedReference = 2; break;
        case Raw::HdrMergeReferenceMode::Frame1:
        case Raw::HdrMergeReferenceMode::Auto:
        default:
            requestedReference = 0;
            break;
    }
    if (settings.referenceMode != Raw::HdrMergeReferenceMode::Auto && activeInputs[requestedReference]) {
        analysis.referenceIndex = requestedReference;
    }

    std::array<HdrMergeFeatureImage, 3> features {};
    bool gatheredAnyFeature = false;
    if (settings.alignmentMode != Raw::HdrMergeAlignmentMode::Off ||
        settings.referenceMode == Raw::HdrMergeReferenceMode::Auto) {
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i] || textures[i] == 0) {
                continue;
            }
            std::vector<float> rgbaPixels;
            if (!ReadTextureToFloatRgba(textures[i], width, height, rgbaPixels)) {
                continue;
            }
            features[i] = BuildHdrMergeFeatureImage(
                rgbaPixels,
                width,
                height,
                effectiveExposureEv[i],
                clipThreshold[i],
                blackThreshold[i],
                settings.alignmentMode == Raw::HdrMergeAlignmentMode::WideTranslation ? 192 : 160);
            gatheredAnyFeature = gatheredAnyFeature || !features[i].values.empty();
        }
    }

    if (settings.referenceMode == Raw::HdrMergeReferenceMode::Auto && gatheredAnyFeature) {
        float minExposureEv = std::numeric_limits<float>::infinity();
        float maxExposureEv = -std::numeric_limits<float>::infinity();
        float bestClipRatio = std::numeric_limits<float>::infinity();
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i] || features[i].values.empty()) {
                continue;
            }
            minExposureEv = std::min(minExposureEv, effectiveExposureEv[i]);
            maxExposureEv = std::max(maxExposureEv, effectiveExposureEv[i]);
            bestClipRatio = std::min(bestClipRatio, features[i].clipRatio);
        }
        const bool sameExposureBurst =
            std::isfinite(minExposureEv) &&
            std::isfinite(maxExposureEv) &&
            (maxExposureEv - minExposureEv) <= 0.35f;
        float bestScore = std::numeric_limits<float>::infinity();
        int bestIndex = 0;
        const float centerIndex = (activeInputs[0] && activeInputs[1] && activeInputs[2]) ? 1.0f : 0.5f;
        for (int i = 0; i < 3; ++i) {
            if (!activeInputs[i] || features[i].values.empty()) {
                continue;
            }
            float score = 0.0f;
            if (sameExposureBurst) {
                score =
                    -features[i].detailEnergy +
                    features[i].clipRatio * 0.12f +
                    features[i].blackRatio * 0.10f +
                    std::abs(static_cast<float>(i) - centerIndex) * 0.03f;
            } else {
                const float extraClipPenalty = std::max(0.0f, features[i].clipRatio - (bestClipRatio + 0.02f));
                const float shadowPenalty = std::max(0.0f, features[i].blackRatio - 0.45f);
                score =
                    extraClipPenalty * 4.0f +
                    features[i].clipRatio * 0.60f +
                    shadowPenalty * 0.80f +
                    effectiveExposureEv[i] * 0.14f +
                    referenceExposureDistance[i] * 0.01f -
                    features[i].detailEnergy * 0.08f;
            }
            if (score < bestScore) {
                bestScore = score;
                bestIndex = i;
            }
        }
        analysis.referenceIndex = bestIndex;
    }

    if (settings.alignmentMode == Raw::HdrMergeAlignmentMode::Off || !gatheredAnyFeature) {
        for (int i = 0; i < 3; ++i) {
            analysis.confidence[i] = activeInputs[i] ? 1.0f : 0.0f;
        }
        return analysis;
    }

    const HdrMergeFeatureImage& reference = features[analysis.referenceIndex];
    if (reference.values.empty()) {
        return analysis;
    }
    const int maxShift = settings.alignmentMode == Raw::HdrMergeAlignmentMode::WideTranslation ? 20 : 12;
    float minActiveExposureEv = std::numeric_limits<float>::infinity();
    float maxActiveExposureEv = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            continue;
        }
        minActiveExposureEv = std::min(minActiveExposureEv, effectiveExposureEv[i]);
        maxActiveExposureEv = std::max(maxActiveExposureEv, effectiveExposureEv[i]);
    }
    const bool sameExposureBurst =
        std::isfinite(minActiveExposureEv) &&
        std::isfinite(maxActiveExposureEv) &&
        (maxActiveExposureEv - minActiveExposureEv) <= 0.35f;
    for (int i = 0; i < 3; ++i) {
        if (!activeInputs[i]) {
            analysis.confidence[i] = 0.0f;
            continue;
        }
        if (i == analysis.referenceIndex) {
            analysis.confidence[i] = 1.0f;
            continue;
        }
        const HdrMergeFeatureImage& candidate = features[i];
        if (candidate.values.empty()) {
            analysis.confidence[i] = 0.0f;
            continue;
        }

        float bestDetailScore = std::numeric_limits<float>::infinity();
        float bestThresholdScore = std::numeric_limits<float>::infinity();
        int bestShiftX = 0;
        int bestShiftY = 0;
        for (int dy = -maxShift; dy <= maxShift; ++dy) {
            for (int dx = -maxShift; dx <= maxShift; ++dx) {
                const float detailScore = HdrMergeTranslationScore(reference, candidate, dx, dy);
                const float thresholdScore = HdrMergeThresholdBitmapScore(reference, candidate, dx, dy);

                bool better = false;
                if (sameExposureBurst) {
                    if (detailScore < bestDetailScore - 0.0001f) {
                        better = true;
                    } else if (std::abs(detailScore - bestDetailScore) <= 0.0001f &&
                               thresholdScore < bestThresholdScore) {
                        better = true;
                    }
                } else {
                    if (thresholdScore < bestThresholdScore - 0.0025f) {
                        better = true;
                    } else if (std::abs(thresholdScore - bestThresholdScore) <= 0.0025f &&
                               detailScore < bestDetailScore) {
                        better = true;
                    }
                }

                if (better) {
                    bestDetailScore = detailScore;
                    bestThresholdScore = thresholdScore;
                    bestShiftX = dx;
                    bestShiftY = dy;
                }
            }
        }

        const float zeroDetailScore = HdrMergeTranslationScore(reference, candidate, 0, 0);
        const float zeroThresholdScore = HdrMergeThresholdBitmapScore(reference, candidate, 0, 0);
        const float scaleX = candidate.scaleX;
        const float scaleY = candidate.scaleY;
        analysis.offsetX[i] = -static_cast<float>(bestShiftX) * scaleX;
        analysis.offsetY[i] = -static_cast<float>(bestShiftY) * scaleY;
        const float detailGain = (std::isfinite(zeroDetailScore) && zeroDetailScore > 0.00001f &&
                                  std::isfinite(bestDetailScore))
            ? std::max(0.0f, (zeroDetailScore - bestDetailScore) / zeroDetailScore)
            : 0.0f;
        const float thresholdGain = (std::isfinite(zeroThresholdScore) && zeroThresholdScore > 0.00001f &&
                                     std::isfinite(bestThresholdScore))
            ? std::max(0.0f, (zeroThresholdScore - bestThresholdScore) / zeroThresholdScore)
            : 0.0f;
        const float relativeGain = sameExposureBurst
            ? (detailGain * 0.68f + thresholdGain * 0.32f)
            : (thresholdGain * 0.72f + detailGain * 0.28f);
        const float shiftPenalty = 1.0f - std::min(1.0f, static_cast<float>(std::abs(bestShiftX) + std::abs(bestShiftY)) / static_cast<float>(maxShift * 2));
        analysis.confidence[i] = Clamp01(relativeGain * 0.7f + shiftPenalty * 0.3f);
    }

    return analysis;
}



} // namespace


bool RenderPipeline::RenderHdrMerge(
    unsigned int texture1,
    unsigned int texture2,
    unsigned int texture3,
    bool hasTexture2,
    bool hasTexture3,
    const Raw::HdrMergeSettings& settings,
    const HdrMergeResolvedSettings& resolved,
    unsigned int targetFBO) {
    EnsureHdrMergeProgram();
    if (!m_HdrMergeProgram || !texture1) {
        return false;
    }

    const bool useTexture2 = hasTexture2 && texture2 != 0;
    const bool useTexture3 = hasTexture3 && texture3 != 0;
    if (!useTexture2) {
        return false;
    }

    const auto queryTextureSize = [](unsigned int texture, int& outWidth, int& outHeight) {
        outWidth = 0;
        outHeight = 0;
        if (!texture) {
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, texture);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &outWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &outHeight);
        return outWidth > 0 && outHeight > 0;
    };

    int width1 = 0;
    int height1 = 0;
    int width2 = 0;
    int height2 = 0;
    int width3 = 0;
    int height3 = 0;
    if (!queryTextureSize(texture1, width1, height1) ||
        !queryTextureSize(texture2, width2, height2) ||
        (useTexture3 && !queryTextureSize(texture3, width3, height3))) {
        return false;
    }
    if (width2 != width1 || height2 != height1 ||
        (useTexture3 && (width3 != width1 || height3 != height1))) {
        return false;
    }

    std::array<unsigned int, 3> textures { texture1, texture2, texture3 };
    std::array<bool, 3> activeInputs { true, true, useTexture3 };
    const HdrMergeAlignmentAnalysis alignment = AnalyzeHdrMergeAlignment(
        textures,
        activeInputs,
        width1,
        height1,
        settings,
        resolved.exposureEv,
        resolved.referenceExposureDistance,
        resolved.clipThreshold,
        resolved.blackThreshold);
    float deghostStrength = 0.0f;
    switch (settings.deghostMode) {
        case Raw::HdrMergeDeghostMode::Low: deghostStrength = 0.35f; break;
        case Raw::HdrMergeDeghostMode::Medium: deghostStrength = 0.65f; break;
        case Raw::HdrMergeDeghostMode::High: deghostStrength = 0.90f; break;
        case Raw::HdrMergeDeghostMode::Off:
        default:
            deghostStrength = 0.0f;
            break;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_HdrMergeProgram);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture1);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uInput1"), 0);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, useTexture2 ? texture2 : texture1);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uInput2"), 1);

    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, useTexture3 ? texture3 : texture1);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uInput3"), 2);

    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uHasInput2"), useTexture2 ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uHasInput3"), useTexture3 ? 1 : 0);
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uTexelSize"), 1.0f / static_cast<float>(width1), 1.0f / static_cast<float>(height1));
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uInputOffsetPx[0]"), alignment.offsetX[0], alignment.offsetY[0]);
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uInputOffsetPx[1]"), alignment.offsetX[1], alignment.offsetY[1]);
    glUniform2f(glGetUniformLocation(m_HdrMergeProgram, "uInputOffsetPx[2]"), alignment.offsetX[2], alignment.offsetY[2]);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uReferenceIndex"), std::clamp(alignment.referenceIndex, 0, useTexture3 ? 2 : 1));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uAlignmentConfidence"), alignment.confidence[0], alignment.confidence[1], alignment.confidence[2]);
    glUniform1f(glGetUniformLocation(m_HdrMergeProgram, "uDeghostStrength"), deghostStrength);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uMotionPriority"),
        settings.motionPriority == Raw::HdrMergeMotionPriority::PreserveReference ? 0 : 1);
    glUniform3f(
        glGetUniformLocation(m_HdrMergeProgram, "uExposureEv"),
        resolved.exposureEv[0],
        resolved.exposureEv[1],
        resolved.exposureEv[2]);
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uClipThreshold"),
        std::max(0.0f, resolved.clipThreshold[0]),
        std::max(0.0f, resolved.clipThreshold[1]),
        std::max(0.0f, resolved.clipThreshold[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uClipFeather"),
        std::max(0.0001f, resolved.clipFeather[0]),
        std::max(0.0001f, resolved.clipFeather[1]),
        std::max(0.0001f, resolved.clipFeather[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uBlackThreshold"),
        std::max(0.0f, resolved.blackThreshold[0]),
        std::max(0.0f, resolved.blackThreshold[1]),
        std::max(0.0f, resolved.blackThreshold[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uBlackFeather"),
        std::max(0.0001f, resolved.blackFeather[0]),
        std::max(0.0001f, resolved.blackFeather[1]),
        std::max(0.0001f, resolved.blackFeather[2]));
    glUniform3f(glGetUniformLocation(m_HdrMergeProgram, "uReadNoise"),
        std::max(0.0f, resolved.readNoise[0]),
        std::max(0.0f, resolved.readNoise[1]),
        std::max(0.0f, resolved.readNoise[2]));
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uNoiseAware"), settings.noiseAware ? 1 : 0);
    glUniform1i(glGetUniformLocation(m_HdrMergeProgram, "uDebugView"), static_cast<int>(settings.debugView));

    m_Quad.Draw();
    glActiveTexture(GL_TEXTURE0);
    return true;
}
